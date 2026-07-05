#include "RemoteEngine.h"

#include "Presentation.h"
#include "ipc/Protocol.h"
#include "ipc/Serialize.h"

#include <chrono>
#include <cstdio>
#include <cstring>

namespace {

double steadySeconds()
{
    using namespace std::chrono;
    return duration_cast<duration<double>>(steady_clock::now().time_since_epoch()).count();
}

template <typename T>
bool parse(const ipc::Message &m, T &out)
{
    if (m.payload.size() < sizeof(T))
        return false;
    std::memcpy(&out, m.payload.data(), sizeof(T));
    return true;
}

} // namespace

RemoteEngine::RemoteEngine(std::string worker_exe, Settings settings)
    : settings_(settings), proc_(std::move(worker_exe)), t0_(steadySeconds())
{
    logic_.start(0.0);
}

RemoteEngine::~RemoteEngine() = default;

double RemoteEngine::now() const
{
    return steadySeconds() - t0_;
}

int RemoteEngine::slideIndexOf(const SourceFile &sf)
{
    std::string name = sf.path.filename().string();
    if (name.rfind("slide", 0) == 0)
        return std::atoi(name.c_str() + 5);
    return -1; // setup.cpp
}

void RemoteEngine::compileSetup(SourceFile &setup)
{
    double t = now();
    pump(t);
    submitIfNeeded(setup, t);
}

void RemoteEngine::compileSlide(SourceFile &slide)
{
    submitIfNeeded(slide, now());
}

void RemoteEngine::dump(const char *what, const char *filter)
{
    (void)filter;
    fprintf(stderr, "RemoteEngine: interpreter dump ('%s') not supported yet\n", what);
}

void RemoteEngine::pump(double t)
{
    if (proc_.running() && proc_.reap()) {
        killing_ = false;
        logic_.onWorkerExit(t);
        applyPoison(logic_.takePoisonedSlide());
    }

    if (killing_ && proc_.running() && t >= kill_deadline_) {
        proc_.kill(); // SIGTERM grace expired
        kill_deadline_ = t + 60.0; // don't spam; waitpid picks it up
    }

    if (proc_.running()) {
        ipc::Message m;
        while (proc_.channel().recv(m) == ipc::Channel::RecvResult::Message)
            handleMessage(m, t);
    }

    switch (logic_.update(t)) {
    case supervisor::Action::Spawn:
        if (proc_.spawn()) {
            logic_.onSpawned(t);
            ipc::HelloMsg hello{ipc::kProtocolVersion, ipc::kTransportShm,
                                settings_.dpi_scale, settings_.design_w, settings_.design_h};
            proc_.channel().send(ipc::MsgType::Hello, &hello, sizeof(hello));
            // Everything previously submitted died with the old interpreter;
            // resubmit on the next visit (poisoned files keep syntax_error
            // until edited, which blocks resubmission).
            for (auto &[idx, sf] : known_) {
                if (sf && !sf->syntax_error) {
                    sf->compiled = false;
                    sf->validated = false;
                    sf->compile_in_flight = false;
                }
            }
        } else {
            // Treat a failed spawn as an instant crash: backoff applies and
            // a persistent failure hits the crash-storm cutoff.
            logic_.onSpawned(t);
            logic_.onWorkerExit(t);
        }
        break;
    case supervisor::Action::Kill:
        proc_.terminate();
        killing_ = true;
        kill_deadline_ = t + 0.5;
        break;
    case supervisor::Action::None:
        break;
    }

    if (proc_.running() && logic_.shouldPing(t)) {
        ipc::PingMsg ping{next_request_id_++};
        if (proc_.channel().send(ipc::MsgType::Ping, &ping, sizeof(ping)))
            logic_.onPingSent(t);
    }
}

void RemoteEngine::handleMessage(ipc::Message &m, double t)
{
    switch (m.header.type) {
    case ipc::MsgType::HelloAck: {
        ipc::HelloAckMsg ack{};
        if (parse(m, ack)) {
            if (ack.protocol_version != ipc::kProtocolVersion)
                fprintf(stderr,
                        "marcel_worker protocol mismatch (worker %u, main %u) — "
                        "worker binary out of date?\n",
                        ack.protocol_version, ipc::kProtocolVersion);
            logic_.onHandshake(t);
        }
        break;
    }
    case ipc::MsgType::Pong:
        logic_.onPong(t);
        break;
    case ipc::MsgType::CompileBusy: {
        ipc::CompileBusyMsg busy{};
        if (parse(m, busy))
            logic_.onCompileBusy(busy.slide, t);
        break;
    }
    case ipc::MsgType::CompileResult: {
        ipc::Reader r(m.payload.data(), m.payload.size());
        ipc::CompileResultMsg res{};
        if (!r.read(res))
            break;
        logic_.onCompileResult(res.slide, t);

        auto it = known_.find(res.slide);
        SourceFile *sf = it != known_.end() ? it->second : nullptr;
        if (!sf || sf->compile_request_id != res.request_id)
            break; // stale result (source changed since submission)

        std::string value, exception, stderr_text;
        r.readString(value, res.value_len);
        r.readString(exception, res.exception_len);
        r.readString(stderr_text, res.stderr_len);
        sf->error_markers.clear();
        for (uint32_t i = 0; i < res.num_markers && r.ok(); ++i) {
            ipc::ErrorMarkerWire mw{};
            std::string text;
            if (r.read(mw) && r.readString(text, mw.text_len))
                sf->error_markers[static_cast<int>(mw.line)] = text;
        }
        if (!r.ok())
            break;

        sf->compile_in_flight = false;
        sf->validated = res.validated;
        sf->compiled = res.compiled;
        sf->syntax_error = res.syntax_error;
        sf->value = value;
        sf->exception = exception;
        // The compiled lambda lives in the worker; it cannot cross the
        // process boundary. Remote rendering (step 3b) replaces this.
        sf->function = nullptr;
        break;
    }
    case ipc::MsgType::LogText:
        fwrite(m.payload.data(), 1, m.payload.size(), stderr);
        break;
    default:
        break;
    }
}

void RemoteEngine::submitIfNeeded(SourceFile &sf, double)
{
    int idx = slideIndexOf(sf);
    known_[idx] = &sf;

    // Same gate as the in-process engine: a failed compile (or a poisoned
    // file) stays put until the user edits, which resets the flags.
    if (sf.compiled || sf.syntax_error || sf.compile_in_flight)
        return;
    if (!proc_.running() || !logic_.running())
        return;

    ipc::SetSourceMsg msg{};
    msg.slide = idx;
    msg.request_id = next_request_id_++;
    msg.is_cuda = sf.is_cuda ? 1 : 0;
    std::string text = sf.text();
    msg.text_len = static_cast<uint32_t>(text.size());

    ipc::Writer w;
    w.append(msg);
    w.appendString(text);
    if (w.size() > ipc::kMaxPayloadSize)
        return; // absurdly large source; leave it un-submitted
    if (proc_.channel().send(ipc::MsgType::SetSource, w.data(), w.size())) {
        sf.compile_request_id = msg.request_id;
        sf.compile_in_flight = true;
    }
}

void RemoteEngine::applyPoison(int slide)
{
    if (slide == supervisor::kNoSlide)
        return;
    auto it = known_.find(slide);
    if (it == known_.end() || !it->second)
        return;
    SourceFile &sf = *it->second;
    sf.compile_in_flight = false;
    sf.validated = true;
    sf.compiled = true;
    sf.syntax_error = true; // blocks resubmission until the user edits
    sf.function = nullptr;
    sf.error_markers.clear();
    sf.error_markers[1] = "This code crashed the interpreter (worker restarted)";
}
