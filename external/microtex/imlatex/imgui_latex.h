#pragma once

#include <imgui.h>

typedef int ImGuiLatexFlags;       // -> enum ImGuiLatexFlags_     // Flags: for Latex() etc.

// If it capitalizes GUI as Gui, we'd better capitalize LaTeX as Latex
namespace ImGui {
    void InitLatex();
    void Latex(const char* src, ImGuiLatexFlags flags = 0);

    void LatexInternal(ImGuiID id, const ImVec2& pos, ImU32 col, const char *src_begin, const char *src_end = 0, float wrap_pos_x = 0, ImGuiLatexFlags flags = 0);
    ImVec2 LatexGetSize(ImGuiID id, const char *src_begin, const char *src_end, float wrap_pos_x);
}

// Flags for Latex() etc.
enum ImGuiLatexFlags_
{
    ImGuiLatexFlags_None               = 0,
};