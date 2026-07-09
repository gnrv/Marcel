// marcel_snap: headless compile + render + screenshot driver for
// marcel_worker. It spawns the worker, sends a presentation's setup + slide
// sources, injects deterministic frame time, and writes each rendered frame
// to a PNG — closing a code->render->look loop for slide development (and CI)
// with zero Marcel rebuilds. Links marcel_ipc only: shm frame buffers are
// plain memory, so no GL or Cling here.
//
// Usage:
//   marcel_snap <presentation_dir> [options]
//     --slide N        only this slide (default: all slide0..9 with content)
//     --frames K        frames to render per slide (default 3)
//     --dt SEC          delta time per frame (default 1/60)
//     --time T0         time of the first frame (default 0)
//     --size WxH        design resolution (default 960x720; small = cheap PNGs)
//     --out DIR         directory for PNGs (default ".")
//     --worker PATH     marcel_worker path (default: alongside this binary)
//     --timeout SEC     overall wall-clock guard (default 120)
//
// Output PNGs: <out>/slideN_frameM.png. Diagnostics (compile results, error
// markers, exceptions) go to stdout; the worker inherits our stderr, so its
// own logging passes straight through.
//
// Exit codes: 0 ok, 2 usage, 3 spawn failure, 4 compile failure,
//             5 render exception, 6 worker death, 7 timeout.

#include "ipc/Channel.h"
#include "ipc/Protocol.h"
#include "ipc/Serialize.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image/stb_image_write.h"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include <fcntl.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {

enum ExitCode {
    kOk = 0,
    kUsage = 2,
    kSpawnFail = 3,
    kCompileFail = 4,
    kRenderException = 5,
    kWorkerDeath = 6,
    kTimeout = 7,
};

struct Options {
    std::string pres_dir;
    std::string out_dir = ".";
    std::string worker_path;
    int slide = -1; // -1 = all slides with content
    int frames = 3;
    double t0 = 0.0;
    float dt = 1.0f / 60.0f;
    uint32_t w = 960, h = 720;
    int timeout_sec = 120;
};

using Clock = std::chrono::steady_clock;

// SIGALRM backstop: if any single blocking wait outlives the overall timeout
// (a wedged worker that neither answers nor dies), tear the worker down and
// exit rather than hang the caller. Set once main knows the worker pid.
pid_t g_worker_pid = -1;

void onAlarm(int)
{
    if (g_worker_pid > 0)
        ::kill(g_worker_pid, SIGKILL);
    const char msg[] = "marcel_snap: hard timeout\n";
    ssize_t n = ::write(STDERR_FILENO, msg, sizeof(msg) - 1);
    (void)n;
    _exit(kTimeout);
}

// ---- source loading -------------------------------------------------------

// Returns file contents, or empty string if missing/unreadable. `exists` is
// set true only when the file opened (so a present-but-empty file is
// distinguishable from a missing one).
std::string readFile(const std::string &path, bool &exists)
{
    exists = false;
    FILE *f = fopen(path.c_str(), "rb");
    if (!f)
        return {};
    exists = true;
    std::string out;
    char buf[8192];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
        out.append(buf, n);
    fclose(f);
    return out;
}

bool isBlank(const std::string &s)
{
    for (char c : s)
        if (c != ' ' && c != '\t' && c != '\n' && c != '\r')
            return false;
    return true;
}

// ---- worker plumbing ------------------------------------------------------

std::string selfDir()
{
    char buf[4096];
    ssize_t n = ::readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n <= 0)
        return ".";
    buf[n] = '\0';
    std::string p(buf);
    size_t slash = p.find_last_of('/');
    return slash == std::string::npos ? "." : p.substr(0, slash);
}

pid_t spawnWorker(const std::string &worker, ipc::Channel &parent_end)
{
    ipc::Channel child_end;
    if (!ipc::Channel::createPair(parent_end, child_end))
        return -1;

    pid_t pid = fork();
    if (pid < 0)
        return -1;
    if (pid == 0) {
        // Child: put our socket end on the well-known fd (dup2 clears
        // CLOEXEC) and exec. Inherit stdout/stderr and env untouched, so the
        // worker's stderr and MARCEL_RS_BAG et al. flow through for free.
        if (dup2(child_end.fd(), ipc::kWorkerSocketFd) < 0)
            _exit(127);
        char fd_arg[32];
        snprintf(fd_arg, sizeof(fd_arg), "--ipc-fd=%d", ipc::kWorkerSocketFd);
        char *args[] = {const_cast<char *>(worker.c_str()), fd_arg, nullptr};
        execv(worker.c_str(), args);
        _exit(127);
    }
    child_end.close();
    return pid;
}

enum class Wait { Message, Disconnected, Timeout };

// Blocking recv with an overall deadline and worker-death detection. Polls in
// short slices so a worker that dies without closing the socket (rare, but
// possible mid-crash) is still noticed via waitpid.
Wait recvUntil(ipc::Channel &chan, pid_t worker, Clock::time_point deadline,
               ipc::Message &out)
{
    for (;;) {
        auto now = Clock::now();
        if (now >= deadline)
            return Wait::Timeout;
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                      deadline - now)
                      .count();
        int slice = ms < 200 ? static_cast<int>(ms) : 200;
        chan.wait(slice);
        auto r = chan.recv(out);
        if (r == ipc::Channel::RecvResult::Message)
            return Wait::Message;
        if (r == ipc::Channel::RecvResult::Disconnected)
            return Wait::Disconnected;
        // WouldBlock: confirm the worker is still alive before looping.
        int status = 0;
        if (waitpid(worker, &status, WNOHANG) == worker)
            return Wait::Disconnected;
    }
}

// ---- shm buffer tracking --------------------------------------------------

struct Slot {
    uint8_t *map = nullptr;
    size_t map_size = 0;
    uint32_t stride = 0, w = 0, h = 0;
};
// Keyed by (slide, buffer_index). memfds stay valid after worker death (our
// mapping keeps them alive), so slots never dangle mid-run.
std::map<std::pair<int, uint32_t>, Slot> g_slots;

void handleAnnounce(ipc::Message &m)
{
    if (m.payload.size() < sizeof(ipc::TextureAnnounceMsg))
        return;
    ipc::TextureAnnounceMsg ann{};
    std::memcpy(&ann, m.payload.data(), sizeof(ann));
    int fd = m.takeFd(0);
    if (fd < 0)
        return;
    if (ann.transport != ipc::kTransportShm) {
        // The tool only advertises shm; anything else is a protocol error.
        fprintf(stderr, "marcel_snap: unexpected transport %u in announce\n",
                ann.transport);
        ::close(fd);
        return;
    }
    Slot slot;
    slot.stride = ann.stride[0];
    slot.w = ann.width;
    slot.h = ann.height;
    slot.map_size = static_cast<size_t>(ann.stride[0]) * ann.height;
    void *p = mmap(nullptr, slot.map_size, PROT_READ, MAP_SHARED, fd, 0);
    ::close(fd);
    if (p == MAP_FAILED) {
        perror("marcel_snap: mmap");
        return;
    }
    slot.map = static_cast<uint8_t *>(p);
    auto key = std::make_pair(ann.slide, ann.buffer_index);
    auto it = g_slots.find(key);
    if (it != g_slots.end() && it->second.map)
        munmap(it->second.map, it->second.map_size);
    g_slots[key] = slot;
}

bool savePng(const std::string &path, const Slot &s)
{
    if (!s.map || s.h == 0)
        return false;
    // Buffer rows are bottom-up; point at the last row and step back with a
    // negative stride so the PNG comes out upright. RGBA8 (bytes R,G,B,A).
    const uint8_t *last = s.map + static_cast<size_t>(s.h - 1) * s.stride;
    return stbi_write_png(path.c_str(), static_cast<int>(s.w),
                          static_cast<int>(s.h), 4, last,
                          -static_cast<int>(s.stride)) != 0;
}

// ---- diagnostics ----------------------------------------------------------

// Prints one CompileResult's tail (value / exception / markers). Returns true
// if the source compiled cleanly.
bool reportCompile(const ipc::Message &m)
{
    ipc::Reader r(m.payload.data(), m.payload.size());
    ipc::CompileResultMsg res{};
    if (!r.read(res))
        return false;
    std::string value, exception;
    r.readString(value, res.value_len);
    r.readString(exception, res.exception_len);
    std::string stderr_text;
    r.readString(stderr_text, res.stderr_len);

    const char *who = res.slide < 0 ? "setup" : nullptr;
    char name[32];
    if (who)
        snprintf(name, sizeof(name), "setup");
    else
        snprintf(name, sizeof(name), "slide%d", res.slide);

    printf("[%s] compiled=%d syntax_error=%d has_function=%d\n", name,
           res.compiled, res.syntax_error, res.has_function);
    if (!value.empty())
        printf("    value: %s\n", value.c_str());
    if (!exception.empty())
        printf("    exception: %s\n", exception.c_str());
    for (uint32_t i = 0; i < res.num_markers; ++i) {
        ipc::ErrorMarkerWire mw{};
        if (!r.read(mw))
            break;
        std::string text;
        r.readString(text, mw.text_len);
        printf("    line %u: %s\n", mw.line, text.c_str());
    }
    return res.compiled && !res.syntax_error && exception.empty();
}

void usage()
{
    fprintf(stderr,
            "usage: marcel_snap <presentation_dir> [--slide N] [--frames K]\n"
            "                   [--dt SEC] [--time T0] [--size WxH] [--out DIR]\n"
            "                   [--worker PATH] [--timeout SEC]\n");
}

bool parseArgs(int argc, char **argv, Options &o)
{
    auto need = [&](int &i) -> const char * {
        if (i + 1 >= argc)
            return nullptr;
        return argv[++i];
    };
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--slide") {
            const char *v = need(i);
            if (!v)
                return false;
            o.slide = atoi(v);
        } else if (a == "--frames") {
            const char *v = need(i);
            if (!v)
                return false;
            o.frames = atoi(v);
        } else if (a == "--dt") {
            const char *v = need(i);
            if (!v)
                return false;
            o.dt = static_cast<float>(atof(v));
        } else if (a == "--time") {
            const char *v = need(i);
            if (!v)
                return false;
            o.t0 = atof(v);
        } else if (a == "--size") {
            const char *v = need(i);
            unsigned w = 0, h = 0;
            if (!v || sscanf(v, "%ux%u", &w, &h) != 2 || w == 0 || h == 0)
                return false;
            o.w = w;
            o.h = h;
        } else if (a == "--out") {
            const char *v = need(i);
            if (!v)
                return false;
            o.out_dir = v;
        } else if (a == "--worker") {
            const char *v = need(i);
            if (!v)
                return false;
            o.worker_path = v;
        } else if (a == "--timeout") {
            const char *v = need(i);
            if (!v)
                return false;
            o.timeout_sec = atoi(v);
        } else if (a == "-h" || a == "--help") {
            return false;
        } else if (!a.empty() && a[0] == '-') {
            fprintf(stderr, "marcel_snap: unknown option %s\n", a.c_str());
            return false;
        } else if (o.pres_dir.empty()) {
            o.pres_dir = a;
        } else {
            fprintf(stderr, "marcel_snap: unexpected argument %s\n", a.c_str());
            return false;
        }
    }
    if (o.pres_dir.empty() || o.frames < 1)
        return false;
    if (o.worker_path.empty())
        o.worker_path = selfDir() + "/marcel_worker";
    return true;
}

} // namespace

int main(int argc, char **argv)
{
    Options o;
    if (!parseArgs(argc, argv, o)) {
        usage();
        return kUsage;
    }
    mkdir(o.out_dir.c_str(), 0755); // ignore EEXIST

    // Which slides to send: -1 means scan slide0..9 for content.
    struct Src {
        int slide;
        std::string text;
    };
    std::vector<Src> slides;
    for (int i = 0; i < 10; ++i) {
        if (o.slide >= 0 && i != o.slide)
            continue;
        bool exists = false;
        std::string text = readFile(o.pres_dir + "/slide" + std::to_string(i) +
                                        ".cpp",
                                    exists);
        if (!exists) {
            if (o.slide >= 0)
                printf("[slide%d] missing — nothing to render\n", i);
            continue;
        }
        if (isBlank(text)) {
            printf("[slide%d] empty — skipped\n", i);
            continue;
        }
        slides.push_back({i, std::move(text)});
    }
    if (slides.empty()) {
        fprintf(stderr, "marcel_snap: no slides with content in %s\n",
                o.pres_dir.c_str());
        return kUsage;
    }

    bool setup_exists = false;
    std::string setup = readFile(o.pres_dir + "/setup.cpp", setup_exists);

    // Spawn the worker.
    ipc::Channel chan;
    pid_t worker = spawnWorker(o.worker_path, chan);
    if (worker < 0) {
        fprintf(stderr, "marcel_snap: failed to spawn %s\n",
                o.worker_path.c_str());
        return kSpawnFail;
    }
    g_worker_pid = worker;
    signal(SIGALRM, onAlarm);
    alarm(static_cast<unsigned>(o.timeout_sec) + 10);

    auto deadline = Clock::now() + std::chrono::seconds(o.timeout_sec);
    int exit_code = kOk;
    auto fail = [&](int code) {
        if (exit_code == kOk || code == kWorkerDeath || code == kTimeout)
            exit_code = code;
    };

    // Handshake: advertise shm only (no GL in this process).
    ipc::HelloMsg hello{};
    hello.protocol_version = ipc::kProtocolVersion;
    hello.transport_caps = ipc::kTransportShm;
    hello.dpi_scale = 1.0f;
    hello.design_w = o.w;
    hello.design_h = o.h;
    if (!chan.send(ipc::MsgType::Hello, &hello, sizeof(hello))) {
        fprintf(stderr, "marcel_snap: failed to send Hello\n");
        ::kill(worker, SIGKILL);
        waitpid(worker, nullptr, 0);
        return kWorkerDeath;
    }

    // A single recv helper that services async traffic (announces, log) and
    // returns the next message the caller actually cares about. Returns false
    // on worker death / timeout, having set the exit code.
    auto pump = [&](ipc::Message &m) -> bool {
        for (;;) {
            Wait w = recvUntil(chan, worker, deadline, m);
            if (w == Wait::Disconnected) {
                fprintf(stderr, "marcel_snap: worker died\n");
                fail(kWorkerDeath);
                return false;
            }
            if (w == Wait::Timeout) {
                fprintf(stderr, "marcel_snap: timed out after %ds\n",
                        o.timeout_sec);
                fail(kTimeout);
                return false;
            }
            if (m.header.type == ipc::MsgType::TextureAnnounce) {
                handleAnnounce(m);
                continue;
            }
            return true;
        }
    };

    ipc::Message m;
    if (!pump(m)) { // expect HelloAck
        ::kill(worker, SIGKILL);
        waitpid(worker, nullptr, 0);
        return exit_code;
    }
    if (m.header.type == ipc::MsgType::HelloAck &&
        m.payload.size() >= sizeof(ipc::HelloAckMsg)) {
        ipc::HelloAckMsg ack{};
        std::memcpy(&ack, m.payload.data(), sizeof(ack));
        printf("[worker] renderer='%s' transport=%s\n", ack.gl_renderer,
               ack.chosen_transport == ipc::kTransportShm ? "shm"
               : ack.chosen_transport == ipc::kTransportDmabuf ? "dmabuf"
                                                               : "none");
        if (ack.chosen_transport != ipc::kTransportShm) {
            fprintf(stderr,
                    "marcel_snap: worker did not choose shm (got 0x%x)\n",
                    ack.chosen_transport);
            ::kill(worker, SIGKILL);
            waitpid(worker, nullptr, 0);
            return kSpawnFail;
        }
    } else {
        fprintf(stderr, "marcel_snap: expected HelloAck, got %u\n",
                static_cast<uint32_t>(m.header.type));
        ::kill(worker, SIGKILL);
        waitpid(worker, nullptr, 0);
        return kSpawnFail;
    }

    // Compile: setup first (so its globals exist before slides), then slides.
    uint64_t req = 1;
    auto compile = [&](int slide, const std::string &text) -> bool {
        ipc::SetSourceMsg src{};
        src.slide = slide;
        src.request_id = req++;
        src.is_cuda = 0;
        src.text_len = static_cast<uint32_t>(text.size());
        ipc::Writer w;
        w.append(src);
        w.appendString(text);
        if (!chan.send(ipc::MsgType::SetSource, w.data(), w.size())) {
            fail(kWorkerDeath);
            return false;
        }
        for (;;) {
            if (!pump(m))
                return false;
            if (m.header.type == ipc::MsgType::CompileResult) {
                bool ok = reportCompile(m);
                if (!ok)
                    fail(kCompileFail);
                return true; // observed the result; success tracked via exit
            }
            // CompileBusy, LogText, etc. — keep pumping.
        }
    };

    if (setup_exists && !isBlank(setup)) {
        if (!compile(-1, setup)) {
            ::kill(worker, SIGKILL);
            waitpid(worker, nullptr, 0);
            return exit_code;
        }
    } else {
        printf("[setup] absent/empty — skipped\n");
    }
    for (const Src &s : slides) {
        if (!compile(s.slide, s.text)) {
            ::kill(worker, SIGKILL);
            waitpid(worker, nullptr, 0);
            return exit_code;
        }
    }

    // Render frames. Each FrameBegin carries a SlideInput per slide (visible,
    // not hovered → mouse ignored). FrameDone reports one result per slide;
    // we save each rendered buffer, then release it so the 3-slot ring never
    // starves across frames.
    for (int fr = 0; fr < o.frames; ++fr) {
        ipc::FrameBeginMsg fb{};
        fb.frame_id = static_cast<uint64_t>(fr) + 1;
        fb.time = o.t0 + static_cast<double>(fr) * o.dt;
        fb.delta_time = o.dt;
        fb.num_slides = static_cast<uint32_t>(slides.size());
        fb.num_events = 0;
        ipc::Writer w;
        w.append(fb);
        for (const Src &s : slides) {
            ipc::SlideInput in{};
            in.slide = s.slide;
            in.visible = 1;
            in.hovered = 0;
            in.focused = 0;
            in.mouse_x = in.mouse_y = std::nanf("");
            in.wheel_x = in.wheel_y = 0.f;
            w.append(in);
        }
        if (!chan.send(ipc::MsgType::FrameBegin, w.data(), w.size())) {
            fail(kWorkerDeath);
            break;
        }

        // Wait for the matching FrameDone (announces handled inside pump).
        bool got_done = false;
        while (!got_done) {
            if (!pump(m))
                break;
            if (m.header.type != ipc::MsgType::FrameDone)
                continue;
            ipc::Reader r(m.payload.data(), m.payload.size());
            ipc::FrameDoneMsg done{};
            if (!r.read(done))
                break;
            for (uint32_t i = 0; i < done.num_slides; ++i) {
                ipc::SlideFrameResult sr{};
                if (!r.read(sr))
                    break;
                std::string exception;
                r.readString(exception, sr.exception_len);
                if (!exception.empty()) {
                    printf("[slide%d frame%d] exception: %s\n", sr.slide, fr,
                           exception.c_str());
                    fail(kRenderException);
                }
                if (!sr.rendered)
                    continue;
                auto key = std::make_pair(sr.slide, sr.buffer_index);
                auto it = g_slots.find(key);
                if (it == g_slots.end()) {
                    printf("[slide%d frame%d] rendered but buffer %u "
                           "unannounced\n",
                           sr.slide, fr, sr.buffer_index);
                } else {
                    char path[512];
                    snprintf(path, sizeof(path), "%s/slide%d_frame%d.png",
                             o.out_dir.c_str(), sr.slide, fr);
                    if (savePng(path, it->second))
                        printf("[slide%d frame%d] wrote %s\n", sr.slide, fr,
                               path);
                    else
                        printf("[slide%d frame%d] PNG write failed\n", sr.slide,
                               fr);
                }
                // Return the buffer to the ring regardless of save outcome.
                ipc::BufferReleaseMsg rel{sr.slide, sr.buffer_index};
                chan.send(ipc::MsgType::BufferRelease, &rel, sizeof(rel));
            }
            got_done = true;
        }
        if (!got_done)
            break; // worker death / timeout already recorded
    }

    // Clean shutdown; fall back to a kill if the worker doesn't exit promptly.
    chan.send(ipc::MsgType::Shutdown, nullptr, 0);
    for (int i = 0; i < 50; ++i) {
        int status = 0;
        pid_t r = waitpid(worker, &status, WNOHANG);
        if (r == worker) {
            worker = -1;
            break;
        }
        usleep(20 * 1000);
    }
    if (worker > 0) {
        ::kill(worker, SIGKILL);
        waitpid(worker, nullptr, 0);
    }

    for (auto &[key, slot] : g_slots)
        if (slot.map)
            munmap(slot.map, slot.map_size);

    return exit_code;
}
