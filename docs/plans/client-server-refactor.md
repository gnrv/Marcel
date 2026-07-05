# Marcel Client–Server Refactor: Cling Worker Process

> Implementation plan for branch `topic/client-server-refactor`. Committed before any code changes (step 0); one commit per migration step below.

## Context

Marcel (formerly QuickTex) is an ImGui + MicroTeX presentation tool — "Manim but C++" — that uses the Cling C++ interpreter for live slide editing. Cling is unstable: asserts, JIT crashes, and hangs in user slide code take down the entire application, costing hours of lost work. This refactor was designed in April 2026 (recovered from VS Code Copilot chat "Inter-process texture sharing in OpenGL 3", session `f4a7aae3`, workspace storage `40796081c...`) and moves Cling into a supervised, restartable `marcel_worker` process. **Decisions locked in that session and reconfirmed now:** Unix domain sockets with binary structs + `SCM_RIGHTS` FD passing; GPU DMA-BUF zero-copy texture sharing (with a permanent SHM fallback for WSL2-class drivers); full scope through step 6 on branch `topic/client-server-refactor`.

**Critical fact the April plan missed (verified in exploration):** slides do NOT render to a texture today. Cling-JITted slide code makes live immediate-mode ImGui calls into the host's shared ImGui context (`src/main.cpp:1168`). The worker must therefore host its own headless GL + per-slide ImGui contexts, run slide lambdas against forwarded input, render into FBOs, and export those. There is currently **zero** EGL, FBO, `ImGui::Image`, or IPC code in the repo — all greenfield.

## Architecture

```
┌─ marcel (main) ──────────────┐      socketpair(AF_UNIX, SOCK_SEQPACKET)      ┌─ marcel_worker ─────────────────┐
│ GLFW window (EGL context)    │◄────── CompileResult / FrameDone / ───────────│ headless EGL + GL 3.0 context   │
│ editor, layouts, navigation  │        TextureAnnounce(+FDs) / Pong           │ cling::Interpreter               │
│ ImGui::Image(imported tex)   │─────── SetSource / FrameBegin(input) / ──────►│ per-slide ImGuiContext (shared  │
│ WorkerProcess supervisor:    │        BufferRelease / Ping                   │  ImFontAtlas), FBO ring ×3      │
│  spawn/heartbeat/restart     │                                               │ DMA-BUF or SHM export           │
└──────────────────────────────┘                                               └─────────────────────────────────┘
```

- **Main** keeps: editor, all three layouts (tabbed split, notebook, presentation mode), navigation, file management. Slides display as `ImGui::Image()` of the worker's latest frame. Never blocks on the worker — a stalled worker just means a stale (still-displayed) texture.
- **Worker** owns: Cling, syntax validation, stderr capture, slide execution, offscreen rendering. Crashes are expected and recoverable.
- **Target format (per presentation, user decision):** the protocol carries only a raw `design_w × design_h` in the `Hello` handshake — no format enum at the IPC level. Presets are UI-level convenience only: `16:10 → 1728×1080` (laptop-native, current default aspect, `src/main.cpp:834-836` TODO), `4:3 → 1440×1080` (iPad / future CRT-shader videos), `16:9 → 1920×1080` (YouTube), `9:16 → 1080×1920` (shorts). The design resolution is **fixed for the worker's lifetime**: changing it restarts the worker (reusing the supervisor's normal restart + state-resubmission path — cheap and simple). Worker renders at design resolution × dpi factor; main GPU-downscales into `slide_size`; textures are allocated and announced once, never realloc'd. Input translation is a single affine map into design-resolution coordinates.

## New files

### `src/ipc/` → static lib `marcel_ipc` (linked by both binaries; no GL, no Cling)
- **`Protocol.h`** — the protocol source of truth (below).
- **`Channel.h/.cpp`** — `socketpair(AF_UNIX, SOCK_SEQPACKET)` wrapper: `send(type, payload, fds)` via `sendmsg`+`SCM_RIGHTS`; non-blocking `poll()`/`recv()` → `{header, payload, fds}`. One datagram = one message (SEQPACKET preserves boundaries; enforce 1 MB max, bump `SO_SNDBUF`). Peer death via `POLLHUP`/`ECONNRESET`.
- **`Serialize.h`** — append/read helpers for variable-length tails (strings, marker arrays, per-slide blocks).

### `src/engine/` — Cling execution core extracted from main.cpp
- **`SlideEngine.h`** — async interface so in-process and remote impls are interchangeable: `setSource(slide /*-1=setup*/, text, is_cuda, request_id)`, `beginFrame(FrameInput)`, `drawSlide(slide)`, `drainEvents(sink)` (delivers compile results into `SourceFile` fields).
- **`ClingEngine.h/.cpp`** — code motion from main.cpp, verbatim where possible: `exprToString`/`findResultExprFromExtractionFunction` (71-166), `extractMarkers` (169-192), interpreter setup incl. include paths, header preloading, `AllowRedefinition`, CUDA args (375-458), setup compile (782-812), per-slide validate/compile/lambda-wrapper synthesis incl. `reevaluate`/`evaluate` closure (1014-1163), `CaptureStderr` (src/system/stdcapture.h, unchanged) around all interp calls.
- *(No `BakedEngine` — user decision: the `USE_CLING` flag and the `#ifndef USE_CLING` compiled-in fallback (main.cpp:670-711, `#include "setup.cpp"`, `slide_loaders`) are **removed**; Cling is required. The slide-file contract itself is unchanged.)*

### `src/worker/` — only in `marcel_worker`
- **`worker_main.cpp`** — parse `--ipc-fd=3`, send `HelloAck`, run `WorkerApp`. No signal heroics; crashing is allowed.
- **`WorkerApp.h/.cpp`** — owns `ClingEngine` + `Channel` + renderers. Two threads: *IO thread* (blocking recv; answers `Ping`→`Pong` even mid-compile; emits `CompileBusy` before dispatching a `SetSource`) and *work thread* (sole owner of GL + Cling; drains queue; compiles; renders; replies).
- **`HeadlessGL.h/.cpp`** — EGL init: try `EGL_PLATFORM_SURFACELESS_MESA`, fall back to default display + 1×1 pbuffer; `eglBindAPI(EGL_OPENGL_API)`; GL 3.0-compatible context permanently current on the work thread (setup code calls raw GL at compile time). `glfwInit()` with `GLFW_PLATFORM_NULL` so `glfwGetTime()` in user code works.
- **`SlideRenderer.h/.cpp`** — per slide: `ImGuiContext*` via `CreateContext(shared_atlas)` (vendored ImGui 1.91.7 supports it — verified imgui.h:327) + `ImPlotContext`/`ImPlot3DContext` + `ImGui::InitLatex()` per context; `io.IniFilename=nullptr`; input injected via `io.AddMousePosEvent/AddMouseButtonEvent/AddKeyEvent/AddInputCharacter`. 3-deep FBO/texture ring, states FREE→INFLIGHT→HELD. Frame: apply input → NewFrame → fullscreen borderless host window at design resolution → `PushFont` mirroring main.cpp:1010 → `ErrorRecoveryStoreState` + try/catch → invoke slide function → Render → `ImGui_ImplOpenGL3_RenderDrawData` into a FREE buffer → `glFinish()` (fence FDs in step 6).
- **`TextureExport.h/.cpp`** — `ExportBackend`: `DmabufExport` (`eglCreateImageKHR` from GL texture → `eglExportDMABUFImageQueryMESA`/`eglExportDMABUFImageMESA`; `TextureAnnounce`+FDs once per allocation) and `ShmExport` (memfd per buffer; `glReadPixels` per frame; announce once). Chosen at `Hello`/`HelloAck` capability negotiation.

### `src/supervisor/` — only in `marcel`
- **`WorkerProcess.h/.cpp`** — `socketpair`+`fork`+`execv(<exe_dir>/marcel_worker)`; child socket dup2'd to fd 3; child stderr → pipe drained into main's stderr **and** a ring buffer (crash forensics panel). `pump()` once per UI frame: `waitpid(WNOHANG)`, POLLHUP, heartbeat, restart state machine.
- **`RemoteEngine.h/.cpp`** — `SlideEngine` over IPC. Per frame: drain socket (CompileResult→SourceFile; TextureAnnounce→import; FrameDone→promote front buffers, queue `BufferRelease`); scan `Presentation` for `!compiled && !syntax_error && !compile_in_flight` → `SetSource`; at most **one outstanding `FrameBegin`** (mailbox pacing — coalesce input if the previous FrameDone is pending).
- **`TextureImport.h/.cpp`** — DMABUF import (`eglCreateImageKHR(EGL_LINUX_DMA_BUF_EXT)` + `glEGLImageTargetTexture2DOES` via `eglGetProcAddress`) and SHM upload (`mmap` + `glTexSubImage2D` on dirty). Imported textures survive worker death (dma-buf is kernel-refcounted) → stale-frame display during restart.
- **`SlideView.h/.cpp`** — replaces the slide-child body (main.cpp:1008-1185): `ImGui::Image(front_texture, slide_size)`; hover/focus; mouse translated to design-resolution slide-local coords; key/char/wheel collection for hovered/focused slide; exception text under slide; "compiling…" spinner and "worker restarting…" overlay.

### Shared helper
- **`src/render/UiFonts.h/.cpp`** — extracts the exact font-loading sequence (main.cpp:596-610: FiraSans + merged MDI icons + big/small/mono), parameterized by scale. **Load-bearing:** slides index the atlas directly (`documents/test/slide0.cpp:14` uses `Fonts[2]`) — `Fonts[]` order must be byte-identical in the worker. Also extract WSL2/dpi detection (main.cpp:490-524) into a `DpiInfo` helper.

### Changed files
- **`src/main.cpp`** — delete Cling includes (33-44) and all `#ifdef USE_CLING` blocks (moved to `src/engine/`); add `glfwWindowHint(GLFW_CONTEXT_CREATION_API, GLFW_EGL_CONTEXT_API)` near line 477 with GLX+SHM retry fallback; delete the `#ifndef USE_CLING` fallback blocks (62-65-ish `setup.cpp` include, 670-711 `slide_loaders`); construct a `SlideEngine` (`RemoteEngine`; `ClingEngine` in-process during migration); `supervisor.pump()` + `drainEvents()` after `glfwPollEvents()` (722); slide-child body (1008-1185) → `slide_view.draw(...)`. Layouts, navigation, toolbar, menus, final render/swap untouched.
- **`src/slides/Presentation.h`** — remove `cling::Transaction* last_transaction` + forward decl (16-18, 31); add `uint64_t compile_request_id{0}; bool compile_in_flight{false};` and a presentation-level `TargetFormat` (aspect + design resolution, persisted with settings). Worker-side per-slide state moves into a `WorkerSlide` struct in `ClingEngine`.
- **`test/`** — add `test/ipc_test.cpp` (loopback framing, SCM_RIGHTS FD passing, large payloads, POLLHUP on peer death).

## CMake changes

```cmake
add_library(marcel_ipc STATIC src/ipc/Channel.cpp)          # always built
# The USE_CLING option is REMOVED (user decision): Cling is required, no fallback build.
# All #ifdef USE_CLING blocks in source become unconditional; #ifndef blocks are deleted.
# marcel_lib additionally globs src/supervisor/*.cpp, src/render/*.cpp
# LIB_LINK += marcel_ipc EGL

add_executable(marcel_worker
        src/worker/worker_main.cpp src/worker/WorkerApp.cpp
        src/worker/HeadlessGL.cpp  src/worker/SlideRenderer.cpp
        src/worker/TextureExport.cpp src/engine/ClingEngine.cpp
        src/render/UiFonts.cpp)
    # Cling include dirs (mirror CMakeLists.txt:133-135) + link
    # clingInterpreter clingMetaProcessor clingUtils + imgui GL EGL glfw fmt
    # MOVE the -Wl,--export-dynamic-symbol=*cling*... flags (CMakeLists.txt:156-164)
    # from marcel to marcel_worker — the JIT lives here now.
    add_dependencies(marcel marcel_worker)
endif()
```

End state (step 6): `marcel` drops the Cling libraries and export-dynamic flags entirely — no LLVM in the UI process, which also sidesteps the WSL2 GL-driver symbol-clash noted at CMakeLists.txt:163.

## IPC protocol (`src/ipc/Protocol.h`)

```cpp
namespace ipc {
constexpr uint32_t kProtocolVersion = 1;
constexpr int      kWorkerSocketFd  = 3;
constexpr uint32_t kBuffersPerSlide = 3;
constexpr uint32_t kTransportDmabuf = 1u<<0, kTransportShm = 1u<<1;

enum class MsgType : uint32_t {
  // main -> worker
  Hello=1, SetSource, FrameBegin, BufferRelease, ClipboardData, Ping, Shutdown,
  // worker -> main
  HelloAck=100, CompileBusy, CompileResult, TextureAnnounce,
  FrameDone, ClipboardRequest, Pong, LogText,
};
struct MsgHeader { MsgType type; uint32_t payload_size; };

struct HelloMsg     { uint32_t protocol_version, transport_caps; float dpi_scale;
                      uint32_t design_w, design_h; };  // raw pixels; fixed for worker lifetime.
                      // UI presets map onto this: 16:10=1728x1080 (default), 4:3=1440x1080,
                      // 16:9=1920x1080, 9:16=1080x1920. Resolution change = worker restart.
struct HelloAckMsg  { uint32_t protocol_version, transport_caps, chosen_transport;
                      char gl_renderer[128]; };
struct SetSourceMsg { int32_t slide /*-1=setup*/; uint64_t request_id; uint8_t is_cuda;
                      uint32_t text_len; /* char text[] */ };

struct SlideInput { int32_t slide; uint8_t visible, hovered, focused;
                    float mouse_x, mouse_y;   // design-res local px; NaN if not hovered
                    float wheel_x, wheel_y; };
struct InputEvent { uint32_t kind /*MouseButton,Key,Char,FocusLost*/;
                    int32_t slide, a, b; float f; };
struct FrameBeginMsg { uint64_t frame_id; double time; float delta_time;
                       uint32_t num_slides, num_events;
                       /* SlideInput[]; InputEvent[] */ };
struct BufferReleaseMsg { int32_t slide; uint32_t buffer_index; };

struct CompileBusyMsg  { int32_t slide; uint64_t request_id; };  // IO thread, pre-process()
struct ErrorMarkerWire { uint32_t line, text_len; /* char[] */ };
struct CompileResultMsg { int32_t slide; uint64_t request_id;
  uint8_t validated, compiled, syntax_error, has_function;
  uint32_t value_len, exception_len, stderr_len, num_markers;
  /* char value[]; char exception[]; char stderr_text[]; ErrorMarkerWire[] */ };

struct TextureAnnounceMsg {  // + plane FDs (dmabuf) or 1 memfd (shm) via SCM_RIGHTS
  int32_t slide; uint32_t buffer_index, transport, width, height,
  fourcc /*DRM_FORMAT_ABGR8888*/; uint64_t modifier;
  uint32_t num_planes, stride[4], offset[4]; };

struct SlideFrameResult { int32_t slide; uint32_t buffer_index;
  uint8_t rendered, want_capture_mouse, want_capture_keyboard, want_text_input;
  uint32_t exception_len; /* char[] */ };
struct FrameDoneMsg { uint64_t frame_id; uint32_t num_slides; /* SlideFrameResult[] */ };

struct PingMsg { uint64_t token; }; struct PongMsg { uint64_t token; };
} // namespace ipc
```

**Frame loop.** Main, per UI frame: (1) `pump()` — waitpid/drain socket; FrameDone promotes each slide's new front buffer, previous front becomes releasable. (2) If no FrameBegin outstanding, gather visibility/hover/mouse/events → send; else coalesce. (3) Draw UI with current front textures (stale-tolerant). (4) After `glfwSwapBuffers`, send queued `BufferRelease`s (dma-buf implicit sync covers the in-flight sampling; strict fence sync in step 6).
**Worker, per FrameBegin:** for each visible slide with a FREE buffer: inject input → NewFrame → run lambda (try/catch + ErrorRecovery) → Render into FBO; buffers are allocated + `TextureAnnounce`d once, on a slide's first render (design resolution is fixed for the worker's lifetime); `glFinish()`; `FrameDone`. No FREE buffer → skip slide (main shows front; no tearing possible). Version mismatch at Hello → "worker binary out of date", no retry loop.

## Watchdog

Liveness, cheapest first: **(1)** process exit — `waitpid(WNOHANG)` per frame + POLLHUP → immediate. **(2)** hard hang — `Ping` every 500 ms answered by the IO thread; no Pong for 2 s. **(3)** work-thread hang (infinite loop in slide code) — no FrameDone for 1 s while a FrameBegin is outstanding; **suppressed while `CompileBusy` is outstanding** (compiles legitimately take seconds → 30 s timeout + always-available "Restart worker" toolbar button).

Restart: SIGTERM → 500 ms → SIGKILL → waitpid → close socket → **keep imported textures** (slides display last-good frame + dimmed "restarting worker…" overlay). **Poisoned source:** if a `CompileBusy{S}` was outstanding at death, mark S `syntax_error` with marker "This code crashed the interpreter (worker restarted)" + stderr tail; editing S clears it. Respawn with backoff 0/1/2/4…10 s; >5 crashes in 30 s → stop, show crash panel with stderr ring. After handshake (`Hello` carries the design resolution): `SetSource(setup)`, `SetSource(slide0..9)` skipping poisoned; resume FrameBegin after setup's CompileResult. Editor stays fully live throughout.

## Migration order (each step builds & runs; commit per step)

- **Step 0** — commit this plan to `docs/plans/`; extract `UiFonts` + `DpiInfo` (no behavior change).
- **Step 1** — require Cling + engine extraction: remove the `USE_CLING` CMake option and all `#ifdef`/`#ifndef USE_CLING` blocks (fallback path deleted); then extract `SlideEngine` + `ClingEngine` (in-process); main.cpp loses all direct Cling code; behavior identical for Cling builds (pure code motion + dead-code deletion).
- **Step 2** — `marcel_ipc` + `test/ipc_test.cpp`; app untouched.
- **Step 3a** — worker binary compile-only: spawn + `SetSource`/`CompileResult`; rendering still in-process via `--engine=inproc` default. Proves protocol + supervision.
- **Step 3b** — remote rendering over SHM: HeadlessGL, per-slide contexts, FBO ring, ShmExport, SlideView + input forwarding; flip default to `--engine=remote`.
- **Step 4** — DMA-BUF zero-copy: EGL context in main (GLX+SHM fallback), Dmabuf export/import, transport negotiation.
- **Step 5** — watchdog hardening: heartbeat, dual hang timeouts, backoff, poisoned-source, resubmission, overlays, crash panel.
- **Step 6** — cleanup & polish: remove Cling link + inproc engine from `marcel`; fence-FD sync (`EGL_ANDROID_native_fence_sync`) replacing `glFinish`; clipboard proxy (`ClipboardRequest`/`ClipboardData`).

## Verification

- **Unit:** `test/ipc_test.cpp` — framing, FD passing, large payloads, POLLHUP.
- **End-to-end after 3b and again after 4/5:** build both targets; open `documents/test`; verify slides render + `slide0.cpp` slider/plot interaction works through input forwarding.
- **Crash resilience (the point of it all):** slide containing `*(volatile int*)0 = 0;` → worker dies, UI stays up, slide marked poisoned, other slides keep rendering; fix the line → auto-recompile.
- **Hang resilience:** slide with `while(true){}` in the update lambda → FrameDone timeout → auto-restart; UI responsive throughout. Compile-time hang (template bomb) → spinner + 30 s timeout + manual restart button.
- **Restart storm:** crash >5× in 30 s → supervisor stops, crash panel shows stderr.
- **Transport:** force `--transport=shm` and verify identical rendering; check WSL2 path if available.
- **Formats:** switch presentation target 16:10 → 4:3 → 9:16 (worker restarts with new `Hello` design resolution); aspect correct, input still lands, old textures replaced after the new announce.

## Risks & mitigations

1. **WSL2 DMA-BUF flakiness** (d3d12/dozen often lacks `EGL_MESA_image_dma_buf_export`) → capability negotiation; SHM is a permanent first-class fallback (user-confirmed).
2. **GLX→EGL switch of main window** (required for EGLImage import; NVIDIA/X11 quirks) → try EGL, on failure recreate with GLX and force SHM. `GLFW_EXPOSE_NATIVE_X11`/nfd unaffected.
3. **Interactive widgets in slides** (slider in slide0, plot pan/zoom) → one frame extra latency inherent; continuous mouse pos + discrete button events; drag-owning slide keeps input while a button is held; `want_capture_mouse` gates main's scroll/keys (main.cpp:908-916).
4. **Font atlas index mismatch** → shared `UiFonts` loader; slides depend on `Fonts[2]`.
5. **Multi-context libs** → ImPlot/ImPlot3D `SetImGuiContext()` + per-slide contexts; `InitLatex()` per context; verify the vendored `PushScale` patch is per-context during 3b.
6. **FBO on "GL 3.0"** → FBOs are core in 3.0; `glEGLImageTargetTexture2DOES` via `eglGetProcAddress`.
7. **stdcapture deadlock history** → capture is worker-side only now; a deadlock becomes a Pong timeout → auto-restart. Net win.
8. **Error-marker latency** → markers arrive async with CompileResult; today the UI freezes during compiles, afterwards it shows a spinner — net UX improvement.
9. **Setup code doing GL/ImGui at compile time** → worker GL context permanently current; designated "setup" ImGui context current around `interp.process(setup)`.
10. **Clipboard in worker-side InputText** → step 6 proxy; known gap until then.

## NOT changing

`src/editor/*`, TextEditor, `src/search/*`; `Presentation`/`SourceFile` file management (paths, save/reload/dirty, 1 setup + 10 slides); the three layouts and navigation (main.cpp:758-1005 except the innermost slide-child body); the slide-file contract (top-level statics + returned `update` lambda); `src/system/stdcapture.h` internals; MicroTeX/imgui/implot submodules; the Cling build in `external/root-project`.
