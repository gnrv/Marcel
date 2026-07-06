#pragma once

// One shared-memory frame buffer: a memfd mapped on the worker side, its fd
// passed to the main process via SCM_RIGHTS (TextureAnnounce). The worker
// glReadPixels into map(); the main process mmaps the same fd and uploads
// with glTexSubImage2D. Permanent fallback transport for drivers without
// DMA-BUF export (WSL2) — see docs/plans/client-server-refactor.md.

#include <cstddef>

#include <sys/mman.h>
#include <unistd.h>

class ShmBuffer {
public:
    ShmBuffer() = default;
    ~ShmBuffer() { destroy(); }
    ShmBuffer(const ShmBuffer &) = delete;
    ShmBuffer &operator=(const ShmBuffer &) = delete;

    bool create(size_t bytes)
    {
        destroy();
        fd_ = memfd_create("marcel_frame", MFD_CLOEXEC);
        if (fd_ < 0)
            return false;
        if (ftruncate(fd_, static_cast<off_t>(bytes)) != 0) {
            destroy();
            return false;
        }
        map_ = mmap(nullptr, bytes, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, 0);
        if (map_ == MAP_FAILED) {
            map_ = nullptr;
            destroy();
            return false;
        }
        size_ = bytes;
        return true;
    }

    bool valid() const { return fd_ >= 0 && map_; }
    int fd() const { return fd_; }
    void *map() { return map_; }
    size_t size() const { return size_; }

private:
    void destroy()
    {
        if (map_)
            munmap(map_, size_);
        if (fd_ >= 0)
            ::close(fd_);
        map_ = nullptr;
        fd_ = -1;
        size_ = 0;
    }

    int fd_ = -1;
    void *map_ = nullptr;
    size_t size_ = 0;
};
