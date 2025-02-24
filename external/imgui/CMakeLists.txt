# Require minimum standard version of CMake
cmake_minimum_required (VERSION 3.14)

# Set C++ 17 compiler flags
set(CMAKE_CXX_STANDARD 17)

# Set project name
project (imgui LANGUAGES CXX)

# Set the source files to a list
set(source_list
    imgui/imconfig.h
    imgui/imgui.cpp
    imgui/imgui.h
    imgui/imgui_demo.cpp
    imgui/imgui_draw.cpp
    imgui/misc/cpp/imgui_stdlib.h
    imgui/misc/cpp/imgui_stdlib.cpp
    imgui/imgui_internal.h
    imgui/imgui_tables.cpp
    imgui/imgui_widgets.cpp
    imgui/imstb_rectpack.h
    imgui/imstb_textedit.h
    imgui/imstb_truetype.h
    imgui/backends/imgui_impl_glfw.cpp
    imgui/backends/imgui_impl_opengl3.cpp
#   imgui/misc/freetype/imgui_freetype.cpp

    implot/implot.h
    implot/implot.cpp
    implot/implot_internal.h
    implot/implot_items.cpp

    implot3d/implot3d.h
    implot3d/implot3d.cpp
    implot3d/implot3d_internal.h
    implot3d/implot3d_items.cpp

    extra/imgui_scale.h
    extra/imgui_scale.cpp

    imga/imga.h
    imga/imga.cpp
)


# Create executable file as project name
add_library(imgui SHARED ${source_list})

# If using IMGUI_ENABLE_STB_TRUETYPE
# find_package(Freetype REQUIRED)
# target_link_libraries(imgui Freetype::Freetype)
target_compile_definitions(imgui PUBLIC IMGUI_USE_WCHAR32)
target_include_directories(imgui PUBLIC
    "${PROJECT_SOURCE_DIR}/extra"
    "${PROJECT_SOURCE_DIR}/imga"
    "${PROJECT_SOURCE_DIR}/imgui"
    "${PROJECT_SOURCE_DIR}/implot"
    "${PROJECT_SOURCE_DIR}/implot3d"
)
