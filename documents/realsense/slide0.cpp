// RealSense pointcloud in a vanilla ImPlot3D plot — each point colored from
// the color stream. ImPlot3D is orthographic, so the cloud reads like a
// blueprint rather than a photo; that is the whole point of the demo.
//
// The pipeline lives in setup.cpp; here we only poll it (non-blocking — a
// blocking wait would trip the worker's frame watchdog) and draw. State is at
// file scope: on recompile it resets, but the camera in setup keeps running.
//
// No #include here on purpose: the whole slide body is wrapped in a function
// by the engine, so an #include would land at function scope. ImGui/ImPlot3D
// are preloaded by the worker, and rs2::* comes from setup.cpp's global
// include — both are already visible.

// Last-good cloud (kept so a frame with no new data still draws something).
static std::vector<float> px, py, pz;
static std::vector<ImU32> pcol;
static rs2::pointcloud pc;
static rs2::decimation_filter dec;
static bool frozen = false;
static const size_t kMaxPoints = 60000;

auto update = []() {
    try {
        if (rs_have_source && !frozen) {
            rs2::frameset fs;
            if (rs_pipe.poll_for_frames(&fs)) {
                rs2::video_frame color = fs.get_color_frame();
                rs2::depth_frame depth = fs.get_depth_frame();
                if (color && depth) {
                    dec.set_option(RS2_OPTION_FILTER_MAGNITUDE, 3.0f);
                    rs2::depth_frame ddec = dec.process(depth);
                    pc.map_to(color);
                    rs2::points pts = pc.calculate(ddec);

                    const rs2::vertex *verts = pts.get_vertices();
                    const rs2::texture_coordinate *uvs =
                        pts.get_texture_coordinates();
                    const uint8_t *cdata =
                        static_cast<const uint8_t *>(color.get_data());
                    int cw = color.get_width(), ch = color.get_height();
                    int cstride = color.get_stride_in_bytes();
                    size_t n = pts.size();
                    // Subsample if the decimated cloud still exceeds the cap.
                    size_t step = n > kMaxPoints ? (n / kMaxPoints) + 1 : 1;

                    px.clear(); py.clear(); pz.clear(); pcol.clear();
                    for (size_t i = 0; i < n; i += step) {
                        float z = verts[i].z;
                        if (z <= 0.0f)
                            continue;
                        int u = (int)(uvs[i].u * cw);
                        int v = (int)(uvs[i].v * ch);
                        if (u < 0) u = 0; else if (u >= cw) u = cw - 1;
                        if (v < 0) v = 0; else if (v >= ch) v = ch - 1;
                        const uint8_t *c = cdata + (size_t)v * cstride + (size_t)u * 3;
                        px.push_back(verts[i].x);
                        py.push_back(-verts[i].y); // flip so up is up
                        pz.push_back(z);
                        pcol.push_back(IM_COL32(c[0], c[1], c[2], 255));
                    }
                }
            }
        }

        ImGui::Text("%s", rs_status.c_str());
        ImGui::SameLine();
        ImGui::Text("| %zu points", px.size());
        ImGui::SameLine();
        ImGui::Checkbox("freeze", &frozen);

        if (px.empty()) {
            ImGui::Text("(no points yet)");
            return;
        }

        // Data bounds for equal-meters box scaling.
        float xlo = px[0], xhi = px[0], ylo = py[0], yhi = py[0], zlo = pz[0], zhi = pz[0];
        for (size_t i = 1; i < px.size(); ++i) {
            xlo = std::min(xlo, px[i]); xhi = std::max(xhi, px[i]);
            ylo = std::min(ylo, py[i]); yhi = std::max(yhi, py[i]);
            zlo = std::min(zlo, pz[i]); zhi = std::max(zhi, pz[i]);
        }
        float sx = std::max(xhi - xlo, 1e-3f);
        float sy = std::max(yhi - ylo, 1e-3f);
        float sz = std::max(zhi - zlo, 1e-3f);
        float m = std::max(sx, std::max(sy, sz));

        if (ImPlot3D::BeginPlot("##cloud", ImGui::GetContentRegionAvail())) {
            ImPlot3D::SetupAxes("X (m)", "Y (m)", "Z (m)");
            ImPlot3D::SetupAxesLimits(xlo, xhi, ylo, yhi, zlo, zhi);
            ImPlot3D::SetupBoxScale(sx / m, sy / m, sz / m);

            // ImPlot3D::PlotScatter takes one color per call; for per-point
            // color we project each point ourselves (PlotToPixels locks Setup,
            // so this must come after the Setup calls) and splat a 2px square
            // into the plot's clipped draw list.
            ImDrawList *dl = ImPlot3D::GetPlotDrawList();
            ImVec2 p0 = ImPlot3D::GetPlotPos();
            ImVec2 ps = ImPlot3D::GetPlotSize();
            dl->PushClipRect(p0, ImVec2(p0.x + ps.x, p0.y + ps.y), true);
            for (size_t i = 0; i < px.size(); ++i) {
                ImVec2 s = ImPlot3D::PlotToPixels(px[i], py[i], pz[i]);
                dl->AddRectFilled(ImVec2(s.x - 1, s.y - 1),
                                  ImVec2(s.x + 1, s.y + 1), pcol[i]);
            }
            dl->PopClipRect();
            ImPlot3D::EndPlot();
        }
    } catch (const std::exception &e) {
        ImGui::Text("slide error: %s", e.what());
    }
};
return update;
