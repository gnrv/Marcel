#include <iostream>
#include <GL/gl.h>
#define GLFW_EXPOSE_NATIVE_X11
#include <GLFW/glfw3.h>
#include "imgui.h"
#include "imgui_internal.h"
#include "implot.h"
#include "implot3d.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"

#include "IconsMaterialDesignIcons.h"

#include "Editor.h"
#include "Presentation.h"
#include "Slide.h"
#include "TextEditor.h"
#include "imgui_latex.h"
#include "imgui_scale.h"

#include <cmath>

#include <string>

#include <chrono>
#include <iostream>
#include <exception>
#include <fmt/format.h>

#include <filesystem>
#include <fstream>

#include "system/sys_util.h"
#include "system/DpiInfo.h"
#include "engine/ClingEngine.h"
#include "render/UiFonts.h"


#include <nfd.hpp>
#include <nfd_glfw3.h>

static float f_adjust = 0.0f;

struct MyAppSettings {
    std::function<void()> ToggleFullscreen = nullptr;
    GLFWwindow* window = nullptr;
    int window_x = 100, window_y = 100, window_w = 1280, window_h = 720;
    std::string current_folder{ getExecutablePath() + "/../documents/test" };
    bool notebook_mode{ true }; // Start in notebook mode
};
static MyAppSettings g_settings;

// Called when a [MyApp][main] section is found
static void* MySettings_ReadOpen(ImGuiContext*, ImGuiSettingsHandler*, const char* name) {
    if (strcmp(name, "main") == 0)
        return &g_settings;
    return nullptr;
}

#define STRINGIFY_HELPER(x) #x
#define STRINGIFY(x) STRINGIFY_HELPER(x)

// Called for each line in the section
static void MySettings_ReadLine(ImGuiContext*, ImGuiSettingsHandler*, void* entry, const char* line) {
    MyAppSettings* settings = (MyAppSettings*)entry;
    int x, y, w, h;
    settings->current_folder.reserve(MAX_PATH);
    if (sscanf(line, "Window=%d,%d,%d,%d", &x, &y, &w, &h) == 4) {
        settings->window_x = x;
        settings->window_y = y;
        settings->window_w = w;
        settings->window_h = h;

        // Apply the settings to the window if it exists
        if (settings->window) {
            // Get primary monitor work area
            GLFWmonitor* monitor = glfwGetPrimaryMonitor();
            int mx = 0, my = 0, mw = 1920, mh = 1080;
            glfwGetMonitorWorkarea(monitor, &mx, &my, &mw, &mh);

            // Clamp window size
            g_settings.window_w = std::min(g_settings.window_w, mw);
            g_settings.window_h = std::min(g_settings.window_h, mh);

            // Clamp window position (optional, to keep window on screen)
            g_settings.window_x = std::max(mx, std::min(g_settings.window_x, mx + mw - g_settings.window_w));
            g_settings.window_y = std::max(my, std::min(g_settings.window_y, my + mh - g_settings.window_h));

            glfwSetWindowPos(settings->window, settings->window_x, settings->window_y);
            glfwSetWindowSize(settings->window, settings->window_w, settings->window_h);
        }
    } else if (sscanf(line, "CurrentFolder=%" STRINGIFY(MAX_PATH) "s", settings->current_folder.data()) == 1) {
        // Set the string length based on the actual read length
        settings->current_folder.resize(strlen(settings->current_folder.c_str()));

        // Remove quotes if present
        if (!settings->current_folder.empty() && settings->current_folder.front() == '"' && settings->current_folder.back() == '"') {
            settings->current_folder = settings->current_folder.substr(1, settings->current_folder.size() - 2);
        }

        // We don't apply these settings here, the app will load the folder when it starts.
    } else if (strcmp(line, "NotebookMode=1") == 0) {
        settings->notebook_mode = true;
    } else if (strcmp(line, "NotebookMode=0") == 0) {
        settings->notebook_mode = false;
    } else {
        std::cerr << "Unknown setting line: " << line << std::endl;
    }
}

// Called when saving the ini file
static void MySettings_WriteAll(ImGuiContext*, ImGuiSettingsHandler*, ImGuiTextBuffer* out_buf) {
    out_buf->appendf("[MyApp][main]\n");
    out_buf->appendf("Window=%d,%d,%d,%d\n", g_settings.window_x, g_settings.window_y, g_settings.window_w, g_settings.window_h);
    out_buf->appendf("CurrentFolder=%s\n", g_settings.current_folder.c_str());
    out_buf->appendf("NotebookMode=%d\n", g_settings.notebook_mode ? 1 : 0);
}

static bool nfd_initialized = false;

std::string OpenFolderDialog() {
    if (!nfd_initialized) {
        nfd_initialized = true;
        auto start = std::chrono::high_resolution_clock::now();
        NFD::Init();
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
        printf("Native file dialog init took: %ld ms\n", duration.count());
    }

    NFD::UniquePath outPath;
    GLFWwindow* glfwParentWindow = g_settings.window;
    nfdwindowhandle_t parentWindow;
    NFD_GetNativeWindowFromGLFWWindow(glfwParentWindow, &parentWindow);
    nfdresult_t result = NFD::PickFolder(outPath, g_settings.current_folder.c_str(), parentWindow);
    if (result == NFD_OKAY) {
        return std::string(outPath.get());
    }
    return "";
}

template<typename TToggleFullscreen, typename TOpenFolder>
void RenderMenu(Editor &editor, std::string &exception_what, TToggleFullscreen ToggleFullscreen, TOpenFolder OpenFolder) {
    TextEditor &active_editor = editor.GetActiveEditor();

    if (ImGui::BeginMenu("File")) {
    // Disable this menu item if we don't have access to the Cling interpreter.
    if (ImGui::MenuItem("Open Folder", "Ctrl-O", false,
        true
    )) {
        // Open a folder dialog to select the presentation folder
        std::string path = OpenFolderDialog();
        if (!path.empty()) {
            try {
                OpenFolder(path);
            } catch (std::exception& e) {
                exception_what = e.what();
                ImGui::OpenPopup("Exception");
            }
        }
    }
    if (ImGui::MenuItem("Save")) {
        try {
            editor.GetPresentation().getSourceFile(editor.GetActiveTab()).save();
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
    if (ImGui::MenuItem("Start Presentation", "F5")) {
    }
    if (ImGui::MenuItem("Toggle Notebook View", "F10")) {
        g_settings.notebook_mode = !g_settings.notebook_mode;
        ImGui::SaveIniSettingsToDisk(ImGui::GetIO().IniFilename);
    }
    if (ImGui::MenuItem("Toggle Full Screen", "F11")) {
        ToggleFullscreen();
    }
    ImGui::EndMenu();
}
}

int main(int argc, char **argv) {
    std::filesystem::current_path(getExecutablePath());

    ClingEngine engine(argc, argv);

    // Setup window
    auto start_glfw = std::chrono::high_resolution_clock::now();
    glfwSetErrorCallback([](int error, const char* description) {
        fprintf(stderr, "Glfw Error %d: %s\n", error, description);
    });
    if (!glfwInit())
        return 1;
    auto end_glfw = std::chrono::high_resolution_clock::now();
    auto duration_glfw = std::chrono::duration_cast<std::chrono::milliseconds>(end_glfw - start_glfw);
    printf("GLFW init took: %ld ms\n", duration_glfw.count());

    // GL 3.0 + GLSL 130
    const char* glsl_version = "#version 130";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    //glfwWindowHint( GLFW_DECORATED, GLFW_FALSE );
    //glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);  // 3.2+ only
    //glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);            // 3.0+ only

    // WSL2/dpi detection shared with the worker process (see src/system/DpiInfo.h)
    DpiInfo dpi_info = detectDpi();
    float dpi_scale = dpi_info.dpi_scale;
    float window_size_scale_factor = dpi_info.window_size_scale_factor;
    bool is_wsl2 = dpi_info.is_wsl2;

    // To determine possible window height, query the monitor height.
    int monitor_count;
    GLFWmonitor **monitors = glfwGetMonitors(&monitor_count);
    int window_height = 720;
    if (monitor_count) {
        int w, h;
        glfwGetMonitorWorkarea(monitors[0], NULL, NULL, &w, &h);
        // Now find the greatest "p" we can have in 720p, 1080p etc
        if (h > 1440) {
            window_height = 1440;
        } else if (h > 1080) {
            window_height = 1080;
        } else if (h > 720) {
            window_height = 720;
        }

        if (!is_wsl2)
            glfwGetMonitorContentScale(monitors[0], NULL, &dpi_scale);
    }
    printf("Content scale: %f\n", dpi_scale);

    // Create window with graphics context
    ImVec2 window_size{ 16*window_height*window_size_scale_factor/10, window_height*window_size_scale_factor };
    GLFWwindow* window = glfwCreateWindow(window_size.x,
                                          window_size.y,
                                          "Marcel", NULL, NULL);
    if (window == NULL)
        return 1;
    g_settings.window = window;
    g_settings.window_w = window_size.x;
    g_settings.window_h = window_size.y;
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1); // Enable vsync

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();

    // ImGUI won't load the settings from the ini file until we call NewFrame()
    // Pre-load our settings here
    auto ini_path = std::filesystem::path(getExecutablePath()) / "imgui.ini";
    std::string keep_alive = ini_path.string();
    io.IniFilename = keep_alive.c_str();

    ImGuiSettingsHandler ini_handler;
    ini_handler.TypeName = "MyApp";
    ini_handler.TypeHash = ImHashStr("MyApp");
    ini_handler.ReadOpenFn = MySettings_ReadOpen;
    ini_handler.ReadLineFn = MySettings_ReadLine;
    ini_handler.WriteAllFn = MySettings_WriteAll;
    ImGui::GetCurrentContext()->SettingsHandlers.push_back(ini_handler);

    // Do an initial read of all settings
    ImGui::LoadIniSettingsFromDisk(io.IniFilename);

    ImPlot::CreateContext();
    ImPlot3D::CreateContext();
    ImGui::InitLatex();
    // NavEnableKeyboard messes with TextEditor
    //io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;       // Enable Keyboard Controls
    //io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls
    //io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;           // Enable Docking
    //io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;         // Enable Multi-Viewport / Platform Windows
    //io.ConfigViewportsNoAutoMerge = true;
    //io.ConfigViewportsNoTaskBarIcon = true;

    // Instead of disabling assert, we define a throwing IM_ASSERT
    // UPDATE: Nope, the exception was not propagated across the interpreter to us
    io.ConfigErrorRecoveryEnableAssert = false;

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();
    //ImGui::StyleColorsClassic();

    ImGuiStyle& style = ImGui::GetStyle();
    style.ScaleAllSizes(dpi_scale);

    // Setup Platform/Renderer backends
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glsl_version);

    // Load Fonts
    // - If no fonts are loaded, dear imgui will use the default font. You can also load multiple fonts and use ImGui::PushFont()/PopFont() to select them.
    // - AddFontFromFileTTF() will return the ImFont* so you can store it if you need to select the font among multiple.
    // - If the file cannot be loaded, the function will return NULL. Please handle those errors in your application (e.g. use an assertion, or display an error and quit).
    // - The fonts will be rasterized at a given size (w/ oversampling) and stored into a texture when calling ImFontAtlas::Build()/GetTexDataAsXXXX(), which ImGui_ImplXXXX_NewFrame below will call.
    // - Read 'docs/FONTS.md' for more instructions and details.
    // - Remember that in C/C++ if you want to include a backslash \ in a string literal you need to write a double backslash \\ !
    // Font set shared with the worker process — order is load-bearing, see UiFonts.h
    UiFonts ui_fonts = LoadUiFonts(io.Fonts, dpi_scale);
    ImFont *fira_sans_big = ui_fonts.fira_sans_big;
    ImFont *fira_sans_small = ui_fonts.fira_sans_small;
    (void)fira_sans_small;
    ImFont *fira_mono = ui_fonts.fira_mono;

    // Our state
    bool show_demo_window = false;
    bool show_another_window = false;
    ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);
    //ImVec4 clear_color = ImVec4(0.f, 0.f, 0.f, 1.00f);

    std::filesystem::current_path(g_settings.current_folder);
    std::shared_ptr<Presentation> presentation = std::make_shared<Presentation>(g_settings.current_folder);
#ifdef USE_CUDA
    presentation->setup.is_cuda = true;
    for (auto& slide : presentation->slides) {
        slide.is_cuda = true;
    }
#endif
    Editor editor(presentation);
    editor.SetMonoFont(fira_mono);

    // The editor uses window focus to determine if it should reload files.
    glfwSetWindowUserPointer(window, &editor);
    glfwSetWindowFocusCallback(window, [](GLFWwindow* window, int focused) {
        if (focused) {
            // Window gained focus - mark all files for reload check
            Editor* editor = static_cast<Editor*>(glfwGetWindowUserPointer(window));
            if (editor) {
                editor->OnWindowFocusGained();
            }
        }
    });

    auto ToggleFullscreen = [window_size, window](){
            static int w = window_size.x, h = window_size.y;
        if (glfwGetWindowMonitor(window)) {
            glfwSetWindowMonitor(window, nullptr, 100, 100, w, h, 0);
            glfwSetWindowSize(window, w, h);
        } else {
            // Get the size when windowed
            glfwGetWindowSize(window, &w, &h);
            GLFWmonitor* monitor = glfwGetPrimaryMonitor();
            const GLFWvidmode* mode = glfwGetVideoMode(monitor);
            glfwSetWindowMonitor(window, monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
        };
        g_settings.window_w = w;
        g_settings.window_h = h;
    };
    g_settings.ToggleFullscreen = ToggleFullscreen;

    auto OpenFolder = [&editor, &presentation](const std::string& path) {
        std::filesystem::current_path(path);
        presentation = std::make_shared<Presentation>(path);
        editor.SetPresentation(presentation);
        g_settings.current_folder = path;
        ImGui::SaveIniSettingsToDisk(ImGui::GetIO().IniFilename);
    };


    // Main loop
    std::string exception_what;
    std::string active_tab = editor.GetActiveTab();
    while (!glfwWindowShouldClose(window)) {
        // Poll and handle events (inputs, window resize, etc.)
        // You can read the io.WantCaptureMouse, io.WantCaptureKeyboard flags to tell if dear imgui wants to use your inputs.
        // - When io.WantCaptureMouse is true, do not dispatch mouse input data to your main application.
        // - When io.WantCaptureKeyboard is true, do not dispatch keyboard input data to your main application.
        // Generally you may always pass all inputs to dear imgui, and hide them from your application based on those two flags.
        glfwPollEvents();

        if (ImGui::IsKeyPressed(ImGuiKey_F11, false /* repeat */)) {
            ToggleFullscreen();
        }

        static bool presentation_mode = false;
        static int current_slide = 0;
        static bool current_slide_changed = true;
        if (ImGui::IsKeyPressed(ImGuiKey_F5)) {
            presentation_mode = io.KeyShift ? false : true;
            if (current_slide != 0) {
                current_slide = 0;
                current_slide_changed = true;
            }
        }
        if (ImGui::IsKeyPressed(ImGuiKey_F10)) {
            g_settings.notebook_mode = !g_settings.notebook_mode;
            ImGui::SaveIniSettingsToDisk(io.IniFilename);
            current_slide_changed = true;
        }

        // Start the Dear ImGui frame
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // Background windows:
        auto& io = ImGui::GetIO();
        float width = io.DisplaySize.x;
        float height = io.DisplaySize.y;
        int flags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                    ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus |
                    ImGuiWindowFlags_NoNavFocus |
                    ImGuiWindowFlags_NoSavedSettings;

        // 1. Code window
        if (!presentation_mode && !g_settings.notebook_mode) {
            ImVec2 code_window_size{ width/2, height };
            ImGui::SetNextWindowSize(code_window_size);
            ImGui::SetNextWindowPos(ImVec2(0, 0));

            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
            if (ImGui::Begin("Code", 0, flags | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_MenuBar)) {
                ImGui::PopStyleVar();
                if (ImGui::BeginMenuBar()) {
                    RenderMenu(editor, exception_what, ToggleFullscreen, OpenFolder);

                    ImGui::EndMenuBar();
                }
                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));

                editor.Render(exception_what);
            } // "Code" window
            ImGui::End();
            ImGui::PopStyleVar();
        }

        SourceFile &setup = presentation->setup;
        try {
            engine.compileSetup(setup);
        } catch (std::exception& e) {
            exception_what = e.what();
            ImGui::OpenPopup("Exception");
        }

        // 2. Presentation window
        float presentation_width = (presentation_mode || g_settings.notebook_mode) ? width : width/2;
        ImGui::SetNextWindowSize(ImVec2(presentation_width, height));
        ImGui::SetNextWindowPos(ImVec2((presentation_mode || g_settings.notebook_mode) ? 0 : presentation_width, 0));
        // Get rid of horizontal padding
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, ImVec2(0, 0));
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
        // Transparent scrollbar bg
        // Doesn't work, the scrollbar always affects clip rect and layout
        // if we want an overlaid scroll bar, we need to draw it ourselves
        // So disable the built in scrollbar completely
        flags |= ImGuiWindowFlags_NoScrollbar;
        // Let the presentation area contain 10 placeholder slides
        // of aspect ratio 16:10
        // Slides are designed for 1080p, 16:10 aspect ratio
        // TODO: Once ImGui::SetScale is better implemented across imgui and the glfw backend, we can
        //       use a fixed slide_size of 1728x1080 and use ImGui::PushScale() to accomplish this.
        ImVec2 slide_size{ presentation_width, presentation_width*10/16 };
        float slide_scale = slide_size.y / 1080.f;
        // Watch out, my PushScale implementation multiplies onto the current DPI scale
        // so we need to divide by that here.
        slide_scale /= dpi_scale;
        float text_height = ImGui::GetTextLineHeightWithSpacing();
        float setup_spacer_height = slide_size.y/2 - text_height;

        auto NextSlide = [&]() {
            int next_slide = current_slide + 1;
            if (next_slide >= (int)presentation->slides.size())
                next_slide = presentation->slides.size() - 1;
            if (next_slide != current_slide) {
                current_slide = next_slide;
                current_slide_changed = true;
            }
        };
        auto PreviousSlide = [&]() {
            int next_slide = current_slide - 1;
            int limit = presentation_mode ? 0 : -1;
            if (next_slide < limit)
                next_slide = limit;
            if (next_slide != current_slide) {
                current_slide = next_slide;
                current_slide_changed = true;
            }
        };
        auto GoToSlide = [&](int i) {
            int next_slide = i;
            int limit = presentation_mode ? 0 : -1;
            if (next_slide < limit)
                next_slide = limit;
            if (next_slide != current_slide) {
                current_slide = next_slide;
                current_slide_changed = true;
            }
        };
        ImVec2 current_slide_screen_pos(0, 0); // The top left corner of the current slide in screen coordinates

        // To get zero lag in presentation mode, use SetNextWindowScroll.
        if (presentation_mode) {
            if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) {
                NextSlide();
            }

            if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) {
                PreviousSlide();
            }
            ImGui::SetNextWindowScroll(ImVec2(-1, text_height + current_slide * (slide_size.y + 2*text_height)));
        }

        if (ImGui::Begin("Presentation", 0, flags)) {
            if (active_tab != editor.GetActiveTab()) {
                // If the active tab changed, we need to scroll to the top of the new tab
                current_slide = presentation->indexOf(presentation->getSourceFile(editor.GetActiveTab()));
                if (current_slide < -1) current_slide = -1;
            }

            // In presentation mode, scrolling was done before the Begin() call to reduce lag.
            bool allow_keyboard_scrolling = !presentation_mode;
            auto shift = io.KeyShift;
            auto ctrl = io.ConfigMacOSXBehaviors ? io.KeySuper : io.KeyCtrl;
            auto alt = io.ConfigMacOSXBehaviors ? io.KeyCtrl : io.KeyAlt;
            if (g_settings.notebook_mode && !presentation_mode) {

                allow_keyboard_scrolling = (ImGui::IsKeyPressed(ImGuiKey_UpArrow) && editor.IsCursorAtFirstLine()) ||
                                           (ImGui::IsKeyPressed(ImGuiKey_DownArrow) && editor.IsCursorAtLastLine());
                if (shift || ctrl || alt) {
                    // Don't allow keyboard scrolling in notebook mode if any modifier is pressed
                    allow_keyboard_scrolling = false;
                }
            }
            if (allow_keyboard_scrolling && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
                if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) {
                    NextSlide();
                }

                if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) {
                    PreviousSlide();
                }
            }

            if (ctrl && ImGui::IsKeyPressed(ImGuiKey_PageDown)) {
                if (g_settings.notebook_mode)
                    NextSlide();
                else
                    editor.ActivateNextTab();
                ImGui::SetKeyOwner(ImGuiKey_PageDown, ImGui::GetItemID(), ImGuiInputFlags_LockThisFrame);
            }

            if (ctrl && ImGui::IsKeyPressed(ImGuiKey_PageUp)) {
                if (g_settings.notebook_mode)
                    PreviousSlide();
                else
                    editor.ActivatePreviousTab();
                ImGui::SetKeyOwner(ImGuiKey_PageUp, ImGui::GetItemID(), ImGuiInputFlags_LockThisFrame);
            }

            active_tab = editor.GetActiveTab();

            // Before all the slides, the "setup" placeholder
            // Calculate the height of one row of ImGui::Text
            if (!presentation_mode) {
                if (g_settings.notebook_mode) {
                    auto top_left = ImGui::GetCursorScreenPos();
                    ImGui::Text("Setup %s", presentation->setup.dirty ? "*" : "");

                    if (current_slide == -1 && current_slide_changed) {
                        ImGui::SetScrollY(ImGui::GetCursorPosY() - text_height);
                        ImGui::SetNextWindowFocus();
                        // Prevent the editor from reacting to key up/down
                        ImGui::SetKeyOwner(ImGuiKey_DownArrow, ImGui::GetItemID(), ImGuiInputFlags_LockThisFrame);
                        ImGui::SetKeyOwner(ImGuiKey_UpArrow, ImGui::GetItemID(), ImGuiInputFlags_LockThisFrame);
                        current_slide_changed = false;
                    }

                    editor.RenderInline("setup", exception_what);
                    auto bottom_left = ImGui::GetCursorScreenPos();
                    ImU32 color = ImGui::GetColorU32(ImGuiCol_Border);
                    if (editor.GetActiveTab() == "setup")
                        color = ImGui::GetColorU32(ImGuiCol_HeaderActive);
                    ImGui::GetWindowDrawList()->AddRect(top_left, bottom_left + ImVec2(slide_size.x, 0), color);
                }

                SourceFile &setup = presentation->setup;
                ImGui::BeginChild("Setup", ImVec2(slide_size.x, setup_spacer_height), false);
                ImGui::Text("%s", setup.value.c_str());
                ImGui::EndChild();
            }
            for (int i = 0; i < 10; i++) {
                bool animate = current_slide_changed && presentation_mode && (i == current_slide);
                ImGui::PushID(i);
                auto top_left = ImGui::GetCursorScreenPos();
                if (i == current_slide)
                    current_slide_screen_pos = top_left;
                ImGui::Text("Slide %d %s", i, presentation->slides[i].dirty ? "*" : "");
                if (!presentation_mode) {
                    std::string slide_id = fmt::format("slide{}", i);
                    if (g_settings.notebook_mode) {
                        if (current_slide == i && current_slide_changed) {
                            ImGui::SetScrollY(ImGui::GetCursorPosY() - text_height);
                            ImGui::SetNextWindowFocus();
                            // Prevent the editor from reacting to key up/down
                            ImGui::SetKeyOwner(ImGuiKey_DownArrow, ImGui::GetItemID(), ImGuiInputFlags_LockThisFrame);
                            ImGui::SetKeyOwner(ImGuiKey_UpArrow, ImGui::GetItemID(), ImGuiInputFlags_LockThisFrame);
                            current_slide_changed = false;
                        }
                        editor.RenderInline(slide_id, exception_what);
                    } else {
                        ImGui::SameLine();
                        ImGui::SetCursorPosX(slide_size.x - ImGui::GetStyle().FramePadding.x * 2 -
                                            ImGui::CalcTextSize(ICON_MDI_REFRESH).x -
                                            ImGui::CalcTextSize(ICON_MDI_PENCIL).x);
                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
                        if (ImGui::Button(ICON_MDI_PENCIL))
                            editor.ActivateTab(slide_id);
                        ImGui::SameLine();
                        if (ImGui::Button(ICON_MDI_REFRESH))
                            animate = true;
                        ImGui::PopStyleColor(1);
                    }
                    auto bottom_left = ImGui::GetCursorScreenPos();
                    // The problem here is that the drawlist uses window coordinates.
                    // We need to convert the coordinates to window coordinates.
                    // We can do this by using the cursor position.
                    ImU32 color = ImGui::GetColorU32(ImGuiCol_Border);
                    if (editor.GetActiveTab() == slide_id) {
                        color = ImGui::GetColorU32(ImGuiCol_HeaderActive);
                    }
                    ImGui::GetWindowDrawList()->AddRect(top_left, bottom_left + ImVec2(slide_size.x, 0), color);
                }

                ImGui::BeginChild("Slide", slide_size, false);
                ImGui::BeginAnimated(animate);
                ImGui::PushFont(fira_sans_big);
                ImGui::PushScale(slide_scale);

                SourceFile &slide_src = presentation->slides[i];
                try {
                    engine.compileSlide(slide_src);
                } catch (std::exception& e) {
                    exception_what = e.what();
                    ImGui::OpenPopup("Exception");
                }
                ImGuiErrorRecoveryState state;
                ImGui::ErrorRecoveryStoreState(&state);
                try {
                    if (slide_src.function) slide_src.function();
                    else                    ImGui::Text("%s", slide_src.value.c_str());
                } catch (std::exception& e) {
                    ImGui::ErrorRecoveryTryToRecoverState(&state);
                    slide_src.exception = e.what();
                    //std::cerr << "Script exception in slide " << i << ": " << e.what() << std::endl;
                }
                ImGui::PopScale();
                ImGui::PopFont();
                ImGui::EndAnimated();
                ImGui::EndChild();
                if (!slide_src.exception.empty()) {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 0, 0, 1));
                    ImGui::Text("Exception: %s", slide_src.exception.c_str());
                    ImGui::PopStyleColor();
                } else {
                    ImGui::Text(""); // Just add some space for symmetry
                }
                ImGui::PopID();
            }

            // Add a spacer to allow the last slide to be scrolled into view and centered vertically
            ImGui::BeginChild("Final Spacer", ImVec2(slide_size.x, slide_size.y/2 - text_height), false);
            ImGui::EndChild();
            current_slide_changed = false;
        } // "Presentation" window
        ImGui::End();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(5);

        // 3. Overlays
        ImGuiWindowFlags overlay_flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                         ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize |
                                         ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;
        ImGuiViewport* viewport = ImGui::GetMainViewport();

        // Floating editor toolbar
        if (!presentation_mode) {
            // Toolbars don't steal focus! But I can't figure out how to make them not steal focus, so we just manually restore focus
            ImGuiWindow* prev_focus_window = ImGui::GetCurrentContext()->NavWindow;

            ImVec2 toolbar_size = ImVec2(0, ImGui::GetTextLineHeightWithSpacing());
            static float toolbar_measured_width = 0.f;
            toolbar_size.x = toolbar_measured_width;
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
            // Position the toolbar at the top right corner of the viewport, leaving margin equal to the toolbar height
            ImVec2 toolbar_pos(viewport->Pos.x + viewport->Size.x/2 - toolbar_size.x - toolbar_size.y - 1,
                               viewport->Pos.y + toolbar_size.y*3);
            if (g_settings.notebook_mode) {
                toolbar_pos.x += viewport->Size.x/2;
                toolbar_pos.y = std::max(0.f, current_slide_screen_pos.y - toolbar_size.y/2);
            }
            ImGui::SetNextWindowPos(toolbar_pos, ImGuiCond_Always);
            if (ImGui::Begin("EditorToolbar", nullptr, overlay_flags)) {
                // Measure the width of the toolbar buttons
                float cursor_pos_x = ImGui::GetCursorPosX();
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
                if (ImGui::Button(ICON_MDI_PLAY_OUTLINE "##play")) {}
                ImGui::SameLine();
                if (ImGui::Button(ICON_MDI_ARROW_SPLIT_HORIZONTAL "##split")) {}
                ImGui::SameLine();
                if (ImGui::Button(ICON_MDI_DOTS_HORIZONTAL "##overflow")) {}
                ImGui::SameLine();
                if (ImGui::Button(ICON_MDI_TRASH_CAN_OUTLINE "##delete")) {}
                ImGui::SameLine(); // Needed to actually measure the width including the last button
                toolbar_measured_width = ImGui::GetCursorPosX() - cursor_pos_x;
                ImGui::PopStyleColor();
            }
            ImGui::End();
            ImGui::PopStyleVar(2);

            // Toolbar: restore focus
            if (prev_focus_window && prev_focus_window != ImGui::GetCurrentContext()->NavWindow)
                ImGui::FocusWindow(prev_focus_window);
        }

        // Hamburger menu overlay (only in notebook mode)
        if (g_settings.notebook_mode) {
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
            float width = ImGui::GetStyle().FramePadding.x * 2 +
                          ImGui::CalcTextSize(ICON_MDI_MENU).x;
            float height = ImGui::GetTextLineHeightWithSpacing();
            //ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));

            ImVec2 pos = ImVec2(
                viewport->Pos.x + viewport->Size.x - width,
                viewport->Pos.y
            );

            ImGui::SetNextWindowPos(pos, ImGuiCond_Always);
            ImGui::SetNextWindowBgAlpha(0.0f); // Transparent background
            ImGui::SetNextWindowSize(ImVec2(width, height));

            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
            if (ImGui::Begin("HamburgerOverlay", nullptr, ImGuiWindowFlags_NoBackground | overlay_flags)) {
                if (ImGui::Button(ICON_MDI_MENU "##hamburger"))
                    ImGui::OpenPopup("HamburgerMenu");

                ImGui::PopStyleVar();
                if (ImGui::BeginPopup("HamburgerMenu")) {
                    RenderMenu(editor, exception_what, ToggleFullscreen, OpenFolder);
                    ImGui::EndPopup();
                }
                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
            }
            ImGui::PopStyleColor();
            ImGui::PopStyleVar(2);
            ImGui::End();
        }

        // 4. TextEditor dialogs
        editor.RenderPopups(exception_what);

        if (ImGui::BeginPopupModal("Exception", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("%s", exception_what.c_str());
            if (ImGui::Button("OK"))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        // 1. Show the big demo window (Most of the sample code is in ImGui::ShowDemoWindow()! You can browse its code to learn more about Dear ImGui!).
        if (show_demo_window)
            ImGui::ShowDemoWindow(&show_demo_window);

        // 2. Show a simple window that we create ourselves. We use a Begin/End pair to created a named window.
        if (false) {
            static int counter = 0;

            ImGui::Begin("Hello, world!");                          // Create a window called "Hello, world!" and append into it.

            ImGui::Text("This is some useful text.");               // Display some text (you can use a format strings too)
            ImGui::Checkbox("Demo Window", &show_demo_window);      // Edit bools storing our window open/close state
            ImGui::Checkbox("Another Window", &show_another_window);

            ImGui::SliderFloat("float", &f_adjust, 0.0f, 1.0f);            // Edit 1 float using a slider from 0.0f to 1.0f
            ImGui::ColorEdit3("clear color", (float*)&clear_color); // Edit 3 floats representing a color

            if (ImGui::Button("Button"))                            // Buttons return true when clicked (most widgets return true when edited/activated)
                counter++;
            ImGui::SameLine();
            ImGui::Text("counter = %d", counter);

            ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);

            // Interpreter dump controls
            static const char* dump_options[] = {
                "asttree", "ast", "decl", "undo"
            };
            static int current_dump_option = 0;
            static char dump_filter[256] = "";

            ImGui::Text("Interpreter Dump:");
            ImGui::Combo("What", &current_dump_option, dump_options, IM_ARRAYSIZE(dump_options));
            ImGui::InputText("Filter", dump_filter, sizeof(dump_filter));
            if (ImGui::Button("Dump")) {
                engine.dump(dump_options[current_dump_option], dump_filter);
            }

            ImGui::End();
        }

        // 3. Show another simple window.
        if (show_another_window)
        {
            ImGui::Begin("Another Window", &show_another_window);   // Pass a pointer to our bool variable (the window will have a closing button that will clear the bool when clicked)
            ImGui::Text("Hello from another window!");
            if (ImGui::Button("Close Me"))
                show_another_window = false;
            ImGui::End();
        }

        // Rendering
        ImGui::Render();
        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(clear_color.x * clear_color.w, clear_color.y * clear_color.w, clear_color.z * clear_color.w, clear_color.w);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
    }

    // Get the window position and size, store to settings
    // TODO: Handle fullscreen mode. For now, don't save window if it's fullscreen.
    if (!glfwGetWindowMonitor(window)) {
        int x, y;
        glfwGetWindowPos(window, &x, &y);
        glfwGetWindowSize(window, &g_settings.window_w, &g_settings.window_h);
        g_settings.window_x = x;
        g_settings.window_y = y;
    }


    // Cleanup
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImPlot3D::DestroyContext();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();

    if (nfd_initialized)
        NFD::Quit();

    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}