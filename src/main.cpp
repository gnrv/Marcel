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

#ifdef USE_CLING
#include "clang/AST/Mangle.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendActions.h"
#include "cling/Interpreter/Interpreter.h"
#include "cling/Interpreter/Transaction.h"
#include "cling/Interpreter/Value.h"
#include "cling/Utils/Output.h"
#include "cling/MetaProcessor/InputValidator.h"
#include "llvm/ExecutionEngine/Orc/LLJIT.h"
#endif

#include "system/sys_util.h"
#include "system/stdcapture.h"

#ifndef USE_CLING
#include "GL/gl.h"
#include "GLFW/glfw3.h"
#include "imgui.h"
#include "imgui_latex.h"
#include "implot.h"
#include "implot3d.h"
#include "cmath"
#include "cstdio"
#include "algorithm"
#include "iostream"
#include "imga.h"
// Include setup slide - make sure you have a -I flag pointing to the presentation directory
#include "setup.cpp"
#endif

#include <nfd.hpp>
#include <nfd_glfw3.h>

static float f_adjust = 0.0f;

#ifdef USE_CLING
std::string exprToString(clang::Expr* expr, const clang::ASTContext& context) {
    clang::LangOptions langOpts;
    langOpts.CPlusPlus = true;
    clang::PrintingPolicy policy(langOpts);
    policy.AnonymousTagLocations = false;
    policy.SuppressUnwrittenScope = true;

    std::string str;
    llvm::raw_string_ostream os(str);
    expr->printPretty(os, nullptr, policy);
    return os.str();
}

std::string findResultExprFromExtractionFunction(cling::Transaction* tx) {
    for (auto it = tx->rdecls_begin(); it != tx->rdecls_end(); ++it) {
        for (clang::DeclGroupRef::const_iterator I = it->m_DGR.begin(), E = it->m_DGR.end(); I != E; ++I) {

            auto* func = llvm::dyn_cast<clang::FunctionDecl>(*I);
            if (!func) continue;
            // Apparently not all functions have a name, but we can always getNameAsString().
            //if (!func->getName().starts_with("__cling_")) continue;
            if (!func->getNameAsString().starts_with("__cling_")) continue;

            // From ValueExtractionSynthesizer.cpp:
            // We need to synthesize later:
            // Wrapper has signature: void w(cling::Value SVR)
            // case 1):
            //   setValueNoAlloc(gCling, &SVR, lastExprTy, lastExpr())
            // case 2):
            //   new (setValueWithAlloc(gCling, &SVR, lastExprTy)) (lastExpr)
            // case 2.1):
            //   copyArray(src, placement, size)
            if (auto* body = llvm::dyn_cast<clang::CompoundStmt>(func->getBody())) {
                for (auto* stmt : body->body()) {
                    clang::CallExpr* call = nullptr;
                    clang::CXXNewExpr* cxxNew = nullptr;
                    // There are two cases: explicit return statement or implicit return of the last expression.
                    if (auto* ret = llvm::dyn_cast<clang::ReturnStmt>(stmt)) {
                        if (auto* maybeCall = llvm::dyn_cast<clang::CallExpr>(ret->getRetValue()->IgnoreImpCasts())) {
                            call = maybeCall;
                        }
                        if (auto * maybeCxxNew = llvm::dyn_cast<clang::CXXNewExpr>(ret->getRetValue()->IgnoreImpCasts())) {
                            cxxNew = maybeCxxNew;
                        }
                    } else if (auto* maybeCall = llvm::dyn_cast<clang::CallExpr>(stmt)) {
                        call = maybeCall;
                    } else if (auto* maybeCxxNew = llvm::dyn_cast<clang::CXXNewExpr>(stmt)) {
                        cxxNew = maybeCxxNew;
                    }
                    if (call) {
                        if (auto* callee = call->getDirectCallee()) {
                            // case 1): lastExpr is the fourth argument of setValueNoAlloc
                            if (callee->getName() == "setValueNoAlloc") {
                                if (call->getNumArgs() >= 5) {
                                    clang::Expr* last_arg = call->getArg(4)->IgnoreImpCasts();
                                    // if (auto* decl_ref = llvm::dyn_cast<clang::DeclRefExpr>(last_arg)) {
                                    //     return decl_ref->getDecl()->getNameAsString();
                                    // }
                                    std::string expr = exprToString(last_arg, func->getASTContext());
                                    if (expr.starts_with("(void *)")) {
                                        expr = expr.substr(8); // Remove "(void *)"
                                    }
                                    return expr;
                                }
                            }
                        }
                    }
                    if (cxxNew) {
                        // case 2): lastExpr is the argument of placement new
                        //          but we need to check if the destination address is a call to setValueWithAlloc
                        bool is_placement_new = cxxNew->getNumPlacementArgs() > 0;
                        if (is_placement_new) {
                            if (auto* maybeCall = llvm::dyn_cast<clang::CallExpr>(cxxNew->getPlacementArg(0)->IgnoreImpCasts())) {
                                call = maybeCall;
                            }
                            if (call) {
                                if (auto* callee = call->getDirectCallee()) {
                                    if (callee->getName() == "setValueWithAlloc") {
                                        // OK, now we know that the address expression in the placement new
                                        // is a call to setValueWithAlloc, so we can extract the last expression.
                                        // Remember, this is the last expression of the call to _new_, not setValueWithAlloc.
                                        clang::Expr* initializer = cxxNew->getInitializer();
                                        std::string expr = exprToString(initializer, func->getASTContext());
                                        return expr;
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    return std::string();
}
#endif // USE_CLING

void extractMarkers(SourceFile &source_file, const char *buf, size_t size, size_t offset = 0) {
    source_file.error_markers.clear();
    std::string buf_str(buf, size);
    size_t line_no = 0;
    auto colon_pos = buf_str.find(':');
    if (colon_pos != std::string::npos) {
        auto line_str = buf_str.substr(colon_pos + 1);
        try {
            line_no = std::stoi(line_str);
        } catch (const std::invalid_argument& e) {
            line_no = 1;
        } catch (const std::out_of_range& e) {
            line_no = 1;
        }
    }

    line_no += offset;
    line_no = std::min(line_no, source_file.lines);
    line_no = std::max(line_no, size_t(1));

    // For now, strip out the ansi color codes
    buf_str = std::regex_replace(buf_str, std::regex("\033\\[[0-9;]*m"), "");
    source_file.error_markers.emplace(line_no, buf_str);
}

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

std::string OpenFolderDialog() {
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
#ifdef USE_CLING
        true
#else
        false
#endif
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

#ifdef USE_CLING
    // Add --ptrcheck to argc, argv
    std::vector<const char*> new_argv;
    for (int i = 0; i < argc; ++i) {
        new_argv.push_back(argv[i]);
    }
    //new_argv.push_back("--ptrcheck");
    argc = new_argv.size();
    cling::Interpreter interp(argc, new_argv.data());

    // The interpreter has so much internal state going on in order to support incremental parsing,
    // I dont' dare to use it for syntax checking even. Let's create another interpreter for that.
    // We need to pass -fsyntax-only to the compiler.
    // std::vector<const char*> syntax_argv = {argv[0], "-fsyntax-only"};
    // cling::Interpreter syntax(argc, syntax_argv.data());

    // auto result = interp.loadLibrary("libimgui.so");
    // if (result != cling::Interpreter::kSuccess) {
    //     std::cerr << "Failed to load imgui library: " << result << std::endl;
    //     exit(1);
    // }
    // Add the imgui source directory to the include path
    interp.AddIncludePath(getExecutablePath() + "/../external/imgui/imga");
    interp.AddIncludePath(getExecutablePath() + "/../external/imgui/imgui");
    interp.AddIncludePath(getExecutablePath() + "/../external/imgui/implot");
    interp.AddIncludePath(getExecutablePath() + "/../external/imgui/implot3d");
    interp.AddIncludePath(getExecutablePath() + "/../external/imgui/imlatex");
    interp.AddIncludePath(getExecutablePath() + "/../external");
    interp.AddIncludePath(getExecutablePath() + "/../external/nlohmann/json/include");
    // Pre-include it
    std::vector<std::string> headers = {
        "GL/gl.h",
        "GLFW/glfw3.h",
        "imgui.h",
        "imgui_latex.h",
        "implot.h",
        "implot3d.h",
        "cmath",
        "cstdio",
        "algorithm",
        "iostream",
        "imga.h"
    };
    for (const auto& header : headers) {
        auto result = interp.loadHeader(header);
        if (result != cling::Interpreter::kSuccess) {
            std::cerr << "Failed to load header: " << result << std::endl;
            exit(1);
        }
    }

    // Tell cling to allow re-definitions
    interp.getRuntimeOptions().AllowRedefinition = true;
#else
    (void)argc;
    (void)argv;
#endif

    // Setup window
    glfwSetErrorCallback([](int error, const char* description) {
        fprintf(stderr, "Glfw Error %d: %s\n", error, description);
    });
    if (!glfwInit())
        return 1;

    NFD::Init();

    // GL 3.0 + GLSL 130
    const char* glsl_version = "#version 130";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    //glfwWindowHint( GLFW_DECORATED, GLFW_FALSE );
    //glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);  // 3.2+ only
    //glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);            // 3.0+ only

    // On WSL2, the scale returned is 1.0, even though Windows is using a scale of 200%.
    // Watch out, because WSLg might be just blowing up the window to 2x! Resulting in a
    // nastly pixellated look. Our framebuffer is still just 1x.
    // Get rid of that totally fake scaling using
    // $ cat /mnt/c/Users/<user>/.wslgconfig
    // [system-distro-env]
    // WESTON_RDP_DEBUG_DESKTOP_SCALING_FACTOR=100
    float dpi_scale = 1.f;
    float window_size_scale_factor = 1.f;
    bool is_wsl2 = false;
    std::ifstream file("/proc/version");
    if (file.good()) {
        std::string line;
        std::getline(file, line);
        if (line.find("WSL") != std::string::npos) {
            // WSL2 detected
            is_wsl2 = true;
            dpi_scale = 2.f; // Or whatever your setting is in Windows, e.g. 200% is 2.f
            window_size_scale_factor = 1.f; // For the window size
        }
    }

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
                                          "Dear ImGui GLFW+OpenGL3 example", NULL, NULL);
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
    //io.Fonts->AddFontDefault();
    //io.Fonts->AddFontFromFileTTF("../../misc/fonts/Roboto-Medium.ttf", base_font_size);
    const float base_font_size = 16.0f;
    ImFont *fira_sans = io.Fonts->AddFontFromFileTTF("../data/fonts/fira/FiraSans-Regular.ttf", base_font_size*dpi_scale);
    (void)fira_sans; // We don't use this font, it's the default

    ImFontConfig config;
    config.MergeMode = true;
    config.GlyphMinAdvanceX = base_font_size; // Use if you want to make the icon monospaced
    static const ImWchar icon_ranges[] = { ICON_MIN_MDI, ICON_MAX_MDI, 0 };
    io.Fonts->AddFontFromFileTTF("../data/fonts/material-design-icons/materialdesignicons-webfont.ttf", base_font_size*dpi_scale, &config, icon_ranges);

    // Presentation sizes
    ImFont *fira_sans_big = io.Fonts->AddFontFromFileTTF("../data/fonts/fira/FiraSans-Regular.ttf", 48.0f*dpi_scale);
    ImFont *fira_sans_small = io.Fonts->AddFontFromFileTTF("../data/fonts/fira/FiraSans-Regular.ttf", 32.0f*dpi_scale);

    ImFont *fira_mono = io.Fonts->AddFontFromFileTTF("../data/fonts/fira/FiraMono-Regular.ttf", base_font_size*dpi_scale);
    //io.Fonts->AddFontFromFileTTF("../../misc/fonts/DroidSans.ttf", base_font_size);
    //io.Fonts->AddFontFromFileTTF("../../misc/fonts/ProggyTiny.ttf", 10.0f);
    //ImFont* font = io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\ArialUni.ttf", 18.0f, NULL, io.Fonts->GetGlyphRangesJapanese());
    //IM_ASSERT(font != NULL);

    // Our state
    bool show_demo_window = false;
    bool show_another_window = false;
    ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);
    //ImVec4 clear_color = ImVec4(0.f, 0.f, 0.f, 1.00f);

    std::filesystem::current_path(g_settings.current_folder);
    std::shared_ptr<Presentation> presentation = std::make_shared<Presentation>(g_settings.current_folder);
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

#ifndef USE_CLING
    // The cpp file defines and returns an "update" lambda that will be called later, each frame, to render the ImGui
    // interface for the slide.
    std::vector<std::function<std::function<void()>()>> slide_loaders;
    slide_loaders.push_back([]() -> std::function<void()> {
        #include "slide0.cpp"
    });
    slide_loaders.push_back([]() -> std::function<void()> {
        #include "slide1.cpp"
    });
    slide_loaders.push_back([]() -> std::function<void()> {
        #include "slide2.cpp"
    });
    slide_loaders.push_back([]() -> std::function<void()> {
        #include "slide3.cpp"
    });
    slide_loaders.push_back([]() -> std::function<void()> {
        #include "slide4.cpp"
    });
    slide_loaders.push_back([]() -> std::function<void()> {
        #include "slide5.cpp"
    });
    slide_loaders.push_back([]() -> std::function<void()> {
        #include "slide6.cpp"
    });
    slide_loaders.push_back([]() -> std::function<void()> {
        #include "slide7.cpp"
    });
    slide_loaders.push_back([]() -> std::function<void()> {
        #include "slide8.cpp"
    });
    slide_loaders.push_back([]() -> std::function<void()> {
        #include "slide9.cpp"
    });

    for (int i = 0; i < 10; ++i) {
        auto& slide = presentation->slides[i];
        if (i < slide_loaders.size()) {
            slide.function = slide_loaders[i]();
        }
    }
#endif

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

#ifdef USE_CLING
        SourceFile &setup = presentation->setup;
        if (!setup.validated) {
            try {
                cling::InputValidator validator;
                auto result = validator.validate(setup.text());
                setup.setValidated(result == cling::InputValidator::kComplete);
            } catch (std::exception& e) {
                exception_what = e.what();
                ImGui::OpenPopup("Exception");
            }
        }

        if (setup.validated && !setup.compiled && !setup.syntax_error) {
            setup.error_markers.clear();
            CaptureStderr cap([&](const char* buf, size_t szbuf) {
                extractMarkers(setup, buf, szbuf);
            });
            cling::Value V;
            auto result = interp.process(setup.text(), &V, nullptr, true /* disableValuePrinting */);
            setup.compiled = true;
            setup.syntax_error = result != cling::Interpreter::kSuccess;

            setup.value.clear();
            setup.function = nullptr;
            if (V.isValid()) {
                llvm::raw_string_ostream os(setup.value);
                V.print(os);
                os.flush();
            }

        }
#endif

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
#ifdef USE_CLING
                if (!slide_src.validated) {
                    try {
                        cling::InputValidator validator;
                        auto result = validator.validate(slide_src.text());
                        slide_src.setValidated(result == cling::InputValidator::kComplete);
                    } catch (std::exception& e) {
                        exception_what = e.what();
                        ImGui::OpenPopup("Exception");
                    }
                }

                if (slide_src.validated && !slide_src.compiled && !slide_src.syntax_error) {
                    slide_src.error_markers.clear();
                    CaptureStderr cap([&](const char* buf, size_t szbuf) {
                        extractMarkers(slide_src, buf, szbuf, -1);
                    });

                    // If we disable value printing, we don't have to export symbols from the executable
                    // to shared libraries.
                    cling::Value V;
                    cling::Transaction *transaction = nullptr;
                    //auto result = interp.process("void (*update)(ImVec2 slide_size) = [](ImVec2 slide_size){" + slide_src.text() + ";}; update", &V, &transaction, true /* disableValuePrinting */);
                    auto result = interp.process(slide_src.text(), &V, &transaction, true /* disableValuePrinting */);

                    slide_src.compiled = true;
                    if (result != cling::Interpreter::kSuccess) {
                        slide_src.syntax_error = true;
                    } else {
                        slide_src.syntax_error = false;
                    }
                    // The value in lastV should be a function that we call to re-render the slide
                    slide_src.function = nullptr;
                    slide_src.value.clear();

                    if (V.isValid() && transaction) {
                        // cling::log() << "Transaction Decls for slide " << i << ":\n";
                        // for (cling::Transaction::iterator it = transaction->decls_begin();
                        //     it != transaction->decls_end(); ++it) {
                        //     it->dump();
                        // }
                        // cling::log().flush();
                        // cling::log() << "Deserialized Translation Decls for slide " << i << ":\n";
                        // for (cling::Transaction::iterator it = transaction->deserialized_decls_begin();
                        //     it != transaction->deserialized_decls_end(); ++it) {
                        //     it->dump();
                        // }
                        // cling::log().flush();
                        std::string expr = findResultExprFromExtractionFunction(transaction);
                        //std::cerr << "Result expr for slide " << i << ": " << expr << std::endl;

                        bool is_record = false;
                        void *ptr = nullptr;
                        auto T = V.getType().getCanonicalType().getTypePtrOrNull();
                        if (const auto *PtrTy = llvm::dyn_cast<clang::PointerType>(T)) {
                            const clang::Type *Pointee = PtrTy->getPointeeType().getTypePtr();
                            if (const auto *FuncTy = llvm::dyn_cast<clang::FunctionProtoType>(Pointee)) {
                                // It's a function pointer!
                                // You can now inspect the argument types:
                                for (unsigned i = 0; i < FuncTy->getNumParams(); ++i) {
                                    clang::QualType ArgType = FuncTy->getParamType(i);
                                    // ... process ArgType ...
                                }
                                clang::QualType ReturnType = FuncTy->getReturnType();
                                ptr = V.getPtr();
                            }
                        } else if (const auto *RefTy = llvm::dyn_cast<clang::ReferenceType>(T)) {
                            // Reference type (covers both lvalue and rvalue references)
                            const clang::Type *Pointee = RefTy->getPointeeType().getTypePtr();
                            if (const auto *CXXRD = Pointee->getAsCXXRecordDecl()) {
                                if (CXXRD->isLambda()) {
                                    // This is a reference (or pointer) to a lambda!
                                    for (const auto *Method : CXXRD->methods()) {
                                        if (Method->getOverloadedOperator() == clang::OO_Call) {
                                            // This is the lambda's operator()
                                            // You can inspect its parameters and return type:
                                            for (const auto *Param : Method->parameters()) {
                                                clang::QualType ParamType = Param->getType();
                                                // ... process ParamType ...
                                            }
                                            clang::QualType ReturnType = Method->getReturnType();

                                            // Get the lambda object from the reference
                                            ptr = V.getPtr();
                                        }
                                    }
                                }
                            }
                        } else if (const auto *RecTy = llvm::dyn_cast<clang::RecordType>(T)) {
                            // Record type (e.g. a struct or class)
                            const clang::CXXRecordDecl *CXXRD = RecTy->getAsCXXRecordDecl();
                            if (CXXRD && CXXRD->isLambda()) {
                                // This is a lambda, we can call it
                                for (const auto *Method : CXXRD->methods()) {
                                    if (Method->getOverloadedOperator() == clang::OO_Call) {
                                        // This is the lambda's operator()
                                        // You can inspect its parameters and return type:
                                        for (const auto *Param : Method->parameters()) {
                                            clang::QualType ParamType = Param->getType();
                                            // ... process ParamType ...
                                        }
                                        clang::QualType ReturnType = Method->getReturnType();

                                        // Get the lambda object from the reference
                                        is_record = true;
                                        ptr = V.getPtr();
                                    }
                                }
                            }
                        }
                        if (ptr && !expr.empty()) {
                            // If expr has the type pointer or reference to lambda:
                            std::string code = fmt::format(
                                "(*reinterpret_cast<decltype({})>(0x{:x}))();",
                                expr,
                                reinterpret_cast<uintptr_t>(ptr));
                            // If expr has the type of the lambda:
                            if (is_record) {
                                code = fmt::format(
                                    "auto similar_lambda = {}; (*reinterpret_cast<decltype(similar_lambda)*>(0x{:x}))();",
                                    expr,
                                    reinterpret_cast<uintptr_t>(ptr));
                            }
                            slide_src.last_transaction = nullptr;
                            slide_src.function = [&interp, code, &slide_src]() {
                                cling::Value V;
                                if (slide_src.last_transaction)
                                    interp.reevaluate(slide_src.last_transaction, nullptr);
                                else {
                                    CaptureStderr cap([&](const char* buf, size_t szbuf) {
                                        extractMarkers(slide_src, buf, szbuf, -1);
                                    });
                                    auto result = interp.evaluate(code, V, &slide_src.last_transaction);
                                    if (result != cling::Interpreter::kSuccess) {
                                        // If we failed to evaluate, kill this function to prevent further calls
                                        slide_src.last_transaction = nullptr;
                                        slide_src.function = nullptr;
                                    }
                                }
                            };
                        }
                        if (!slide_src.function) {
                            // If we don't have a function, we can still print the value
                            llvm::raw_string_ostream os(slide_src.value);
                            V.print(os);
                            os.flush();
                        }
                    }
                }
#endif
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
        if (g_settings.notebook_mode) {
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
            float width = ImGui::GetStyle().FramePadding.x * 2 +
                          ImGui::CalcTextSize(ICON_MDI_MENU).x;
            float height = ImGui::GetTextLineHeightWithSpacing();
            //ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));

            ImGuiViewport* viewport = ImGui::GetMainViewport();
            ImVec2 pos = ImVec2(
                viewport->Pos.x + viewport->Size.x - width,
                viewport->Pos.y
            );

            ImGui::SetNextWindowPos(pos, ImGuiCond_Always);
            ImGui::SetNextWindowBgAlpha(0.0f); // Transparent background
            ImGui::SetNextWindowSize(ImVec2(width, height));
            ImGuiWindowFlags flags = ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                    ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize |
                                    ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;

            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
            if (ImGui::Begin("HamburgerOverlay", nullptr, flags)) {
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
            #ifdef USE_CLING
                interp.dump(dump_options[current_dump_option], dump_filter);
            #else
                std::cout << "Cling not available - dump not supported" << std::endl;
            #endif
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

    NFD::Quit();

    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}