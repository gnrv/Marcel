#include "RemoteEngine.h"

#include "Presentation.h"
#include "ipc/Protocol.h"
#include "ipc/Serialize.h"
#include "supervisor/TextureImport.h"

#include "imgui.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>

#include <GL/gl.h>
#include <sys/mman.h>
#include <unistd.h>

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

RemoteEngine::~RemoteEngine()
{
    // GL textures are left to die with the context; the mappings are ours.
    for (auto &[slide, stream] : streams_) {
        for (auto &mp : stream.maps) {
            if (mp.ptr)
                munmap(mp.ptr, mp.size);
            if (mp.fd >= 0)
                close(mp.fd);
        }
    }
}

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

void RemoteEngine::noteStderr(const std::string &text)
{
    constexpr size_t kTailMax = 64 * 1024;
    stderr_tail_ += text;
    if (stderr_tail_.size() > kTailMax)
        stderr_tail_.erase(0, stderr_tail_.size() - kTailMax);
}

void RemoteEngine::drainWorkerStderr()
{
    std::string chunk;
    if (proc_.drainStderr(chunk)) {
        fwrite(chunk.data(), 1, chunk.size(), stderr); // live echo
        noteStderr(chunk);
    }
}

void RemoteEngine::pump(double t)
{
    // New UI frame: SlideView re-reports every visible slide before endFrame.
    frame_inputs_.clear();

    drainWorkerStderr();
    if (proc_.running() && proc_.reap()) {
        drainWorkerStderr(); // last words, before the exit marker
        std::string note = "[worker " + proc_.exitDescription() + "]\n";
        fputs(note.c_str(), stderr);
        noteStderr(note);
        killing_ = false;
        frame_outstanding_ = false; // died mid-frame; textures keep the last image
        // The next worker generation starts with all-free rings; releases
        // aimed at the dead one are meaningless.
        release_after_swap_.clear();
        for (auto &[slide, stream] : streams_)
            stream.front = -1;
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
            ipc::HelloMsg hello{ipc::kProtocolVersion, transport_caps_,
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
    case ipc::MsgType::TextureAnnounce:
        handleTextureAnnounce(m);
        break;
    case ipc::MsgType::FrameDone:
        handleFrameDone(m, t);
        break;
    case ipc::MsgType::LogText:
        fwrite(m.payload.data(), 1, m.payload.size(), stderr);
        break;
    case ipc::MsgType::ClipboardRequest:
        pushClipboard();
        break;
    case ipc::MsgType::ClipboardData: {
        // Slide code copied text; publish it as the real clipboard.
        ipc::Reader r(m.payload.data(), m.payload.size());
        ipc::ClipboardDataMsg msg{};
        std::string text;
        if (r.read(msg) && r.readString(text, msg.text_len) &&
            ImGui::GetCurrentContext())
            ImGui::SetClipboardText(text.c_str());
        break;
    }
    default:
        break;
    }
}

void RemoteEngine::handleTextureAnnounce(ipc::Message &m)
{
    ipc::TextureAnnounceMsg ann{};
    if (!parse(m, ann) || ann.buffer_index >= ipc::kBuffersPerSlide) {
        // Message's dtor closes any fds we don't take.
        return;
    }

    if (ann.transport == ipc::kTransportDmabuf) {
        int fds[ipc::kMaxFds] = {-1, -1, -1, -1};
        uint32_t n = std::min(ann.num_planes, ipc::kMaxFds);
        for (uint32_t i = 0; i < n; ++i)
            fds[i] = m.takeFd(i);
        Stream &s = streams_[ann.slide];
        s.transport = ann.transport;
        s.frame.w = ann.width;
        s.frame.h = ann.height;
        unsigned tex = texture_import::importDmabuf(ann, fds, n);
        for (uint32_t i = 0; i < n; ++i)
            if (fds[i] >= 0)
                close(fds[i]); // the texture holds its own kernel reference
        if (tex) {
            unsigned &slot = s.buffer_tex[ann.buffer_index];
            if (slot) {
                GLuint old = slot;
                glDeleteTextures(1, &old); // worker restarted; old gen buffer
            }
            slot = tex;
        }
        return;
    }

    if (ann.transport != ipc::kTransportShm || ann.num_planes != 1)
        return;
    int fd = m.takeFd(0);
    if (fd < 0)
        return;

    Stream &s = streams_[ann.slide];
    s.transport = ann.transport;
    s.frame.w = ann.width;
    s.frame.h = ann.height;

    ShmMapping &mp = s.maps[ann.buffer_index];
    if (mp.ptr)
        munmap(mp.ptr, mp.size); // replaced (worker restarted)
    if (mp.fd >= 0)
        close(mp.fd);
    mp.fd = fd;
    mp.size = size_t(ann.stride[0]) * ann.height;
    mp.ptr = mmap(nullptr, mp.size, PROT_READ, MAP_SHARED, fd, 0);
    if (mp.ptr == MAP_FAILED) {
        perror("RemoteEngine: mmap frame buffer");
        close(mp.fd);
        mp = ShmMapping{};
    }
}

void RemoteEngine::handleFrameDone(ipc::Message &m, double t)
{
    frame_outstanding_ = false;
    logic_.onFrameDone(t);

    // kCapFenceSync: GPU-wait on the worker's render completion before any
    // command that samples the dma-buf textures promoted below.
    int fence = m.takeFd(0);
    if (fence >= 0)
        texture_import::waitFence(fence);

    ipc::Reader r(m.payload.data(), m.payload.size());
    ipc::FrameDoneMsg done{};
    if (!r.read(done))
        return;
    for (uint32_t i = 0; i < done.num_slides && r.ok(); ++i) {
        ipc::SlideFrameResult sr{};
        std::string exception;
        if (!r.read(sr) || !r.readString(exception, sr.exception_len))
            break;
        if (!sr.rendered)
            continue; // skipped (invisible, no content, or ring starved)

        auto it = streams_.find(sr.slide);
        if (it == streams_.end() || sr.buffer_index >= ipc::kBuffersPerSlide)
            continue;
        Stream &s = it->second;
        RemoteSlideFrame &f = s.frame;
        if (s.transport == ipc::kTransportDmabuf) {
            // Zero-copy: displaying is just pointing at the imported
            // texture. The displaced front buffer is still sampled until
            // this UI frame's swap — release it in postSwap().
            if (s.buffer_tex[sr.buffer_index]) {
                f.tex = s.buffer_tex[sr.buffer_index];
                f.rendered_once = true;
                if (s.front >= 0 && s.front != static_cast<int>(sr.buffer_index))
                    release_after_swap_.push_back(
                        {sr.slide, static_cast<uint32_t>(s.front)});
                s.front = static_cast<int>(sr.buffer_index);
            }
        } else {
            const ShmMapping &mp = s.maps[sr.buffer_index];
            if (mp.ptr && f.w && f.h) {
                if (!f.tex) {
                    GLuint tex = 0;
                    glGenTextures(1, &tex);
                    glBindTexture(GL_TEXTURE_2D, tex);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, f.w, f.h, 0,
                                 GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
                    f.tex = tex;
                }
                glBindTexture(GL_TEXTURE_2D, f.tex);
                glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
                glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, f.w, f.h,
                                GL_RGBA, GL_UNSIGNED_BYTE, mp.ptr);
                glBindTexture(GL_TEXTURE_2D, 0);
                f.rendered_once = true;
            }
            // SHM is a copy: the buffer is free the moment the upload returns.
            ipc::BufferReleaseMsg rel{sr.slide, sr.buffer_index};
            proc_.channel().send(ipc::MsgType::BufferRelease, &rel, sizeof(rel));
        }

        f.want_capture_mouse = sr.want_capture_mouse;
        f.want_capture_keyboard = sr.want_capture_keyboard;
        f.want_text_input = sr.want_text_input;

        auto kit = known_.find(sr.slide);
        if (kit != known_.end() && kit->second)
            kit->second->exception = exception; // empty clears a fixed slide
    }
}

void RemoteEngine::postSwap()
{
    if (release_after_swap_.empty())
        return;
    if (proc_.running())
        for (const ipc::BufferReleaseMsg &rel : release_after_swap_)
            proc_.channel().send(ipc::MsgType::BufferRelease, &rel, sizeof(rel));
    release_after_swap_.clear();
}

const RemoteSlideFrame *RemoteEngine::slideFrame(int slide) const
{
    auto it = streams_.find(slide);
    return it != streams_.end() ? &it->second.frame : nullptr;
}

void RemoteEngine::setSlideInput(const ipc::SlideInput &in)
{
    frame_inputs_[in.slide] = in;
}

void RemoteEngine::addInputEvent(const ipc::InputEvent &ev)
{
    frame_events_.push_back(ev);
}

void RemoteEngine::pushClipboard()
{
    if (!proc_.running() || !ImGui::GetCurrentContext())
        return;
    const char *txt = ImGui::GetClipboardText();
    std::string text = txt ? txt : "";
    if (text.size() > ipc::kClipboardMax)
        text.resize(ipc::kClipboardMax);
    ipc::ClipboardDataMsg msg{static_cast<uint32_t>(text.size())};
    ipc::Writer w;
    w.append(msg);
    w.appendString(text);
    proc_.channel().send(ipc::MsgType::ClipboardData, w.data(), w.size());
}

void RemoteEngine::endFrame(double time, float delta_time)
{
    // Clipboard follows focus: when a slide gains focus, push main's
    // clipboard so a paste into a worker-side InputText finds fresh text.
    int focused = kNoFocusedSlide;
    for (const auto &[slide, in] : frame_inputs_)
        if (in.focused) {
            focused = slide;
            break;
        }
    if (focused != clipboard_focus_) {
        clipboard_focus_ = focused;
        if (focused != kNoFocusedSlide)
            pushClipboard();
    }

    if (!proc_.running() || !logic_.running() || frame_outstanding_)
        return; // coalesce: inputs are rebuilt next frame, events accumulate
    if (frame_inputs_.empty()) {
        frame_events_.clear(); // nothing visible; drop stale events
        return;
    }

    ipc::FrameBeginMsg fb{};
    fb.frame_id = next_frame_id_++;
    fb.time = time;
    fb.delta_time = delta_time;
    fb.num_slides = static_cast<uint32_t>(frame_inputs_.size());
    fb.num_events = static_cast<uint32_t>(frame_events_.size());

    ipc::Writer w;
    w.append(fb);
    for (const auto &[slide, in] : frame_inputs_)
        w.append(in);
    for (const ipc::InputEvent &ev : frame_events_)
        w.append(ev);
    if (w.size() > ipc::kMaxPayloadSize)
        frame_events_.clear(); // absurd event backlog; drop rather than wedge
    else if (proc_.channel().send(ipc::MsgType::FrameBegin, w.data(), w.size())) {
        frame_outstanding_ = true;
        logic_.onFrameBeginSent(now());
        frame_events_.clear();
    }
}

void RemoteEngine::submitIfNeeded(SourceFile &sf, double t)
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
        // Suppresses the frame watchdog until the CompileResult: the queued
        // frame legitimately waits behind this compile in the worker.
        logic_.onCompileSubmitted(t);
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
