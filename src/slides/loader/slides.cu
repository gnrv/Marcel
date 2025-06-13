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

#include "setup.cu"

std::vector<std::function<std::function<void()>()>> get_slide_loaders()
{
    std::vector<std::function<std::function<void()>()>> slide_loaders{

    // The cu file defines and returns an "update" lambda that will be called later, each frame, to render the ImGui
    // interface for the slide.
    []() -> std::function<void()> {
        #include "slide0.cpp"
    },
    []() -> std::function<void()> {
        #include "slide1.cu"
    },
    []() -> std::function<void()> {
        #include "slide2.cu"
    },
    []() -> std::function<void()> {
        #include "slide3.cu"
    },
    []() -> std::function<void()> {
        #include "slide4.cu"
    },
    []() -> std::function<void()> {
        #include "slide5.cpp"
    },
    []() -> std::function<void()> {
        #include "slide6.cpp"
    },
    []() -> std::function<void()> {
        #include "slide7.cpp"
    },
    []() -> std::function<void()> {
        #include "slide8.cpp"
    },
    []() -> std::function<void()> {
        #include "slide9.cpp"
    }
    };

    return slide_loaders;
}
