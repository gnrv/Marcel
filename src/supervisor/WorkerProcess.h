#pragma once

// Owns the marcel_worker child process: socketpair + fork + exec, signals,
// and reaping. No policy here — SupervisorLogic decides when to spawn/kill,
// RemoteEngine drives both.

#include "ipc/Channel.h"

#include <string>

#include <sys/types.h>

class WorkerProcess {
public:
    // exe_path: absolute path to the marcel_worker binary.
    explicit WorkerProcess(std::string exe_path) : exe_(std::move(exe_path)) {}
    ~WorkerProcess();
    WorkerProcess(const WorkerProcess &) = delete;
    WorkerProcess &operator=(const WorkerProcess &) = delete;

    // fork/exec with the child's socket end dup2'd to ipc::kWorkerSocketFd.
    // Returns false if the pair or fork fails (exec failure surfaces as an
    // immediate child exit, reported by reap()).
    bool spawn();

    void terminate(); // SIGTERM (graceful; worker may just die, that's fine)
    void kill();      // SIGKILL

    // waitpid(WNOHANG). Returns true exactly once, when the child has been
    // reaped; also closes our channel end.
    bool reap();

    // Non-blocking drain of the child's stderr pipe, appended to out.
    // Returns bytes appended. Keeps working after reap() — the pipe closes
    // itself once the last buffered bytes have been read (EOF).
    size_t drainStderr(std::string &out);

    // Human-readable cause of death ("exited with status 1", "killed by
    // signal 11 (Segmentation fault)"). Set when reap() returns true.
    const std::string &exitDescription() const { return exit_desc_; }

    bool running() const { return pid_ > 0; }
    pid_t pid() const { return pid_; }
    ipc::Channel &channel() { return chan_; }

private:
    std::string exe_;
    pid_t pid_ = -1;
    int err_fd_ = -1; // read end of the child's stderr pipe
    std::string exit_desc_;
    ipc::Channel chan_;
};
