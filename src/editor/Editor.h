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

    Presentation &GetPresentation() {
        return presentation;
    }

    void SetMonoFont(ImFont *font) {
        mono_font = font;
    }

    void ActivateTab(const std::string &tab) {
        if (editors.find(tab) == editors.end()) {
            throw std::runtime_error(fmt::format("Tab '{}' does not exist", tab));
        }
        activate_tab = tab;
    }
    std::string GetActiveTab() const {
        return active_tab;
    }

    TextEditor &GetActiveEditor() {
        auto it = editors.find(active_tab);
        if (it != editors.end())
            return it->second;
        throw std::runtime_error("Active editor not found");
    }

    void Render(std::string &exception_what);
    void RenderInline(const std::string &id, std::string &exception_what, const ImVec2 &size = ImVec2(0.0f, 0.0f));

    bool IsCursorAtFirstLine() const {
        auto it = editors.find(active_tab);
        if (it != editors.end())
            return it->second.IsCursorAtFirstLine();
        return false;
    }
    bool IsCursorAtLastLine() const {
        auto it = editors.find(active_tab);
        if (it != editors.end())
            return it->second.IsCursorAtLastLine();
        return false;
    }
};
