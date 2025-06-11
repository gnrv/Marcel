int blocks = 1024/256; int threads = 256;
igubanit<<<blocks, threads>>>(d_A, dim);
float *d_A_host = (float *)malloc(dim*sizeof(float));
cudaMemcpy(d_A_host, d_A, dim * sizeof(float), cudaMemcpyDeviceToHost);
d_A_host[42]