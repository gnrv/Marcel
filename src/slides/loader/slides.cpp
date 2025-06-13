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

#include "setup.cpp"

std::vector<std::function<std::function<void()>()>> get_slide_loaders()
{
    std::vector<std::function<std::function<void()>()>> slide_loaders(10);

    // The cpp file defines and returns an "update" lambda that will be called later, each frame, to render the ImGui
    // interface for the slide.
    slide_loaders[0] = []() -> std::function<void()> {
        #include "slide0.cpp"
    };
    slide_loaders[1] = []() -> std::function<void()> {
        #include "slide1.cpp"
    };
    slide_loaders[2] = []() -> std::function<void()> {
        #include "slide2.cpp"
    };
    slide_loaders[3] = []() -> std::function<void()> {
        #include "slide3.cpp"
    };
    slide_loaders[4] = []() -> std::function<void()> {
        #include "slide4.cpp"
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

    return slide_loaders;
}
