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

void add_cuda_slides(std::vector<std::function<std::function<void()>()>> &slide_loaders)
{
    // The cu file defines and returns an "update" lambda that will be called later, each frame, to render the ImGui
    // interface for the slide.
    slide_loaders[1] = []() -> std::function<void()> {
        #include "slide1.cu"
    };
    slide_loaders[2] = []() -> std::function<void()> {
        #include "slide2.cu"
    };
    slide_loaders[3] = []() -> std::function<void()> {
        #include "slide3.cu"
    };
    slide_loaders[4] = []() -> std::function<void()> {
        #include "slide4.cu"
    };
}
