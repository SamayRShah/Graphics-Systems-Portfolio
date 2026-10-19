#pragma once
#include <cstdint>

// #define internal static
// #define local_persist static
// #define global_variable static

#if DEV_BUILD
#ifndef DLL_NAME
#define DLL_NAME "app.dll"
#endif
#ifndef DLL_NAME_TEMP
#define DLL_NAME_TEMP "app_temp.dll"
#endif
#endif

#define STR_HELPER(x) #x
#define STR(x) STR_HELPER(x)

#define bool32 int32_t
#define Pi32 3.14159265359f
#define ARG_BUFFER_SIZE 256

#define KILO_BYTES(Value) ((Value)*1024LL)
#define MEGA_BYTES(Value) (KILO_BYTES(Value)*1024LL)

#define ArrayCount(Array) (sizeof(Array) / sizeof((Array)[0]))
#define APPEND_STR_UNSAFE(str, dest) { const char* p = str; while(*p) { *Dest++ = *p++; } }
#define APPEND_CHAR_UNSAFE(c, dest) { *dest++ = c; }

#if DEV_BUILD
#define Assert(Expression) if(!(Expression)) { *(int*)0 = 0; } 
#else
#define Assert(Expression)
#endif

inline uint32_t
SafeTruncateUint64(uint64_t value) {
  Assert(value <= 0xFFFFFFFF);
  uint32_t Result = (uint32_t)value;
  return Result;
}

struct platform_file_read_result {
  void* Data;
  uint32_t Size;
};

#define PLATFORM_FILE_READ(name) platform_file_read_result name(char* Filename)
typedef platform_file_read_result(platform_read_entire_file_func)(char* Filename);
#define PLATFORM_WRITE_FILE(name) name(char* Filename, void* Data, uint32_t DataSize)
typedef bool32(platform_write_entire_file_func)(char* Filename, void* Data, uint32_t DataSize);
#define PLATFORM_FREE_FILE_MEMORY(name) void name(platform_free_file_memory_func)(void* Data)
typedef void (platform_free_file_memory_func)(void* Data);
#define PLATFORM_PRINT_CONSOLE(name) void name(char* Msg)
typedef void (platform_print_console_func)(char* Msg);
#define PLATFORM_READ_CONSOLE(name) bool32 name(char* DstBuffer, uint32_t DstSize)
typedef bool32(platform_read_console_func)(char*, uint32_t);
#define PLATFORM_READ_ARGS(name) bool32 name(char* DstBuffer, uint32_t DstSize)
typedef bool32(platform_read_args_func)(char*, uint32_t);

struct platform_services {
  platform_read_entire_file_func* PlatformReadEntireFile;
  platform_write_entire_file_func* PlatformWriteEntireFile;
  platform_free_file_memory_func* PlatformFreeFileMemory;
  platform_print_console_func* PlatformPrintConsole;
  platform_read_console_func* PlatformReadConsole;
  platform_read_args_func* PlatformReadArgs;
};

struct platform_button_state {
  bool32 IsDown;
  bool32 WasDown;
};

struct platform_input {
  platform_button_state Keys[256];
  platform_button_state MouseButtons[5];
  int32_t MouseX;
  int32_t MouseY;
  float DeltaTime;
};

struct app_configuration {
  int32_t Width;
  int32_t Height;
  uint64_t PermanentStorageSize;
  uint64_t TransientStorageSize;
  char* WindowTitle;

  uint32_t ArgBufferSize;
  char* ArgBuffer;
};

struct app_memory {
  app_configuration Config;
  platform_services PlatformServices;
  void* PermanentStorage;
  uint64_t PermanentStorageSize;
  void* TransientStorage;
  uint64_t TransientStorageSize;
  bool32 IsInitialized;
};

struct graphics_buffer {
  uint32_t Size;
  uint32_t* Data;
  uint32_t Width;
  uint32_t Height;
  uint32_t Pitch;
  uint32_t BytesPerPixel;
};

#define APP_CONFIG(config, services) extern "C" __declspec(dllexport) void AppConfigure(app_configuration* config, platform_services* services)
typedef void (*app_configuration_func)(app_configuration*, platform_services*);
#define APP_INITIALIZATION(mem) extern "C" __declspec(dllexport) void AppInitialize(app_memory* mem)
typedef void (*app_initialization_func)(app_memory*);
#define APP_RELOAD_DLL(mem) extern "C" __declspec(dllexport) void AppReloadDll(app_memory* mem)
typedef void (*app_reload_dll_func)(app_memory*);
#define APP_UPDATE_AND_RENDER(mem, input, buffer) extern "C" __declspec(dllexport) void AppUpdateAndRender(app_memory* mem, platform_input* input, graphics_buffer* buffer)
typedef void (*app_update_and_render_func)(app_memory*, platform_input*, graphics_buffer*);
#define APP_RUN_HEADLESS(mem) extern "C" __declspec(dllexport) void AppRunHeadless(app_memory* mem)
typedef void (*app_run_headless_func)(app_memory*);