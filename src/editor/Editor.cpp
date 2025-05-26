#include "Editor.h"

void Editor::Render(std::string &exception_what) {
    ImGuiIO& io = ImGui::GetIO();

    TextEditor *rendered_editor = nullptr;
    if (ImGui::BeginTabBar("MyTabBar")) {
        std::string label_and_id = presentation->setup.path.filename().string() + "###setup";
        ImGuiTabItemFlags flags = 0;
        if (activate_tab == "setup") {
            // The tab will not render it contents until the next lap of the loop.
            flags |= ImGuiTabItemFlags_SetSelected;
        }
        if (presentation->setup.dirty)
            flags |= ImGuiTabItemFlags_UnsavedDocument;
        if (ImGui::BeginTabItem(label_and_id.c_str(), nullptr, flags)) {
            active_tab = "setup";
            if (activate_tab == active_tab)
                activate_tab.clear();
            ImGui::PushFont(mono_font);
            auto &editor = editors["setup"];
            ImVec2 editor_size = ImGui::GetContentRegionAvail();
            editor_size.y -= ImGui::GetTextLineHeightWithSpacing();
            editor.SetErrorMarkers(presentation->setup.error_markers);
            editor.Render("TextEditor", editor_size);
            rendered_editor = &editor;
            if (editor.IsTextChanged())
                try {
                    presentation->setup.setText(editor.GetText());
                } catch (std::exception& e) {
                    exception_what = e.what();
                    ImGui::OpenPopup("Exception");
                }

            ImGui::PopFont();
            ImGui::EndTabItem();
        }
        for (auto &slide : presentation->slides) {
            int i = presentation->indexOf(slide);
            std::string slide_id = fmt::format("slide{}", i);
            std::string label_and_id = slide.path.filename().string() + "###" + slide_id;
            ImGuiTabItemFlags flags = 0;
            if (activate_tab == slide_id) {
                flags |= ImGuiTabItemFlags_SetSelected;
            }
            if (slide.dirty)
                flags |= ImGuiTabItemFlags_UnsavedDocument;
            if (ImGui::BeginTabItem(label_and_id.c_str(), nullptr, flags)) {
                active_tab = slide_id;
                if (activate_tab == active_tab)
                    activate_tab.clear();
                ImGui::PushFont(mono_font);
                auto &editor = editors[slide_id];
                ImVec2 editor_size = ImGui::GetContentRegionAvail();
                editor_size.y -= ImGui::GetTextLineHeightWithSpacing();
                editor.SetErrorMarkers(slide.error_markers);
                editor.Render("TextEditor", editor_size);
                rendered_editor = &editor;
                if (editor.IsTextChanged())
                    try {
                        slide.setText(editor.GetText());
                    } catch (std::exception& e) {
                        exception_what = e.what();
                        ImGui::OpenPopup("Exception");
                    }

                ImGui::PopFont();
                ImGui::EndTabItem();
            }
        }
        ImGui::EndTabBar();
    }
    auto cpos = rendered_editor ? rendered_editor->GetCursorPosition() : TextEditor::Coordinates();
    ImGui::Text("%6d/%-6d %6d lines  | %s | %s", cpos.mLine + 1, cpos.mColumn + 1,
        rendered_editor ? rendered_editor->GetTotalLines() : 0,
        rendered_editor ? (rendered_editor->IsOverwrite() ? "Ovr" : "Ins") : "---",
        rendered_editor ? (rendered_editor->CanUndo() ? "*" : " ") : "-");

    TrySave(exception_what);
}

void Editor::TrySave(std::string &exception_what) {
    ImGuiIO& io = ImGui::GetIO();

    // TextEditor actions
    auto shift = io.KeyShift;
    auto ctrl = io.ConfigMacOSXBehaviors ? io.KeySuper : io.KeyCtrl;
    auto alt = io.ConfigMacOSXBehaviors ? io.KeyCtrl : io.KeyAlt;

    if (ctrl && !shift && !alt && ImGui::IsKeyPressed(ImGuiKey_S)) {
        try {
            presentation->getSourceFile(active_tab).save();
        } catch (std::exception& e) {
            exception_what = e.what();
            ImGui::OpenPopup("Exception");
        }
    }

    if (ctrl && !shift && !alt && ImGui::IsKeyPressed(ImGuiKey_Enter)) {
        try {
            presentation->getSourceFile(active_tab).saveAndExecute();
        } catch (std::exception& e) {
            exception_what = e.what();
            ImGui::OpenPopup("Exception");
        }
    }
}

void Editor::RenderInline(const std::string &id, std::string &exception_what, const ImVec2 &size)
{
    ImGuiIO& io = ImGui::GetIO();
    TextEditor &editor = editors[id];
    SourceFile &source_file = presentation->getSourceFile(id);
    ImGui::PopStyleVar();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushFont(mono_font);
    editor.SetErrorMarkers(source_file.error_markers);
    //editor.SetImGuiChildIgnored(true);
    editor.Render(id.c_str(), ImVec2(size.x ? size.x : 0, size.y ? size.y : editor.PreferredHeight()));
    //editor.SetImGuiChildIgnored(false);
    if (editor.IsTextChanged()) {
        try {
            source_file.setText(editor.GetText());
        } catch (std::exception& e) {
            exception_what = e.what();
            ImGui::OpenPopup("Exception");
        }
    }
    if (editor.IsFocused()) {
        active_tab = id;
        activate_tab = id; // For when we next leave notebook mode

        TrySave(exception_what);
    }
    ImGui::PopFont();
}
