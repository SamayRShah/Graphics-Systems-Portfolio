# Graphics Systems Portfolio

**Target Specializations:** Graphics Co-Design, Low-Level Systems, Hardware-Software Architecture  

This repository aggregates primary engineering projects designed to demonstrate core competencies in low-level systems engineering, modern graphics APIs, and real-time rendering.

## Projects

### [01. SIMD BVH CPU Raytracer](./01_simd_bvh_raytracer/)
*   **Core Infrastructure:** C++, Intel AVX/SSE Intrinsics (mostly using DirectXMath with custom intirinsics functions), Win32 API, Custom Platform Layer, Dynamic Link Library (DLL) Hot-Reloading.
*   Formulated a zero-dependency platform layer interacting directly with native Windows APIs. Designed a data-oriented execution model leveraging runtime DLL hot-reloading for fast-iteration code execution. Structured custom, data-aligned memory layouts to limit cache thrashing and optimize execution across parallel hardware SIMD registers.