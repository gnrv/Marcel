#pragma once

// Clipboard proxy interface for the worker's ImGui contexts. The worker has
// no window system, so slide-visible clipboard reads come from a cache that
// main pushes over IPC (ClipboardData) and writes are forwarded back to
// main. WorkerApp implements this; SlideRenderer (and the compile context)
// install the hooks.
struct SlideClipboard {
    virtual ~SlideClipboard() = default;
    // Returned pointer must stay valid until the next get() or set().
    virtual const char *get() = 0;
    virtual void set(const char *text) = 0;
};

// Points the CURRENT ImGui context's clipboard handlers at cb.
void InstallSlideClipboard(SlideClipboard *cb);
