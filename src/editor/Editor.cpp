#include "Editor.h"

void Editor::Render(std::string &exception_what) {
    ImGuiIO& io = ImGui::GetIO();
    TextEditor &active_editor = editors[active_tab];
    ImGui::PopStyleVar();
    if (ImGui::BeginMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Save")) {
                try {
                    presentation.getSourceFile(active_tab).save();
                } catch (std::exception& e) {
                    exception_what = e.what();
                    ImGui::OpenPopup("Exception");
                }
            }
            if (ImGui::MenuItem("Quit", "Alt-F4"))
                exit(0);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Edit")) {
            bool ro = active_editor.IsReadOnly();
            if (ImGui::MenuItem("Read-only mode", nullptr, &ro))
                active_editor.SetReadOnly(ro);
            ImGui::Separator();

            if (ImGui::MenuItem("Undo", "Ctrl-Z", nullptr, !ro && active_editor.CanUndo()))
                active_editor.Undo();
            if (ImGui::MenuItem("Redo", "Ctrl-Y", nullptr, !ro && active_editor.CanRedo()))
                active_editor.Redo();

            ImGui::Separator();

            if (ImGui::MenuItem("Copy", "Ctrl-C", nullptr, active_editor.HasSelection()))
                active_editor.Copy();
            if (ImGui::MenuItem("Cut", "Ctrl-X", nullptr, !ro && active_editor.HasSelection()))
                active_editor.Cut();
            if (ImGui::MenuItem("Delete", "Del", nullptr, !ro && active_editor.HasSelection()))
                active_editor.Delete();
            if (ImGui::MenuItem("Paste", "Ctrl-V", nullptr, !ro && ImGui::GetClipboardText() != nullptr))
                active_editor.Paste();

            ImGui::Separator();

            if (ImGui::MenuItem("Select all", nullptr, nullptr))
                active_editor.SetSelection(TextEditor::Coordinates(), TextEditor::Coordinates(active_editor.GetTotalLines(), 0));

            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("View")) {
            if (ImGui::MenuItem("Full Screen", "F11")) {
                //ToggleFullscreen();
            }
            ImGui::EndMenu();
        }
        ImGui::EndMenuBar();
    }
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));

    TextEditor *rendered_editor = nullptr;
    if (ImGui::BeginTabBar("MyTabBar")) {
        std::string label_and_id = presentation.setup.path.filename().string() + "###setup";
        ImGuiTabItemFlags flags = 0;
        if (activate_tab == "setup") {
            // The tab will not render it contents until the next lap of the loop.
            flags |= ImGuiTabItemFlags_SetSelected;
        }
        if (presentation.setup.dirty)
            flags |= ImGuiTabItemFlags_UnsavedDocument;
        if (ImGui::BeginTabItem(label_and_id.c_str(), nullptr, flags)) {
            active_tab = "setup";
            if (activate_tab == active_tab)
                activate_tab.clear();
            ImGui::PushFont(mono_font);
            auto &editor = editors["setup"];
            ImVec2 editor_size = ImGui::GetContentRegionAvail();
            editor_size.y -= ImGui::GetTextLineHeightWithSpacing();
            editor.SetErrorMarkers(presentation.setup.error_markers);
            editor.Render("TextEditor", editor_size);
            rendered_editor = &editor;
            if (editor.IsTextChanged())
                try {
                    presentation.setup.setText(editor.GetText());
                } catch (std::exception& e) {
                    exception_what = e.what();
                    ImGui::OpenPopup("Exception");
                }

            ImGui::PopFont();
            ImGui::EndTabItem();
        }
        for (auto &slide : presentation.slides) {
            int i = presentation.indexOf(slide);
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

    // TextEditor actions
    auto shift = io.KeyShift;
    auto ctrl = io.ConfigMacOSXBehaviors ? io.KeySuper : io.KeyCtrl;
    auto alt = io.ConfigMacOSXBehaviors ? io.KeyCtrl : io.KeyAlt;

    if (ctrl && !shift && !alt && ImGui::IsKeyPressed(ImGuiKey_S)) {
        try {
            presentation.getSourceFile(active_tab).save();
        } catch (std::exception& e) {
            exception_what = e.what();
            ImGui::OpenPopup("Exception");
        }
    }

    // TextEditor dialogs
    if (ImGui::BeginPopupModal("Exception", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("%s", exception_what.c_str());
        if (ImGui::Button("OK"))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}
