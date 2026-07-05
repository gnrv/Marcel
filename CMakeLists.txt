# On Ubuntu, use https://apt.kitware.com to get a recent CMake version
cmake_minimum_required(VERSION 3.27)

set(CMAKE_CXX_STANDARD 20)

# Cacheable configuration options
option(USE_CUDA "Use CUDA" OFF)

# Cling is a hard requirement: slides are interpreted C++ at runtime.
# (A future tool will bake presentations into standalone playback binaries.)

if(UNIX AND NOT APPLE)
    set(LINUX TRUE)
endif()

# Set project name
project(marcel LANGUAGES CXX)


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

# Sources of program
include_directories(src src/editor src/slides external/ImGuiColorTextEdit)
file(GLOB source_list RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
    src/core/*.cpp
    src/editor/*.cpp
    src/engine/*.cpp
    src/render/*.cpp
    src/search/*.cpp
    src/slides/*.cpp
    src/supervisor/*.cpp
    src/system/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/external/ImGuiColorTextEdit/TextEditor.cpp
)

# IPC static lib: the marcel <-> marcel_worker protocol + channel. No GL,
# no Cling — linked by marcel, marcel_worker, and the tests.
add_library(${PROJECT_NAME}_ipc STATIC src/ipc/Channel.cpp)
target_include_directories(${PROJECT_NAME}_ipc PUBLIC src)

# Making a library without the main, for the tests and the program.
# ClingEngine links Cling/LLVM, so it belongs to the executable, not the lib —
# the tests link ${PROJECT_NAME}_lib and must not need LLVM.
list(REMOVE_ITEM source_list src/engine/ClingEngine.cpp)
add_library(${PROJECT_NAME}_lib
    ${source_list}
)

# Main program executable
set(LIBS clingInterpreter clingMetaProcessor clingUtils)

add_executable(${PROJECT_NAME}
src/main.cpp
src/engine/ClingEngine.cpp
${APP_ICON_RESOURCE_WINDOWS}
)
target_link_libraries(${PROJECT_NAME} PUBLIC ${LIBS})

# Include directories
target_include_directories(${PROJECT_NAME} PUBLIC ${LLVM_INCLUDE_DIRS})
target_include_directories(${PROJECT_NAME} PUBLIC ${CLANG_INCLUDE_DIRS})
target_include_directories(${PROJECT_NAME} PUBLIC ${CLING_INCLUDE_DIRS})
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

set(LIB_LINK rapidfuzz::rapidfuzz imgui GL glfw fmt::fmt nfd::nfd)
#target_link_libraries(${PROJECT_NAME}_lib PUBLIC someObjs)
target_link_libraries(${PROJECT_NAME}_lib PUBLIC ${LIB_LINK} ${PROJECT_NAME}_ipc)
target_link_libraries(${PROJECT_NAME} PRIVATE ${PROJECT_NAME}_lib)

# Worker process: owns Cling, supervised by marcel (--engine=remote).
# Compile-only for now; rendering moves here in step 3b. Gets the same
# Cling include dirs and JIT symbol-export flags as marcel — both move off
# marcel entirely in step 6.
add_executable(${PROJECT_NAME}_worker
    src/worker/worker_main.cpp
    src/engine/ClingEngine.cpp
)
target_include_directories(${PROJECT_NAME}_worker PRIVATE
    ${LLVM_INCLUDE_DIRS} ${CLANG_INCLUDE_DIRS} ${CLING_INCLUDE_DIRS})
target_link_libraries(${PROJECT_NAME}_worker PRIVATE ${PROJECT_NAME}_lib ${PROJECT_NAME}_ipc ${LIBS})
target_link_options(${PROJECT_NAME}_worker PRIVATE -Wl,--export-dynamic-symbol=*cling10printValue*, -Wl,--export-dynamic-symbol=*setValueNoAlloc*, -Wl,--export-dynamic-symbol=*setValueWithAlloc*, -Wl,--export-dynamic-symbol=*cling_runtime_internal_throwIfInvalidPointer*)
if(USE_CUDA)
    target_compile_definitions(${PROJECT_NAME}_worker PRIVATE USE_CUDA=1
        CUDA_PATH="${CUDAToolkit_ROOT}" CUDA_LIB_DIR="${CUDAToolkit_LIBRARY_DIR}")
endif()
add_dependencies(${PROJECT_NAME} ${PROJECT_NAME}_worker)

# Copy the data (such as fonts) in build directory
add_custom_command(TARGET ${PROJECT_NAME} PRE_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_SOURCE_DIR}/data/ $<TARGET_FILE_DIR:${PROJECT_NAME}>/data)

if(MSVC)
    # CMake 3.21 is for this functionnality / Copy the dlls
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_RUNTIME_DLLS:${PROJECT_NAME}> $<TARGET_FILE_DIR:${PROJECT_NAME}>
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
# ---- Unit tests (CppUnit + CTest) ----
# CppUnit comes from the Ubuntu package libcppunit-dev (found via pkg-config).
enable_testing()
find_package(PkgConfig REQUIRED)
pkg_check_modules(CPPUNIT REQUIRED IMPORTED_TARGET cppunit)

add_executable(${PROJECT_NAME}_tests
    test/main_test.cpp
    test/DpiInfoTest.cpp
    test/MarkerExtractTest.cpp
    test/SerializeTest.cpp
    test/IpcChannelTest.cpp
    test/SupervisorLogicTest.cpp
)
target_link_libraries(${PROJECT_NAME}_tests PRIVATE ${PROJECT_NAME}_lib ${PROJECT_NAME}_ipc PkgConfig::CPPUNIT)

# One CTest entry per suite; marcel_tests <SuiteName> runs a single named suite.
add_test(NAME DpiInfo       COMMAND ${PROJECT_NAME}_tests DpiInfoTest)
add_test(NAME MarkerExtract COMMAND ${PROJECT_NAME}_tests MarkerExtractTest)
add_test(NAME Serialize     COMMAND ${PROJECT_NAME}_tests SerializeTest)
add_test(NAME IpcChannel    COMMAND ${PROJECT_NAME}_tests IpcChannelTest)
add_test(NAME SupervisorLogic COMMAND ${PROJECT_NAME}_tests SupervisorLogicTest)
