json test;

auto update = []() {
	ImPlotPoint pelle{ 3, 5 };
	ImGui::Text("Hej! %f", pelle.x);
};

return update;
