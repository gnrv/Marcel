#pragma once

// Append/read helpers for the variable-length payload tails described in
// Protocol.h: a trivially-copyable fixed struct followed by strings and
// repeated sub-blocks. Writer builds the payload; Reader walks it with
// bounds checking — a short or corrupt payload flips ok() to false and all
// subsequent reads become no-ops (never reads past the end).

#include <cstdint>
#include <cstring>
#include <string>
#include <type_traits>
#include <vector>

namespace ipc {

class Writer {
public:
    template <typename T>
    void append(const T &v)
    {
        static_assert(std::is_trivially_copyable_v<T>);
        appendBytes(&v, sizeof(T));
    }

    void appendBytes(const void *data, size_t size)
    {
        const uint8_t *p = static_cast<const uint8_t *>(data);
        buf_.insert(buf_.end(), p, p + size);
    }

    void appendString(const std::string &s) { appendBytes(s.data(), s.size()); }

    const uint8_t *data() const { return buf_.data(); }
    uint32_t size() const { return static_cast<uint32_t>(buf_.size()); }
    void clear() { buf_.clear(); }

private:
    std::vector<uint8_t> buf_;
};

class Reader {
public:
    Reader(const void *data, size_t size)
        : p_(static_cast<const uint8_t *>(data)), end_(p_ + size)
    {
    }

    template <typename T>
    bool read(T &v)
    {
        static_assert(std::is_trivially_copyable_v<T>);
        return readBytes(&v, sizeof(T));
    }

    bool readBytes(void *out, size_t size)
    {
        if (!ok_ || size > remaining()) {
            ok_ = false;
            return false;
        }
        std::memcpy(out, p_, size);
        p_ += size;
        return true;
    }

    bool readString(std::string &out, uint32_t len)
    {
        if (!ok_ || len > remaining()) {
            ok_ = false;
            return false;
        }
        out.assign(reinterpret_cast<const char *>(p_), len);
        p_ += len;
        return true;
    }

    bool ok() const { return ok_; }
    size_t remaining() const { return static_cast<size_t>(end_ - p_); }

private:
    const uint8_t *p_;
    const uint8_t *end_;
    bool ok_ = true;
};

} // namespace ipc
