# RealSense Pointcloud Demo + marcel_snap Headless Tool

> **Execution step 0:** copy this file into the repo as `docs/plans/realsense-snap.md` and commit it on the topic branch before any code changes; execute from that copy.

## Context

Marcel's client–server refactor is done: slides are interpreted C++ (Cling) running in the supervised `marcel_worker`, rendered offscreen and shipped to the UI over IPC. This plan builds on that in three parts:

1. **`documents/realsense/`** — a new presentation whose `setup.cpp` links librealsense2 and finds the connected camera (one is plugged in now), and whose `slide0.cpp` renders the live pointcloud in an ImPlot3D plot inside its update lambda, **each point colored from the color stream** (depth + color, UV-mapped). ImPlot3D is orthographic-only, which is the point of the demo: the cloud reads like a blueprint, not a photo. No perspective comparison — just the colored cloud in ImPlot3D.
2. **`marcel_snap`** — a headless CLI that lets Claude (and CI) compile + render + screenshot slides without building or launching the app: it spawns `marcel_worker`, sends sources, injects deterministic frame time, and dumps each rendered frame as PNG. This closes Claude's code→render→look loop: compile diagnostics with line markers on stdout, worker stderr passed through, PNGs readable with the Read tool.
3. **CLAUDE.md** — a "slide development workflow" section documenting the loop and the gdb recipe for worker debugging.

Key facts verified during exploration:
- **Linking needs no build changes**: the vendored Cling has ClingPragmas compiled in — `#pragma cling load("realsense2")` dlopens the system lib (v2.58.2) into the JIT. `/usr/include` is on Cling's default search path, so `#include <librealsense2/rs.hpp>` just works. So yes: Claude can already link additional libraries from slide/setup source.
- setup.cpp is `interp.process()`ed into the shared interpreter — its globals/pragmas persist and are visible to all slides. Slide contract: last statement `return update;` where `update` is the per-frame lambda.
- The worker injects `FrameBegin.time` into `ImGui::GetTime()` (SlideRenderer.cpp:185) — deterministic rendering under marcel_snap.
- shm transport: one memfd per ring slot, `kBuffersPerSlide = 3`, rows **bottom-up**, RGBA8, stride from TextureAnnounce. A tool must send `BufferRelease` after consuming each frame or the ring starves after 3 frames.
- `stb_image_write.h` is vendored at `external/stb_image/` (include path `external` is global) and currently unused — used by marcel_snap for PNG output.
- `SourceFile` auto-creates missing slide files (Presentation.cpp:16-19), so the presentation commits all 10 slide files (slide1–9 empty, like `documents/test`); empty slides are skipped by the worker (`has_content` check, WorkerApp.cpp:275).
- ImPlot3D v0.1 has no public rotation setter; its initial rotation is a pleasant oblique view (implot3d.cpp:79) — fine for "vanilla". `SetupBoxScale` gives equal-meters axes.
- `PlotScatter` takes ONE color per call — but `PlotToPixels(x,y,z)` (implot3d.h:409) and `GetPlotDrawList()` (:425) are public, so per-point RGB is done by projecting each point ourselves and emitting a small `AddRectFilled` per point into the plot's draw list (2 tris/point; 32-bit ImDrawIdx already configured; no depth sort — fine for an ortho cloud).
- Worker cwd is the exe dir, not the presentation folder — any data paths in slides must be absolute.
- 1s frame watchdog in the real app → the update lambda must use non-blocking `rs2::pipeline::poll_for_frames()`, never `wait_for_frames()`.

Branch: new topic branch `topic/realsense-snap` off master, one commit per step, rebase + ff to land (repo never merges). Never stage `documents/test/slide*.cpp`. First commit: this plan as `docs/plans/realsense-snap.md` (step 0 above).

## Step 1 — `marcel_snap` tool

**New file `src/tools/snap_main.cpp`** (single file, ~450 lines, links `marcel_ipc` only — no GL, no Cling; shm buffers are plain memory). Mirrors the scratchpad driver pattern (fence_probe et al.) but productized:

- **argv**: `marcel_snap <presentation_dir> [--slide N] [--frames K=3] [--dt 1/60] [--time T0=0] [--out DIR=.] [--worker PATH] [--timeout SEC=120] [--size WxH]`. Default worker path: `dirname(/proc/self/exe)/marcel_worker`. Default size **960×720** — deliberately small so the PNGs cost few tokens when Claude Reads them (plenty of pixels to judge a render; the worker scales slide content by short-side/1080, SlideRenderer.cpp:197, so any size works). `--size 1728x1080` for app-fidelity checks.
- **Spawn**: mirror `WorkerProcess::spawn` (src/supervisor/WorkerProcess.cpp:22-71) minus the supervisor: `ipc::Channel::createPair`, fork, child `dup2(fd, 3)`, `execv(worker, {"marcel_worker","--ipc-fd=3"})`. Do **not** pipe stderr — the child inherits the tool's stderr, giving passthrough for free. Env (e.g. `MARCEL_RS_BAG`) inherits automatically.
- **Handshake**: `Hello{kProtocolVersion, kTransportShm /*shm only*/, 1.0f, w, h}` → print `HelloAck.gl_renderer`.
- **Compile**: read `setup.cpp` + `slideN.cpp` (skip missing/empty with a note; never create files). `SetSource(-1, req, setup)` then per slide; print every `CompileResult` — compiled/syntax_error flags, error markers as `line N: msg`, value/exception/stderr tails (walk the variable tail with `ipc::Reader`, same layout as WorkerApp.cpp:192-214). Setup compile failure: report, continue, remember for exit code.
- **Render**: for m in 0..K-1 send `FrameBegin{frame_id, time=t0+m*dt, dt, num_slides, 0 events}` + `SlideInput{slide, visible=1}` per slide; recv until matching `FrameDone` (handling `TextureAnnounce` → mmap memfd PROT_READ, and `LogText` en route). Per rendered slide: write `out/slideN_frameM.png` with `stbi_write_png(..., map + (h-1)*stride, -stride)` (bottom-up flip), then send `BufferRelease{slide, buffer_index}` — **mandatory**, 3-slot ring.
- **Robustness**: every recv goes through a deadline helper (`chan.wait(≤200ms)` + `waitpid(WNOHANG)` loop → detect worker death / overall `--timeout`); `alarm(timeout+10)` as hard backstop. No per-compile watchdog — that's the point (rs.hpp takes seconds to parse). memfds stay valid after worker death (our fd holds them), so mapped slots never dangle.
- **Exit codes**: 0 ok, 2 usage, 3 spawn fail, 4 compile failure, 5 render exception, 6 worker death, 7 timeout. Finish with `Shutdown`, short waitpid, SIGKILL fallback.

**CMake** (CMakeLists.txt, after the marcel_worker target ~line 174):
```cmake
add_executable(${PROJECT_NAME}_snap src/tools/snap_main.cpp)
target_link_libraries(${PROJECT_NAME}_snap PRIVATE ${PROJECT_NAME}_ipc)
```
(`marcel_ipc` publishes include dir `src`; global `include_directories(external)` covers stb. No other changes.)

## Step 2 — `documents/realsense/` presentation

Files: `setup.cpp`, `slide0.cpp`, plus empty `slide1.cpp`–`slide9.cpp` (committed so the app doesn't create untracked ones).

**setup.cpp** — plain-processed; globals persist for all slides. Pipeline lives here so slide recompiles (Ctrl+Enter) never restart the camera:
```cpp
#pragma cling load("realsense2")
#include <librealsense2/rs.hpp>

std::string rs_status = "no source";
bool rs_have_source = false;
rs2::pipeline rs_pipe;
rs2::config rs_cfg;

try {
    rs2::context ctx;
    auto devs = ctx.query_devices();
    if (devs.size() > 0) {
        rs_status = std::string("camera: ") + devs[0].get_info(RS2_CAMERA_INFO_NAME)
                  + " #" + devs[0].get_info(RS2_CAMERA_INFO_SERIAL_NUMBER);
        rs_cfg.enable_stream(RS2_STREAM_DEPTH);
        rs_cfg.enable_stream(RS2_STREAM_COLOR, RS2_FORMAT_RGB8);
        rs_pipe.start(rs_cfg);
        rs_have_source = true;
    } else if (const char *bag = getenv("MARCEL_RS_BAG")) {
        // recorded with realsense-viewer (must contain depth AND color);
        // absolute path (worker cwd = exe dir)
        rs_cfg.enable_device_from_file(bag);
        rs_pipe.start(rs_cfg);
        rs_have_source = true;
        rs_status = std::string("bag: ") + bag;
    } else {
        rs_status = "no camera and no MARCEL_RS_BAG set";
    }
} catch (const rs2::error &e) { rs_status = std::string("rs2 error: ") + e.what(); }
  catch (const std::exception &e) { rs_status = std::string("error: ") + e.what(); }
```
Everything guarded — an exception escaping setup poisons the interpreter for all slides.

**slide0.cpp** — ImPlot3D pointcloud, per-point color from the color stream:
- File-scope statics (re-declared on recompile under AllowRedefinition, which is fine): last-good cloud as `std::vector<float> xs, ys, zs;` + `std::vector<ImU32> cols;`, `rs2::pointcloud pc;`, `rs2::decimation_filter dec;` (magnitude ~3, applied to depth only), `bool frozen`.
- Update lambda, all inside try/catch → status text on error:
  1. If `rs_have_source && !frozen`: `rs2::frameset fs; if (rs_pipe.poll_for_frames(&fs))` →
     `auto color = fs.get_color_frame(); auto depth = dec.process(fs.get_depth_frame());`
     `pc.map_to(color); auto points = pc.calculate(depth);` — then walk `points.get_vertices()` + `points.get_texture_coordinates()` in lockstep: for each vertex with `z > 0`, push xyz (flip y so up is up) and sample the color frame at the UV (clamp, `u*w`, `v*h`, RGB8 stride via `get_stride_in_bytes()`) into `cols` as `IM_COL32(r,g,b,255)`. Cap ~60k points.
  2. Header line: `rs_status` + point count; `Checkbox("freeze")`.
  3. `ImPlot3D::BeginPlot("##cloud", GetContentRegionAvail())` → `SetupAxesLimits` from the cloud's bounds + `SetupBoxScale` proportional to the data spans (equal meters per axis) → per-point draw: `ImDrawList *dl = ImPlot3D::GetPlotDrawList();` and for each point `ImVec2 p = ImPlot3D::PlotToPixels(x,y,z); dl->AddRectFilled(p - ImVec2(1,1), p + ImVec2(1,1), cols[i]);` (PlotScatter can't do per-point color; this is the public-API way — PlotToPixels locks setup, so it comes after the Setup calls) → `EndPlot()`. Default rotation, user can drag. Clip to `GetPlotPos()/GetPlotSize()` rect via `dl->PushClipRect` so points don't spill outside the plot.
- No blocking calls, no heavy work at slide top level (top-level statements run at compile time).

## Step 3 — CLAUDE.md workflow section

Append "Slide development workflow (Claude)":
- Slides are interpreted — slide-only edits need **no build step**.
- Loop: edit `documents/<name>/slideN.cpp` → `./build/marcel_snap documents/<name> --slide N --frames 3 --out $TMPDIR/snap` → read compile diagnostics on stdout / worker stderr → **Read the PNGs to see the rendered slide** (default 960×720 keeps Read cheap; `--size 1728x1080` for fidelity checks).
- Exit-code table; `--time/--dt` determinism note; env passthrough (`MARCEL_RS_BAG=/abs/path.bag`, absolute paths since worker cwd = exe dir).
- Worker debugging: `gdb --args ./build/marcel_snap ...` with `set follow-fork-mode child`, `set detach-on-fork off`, `catch exec`; post-mortem via `ulimit -c unlimited` + `gdb build/marcel_worker core`.

## Risks

1. **Cling parsing/loading librealsense**: rs.hpp is heavy C++; if header parse or dlopen fails, the CompileResult diagnostics from marcel_snap say exactly what — fallback pragmas (`add_include_path`) available. Verify setup.cpp first, alone.
2. **Sandbox USB access**: Claude's Bash sandbox may block the camera device nodes (`/dev/bus/usb` write/ioctl). Fallback: Arvid records a `.bag` with realsense-viewer; `MARCEL_RS_BAG=/abs/path.bag ./build/marcel_snap ...` plays it through the identical pipeline API (playback is plain file reads — sandbox-safe), so the full visual loop still verifies in-sandbox.
3. **shm ring starvation**: handled — BufferRelease after every saved PNG.
4. **rs2 exceptions**: setup fully wrapped; lambda wrapped; only `poll_for_frames` (non-blocking) in the frame path so the app's 1s watchdog never trips.
5. **Local uncommitted edits in documents/test**: golden check tolerates them; never stage those files.

## Verification

1. `cmake --build build --target marcel_snap marcel_worker`.
2. **Golden check**: `./build/marcel_snap documents/test --out $TMPDIR/snap_test` → exit 0; Read `slide0_frame*.png` — known-good slides render (validates the tool before any realsense code).
3. **Negative checks**: scratch slide with a syntax error → exit 4 + line markers; `abort()` → exit 6.
4. `./build/marcel_snap documents/realsense --slide 0 --frames 5 --out $TMPDIR/snap_rs` → setup CompileResult shows the camera found; Read PNGs — pointcloud in the ImPlot3D box with recognizable camera colors. (If sandbox blocks USB: Arvid records a depth+color `.bag` in realsense-viewer, then `MARCEL_RS_BAG=/abs/path.bag` re-runs the same command in-sandbox.)
5. Iterate on slide0 styling purely by editing + re-running snap — no rebuilds.
6. Arvid: open the app on `documents/realsense`, confirm live cloud + drag interaction.
7. `ctest` still green (nothing existing touched except the CMake addition).
