#pragma once

// IPC protocol between marcel (main/UI process) and marcel_worker (Cling +
// offscreen rendering). Source of truth: docs/plans/client-server-refactor.md.
//
// Transport: socketpair(AF_UNIX, SOCK_SEQPACKET). One datagram = one message:
// MsgHeader immediately followed by payload_size bytes of payload. File
// descriptors (dma-buf planes or memfds) ride along via SCM_RIGHTS.
//
// All payload structs are trivially copyable and sent raw — both ends are
// the same binary architecture (fork/exec of a sibling executable), so no
// endianness or padding negotiation is needed. Variable-length data (source
// text, error strings, per-slide arrays) follows the fixed struct as a tail;
// see Serialize.h for the append/read helpers.

#include <cstdint>
#include <type_traits>

namespace ipc {

constexpr uint32_t kProtocolVersion = 1;
constexpr int      kWorkerSocketFd  = 3;
constexpr uint32_t kBuffersPerSlide = 3;

// Largest datagram we ever send. AF_UNIX datagram size is bounded by the
// socket send buffer, whose unprivileged ceiling (net.core.wmem_max) is
// ~208 KB on stock Ubuntu — stay safely under it. Frame pixels never travel
// inline (dma-buf/memfd), so real payloads are source text and diagnostics;
// oversized stderr gets truncated by the sender.
constexpr uint32_t kMaxPayloadSize = 192 * 1024;
constexpr uint32_t kMaxFds = 4;  // max dma-buf planes per TextureAnnounce

// Clipboard text cap: comfortably under kMaxPayloadSize; anything larger
// gets truncated by the sender (either side).
constexpr uint32_t kClipboardMax = 128 * 1024;

// Transport capability bits (HelloMsg/HelloAckMsg).
constexpr uint32_t kTransportDmabuf = 1u << 0;
constexpr uint32_t kTransportShm    = 1u << 1;
// Feature bit in the same caps word: main can GPU-wait on native-fence FDs
// (EGL_ANDROID_native_fence_sync). When set and the worker can export
// fences, FrameDone carries one fence FD instead of the worker stalling in
// glFinish. dma-buf transport only; shm copies are synchronous anyway.
constexpr uint32_t kCapFenceSync    = 1u << 2;

// DRM_FORMAT_ABGR8888 ('AB24' little-endian): bytes R,G,B,A — matches what
// glReadPixels(GL_RGBA, GL_UNSIGNED_BYTE) writes and glTexSubImage2D reads.
constexpr uint32_t kFourccAbgr8888 = 0x34324241;

enum class MsgType : uint32_t {
    // main -> worker (ClipboardData travels both directions)
    Hello = 1, SetSource, FrameBegin, BufferRelease, ClipboardData, Ping, Shutdown,
    // worker -> main
    HelloAck = 100, CompileBusy, CompileResult, TextureAnnounce,
    FrameDone, ClipboardRequest, Pong, LogText,
};

struct MsgHeader { MsgType type; uint32_t payload_size; };

struct HelloMsg {
    uint32_t protocol_version, transport_caps;
    float dpi_scale;
    // Raw design resolution in pixels; fixed for the worker's lifetime.
    // UI presets map onto this: 16:10=1728x1080 (default), 4:3=1440x1080,
    // 16:9=1920x1080, 9:16=1080x1920. Resolution change = worker restart.
    uint32_t design_w, design_h;
};
struct HelloAckMsg {
    uint32_t protocol_version, transport_caps, chosen_transport;
    char gl_renderer[128];
};

struct SetSourceMsg {
    int32_t slide;  // -1 = setup
    uint64_t request_id;
    uint8_t is_cuda;
    uint32_t text_len;
    /* tail: char text[text_len] */
};

struct SlideInput {
    int32_t slide;
    uint8_t visible, hovered, focused;
    float mouse_x, mouse_y;  // design-res slide-local px; NaN if not hovered
    float wheel_x, wheel_y;
};
enum class InputEventKind : uint32_t { MouseButton = 1, Key, Char, FocusLost };
struct InputEvent {
    uint32_t kind;  // InputEventKind
    int32_t slide, a, b;
    float f;
};
struct FrameBeginMsg {
    uint64_t frame_id;
    double time;
    float delta_time;
    uint32_t num_slides, num_events;
    /* tail: SlideInput[num_slides]; InputEvent[num_events] */
};
struct BufferReleaseMsg { int32_t slide; uint32_t buffer_index; };

// Sent by the worker's IO thread before the work thread starts a compile, so
// the supervisor can suppress the frame-hang watchdog during long compiles.
struct CompileBusyMsg { int32_t slide; uint64_t request_id; };

struct ErrorMarkerWire { uint32_t line, text_len; /* tail: char[text_len] */ };
struct CompileResultMsg {
    int32_t slide;
    uint64_t request_id;
    uint8_t validated, compiled, syntax_error, has_function;
    uint32_t value_len, exception_len, stderr_len, num_markers;
    /* tail: char value[]; char exception[]; char stderr_text[];
             ErrorMarkerWire[num_markers] (each with its own text tail) */
};

// Accompanied by plane FDs (dmabuf) or one memfd (shm) via SCM_RIGHTS.
struct TextureAnnounceMsg {
    int32_t slide;
    uint32_t buffer_index, transport, width, height;
    uint32_t fourcc;    // DRM_FORMAT_ABGR8888
    uint64_t modifier;
    uint32_t num_planes, stride[4], offset[4];
};

struct SlideFrameResult {
    int32_t slide;
    uint32_t buffer_index;
    uint8_t rendered, want_capture_mouse, want_capture_keyboard, want_text_input;
    uint32_t exception_len;
    /* tail: char exception[] */
};
struct FrameDoneMsg {
    uint64_t frame_id;
    uint32_t num_slides;
    /* tail: SlideFrameResult[num_slides] (each with its own text tail).
       May carry one native-fence FD via SCM_RIGHTS (kCapFenceSync): the
       GPU work for every slide rendered this frame has completed when it
       signals. Absent when nothing rendered or the fallback glFinish ran. */
};

// Clipboard proxy: the worker has no window system, so slides read from a
// worker-local cache and writes are forwarded. Main sends ClipboardData
// (its current clipboard) when a slide gains focus and in answer to
// ClipboardRequest; the worker sends ClipboardData back when slide code
// copies text. ClipboardRequest (empty payload) is fired by the worker on
// every paste — that paste uses the cache as-is, and the reply refreshes it
// for the next one.
struct ClipboardDataMsg { uint32_t text_len; /* tail: char text[text_len] */ };

struct PingMsg { uint64_t token; };
struct PongMsg { uint64_t token; };

static_assert(std::is_trivially_copyable_v<MsgHeader>);
static_assert(std::is_trivially_copyable_v<HelloMsg>);
static_assert(std::is_trivially_copyable_v<HelloAckMsg>);
static_assert(std::is_trivially_copyable_v<SetSourceMsg>);
static_assert(std::is_trivially_copyable_v<FrameBeginMsg>);
static_assert(std::is_trivially_copyable_v<CompileResultMsg>);
static_assert(std::is_trivially_copyable_v<TextureAnnounceMsg>);
static_assert(std::is_trivially_copyable_v<FrameDoneMsg>);
static_assert(std::is_trivially_copyable_v<ClipboardDataMsg>);

} // namespace ipc
