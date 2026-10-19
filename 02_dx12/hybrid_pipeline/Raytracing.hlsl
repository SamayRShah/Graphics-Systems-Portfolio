// === Defines ===
#define PI 3.141592654f

// === UTILITY FUNCTIONS ============================================

// Basic sample and unpack
float3 SampleAndUnpackNormalMap(Texture2D map, SamplerState samp, float2 uv)
{
    return map.SampleLevel(samp, uv, 0).rgb * 2.0f - 1.0f;
}

// Handle converting tangent-space normal map to world space normal
float3 NormalMapping(Texture2D map, SamplerState samp, float2 uv, float3 normal, float3 tangent)
{
	// Grab the normal from the map
    float3 normalFromMap = SampleAndUnpackNormalMap(map, samp, uv);

	// Gather the required vectors for converting the normal
    float3 N = normal;
    float3 T = normalize(tangent - N * dot(tangent, N));
    float3 B = cross(T, N);

	// Create the 3x3 matrix to convert from TANGENT-SPACE normals to WORLD-SPACE normals
    float3x3 TBN = float3x3(T, B, N);

	// Adjust the normal from the map and simply use the results
    return normalize(mul(normalFromMap, TBN));
}

float FresnelView(float3 n, float3 v, float f0)
{
	// Pre-calculations
    float NdotV = saturate(dot(n, v));

	// Final value
    return f0 + (1 - f0) * pow(1 - NdotV, 5);
}

float3 ReconstructWorldPosition(float2 uv, float depth, float4x4 invVP)
{
    float2 ndc = uv * 2.0f - 1.0f;
    ndc.y = -ndc.y;

    float4 clip = float4(ndc, depth, 1.0f);
    float4 world = mul(invVP, clip);
    world.xyz /= world.w;
    return world.xyz;
}

// === Structs ===

// Layout of data in the vertex buffer
struct Vertex
{
    float3 localPosition;
    float2 uv;
    float3 normal;
    float3 tangent;
};

struct SceneData
{
    matrix InverseViewProjection;
    float3 CameraPosition;
    uint RaysPerPixel;
};

struct EntityData
{
    float4 Color;
    uint VertexBufferDescriptorIndex;
    uint IndexBufferDescriptorIndex;
	
    float2 UvScale;
    float2 UvOffset;
    uint AlbedoIndex;
    uint NormalMapIndex;
    uint RoughnessIndex;
    uint MetalnessIndex;
	
    float Roughness;
    float Metalness;
    float Emissive;
    float IOR;
    float Alpha;
    
    float pad;
};

// Payload for rays (data that is "sent along" with each ray during raytrace)
// Note: This should be as small as possible, and must match our C++ size definition
struct RayPayload
{
	float3 Color;
    uint RecursionDepth;
    uint RayPerPixelIndex;
};

struct ShadowPayload
{
    bool Hit;
};

// Note: We'll be using the built-in BuiltInTriangleIntersectionAttributes struct
// for triangle attributes, so no need to define our own.  It contains a single float2.

// === Samplers ===
SamplerState BasicSampler : register(s0);

// === Constant buffers ===

cbuffer DrawData : register(b0)
{
    uint SceneDataConstantBufferIndex;
    uint EntityDataDescriptorIndex;
    uint SceneTLASDescriptorIndex;
    uint OutputUAVDescriptorIndex;
    uint SkyboxDescriptorIndex;
    
    // GBuffer
    uint GBufferAlbedoIndex;
    uint GBufferNormalIndex;
    uint GBufferMaterialIndex;
    uint GBufferDepthIndex;
};


// === Helpers ===

// Barycentric interpolation of data from the triangle's vertices
Vertex InterpolateVertices(uint triangleIndex, float2 barycentrics)
{
	// Get the data for this entity
    StructuredBuffer<EntityData> ed =
		ResourceDescriptorHeap[EntityDataDescriptorIndex];
	EntityData thisEntity = ed[InstanceIndex()];
	
	// get geometry buffers
    StructuredBuffer<uint> IndexBuffer =
		ResourceDescriptorHeap[thisEntity.IndexBufferDescriptorIndex];
    StructuredBuffer<Vertex> VertexBuffer =
		ResourceDescriptorHeap[thisEntity.VertexBufferDescriptorIndex];
	
	// Grab the 3 indices for this triangle
	uint firstIndex = triangleIndex * 3;
	uint indices[3];
	indices[0] = IndexBuffer[firstIndex + 0];
	indices[1] = IndexBuffer[firstIndex + 1];
	indices[2] = IndexBuffer[firstIndex + 2];

	// Grab the 3 corresponding vertices
	Vertex verts[3];
	verts[0] = VertexBuffer[indices[0]];
	verts[1] = VertexBuffer[indices[1]];
	verts[2] = VertexBuffer[indices[2]];
	
	// Calculate the barycentric data for vertex interpolation
	float3 barycentricData = float3(
		1.0f - barycentrics.x - barycentrics.y,
		barycentrics.x,
		barycentrics.y);
	
	// Loop through the barycentric data and interpolate
    Vertex finalVert = (Vertex)0;
	for (uint i = 0; i < 3; i++)
	{
		finalVert.localPosition += verts[i].localPosition * barycentricData[i];
		finalVert.uv += verts[i].uv * barycentricData[i];
		finalVert.normal += verts[i].normal * barycentricData[i];
		finalVert.tangent += verts[i].tangent * barycentricData[i];
	}
	return finalVert;
}


// Calculates an origin and direction from the camera for specific pixel indices
RayDesc CalcRayFromCamera(float2 rayIndices, float3 camPos, float4x4 invVP)
{
	// Offset to the middle of the pixel
	float2 pixel = rayIndices + 0.5f;
	float2 screenPos = pixel / DispatchRaysDimensions().xy * 2.0f - 1.0f;
	screenPos.y = -screenPos.y;

	// Unproject the coords
	float4 worldPos = mul(invVP, float4(screenPos, 0, 1));
	worldPos.xyz /= worldPos.w;

	// Set up the ray
	RayDesc ray;
	ray.Origin = camPos.xyz;
	ray.Direction = normalize(worldPos.xyz - ray.Origin);
	ray.TMin = 0.01f;
	ray.TMax = 1000.0f;
	return ray;
}

// === Pseudo-Random Number Generation ===

// Based on https://thebookofshaders.com/10/
float rand(float2 uv)
{
    return frac(sin(dot(uv, float2(12.9898, 78.233))) * 43758.5453);
}

float2 rand2(float2 uv)
{
    return float2(
		rand(uv),
		rand(uv.yx));
}

// From: https://www.reedbeta.com/blog/hash-functions-for-gpu-rendering/
uint pcg_hash(uint input)
{
    uint state = input * 747796405u + 2891336453u;
    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

float pcg_hash_0_1(uint input)
{
    return (float) pcg_hash(input) / (-1u);
}


// === Sampling Functions ===
// Based on Chapter 16 of Raytracing Gems

// Params should be uniform between [0,1]
float3 RandomVector(float u0, float u1)
{
    float a = u0 * 2 - 1;
    float b = sqrt(1 - a * a);
    float phi = 2.0f * PI * u1;

    float x = b * cos(phi);
    float y = b * sin(phi);
    float z = a;

    return float3(x, y, z);
}

// First two params should be uniform between [0,1]
float3 RandomCosineWeightedHemisphere(float u0, float u1, float3 unitNormal)
{
    float a = u0 * 2 - 1;
    float b = sqrt(1 - a * a);
    float phi = 2.0f * PI * u1;

    float x = unitNormal.x + b * cos(phi);
    float y = unitNormal.y + b * sin(phi);
    float z = unitNormal.z + a;

	// float pdf = a / PI;
    return float3(x, y, z);
}

// === Shaders ===

// Ray generation shader - Launched once for each ray we want to generate
// (which is generally once per pixel of our output texture)
[shader("raygeneration")]
void RayGen()
{
	// Grab the constant buffer
    ConstantBuffer<SceneData> cb = ResourceDescriptorHeap[SceneDataConstantBufferIndex];

    // Each dispatch thread corresponds to one screen pixel.
    uint2 rayIndices = DispatchRaysIndex().xy;

    RWTexture2D<float4> OutputColor = ResourceDescriptorHeap[OutputUAVDescriptorIndex];
    Texture2D<float4> GBufferNormal = ResourceDescriptorHeap[GBufferNormalIndex];
    Texture2D<float> GBufferDepth = ResourceDescriptorHeap[GBufferDepthIndex];
    float2 uv = ((float2)rayIndices + 0.5f) / DispatchRaysDimensions().xy;
    float depth = GBufferDepth.SampleLevel(BasicSampler, uv, 0);

    // The G-buffer has no surface at background pixels, so they have no reflection.
    if (depth >= 1.0f)
    {
        OutputColor[rayIndices] = float4(0, 0, 0, 0);
        return;
    }

    float3 position = ReconstructWorldPosition(uv, depth, cb.InverseViewProjection);
    float3 normal = normalize(GBufferNormal.SampleLevel(BasicSampler, uv, 0).rgb * 2.0f - 1.0f);
    float3 incident = normalize(position - cb.CameraPosition);

    RayDesc ray;
    ray.Origin = position + normal * 0.01f;
    ray.Direction = normalize(reflect(incident, normal));
    ray.TMin = 0.001f;
    ray.TMax = 1000.0f;

    RayPayload payload = (RayPayload)0;
    payload.Color = float3(1, 1, 1);
    payload.RecursionDepth = 0;
    payload.RayPerPixelIndex = 0;

    RaytracingAccelerationStructure SceneTLAS = ResourceDescriptorHeap[SceneTLASDescriptorIndex];
    TraceRay(SceneTLAS, RAY_FLAG_NONE, 0xFF, 0, 0, 0, ray, payload);

    // OutputColor[rayIndices] = float4(pow(max(payload.Color, 0.0f), 1.0f / 2.2f), 1.0f); // Gamma Corrected final color
    OutputColor[rayIndices] = float4(payload.Color, 1.0f);
}


// Miss shader - What happens if the ray doesn't hit anything?
[shader("miss")]
void Miss(inout RayPayload payload)
{
	// Hit the skybox, alter by the sky color
    TextureCube sky = ResourceDescriptorHeap[SkyboxDescriptorIndex];
    payload.Color *= sky.SampleLevel(BasicSampler, WorldRayDirection(), 0).rgb;
}


// Closest hit shader - Runs the first time a ray hits anything
[shader("closesthit")]
void ClosestHit(inout RayPayload payload, BuiltInTriangleIntersectionAttributes hitAttributes)
{
    // If we've reached the max recursion, we haven't hit a light source (the sky, which is the "miss shader" here)
    if (payload.RecursionDepth == 2)
    {
        payload.Color = float3(0, 0, 0);
        return;
    }
	
	// Get the data for this entity
    StructuredBuffer<EntityData> entityDataBuffer = ResourceDescriptorHeap[EntityDataDescriptorIndex];
    EntityData thisEntity = entityDataBuffer[InstanceIndex()];
	
	// Get the geometry hit details and convert normal to world space
    Vertex hit = InterpolateVertices(PrimitiveIndex(), hitAttributes.barycentrics);
    float3 normal_WS = normalize(mul(hit.normal, (float3x3) ObjectToWorld4x3()));
    float3 tangent_WS = normalize(mul(hit.tangent, (float3x3) ObjectToWorld4x3()));

	// Basic surface color
    float metalness = thisEntity.Metalness;
    float roughness = saturate(pow(thisEntity.Roughness, 2)); // Squared remap
    float3 surfaceColor = thisEntity.Color.rgb;
	
	// Texture?
    if (thisEntity.AlbedoIndex != -1)
    {
        Texture2D Albedo = ResourceDescriptorHeap[thisEntity.AlbedoIndex];
        // surfaceColor = pow(Albedo.SampleLevel(BasicSampler, hit.uv, 0).rgb, 2.2f);
        surfaceColor = Albedo.SampleLevel(BasicSampler, hit.uv, 0).rgb;
    }
    if (thisEntity.NormalMapIndex != -1)
    {
        Texture2D NormalMap = ResourceDescriptorHeap[thisEntity.NormalMapIndex];
        normal_WS = NormalMapping(NormalMap, BasicSampler, hit.uv, normal_WS, tangent_WS);
    }
    if (thisEntity.RoughnessIndex != -1)
    {
        Texture2D Roughness = ResourceDescriptorHeap[thisEntity.RoughnessIndex];
        // roughness = pow(Roughness.SampleLevel(BasicSampler, hit.uv, 0).r, 2);
        roughness = Roughness.SampleLevel(BasicSampler, hit.uv, 0).r;
    }
    if (thisEntity.MetalnessIndex != -1)
    {
        Texture2D Metalness = ResourceDescriptorHeap[thisEntity.MetalnessIndex];
        metalness = Metalness.SampleLevel(BasicSampler, hit.uv, 0).r;
    }
	
	// Calc a unique RNG value for this ray, based on the "uv" (0-1 location) of this pixel and other per-ray data
    float2 pixelUV = (float2) DispatchRaysIndex().xy / DispatchRaysDimensions().xy;
    float2 rng = rand2(pixelUV * (payload.RecursionDepth + 1) + payload.RayPerPixelIndex + RayTCurrent());
	
	// Interpolate between perfect reflection and random bounce based on roughness
    float3 refl = reflect(WorldRayDirection(), normal_WS);
    float3 randomBounce = RandomCosineWeightedHemisphere(rand(rng), rand(rng.yx), normal_WS);
    float3 dir = normalize(lerp(refl, randomBounce, roughness));
	
	// Interpolate between fully random bounce and roughness-based bounce based on fresnel/metal switch
	// - If we're a "diffuse" ray, we need a random bounce
	// - If we're a "specular" ray, we need the roughness-based bounce
	// - Metals will have a fresnel result of 1.0, so this won't affect them
    float fres = FresnelView(-WorldRayDirection(), normal_WS, lerp(0.04f, 1.0f, metalness));
    dir = normalize(lerp(randomBounce, dir, fres > rng.x));
	
	// Determine how we color the ray:
	// - If this is a "diffuse" ray, use the surface color
	// - If this is a "specular" ray, assume a bounce without tint
	// - Metals always tint, so the final lerp below takes care of that
    float3 roughnessBounceColor = lerp(float3(1, 1, 1), surfaceColor, roughness); // Dir is roughness-based, so color is too
    float3 diffuseColor = lerp(surfaceColor, roughnessBounceColor, fres > rng.x); // Diffuse "reflection" chance
    float3 finalColor = lerp(diffuseColor, surfaceColor, metalness); // Metal always tints
    payload.Color *= finalColor;
    // Create the new recursive ray
    RayDesc ray;
    ray.Origin = WorldRayOrigin() + WorldRayDirection() * RayTCurrent();
    ray.Direction = dir;
    ray.TMin = 0.0001f;
    ray.TMax = 1000.0f;
	
	// Recursive ray trace
    payload.RecursionDepth++;
    RaytracingAccelerationStructure SceneTLAS = ResourceDescriptorHeap[SceneTLASDescriptorIndex];
    TraceRay(
		SceneTLAS,
		RAY_FLAG_NONE,
		0xFF, 0, 0, 0, // Mask and offsets
		ray,
		payload);
}

// shadowRay miss shader
[shader("miss")]
void MissShadow(inout ShadowPayload payload)
{
    payload.Hit = false;
}


// shadow hit shader
[shader("closesthit")]
void ClosestHitShadow(inout ShadowPayload payload, BuiltInTriangleIntersectionAttributes hitAttributes)
{
    payload.Hit = true;
}
