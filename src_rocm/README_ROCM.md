# ROCm/HIP Port of CUDA Renderer

This directory contains the ROCm/HIP port of the CUDA-based 3D raytracing renderer.

## Overview

The original CUDA renderer has been ported to use AMD's ROCm (Radeon Open Compute) platform via HIP (Heterogeneous-compute Interface for Portability). This allows the renderer to run on AMD GPUs instead of NVIDIA GPUs.

## Key Changes

### CUDA to HIP API Mapping

| CUDA API | HIP API |
|----------|---------|
| `cuda_runtime.h` | `hip_runtime.h` |
| `cuda_gl_interop.h` | `hip_gl_interop.h` |
| `cudaMalloc` | `hipMalloc` |
| `cudaMemcpy` | `hipMemcpy` |
| `cudaFree` | `hipFree` |
| `cudaGLRegisterBufferObject` | `hipGLRegisterBufferObject` |
| `__global__`, `__device__`, `__constant__` | Same in HIP |
| `cudaTextureObject_t` | `hipTextureObject_t` |
| `nvcc` | `hipcc` |

### Files

- `hiprenderer.hip.cpp` - Main HIP kernel implementation (converted from `cudarenderer.cu`)
- `hiprenderer.h` - Header file for HIP renderer functions
- `main_rocm.cpp` - Main program (converted from `main.cpp`)
- `Makefile_rocm` - Simple Makefile for building
- `Makefile.am_rocm` - Automake configuration (optional)

## Requirements

1. **ROCm/HIP Toolkit** - Install ROCm from AMD's website or your distribution's package manager
2. **OpenGL libraries** - GLUT, GLEW, SDL
3. **C++ compiler** - GCC or Clang (used by hipcc)

### Installing ROCm

**Ubuntu/Debian:**
```bash
# Add AMD repository
wget https://repo.radeon.com/amdgpu/latest/Ubuntu/dists/focal/amdgpu.list
sudo mv amdgpu.list /etc/apt/sources.list.d/
# Install ROCm
sudo apt update
sudo apt install rocm-dev hipblas
```

**Fedora/RHEL:**
```bash
sudo dnf install rocm-dev hipblas
```

**Manual installation:**
Download from https://www.rocm.com/

## Building

### Method 1: Simple Makefile (Recommended)

```bash
cd src_rocm
cp Makefile_rocm Makefile
make
```

### Method 2: Custom Build

```bash
cd src_rocm

# Set HIP path if not in default location
export HIP_PATH=/opt/rocm

# Compile
hipcc -O2 -g -std=c++11 -I$(HIP_PATH)/include -I. \
    -c -o main_rocm.o main_rocm.cpp
hipcc -O2 -g -std=c++11 -I$(HIP_PATH)/include -I. \
    -c -o hiprenderer.o hiprenderer.hip.cpp
hipcc -O2 -g -std=c++11 -I$(HIP_PATH)/include -I. \
    -c -o Algebra.o Algebra.cpp
# ... repeat for other .cpp files

# Link
hipcc -o hipRenderer *.o \
    -L$(HIP_PATH)/lib -lGL -lGLU -lglut -lSDL -lhipblas -lamdhip64
```

## Running

```bash
./hipRenderer path/to/model.ply
```

### Options

- `-b <frames>` - Benchmark mode (render specified number of frames)
- `<model.ply>` - Path to 3D model in PLY format

### Keyboard Controls (during runtime)

- **R** - Toggle auto-rotation
- **Arrow keys** - Fly camera
- **W/Q** - Rotate light
- **S/F** - Strafe left/right
- **E/D** - Strafe up/down
- **F4** - Toggle points mode
- **F5** - Toggle specular lighting
- **F6** - Toggle Phong normal interpolation
- **F7** - Toggle reflections
- **F8** - Toggle shadows
- **F9** - Toggle anti-aliasing
- **H** - Show help
- **ESC** - Quit

## Troubleshooting

### Common Issues

1. **"hipcc: command not found"**
   - Ensure ROCm is installed and `hipcc` is in your PATH
   - Check: `which hipcc`

2. **"Cannot open display"**
   - Ensure you have a display or use X11 forwarding
   - For headless: set `DISPLAY=:0` or use virtual framebuffer

3. **"HIP error: no device"**
   - Ensure you have an AMD GPU with ROCm support
   - Check supported GPUs: https://rocm.docs.amd.com/
   - Run `rocminfo` to verify GPU detection

4. **Linker errors for HIP libraries**
   - Ensure `HIP_PATH` is correct
   - Check that `-L$(HIP_PATH)/lib` includes the HIP libraries
   - Verify library files exist: `ls $(HIP_PATH)/lib/libamdhip64.so`

## Performance

The HIP version should achieve similar performance to the CUDA version on equivalent hardware. Performance may vary based on:

- GPU architecture (AMD vs NVIDIA)
- ROCm driver version
- Specific GPU model

## License

Same as original CUDA renderer:
GNU General Public License v2.0

## Acknowledgments

Original CUDA renderer by Thanassis Tsiodras (2004).
ROCm/HIP port maintains the same functionality while enabling AMD GPU support.
