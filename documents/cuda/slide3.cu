__global__ void brunit(float *matrix, int size) {
   int x = blockIdx.x * blockDim.x + threadIdx.x;
   if (x < size)
   matrix[x] = 2*x;
}
int dim = 1024;
float *d_AA;
cudaMalloc(&d_AA, dim*sizeof(float));