int blocks = 1024/256; int threads = 256;
igubanit<<<blocks, threads>>>(d_A, dim);
float *d_AA_host = (float *)malloc(dim*sizeof(float));
cudaMemcpy(d_AA_host, d_A, dim * sizeof(float), cudaMemcpyDeviceToHost);
d_AA_host[42];

auto update = [&]() {
	ImGui::Text("Hej! %f", d_AA_host[42]);
	ImGui::Latex(R"(\alpha \wedge \beta)");
	if (ImPlot::BeginPlot("Hej")) {
		ImPlot::EndPlot();
	}
};

return update;