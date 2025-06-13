cudaMalloc(&d_A, dim*sizeof(float));

auto update = [&](){
   ImGui::Text("CUDA setup complete, d_A allocated: %p.", d_A);
};
return update;