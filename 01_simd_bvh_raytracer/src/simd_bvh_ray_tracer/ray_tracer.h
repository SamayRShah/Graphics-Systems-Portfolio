#pragma once
#include <cstdint>
#include <DirectXMath.h>
using namespace DirectX;

#define MAX_SPHERES 1000
#define MAX_FILENAME_SIZE 64

enum material_type : int32_t {
  MATERIAL_METAL, MATERIAL_LAMBERT, MATERIAL_DIELECTRIC
};

struct ray {
  DirectX::XMVECTOR Origin;
  DirectX::XMVECTOR Direction;
  DirectX::XMVECTOR InvDirection;
};

struct material {
  DirectX::XMVECTOR Albedo;
  material_type Type;
  union {
    float RefractiveIndex;
    float Fuzz;
  };
};

struct hit_record {
  DirectX::XMVECTOR Point;
  DirectX::XMVECTOR Normal;
  float T;
  int32_t MatId;
  bool FrontFace;

  inline void SetFaceNormal(const ray& R, const DirectX::XMVECTOR OutwardNormal) {
    FrontFace = DirectX::XMVectorGetX(DirectX::XMVector3Dot(R.Direction, OutwardNormal)) < 0;
    Normal = FrontFace ? OutwardNormal : -OutwardNormal;
  }
};

struct sphere_soa {
  DirectX::XMVECTOR* Centers;
  float* Radii;
  material* Materials;
  uint32_t Count;
  uint32_t MaxCount;

  void Add(const DirectX::XMVECTOR& Center, float Radius, const material& Mat) {
    if (Count < MaxCount) {
      Centers[Count] = Center;
      Radii[Count] = Radius;
      Materials[Count] = Mat;
      Count++;
    }
  }
};

struct aabb {
  DirectX::XMVECTOR Min;
  DirectX::XMVECTOR Max;

  // Fully Vectorized, Branchless Ray-AABB Intersection
  inline bool Hit(const ray& R, float TMin, float TMax, float& OutT) const {
    // (Min - Origin) * InvDir  and  (Max - Origin) * InvDir
    DirectX::XMVECTOR T0 = (Min - R.Origin) * R.InvDirection;
    DirectX::XMVECTOR T1 = (Max - R.Origin) * R.InvDirection;

    // Sort intervals per axis
    DirectX::XMVECTOR Vmin = DirectX::XMVectorMin(T0, T1);
    DirectX::XMVECTOR Vmax = DirectX::XMVectorMax(T0, T1);

    // Find max component of Vmin, and min component of Vmax across X, Y, Z
    DirectX::XMVECTOR MaxOfMin = DirectX::XMVectorMax(Vmin, DirectX::XMVectorSwizzle<1, 2, 0, 3>(Vmin));
    MaxOfMin = DirectX::XMVectorMax(MaxOfMin, DirectX::XMVectorSwizzle<2, 0, 1, 3>(Vmin)); // Real t_near

    DirectX::XMVECTOR MinOfMax = DirectX::XMVectorMin(Vmax, DirectX::XMVectorSwizzle<1, 2, 0, 3>(Vmax));
    MinOfMax = DirectX::XMVectorMin(MinOfMax, DirectX::XMVectorSwizzle<2, 0, 1, 3>(Vmax)); // Real t_far

    float TNear = DirectX::XMVectorGetX(MaxOfMin);
    float TFar = DirectX::XMVectorGetX(MinOfMax);

    float TStart = TNear > TMin ? TNear : TMin;
    float TEnd = TFar < TMax ? TFar : TMax;

    OutT = TStart;
    return TStart < TEnd;
  }
};

struct binary_node {
  aabb Box;
  int32_t Left;
  int32_t Right;
  uint32_t IndexOffset;
  uint32_t ObjectCount;

  inline bool IsLeaf() const { return ObjectCount > 0; }
};

// Ideally should align to 128 (To Revisit later)
// Potentially move to bvh8 aligned to 256
struct alignas(16) bvh4_node {
  // Structure of Arrays (SoA) layout for 4 child bounding boxes
  DirectX::XMVECTOR MinX; // [Box0.Min.x, Box1.Min.x, Box2.Min.x, Box3.Min.x]
  DirectX::XMVECTOR MinY; // [Box0.Min.y, Box1.Min.y, Box2.Min.y, Box3.Min.y]
  DirectX::XMVECTOR MinZ; // [Box0.Min.z, Box1.Min.z, Box2.Min.z, Box3.Min.z]
  DirectX::XMVECTOR MaxX; // [Box0.Max.x, Box1.Max.x, Box2.Max.x, Box3.Max.x]
  DirectX::XMVECTOR MaxY; // [Box0.Max.y, Box1.Max.y, Box2.Max.y, Box3.Max.y]
  DirectX::XMVECTOR MaxZ; // [Box0.Max.z, Box1.Max.z, Box2.Max.z, Box3.Max.z]

  // Node Metadata & Offsets (Packed into remaining space)
  int32_t ChildIndices[4];  // Positive = Internal Node ID, Negative = encoded Leaf Offset
  uint32_t IndexOffsets[4]; // Offset into GlobalObjectIndices for leaves
  uint8_t ObjectCounts[4];  // Items in leaf (0 if internal node)
  uint8_t ActiveMask;       // Which child lanes actually contain valid geometry
  uint8_t Padding[3];       // alignment
};

struct camera {
  // Postion
  DirectX::XMVECTOR Center;
  DirectX::XMVECTOR Pixel00Loc;
  DirectX::XMVECTOR PixelDeltaU;
  DirectX::XMVECTOR PixelDeltaV;
  DirectX::XMVECTOR LookFrom;
  DirectX::XMVECTOR LookAt;
  DirectX::XMVECTOR Vup;

  // blur
  DirectX::XMVECTOR DefocusDiskU;
  DirectX::XMVECTOR DefocusDiskV;
  DirectX::XMVECTOR U, V, W;

  // Buffer
  uint32_t ImageWidth;
  uint32_t ImageHeight;
  
  // Quality
  uint32_t SamplesPerPixel;
  uint32_t MaxBounces;
  float PixelSamplesScale;

  // Framing
  float AspectRatio;
  float VfoV;
  float ViewportWidth;
  float ViewportHeight;

  // Preview Quality
  int PreviewScale;
  int PreviewBounces;
  int PreviewSamplesPerPixel;
  float PreviewSamplesScale;


  float Yaw;
  float Pitch;
  float MoveSpeed;
  float MouseSensitivity;
  float Vfov;

  // Blur
  float DefocusAngle;
  float DefocusRadius;
  float FocusDist;
};

struct app_state {
  char Filename[MAX_FILENAME_SIZE];
  uint32_t PrintCount;
  bool32 FullRender;
  bool32 CompletedRender;
  camera Cam;
  sphere_soa World;

  // --- BVH4 Storage Arrays ---
  bvh4_node* Bvh4Nodes;
  uint32_t Bvh4NodeCount;
  uint32_t Bvh4NodeMaxCount;
  int32_t RootNodeBVH4;

  // --- Temporary Build Allocations ---
  binary_node* BinaryNodes;
  uint32_t BinaryNodeCount;
  uint32_t BinaryNodeMaxCount;

  // References mapping memory indices
  uint32_t* GlobalObjectIndices;
  uint32_t* PreviewBuffer;

#if HEADLESS_BUILD
  graphics_buffer HeadlessGraphicsBuffer;
#endif
};
