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
#include "supervisor/SupervisorLogic.h"
#include "supervisor/WorkerProcess.h"

#include <cstdint>
#include <map>
#include <string>

namespace ipc {
struct Message;
}

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

private:
    void pump(double now);
    void handleMessage(ipc::Message &m, double now);
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
};
