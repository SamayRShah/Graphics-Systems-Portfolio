# SIMD BVH4 Ray Tracer & Win32 Platform Layer

A CPU based, minimal-standard-library ray tracer based on [Ray Tracing in a Weekend Book One (RTIOW)](https://raytracing.github.io/books/RayTracingInOneWeekend.html) featuring a vectorized 4-way Bounding Volume Hierarchy (BVH4) built from first principles. 

The ray tracer focuses on cache-aware data layouts, manual memory arenas, and SIMD mathematics to achieve high performance with minimal footprint. The platform layer is an in-progress learning exercise to build a native implementation from scratch, it currently features debug runtime DLL hotloading, basic window setup with d3d11, FileIO, Input handling, and a basic Headless mode.
Planned upgrades include multi-threading, replacing used std libraries, and raw AVX & SSE intrinsics instead of dxmath

## Demo
https://github.com/user-attachments/assets/5f76a434-5a09-42d1-aa98-c00bb60b8e39

## Build Instructions

Usese a bat script to with MSVC (`cl`) compiler toolchain. Does not require visual studio project files, cmake, or external build dependencies.

### Prerequisites
* Windows OS
* Microsoft Visual Studio Developer Command Prompt (with C++ build tools installed)

### Compilation Steps
1. Open the **Developer Command Prompt for VS**.
2. Navigate to the project's scripts directory: `cd directory_path\scripts`
3. **(If not using Visual Studio Developer Console, run `init.h` - may have to manually enter varsall directory)**
4. Run the build script using one of the supported flags:
   * **Interactive Developer Mode (Default):** Compiles the platform application and engine DLL with full hot-reloading tracking enabled.
   * **Interactive Release Mode (`-r`):** Compiles optimized without debug symbols
   * **Headless Debug Mode (`-h`):** Compiles console window-less version (DLL hot reloading after every full run).
   * **Headless Release Mode (`-hr`):** Compiles an optimized console production application pipeline.

## Performance Profile

Rough overall comparison (i7-9750H CPU, 16GB RAM). 

Compared headless mode to slightly modified baseline version of RTIOW - switched doubles to floats, writing pixels into pre-allocated graphics buffer, removed logging, and final output into bmp file, (didn't modify runtime memory allocations, type inference, exeptions or other structs). 

* **Storage Allocated:** 51810304(~6.4mb) permanent, 51494912(~6.4mb) Transient | Alllocations not measured for RTIOW
* **Scene Configuration:** 486 spheres, 1920x1080 resolution, 25 samples per pixel, 25 max ray depth.
* **Compiler Environment:** MSVC (`/O2 /Oi /fp:fast /arch:AVX`)
* **SIMD BVH Results:** Completed in 39.248974 seconds
* **RTIOW Results:** Completed in 373.01800 seconds
* **~9.5x Total Speed up**

## Systems Architecture

### State-Isolating Runtime Code Hot-Swapping

The codebase cleanly separates the platform plumbing (`win32_app.cpp`) from the core engine kernel (`ray_tracer.cpp`). The engine compiles into a standalone dynamic library (DLL). The platform layer monitors this binary at runtime and hot-swaps execution pointers mid-frame loop upon modification. This permits rapid testing of mathematical and geometric heuristics without losing application state or reloading scene data.

### Contiguous Memory Arenas

The project completely bypasses the standard library allocator (`malloc`/`free`) and standard containers. At initialization, a single virtual memory block is requested directly from the OS (`VirtualAlloc`) and partitioned into two linear arenas:

* **Permanent Storage:** Tracks global object indices, sphere geometry, and collapsed BVH nodes, and graphics buffer.
* **Transient Storage:** Acts as a scratchpad for initial hierarchy construction and buffer for file IO.

## Computational Graphics

### Structure of Arrays (SoA) Layout

To maximize SIMD efficiency, traditional object-oriented representations (e.g., arrays of sphere structures) were replaced by a Structure of Arrays (SoA) design. Center coordinates (X, Y, Z) and radii are isolated into parallel, contiguous memory buffers. This matches processor vector registration patterns, allowing a single execution lane to load and evaluate four distinct primitives concurrently.

### 4-Way BVH Generation & Traversal

The acceleration structure is constructed by sorting spatial objects along their widest extent axis via an iterative QuickSort. The resulting binary tree is compressed two levels deep into a quaternary structure (`bvh4_node`). 

* Each node tracks four child bounding boxes.
* Ray propagation evaluates these four bounding boxes simultaneously using horizontal SIMD vector minimum and maximum operations.
* The intersection loop utilizes value masking rather than conditional statements, generating branchless code.

## Project Structure

```text
├── src/
│   ├── platform/
│   │   ├── platform_interface.h  # Memory layout boundaries and system input mapping
│   │   └── win32_app.cpp         # Win32 window management, D3D11 blitting, virtual memory
│   └── simd_bvh_ray_tracer/
│       ├── ray_tracer.cpp        # Tree compaction, sorting, and vectorized traversal
│       ├── ray_tracer.h          # Data-oriented SoA architecture definitions
│       ├── rt_image_utils.h      # Top-down bitmap file generation tracking
│       └── utils.h               # Custom memory arenas, random, and SIMD register packaging
└── scripts/
    ├── build.bat                 # Direct compiler optimization toolchain configurations
    └── init.bat                  # Helper to run vcvarsall.bat - may need to manually set file location
```
