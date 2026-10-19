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