// RealSense pointcloud demo — shared setup.
//
// Links librealsense2 into the Cling JIT with a single pragma (the system lib
// is at /lib/x86_64-linux-gnu/librealsense2.so; /usr/include is on Cling's
// default search path, so the header needs no add_include_path).
//
// IMPORTANT: this file must contain only *declarations*. Cling wraps any
// top-level executable statement into a function, which would pull the
// #include below into function scope and make rs.hpp fail to parse
// (`extern "C"` / namespaces are only legal at global scope). So the camera
// is brought up by a global-variable initializer (rs_setup_done), which runs
// rs_setup() once at global scope with no wrapping. Because rs.hpp is included
// here at global scope, its types (rs2::pipeline, rs2::pointcloud, …) are then
// visible to every slide without the slide re-including anything.
#pragma cling load("realsense2")
#include <librealsense2/rs.hpp>
#include <cstdlib>
#include <string>

// Visible to all slides.
std::string rs_status = "no source";
bool rs_have_source = false;
rs2::pipeline rs_pipe;
rs2::config rs_cfg;

// Source cascade: live camera, else a recorded .bag from $MARCEL_RS_BAG, else
// nothing (the slide shows the status text). Fully guarded — an exception
// escaping setup would poison the interpreter for every slide.
static bool rs_setup()
{
    try {
        rs2::context ctx;
        auto devices = ctx.query_devices();
        if (devices.size() > 0) {
            rs_status = std::string("camera: ") +
                        devices[0].get_info(RS2_CAMERA_INFO_NAME) + " #" +
                        devices[0].get_info(RS2_CAMERA_INFO_SERIAL_NUMBER);
            rs_cfg.enable_stream(RS2_STREAM_DEPTH);
            rs_cfg.enable_stream(RS2_STREAM_COLOR, RS2_FORMAT_RGB8);
            rs_pipe.start(rs_cfg);
            rs_have_source = true;
        } else if (const char *bag = getenv("MARCEL_RS_BAG")) {
            // Recorded in realsense-viewer; must contain depth AND color.
            // Both the classic ROS1 .bag and the rosbag2 .db3 that
            // realsense-viewer 2.58 writes load fine. Use an absolute path —
            // the worker's cwd is its executable directory.
            rs_cfg.enable_device_from_file(bag);
            rs_pipe.start(rs_cfg);
            rs_have_source = true;
            rs_status = std::string("bag: ") + bag;
        } else {
            rs_status = "no camera and no MARCEL_RS_BAG set";
        }
    } catch (const rs2::error &e) {
        rs_status = std::string("rs2 error: ") + e.what();
    } catch (const std::exception &e) {
        rs_status = std::string("error: ") + e.what();
    }
    return true;
}

// Global initializer: runs rs_setup() once, at global scope, no statement
// wrapping.
static bool rs_setup_done = rs_setup();
