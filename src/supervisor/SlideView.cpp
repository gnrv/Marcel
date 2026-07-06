#include "SlideView.h"

#include "supervisor/RemoteEngine.h"

#include "imgui.h"

void SlideView::draw(RemoteEngine &engine, int slide, const ImVec2 &size)
{
    const RemoteSlideFrame *f = engine.slideFrame(slide);
    PerSlide &ps = slides_[slide];
    ImGuiIO &io = ImGui::GetIO();
    ImVec2 pos = ImGui::GetCursorScreenPos();

    if (f && f->rendered_once) {
        // The worker's glReadPixels rows are bottom-up: flip via UVs.
        ImGui::Image(static_cast<ImTextureID>(f->tex), size,
                     ImVec2(0, 1), ImVec2(1, 0));
        if (!engine.workerRunning()) {
            // Stale frame while the worker is down: dim it and say why.
            ImDrawList *dl = ImGui::GetWindowDrawList();
            dl->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y),
                              IM_COL32(0, 0, 0, 128));
            const char *msg = engine.gaveUp()
                                  ? "worker crashed repeatedly — see crash panel"
                                  : "restarting worker...";
            ImVec2 ts = ImGui::CalcTextSize(msg);
            dl->AddText(ImVec2(pos.x + (size.x - ts.x) * 0.5f,
                               pos.y + (size.y - ts.y) * 0.5f),
                        IM_COL32(255, 255, 255, 220), msg);
        }
    } else {
        ImGui::Dummy(size);
        const char *msg = engine.gaveUp() ? "worker crashed repeatedly (see stderr)"
                        : engine.workerRunning() ? "compiling..."
                                                 : "restarting worker...";
        ImGui::GetWindowDrawList()->AddText(
            ImVec2(pos.x + 8, pos.y + 8),
            ImGui::GetColorU32(ImGuiCol_TextDisabled), msg);
    }
    bool hovered = ImGui::IsItemHovered();

    ps.map = SlideInputMap{pos.x, pos.y, size.x, size.y,
                           static_cast<float>(engine.designW()),
                           static_cast<float>(engine.designH())};

    ipc::SlideInput in{};
    in.slide = slide;
    in.visible = 1;
    in.hovered = hovered;
    in.focused = hovered;
    if (hovered) {
        ps.map.toDesign(io.MousePos.x, io.MousePos.y, in.mouse_x, in.mouse_y);

        if (f && f->want_capture_mouse) {
            in.wheel_x = io.MouseWheelH;
            in.wheel_y = io.MouseWheel;
            // The slide's widget wants the wheel: keep the deck from
            // scrolling (owner routing applies from the next frame on).
            ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY);
            ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelX);
        }
        for (int b = 0; b < 3; ++b) {
            if (ImGui::IsMouseClicked(static_cast<ImGuiMouseButton>(b))) {
                engine.addInputEvent(
                    {static_cast<uint32_t>(ipc::InputEventKind::MouseButton),
                     slide, b, 1, 0.f});
                drag_slide_ = slide; // releases route here, even off-rect
            }
        }
        if (f && (f->want_capture_keyboard || f->want_text_input)) {
            // Keyboard keys only; gamepad, mouse aliases and mod aggregates
            // sit above GamepadStart and must not go through AddKeyEvent.
            for (int k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_GamepadStart; ++k) {
                auto key = static_cast<ImGuiKey>(k);
                if (ImGui::IsKeyPressed(key, false))
                    engine.addInputEvent(
                        {static_cast<uint32_t>(ipc::InputEventKind::Key),
                         slide, k, 1, 0.f});
                if (ImGui::IsKeyReleased(key))
                    engine.addInputEvent(
                        {static_cast<uint32_t>(ipc::InputEventKind::Key),
                         slide, k, 0, 0.f});
            }
            for (int n = 0; n < io.InputQueueCharacters.Size; ++n)
                engine.addInputEvent(
                    {static_cast<uint32_t>(ipc::InputEventKind::Char), slide,
                     static_cast<int32_t>(io.InputQueueCharacters[n]), 0, 0.f});
        }
    } else if (ps.hovered) {
        engine.addInputEvent(
            {static_cast<uint32_t>(ipc::InputEventKind::FocusLost), slide,
             0, 0, 0.f});
    }
    ps.hovered = hovered;
    engine.setSlideInput(in);
}

void SlideView::endFrame(RemoteEngine &engine)
{
    ImGuiIO &io = ImGui::GetIO();
    if (drag_slide_ != kNoSlide) {
        for (int b = 0; b < 3; ++b)
            if (ImGui::IsMouseReleased(static_cast<ImGuiMouseButton>(b)))
                engine.addInputEvent(
                    {static_cast<uint32_t>(ipc::InputEventKind::MouseButton),
                     drag_slide_, b, 0, 0.f});
        if (ImGui::IsMouseDown(0) || ImGui::IsMouseDown(1) || ImGui::IsMouseDown(2)) {
            auto it = slides_.find(drag_slide_);
            if (it != slides_.end() && !it->second.hovered) {
                // Cursor left the image mid-drag (e.g. dragging a slider
                // past the edge): keep feeding the mapped position.
                ipc::SlideInput in{};
                in.slide = drag_slide_;
                in.visible = 1;
                in.hovered = 1;
                in.focused = 1;
                it->second.map.toDesign(io.MousePos.x, io.MousePos.y,
                                        in.mouse_x, in.mouse_y);
                engine.setSlideInput(in);
            }
        } else {
            drag_slide_ = kNoSlide;
        }
    }
    engine.endFrame(ImGui::GetTime(), io.DeltaTime);
}
