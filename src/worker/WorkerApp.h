#pragma once

// The work-thread half of marcel_worker: owns the GL context, the Cling
// engine, the shared font atlas, and one SlideRenderer + buffer ring +
// shm buffers per slide. The IO thread posts events; run() drains them,
// compiles, renders, and replies (via the injected thread-safe sender).

#include "ipc/BufferRing.h"
#include "ipc/Protocol.h"
#include "worker/HeadlessGL.h"
#include "worker/ShmBuffer.h"

#include <array>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

class ClingEngine;
class SlideRenderer;
class SourceFile;
struct ImFontAtlas;
struct ImGuiContext;

struct WorkerEvent {
    enum class Type { SetSource, FrameBegin, BufferRelease, Shutdown };
    Type type;
    // SetSource
    int32_t slide = 0;
    uint64_t request_id = 0;
    bool is_cuda = false;
    std::string text;
    // FrameBegin
    ipc::FrameBeginMsg frame{};
    std::vector<ipc::SlideInput> inputs;
    std::vector<ipc::InputEvent> events;
    // BufferRelease
    uint32_t buffer_index = 0;
};

class WorkerApp {
public:
    // send must be thread-safe (the IO thread also sends Pongs through it).
    using SendFn = std::function<bool(ipc::MsgType, const void *, uint32_t,
                                      const int *, uint32_t)>;

    WorkerApp(const ipc::HelloMsg &hello, SendFn send);
    ~WorkerApp();

    // GL first (setup code may call raw GL at compile time), then fonts and
    // the compile-time ImGui context. Returns false if EGL fails — the
    // caller reports SHM-less/compile-only capabilities instead of dying.
    bool initGL();
    const std::string &glRenderer() const { return gl_.renderer(); }

    // Transports this worker can serve (0 without GL). The driver picks the
    // intersection with main's Hello caps and tells us via setTransport.
    uint32_t transportCaps() const;
    void setTransport(uint32_t transport) { transport_ = transport; }

    // Heavy: constructs the Cling interpreter. Call after the IO thread is
    // answering pings.
    void initEngine(int argc, char **argv);

    void post(WorkerEvent ev); // IO thread
    void run();                // work thread; returns on Shutdown

private:
    struct SlideState {
        std::unique_ptr<SourceFile> src;
        std::unique_ptr<SlideRenderer> renderer;
        ipc::BufferRing ring;
        std::array<ShmBuffer, ipc::kBuffersPerSlide> buffers;   // shm only
        std::array<bool, ipc::kBuffersPerSlide> announced{};    // dmabuf only
    };

    void handleSetSource(WorkerEvent &ev);
    void handleFrameBegin(WorkerEvent &ev);
    // First use of a ring buffer: create/export it and send TextureAnnounce
    // (memfd for shm, dma-buf plane fds for dmabuf). False = buffer unusable.
    bool announceBuffer(SlideState &st, int32_t slide, uint32_t b);
    SlideState &state(int32_t slide); // in .cpp: needs complete SlideRenderer
    SourceFile &sourceFor(int32_t slide, SlideState &st);

    ipc::HelloMsg hello_;
    SendFn send_;
    HeadlessGL gl_;
    uint32_t transport_ = ipc::kTransportShm;
    std::unique_ptr<ClingEngine> engine_;
    ImFontAtlas *atlas_ = nullptr;
    struct ImFont *big_font_ = nullptr;
    ImGuiContext *compile_ctx_ = nullptr; // current during interp.process()
    std::filesystem::path scratch_;
    std::map<int32_t, SlideState> slides_;

    std::mutex queue_mx_;
    std::condition_variable queue_cv_;
    std::deque<WorkerEvent> queue_;
    bool quit_ = false;
};
