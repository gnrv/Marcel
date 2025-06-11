#include "slides.h"

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

std::vector<std::function<std::function<void()>()>> slide_loaders;

std::vector<std::function<std::function<void()>()>> get_slide_loaders() {
    static bool initialized = false;
    if (!initialized) {
        initialized = true;
        add_cpp_slides(slide_loaders);
        add_cuda_slides(slide_loaders);
    }

    return slide_loaders;
}

void add_cpp_slides(std::vector<std::function<std::function<void()>()>> &slide_loaders)
{
    // The cpp file defines and returns an "update" lambda that will be called later, each frame, to render the ImGui
    // interface for the slide.
    slide_loaders[0] = []() -> std::function<void()> {
        #include "slide0.cpp"
    };
    slide_loaders[5] = []() -> std::function<void()> {
        #include "slide5.cpp"
    };
    slide_loaders[6] = []() -> std::function<void()> {
        #include "slide6.cpp"
    };
    slide_loaders[7] = []() -> std::function<void()> {
        #include "slide7.cpp"
    };
    slide_loaders[8] = []() -> std::function<void()> {
        #include "slide8.cpp"
    };
    slide_loaders[9] = []() -> std::function<void()> {
        #include "slide9.cpp"
    };
}
