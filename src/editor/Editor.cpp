#include "Editor.h"

void Editor::OnWindowFocusGained() {
    // Mark all source files for reload check
    presentation->setup.markForReloadCheck();
    for (auto& slide : presentation->slides) {
        slide.markForReloadCheck();
    }
}

void Editor::CheckForFileChanges(SourceFile &source_file, std::string &exception_what) {
    // Check setup file
    if (source_file.needsReloadCheck()) {
        source_file.clearReloadCheck();
        if (source_file.hasFileChangedOnDisk()) {
            if (source_file.dirty) {
                // File is dirty, ask user
                files_to_reload.insert(presentation->getName(source_file));
            } else {
                // File is clean, reload automatically
                ReloadFile(presentation->getName(source_file), exception_what);
            }
        }
    }
}

void Editor::ReloadFile(const std::string& id, std::string &exception_what) {
    try {
        SourceFile& source_file = presentation->getSourceFile(id);
        source_file.reload();

        // Update the editor content
        if (editors.find(id) != editors.end()) {
            editors[id].SetText(source_file.text());
        }
    } catch (const std::exception& e) {
        exception_what = e.what();
    }
}

void Editor::ActivateNextTab()
{
    if (active_tab == "setup") {
        if (presentation->slides.empty()) return;
        activate_tab = fmt::format("slide{}", 0);
    } else {
        int current_index = std::stoi(active_tab.substr(5));
        if (current_index + 1 < presentation->slides.size()) {
            activate_tab = fmt::format("slide{}", current_index + 1);
        } else {
            activate_tab = "setup"; // Wrap around to setup
        }
    }
}

void Editor::ActivatePreviousTab()
{
    if (active_tab == "setup") {
        if (presentation->slides.empty()) return;
        activate_tab = fmt::format("slide{}", presentation->slides.size() - 1);
    } else {
        int current_index = std::stoi(active_tab.substr(5));
        if (current_index > 0) {
            activate_tab = fmt::format("slide{}", current_index - 1);
        } else {
            activate_tab = "setup"; // Wrap around to setup
        }
    }
}

void Editor::Render(std::string &exception_what) {
    ImGuiIO& io = ImGui::GetIO();

    TextEditor *rendered_editor = nullptr;
    SourceFile *active_source_file = nullptr;
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
            active_source_file = &presentation->setup;
            if (activate_tab == active_tab)
                activate_tab.clear();
            ImGui::PushFont(mono_font);
            auto &editor = editors["setup"];
            ImVec2 editor_size = ImGui::GetContentRegionAvail();
            editor_size.y -= ImGui::GetTextLineHeightWithSpacing();
            CheckForFileChanges(presentation->setup, exception_what);
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
            active_source_file = &slide;
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
                CheckForFileChanges(slide, exception_what);
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

    if (active_source_file) {
        ImGui::SameLine();
        const char* languages[] = {"C++", "CUDA"};
        int cuda_width = ImGui::CalcTextSize("CUDA").x + ImGui::GetStyle().FramePadding.x * 2;
        ImGui::SetNextItemWidth(cuda_width);
        // Align it to the right side of the window
        ImGui::SetCursorPosX(ImGui::GetWindowSize().x - cuda_width);
        if (ImGui::MenuItem(languages[active_source_file->is_cuda ? 1 : 0])) {
            ImGui::OpenPopup("LanguagePopup");
        }
        // ImGui::Combo("##language", &selected_language, languages, IM_ARRAYSIZE(languages));
        if (ImGui::BeginPopup("LanguagePopup")) {
            if (ImGui::MenuItem("C++"))
                active_source_file->is_cuda = 0;
            if (ImGui::MenuItem("CUDA"))
                active_source_file->is_cuda = 1;

            ImGui::EndPopup();
        }
    }

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

void Editor::RenderPopups(std::string &exception_what) {
    if (!files_to_reload.empty()) {
        ImGui::OpenPopup("File Changed");
    }
    if (ImGui::BeginPopupModal("File Changed", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("The file has been modified outside the editor.");
        ImGui::Text("Do you want to reload it? (This will discard your changes)");
        ImGui::Separator();

        for (const auto &file_to_reload : files_to_reload) {
            ImGui::BulletText("%s", file_to_reload.c_str());
        }
        if (ImGui::Button("Reload", ImVec2(120, 0))) {
            for (const auto &file_to_reload : files_to_reload) {
                ReloadFile(file_to_reload, exception_what);
            }
            files_to_reload.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Keep Changes", ImVec2(120, 0))) {
            // Update the file's last write time to current to avoid future prompts
            for (const auto &file_to_reload : files_to_reload) {
                presentation->getSourceFile(file_to_reload).updateLastWriteTime();
            }
            files_to_reload.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void Editor::RenderInline(const std::string &id, std::string &exception_what, const ImVec2 &size)
{
    ImGuiIO& io = ImGui::GetIO();
    TextEditor &editor = editors[id];
    SourceFile &source_file = presentation->getSourceFile(id);
    ImGui::PopStyleVar();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));

    CheckForFileChanges(source_file, exception_what);

    editor.SetErrorMarkers(source_file.error_markers);
    //editor.SetImGuiChildIgnored(true);
    ImGui::PushFont(mono_font);
    editor.Render(id.c_str(), ImVec2(size.x ? size.x : 0, size.y ? size.y : editor.PreferredHeight()), false, [&]() {
        ImGui::PopFont();

        const char* languages[] = {"C++", "CUDA"};
        int cuda_width = ImGui::CalcTextSize("CUDA").x + ImGui::GetStyle().FramePadding.x * 2;
        ImGui::SetNextItemWidth(cuda_width);
        // Align it to the right side of the window
        ImGui::SetCursorPosX(ImGui::GetWindowSize().x - cuda_width - 10);
        ImGui::SetCursorPosY(ImGui::GetWindowSize().y - ImGui::GetFrameHeight() - 10);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
        if (ImGui::BeginChild("LanguageChild", ImVec2(cuda_width, ImGui::GetFrameHeight()), ImGuiChildFlags_Borders)) {
            if (ImGui::MenuItem(languages[source_file.is_cuda ? 1 : 0])) {
                ImGui::OpenPopup("LanguagePopup");
            }
            // ImGui::Combo("##language", &selected_language, languages, IM_ARRAYSIZE(languages));
            if (ImGui::BeginPopup("LanguagePopup")) {
                if (ImGui::MenuItem("C++"))
                    source_file.is_cuda = 0;
                if (ImGui::MenuItem("CUDA"))
                    source_file.is_cuda = 1;

                ImGui::EndPopup();
            }
        }
        ImGui::EndChild();
        ImGui::PopStyleVar();
    });
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
}
