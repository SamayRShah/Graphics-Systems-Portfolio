# DX 11 Real-time Engine

Coursework based graphics engine build on DX11 API

## Renderer Demo

https://github.com/user-attachments/assets/54f9fd61-b61e-479b-b5f1-79ce37059262

## Core Milestones & Features

* **Programmable Shading Pipeline:** Vertex and pixel shaders written in HLSL, implementing hardware-accelerated Blinn-Phong reflection models, directional lighting arrays, normal space corrections.
* **Dear ImGui Diagnostics Layer:** Integrated IMGUI layer mapped straight to the active swap chain. Features real-time matrix configuration manipulation, active material tuning, scene node transformation controls, Render stage and texture overviews, And post processing control
* **Hardware Resource Binding:** Abstract encapsulation of native hardware structures, including Vertex Buffers, Index Buffers, and Constant Buffers mapped to structured alignment constraints (Current version uses Simple Shader Library simplify process).

## Build & Installation environment

The project compiles through standard Microsoft Visual Studio configurations

### System Prerequisites

* Windows 10 / 11
* Microsoft Visual Studio 2022 or newer
* Desktop development with C++ workload

### Build Steps

1. Open `D3D11Starter.sln` in Visual Studio.
2. Select a Debug or Release configuration.
3. Build the solution.
4. Run the executable from Visual Studio.

## Runtime Notes

* The application uses a free-fly FPS camera for scene navigation.
* The renderer expects the assets under `Assets/` to remain in place relative to the project.
* Ray tracing is used for the reflection stage; if the device does not support DXR, that path will not initialize.

## Project Structure

```text
├── Assets/                 # Meshes, textures, and skybox resources
├── ImGui/                  # Dear ImGui sources and backends
├── SimpleShader/           # Library to simplify shader resource binding
├── *.cpp / *.h             # DirectX 12 application, scene, and rendering code
├── *.hlsl / *.hlsli        # Raster, lighting, debug, post process, and effect Shaders
└── D3D11Starter.sln        # Visual Studio solution
```
