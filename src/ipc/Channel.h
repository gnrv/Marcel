#pragma once

// socketpair(AF_UNIX, SOCK_SEQPACKET) wrapper used on both sides of the
// marcel <-> marcel_worker boundary. One datagram = one message (SEQPACKET
// preserves boundaries): MsgHeader + payload, with optional file descriptors
// via SCM_RIGHTS. No GL, no Cling — this is the whole of marcel_ipc.

#include "Protocol.h"

#include <cstddef>
#include <vector>

namespace ipc {

// A received message. Owns any passed file descriptors: whatever is still
// in fds when the Message is destroyed gets closed. Use takeFd() to assume
// ownership of one.
struct Message {
    MsgHeader header{};
    std::vector<uint8_t> payload;
    std::vector<int> fds;

    Message() = default;
    Message(const Message &) = delete;
    Message &operator=(const Message &) = delete;
    Message(Message &&other) noexcept { *this = static_cast<Message &&>(other); }
    Message &operator=(Message &&other) noexcept
    {
        if (this != &other) {
            closeFds();
            header = other.header;
            payload = static_cast<std::vector<uint8_t> &&>(other.payload);
            fds = static_cast<std::vector<int> &&>(other.fds);
            other.fds.clear();
        }
        return *this;
    }
    ~Message() { closeFds(); }

    // Transfers ownership of fds[i] to the caller; returns -1 if out of range.
    int takeFd(size_t i)
    {
        if (i >= fds.size())
            return -1;
        int fd = fds[i];
        fds[i] = -1;
        return fd;
    }

    void closeFds();
};

class Channel {
public:
    Channel() = default;
    explicit Channel(int fd) : fd_(fd) {}  // takes ownership
    Channel(const Channel &) = delete;
    Channel &operator=(const Channel &) = delete;
    Channel(Channel &&other) noexcept : fd_(other.fd_) { other.fd_ = -1; }
    Channel &operator=(Channel &&other) noexcept;
    ~Channel();

    // Connected SEQPACKET pair with send/recv buffers sized for
    // kMaxPayloadSize. Returns false on failure (errno set).
    static bool createPair(Channel &a, Channel &b);

    bool valid() const { return fd_ >= 0; }
    int fd() const { return fd_; }
    int releaseFd();  // caller assumes ownership (e.g. to pass across exec)
    void close();

    // Sends one datagram (blocking). Returns false if the peer is gone or
    // the message is oversized/invalid.
    bool send(MsgType type, const void *payload, uint32_t payload_size,
              const int *fds = nullptr, uint32_t num_fds = 0);

    enum class RecvResult { Message, WouldBlock, Disconnected };
    RecvResult recv(Message &out);          // non-blocking
    RecvResult recvBlocking(Message &out);  // blocks until message or peer death

    // poll() for readability (or peer death). timeout_ms < 0 = infinite.
    // Returns true when recv() will make progress.
    bool wait(int timeout_ms);

private:
    RecvResult recvInternal(Message &out, int flags);

    int fd_ = -1;
};

} // namespace ipc
