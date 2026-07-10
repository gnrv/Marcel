# Marcel — repo conventions

## Git workflow: linear history, no merge commits

This repo never merges. History on `master` must be a straight line:

- Work on a topic branch (`topic/<name>`), one commit per plan step.
- Before landing, rebase the branch onto the latest `master` so every
  commit applies cleanly on top (`git rebase master`). Re-run the tests
  after the rebase.
- Land by fast-forwarding `master` to the branch tip — never
  `git merge` (no `--no-ff`, no merge commits):
  `git push . topic/<name>:master` (or
  `git checkout master && git merge --ff-only topic/<name>`), then
  `git push origin master`.

## Other git rules

- Stage files explicitly by path. NEVER `git add -A` / `git add .` —
  the repo root contains untracked dotfiles (some are character
  devices) that must not be committed.
- No `Co-Authored-By` trailers in commit messages.
- `documents/test/slide*.cpp` often carry the user's uncommitted local
  experiments — do not stage them unless explicitly asked.

## Building & testing

- `cmake --build build --target marcel marcel_worker marcel_tests`
- Tests: CppUnit via CTest — `ctest` in `build/` (one entry per suite;
  `marcel_tests <SuiteName>` runs one). New logic is test-first.
- The sandbox has no X11; GUI verification is done by the user. Worker
  behavior can be exercised headless (EGL/llvmpipe) with small IPC
  drivers against `build/marcel_worker` — see
  `docs/plans/client-server-refactor.md` for the architecture.

## Slide development workflow (Claude)

Slides are interpreted (Cling), so editing a `documents/<deck>/slideN.cpp`
needs **no rebuild** — only `marcel_worker` itself does. Use `marcel_snap`
(built via `cmake --build build --target marcel_snap`) to compile + render a
deck headlessly and dump PNGs you can open with the Read tool:

```
./build/marcel_snap documents/<deck> --slide 0 --frames 3 --out $TMPDIR/snap
```

- It spawns the worker, sends `setup.cpp` then the slide sources, prints each
  compile result (with `line N: msg` markers on error), then renders `--frames`
  frames and writes `$TMPDIR/snap/slideN_frameM.png`. **Read the PNGs to see
  what rendered.** The worker's stderr passes straight through.
- Default size is 960×720 to keep the PNGs cheap to Read; pass
  `--size 1728x1080` for an app-fidelity check.
- Deterministic clock: `--time T0` / `--dt` set `ImGui::GetTime()`, so a slide
  that animates off `GetTime()` renders reproducibly (same `--time` ⇒ identical
  PNG). Also `--worker PATH`, `--timeout SEC`.
- Exit codes: `0` ok, `2` usage, `3` spawn fail, `4` compile failure,
  `5` render exception, `6` worker death, `7` timeout.
- Env passes through to the worker: e.g. `MARCEL_RS_BAG=/abs/path.bag
  ./build/marcel_snap …`. The worker's cwd is its executable directory, so any
  data path a slide reads must be **absolute**.

Slide/setup source rules (learned the hard way):

- The engine compiles a **slide** body inside a wrapper function, so a slide
  must not `#include` anything and ends with `return update;`. All the usual
  headers (ImGui, ImPlot, ImPlot3D, cmath, …) are preloaded and already in
  scope.
- **`setup.cpp` is compiled at global scope** (`declare()`), so it *is* the
  place to `#include` extra libraries and declare globals/typedefs that every
  slide then sees. It must contain only declarations — run one-time work from a
  global-variable initializer (e.g. `static bool done = init();`), not a bare
  top-level statement. Link an extra library from setup with
  `#pragma cling load("<name>")` (see `documents/realsense/setup.cpp`).

Debugging a worker crash: `gdb --args ./build/marcel_snap documents/<deck>
--out $TMPDIR/snap`, then `set follow-fork-mode child`, `set detach-on-fork
off`, `catch exec`, `run` — gdb follows through the fork/exec into
`marcel_worker`. For a post-mortem, run with `ulimit -c unlimited` and open the
core with `gdb build/marcel_worker core`.
