#pragma once

// DPI/scale detection shared by the main process and the worker process.
//
// On WSL2, the scale returned by GLFW is 1.0, even though Windows is using a
// scale of 200%. Watch out, because WSLg might be just blowing up the window
// to 2x! Resulting in a nastly pixellated look. Our framebuffer is still just
// 1x. Get rid of that totally fake scaling using
// $ cat /mnt/c/Users/<user>/.wslgconfig
// [system-distro-env]
// WESTON_RDP_DEBUG_DESKTOP_SCALING_FACTOR=100
struct DpiInfo {
    float dpi_scale = 1.f;
    float window_size_scale_factor = 1.f;
    bool is_wsl2 = false;
};

// Detects WSL2 via /proc/version and applies the corresponding fixed scale.
// Callers with a window system available (the main process) should refine
// dpi_scale with glfwGetMonitorContentScale() when !is_wsl2; the headless
// worker uses the value as-is (the main process forwards its own).
DpiInfo detectDpi();
