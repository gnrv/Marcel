// https://github.com/dmikushin/stdcapture
#pragma once

#ifdef _MSC_VER
#include <io.h>
#define popen _popen
#define pclose _pclose
#define stat _stat
#define dup _dup
#define dup2 _dup2
#define fileno _fileno
#define close _close
#define pipe _pipe
#define read _read
#define eof _eof
#else
#include <unistd.h>
#endif
#include <chrono>
#include <cstdio>
#include <fcntl.h>
#include <functional>
#include <thread>

#ifndef STD_OUT_FD
#define STD_OUT_FD (fileno(stdout))
#endif

#ifndef STD_ERR_FD
#define STD_ERR_FD (fileno(stderr))
#endif

class CaptureOutput {
    FILE* stream;
    int fd;

    enum PIPES { READ,
                    WRITE };

    int pipes[2];
    int streamOld;

    std::function<void(const char*, size_t)> callback;

    std::thread readerThread;
    std::atomic<bool> stopReading{false};
    std::string accumulatedOutput;

public:
    CaptureOutput(FILE* stream_, int fd_, std::function<void(const char*, size_t)> callback_)
        : stream(stream_)
        , fd(fd_)
        , callback(callback_)
    {
        // Make output stream unbuffered, so that we don't need to flush
        // the streams before capture and after capture (fflush can cause a deadlock)
        setvbuf(stream, NULL, _IONBF, 0);

        // Start capturing.
        secure_pipe(pipes);
        streamOld = secure_dup(fd);
        secure_dup2(pipes[WRITE], fd);
#ifndef _MSC_VER
        secure_close(pipes[WRITE]);
#endif

        // Start background reader
        try {
            readerThread = std::thread([this]() { reader(true /* isBackground */); });
        } catch (...) {
            // Thread creation failed
        }
    }

    ~CaptureOutput()
    {
        // Signal reader to stop
        stopReading = true;

        // End capturing.
        secure_dup2(streamOld, fd);

        // Wait for reader thread to finish ,if it was ever started
        // else call reader directly
        if (readerThread.joinable())
            readerThread.join();
        else
            reader(false /* isBackground */);

        // Now safely call the callback from the main thread
        if (!accumulatedOutput.empty()) {
            callback(accumulatedOutput.c_str(), accumulatedOutput.size());
        }

        secure_close(streamOld);
        secure_close(pipes[READ]);
#ifdef _MSC_VER
        secure_close(pipes[WRITE]);
#endif
    }

private:
    void reader(bool isBackground)
    {
        const int bufSize = 1025;
        char buf[bufSize];
        int bytesRead = 0;

        while (!stopReading) {
            bytesRead = 0;
            bool fd_blocked = false;

#ifdef _MSC_VER
            if (!eof(pipes[READ]))
                bytesRead = read(pipes[READ], buf, bufSize - 1);
#else
            bytesRead = read(pipes[READ], buf, bufSize - 1);
#endif

            if (bytesRead > 0) {
                buf[bytesRead] = 0;
                accumulatedOutput.append(buf, bytesRead);
                write(streamOld, buf, bytesRead); // Echo to original stream
            } else if (bytesRead < 0) {
                fd_blocked = (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR);
                if (fd_blocked) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                } else {
                    break; // Real error, exit thread
                }
            } else {
                // bytesRead == 0, EOF reached
                break;
            }
        }

        // Final drain of any remaining data
        do {
            bytesRead = 0;
#ifdef _MSC_VER
            if (!eof(pipes[READ]))
                bytesRead = read(pipes[READ], buf, bufSize - 1);
#else
            bytesRead = read(pipes[READ], buf, bufSize - 1);
#endif
            if (bytesRead > 0) {
                buf[bytesRead] = 0;
                accumulatedOutput.append(buf, bytesRead);
                write(streamOld, buf, bytesRead); // Echo to original stream
            }
        } while (bytesRead > 0);
    }

    int secure_dup(int src)
    {
        int ret = -1;
        bool fd_blocked = false;
        do {
            ret = dup(src);
            fd_blocked = (errno == EINTR || errno == EBUSY);
            if (fd_blocked)
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
        } while (ret < 0);
        return ret;
    }

    void secure_pipe(int* pipes)
    {
        int ret = -1;
        bool fd_blocked = false;
        do {
#ifdef _MSC_VER
            ret = pipe(pipes, 65536, O_BINARY);
#else
            ret = pipe(pipes) == -1;
#endif
            fd_blocked = (errno == EINTR || errno == EBUSY);
            if (fd_blocked)
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
        } while (ret < 0);
    }

    void secure_dup2(int src, int dest)
    {
        int ret = -1;
        bool fd_blocked = false;
        do {
            ret = dup2(src, dest);
            fd_blocked = (errno == EINTR || errno == EBUSY);
            if (fd_blocked)
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
        } while (ret < 0);
    }

    void secure_close(int& fd)
    {
        int ret = -1;
        bool fd_blocked = false;
        do {
            ret = close(fd);
            fd_blocked = (errno == EINTR);
            if (fd_blocked)
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
        } while (ret < 0);

        fd = -1;
    }
};

class CaptureStdout : public CaptureOutput {
public:
    CaptureStdout(std::function<void(const char*, size_t)> callback)
        : CaptureOutput(stdout, STD_OUT_FD, callback)
    {
    }
};

class CaptureStderr : public CaptureOutput {
public:
    CaptureStderr(std::function<void(const char*, size_t)> callback)
        : CaptureOutput(stderr, STD_ERR_FD, callback)
    {
    }
};
