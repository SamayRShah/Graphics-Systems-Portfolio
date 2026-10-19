#include <math.h>
#include <stdio.h> // for sscan
#include "platform_interface.h"
#include "ray_tracer.h"
#include "rt_image_utils.h"
#include "utils.h"

static const DirectX::XMVECTOR White = { 1.0f, 1.0f, 1.0f, 1.0f };
static const DirectX::XMVECTOR Blue = { 0.5f, 0.7f, 1.0f, 1.0f };

aabb SphereBoundingBox(const DirectX::XMVECTOR& Center, float Radius) {
  DirectX::XMVECTOR RadVec = DirectX::XMVectorReplicate(Radius);
  return { Center - RadVec, Center + RadVec };
}

struct sort_context { const sphere_soa* Spheres; int Axis; };
void QuickSortIndices(uint32_t* Indices, int Left, int Right, sort_context Context) {
  // Index tracking pair stack (max 64 elements is safe for 2^32 primitives)
  struct sort_range { int Left; int Right; };
  sort_range RangeStack[64];
  int StackPtr = 0;

  RangeStack[StackPtr++] = { Left, Right };

  while (StackPtr > 0) {
    sort_range Curr = RangeStack[--StackPtr];
    int L = Curr.Left;
    int R = Curr.Right;

    if (L >= R) continue;

    int I = L, J = R;
    uint32_t PivotIdx = Indices[L + (R - L) / 2];

    alignas(16) float PCenter[4];
    DirectX::XMStoreFloat4((DirectX::XMFLOAT4*)PCenter, Context.Spheres->Centers[PivotIdx]);
    float PivotVal = PCenter[Context.Axis];

    while (I <= J) {
      alignas(16) float ICenter[4];
      alignas(16) float JCenter[4];

      DirectX::XMStoreFloat4((DirectX::XMFLOAT4*)ICenter, Context.Spheres->Centers[Indices[I]]);
      DirectX::XMStoreFloat4((DirectX::XMFLOAT4*)JCenter, Context.Spheres->Centers[Indices[J]]);
      while (I < R && ICenter[Context.Axis] < PivotVal) {
        I++;
        DirectX::XMStoreFloat4((DirectX::XMFLOAT4*)ICenter, Context.Spheres->Centers[Indices[I]]);
      }
      while (J > L && JCenter[Context.Axis] > PivotVal) {
        J--;
        DirectX::XMStoreFloat4((DirectX::XMFLOAT4*)JCenter, Context.Spheres->Centers[Indices[J]]);
      }
      if (I <= J) {
        uint32_t Tmp = Indices[I]; Indices[I] = Indices[J]; Indices[J] = Tmp;
        I++; J--;
      }
    }

    // Push partitions onto satck
    if (I < R) {
      Assert(StackPtr < 64);
      RangeStack[StackPtr++] = { I, R };
    }
    if (L < J) {
      Assert(StackPtr < 64);
      RangeStack[StackPtr++] = { L, J };
    }
  }
}

int BuildBinaryTree(app_state* State, const sphere_soa& Spheres, uint32_t IndexOffset, uint32_t Count, int LeafCount = 2) {
  Assert(State->BinaryNodeCount < State->BinaryNodeMaxCount);
  int CurrentIdx = (int)State->BinaryNodeCount++;
  binary_node& Node = State->BinaryNodes[CurrentIdx];

  DirectX::XMVECTOR BoxMin = DirectX::XMVectorSet(FLT_MAX, FLT_MAX, FLT_MAX, 0);
  DirectX::XMVECTOR BoxMax = DirectX::XMVectorSet(-FLT_MAX, -FLT_MAX, -FLT_MAX, 0);

  for (uint32_t I = 0; I < Count; ++I) {
    uint32_t ObjIdx = State->GlobalObjectIndices[IndexOffset + I];
    aabb Bbox = SphereBoundingBox(Spheres.Centers[ObjIdx], Spheres.Radii[ObjIdx]);
    BoxMin = DirectX::XMVectorMin(BoxMin, Bbox.Min);
    BoxMax = DirectX::XMVectorMax(BoxMax, Bbox.Max);
  }
  Node.Box = { BoxMin, BoxMax };

  if (Count <= (uint32_t)LeafCount) {
    Node.Left = -1;
    Node.Right = -1;
    Node.IndexOffset = IndexOffset;
    Node.ObjectCount = Count;
    return CurrentIdx;
  }

  Node.ObjectCount = 0;

  DirectX::XMVECTOR Extents = BoxMax - BoxMin;
  float Ex = DirectX::XMVectorGetX(Extents), Ey = DirectX::XMVectorGetY(Extents), Ez = DirectX::XMVectorGetZ(Extents);
  int Axis = Ex > Ey ? (Ex > Ez ? 0 : 2) : (Ey > Ez ? 1 : 2);

  sort_context Ctx = { &Spheres, Axis };
  QuickSortIndices(State->GlobalObjectIndices, IndexOffset, IndexOffset + Count - 1, Ctx);

  uint32_t Mid = Count / 2;
  Node.Left = BuildBinaryTree(State, Spheres, IndexOffset, Mid, LeafCount);
  Node.Right = BuildBinaryTree(State, Spheres, IndexOffset + Mid, Count - Mid, LeafCount);

  return CurrentIdx;
}

int CollapseBinaryToBVH4(app_state* State, int BinNodeIdx) {
  if (BinNodeIdx == -1) return -1;

  binary_node& BinNode = State->BinaryNodes[BinNodeIdx];

  // Allocate new bvh array from flat arena array
  uint32_t Bvh4Idx = State->Bvh4NodeCount++;
  bvh4_node& Node = State->Bvh4Nodes[Bvh4Idx];

  // Arrays to collect up to 4 binary targets to represent the 4 lanes
  int Targets[4] = { -1, -1, -1, -1 };
  int TargetCount = 0;

  // Look-ahead queue to pull children/grandchildren out of the binary tree
  if (!BinNode.IsLeaf()) {
    int Queue[4];
    int Head = 0, Tail = 0;
    Queue[Tail++] = BinNode.Left;
    Queue[Tail++] = BinNode.Right;

    // Flatten up to 2 levels deep to get 4 nodes
    while (Head < Tail && Tail < 4) {
      int Curr = Queue[Head++];
      binary_node& CNode = State->BinaryNodes[Curr];
      if (!CNode.IsLeaf() && (Tail - Head + 2 <= 4)) {
        Queue[Tail++] = CNode.Left;
        Queue[Tail++] = CNode.Right;
      }
      else {
        Targets[TargetCount++] = Curr;
      }
    }
    while (Head < Tail) {
      Targets[TargetCount++] = Queue[Head++];
    }
  }
  else {
    Targets[TargetCount++] = BinNodeIdx;
  }

  Node.ActiveMask = 0;
  alignas(16) float Mx[4] = { FLT_MAX, FLT_MAX, FLT_MAX, FLT_MAX };
  alignas(16) float My[4] = { FLT_MAX, FLT_MAX, FLT_MAX, FLT_MAX };
  alignas(16) float Mz[4] = { FLT_MAX, FLT_MAX, FLT_MAX, FLT_MAX };
  alignas(16) float Kx[4] = { -FLT_MAX, -FLT_MAX, -FLT_MAX, -FLT_MAX };
  alignas(16) float Ky[4] = { -FLT_MAX, -FLT_MAX, -FLT_MAX, -FLT_MAX };
  alignas(16) float Kz[4] = { -FLT_MAX, -FLT_MAX, -FLT_MAX, -FLT_MAX };

  for (int I = 0; I < TargetCount; ++I) {
    int BIdx = Targets[I];
    if (BIdx == -1) continue;

    binary_node& BNode = State->BinaryNodes[BIdx];
    Node.ActiveMask |= (1 << I);

    // Store bounding dimensions into flat staging structures for SIMD loading
    Mx[I] = DirectX::XMVectorGetX(BNode.Box.Min);
    My[I] = DirectX::XMVectorGetY(BNode.Box.Min);
    Mz[I] = DirectX::XMVectorGetZ(BNode.Box.Min);

    Kx[I] = DirectX::XMVectorGetX(BNode.Box.Max);
    Ky[I] = DirectX::XMVectorGetY(BNode.Box.Max);
    Kz[I] = DirectX::XMVectorGetZ(BNode.Box.Max);

    if (BNode.IsLeaf()) {
      Node.ChildIndices[I] = -1; // Negative flags a leaf node base
      Node.IndexOffsets[I] = BNode.IndexOffset;
      Node.ObjectCounts[I] = (uint8_t)BNode.ObjectCount;
    }
    else {
      // Recursively collapse subtrees and record the generated BVH4 index
      Node.ChildIndices[I] = CollapseBinaryToBVH4(State, BIdx);
      Node.ObjectCounts[I] = 0;
    }
  }

  // Load arrays directly into parallel SIMD lanes
  Node.MinX = DirectX::XMLoadFloat4((DirectX::XMFLOAT4*)Mx);
  Node.MinY = DirectX::XMLoadFloat4((DirectX::XMFLOAT4*)My);
  Node.MinZ = DirectX::XMLoadFloat4((DirectX::XMFLOAT4*)Mz);
  Node.MaxX = DirectX::XMLoadFloat4((DirectX::XMFLOAT4*)Kx);
  Node.MaxY = DirectX::XMLoadFloat4((DirectX::XMFLOAT4*)Ky);
  Node.MaxZ = DirectX::XMLoadFloat4((DirectX::XMFLOAT4*)Kz);

  return (int)Bvh4Idx;
}

inline bool HitSphereSoA(const sphere_soa& Spheres, size_t I, const ray& Ray, float TMin, float TMax, hit_record& Rec) {
  DirectX::XMVECTOR Oc = Ray.Origin - Spheres.Centers[I];
  float HalfB = DirectX::XMVectorGetX(DirectX::XMVector3Dot(Oc, Ray.Direction));
  float C = DirectX::XMVectorGetX(DirectX::XMVector3Dot(Oc, Oc)) - Spheres.Radii[I] * Spheres.Radii[I];

  float Discriminant = HalfB * HalfB - C;
  if (Discriminant < 0) return false;

  float Sqrtd = sqrtf(Discriminant);
  float Root = (-HalfB - Sqrtd);
  if (Root < TMin || Root > TMax) {
    Root = -HalfB + Sqrtd;
    if (Root < TMin || Root > TMax) return false;
  }

  Rec.T = Root;
  Rec.Point = Ray.Origin + Root * Ray.Direction;
  DirectX::XMVECTOR OutwardNormal = (Rec.Point - Spheres.Centers[I]) / Spheres.Radii[I];
  Rec.SetFaceNormal(Ray, OutwardNormal);
  Rec.MatId = (int32_t)I; // Lightweight reference tracking
  return true;
}

bool HitBVH4(const app_state* State, int RootIdx, const ray& Ray, float TMin, float TMax, hit_record& Rec) {
  // Tiny, flat stack containing internal node indices to trace
  int32_t NodeStack[64];
  int32_t StackPtr = 0;
  NodeStack[StackPtr++] = RootIdx;

  bool HitAnything = false;
  float Closest = TMax;

  // Replicate ray vectors to compute transformations across 4 vector lanes smoothly
  DirectX::XMVECTOR OrigX = DirectX::XMVectorReplicate(DirectX::XMVectorGetX(Ray.Origin));
  DirectX::XMVECTOR OrigY = DirectX::XMVectorReplicate(DirectX::XMVectorGetY(Ray.Origin));
  DirectX::XMVECTOR OrigZ = DirectX::XMVectorReplicate(DirectX::XMVectorGetZ(Ray.Origin));

  DirectX::XMVECTOR InvX = DirectX::XMVectorReplicate(DirectX::XMVectorGetX(Ray.InvDirection));
  DirectX::XMVECTOR InvY = DirectX::XMVectorReplicate(DirectX::XMVectorGetY(Ray.InvDirection));
  DirectX::XMVECTOR InvZ = DirectX::XMVectorReplicate(DirectX::XMVectorGetZ(Ray.InvDirection));

  while (StackPtr > 0) {
    int32_t CurrIdx = NodeStack[--StackPtr];
    const bvh4_node& Node = State->Bvh4Nodes[CurrIdx];

    // Vectorized Ray To 4 AABB intersections
    DirectX::XMVECTOR T0x = (Node.MinX - OrigX) * InvX;
    DirectX::XMVECTOR T1x = (Node.MaxX - OrigX) * InvX;
    DirectX::XMVECTOR TminX = DirectX::XMVectorMin(T0x, T1x);
    DirectX::XMVECTOR TmaxX = DirectX::XMVectorMax(T0x, T1x);

    DirectX::XMVECTOR T0y = (Node.MinY - OrigY) * InvY;
    DirectX::XMVECTOR T1y = (Node.MaxY - OrigY) * InvY;
    DirectX::XMVECTOR TminY = DirectX::XMVectorMin(T0y, T1y);
    DirectX::XMVECTOR TmaxY = DirectX::XMVectorMax(T0y, T1y);

    DirectX::XMVECTOR T0z = (Node.MinZ - OrigZ) * InvZ;
    DirectX::XMVECTOR T1z = (Node.MaxZ - OrigZ) * InvZ;
    DirectX::XMVECTOR TminZ = DirectX::XMVectorMin(T0z, T1z);
    DirectX::XMVECTOR TmaxZ = DirectX::XMVectorMax(T0z, T1z);

    // Find entry and exit planes per lane
    DirectX::XMVECTOR TNear = DirectX::XMVectorMax(DirectX::XMVectorMax(TminX, TminY), TminZ);
    DirectX::XMVECTOR TFar = DirectX::XMVectorMin(DirectX::XMVectorMin(TmaxX, TmaxY), TmaxZ);
    // Ensure hits fall safely within current closest tracking distance
    DirectX::XMVECTOR V_TMin = DirectX::XMVectorReplicate(TMin);
    DirectX::XMVECTOR V_Closest = DirectX::XMVectorReplicate(Closest);
    TNear = DirectX::XMVectorMax(TNear, V_TMin);
    TFar = DirectX::XMVectorMin(TFar, V_Closest);

    DirectX::XMVECTOR Comparison = DirectX::XMVectorLessOrEqual(TNear, TFar);

    alignas(16) uint32_t MaskBits[4];
    DirectX::XMStoreInt4(MaskBits, Comparison);

    uint32_t HitMask = 0;
    if (MaskBits[0]) HitMask |= 0x1;
    if (MaskBits[1]) HitMask |= 0x2;
    if (MaskBits[2]) HitMask |= 0x4;
    if (MaskBits[3]) HitMask |= 0x8;

    HitMask &= Node.ActiveMask;
    if (HitMask == 0) { continue; }

    // sort active child hits by proximity
    alignas(16) float NearDistances[4];
    DirectX::XMStoreFloat4((DirectX::XMFLOAT4*)NearDistances, TNear);
    int32_t HitLanes[4];
    int32_t HitCount = 0;
    for (int I = 0; I < 4; ++I) {
      if (HitMask & (1 << I)) {
        HitLanes[HitCount++] = I;
      }
    }
    // Bubble Sort for closest hit to hit stack last
    for (int I = 0; I < HitCount - 1; ++I) {
      for (int J = 0; J < HitCount - I - 1; ++J) {
        if (NearDistances[HitLanes[J]] < NearDistances[HitLanes[J + 1]]) {
          int Tmp = HitLanes[J];
          HitLanes[J] = HitLanes[J + 1];
          HitLanes[J + 1] = Tmp;
        }
      }
    }
    // Process intersected child nodes
    for (int I = 0; I < HitCount; ++I) {
      int Lane = HitLanes[I];
      if (Node.ObjectCounts[Lane] > 0) {
        bool LaneHit = false;
        hit_record LeafRec = {};
        float LeafClosest = Closest;
        for (uint32_t Obj = 0; Obj < Node.ObjectCounts[Lane]; ++Obj) {
          uint32_t ObjIdx = State->GlobalObjectIndices[Node.IndexOffsets[Lane] + Obj];
          hit_record TempRec = {};
          if (HitSphereSoA(State->World, ObjIdx, Ray, TMin, LeafClosest, TempRec)) {
            LaneHit = true;
            LeafClosest = TempRec.T;
            LeafRec = TempRec;
          }
        }
        if (LaneHit) {
          HitAnything = true;
          Closest = LeafClosest;
          Rec = LeafRec;
        }
      }
      else {
        int32_t ChildNodeIdx = Node.ChildIndices[Lane];
        if (ChildNodeIdx >= 0) {
          Assert(StackPtr < 64);
          NodeStack[StackPtr++] = ChildNodeIdx;
        }
      }
    }
  }
  return HitAnything;
}
bool Scatter(const ray& RayIn, const material& Mat, const hit_record& Rec, random_state* R, DirectX::XMVECTOR& Attenuation, ray& Scattered) {
  switch (Mat.Type) {
  case material_type::MATERIAL_METAL: {
    DirectX::XMVECTOR Reflected = DirectX::XMVector3Reflect(RayIn.Direction, Rec.Normal);
    Reflected = DirectX::XMVector3Normalize(Reflected) + DirectX::XMVectorScale(RandUnitVector(R), Mat.Fuzz);
    DirectX::XMVECTOR InvDir = DirectX::XMVectorSet(1.0f, 1.0f, 1.0f, 1.0f) / Reflected;
    Scattered = ray{ Rec.Point, Reflected, InvDir };
    Attenuation = Mat.Albedo;
    return DirectX::XMVectorGetX(DirectX::XMVector3Dot(Scattered.Direction, Rec.Normal)) > 0;
  }
  case material_type::MATERIAL_LAMBERT: {
    DirectX::XMVECTOR ScatterDirection = Rec.Normal + RandUnitVector(R);
    if (DirectX::XMVectorGetX(DirectX::XMVector3LengthSq(ScatterDirection)) < 1e-6f)
      ScatterDirection = Rec.Normal;
    DirectX::XMVECTOR InvDir = DirectX::XMVectorSet(1.0f, 1.0f, 1.0f, 1.0f) / ScatterDirection;
    Scattered = ray{ Rec.Point, ScatterDirection, InvDir };
    Attenuation = Mat.Albedo;
    return true;
  }
  case material_type::MATERIAL_DIELECTRIC: {
    Attenuation = DirectX::XMVectorSet(1, 1, 1, 0);
    float Ri = Rec.FrontFace ? (1.0f / Mat.RefractiveIndex) : Mat.RefractiveIndex;
    DirectX::XMVECTOR UnitDirection = RayIn.Direction;
    float CosTheta = DirectX::XMVectorGetX(DirectX::XMVector3Dot(-UnitDirection, Rec.Normal));
    CosTheta = CosTheta > 1.0f ? 1.0f : CosTheta;
    float SinTheta = sqrtf(1.0f - CosTheta * CosTheta);
    bool CannotReflect = Ri * SinTheta > 1.0f;
    DirectX::XMVECTOR Direction;
    float R0 = (1.0f - Ri) / (1.0f + Ri); R0 = R0 * R0;
    float Reflectance = R0 + (1.0f - R0) * powf((1.0f - CosTheta), 5.0f);
    if (CannotReflect || Reflectance > RandF(R)) {
      Direction = DirectX::XMVector3Reflect(UnitDirection, Rec.Normal);
    }
    else {
      Direction = DirectX::XMVector3Refract(UnitDirection, Rec.Normal, Ri);
    }
    DirectX::XMVECTOR InvDir = DirectX::XMVectorSet(1.0f, 1.0f, 1.0f, 1.0f) / Direction;
    Scattered = ray{ Rec.Point, Direction, InvDir };
    return true;
  }
  }
  return false;
}
DirectX::XMVECTOR RayColorBVH(ray RayObj, app_state* State, random_state* R, int Depth) {
  DirectX::XMVECTOR Accumulated = White;
  hit_record Rec;
  for (int Bounce = 0; Bounce < Depth; Bounce++) {
    if (!HitBVH4(State, State->RootNodeBVH4, RayObj, 0.001f, FLT_MAX, Rec)) {
      DirectX::XMVECTOR Dir = DirectX::XMVector3Normalize(RayObj.Direction);
      float A = 0.5f * (DirectX::XMVectorGetY(Dir) + 1.0f);
      Accumulated *= (White * (1.0f - A) + Blue * A);
      break;
    }
    ray Scattered;
    DirectX::XMVECTOR Attenuation;
    if (!Scatter(RayObj, State->World.Materials[Rec.MatId], Rec, R, Attenuation, Scattered)) {
      return DirectX::XMVectorZero();
    }
    Attenuation = DirectX::XMVectorSetW(Attenuation, 1.0f);
    Accumulated *= Attenuation;
    RayObj = Scattered;
  }
  return Accumulated;
}
void GenWorld(sphere_soa* World, random_state* R) {
  material GroundMat{ DirectX::XMVectorSet(0.5f, 0.5f, 0.5f, 0), material_type::MATERIAL_LAMBERT };
  World->Add(DirectX::XMVectorSet(0.0f, -1000.0f, 0.0f, 0), 1000.0f, GroundMat);
  for (int A = -11; A < 11; A++) {
    for (int B = -11; B < 11; B++) {
      float ChooseMat = RandF(R);
      DirectX::XMVECTOR Center = DirectX::XMVectorSet(A + 0.9f * RandF(R), 0.2f, B + 0.9f * RandF(R), 0);
      DirectX::XMVECTOR Diff = Center - DirectX::XMVectorSet(4.0f, 0.2f, 0, 0);
      if (DirectX::XMVectorGetX(DirectX::XMVector3Length(Diff)) > 0.9f) {
        material SphereMat{};
        if (ChooseMat < 0.8f) {
          DirectX::XMVECTOR Albedo = RandVec3(R) * RandVec3(R);
          SphereMat = { Albedo, material_type::MATERIAL_LAMBERT };
        }
        else if (ChooseMat < 0.95f) {
          DirectX::XMVECTOR Albedo = RandVec3(R, 0.5f, 1.0f);
          float Fuzz = RandF(R, 0.0f, 0.5f);
          SphereMat = { Albedo, material_type::MATERIAL_METAL, Fuzz };
        }
        else {
          SphereMat = { White, material_type::MATERIAL_DIELECTRIC, 1.5f };
        }
        World->Add(Center, 0.2f, SphereMat);
      }
    }
  }
  material Material1{ DirectX::XMVectorSet(1,1,1,0), material_type::MATERIAL_DIELECTRIC, 1.5f };
  material Material2{ DirectX::XMVectorSet(0.4f, 0.2f, 0.1f, 0), material_type::MATERIAL_LAMBERT };
  material Material3{ DirectX::XMVectorSet(0.7f, 0.6f, 0.5f, 0), material_type::MATERIAL_METAL, 0.0f };
  World->Add(DirectX::XMVectorSet(0, 1, 0, 0), 1.0f, Material1);
  World->Add(DirectX::XMVectorSet(-4, 1, 0, 0), 1.0f, Material2);
  World->Add(DirectX::XMVectorSet(4, 1, 0, 0), 1.0f, Material3);
}
void InitializeCamera(camera& Cam) {
  Cam.ImageHeight = int(Cam.ImageWidth / Cam.AspectRatio);
  if (Cam.ImageHeight < 1) { Cam.ImageHeight = 1; }

  Cam.PixelSamplesScale = 1.0f / Cam.SamplesPerPixel;
  Cam.PreviewSamplesScale = 1.0f / Cam.PreviewSamplesPerPixel;
  Cam.Center = Cam.LookFrom;

  float Theta = DirectX::XMConvertToRadians(Cam.Vfov);
  float H = tanf(Theta / 2.0f);
  Cam.ViewportHeight = 2.0f * H * Cam.FocusDist;
  Cam.ViewportWidth = Cam.ViewportHeight * float(Cam.ImageWidth) / float(Cam.ImageHeight);

  Cam.W = DirectX::XMVector3Normalize(Cam.LookFrom - Cam.LookAt);
  Cam.U = DirectX::XMVector3Normalize(DirectX::XMVector3Cross(Cam.Vup, Cam.W));
  Cam.V = DirectX::XMVector3Cross(Cam.W, Cam.U);

  DirectX::XMVECTOR ViewportU = Cam.ViewportWidth * Cam.U;
  DirectX::XMVECTOR ViewportV = Cam.ViewportHeight * -Cam.V;

  Cam.PixelDeltaU = ViewportU / float(Cam.ImageWidth);
  Cam.PixelDeltaV = ViewportV / float(Cam.ImageHeight);

  DirectX::XMVECTOR ViewportUpperLeft = Cam.Center - (Cam.FocusDist * Cam.W) - ViewportU / 2.0f - ViewportV / 2.0f;
  Cam.Pixel00Loc = ViewportUpperLeft + 0.5f * (Cam.PixelDeltaU + Cam.PixelDeltaV);

  Cam.DefocusRadius = Cam.FocusDist * tanf(DirectX::XMConvertToRadians(Cam.DefocusAngle / 2.0f));
  Cam.DefocusDiskU = Cam.U * Cam.DefocusRadius;
  Cam.DefocusDiskV = Cam.V * Cam.DefocusRadius;
}

void MoveCamera(camera& Cam) {
  // Update WS
  Cam.Center = Cam.LookFrom;

  // Re-orthagonalize view vectors
  Cam.W = DirectX::XMVector3Normalize(Cam.LookFrom - Cam.LookAt);
  Cam.U = DirectX::XMVector3Normalize(DirectX::XMVector3Cross(Cam.Vup, Cam.W));
  Cam.V = DirectX::XMVector3Cross(Cam.W, Cam.U);

  // Re-orient viewports
  DirectX::XMVECTOR ViewportU = Cam.ViewportWidth * Cam.U;
  DirectX::XMVECTOR ViewportV = Cam.ViewportHeight * -Cam.V;

  // Reproject WS Grid
  Cam.PixelDeltaU = ViewportU / float(Cam.ImageWidth);
  Cam.PixelDeltaV = ViewportV / float(Cam.ImageHeight);

  // Shift Pixel Tracking
  DirectX::XMVECTOR ViewportUpperLeft = Cam.Center - (Cam.FocusDist * Cam.W) - ViewportU / 2.0f - ViewportV / 2.0f;
  Cam.Pixel00Loc = ViewportUpperLeft + 0.5f * (Cam.PixelDeltaU + Cam.PixelDeltaV);

  //Re-orient lens defocus blur
  Cam.DefocusDiskU = Cam.U * Cam.DefocusRadius;
  Cam.DefocusDiskV = Cam.V * Cam.DefocusRadius;
}


ray GetRay(camera& Cam, int I, int J, random_state* R) {
  float Rx = RandF(R) - 0.5f;
  float Ry = RandF(R) - 0.5f;
  DirectX::XMVECTOR PixelSample = Cam.Pixel00Loc + ((I + Rx) * Cam.PixelDeltaU) + ((J + Ry) * Cam.PixelDeltaV);
  DirectX::XMVECTOR RayOrigin;
  if (Cam.DefocusAngle <= 0) {
    RayOrigin = Cam.Center;
  }
  else {
    DirectX::XMVECTOR P = RandUnitDisc(R);
    RayOrigin = Cam.Center + (Cam.DefocusDiskU * DirectX::XMVectorGetX(P)) + (Cam.DefocusDiskV * DirectX::XMVectorGetY(P));
  }
  DirectX::XMVECTOR RayDir = DirectX::XMVector3Normalize(PixelSample - RayOrigin);
  DirectX::XMVECTOR InvDir = DirectX::XMVectorSet(1.0f, 1.0f, 1.0f, 1.0f) / RayDir; // Cache inverse tracking
  return ray{ RayOrigin, RayDir, InvDir };
}
void RenderRegionInternal(app_state* State, int XStart, int XEnd, int YStart, int YEnd, uint32_t* Buffer,
  int BufWidth, int BufHeight, int Spp, int MaxDepth, float SampleScale, uint32_t DestPitch,
  int BlockSizeX = 1, int BlockSizeY = 1) {
  camera& Cam = State->Cam;
  random_state R = { (uint32_t)(YStart * BufWidth + XStart + 1) };
  // Advance by the block size to sample at a much lower resolution
  for (int J = YStart; J < YEnd; J += BlockSizeY) {
    for (int I = XStart; I < XEnd; I += BlockSizeX) {
      DirectX::XMVECTOR PixelColor = DirectX::XMVectorZero();
      // Core ray tracing math
      for (int S = 0; S < Spp; S++) {
        ray RayObj = GetRay(Cam, I, J, &R);
        PixelColor += RayColorBVH(RayObj, State, &R, MaxDepth);
      }
      PixelColor = DirectX::XMVectorScale(PixelColor, SampleScale);
      uint32_t PackedColor = PackLinearColorToPixel(PixelColor, true, true);
      // Fill the BlockSizeX by BlockSizeY region directly in the hardware frame buffer
      int FillMaxY = (J + BlockSizeY < YEnd) ? J + BlockSizeY : YEnd;
      int FillMaxX = (I + BlockSizeX < XEnd) ? I + BlockSizeX : XEnd;
      for (int FillY = J; FillY < FillMaxY; ++FillY) {
        uint32_t* DestRow = (uint32_t*)((uint8_t*)Buffer + FillY * DestPitch);
        for (int FillX = I; FillX < FillMaxX; ++FillX) {
          DestRow[FillX] = PackedColor;
        }
      }
    }
  }
}
inline void Render(app_state* State, graphics_buffer* GraphicsBuffer) {
  camera& Cam = State->Cam;
  RenderRegionInternal(
    State, 0, GraphicsBuffer->Width, 0, GraphicsBuffer->Height, GraphicsBuffer->Data,
    GraphicsBuffer->Width, GraphicsBuffer->Height, Cam.SamplesPerPixel,
    Cam.MaxBounces, Cam.PixelSamplesScale, GraphicsBuffer->Pitch, 1, 1
  );
}
inline void RenderPreview(app_state* State, graphics_buffer* GraphicsBuffer) {
  camera& Cam = State->Cam;
  RenderRegionInternal(
    State, 0, GraphicsBuffer->Width, 0, GraphicsBuffer->Height, GraphicsBuffer->Data,
    GraphicsBuffer->Width, GraphicsBuffer->Height, Cam.PreviewSamplesPerPixel,
    Cam.PreviewBounces, Cam.PreviewSamplesScale, GraphicsBuffer->Pitch, Cam.PreviewScale, Cam.PreviewScale
  );
}
void UpdateLookDirection(camera& Cam) {
  float YawRad = DirectX::XMConvertToRadians(Cam.Yaw);
  float PitchRad = DirectX::XMConvertToRadians(Cam.Pitch);
  DirectX::XMVECTOR Dir = DirectX::XMVectorSet(
    cosf(YawRad) * cosf(PitchRad),
    sinf(PitchRad),
    cosf(YawRad) * cosf(PitchRad),
    0.0f);
  Cam.LookAt = Cam.LookFrom + DirectX::XMVector3Normalize(Dir);
}

void ParseConfigArgs(app_configuration* Config) {
  int WindowWidth = 1280;
  int WindowHeight = 720;

  const char* WidthFlag = strstr(Config->ArgBuffer, "-w");
  if (WidthFlag) { sscanf_s(WidthFlag, "-w %d", &WindowWidth); }
  const char* HeightFlag = strstr(Config->ArgBuffer, "-h");
  if (HeightFlag) { sscanf_s(HeightFlag, "-h %d", &WindowHeight); }

  Config->Width = WindowWidth;
  Config->Height = WindowHeight;
}

APP_CONFIG(Config, Services) {
#if HEADLESS_BUILD
  Services->PlatformPrintConsole("SOA BVH RayTracer (Based on RTIOW book 1):\n");
  Services->PlatformPrintConsole(" -wWidth -hHeight -oFilename\n");
  Services->PlatformPrintConsole(" -Defaults: -w1280 -h720 -oscreenshot_##\n");
  Services->PlatformReadConsole(Config->ArgBuffer, ARG_BUFFER_SIZE);
  ParseConfigArgs(Config);
#else
  Services->PlatformReadArgs(Config->ArgBuffer, ARG_BUFFER_SIZE);
  ParseConfigArgs(Config);
  Config->WindowTitle = "SOA BVH RayTracer";
#endif
  uint32_t OuputSize = Config->Width * Config->Height * sizeof(uint32_t);
  uint64_t PermSize = sizeof(app_state) +
    (MAX_SPHERES * sizeof(DirectX::XMVECTOR)) +
    (MAX_SPHERES * sizeof(float)) +
    (MAX_SPHERES * sizeof(material)) +
    (MAX_SPHERES * sizeof(uint32_t)) +
    (MAX_SPHERES * sizeof(bvh4_node)) +
    64;
#if HEADLESS_BUILD
  uint32_t PixelBufferSize = Config->Width * Config->Height * sizeof(uint32_t);
  PermSize += PixelBufferSize;
#endif
  uint64_t BTSSize = (MAX_SPHERES * 2) * sizeof(binary_node);
  uint64_t SSSize = sizeof(bmp_header) + OuputSize;
  uint64_t TransSize = (SSSize > BTSSize ? SSSize : BTSSize) + 256;
  Config->PermanentStorageSize = PermSize;
  Config->TransientStorageSize = TransSize;
}

void ParseInitArgs(app_memory* Memory) {
  app_configuration Config = Memory->Config;
  app_state* State = (app_state*)Memory->PermanentStorage;

  char FN[MAX_FILENAME_SIZE] = { 0 };
  const char* FileFlag = strstr(Config.ArgBuffer, "-o");
  if (FileFlag) { sscanf_s(FileFlag, "-o %255s", FN, (unsigned int)sizeof(FN)); }
  if (FN[0] != '\0') { strncpy_s(State->Filename, sizeof(State->Filename), FN, _TRUNCATE); }
  else { strncpy_s(State->Filename, sizeof(State->Filename), "screenshot", _TRUNCATE); }
}

APP_INITIALIZATION(Memory) {
  ParseInitArgs(Memory);
  app_configuration Config = Memory->Config;
  memory_arena PermanentArena;
  PushArenaInit(&PermanentArena, Memory->PermanentStorage, Memory->PermanentStorageSize);
  app_state* State = PushStruct(&PermanentArena, app_state);

  // Core camera setup configuration parameters
  State->Cam.AspectRatio = 16.0f / 9.0f;
  State->Cam.ImageWidth = Config.Width;
  State->Cam.SamplesPerPixel = 15;
  State->Cam.MaxBounces = 15;

  // Preview Setup
  State->Cam.PreviewScale = 2;
  State->Cam.PreviewSamplesPerPixel = 1;
  State->Cam.PreviewBounces = 3;

  // positions etc.
  State->Cam.Vfov = 20;
  State->Cam.LookFrom = DirectX::XMVectorSet(13, 2, 3, 0);
  State->Cam.LookAt = DirectX::XMVectorZero();
  State->Cam.Vup = DirectX::XMVectorSet(0, 1, 0, 0);
  State->Cam.DefocusAngle = 0.6f;
  State->Cam.FocusDist = 10.0f;
  State->Cam.MoveSpeed = 1;
  InitializeCamera(State->Cam);

  // allocate world
  State->World.MaxCount = MAX_SPHERES;
  State->World.Count = 0;
  State->World.Centers = PushArray(&PermanentArena, MAX_SPHERES, DirectX::XMVECTOR);
  State->World.Radii = PushArray(&PermanentArena, MAX_SPHERES, float);
  State->World.Materials = PushArray(&PermanentArena, MAX_SPHERES, material);
  random_state InitRand = { 42 };
  GenWorld(&State->World, &InitRand);
  State->GlobalObjectIndices = PushArray(&PermanentArena, State->World.Count, uint32_t);
  for (uint32_t I = 0; I < State->World.Count; ++I) { State->GlobalObjectIndices[I] = I; }
  // Permenant space
  State->Bvh4NodeMaxCount = State->World.Count;
  State->Bvh4NodeCount = 0;
  State->Bvh4Nodes = PushArray(&PermanentArena, State->Bvh4NodeMaxCount, bvh4_node);

  // Temp memory for building bst and staging img print
  memory_arena TransientArena;
  PushArenaInit(&TransientArena, Memory->TransientStorage, Memory->TransientStorageSize);
  State->BinaryNodeMaxCount = State->World.Count * 2;
  State->BinaryNodeCount = 0;
  State->BinaryNodes = PushArray(&TransientArena, State->BinaryNodeMaxCount, binary_node);

  // build binary tree in transient storage
  int TemporaryRootBin = BuildBinaryTree(State, State->World, 0, State->World.Count);
  // compress and vectorize bvh
  State->RootNodeBVH4 = CollapseBinaryToBVH4(State, TemporaryRootBin);
  State->FullRender = false;
  State->PrintCount = 0;

#if HEADLESS_BUILD
  State->HeadlessGraphicsBuffer.Width = Config.Width;
  State->HeadlessGraphicsBuffer.Height = Config.Height;
  State->HeadlessGraphicsBuffer.Pitch = Config.Width * sizeof(uint32_t);
  State->HeadlessGraphicsBuffer.Size = State->HeadlessGraphicsBuffer.Pitch * State->HeadlessGraphicsBuffer.Height;
  State->HeadlessGraphicsBuffer.BytesPerPixel = sizeof(uint32_t);

  uint32_t PixelCount = Config.Width * Config.Height;
  State->HeadlessGraphicsBuffer.Data = PushArray(&PermanentArena, PixelCount, uint32_t);

  char LogBuffer[256];
  snprintf(LogBuffer, sizeof(LogBuffer),
    "Initialized world: %u spheres, %dx%d, %d Samples per pixel, %d Max Depth\n",
    State->World.Count, State->HeadlessGraphicsBuffer.Width,
    State->HeadlessGraphicsBuffer.Height, State->Cam.SamplesPerPixel, State->Cam.MaxBounces
  );
  Memory->PlatformServices.PlatformPrintConsole(LogBuffer);
#endif

  // Explicitly notify engine environment state initialization complete
  Memory->IsInitialized = true;
}
#if DEV_BUILD
APP_RELOAD_DLL(Memory) {
  // Reload scratchpad

  // PREV VALUES:
  // State->Cam.SamplesPerPixel = 15;
  // State->Cam.MaxBounces = 15;
  app_state* State = (app_state*)Memory->PermanentStorage;
  State->Cam.SamplesPerPixel = 5;
  State->Cam.MaxBounces = 5;

  // PREV VALUES:
  // State->Cam.PreviewScale = 2;
  // State->Cam.PreviewSamplesPerPixel = 1;
  // State->Cam.PreviewBounces = 3;
  State->Cam.PreviewScale = 2;
  State->Cam.PreviewBounces = 1;
  State->World.Materials[0] = { DirectX::XMVectorSet(0, 0.8f, 0, 0), material_type::MATERIAL_LAMBERT };
  
  
  State->FullRender = false;
  State->CompletedRender = false;
  InitializeCamera(State->Cam);
}

#endif
inline bool32 GetKeyDown(platform_input* Input, uint8_t KeyCode) {
  return Input->Keys[KeyCode].IsDown && !Input->Keys[KeyCode].WasDown;
}
inline bool32 GetKey(platform_input* Input, uint8_t KeyCode) {
  return Input->Keys[KeyCode].IsDown;
}
APP_UPDATE_AND_RENDER(Memory, Input, GraphicsBuffer) {
  app_state* State = (app_state*)Memory->PermanentStorage;
  camera& Cam = State->Cam;
  if (!State->FullRender) {
    bool Moved = false;
    float Velocity = Cam.MoveSpeed * Input->DeltaTime;
    DirectX::XMVECTOR Forward = DirectX::XMVector3Normalize(Cam.LookAt - Cam.LookFrom);
    DirectX::XMVECTOR Right = DirectX::XMVector3Normalize(DirectX::XMVector3Cross(Forward, Cam.Vup));
    if (GetKey(Input, 'W')) { Cam.LookFrom += Forward * Velocity; Moved = true; }
    if (GetKey(Input, 'S')) { Cam.LookFrom -= Forward * Velocity; Moved = true; }
    if (GetKey(Input, 'A')) { Cam.LookFrom -= Right * Velocity; Moved = true; }
    if (GetKey(Input, 'D')) { Cam.LookFrom += Right * Velocity; Moved = true; }
    if (GetKey(Input, 'E')) { Cam.LookFrom += Cam.Vup * Velocity; Moved = true; }
    if (GetKey(Input, 'Q')) { Cam.LookFrom -= Cam.Vup * Velocity; Moved = true; }
    if (Moved) {
      MoveCamera(Cam);
      State->CompletedRender = false;
      Moved = false;
    }
    if (!State->CompletedRender) {
      RenderPreview(State, GraphicsBuffer);
      State->CompletedRender = true;
    }
    else if (GetKeyDown(Input, 'R')) {
      State->FullRender = true;
      State->CompletedRender = false;
    }
    else if (GetKeyDown(Input, 'L')) {
      SaveGraphicsBuffer(Memory, GraphicsBuffer, image_file::BMP);
    }
  }
  else {
    if (!State->CompletedRender) {
      Render(State, GraphicsBuffer);
      State->CompletedRender = true;
    }
    else if (GetKeyDown(Input, 'R')) {
      State->FullRender = false;
      State->CompletedRender = false;
    }
    else if (GetKeyDown(Input, 'L')) {
      SaveGraphicsBuffer(Memory, GraphicsBuffer, image_file::BMP);
    }
  }
}

#if HEADLESS_BUILD
APP_RUN_HEADLESS(Memory) {
  app_state* State = (app_state*)Memory->PermanentStorage;

  Memory->PlatformServices.PlatformPrintConsole("Headless Engine starting render capture pass...\n");

  Render(State, &State->HeadlessGraphicsBuffer);
  State->CompletedRender = true;

  Memory->PlatformServices.PlatformPrintConsole("Render sequence completed. Exporting asset to disk...\n");

  SaveGraphicsBuffer(Memory, &State->HeadlessGraphicsBuffer, image_file::BMP);

  Memory->PlatformServices.PlatformPrintConsole("Headless file pipeline task successful.\n");
}
#endif