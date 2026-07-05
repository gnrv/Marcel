#pragma once

#include <string>
#include <filesystem>

#ifdef _WIN32
#include <windows.h>
#endif

#define MAX_PATH 260

inline std::string getExecutablePath() {
#ifdef _WIN32
    char buffer[MAX_PATH];
    GetModuleFileNameA(NULL, buffer, MAX_PATH);
    std::string::size_type pos = std::string(buffer).find_last_of("\\/");
    return std::string(buffer).substr(0, pos);
#else
    std::string out = std::filesystem::canonical("/proc/self/exe");
    size_t pos = out.find_last_of("/");
    return out.substr(0, pos);
#endif
}