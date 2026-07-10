#include "SupervisorLogic.h"

#include <algorithm>

namespace supervisor {

void SupervisorLogic::start(double now)
{
    if (started_)
        return;
    started_ = true;
    spawn_pending_ = true;
    spawn_time_ = now;
}

void SupervisorLogic::onSpawned(double now)
{
    spawned_ = true;
    handshake_ = false;
    killing_ = false;
    spawn_pending_ = false;
    spawned_at_ = now;
    last_ping_time_ = now;
    ping_outstanding_ = false;
    frame_outstanding_ = false;
    compile_busy_ = false;
    busy_slide_ = kNoSlide;
    compiles_pending_ = 0;
}

void SupervisorLogic::onHandshake(double)
{
    handshake_ = true;
}

void SupervisorLogic::onWorkerExit(double now)
{
    if (!spawned_)
        return;
    spawned_ = false;
    handshake_ = false;
    killing_ = false;
    restart_requested_ = false; // an exit is the restart half-done
    ping_outstanding_ = false;
    frame_outstanding_ = false;
    if (compile_busy_)
        poisoned_slide_ = busy_slide_;
    compile_busy_ = false;
    busy_slide_ = kNoSlide;
    compiles_pending_ = 0; // the queue died with the worker

    // A long stable run forgives past instability.
    if (now - spawned_at_ >= cfg_.crash_storm_window) {
        backoff_index_ = 0;
        crash_times_.clear();
    }

    crash_times_.push_back(now);
    while (!crash_times_.empty() && crash_times_.front() < now - cfg_.crash_storm_window)
        crash_times_.pop_front();
    if (static_cast<int>(crash_times_.size()) > cfg_.crash_storm_count) {
        gave_up_ = true;
        return;
    }

    spawn_time_ = now + backoffDelay();
    ++backoff_index_;
    spawn_pending_ = true;
}

double SupervisorLogic::backoffDelay() const
{
    if (backoff_index_ == 0)
        return 0.0;
    double d = static_cast<double>(1u << std::min(backoff_index_ - 1, 30));
    return std::min(d, cfg_.backoff_max);
}

void SupervisorLogic::onPingSent(double now)
{
    ping_outstanding_ = true;
    ping_sent_ = now;
    last_ping_time_ = now;
}

void SupervisorLogic::onPong(double)
{
    ping_outstanding_ = false;
}

void SupervisorLogic::onFrameBeginSent(double now)
{
    frame_outstanding_ = true;
    frame_sent_ = now;
}

void SupervisorLogic::onFrameDone(double)
{
    frame_outstanding_ = false;
}

void SupervisorLogic::onCompileSubmitted(double now)
{
    if (compiles_pending_++ == 0)
        compile_wait_since_ = now;
}

void SupervisorLogic::onCompileBusy(int slide, double now)
{
    compile_busy_ = true;
    busy_slide_ = slide;
    compile_since_ = now;
    compile_wait_since_ = now; // the work thread is servicing the queue
}

void SupervisorLogic::requestRestart(double now)
{
    if (!started_)
        return;
    gave_up_ = false;
    crash_times_.clear();
    backoff_index_ = 0;
    if (spawned_) {
        restart_requested_ = true; // update() emits one Kill
    } else {
        spawn_pending_ = true;
        spawn_time_ = now;
    }
}

void SupervisorLogic::onCompileResult(int slide, double now)
{
    if (compiles_pending_ > 0)
        --compiles_pending_;
    compile_wait_since_ = now;
    if (compile_busy_ && slide == busy_slide_) {
        compile_busy_ = false;
        busy_slide_ = kNoSlide;
    }
    // The worker just proved it is servicing its queue; an outstanding frame
    // (queued behind the compiles) gets a fresh window from here.
    if (frame_outstanding_)
        frame_sent_ = now;
}

Action SupervisorLogic::update(double now)
{
    if (!started_ || gave_up_)
        return Action::None;

    if (!spawned_) {
        if (spawn_pending_ && now >= spawn_time_) {
            spawn_pending_ = false; // emitted exactly once
            return Action::Spawn;
        }
        return Action::None;
    }

    if (killing_)
        return Action::None; // waiting for onWorkerExit

    if (restart_requested_) {
        restart_requested_ = false;
        killing_ = true;
        return Action::Kill;
    }

    bool compiling = compile_busy_ || compiles_pending_ > 0;
    bool overdue =
        (ping_outstanding_ && now - ping_sent_ > cfg_.pong_timeout) ||
        (compile_busy_ && now - compile_since_ > cfg_.compile_timeout) ||
        (!compile_busy_ && compiles_pending_ > 0 &&
         now - compile_wait_since_ > cfg_.compile_timeout) ||
        (frame_outstanding_ && !compiling && now - frame_sent_ > cfg_.frame_timeout);
    if (overdue) {
        killing_ = true;
        return Action::Kill;
    }
    return Action::None;
}

bool SupervisorLogic::shouldPing(double now) const
{
    return spawned_ && !killing_ && !ping_outstanding_ &&
           now - last_ping_time_ >= cfg_.ping_interval;
}

int SupervisorLogic::takePoisonedSlide()
{
    int s = poisoned_slide_;
    poisoned_slide_ = kNoSlide;
    return s;
}

} // namespace supervisor
