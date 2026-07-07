#pragma once

// SlideEngine implemented over IPC to a supervised marcel_worker process
// (step 3a of docs/plans/client-server-refactor.md: compile-only).
//
// Compiles happen out of process: sources go over as SetSource, results
// come back as CompileResult and are applied to the SourceFile — except
// SourceFile::function, which cannot cross a process boundary. Until remote
// rendering lands (step 3b), a remote-compiled slide displays its value
// text instead of running; the win being proven here is that a crashing or
// hanging compile takes down the worker, not the app.
//
// Drive: compileSetup() runs once per UI frame (main calls it before the
// slide loop) and doubles as the pump — spawn/kill/backoff via
// SupervisorLogic, socket drain, ping. compileSlide() only submits/observes.

#include "engine/SlideEngine.h"
#include "ipc/Protocol.h"
#include "supervisor/SupervisorLogic.h"
#include "supervisor/WorkerProcess.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace ipc {
struct Message;
}

// The latest frame the worker delivered for one slide, as a GL texture in
// the main process (uploaded from the announced shm buffer on FrameDone).
// The texture outlives the worker, so a dead worker shows its last frame.
struct RemoteSlideFrame {
    unsigned tex = 0;      // GL texture name; 0 until the first frame lands
    uint32_t w = 0, h = 0; // texture size (worker design resolution)
    bool rendered_once = false;
    // From the last FrameDone: is the mouse on an interactive widget, does
    // the slide want keys/text. Gate input forwarding and deck scrolling.
    bool want_capture_mouse = false;
    bool want_capture_keyboard = false;
    bool want_text_input = false;
};

struct RemoteEngineSettings {
    float dpi_scale = 1.0f;
    uint32_t design_w = 1728, design_h = 1080; // 16:10 default
};

class RemoteEngine : public SlideEngine {
public:
    using Settings = RemoteEngineSettings;

    RemoteEngine(std::string worker_exe, Settings settings = {});
    ~RemoteEngine() override;

    void compileSetup(SourceFile &setup) override;
    void compileSlide(SourceFile &slide) override;
    void dump(const char *what, const char *filter) override;

    bool workerRunning() const { return logic_.running(); }
    bool gaveUp() const { return logic_.gaveUp(); }
    int crashCount() const { return logic_.crashCount(); }

    // Manual restart (toolbar / crash panel): kills a running worker and
    // respawns immediately; also revives a gave-up supervisor.
    void restartWorker() { logic_.requestRestart(now()); }

    // Bounded tail of the worker's stderr (echoed live to ours as well),
    // with "[worker exited: ...]" lines between generations. Crash panel food.
    const std::string &stderrTail() const { return stderr_tail_; }

    // --- Remote rendering (step 3b) ---------------------------------------
    // Per UI frame: pump (via compileSetup) clears the input set, SlideView
    // fills it for every visible slide, endFrame() sends one FrameBegin if
    // the previous one has been answered (mailbox pacing: discrete events
    // accumulate across skipped frames, continuous input is rebuilt anyway).
    const RemoteSlideFrame *slideFrame(int slide) const;
    void setSlideInput(const ipc::SlideInput &in);
    void addInputEvent(const ipc::InputEvent &ev);
    void endFrame(double time, float delta_time);
    uint32_t designW() const { return settings_.design_w; }
    uint32_t designH() const { return settings_.design_h; }

    // Transports this process can display, advertised in Hello. Set after
    // the GL context exists (TextureImport::dmabufAvailable probes it) and
    // before the first pump — the worker spawns lazily on the first frame.
    void setTransportCaps(uint32_t caps) { transport_caps_ = caps; }

    // After glfwSwapBuffers: release dma-buf buffers displaced this frame
    // (the GPU may sample the front buffer up to the swap; shm buffers are
    // copies and were released at upload time).
    void postSwap();

private:
    struct ShmMapping {
        int fd = -1;
        void *ptr = nullptr;
        size_t size = 0;
    };
    struct Stream {
        RemoteSlideFrame frame;
        uint32_t transport = 0; // of the last TextureAnnounce
        ShmMapping maps[ipc::kBuffersPerSlide];            // shm
        unsigned buffer_tex[ipc::kBuffersPerSlide] = {};   // dmabuf imports
        int front = -1; // dmabuf: buffer index currently displayed
    };

    void pump(double now);
    void drainWorkerStderr();
    void noteStderr(const std::string &text);
    void handleMessage(ipc::Message &m, double now);
    void handleTextureAnnounce(ipc::Message &m);
    void handleFrameDone(ipc::Message &m, double now);
    // Sends main's current clipboard as ClipboardData (focus gain, or the
    // worker asked). X11 clipboard reads are a server round trip, so this
    // is event-driven, never polled.
    void pushClipboard();
    void submitIfNeeded(SourceFile &sf, double now);
    void applyPoison(int slide);
    double now() const;

    // Slide index from the SourceFile's on-disk name (Presentation names
    // them setup.cpp / slideN.cpp): -1 for setup, N for slides.
    static int slideIndexOf(const SourceFile &sf);

    Settings settings_;
    supervisor::SupervisorLogic logic_;
    WorkerProcess proc_;
    std::map<int, SourceFile *> known_; // slide index -> last SourceFile seen
    uint64_t next_request_id_ = 1;
    bool killing_ = false;
    double kill_deadline_ = 0.0;
    double t0_;

    std::map<int, Stream> streams_;
    std::map<int, ipc::SlideInput> frame_inputs_; // this UI frame's visible slides
    std::vector<ipc::InputEvent> frame_events_;
    std::vector<ipc::BufferReleaseMsg> release_after_swap_; // dmabuf only
    uint32_t transport_caps_ = ipc::kTransportShm;
    bool frame_outstanding_ = false;
    uint64_t next_frame_id_ = 1;
    static constexpr int kNoFocusedSlide = -1000;
    int clipboard_focus_ = kNoFocusedSlide; // slide whose focus we last saw

    std::string stderr_tail_; // bounded ring of worker stderr
};
