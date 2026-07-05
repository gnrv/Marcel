#include "DpiInfo.h"

#include <fstream>
#include <string>

DpiInfo detectDpi()
{
    DpiInfo info;
    std::ifstream file("/proc/version");
    if (file.good()) {
        std::string line;
        std::getline(file, line);
        if (line.find("WSL") != std::string::npos) {
            // WSL2 detected
            info.is_wsl2 = true;
            info.dpi_scale = 2.f; // Or whatever your setting is in Windows, e.g. 200% is 2.f
            info.window_size_scale_factor = 1.f; // For the window size
        }
    }
    return info;
}
