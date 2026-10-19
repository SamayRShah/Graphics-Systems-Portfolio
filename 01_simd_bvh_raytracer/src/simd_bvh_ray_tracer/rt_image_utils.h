#pragma once
#include <cstring>

enum struct image_file : uint32_t { P6, BMP };

#pragma pack(push, 1)
struct bmp_header {
  uint16_t FileType;       // Always 0x4D42 ('BM')
  uint32_t FileSize;       // Total size of the file
  uint16_t Reserved1;      // Always 0
  uint16_t Reserved2;      // Always 0
  uint32_t BitmapOffset;   // Offset to the start of pixel data (54)

  uint32_t Size;           // Size of this info header (40)
  int32_t  Width;          // Width of bitmap in pixels
  int32_t  Height;         // NEGATIVE value forces top-down row layout
  uint16_t Planes;         // Always 1
  uint16_t BitsPerPixel;   // 32 bits (BGRA matches your D3D layout!)
  uint32_t Compression;    // 0
  uint32_t SizeOfBitmap;   // Size of the pixel array data
  int32_t  HorzResolution; // 0
  int32_t  VertResolution; // 0
  uint32_t ColorsUsed;     // 0
  uint32_t ColorsImportant;// 0
};
#pragma pack(pop)

static void
SaveGraphicsBufferP6(app_memory* AppMemory, graphics_buffer* GraphicsBuffer) {
  uint8_t* BufferBase = (uint8_t*)AppMemory->TransientStorage;
  uint8_t* Dest = BufferBase;

  int HeaderLength = snprintf(
    (char*)Dest, AppMemory->TransientStorageSize, "P6\n%d %d\n255\n",
    GraphicsBuffer->Width, GraphicsBuffer->Height
  );

  // Advance Dest by the actual number of string characters written safely
  Dest += HeaderLength;
  uint8_t* Row = (uint8_t*)GraphicsBuffer->Data;
  for (uint32_t Y = 0; Y < GraphicsBuffer->Height; ++Y) {
    uint32_t* Pixel = (uint32_t*)Row;
    for (uint32_t X = 0; X < GraphicsBuffer->Width; ++X) {
      uint32_t Color = *Pixel++;

      uint8_t Red = (uint8_t)((Color >> 16) & 0xFF);
      uint8_t Green = (uint8_t)((Color >> 8) & 0xFF);
      uint8_t Blue = (uint8_t)((Color) & 0xFF);

      *Dest++ = Red;
      *Dest++ = Green;
      *Dest++ = Blue;
    }
    Row += GraphicsBuffer->Pitch;
  }

  uint32_t DataSize = (uint32_t)(Dest - BufferBase);

  app_state* State = (app_state*)AppMemory->PermanentStorage;
  char Filename[256];
  snprintf(Filename, 256, "screenshot_%02d.ppm", State->PrintCount++);

  AppMemory->PlatformServices.PlatformWriteEntireFile(Filename, BufferBase, DataSize);
}

static void
SaveGraphicsBufferBitmap(app_memory* AppMemory, graphics_buffer* GraphicsBuffer) {
  uint8_t* BufferBase = (uint8_t*)AppMemory->TransientStorage;

  uint32_t TotalFileSize = sizeof(bmp_header) + GraphicsBuffer->Size;
  Assert(TotalFileSize <= AppMemory->TransientStorageSize);

  bmp_header* Header = (bmp_header*)BufferBase;
  Header->FileType = 0x4D42;
  Header->FileSize = TotalFileSize;
  Header->Reserved1 = 0;
  Header->Reserved2 = 0;
  Header->BitmapOffset = sizeof(bmp_header);
  Header->Size = 40;
  Header->Width = GraphicsBuffer->Width;
  Header->Height = -(int32_t)GraphicsBuffer->Height;
  Header->Planes = 1;
  Header->BitsPerPixel = 32;
  Header->Compression = 0;
  Header->SizeOfBitmap = GraphicsBuffer->Size;
  Header->HorzResolution = 0;
  Header->VertResolution = 0;
  Header->ColorsUsed = 0;
  Header->ColorsImportant = 0;

  uint8_t* Dest = BufferBase + sizeof(bmp_header);
  memcpy(Dest, GraphicsBuffer->Data, GraphicsBuffer->Size);

  app_state* State = (app_state*)AppMemory->PermanentStorage;
  char Filename[256];
  snprintf(Filename, 256, "%s_%02d.bmp", State->Filename, State->PrintCount);

  AppMemory->PlatformServices.PlatformWriteEntireFile(Filename, BufferBase, Header->FileSize);
  State->PrintCount++;
}

static void SaveGraphicsBuffer(app_memory* AppMemory, graphics_buffer* GraphicsBuffer, image_file Type) {
  switch (Type) {
  case image_file::P6: { SaveGraphicsBufferP6(AppMemory, GraphicsBuffer); } break;
  case image_file::BMP: { SaveGraphicsBufferBitmap(AppMemory, GraphicsBuffer); } break;
  default: return;
  }
}