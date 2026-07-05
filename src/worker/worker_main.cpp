// marcel_worker: the supervised process that owns Cling (step 3a of
// docs/plans/client-server-refactor.md — compile-only; rendering arrives in
// step 3b). Crashing is allowed and expected: no signal handling heroics,
// the supervisor in the main process restarts us.
//
// Threads:
//  - IO thread: blocking recv loop. Answers Ping -> Pong immediately (even
//    while a compile is running), queues SetSource jobs, exits the process
//    when the peer disappears or sends Shutdown.
//  - main (work) thread: sole owner of the Cling interpreter. Dequeues
//    jobs, announces CompileBusy when a compile actually starts (so the
//    supervisor's poisoned-slide marking is precise), compiles, replies
//    with CompileResult.
#include "engine/ClingEngine.h"
#include "ipc/Channel.h"
#include "ipc/Protocol.h"
#include "ipc/Serialize.h"
#include "Presentation.h"

#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <deque>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <unistd.h>

namespace {

ipc::Channel g_chan;
std::mutex g_send_mx; // Channel is not thread-safe; IO + work threads both send

bool sendMsg(ipc::MsgType type, const void *payload, uint32_t size)
{
    std::lock_guard<std::mutex> lock(g_send_mx);
    return g_chan.send(type, payload, size);
}

struct Job {
    int32_t slide;
    uint64_t request_id;
    bool is_cuda;
    std::string text;
};

std::mutex g_queue_mx;
std::condition_variable g_queue_cv;
std::deque<Job> g_queue;
bool g_quit = false;

void ioThread()
{
    for (;;) {
        ipc::Message m;
        auto r = g_chan.recvBlocking(m);
        if (r == ipc::Channel::RecvResult::Disconnected) {
            // Parent is gone. The work thread may be deep inside a compile;
            // don't wait for it.
            _exit(0);
        }
        if (r != ipc::Channel::RecvResult::Message)
            continue;
        switch (m.header.type) {
        case ipc::MsgType::Ping: {
            ipc::PingMsg ping{};
            if (m.payload.size() >= sizeof(ping)) {
                std::memcpy(&ping, m.payload.data(), sizeof(ping));
                ipc::PongMsg pong{ping.token};
                sendMsg(ipc::MsgType::Pong, &pong, sizeof(pong));
            }
            break;
        }
        case ipc::MsgType::SetSource: {
            ipc::Reader r2(m.payload.data(), m.payload.size());
            ipc::SetSourceMsg msg{};
            Job job;
            if (r2.read(msg) && r2.readString(job.text, msg.text_len)) {
                job.slide = msg.slide;
                job.request_id = msg.request_id;
                job.is_cuda = msg.is_cuda != 0;
                std::lock_guard<std::mutex> lock(g_queue_mx);
                g_queue.push_back(std::move(job));
                g_queue_cv.notify_one();
            }
            break;
        }
        case ipc::MsgType::Shutdown: {
            std::lock_guard<std::mutex> lock(g_queue_mx);
            g_quit = true;
            g_queue_cv.notify_one();
            break;
        }
        default:
            break; // FrameBegin/BufferRelease arrive with step 3b
        }
    }
}

void sendCompileResult(const SourceFile &sf, int32_t slide, uint64_t request_id,
                       const std::string &exception)
{
    ipc::CompileResultMsg res{};
    res.slide = slide;
    res.request_id = request_id;
    res.validated = sf.validated;
    res.compiled = sf.compiled;
    res.syntax_error = sf.syntax_error;
    res.has_function = sf.function != nullptr;
    res.value_len = static_cast<uint32_t>(sf.value.size());
    res.exception_len = static_cast<uint32_t>(exception.size());
    res.stderr_len = 0; // stderr tail lands with the crash panel (step 5)
    res.num_markers = static_cast<uint32_t>(sf.error_markers.size());

    ipc::Writer w;
    w.append(res);
    w.appendString(sf.value);
    w.appendString(exception);
    for (const auto &[line, text] : sf.error_markers) {
        ipc::ErrorMarkerWire mw{static_cast<uint32_t>(line),
                                static_cast<uint32_t>(text.size())};
        w.append(mw);
        w.appendString(text);
    }
    sendMsg(ipc::MsgType::CompileResult, w.data(), w.size());
}

} // namespace

int main(int argc, char **argv)
{
    // Split off our own arguments; everything else is forwarded to the
    // Cling interpreter (which rejects flags it doesn't know).
    int fd = ipc::kWorkerSocketFd;
    std::vector<char *> cling_argv;
    for (int i = 0; i < argc; ++i) {
        if (strncmp(argv[i], "--ipc-fd=", 9) == 0)
            fd = atoi(argv[i] + 9);
        else
            cling_argv.push_back(argv[i]);
    }
    g_chan = ipc::Channel(fd);

    // Handshake before anything heavy, so the supervisor learns quickly
    // whether it spawned a compatible binary.
    ipc::Message m;
    if (g_chan.recvBlocking(m) != ipc::Channel::RecvResult::Message ||
        m.header.type != ipc::MsgType::Hello || m.payload.size() < sizeof(ipc::HelloMsg)) {
        fprintf(stderr, "marcel_worker: bad handshake\n");
        return 2;
    }
    ipc::HelloMsg hello{};
    std::memcpy(&hello, m.payload.data(), sizeof(hello));

    ipc::HelloAckMsg ack{};
    ack.protocol_version = ipc::kProtocolVersion;
    ack.transport_caps = ipc::kTransportShm;
    ack.chosen_transport = ipc::kTransportShm;
    snprintf(ack.gl_renderer, sizeof(ack.gl_renderer), "(compile-only)");
    if (!sendMsg(ipc::MsgType::HelloAck, &ack, sizeof(ack)))
        return 2;
    if (hello.protocol_version != ipc::kProtocolVersion)
        return 3; // main decides what to tell the user; don't limp along

    // IO thread first: pings must be answered while the interpreter builds
    // (which takes seconds) and during long compiles.
    std::thread io(ioThread);
    io.detach();

    ClingEngine engine(static_cast<int>(cling_argv.size()), cling_argv.data());

    // Worker-side SourceFiles are backed by scratch files (SourceFile is
    // file-backed by contract); the real files stay owned by the main
    // process. Also handy forensics: the exact source the worker last saw.
    namespace fs = std::filesystem;
    fs::path scratch = fs::temp_directory_path() / ("marcel_worker." + std::to_string(getpid()));
    fs::create_directories(scratch);
    std::map<int32_t, std::unique_ptr<SourceFile>> files;

    for (;;) {
        Job job;
        {
            std::unique_lock<std::mutex> lock(g_queue_mx);
            g_queue_cv.wait(lock, [] { return g_quit || !g_queue.empty(); });
            if (g_quit && g_queue.empty())
                break;
            job = std::move(g_queue.front());
            g_queue.pop_front();
        }

        ipc::CompileBusyMsg busy{job.slide, job.request_id};
        sendMsg(ipc::MsgType::CompileBusy, &busy, sizeof(busy));

        auto &sf_ptr = files[job.slide];
        if (!sf_ptr) {
            std::string name = job.slide < 0 ? "setup.cpp"
                                             : "slide" + std::to_string(job.slide) + ".cpp";
            sf_ptr = std::make_unique<SourceFile>(scratch / name);
        }
        SourceFile &sf = *sf_ptr;
        sf.setText(job.text);
        sf.execute(); // reset validated/compiled/syntax_error, count lines
        sf.is_cuda = job.is_cuda;

        std::string exception;
        try {
            if (job.slide < 0)
                engine.compileSetup(sf);
            else
                engine.compileSlide(sf);
        } catch (std::exception &e) {
            exception = e.what();
        }
        sendCompileResult(sf, job.slide, job.request_id, exception);
    }
    return 0;
}
