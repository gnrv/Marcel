#include "HeadlessGL.h"

#include <cstdio>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GL/gl.h>

#ifndef EGL_PLATFORM_SURFACELESS_MESA
#define EGL_PLATFORM_SURFACELESS_MESA 0x31DD
#endif

namespace {

EGLDisplay getDisplay()
{
    // Mesa's surfaceless platform needs no display server at all — the
    // right thing for a render-only process (and works under WSL2).
    auto getPlatformDisplay = reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(
        eglGetProcAddress("eglGetPlatformDisplayEXT"));
    if (getPlatformDisplay) {
        EGLDisplay d = getPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, nullptr);
        if (d != EGL_NO_DISPLAY && eglInitialize(d, nullptr, nullptr))
            return d;
    }
    EGLDisplay d = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (d != EGL_NO_DISPLAY && eglInitialize(d, nullptr, nullptr))
        return d;
    return EGL_NO_DISPLAY;
}

} // namespace

bool HeadlessGL::init()
{
    EGLDisplay display = getDisplay();
    if (display == EGL_NO_DISPLAY) {
        fprintf(stderr, "HeadlessGL: no EGL display (0x%x)\n", eglGetError());
        return false;
    }

    if (!eglBindAPI(EGL_OPENGL_API)) {
        fprintf(stderr, "HeadlessGL: eglBindAPI(EGL_OPENGL_API) failed (0x%x)\n", eglGetError());
        return false;
    }

    const EGLint config_attribs[] = {
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
        EGL_NONE};
    EGLConfig config;
    EGLint num_configs = 0;
    if (!eglChooseConfig(display, config_attribs, &config, 1, &num_configs) || num_configs == 0) {
        // Surfaceless platforms may expose no pbuffer configs at all; retry
        // without a surface type and rely on surfaceless make-current.
        const EGLint bare_attribs[] = {
            EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
            EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
            EGL_NONE};
        if (!eglChooseConfig(display, bare_attribs, &config, 1, &num_configs) || num_configs == 0) {
            fprintf(stderr, "HeadlessGL: no EGL config (0x%x)\n", eglGetError());
            return false;
        }
    }

    // No version attribs: ask for the highest compatibility-profile GL the
    // driver offers (we need >= 3.0 for FBOs, same as the main window).
    EGLContext context = eglCreateContext(display, config, EGL_NO_CONTEXT, nullptr);
    if (context == EGL_NO_CONTEXT) {
        fprintf(stderr, "HeadlessGL: eglCreateContext failed (0x%x)\n", eglGetError());
        return false;
    }

    EGLSurface surface = EGL_NO_SURFACE;
    if (!eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, context)) {
        // No EGL_KHR_surfaceless_context: fall back to a 1x1 pbuffer.
        const EGLint pbuffer_attribs[] = {EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE};
        surface = eglCreatePbufferSurface(display, config, pbuffer_attribs);
        if (surface == EGL_NO_SURFACE ||
            !eglMakeCurrent(display, surface, surface, context)) {
            fprintf(stderr, "HeadlessGL: make-current failed (0x%x)\n", eglGetError());
            eglDestroyContext(display, context);
            return false;
        }
    }

    display_ = display;
    context_ = context;
    surface_ = surface;
    const GLubyte *renderer = glGetString(GL_RENDERER);
    renderer_ = renderer ? reinterpret_cast<const char *>(renderer) : "(unknown)";
    valid_ = true;
    fprintf(stderr, "HeadlessGL: %s\n", renderer_.c_str());
    return true;
}

HeadlessGL::~HeadlessGL()
{
    if (!valid_)
        return;
    EGLDisplay d = static_cast<EGLDisplay>(display_);
    eglMakeCurrent(d, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    if (surface_)
        eglDestroySurface(d, static_cast<EGLSurface>(surface_));
    eglDestroyContext(d, static_cast<EGLContext>(context_));
    eglTerminate(d);
}
