# CUDA to HIP Conversion Guide

This document details the conversion from CUDA to HIP (Heterogeneous-compute Interface for Portability) for the 3D raytracing renderer.

## Summary of Changes

### 1. Header Files

**CUDA:**
```cpp
#include <cuda.h>
#include <cuda_runtime.h>
#include <cuda_gl_interop.h>
#include <vector_types.h>
```

**HIP:**
```cpp
#include <hip/hip_runtime.h>
#include <hip/hip_gl_interop.h>
#include <hip/vector_types_compat.h>
```

### 2. Memory Management

| CUDA | HIP |
|------|-----|
| `cudaMalloc(&ptr, size)` | `hipMalloc(&ptr, size)` |
| `cudaMemcpy(dst, src, size, kind)` | `hipMemcpy(dst, src, size, kind)` |
| `cudaFree(ptr)` | `hipFree(ptr)` |
| `cudaMemset(ptr, val, size)` | `hipMemset(ptr, val, size)` |
| `cudaMemcpyToSymbol(sym, data, size)` | `hipMemcpyToSymbol(sym, data, size)` |

**Memory Copy Types:**
- `cudaMemcpyHostToDevice` → `hipMemcpyHostToDevice`
- `cudaMemcpyDeviceToHost` → `hipMemcpyDeviceToHost`
- `cudaMemcpyDeviceToDevice` → `hipMemcpyDeviceToDevice`

### 3. Error Handling

**CUDA:**
```cpp
cudaError_t err = cudaGetLastError();
if (err != cudaSuccess) {
    printf("CUDA error: %s\n", cudaGetErrorString(err));
}
```

**HIP:**
```cpp
hipError_t err = hipGetLastError();
if (err != hipSuccess) {
    printf("HIP error: %s\n", hipGetErrorString(err));
}
```

### 4. OpenGL Interop

| CUDA | HIP |
|------|-----|
| `cudaGLRegisterBufferObject(buffer)` | `hipGLRegisterBufferObject(buffer)` |
| `cudaGLMapBufferObject(&ptr, buffer)` | `hipGLMapBufferObject(&ptr, buffer)` |
| `cudaGLUnmapBufferObject(buffer)` | `hipGLUnmapBufferObject(buffer)` |
| `cudaGLUnregisterBufferObject(buffer)` | `hipGLUnregisterBufferObject(buffer)` |

### 5. Texture Objects

**CUDA Texture API:**
```cpp
cudaChannelFormatDesc channelDesc = cudaCreateChannelDesc<float4>();
cudaTextureDesc texDesc = {};
texDesc.addressMode[0] = cudaAddressModeClamp;
texDesc.filterMode = cudaFilterModePoint;
texDesc.readMode = cudaReadModeElementType;

cudaResourceDesc resDesc = {};
resDesc.resType = cudaResourceTypeLinear;
resDesc.res.linear.desc = channelDesc;
resDesc.res.linear.devPtr = devicePtr;
resDesc.res.linear.sizeInBytes = size;

cudaTextureObject_t texObj;
cudaCreateTextureObject(&texObj, &resDesc, &texDesc, NULL);
```

**HIP Texture API:**
```cpp
hipChannelFormatDesc channelDesc = hipCreateChannelDesc<float4>();
hipTextureDesc texDesc = {};
texDesc.addressMode[0] = hipAddressModeClamp;
texDesc.filterMode = hipFilterModePoint;
texDesc.readMode = hipReadModeElementType;

hipResourceDesc resDesc = {};
resDesc.resType = hipResourceTypeLinear;
resDesc.res.linear.desc = channelDesc;
resDesc.res.linear.devPtr = devicePtr;
resDesc.res.linear.sizeInBytes = size;

hipTextureObject_t texObj;
hipCreateTextureObject(&texObj, &resDesc, &texDesc, NULL);
```

### 6. Kernel Launch

**CUDA:**
```cpp
myKernel<<<blocks, threads>>>(args...);
```

**HIP:**
```cpp
myKernel<<<blocks, threads>>>(args...);
```

*Note: Kernel launch syntax is identical in HIP.*

### 7. Device Synchronization

| CUDA | HIP |
|------|-----|
| `cudaDeviceSynchronize()` | `hipDeviceSynchronize()` |
| `cudaThreadExit()` (deprecated) | `hipDeviceSynchronize()` |

### 8. Constant Memory

**CUDA:**
```cpp
__constant__ float data[1024];
cudaMemcpyToSymbol(data, hostPtr, size);
```

**HIP:**
```cpp
__constant__ float data[1024];
hipMemcpyToSymbol(data, hostPtr, size);
```

*Note: `__constant__` qualifier is supported identically in HIP.*

### 9. Function Qualifiers

All of these are supported identically in HIP:
- `__global__` - Device function launched from host
- `__device__` - Device function callable from device
- `__host__` - Host function
- `__constant__` - Constant memory variable

### 10. Vector Types

CUDA vector types map directly to HIP:
- `float2`, `float3`, `float4` → Same in HIP
- `uint1`, `uint2`, `uint3`, `uint4` → Same in HIP
- `int1`, `int2`, `int3`, `int4` → Same in HIP

## Function Renaming

For better code clarity, some functions were renamed from `*CUDA` to `*HIP`:

| Original | New |
|----------|-----|
| `dotCUDA()` | `dotHIP()` |
| `crossCUDA()` | `crossHIP()` |
| `distanceCUDA()` | `distanceHIP()` |
| `distancesqCUDA()` | `distancesqHIP()` |
| `CudaRender()` | `HipRender()` |
| `setConstants()` | `setHipConstants()` |

## Build System Changes

### CUDA Build (Original)
```makefile
NVCC = nvcc
NVCCFLAGS = -arch=sm_60 -O3
LDFLAGS = -lGL -lGLU -lglut -lSDL -lcudart
```

### HIP Build (New)
```makefile
HIPCC = hipcc
HIPFLAGS = -O3
LDFLAGS = -lGL -lGLU -lglut -lSDL -lhipblas -lamdhip64
```

## Testing the Port

### 1. Verify HIP Installation
```bash
hipcc --version
rocminfo
```

### 2. Build the HIP Version
```bash
cd src_rocm
make -f Makefile_rocm
```

### 3. Run with Test Model
```bash
./hipRenderer test_model.ply
```

### 4. Compare Output
Render the same scene with both CUDA and HIP versions and compare:
- Visual output should be identical
- Performance should be comparable (within 10-20%)

## Known Differences

1. **Performance Characteristics**: AMD and NVIDIA GPUs have different architectures, so performance may vary
2. **Driver Maturity**: CUDA drivers are generally more mature than ROCm drivers
3. **Feature Support**: Some cutting-edge CUDA features may not yet be available in HIP
4. **Debugging Tools**: NVIDIA Nsight vs AMD ROCm Profiler have different capabilities

## Migration Checklist

- [x] Replace CUDA headers with HIP headers
- [x] Replace CUDA runtime API calls with HIP equivalents
- [x] Replace CUDA GL interop calls with HIP equivalents
- [x] Update texture object API calls
- [x] Update error handling macros
- [x] Update build system (Makefile)
- [x] Test on AMD GPU
- [x] Verify visual output matches CUDA version
- [x] Document changes

## References

- [HIP Documentation](https://rocm.docs.amd.com/projects/HIP/en/)
- [CUDA to HIP Porting Guide](https://rocm.docs.amd.com/projects/HIP/en/latest/docs/hip_cuda_api_map.html)
- [ROCm Installation Guide](https://rocm.docs.amd.com/en/latest/)
