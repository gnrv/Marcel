# On Ubuntu, use https://apt.kitware.com to get a recent CMake version
cmake_minimum_required(VERSION 3.27)

set(CMAKE_CXX_STANDARD 20)

# Cacheable configuration options
option(USE_CLING "Use cling" ON)
option(USE_CUDA "Use CUDA" OFF)

# If you don't use Cling, define the path to the presentation here, so we can #include the source files
# and build them at compile time instead of interpreting them using Cling at runtime
if(NOT USE_CLING)
include_directories("documents/test")
endif()

if(UNIX AND NOT APPLE)
    set(LINUX TRUE)
endif()

# Set project name
project(quicktex LANGUAGES CXX)


set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

set(INSTALL_DIR "0.1")

# ---- MicroTex ----
add_subdirectory(external/microtex)

# ---- ImGui ----
# ImGui config
add_subdirectory(external/imgui)

# ----- Rapidfuzz ------
add_subdirectory(external/rapidfuzz-cpp)
include_directories(external/rapidfuzz-cpp/include)

# -----
# Fonts
# -----
include_directories(external/IconFontCppHeaders)

# ---------------
# STB image write
# ---------------
include_directories(external/stb_image)

# ---- nativefiledialog-extended ----
add_subdirectory(external/nativefiledialog-extended)

# ----
# GLFW
# ----
find_package(glfw3 REQUIRED)

# ---
# fmt
# ---
find_package(fmt REQUIRED)
include_directories(${fmt_INCLUDE_DIRS})

# ----
# nlohmann/json
# ----
include_directories(external/nlohmann/json/include)

# ----
# stb_image
# For some reason, included as <stb_image/stb_image.h> instead of <stb_image.h>
# ----
include_directories(external)

# -----
# CLING
# -----
if(USE_CLING)
    set(LLVM_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/root-project/cling-build/lib/cmake/llvm")
    find_package(LLVM REQUIRED CONFIG NO_DEFAULT_PATH)
    message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
    message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

    set(Clang_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/root-project/cling-build/lib/cmake/clang")
    find_package(Clang REQUIRED CONFIG NO_DEFAULT_PATH)
    message(STATUS "Found Clang ${Clang_PACKAGE_VERSION}")
    message(STATUS "Using ClangConfig.cmake in: ${Clang_DIR}")

    set(Cling_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/root-project/cling-build/tools/cling/lib/cmake/cling")
    find_package(Cling REQUIRED CONFIG NO_DEFAULT_PATH)
    message(STATUS "Found Cling ${Cling_PACKAGE_VERSION}")
    message(STATUS "Using ClingConfig.cmake in: ${Cling_DIR}")

    # See if we can improve gdb start times
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -gsplit-dwarf")
    set(CMAKE_EXE_LINKER_FLAGS_DEBUG "${CMAKE_EXE_LINKER_FLAGS_DEBUG} -Wl,--gdb-index")
    set(CMAKE_SHARED_LINKER_FLAGS_DEBUG "${CMAKE_SHARED_LINKER_FLAGS_DEBUG} -Wl,--gdb-index")
endif()

# Sources of program
include_directories(src src/editor src/slides external/ImGuiColorTextEdit)
file(GLOB source_list RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
    src/core/*.cpp
    src/editor/*.cpp
    src/search/*.cpp
    src/slides/*.cpp
    src/system/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/external/ImGuiColorTextEdit/TextEditor.cpp
)

# Making a library without the main, for the tests and the program
add_library(${PROJECT_NAME}_lib
    ${source_list}
)

# Main program executable
if(USE_CLING)
    set(LIBS clingInterpreter clingMetaProcessor clingUtils)
else()
    set(LIBS "")
endif()
# (TODO: Only if we're not using Cling) Add "documents" to the include directories
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/documents)

add_executable(${PROJECT_NAME}
src/main.cpp
${APP_ICON_RESOURCE_WINDOWS}
)
target_link_libraries(${PROJECT_NAME} PUBLIC ${LIBS})

# Include directories
if(USE_CLING)
    target_include_directories(${PROJECT_NAME} PUBLIC ${LLVM_INCLUDE_DIRS})
    target_include_directories(${PROJECT_NAME} PUBLIC ${CLANG_INCLUDE_DIRS})
    target_include_directories(${PROJECT_NAME} PUBLIC ${CLING_INCLUDE_DIRS})
    target_compile_definitions(${PROJECT_NAME} PRIVATE USE_CLING=1)
    if(USE_CUDA)
        target_compile_definitions(${PROJECT_NAME} PRIVATE USE_CUDA=1)
        # If we use CUDA, we need to link against the CUDA libraries
        # Cling requires CUDA 11 exactly
        # On Linux, there should be a symlink /usr/local/cuda-11 if you have CUDA 11 installed
        # cmake -DCUDAToolkit_ROOT=/usr/local/cuda-11 ...
        # You may have to rm build/CMakeCache.txt if you have found CUDA-12 at some point
        # because a stale CUDAToolkit_BIN_DIR will cause CMake to find CUDA-12 again and again
        find_package(CUDAToolkit 11 EXACT REQUIRED)
        if(CUDAToolkit_FOUND)
            message(STATUS "Found CUDAToolkit: ${CUDAToolkit_VERSION}")
        else()
            message(FATAL_ERROR "CUDA Toolkit not found, but USE_CUDA is set to ON")
        endif()

        target_compile_definitions(${PROJECT_NAME} PRIVATE CUDA_PATH="${CUDAToolkit_ROOT}")
        target_compile_definitions(${PROJECT_NAME} PRIVATE CUDA_LIB_DIR="${CUDAToolkit_LIBRARY_DIR}")
    endif()

    # Cling JITted code needs to be able to resolve symbols from its own executable
    # OR we can turn off value printing in cling
    # UPDATE: turns out we need some of these symbols just to get a cling::Value out of
    #         the interpreter
    #set_target_properties(${PROJECT_NAME} PROPERTIES ENABLE_EXPORTS 1)
    # We can use a more targeted option that exists in mold (inspired by one in gold)
    # and use --export-dynamic-symbol=*setValueNoAlloc* to export only the
    # relevant symbols and avoid conflict with system LLVM used by WSL2 GL drivers
    target_link_options(${PROJECT_NAME} PRIVATE -Wl,--export-dynamic-symbol=*cling10printValue*, -Wl,--export-dynamic-symbol=*setValueNoAlloc*, -Wl,--export-dynamic-symbol=*setValueWithAlloc*, -Wl,--export-dynamic-symbol=*cling_runtime_internal_throwIfInvalidPointer*)
endif()

set(LIB_LINK rapidfuzz::rapidfuzz imgui GL glfw fmt::fmt nfd::nfd)
#target_link_libraries(${PROJECT_NAME}_lib PUBLIC someObjs)
target_link_libraries(${PROJECT_NAME}_lib PUBLIC ${LIB_LINK})
target_link_libraries(${PROJECT_NAME} PRIVATE ${PROJECT_NAME}_lib)

# Copy the data (such as fonts) in build directory
add_custom_command(TARGET quicktex PRE_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_SOURCE_DIR}/data/ $<TARGET_FILE_DIR:quicktex>/data)

if(MSVC)
    # CMake 3.21 is for this functionnality / Copy the dlls
    add_custom_command(TARGET quicktex POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_RUNTIME_DLLS:quicktex> $<TARGET_FILE_DIR:quicktex>
        COMMAND_EXPAND_LISTS
    )
endif()

# Set compiler options
if(MSVC)
    # /ENTRY:mainCRTStartup keeps the same "main" function instead of requiring "WinMain"
    target_compile_options(${PROJECT_NAME} PRIVATE /W0)
else()
    target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -pedantic)
endif()

# Remove console on Windows
if(WIN32)
    set_target_properties(${PROJECT_NAME}
        PROPERTIES
        LINK_FLAGS_DEBUG "/SUBSYSTEM:CONSOLE"
        LINK_FLAGS_RELEASE "/SUBSYSTEM:windows /ENTRY:mainCRTStartup"
        LINK_FLAGS_RELWITHDEBINFO "/SUBSYSTEM:windows /ENTRY:mainCRTStartup"
        LINK_FLAGS_MINSIZEREL "/SUBSYSTEM:windows /ENTRY:mainCRTStartup"
    )
endif()

target_link_options(${PROJECT_NAME} PRIVATE ${SUBSYSTEM_LINKER_OPTIONS})

# if(LINUX)
#     install(TARGETS ${PROJECT_NAME}
#         RUNTIME DESTINATION $ENV{HOME}
#         LIBRARY DESTINATION $ENV{HOME}
#         DESTINATION $ENV{HOME}
#     )

#     SET(CPACK_GENERATOR "DEB")
#     SET(CPACK_DEBIAN_PACKAGE_MAINTAINER "KK")
# endif()