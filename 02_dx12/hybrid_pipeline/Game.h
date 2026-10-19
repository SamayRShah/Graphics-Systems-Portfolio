#pragma once

#include <memory>
#include <vector>

#include <d3d12.h>
#include <wrl/client.h>

#include "ImGui/imgui.h"

#include "GameEntity.h"
#include "Camera.h"
#include "Lights.h"
#include "SkyBox.h"

enum RenderTargetType
{
	GBUFFER_ALBEDO, // Geometry Buffers
	GBUFFER_NORMALS, // Geometry Buffers
	GBUFFER_MATERIAL, // Geometry Buffers
	GBUFFER_DEPTH, // Geometry Buffers
	DBUFFER_LIGHTS, // Data Buffer
	CBUFFER_SDF, // Pixel Data / Color Buffers
	// Count is always the last one!
	RENDER_TARGET_TYPE_COUNT
};

struct GameData {
	DirectX::XMFLOAT3 sdfColor;
	float sdfBlend;
	float totalTime;
	float deltaTime;
};

class Game
{
public:
	// Basic OOP setup
	Game();
	~Game();
	Game(const Game&) = delete; // Remove copy constructor
	Game& operator=(const Game&) = delete; // Remove copy-assignment operator

	// Primary functions
	void Update(GameData& data);
	void Draw(GameData& data);
	void RayTrace(float deltaTime, float totalTime);
	void OnResize();
private:

	// init helpers
	void CreateEntities();
	void CreateLights();
	void CreateRootSigAndPipelineState();

	// graphics  data
	D3D12_VIEWPORT viewport{};
	D3D12_RECT scissorRect{};

	// raster pipeline
	Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState;

	// UI
	void UINewFrame(float deltaTime);
	void BuildUI(GameData& data);
	void ImageWithHover(D3D12_GPU_DESCRIPTOR_HANDLE gpuDescHandle, const ImVec2& size);
	bool showUIDemoWindow;

	// scene
	unsigned int lightCount;
	std::vector<Light> lights;
	std::shared_ptr<FPSCamera> camera;
	std::vector<std::shared_ptr<GameEntity>> entities;
	std::shared_ptr<SkyBox> skyBox;

	// defferred
	bool deferredLighting = false;

	// GBuffer
	static const unsigned int MaxRenderTargets = 10;

	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtvDescriptorHeap;
	TextureDetails RenderTargets[RENDER_TARGET_TYPE_COUNT]{};

	// Going to assume the same root signature is compatible with this PSO
	Microsoft::WRL::ComPtr<ID3D12PipelineState> fullScreenTexturePSO;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> lightingPSO;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> sdfPSO;
public:
	// utils
	std::wstring AssetPath = L"../../Assets/";
};

