#include "WorkerProcess.h"

#include <cstdio>
#include <cstdlib>

#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

WorkerProcess::~WorkerProcess()
{
    if (pid_ > 0) {
        ::kill(pid_, SIGKILL);
        waitpid(pid_, nullptr, 0);
    }
}

bool WorkerProcess::spawn()
{
    if (pid_ > 0)
        return false; // already running; reap first

    ipc::Channel parent_end, child_end;
    if (!ipc::Channel::createPair(parent_end, child_end))
        return false;

    pid_t pid = fork();
    if (pid < 0)
        return false;

    if (pid == 0) {
        // Child. Put our socket end on the well-known fd and exec. dup2
        // clears CLOEXEC on the target fd, so it survives the exec.
        if (dup2(child_end.fd(), ipc::kWorkerSocketFd) < 0)
            _exit(127);
        char fd_arg[32];
        snprintf(fd_arg, sizeof(fd_arg), "--ipc-fd=%d", ipc::kWorkerSocketFd);
        char *args[] = {const_cast<char *>(exe_.c_str()), fd_arg, nullptr};
        execv(exe_.c_str(), args);
        _exit(127); // exec failed; parent sees an immediate exit
    }

    pid_ = pid;
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
    chan_.close();
    return true;
}
