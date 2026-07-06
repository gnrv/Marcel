#pragma once

// Headless EGL + OpenGL context for marcel_worker (step 3b of
// docs/plans/client-server-refactor.md). Tries the Mesa surfaceless
// platform first (works without any display server), then the default
// display with a 1x1 pbuffer. The context is made current on the calling
// (work) thread and stays current for the worker's lifetime — setup code
// is allowed to call raw GL at compile time.

#include <string>

class HeadlessGL {
public:
    HeadlessGL() = default;
    ~HeadlessGL();
    HeadlessGL(const HeadlessGL &) = delete;
    HeadlessGL &operator=(const HeadlessGL &) = delete;

    // Returns false if no EGL context could be created (error on stderr).
    bool init();

    bool valid() const { return valid_; }
    const std::string &renderer() const { return renderer_; }

private:
    void *display_ = nullptr; // EGLDisplay
    void *context_ = nullptr; // EGLContext
    void *surface_ = nullptr; // EGLSurface (may stay EGL_NO_SURFACE)
    std::string renderer_;
    bool valid_ = false;
};
