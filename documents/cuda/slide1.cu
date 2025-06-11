__global__ void igubanit(float *matrix, int size) {
   int x = blockIdx.x * blockDim.x + threadIdx.x;
   if (x < size)
   matrix[x] = 2*x;
}
int dim = 1024;
float *d_A;
cudaMalloc(&d_A, dim*sizeof(float));
