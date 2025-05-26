#include "imgui_latex.h"

#include "imgui_internal.h"
#include "imgui_scale.h"

#include "latex.h"

#include <iostream>
#include <memory>

struct ImLatex {
    std::string src;
    std::unique_ptr<Latex::LatexImage> image;
    int wrap_pos_x{ 0 };
    bool animate{ false };
    float font_size{ 16.f };
    ImU32 col{ 0xFF000000 };
};

float ImGuiFontSizeToLatexFontSize(float font_size) {
    // Latex font size is in points, and 1 point is 1/72 inch.
    // Assuming this is Ubuntu or Windows with 96 DPI, 1 point = 72/96 pixels.
    // If you have a different DPI, you might want to adjust this.
    // FIXME: Why 86? It was calibrated by comparint ImGui::Text("Hello, world!") to
    // ImGui::Latex("\\text{Hello, world!}") and adjusting the font size until they matched.
    return font_size * 72.f / 86.f;
}

namespace ImGui {
    ImPool<ImLatex> g_Latexes;

    void InitLatex() {
        std::string err = Latex::init();
        if (!err.empty()) {
            std::cerr << "Failed to initialize MicroTeX: " << err.c_str();
        }
    }

    ImVec2 LatexGetSize(ImGuiID id, const char *src_begin, const char *src_end, float wrap_pos_x) {
        ImGuiContext& g = *GImGui;
        ImLatex *latex = g_Latexes.GetOrAddByKey(id);
        // The src is the bit before the ### (used to change a label while maintaining constant ID)
        if (!src_end)
            src_end = src_begin + strlen(src_begin);

        std::string_view src_view(src_begin, src_end - src_begin);
        if (latex->src != src_view || latex->wrap_pos_x != wrap_pos_x || latex->font_size != g.FontSize) {
            latex->font_size = g.FontSize;
            latex->src = src_view;
            latex->wrap_pos_x = wrap_pos_x;;
            latex->image = std::make_unique<Latex::LatexImage>(
                latex->src, ImGuiFontSizeToLatexFontSize(latex->font_size),
                wrap_pos_x >= 0 ? wrap_pos_x : 0, 7.f,
                latex->col);
        }

        if (latex->image->getLatexErrorMsg().empty()) {
            return latex->image->getDimensions();
        }

        return ImVec2(0.f, 0.f);
    }

    void LatexInternal(ImGuiID id, const ImVec2& pos, ImU32 col, const char *src_begin, const char *src_end, float wrap_pos_x, ImGuiLatexFlags flags) {
        ImGuiContext& g = *GImGui;
        ImLatex *latex = g_Latexes.GetOrAddByKey(id);
        // The src is the bit before the ### (used to change a label while maintaining constant ID)
        if (!src_end)
            src_end = src_begin + strlen(src_begin);

        std::string_view src_view(src_begin, src_end - src_begin);
        if (latex->src != src_view || latex->wrap_pos_x != wrap_pos_x || latex->font_size != g.FontSize || latex->col != col) {
            latex->font_size = g.FontSize;
            latex->src = src_view;
            latex->wrap_pos_x = wrap_pos_x;
            latex->col = col;
            latex->image = std::make_unique<Latex::LatexImage>(
                latex->src, ImGuiFontSizeToLatexFontSize(latex->font_size),
                wrap_pos_x >= 0 ? wrap_pos_x : 0, 7.f,
                col);
        }

        if (g.CurrentItemFlags & ImGuiItemFlags_Animated) {
            latex->animate = true;
        }

        if (latex->image->getLatexErrorMsg().empty()) {
            // TODO: We should only render if ItemAdd returns true
            ImGuiWindow* window = GetCurrentWindow();
            const ImVec2 prev_cursor_pos(window->DC.CursorPos);
            window->DC.CursorPos = pos;
            latex->animate = latex->image->render(ImVec2(1.f, 1.f), ImVec2(0.f, 0.f), latex->animate);
            window->DC.CursorPos = prev_cursor_pos;
        } else {
            ImGui::GetWindowDrawList()->AddText(pos, col, latex->image->getLatexErrorMsg().c_str());
        }
    }

    void Latex(const char* src, ImGuiLatexFlags flags) {
        ImGuiContext& g = *GImGui;
        ImGuiWindow* window = GetCurrentWindow();
        if (window->SkipItems)
            return;

        const ImGuiID id = ImGui::GetID(src);
        const float wrap_pos_x = window->DC.TextWrapPos;
        // The src is the bit before the ### (used to change a label while maintaining constant ID)
        const char* src_end = src + strlen(src);
        if (const char* p = strstr(src, "###"))
            src_end = p;
        ImU32 col = ImGui::ColorConvertFloat4ToU32(ImGui::GetStyle().Colors[ImGuiCol_Text]);
        const ImVec2 text_pos(window->DC.CursorPos);
        LatexInternal(id, text_pos, col, src, src_end, wrap_pos_x, flags);

        ImLatex *latex = g_Latexes.GetOrAddByKey(id);
        const ImVec2 text_size(latex->image->getDimensions());
        ImRect bb(text_pos, text_pos + text_size);
        ItemSize(text_size, 0.0f);
        ItemAdd(bb, 0);
    }
}
