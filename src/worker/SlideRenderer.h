#pragma once

// One slide's offscreen renderer: a dedicated ImGuiContext (sharing the
// application font atlas so Fonts[] indices match the main process —
// load-bearing, see render/UiFonts.h), its own ImPlot/ImPlot3D contexts,
// and an FBO at the design resolution. Input arrives pre-translated to
// design-resolution slide-local coordinates (SlideInput/InputEvent from
// FrameBegin); the slide lambda runs inside a fullscreen host window,
// mirroring the slide child in main.cpp.

#include "ipc/Protocol.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

struct ImFontAtlas;
struct ImFont;
struct ImGuiContext;
struct ImPlotContext;
struct ImPlot3DContext;

class SlideRenderer {
public:
    struct Result {
        bool rendered = false;
        bool want_capture_mouse = false;
        bool want_capture_keyboard = false;
        bool want_text_input = false;
        std::string exception; // from the slide lambda this frame
    };

    // Requires a current GL context. big_font must live in `atlas`.
    SlideRenderer(uint32_t width, uint32_t height, float dpi_scale,
                  ImFontAtlas *atlas, ImFont *big_font);
    ~SlideRenderer();
    SlideRenderer(const SlideRenderer &) = delete;
    SlideRenderer &operator=(const SlideRenderer &) = delete;

    bool valid() const { return fbo_ != 0; }

    // Runs one ImGui frame with the slide function (or its value text when
    // fn is empty) and rasterizes into the internal FBO.
    Result render(const std::function<void()> &fn, const std::string &value,
                  double time, float delta_time,
                  const ipc::SlideInput &input,
                  const std::vector<ipc::InputEvent> &events);

    // glReadPixels the last rendered frame (RGBA8, bottom-up rows — the
    // reader flips via UV coordinates). dst must hold width*height*4 bytes.
    void readPixels(void *dst);

    uint32_t width() const { return w_; }
    uint32_t height() const { return h_; }

private:
    uint32_t w_, h_;
    float dpi_scale_;
    ImFont *big_font_;
    ImGuiContext *ctx_ = nullptr;
    ImPlotContext *plot_ = nullptr;
    ImPlot3DContext *plot3d_ = nullptr;
    unsigned fbo_ = 0, tex_ = 0;
};
