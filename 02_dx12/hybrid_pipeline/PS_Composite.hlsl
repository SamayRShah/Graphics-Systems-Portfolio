cbuffer DrawData : register(b0)
{
    uint albedoIndex;
    uint normalIndex;
    uint materialIndex;
    uint depthIndex;
    uint lightingIndex;
    uint sdfIndex;
    uint reflectionIndex;
    float sdfBlend;
}

// Defines the input to this pixel shader
struct VertexToPixel
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

// Sampler
SamplerState BasicSampler : register(s0);

// Entry point for this pixel shader
float4 main(VertexToPixel input) : SV_TARGET
{
    // Sample G-buffer textures
    Texture2D albedoTex = ResourceDescriptorHeap[albedoIndex];
    Texture2D normalTex = ResourceDescriptorHeap[normalIndex];
    Texture2D materialTex = ResourceDescriptorHeap[materialIndex];
    Texture2D depthTex = ResourceDescriptorHeap[depthIndex];
    Texture2D lightingTex = ResourceDescriptorHeap[lightingIndex];
    Texture2D sdfTexture = ResourceDescriptorHeap[sdfIndex];
    
    float4 sdf = sdfTexture.SampleLevel(BasicSampler, input.uv, 0);
    float4 albedo = albedoTex.SampleLevel(BasicSampler, input.uv, 0);
    float4 normal = normalTex.SampleLevel(BasicSampler, input.uv, 0); // Assumed to be unpacked in world/view space
    float4 material = materialTex.SampleLevel(BasicSampler, input.uv, 0);
    float depth = depthTex.SampleLevel(BasicSampler, input.uv, 0).r;
    float4 lighting = lightingTex.SampleLevel(BasicSampler, input.uv, 0);
    
    float4 reflection = float4(0.0f, 0.0f, 0.0f, 0.0f);
    if (reflectionIndex != 0)
    {
        Texture2D reflectionTex = ResourceDescriptorHeap[reflectionIndex];
        reflection = reflectionTex.SampleLevel(BasicSampler, input.uv, 0);
    }
    
    bool isSky = depth >= 1.0f;
    
    // Material Parameters
    float roughness = material.r;
    float metalness = material.g;
    float3 N = normalize(normal.xyz * 2.0f - 1.0f);
    
    // TODO: Reconstruct view direction from depth and screen position
    float NdotV = 1.0f;

    // Dielectrics have a default low reflectivity (~0.04). Metals use their albedo color as F0.
    float3 F0 = float3(0.04f, 0.04f, 0.04f);
    F0 = lerp(F0, albedo.rgb, metalness);

    // Smooth surfaces get full specular highlights at grazing angles; rough surfaces blur and lose it.
    float3 F = F0 + (max(float3(1.0f - roughness, 1.0f - roughness, 1.0f - roughness), F0) - F0) * pow(1.0f - NdotV, 5.0f);

    // Diffuse is darkened for metals and completely blocked where reflections take over.
    float3 diffuseColor = albedo.rgb * (1.0f - F0) * (1.0f - metalness);
    float3 diffuseLight = diffuseColor * lighting.rgb;

    // Fade reflections gracefully based on material roughness
    float3 specularReflection = reflection.rgb * F * reflection.a * (1.0f - roughness);

    // 5. Final Composition
    float3 finalColor = diffuseLight + specularReflection;
    
    // override with just sky color if sky
    if (isSky) { finalColor = albedo.rgb; }

    // Composite SDF on top using its alpha if needed
    finalColor = lerp(finalColor, sdf.rgb, sdf.a * sdfBlend);

    // return gamma corrected final color
    return float4(pow(max(finalColor, 0.0f), 1.0f / 2.2f), 1.0f);
}
