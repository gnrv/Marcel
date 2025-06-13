cudaMalloc(&d_AA, dim*sizeof(float));

auto update = [](){
   ImGui::Text("CUDA setup complete, d_A allocated: %p.", d_AA);
};
return update;
