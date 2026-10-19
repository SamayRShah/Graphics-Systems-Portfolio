#define MAX_STEPS 100.0f
#define MAX_DIST 100.0f
#define SURF_DIST 0.01f
#define EPSILON 0.001f

cbuffer BindlessData : register(b0)
{
    uint sdfDataIndex;
}

struct sdfData
{
    matrix inverseViewProjection;
    
    float3 cameraPosition;
    float totalTime;
    
    float3 color;
    int screenWidth;
    int screenHeight;
};

struct VertexToPixel
{
    float4 screenPosition : SV_POSITION;
    float2 uv : TEXCOORD;
};

float SDFSphere(float3 p, float3 center, float radius)
{
    return length(p - center) - radius;
}

float SDFCube(float3 p, float3 center, float3 halfExtents)
{
    float3 d = abs(p - center) - halfExtents;
    return length(max(d, 0.0f)) + min(max(d.x, max(d.y, d.z)), 0.0f);
}

float SDFSmoothUnion(float a, float b, float k = 0.35f)
{
    float h = saturate(0.5f + 0.5f * (b - a) / k);
    return lerp(b, a, h) - k * h * (1.0f - h);
}

float SDFSmoothDifference(float a, float b, float k = 0.35f)
{
    float h = saturate(0.5f - 0.5f * (a + b) / k);
    return lerp(a, -b, h) + k * h * (1.0f - h);
}

float SDFUnion(float a, float b)
{
    return min(a, b);
}

float SDFDifference(float a, float b)
{
    return max(a, -b);
}

float GetDist(float3 p, float totalTime)
{
    // Ground plane
    float groundOffset = 1.0f;
    float planeDist = p.y + groundOffset;
    
    // Cube sitting on the ground at x=0, z=6
    float3 cubeCenter = float3(0.0f, 1.0f, 6.0f);
    float cubeDist = SDFCube(p, cubeCenter, float3(1.0f, 1.0f, 1.0f));

    float sphereRadius = 0.95f;
    float sphereSpeed = 0.25f;
    float spherePhase = totalTime * sphereSpeed * 6.2831853f;
    float sphereWave = sin(spherePhase);
    float sphereProgress = 0.5f * (sphereWave + 1.0f);
    float sideTravel = lerp(3.0f, 0.0f, sphereProgress);

    float3 sphereFromRightCenter = float3(cubeCenter.x + sideTravel, 1.0f, cubeCenter.z);
    float3 sphereFromLeftCenter = float3(cubeCenter.x - sideTravel, 1.0f, cubeCenter.z);
    float3 sphereFromFrontCenter = float3(cubeCenter.x, 1.0f, cubeCenter.z + sideTravel);
    float3 sphereFromBackCenter = float3(cubeCenter.x, 1.0f, cubeCenter.z - sideTravel);

    float sphereFromRightDist = SDFSphere(p, sphereFromRightCenter, sphereRadius);
    float sphereFromLeftDist = SDFSphere(p, sphereFromLeftCenter, sphereRadius);
    float sphereFromFrontDist = SDFSphere(p, sphereFromFrontCenter, sphereRadius);
    float sphereFromBackDist = SDFSphere(p, sphereFromBackCenter, sphereRadius);

    float verticalTravel = 2.0f;
    float sphereY = cubeCenter.y + sphereWave * verticalTravel;
    float3 sphereYCenter = float3(0.0f, sphereY, 6.0f);
    float sphereYDist = SDFSphere(p, sphereYCenter, 0.75f);

    // union ground spheres
    float groundSpheres = SDFUnion(
        SDFUnion(sphereFromRightDist, sphereFromLeftDist),
        SDFUnion(sphereFromFrontDist, sphereFromBackDist));

    // difference sphere from cube
    float objectDist = SDFSmoothDifference(cubeDist, groundSpheres);
    
    // union vertical sphere with cube
    objectDist = SDFSmoothUnion(objectDist, sphereYDist);

    return SDFSmoothUnion(planeDist, objectDist);
}

float RayMarch(float3 ro, float3 rd, float totalTime)
{
    float od = 0;
    float ds = 0;

    [loop]
    for (int i = 0; i < MAX_STEPS; ++i)
    {
        float3 p = ro + rd * od;
        ds = GetDist(p, totalTime);
        od += ds;
        if (od > MAX_DIST || ds < SURF_DIST) break;
    }
    return od;
}


float3 GetNormal(float3 p, float totalTime)
{
    float d = GetDist(p, totalTime);
    float2 e = float2(0.01f, 0.0f);

    float3 n = d - float3(
        GetDist(p - e.xyy, totalTime),
        GetDist(p - e.yxy, totalTime),
        GetDist(p - e.yyx, totalTime)
    );

    return normalize(n);
}

float GetLight(float3 p, float totalTime)
{
    // Circle the light above the cube.
    float lightRadius = 3.5f;
    float lightHeight = 4.0f;

    float lightX = cos(totalTime * 1.5f) * lightRadius;
    float lightZ = 6.0f + sin(totalTime * 1.5f) * lightRadius;
    float3 lightPos = float3(lightX, lightHeight, lightZ);

    float3 l = normalize(lightPos - p);
    float3 n = GetNormal(p, totalTime);

    float diff = saturate(dot(n, l));
    return diff;
}

float4 main(VertexToPixel input) : SV_TARGET
{
    ConstantBuffer<sdfData> data = ResourceDescriptorHeap[sdfDataIndex];

    // Create NDCs
    float2 ndc = input.uv * 2.0f - 1.0f;
    ndc.y = -ndc.y;

    // Reconstruct a world-space ray from the camera through this pixel.
    float4 worldPos = mul(data.inverseViewProjection, float4(ndc, 0.0f, 1.0f));
    worldPos.xyz /= worldPos.w;

    float3 ro = data.cameraPosition;
    float3 rd = normalize(worldPos.xyz - ro);

    float3 color = data.color;
    float d = RayMarch(ro, rd, data.totalTime);

    float3 p = ro + rd * d;
    float diff = GetLight(p, data.totalTime);

    return float4(color * diff, diff);
}
