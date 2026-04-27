# Quick Build Guide for ROCm Renderer

## Prerequisites

1. **AMD GPU** with ROCm support (see [ROCm Supported GPUs](https://rocm.docs.amd.com/))
2. **ROCm/HIP Toolkit** installed
3. **Development libraries**: OpenGL, GLUT, SDL

## Quick Build (3 Steps)

### Step 1: Verify HIP Installation

```bash
# Check HIP compiler
hipcc --version

# Check GPU detection
rocminfo | grep -i "name\|device"
```

### Step 2: Build

```bash
cd src_rocm

# Copy the Makefile
cp Makefile_rocm Makefile

# Build
make
```

### Step 3: Run

```bash
# Run with a PLY model
./hipRenderer /path/to/model.ply

# Benchmark (render 100 frames)
./hipRenderer -b 100 /path/to/model.ply
```

## Troubleshooting

### "hipcc: command not found"

```bash
# Find hipcc
which hipcc

# If not found, check ROCm installation
ls /opt/rocm/

# Add to PATH if needed
export PATH=/opt/rocm/bin:$PATH
```

### "Cannot find ROCm device"

```bash
# Check GPU support
rocminfo

# Check if GPU is visible
lspci | grep -i vga

# May need to add user to rocm group
sudo usermod -a -G render $USER
```

### Library linking errors

```bash
# Check HIP libraries
ls /opt/rocm/lib/libamdhip64.so

# Check OpenGL libraries
ldconfig -p | grep -E "GL|GLU|GLUT|SDL"

# If missing, install:
# Ubuntu/Debian:
sudo apt install libgl1-mesa-dev freeglut3-dev libsdl-dev

# Fedora:
sudo dnf install mesa-libGL-devel glut-devel SDL2-devel
```

## Custom HIP Path

If ROCm is not in `/opt/rocm`:

```bash
# Set HIP_PATH before building
export HIP_PATH=/path/to/rocm

# Or edit Makefile and change:
# HIP_PATH ?= /opt/rocm
# to:
# HIP_PATH ?= /your/path/to/rocm
```

## Build Output

Expected output:
```
hipcc -O2 -g -std=c++11 -I/opt/rocm/include -I. -c -o main_rocm.o main_rocm.cpp
hipcc -O2 -g -std=c++11 -I/opt/rocm/include -I. -c -o hiprenderer.o hiprenderer.hip.cpp
...
hipcc -L/opt/rocm/lib -lGL -lGLU -lglut -lSDL -lhipblas -lamdhip64 -o hipRenderer *.o
```

## Verification

After building, verify the executable:

```bash
# Check it exists and is executable
ls -lh hipRenderer

# Check linked libraries
ldd hipRenderer | grep -E "hip|GL|SDL"

# Run help
./hipRenderer
```

## Performance Tips

1. **Use release build** (add `-O3` to CXXFLAGS in Makefile)
2. **Ensure GPU is not throttled** (check power settings)
3. **Use latest ROCm drivers**
4. **Close other GPU applications** during benchmark

## Next Steps

- See `README_ROCM.md` for detailed documentation
- See `CUDA_TO_HIP_CONVERSION.md` for technical details
- See `cudarenderer.h` for renderer configuration options
