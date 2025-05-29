auto update = [](){
ImGui::Latex(R"(\begin{gather}
\gamma_\mu\gamma_\nu+\gamma_\nu\gamma_\mu=2\eta_{\mu\nu}\\
\mathbf{\sigma}_i = \gamma_i \gamma_0\\
\nabla\psi I\mathbf{\sigma}_3=m\psi\gamma_0\\
i\hat{\gamma}_\mu \frac{\partial}{\partial x^{\mu}} |\psi\rangle = m|\psi\rangle
\end{gather})");
//ImGui::SameLine();
//ImGui::PushFont(fira_sans_small);
static float xs1[1001], ys1[1001], zs1[1001];
for (int i = 0; i < 1001; i++) {
    xs1[i] = i * 0.001f;
    ys1[i] = 0.5f + 0.5f * cosf(50 * (xs1[i] + (float)ImGui::GetTime() / 10));
    zs1[i] = 0.5f + 0.5f * sinf(50 * (xs1[i] + (float)ImGui::GetTime() / 10));
}
static double xs2[20], ys2[20], zs2[20];
for (int i = 0; i < 20; i++) {
    xs2[i] = i * 1 / 19.0f;
    ys2[i] = xs2[i] * xs2[i];
    zs2[i] = xs2[i] * ys2[i];
}
auto slide_size = ImGui::GetContentRegionAvail();
float dim = std::min(slide_size.x, slide_size.y);
ImVec2 plot_size = ImVec2(dim, dim) * 0.8f;
ImGui::SetCursorPos(ImVec2((slide_size.x - plot_size.x) / 2, (slide_size.y - plot_size.y) / 2));
ImPlot3D::PushStyleVar(ImPlot3DStyleVar_LineWeight, 2);
ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[2]);
if (ImPlot3D::BeginPlot("##Line Plots", plot_size, ImPlot3DFlags_NoLegend)) {
    ImPlot3D::SetupAxes("x", "y", "z", ImPlot3DAxisFlags_NoLabel, ImPlot3DAxisFlags_NoLabel, ImPlot3DAxisFlags_NoLabel);
    ImPlot3D::PlotLine("f(x)", xs1, ys1, zs1, 1001);
    ImPlot3D::SetNextMarkerStyle(ImPlot3DMarker_Circle);
    ImPlot3D::PlotLine("g(x)", xs2, ys2, zs2, 20, ImPlot3DLineFlags_Segments);
    ImPlot3D::EndPlot();
}
ImGui::PopFont();
ImPlot3D::PopStyleVar();
//ImGui::PopFont();
ImGui::Text("Wazz flippin up?");
};
return update;

