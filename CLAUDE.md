# Marcel — repo conventions

## Git workflow: linear history, no merge commits

This repo never merges. History on `master` must be a straight line:

- Work on a topic branch (`topic/<name>`), one commit per plan step.
- Before landing, rebase the branch onto the latest `master` so every
  commit applies cleanly on top (`git rebase master`). Re-run the tests
  after the rebase.
- Land by fast-forwarding `master` to the branch tip — never
  `git merge` (no `--no-ff`, no merge commits):
  `git push . topic/<name>:master` (or `git checkout master &&
  git merge --ff-only topic/<name>`), then `git push origin master`.

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
