#include "WorkerProcess.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

WorkerProcess::~WorkerProcess()
{
    if (pid_ > 0) {
        ::kill(pid_, SIGKILL);
        waitpid(pid_, nullptr, 0);
    }
    if (err_fd_ >= 0)
        ::close(err_fd_);
}

bool WorkerProcess::spawn()
{
    if (pid_ > 0)
        return false; // already running; reap first

    ipc::Channel parent_end, child_end;
    if (!ipc::Channel::createPair(parent_end, child_end))
        return false;

    // The child's stderr goes through a pipe so the supervisor can echo it
    // AND keep a tail for the crash panel. Only our read end is non-blocking;
    // the child's writes stay blocking (a worker that outruns the per-frame
    // drain stalls and trips the watchdog instead of losing output).
    int errpipe[2];
    if (pipe2(errpipe, O_CLOEXEC) < 0)
        return false;

    pid_t pid = fork();
    if (pid < 0) {
        ::close(errpipe[0]);
        ::close(errpipe[1]);
        return false;
    }

    if (pid == 0) {
        // Child. Put our socket end on the well-known fd and exec. dup2
        // clears CLOEXEC on the target fd, so it survives the exec.
        if (dup2(child_end.fd(), ipc::kWorkerSocketFd) < 0)
            _exit(127);
        if (dup2(errpipe[1], STDERR_FILENO) < 0)
            _exit(127);
        char fd_arg[32];
        snprintf(fd_arg, sizeof(fd_arg), "--ipc-fd=%d", ipc::kWorkerSocketFd);
        char *args[] = {const_cast<char *>(exe_.c_str()), fd_arg, nullptr};
        execv(exe_.c_str(), args);
        _exit(127); // exec failed; parent sees an immediate exit
    }

    ::close(errpipe[1]);
    if (err_fd_ >= 0)
        ::close(err_fd_); // previous generation's pipe (already EOF-drained
                          // in normal operation; don't leak it regardless)
    err_fd_ = errpipe[0];
    fcntl(err_fd_, F_SETFL, O_NONBLOCK);

    pid_ = pid;
    exit_desc_.clear();
    chan_ = std::move(parent_end);
    return true;
}

void WorkerProcess::terminate()
{
    if (pid_ > 0)
        ::kill(pid_, SIGTERM);
}

void WorkerProcess::kill()
{
    if (pid_ > 0)
        ::kill(pid_, SIGKILL);
}

bool WorkerProcess::reap()
{
    if (pid_ <= 0)
        return false;
    int status = 0;
    pid_t r = waitpid(pid_, &status, WNOHANG);
    if (r != pid_)
        return false;
    pid_ = -1;
    if (WIFSIGNALED(status)) {
        int sig = WTERMSIG(status);
        exit_desc_ = "killed by signal " + std::to_string(sig) + " (" +
                     strsignal(sig) + ")";
    } else if (WIFEXITED(status)) {
        exit_desc_ = "exited with status " + std::to_string(WEXITSTATUS(status));
    } else {
        exit_desc_ = "vanished (status " + std::to_string(status) + ")";
    }
    chan_.close();
    return true;
}

size_t WorkerProcess::drainStderr(std::string &out)
{
    if (err_fd_ < 0)
        return 0;
    size_t total = 0;
    char buf[4096];
    for (;;) {
        ssize_t n = read(err_fd_, buf, sizeof(buf));
        if (n > 0) {
            out.append(buf, static_cast<size_t>(n));
            total += static_cast<size_t>(n);
        } else if (n == 0) {
            // EOF: the child is gone and the pipe is fully drained.
            ::close(err_fd_);
            err_fd_ = -1;
            break;
        } else {
            break; // EAGAIN/EINTR: nothing more right now
        }
    }
    return total;
}
