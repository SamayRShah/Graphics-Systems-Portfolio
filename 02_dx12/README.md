# DirectX 12 Hybrid Renderer

A coursework-based DirectX 12 renderer built on starter code and extended into a hybrid real-time graphics project. The repository focuses on the rendering pipeline, scene composition, PBR materials, a DXR reflection pass, and an SDF overlay/compositing path driven by an ImGui control panel.

**The repository has 2 versions of the project, one with a full pathtracing implementation in dx12, and the other with the hybrid pipeline

## Raytracing demo

https://github.com/user-attachments/assets/cb39e032-5150-4369-9044-8e01e260572a

## Hybrid pipeline demo

https://github.com/user-attachments/assets/a1e38bdd-2961-4349-b2a6-07e7cdffede5

## Core Milestones & Features

* **Deferred-Style G-Buffer Pipeline:** Renders albedo, normals, material properties, and depth into separate render targets before combining them in later passes.
* **Physically Based Raster Shading:** Uses HLSL PBR shaders with directional and point lights, normal mapping, roughness/metalness texture support, and gamma-correct output.
* **Hybrid DXR Reflection Pass:** Adds a ray-traced reflection stage that launches rays from the G-buffer, resolves hits against scene geometry, and falls back to the skybox on misses.
* **Custom Material and Scene Setup:** Builds a small animated scene with multiple meshes, textured and procedural materials, a reflective floor, and a cubemap skybox.
* **Signed Distance Field Composition:** Renders an animated SDF scene in a dedicated pass and blends it into the final frame with adjustable color and blend strength.
* **Dear ImGui Inspector:** Exposes runtime controls for the SDF blend, SDF color, demo window visibility, and render target inspection.
* **DirectX 12 Resource Management:** Uses bindless descriptor indexing, constant-buffer updates, fullscreen passes, and per-frame GPU synchronization to drive the render pipeline.

## Build & Installation Environment

The project builds with standard Microsoft Visual Studio tooling.

### System Prerequisites

* Windows 10 / 11
* Microsoft Visual Studio 2022 or newer
* Desktop development with C++ workload
* Raytracing capable Graphics Card

### Build Steps

1. Open `D3D12.sln` in Visual Studio.
2. Select a Debug or Release configuration.
3. Build the solution.
4. Run the executable from Visual Studio.

## Runtime Notes

* The application uses a free-fly FPS camera for scene navigation.
* The renderer expects the assets under `Assets/` to remain in place relative to the project.
* If the device does not support DXR, the proejct will not initialize.

## Project Structure

```text
├── Assets/                 # Meshes, textures, and skybox resources
├── ImGui/                  # Dear ImGui sources and backends
├── *.cpp / *.h             # DirectX 12 application, scene, and rendering code
├── *.hlsl / *.hlsli        # Raster, lighting, composite, SDF, and ray tracing shaders
└── D3D12.sln               # Visual Studio solution
```
