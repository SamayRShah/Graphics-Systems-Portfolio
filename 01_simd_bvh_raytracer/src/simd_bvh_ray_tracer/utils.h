#pragma once
#include <DirectXMath.h>

using namespace DirectX;

// memory management
struct memory_arena {
  uint8_t* Base;
  size_t Size;
  size_t Used;
};

inline void PushArenaInit(memory_arena* Arena, void* Base, size_t Size) {
  Arena->Base = (uint8_t*)Base;
  Arena->Size = Size;
  Arena->Used = 0;
}

#define PushStruct(Arena, Type) (Type*)PushSize(Arena, sizeof(Type))
#define PushArray(Arena, Count, Type) (Type*)PushSize(Arena, (Count) * sizeof(Type))

inline void* PushSize(memory_arena* Arena, size_t Size) {
  // 8-byte alignment alignment constraint adjustment loop
  size_t Alignment = 8;
  size_t AlignedSize = (Size + (Alignment - 1)) & ~(Alignment - 1);
  Assert((Arena->Used + AlignedSize) <= Arena->Size);
  void* Result = Arena->Base + Arena->Used;
  Arena->Used += AlignedSize;
  return Result;
}

// Random
struct random_state { uint32_t State; };
inline float RandF(random_state* Rand) {
#if 0
  // OLD LCG Implementation
  Rand->State = Rand->State * 1664525u + 1013904223u;
  return (float)(Rand->State >> 9) / 8388608.0f;
#else
  // XOR SHIFT
  uint32_t x = Rand->State;

  // 32-bit Xorshift state update
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  Rand->State = x;

  // Drop 8 bits to get a perfect 24-bit integer (no float rounding bias)
  // Multiply by 1 / 2^24 to get [0.0f, 1.0f) range
  return (float)(x >> 8) * 0x1p-24f;
#endif
}
inline float RandF(random_state* Rand, float min, float max) {
  return min + (max - min) * RandF(Rand);
}
inline DirectX::XMVECTOR RandVec3(random_state* R) {
  return DirectX::XMVectorSet(RandF(R, -1, 1), RandF(R, -1, 1), RandF(R, -1, 1), 0);
}
inline DirectX::XMVECTOR RandVec3(random_state* R, float min, float max) {
  return DirectX::XMVectorSet(RandF(R, min, max), RandF(R, min, max), RandF(R, min, max), 0.0f);
}
inline DirectX::XMVECTOR RandUnitVector(random_state* R) {
  while (true) {
    auto v = RandVec3(R);
    float lenSq = DirectX::XMVectorGetX(DirectX::XMVector3LengthSq(v));
    if (lenSq > 1e-30f) return DirectX::XMVector3Normalize(v);
  }
}
inline DirectX::XMVECTOR RandUnitDisc(random_state* R) {
  while (true) {
    auto v = DirectX::XMVectorSet(RandF(R, -1, 1), RandF(R, -1, 1), 0, 0);
    if (DirectX::XMVectorGetX(DirectX::XMVector3LengthSq(v)) < 1.0f) return v;
  }
}

inline uint32_t PackLinearColorToPixel(DirectX::XMVECTOR Color, bool ApplyGamma = true, bool ToBGRA = true) {
  __m128 Zero = _mm_setzero_ps();
  __m128 One = _mm_set1_ps(1.0f);

  Color = _mm_min_ps(_mm_max_ps(Color, Zero), One);

  if (ApplyGamma) { Color = _mm_sqrt_ps(Color); }
  if (ToBGRA) { Color = _mm_shuffle_ps(Color, Color, _MM_SHUFFLE(3, 0, 1, 2)); }

  Color = _mm_mul_ps(Color, _mm_set1_ps(255.0f));

  __m128i ColorInt = _mm_cvtps_epi32(Color);
  __m128i Packed16 = _mm_packus_epi32(ColorInt, _mm_setzero_si128());
  __m128i Packed8 = _mm_packus_epi16(Packed16, _mm_setzero_si128());

  return (uint32_t)_mm_cvtsi128_si32(Packed8);
}
