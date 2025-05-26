#pragma once

#include "Presentation.h"
#include "TextEditor.h"

#include <cstddef>
#include <fmt/format.h>
#include <map>
#include <memory>

class Editor {
    std::shared_ptr<Presentation> presentation;
    std::string activate_tab = "slide0";
    std::string active_tab = "slide0";
    std::map<std::string, TextEditor> editors;
    ImFont *mono_font = nullptr;

public:
    Editor(std::shared_ptr<Presentation> presentation) : presentation(presentation) {
        editors["setup"].SetText(presentation->setup.text());
        for (size_t i = 0; i < presentation->slides.size(); i++) {
            editors[fmt::format("slide{}", i)].SetText(presentation->slides[i].text());
        }
    }

    Presentation &GetPresentation() {
        return *presentation;
    }

    void SetPresentation(std::shared_ptr<Presentation> new_presentation) {
        presentation = new_presentation;
        editors["setup"].SetText(presentation->setup.text());
        size_t n = presentation->slides.size();
        for (size_t i = 0; i < n; i++) {
            editors[fmt::format("slide{}", i)].SetText(presentation->slides[i].text());
        }
        // Remove editors that are no longer in the presentation
        for (size_t i = n; i < editors.size(); i++) {
            editors.erase(fmt::format("slide{}", i));
        }
        // Reset active tab to the first slide
        if (n > 0) {
            activate_tab = "slide0";
            active_tab = "slide0";
        } else {
            activate_tab = "setup";
            active_tab = "setup";
        }
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
