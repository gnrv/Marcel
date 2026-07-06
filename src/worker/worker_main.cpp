// marcel_worker: the supervised process that owns Cling and renders slides
// offscreen (docs/plans/client-server-refactor.md). Crashing is allowed and
// expected: no signal handling heroics, the supervisor in the main process
// restarts us.
//
// Threads:
//  - IO thread: blocking recv loop. Answers Ping -> Pong immediately (even
//    while a compile is running), posts SetSource/FrameBegin/BufferRelease
//    to the work queue, exits the process when the peer disappears.
//  - main (work) thread: sole owner of GL + Cling (WorkerApp::run()).
#include "ipc/Channel.h"
#include "ipc/Protocol.h"
#include "ipc/Serialize.h"
#include "system/sys_util.h"
#include "worker/WorkerApp.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <thread>
#include <vector>

#include <unistd.h>

namespace {

ipc::Channel g_chan;
std::mutex g_send_mx; // Channel is not thread-safe; IO + work threads both send

bool sendMsg(ipc::MsgType type, const void *payload, uint32_t size,
             const int *fds = nullptr, uint32_t num_fds = 0)
{
    std::lock_guard<std::mutex> lock(g_send_mx);
    return g_chan.send(type, payload, size, fds, num_fds);
}

void ioThread(WorkerApp *app)
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
            WorkerEvent ev;
            ev.type = WorkerEvent::Type::SetSource;
            if (r2.read(msg) && r2.readString(ev.text, msg.text_len)) {
                ev.slide = msg.slide;
                ev.request_id = msg.request_id;
                ev.is_cuda = msg.is_cuda != 0;
                app->post(std::move(ev));
            }
            break;
        }
        case ipc::MsgType::FrameBegin: {
            ipc::Reader r2(m.payload.data(), m.payload.size());
            WorkerEvent ev;
            ev.type = WorkerEvent::Type::FrameBegin;
            if (!r2.read(ev.frame))
                break;
            ev.inputs.resize(ev.frame.num_slides);
            ev.events.resize(ev.frame.num_events);
            bool ok = true;
            for (auto &si : ev.inputs)
                ok = ok && r2.read(si);
            for (auto &ie : ev.events)
                ok = ok && r2.read(ie);
            if (ok)
                app->post(std::move(ev));
            break;
        }
        case ipc::MsgType::BufferRelease: {
            ipc::BufferReleaseMsg msg{};
            if (m.payload.size() >= sizeof(msg)) {
                std::memcpy(&msg, m.payload.data(), sizeof(msg));
                WorkerEvent ev;
                ev.type = WorkerEvent::Type::BufferRelease;
                ev.slide = msg.slide;
                ev.buffer_index = msg.buffer_index;
                app->post(std::move(ev));
            }
            break;
        }
        case ipc::MsgType::Shutdown: {
            WorkerEvent ev;
            ev.type = WorkerEvent::Type::Shutdown;
            app->post(std::move(ev));
            return;
        }
        default:
            break;
        }
    }
}

} // namespace

int main(int argc, char **argv)
{
    // Fonts load relative to the executable directory, mirroring main.
    std::filesystem::current_path(getExecutablePath());

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

    WorkerApp app(hello, [](ipc::MsgType t, const void *p, uint32_t s,
                            const int *fds, uint32_t n) { return sendMsg(t, p, s, fds, n); });
    bool gl_ok = app.initGL();

    // Transport negotiation: dma-buf zero-copy when both sides support it,
    // else shm (memfd + glReadPixels), else compile-only.
    uint32_t caps = app.transportCaps();
    uint32_t chosen = 0;
    if (caps & hello.transport_caps & ipc::kTransportDmabuf)
        chosen = ipc::kTransportDmabuf;
    else if (caps & ipc::kTransportShm)
        chosen = ipc::kTransportShm;
    app.setTransport(chosen);

    ipc::HelloAckMsg ack{};
    ack.protocol_version = ipc::kProtocolVersion;
    ack.transport_caps = caps;
    ack.chosen_transport = chosen;
    snprintf(ack.gl_renderer, sizeof(ack.gl_renderer), "%s",
             gl_ok ? app.glRenderer().c_str() : "(no GL)");
    if (!sendMsg(ipc::MsgType::HelloAck, &ack, sizeof(ack)))
        return 2;
    if (hello.protocol_version != ipc::kProtocolVersion)
        return 3; // main decides what to tell the user; don't limp along

    // IO thread first: pings must be answered while the interpreter builds
    // (which takes seconds) and during long compiles.
    std::thread io(ioThread, &app);
    io.detach();

    app.initEngine(static_cast<int>(cling_argv.size()), cling_argv.data());
    app.run();
    return 0;
}
