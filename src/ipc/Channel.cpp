#include "Channel.h"

#include <cerrno>
#include <cstring>

#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace ipc {

void Message::closeFds()
{
    for (int fd : fds)
        if (fd >= 0)
            ::close(fd);
    fds.clear();
}

Channel &Channel::operator=(Channel &&other) noexcept
{
    if (this != &other) {
        close();
        fd_ = other.fd_;
        other.fd_ = -1;
    }
    return *this;
}

Channel::~Channel()
{
    close();
}

void Channel::close()
{
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

int Channel::releaseFd()
{
    int fd = fd_;
    fd_ = -1;
    return fd;
}

bool Channel::createPair(Channel &a, Channel &b)
{
    int sv[2];
    if (socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, sv) != 0)
        return false;
    // Best effort: make sure a max-sized datagram fits in the socket buffers.
    // The kernel clamps to net.core.{w,r}mem_max; kMaxPayloadSize is chosen
    // to stay under the stock ceiling (see Protocol.h).
    int buf_size = static_cast<int>(kMaxPayloadSize + 8 * 1024);
    for (int fd : sv) {
        setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &buf_size, sizeof(buf_size));
        setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &buf_size, sizeof(buf_size));
    }
    a = Channel(sv[0]);
    b = Channel(sv[1]);
    return true;
}

bool Channel::send(MsgType type, const void *payload, uint32_t payload_size,
                   const int *fds, uint32_t num_fds)
{
    if (fd_ < 0 || payload_size > kMaxPayloadSize || num_fds > kMaxFds)
        return false;

    MsgHeader header{type, payload_size};
    iovec iov[2];
    iov[0].iov_base = &header;
    iov[0].iov_len = sizeof(header);
    iov[1].iov_base = const_cast<void *>(payload);
    iov[1].iov_len = payload_size;

    msghdr msg{};
    msg.msg_iov = iov;
    msg.msg_iovlen = payload_size > 0 ? 2 : 1;

    char cmsg_buf[CMSG_SPACE(sizeof(int) * kMaxFds)];
    if (num_fds > 0) {
        std::memset(cmsg_buf, 0, sizeof(cmsg_buf));
        msg.msg_control = cmsg_buf;
        msg.msg_controllen = CMSG_SPACE(sizeof(int) * num_fds);
        cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
        cmsg->cmsg_level = SOL_SOCKET;
        cmsg->cmsg_type = SCM_RIGHTS;
        cmsg->cmsg_len = CMSG_LEN(sizeof(int) * num_fds);
        std::memcpy(CMSG_DATA(cmsg), fds, sizeof(int) * num_fds);
    }

    for (;;) {
        ssize_t n = sendmsg(fd_, &msg, MSG_NOSIGNAL);
        if (n >= 0)
            return static_cast<size_t>(n) == sizeof(header) + payload_size;
        if (errno == EINTR)
            continue;
        return false;  // EPIPE/ECONNRESET: peer is gone
    }
}

Channel::RecvResult Channel::recv(Message &out)
{
    return recvInternal(out, MSG_DONTWAIT);
}

Channel::RecvResult Channel::recvBlocking(Message &out)
{
    return recvInternal(out, 0);
}

Channel::RecvResult Channel::recvInternal(Message &out, int flags)
{
    if (fd_ < 0)
        return RecvResult::Disconnected;

    out.payload.resize(sizeof(MsgHeader) + kMaxPayloadSize);
    out.closeFds();

    iovec iov;
    iov.iov_base = out.payload.data();
    iov.iov_len = out.payload.size();

    char cmsg_buf[CMSG_SPACE(sizeof(int) * kMaxFds)];
    msghdr msg{};
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = cmsg_buf;
    msg.msg_controllen = sizeof(cmsg_buf);

    ssize_t n;
    for (;;) {
        n = recvmsg(fd_, &msg, flags | MSG_CMSG_CLOEXEC);
        if (n >= 0)
            break;
        if (errno == EINTR)
            continue;
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return RecvResult::WouldBlock;
        return RecvResult::Disconnected;
    }
    if (n == 0)  // SEQPACKET EOF: peer closed
        return RecvResult::Disconnected;

    // Collect passed fds first so they get closed even on a malformed message.
    for (cmsghdr *cmsg = CMSG_FIRSTHDR(&msg); cmsg; cmsg = CMSG_NXTHDR(&msg, cmsg)) {
        if (cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SCM_RIGHTS) {
            size_t count = (cmsg->cmsg_len - CMSG_LEN(0)) / sizeof(int);
            const int *cfds = reinterpret_cast<const int *>(CMSG_DATA(cmsg));
            for (size_t i = 0; i < count; ++i)
                out.fds.push_back(cfds[i]);
        }
    }

    if (static_cast<size_t>(n) < sizeof(MsgHeader) || (msg.msg_flags & MSG_TRUNC))
        return RecvResult::Disconnected;  // framing broken: treat as fatal

    std::memcpy(&out.header, out.payload.data(), sizeof(MsgHeader));
    size_t payload_size = static_cast<size_t>(n) - sizeof(MsgHeader);
    if (out.header.payload_size != payload_size)
        return RecvResult::Disconnected;

    out.payload.erase(out.payload.begin(), out.payload.begin() + sizeof(MsgHeader));
    out.payload.resize(payload_size);
    return RecvResult::Message;
}

bool Channel::wait(int timeout_ms)
{
    if (fd_ < 0)
        return true;  // recv() will report Disconnected
    pollfd pfd{fd_, POLLIN, 0};
    int r = ::poll(&pfd, 1, timeout_ms);
    return r > 0 && (pfd.revents & (POLLIN | POLLHUP | POLLERR)) != 0;
}

} // namespace ipc
