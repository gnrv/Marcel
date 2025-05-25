#pragma once

#include "Presentation.h"
#include "TextEditor.h"

#include <cstddef>
#include <fmt/format.h>
#include <map>

class Editor {
    Presentation &presentation;
    std::string activate_tab = "slide0";
    std::string active_tab = "slide0";
    std::map<std::string, TextEditor> editors;
    ImFont *mono_font = nullptr;

public:
    Editor(Presentation &presentation) : presentation(presentation) {
        editors["setup"].SetText(presentation.setup.text());
        for (size_t i = 0; i < presentation.slides.size(); i++) {
            editors[fmt::format("slide{}", i)].SetText(presentation.slides[i].text());
        }
    }

    void SetMonoFont(ImFont *font) {
        mono_font = font;
    }

    void ActivateTab(const std::string &tab) {
        activate_tab = tab;
    }
    std::string GetActiveTab() const {
        return active_tab;
    }
    void Render(std::string &exception_what);
    void RenderInline(const std::string &id, std::string &exception_what, const ImVec2 &size = ImVec2(0.0f, 0.0f));
};
