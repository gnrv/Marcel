//#include <imga.h>

ImGui::Text("Insehkjhkrt ImPlot Here");

static float vec_y = 5;
static float mk_size = 6; // ImPlot::GetStyle().MarkerSize
static float line_weight = 2;
ImGui::SliderFloat("Vector Y", &vec_y, 0, 10);
ImGui::SliderFloat("Size", &mk_size, 0, 10);
ImGui::SliderFloat("Vector Weight", &line_weight, 0, 10);
if (ImPlot::BeginPlot("hejz")) {
        ImPlot::SetupAxesLimits(0, 10, 0, 12);

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
        ImPlot::Bivector("Bivector", ImVec2(6, 4), ImVec2(8, 4), ImVec2(9, 8), ImPlotItemFlags_NoLegend);
        ImPlot::PopStyleVar(2);

        ImPlot::PlotText("Vectors", 2.5f, 6.0f);

        ImPlot::PushStyleColor(ImPlotCol_InlayText, ImVec4(1,0,1,1));
        ImPlot::PlotText("Vertical Text", 5.0f, 6.0f, ImVec2(0,0), ImPlotTextFlags_Vertical);
        ImPlot::PopStyleColor();

    ImPlot::EndPlot();
}

