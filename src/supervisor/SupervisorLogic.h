#pragma once

// Restart + watchdog policy for the marcel_worker process, as pure logic:
// time is injected as seconds and every side effect (fork/exec, kill, socket
// I/O) belongs to the driver (WorkerProcess). The driver feeds events in,
// calls update() once per UI frame, and executes the returned Action.
//
// Liveness policy (docs/plans/client-server-refactor.md, "Watchdog"):
//  - Ping every ping_interval; no Pong within pong_timeout -> Kill.
//  - A FrameBegin unanswered for frame_timeout -> Kill, but suppressed while
//    compiles are in flight — busy OR merely submitted: the worker services
//    its queue in order, so a queued frame legitimately waits behind engine
//    init and every queued SetSource (a heavy setup compile once made this
//    window seconds wide and killed the worker in a loop). Each
//    CompileResult restarts the frame clock. A compile busy longer than
//    compile_timeout -> Kill; a submitted compile with no CompileBusy /
//    CompileResult for compile_timeout (work thread wedged) -> Kill.
//  - Respawn after death with backoff 0/1/2/4/8/10 s (doubling, capped at
//    backoff_max); a stable run of >= crash_storm_window resets the backoff.
//  - More than crash_storm_count crashes within crash_storm_window -> give
//    up (no more spawns; UI shows the crash panel).
//  - The slide whose CompileBusy was outstanding at death is poisoned: the
//    source likely crashed the interpreter and must not be resubmitted
//    until edited. Fetch it once via takePoisonedSlide().

#include <cstdint>
#include <deque>

namespace supervisor {

// Sentinel for "no slide". Distinct from -1, which is the setup file.
constexpr int kNoSlide = INT32_MIN;

enum class Action {
    None,
    Spawn,  // fork/exec a worker now, then call onSpawned()
    Kill,   // kill the worker (SIGTERM->SIGKILL), then call onWorkerExit()
};

struct Config {
    double ping_interval = 0.5;
    double pong_timeout = 2.0;
    double frame_timeout = 1.0;
    double compile_timeout = 30.0;
    double backoff_max = 10.0;
    int crash_storm_count = 5;        // give up when count exceeded...
    double crash_storm_window = 30.0; // ...within this many seconds
};

class SupervisorLogic {
public:
    explicit SupervisorLogic(Config cfg = {}) : cfg_(cfg) {}

    void start(double now);  // arm the first spawn

    // Events from the driver.
    void onSpawned(double now);
    void onHandshake(double now);  // HelloAck received
    void onWorkerExit(double now); // reaped via waitpid / POLLHUP
    void onPingSent(double now);
    void onPong(double now);
    void onFrameBeginSent(double now);
    void onFrameDone(double now);
    void onCompileSubmitted(double now); // SetSource sent to the worker
    void onCompileBusy(int slide, double now);
    void onCompileResult(int slide, double now);

    // Manual "Restart worker" (toolbar button / crash panel). Clean slate:
    // clears give-up and the crash-storm/backoff history, then kills a
    // running worker (via update() -> Kill) or arms an immediate spawn.
    void requestRestart(double now);

    // Once per UI frame. A returned Spawn/Kill is emitted exactly once; the
    // driver must follow up with onSpawned()/onWorkerExit().
    Action update(double now);

    // True when a Ping should be sent this frame (running, none outstanding,
    // ping_interval elapsed since the last send).
    bool shouldPing(double now) const;

    bool running() const { return spawned_; }
    bool gaveUp() const { return gave_up_; }
    double nextSpawnTime() const { return spawn_time_; }
    int crashCount() const { return static_cast<int>(crash_times_.size()); }

    // The slide being compiled when the worker died, or kNoSlide. Consumed.
    int takePoisonedSlide();

private:
    double backoffDelay() const;

    Config cfg_;
    bool started_ = false;
    bool spawned_ = false;   // a worker process exists
    bool handshake_ = false; // HelloAck seen
    bool killing_ = false;   // Kill emitted, waiting for onWorkerExit
    bool restart_requested_ = false; // manual restart: emit one Kill
    bool gave_up_ = false;
    bool spawn_pending_ = true; // in backoff, spawn due at spawn_time_
    double spawn_time_ = 0.0;
    double spawned_at_ = 0.0;
    int backoff_index_ = 0;
    std::deque<double> crash_times_;

    bool ping_outstanding_ = false;
    double ping_sent_ = 0.0;
    double last_ping_time_ = 0.0;

    bool frame_outstanding_ = false;
    double frame_sent_ = 0.0;

    bool compile_busy_ = false;
    int busy_slide_ = kNoSlide;
    double compile_since_ = 0.0;

    // SetSources sent but not yet answered by a CompileResult. While any are
    // pending the frame watchdog is suppressed (the frame sits behind them
    // in the worker's queue); compile_wait_since_ bounds the wait instead.
    int compiles_pending_ = 0;
    double compile_wait_since_ = 0.0;

    int poisoned_slide_ = kNoSlide;
};

} // namespace supervisor
