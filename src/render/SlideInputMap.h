#pragma once

// The affine map between screen coordinates of a slide's displayed
// ImGui::Image and the worker's design-resolution slide-local coordinates
// (docs/plans/client-server-refactor.md: "input translation is a single
// affine map"). Built fresh each frame by SlideView from the image's screen
// rect; used to translate the mouse before it goes into FrameBegin.

struct SlideInputMap {
    float image_x = 0, image_y = 0; // displayed image top-left, screen px
    float image_w = 0, image_h = 0; // displayed image size, screen px
    float design_w = 0, design_h = 0; // worker render resolution

    void toDesign(float sx, float sy, float &dx, float &dy) const
    {
        dx = image_w > 0 ? (sx - image_x) * (design_w / image_w) : 0.f;
        dy = image_h > 0 ? (sy - image_y) * (design_h / image_h) : 0.f;
    }

    // Top/left inclusive, bottom/right exclusive (pixel convention).
    bool contains(float sx, float sy) const
    {
        return sx >= image_x && sx < image_x + image_w &&
               sy >= image_y && sy < image_y + image_h;
    }
};
