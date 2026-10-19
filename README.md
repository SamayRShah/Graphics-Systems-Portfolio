# Graphics Systems Portfolio

**Target Specializations:** Graphics Co-Design, Low-Level Systems, Hardware-Software Architecture  

This repository aggregates primary engineering projects designed to demonstrate core competencies in low-level systems engineering, modern graphics APIs, and real-time rendering.

## Projects

### [01. SIMD BVH CPU Raytracer](./01_simd_bvh_raytracer/)
*   **Core Infrastructure:** C++, Intel AVX/SSE Intrinsics (mostly using DirectXMath with custom intirinsics functions), Win32 API, Custom Platform Layer, Dynamic Link Library (DLL) Hot-Reloading.
*   Formulated a zero-dependency platform layer interacting directly with native Windows APIs. Designed a data-oriented execution model leveraging runtime DLL hot-reloading for fast-iteration code execution. Structured custom, data-aligned memory layouts to limit cache thrashing and optimize execution across parallel hardware SIMD registers.

### [02. DirectX 12 Real-Time Raytracer & Renderer](./02_dxx12/)
*   **Core Infrastructure:** C++, DirectX 12, DXR (DirectX Raytracing) Pipeline, HLSL.
*   Programmed an explicit rendering architecture utilizing the traditional DXR ray-generation pipeline (handling explicit RayGen, Closest Hit, and Miss shaders). Managed low-level hardware allocations manually, tracking resource barriers, descriptor heaps, and Pipeline State Objects (PSOs).
*   Formulated ray-SDF surface intersection logic using analytic Signed Distance Functions. Calculated real-time surface normals inside pixel shaders via central differences.

### [03. DirectX 11 Real-Time Renderer](./03_dx11/)
*   **Core Infrastructure:** C++, DirectX 11, HLSL.
*   Implemented a classic real-time rasterization pipeline, handling raw vertex/index buffer allocations, constant buffer updates, primitive topology bindings, and shader compilation states.

### [04. Hardware-Software Serial IO Sync](./04_hardware_software_serial_io/)
*   **Core Infrastructure:** Arduino (C++), Unity (C#), C++ Serial Interface, PWM Hardware Control.
*   **Systems Architecture:** Created a low-latency serial communication pipeline handling asynchronous data streams between managed (.NET) and unmanaged (microcontroller) runtime environments.

## Repository Layout

<pre>
├── .gitignore
├── README.md                  <-- (You are here: Executive Summary)
├── 01_simd_bvh_raytracer/
│   ├── src/
│   └── <a href="./01_simd_bvh_raytracer/README.md">README.md</a>
├── 02_dx12/
│   ├── hybrid_pipeline/
│   ├── raytracer/
│   └── <a href="./02_dx12/README.md">README.md</a>
├── 03_dx11/
│   ├── dx11_renderer/
│   └── <a href="./03_dx11/README.md">README.md</a>
└─── 04_hardware_software_serial_io/
    ├── firmware/
    ├── unity_project/
    └── <a href="./04_hardware_software_serial_io/README.md">README.md</a>
</pre>

## AI Usage
**Tooling Disclosure:** Core source code, mathematical architectures, pipeline configurations, etc. were written by myself and/or were part of coursework starter code. AI was used for generating documentation based on code, minor scripting tasks for presentation, e.g. making the bat files automatically search for vcvarsall.bat, or setting up different configurations (compiler flags were added manually), find/replace/rename operations, and generating boiler plate (e.g. bitmap header struct, or finding Windows Fn Definitions like WinMain or HResult Callback).