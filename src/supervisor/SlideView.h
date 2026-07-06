#pragma once

// Draws one remote-rendered slide (the worker's latest frame as an
// ImGui::Image) and gathers the input that goes back to the worker in the
// next FrameBegin: mouse position/wheel/buttons translated into design
// resolution via SlideInputMap, keys and characters when the slide asked for
// them. One instance lives in main(); draw() runs inside each slide child,
// endFrame() runs once after the slide loop and pushes the FrameBegin.

#include "ipc/Protocol.h"
#include "render/SlideInputMap.h"

#include <map>

class RemoteEngine;
struct ImVec2;

class SlideView {
public:
    // Inside the slide's BeginChild. Shows the last frame (or a placeholder
    // while compiling / restarting) and reports this slide's SlideInput.
    void draw(RemoteEngine &engine, int slide, const ImVec2 &size);

    // After the slide loop, once per UI frame: routes a mouse drag that left
    // the slide's rect back to the slide that owns it, then asks the engine
    // to send FrameBegin with the collected input.
    void endFrame(RemoteEngine &engine);

private:
    struct PerSlide {
        bool hovered = false; // last draw()'s hover, for FocusLost edges
        SlideInputMap map;
    };
    static constexpr int kNoSlide = INT32_MIN;

    std::map<int, PerSlide> slides_;
    int drag_slide_ = kNoSlide; // slide that owns the held mouse button(s)
};
