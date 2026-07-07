#include "SlideRenderer.h"

#include "imgui.h"
#include "imgui_internal.h" // ImGuiContext::Time injection, ErrorRecovery
#include "imgui_scale.h"
#include "implot.h"
#include "implot3d.h"
#include "backends/imgui_impl_opengl3.h"

#include <EGL/egl.h>
#include <GL/gl.h>

#include <algorithm>
#include <cstdio>

namespace {

// The GL half of FBO setup is core since GL 3.0; declare the entry points
// via the extension header so we don't depend on a loader library.
#ifndef GL_FRAMEBUFFER
#define GL_FRAMEBUFFER 0x8D40
#define GL_COLOR_ATTACHMENT0 0x8CE0
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#endif
typedef void (*PFNGLGENFRAMEBUFFERS)(GLsizei, GLuint *);
typedef void (*PFNGLBINDFRAMEBUFFER)(GLenum, GLuint);
typedef void (*PFNGLFRAMEBUFFERTEXTURE2D)(GLenum, GLenum, GLenum, GLuint, GLint);
typedef GLenum (*PFNGLCHECKFRAMEBUFFERSTATUS)(GLenum);
typedef void (*PFNGLDELETEFRAMEBUFFERS)(GLsizei, const GLuint *);

PFNGLGENFRAMEBUFFERS p_glGenFramebuffers;
PFNGLBINDFRAMEBUFFER p_glBindFramebuffer;
PFNGLFRAMEBUFFERTEXTURE2D p_glFramebufferTexture2D;
PFNGLCHECKFRAMEBUFFERSTATUS p_glCheckFramebufferStatus;
PFNGLDELETEFRAMEBUFFERS p_glDeleteFramebuffers;

bool loadFboFunctions()
{
    if (p_glGenFramebuffers)
        return true;
    // eglGetProcAddress resolves core GL functions too.
    p_glGenFramebuffers = reinterpret_cast<PFNGLGENFRAMEBUFFERS>(eglGetProcAddress("glGenFramebuffers"));
    p_glBindFramebuffer = reinterpret_cast<PFNGLBINDFRAMEBUFFER>(eglGetProcAddress("glBindFramebuffer"));
    p_glFramebufferTexture2D = reinterpret_cast<PFNGLFRAMEBUFFERTEXTURE2D>(eglGetProcAddress("glFramebufferTexture2D"));
    p_glCheckFramebufferStatus = reinterpret_cast<PFNGLCHECKFRAMEBUFFERSTATUS>(eglGetProcAddress("glCheckFramebufferStatus"));
    p_glDeleteFramebuffers = reinterpret_cast<PFNGLDELETEFRAMEBUFFERS>(eglGetProcAddress("glDeleteFramebuffers"));
    return p_glGenFramebuffers && p_glBindFramebuffer && p_glFramebufferTexture2D &&
           p_glCheckFramebufferStatus && p_glDeleteFramebuffers;
}

} // namespace

void InstallSlideClipboard(SlideClipboard *cb)
{
    ImGuiPlatformIO &pio = ImGui::GetPlatformIO();
    pio.Platform_ClipboardUserData = cb;
    pio.Platform_GetClipboardTextFn = [](ImGuiContext *) -> const char * {
        return static_cast<SlideClipboard *>(
                   ImGui::GetPlatformIO().Platform_ClipboardUserData)->get();
    };
    pio.Platform_SetClipboardTextFn = [](ImGuiContext *, const char *text) {
        static_cast<SlideClipboard *>(
            ImGui::GetPlatformIO().Platform_ClipboardUserData)->set(text);
    };
}

SlideRenderer::SlideRenderer(uint32_t width, uint32_t height, float dpi_scale,
                             ImFontAtlas *atlas, ImFont *big_font,
                             uint32_t num_targets, SlideClipboard *clipboard)
    : w_(width), h_(height), dpi_scale_(dpi_scale), big_font_(big_font)
{
    if (!loadFboFunctions()) {
        fprintf(stderr, "SlideRenderer: FBO entry points unavailable\n");
        return;
    }

    ImGuiContext *prev = ImGui::GetCurrentContext();
    ctx_ = ImGui::CreateContext(atlas);
    ImGui::SetCurrentContext(ctx_);
    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2(static_cast<float>(w_), static_cast<float>(h_));
    io.ConfigErrorRecoveryEnableAssert = false;
    ImGui::StyleColorsDark();
    ImGui::GetStyle().ScaleAllSizes(dpi_scale_); // mirrors main.cpp
    if (clipboard)
        InstallSlideClipboard(clipboard);
    plot_ = ImPlot::CreateContext();
    plot3d_ = ImPlot3D::CreateContext();
    ImGui_ImplOpenGL3_Init("#version 130");
    ImGui::SetCurrentContext(prev);

    for (uint32_t i = 0; i < num_targets; ++i) {
        Target t;
        glGenTextures(1, &t.tex);
        glBindTexture(GL_TEXTURE_2D, t.tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w_, h_, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        p_glGenFramebuffers(1, &t.fbo);
        p_glBindFramebuffer(GL_FRAMEBUFFER, t.fbo);
        p_glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t.tex, 0);
        GLenum status = p_glCheckFramebufferStatus(GL_FRAMEBUFFER);
        p_glBindFramebuffer(GL_FRAMEBUFFER, 0);
        if (status != GL_FRAMEBUFFER_COMPLETE) {
            fprintf(stderr, "SlideRenderer: FBO incomplete (0x%x)\n", status);
            p_glDeleteFramebuffers(1, &t.fbo);
            glDeleteTextures(1, &t.tex);
            for (Target &prev : targets_) {
                p_glDeleteFramebuffers(1, &prev.fbo);
                glDeleteTextures(1, &prev.tex);
            }
            targets_.clear();
            return;
        }
        targets_.push_back(t);
    }
}

SlideRenderer::~SlideRenderer()
{
    if (ctx_) {
        ImGuiContext *prev = ImGui::GetCurrentContext();
        ImGui::SetCurrentContext(ctx_);
        ImGui_ImplOpenGL3_Shutdown();
        if (plot3d_)
            ImPlot3D::DestroyContext(plot3d_);
        if (plot_)
            ImPlot::DestroyContext(plot_);
        ImGui::SetCurrentContext(prev == ctx_ ? nullptr : prev);
        ImGui::DestroyContext(ctx_);
    }
    for (Target &t : targets_) {
        p_glDeleteFramebuffers(1, &t.fbo);
        glDeleteTextures(1, &t.tex);
    }
}

SlideRenderer::Result SlideRenderer::render(const std::function<void()> &fn,
                                            const std::string &value,
                                            double time, float delta_time,
                                            const ipc::SlideInput &input,
                                            const std::vector<ipc::InputEvent> &events,
                                            uint32_t target)
{
    Result result;
    if (!valid() || target >= targets_.size())
        return result;

    ImGuiContext *prev = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(ctx_);
    ImPlot::SetCurrentContext(plot_);
    ImPlot3D::SetCurrentContext(plot3d_);
    ImGuiIO &io = ImGui::GetIO();

    // Inject this frame's input (already in design-resolution coordinates).
    if (input.hovered)
        io.AddMousePosEvent(input.mouse_x, input.mouse_y);
    else
        io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
    if (input.wheel_x != 0.f || input.wheel_y != 0.f)
        io.AddMouseWheelEvent(input.wheel_x, input.wheel_y);
    for (const ipc::InputEvent &ev : events) {
        switch (static_cast<ipc::InputEventKind>(ev.kind)) {
        case ipc::InputEventKind::MouseButton:
            io.AddMouseButtonEvent(ev.a, ev.b != 0);
            break;
        case ipc::InputEventKind::Key:
            io.AddKeyEvent(static_cast<ImGuiKey>(ev.a), ev.b != 0);
            break;
        case ipc::InputEventKind::Char:
            io.AddInputCharacter(static_cast<unsigned>(ev.a));
            break;
        case ipc::InputEventKind::FocusLost:
            io.AddFocusEvent(false);
            break;
        }
    }
    io.DeltaTime = delta_time > 0.f ? delta_time : 1.f / 60.f;

    ImGui_ImplOpenGL3_NewFrame();
    ImGui::NewFrame();
    // Deterministic clock: slide code sees the injected time via
    // ImGui::GetTime(), not this process's wall clock.
    ctx_->Time = time;

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin("Slide", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                     ImGuiWindowFlags_NoSavedSettings);
    ImGui::PushFont(big_font_);
    // Mirrors main.cpp: slides are designed for a canvas whose SHORT side
    // is 1080 px (equals height for all landscape formats; width for 9:16),
    // and PushScale multiplies onto the DPI scale baked into the fonts.
    ImGui::PushScale(static_cast<float>(std::min(w_, h_)) / 1080.f / dpi_scale_);

    ImGuiErrorRecoveryState state;
    ImGui::ErrorRecoveryStoreState(&state);
    try {
        if (fn)
            fn();
        else
            ImGui::Text("%s", value.c_str());
        result.rendered = true;
    } catch (std::exception &e) {
        ImGui::ErrorRecoveryTryToRecoverState(&state);
        result.exception = e.what();
    }

    ImGui::PopScale();
    ImGui::PopFont();
    ImGui::End();
    ImGui::Render();

    // Not io.WantCaptureMouse: the fullscreen host window makes that true for
    // any position, which would steal the deck's scroll wheel. What main
    // needs to know is "is the mouse on an interactive widget in the slide".
    result.want_capture_mouse = ctx_->HoveredId != 0 || ctx_->ActiveId != 0;
    result.want_capture_keyboard = io.WantCaptureKeyboard;
    result.want_text_input = io.WantTextInput;

    p_glBindFramebuffer(GL_FRAMEBUFFER, targets_[target].fbo);
    glViewport(0, 0, static_cast<GLsizei>(w_), static_cast<GLsizei>(h_));
    glClearColor(0.06f, 0.06f, 0.06f, 1.0f); // ImGui dark WindowBg
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    p_glBindFramebuffer(GL_FRAMEBUFFER, 0);

    ImGui::SetCurrentContext(prev);
    return result;
}

void SlideRenderer::readPixels(uint32_t target, void *dst)
{
    if (target >= targets_.size())
        return;
    p_glBindFramebuffer(GL_FRAMEBUFFER, targets_[target].fbo);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, static_cast<GLsizei>(w_), static_cast<GLsizei>(h_),
                 GL_RGBA, GL_UNSIGNED_BYTE, dst);
    p_glBindFramebuffer(GL_FRAMEBUFFER, 0);
}
