int blocks = 1024/256; int threads = 256;
brunit<<<blocks, threads>>>(d_AA, dim);
float *d_AA_host = (float *)malloc(dim*sizeof(float));
cudaMemcpy(d_AA_host, d_AA, dim * sizeof(float), cudaMemcpyDeviceToHost);

auto update = [=]() {
    ImGui::Text("d_AA_host[42] = %f", d_AA_host[42]);
};
return update;
