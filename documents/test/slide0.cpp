static float vec_y = 5;
static float mk_size = 6; // ImPlot::GetStyle().MarkerSize
static float line_weight = 2;

auto update = []() {
ImGui::Text("Insert ImPlot Here");
ImGui::SliderFloat("Vector Y", &vec_y, 0, 10);
ImGui::SliderFloat("Size", &mk_size, 0, 10);
ImGui::SliderFloat("Vector Weight", &line_weight, 0, 10);
auto slide_size = ImGui::GetContentRegionAvail();
float dim = std::min(slide_size.x, slide_size.y);
ImVec2 plot_size = ImVec2(dim, dim) * 0.8f;
ImGui::SetCursorPosX((slide_size.x - plot_size.x)/2);
ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[2]);
if (ImPlot::BeginPlot("Hej", plot_size)) {
        ImPlot::SetupAxesLimits(0, 12, 0, 12);

        ImS8 xs[4] = {1,4,5,6};
        ImS8 ys[4] = {10,11,7,8};

        // filled markers
        ImPlot::PushStyleVar(ImPlotStyleVar_LineWeight, line_weight);
        ImPlot::PushStyleVar(ImPlotStyleVar_MarkerSize, mk_size);
        for (int m = 0; m < ImPlotMarker_COUNT; ++m) {
            ImGui::PushID(m);
            if (m % 2) {
                ImPlot::Vector("Hej", 
                    ImVec2(xs[0]+2, ys[0]+vec_y),
                    ImVec2(xs[0], ys[0]),
                    ImPlotItemFlags_NoLegend | ImPlotItemFlags_NoLabel);
            } else {
                ImPlot::Vector("Hej", 
                    ImVec2(xs[0], ys[0]),
                    ImVec2(xs[0]+2, ys[0]+vec_y),
                    ImPlotItemFlags_NoLegend | ImPlotItemFlags_NoLabel);
            }
            ImGui::PopID();
            ys[0]--; ys[1]--;
            ys[2]--; ys[3]--;
        }
        xs[0] = 6; xs[1] = 9; ys[0] = 10; ys[1] = 11;
        // open markers
        ImPlot::Bivector(R"(a \wedge b)", ImVec2(6, 7), ImVec2(8, 7), ImVec2(9, 11), ImPlotItemFlags_NoLegend);
        ImVec2 d(0.5, -0.5);
        ImPlot::Vector("a", ImVec2(6, 7)+d, ImVec2(8, 7)+d, ImPlotItemFlags_NoLegend);
        ImPlot::Vector("b", ImVec2(8, 7)+d, ImVec2(9, 11)+d, ImPlotItemFlags_NoLegend);
        ImPlot::Bivector("A", ImVec2(7, 4), vec_y, ImPlotItemFlags_NoLegend);
        ImPlot::PopStyleVar(2);

        ImPlot::PlotText("Vectors", 2.5f, 6.0f);

        ImPlot::PushStyleColor(ImPlotCol_InlayText, ImVec4(1,0,1,1));
        ImPlot::PlotText("Vertical Text", 5.0f, 6.0f, ImVec2(0,0), ImPlotTextFlags_Vertical);
        ImPlot::PopStyleColor();

    ImPlot::EndPlot();
}
ImGui::PopFont();
};
return update;
