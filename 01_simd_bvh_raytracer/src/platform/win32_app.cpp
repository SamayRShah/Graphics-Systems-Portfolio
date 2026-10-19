#include "platform_interface.h"
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_3.h>
#include <stdio.h>

#define WIN32_STATE_FILENAME_COUNT MAX_PATH

#if !DEV_BUILD
extern "C" {
  void AppConfigure(app_configuration* Config, platform_services* Services);
  void AppInitialize(app_memory* Memory);
#if HEADLESS_BUILD
  void AppRunHeadless(app_memory* Memory);
#else
  void AppUpdateAndRender(app_memory* Memory, platform_input* Input, graphics_buffer* Buffer);
#endif
}
#else
struct win32_engine_code {
  HMODULE Library;
  FILETIME LastWriteTime;
  app_configuration_func Configure;
  app_initialization_func Initialize;
  app_reload_dll_func ReloadDll;
#if HEADLESS_BUILD
  app_run_headless_func RunHeadless;
#else
  app_update_and_render_func UpdateAndRender;
#endif
  bool32 IsValid;
};
#endif

struct win32_state {
  uint64_t TotalSize;
  void* MasterMemoryBlock;
#if DEV_BUILD
  char EXEFilename[WIN32_STATE_FILENAME_COUNT];
  char* OnePastLastEXEFilenameSlash;
#endif
};

namespace {
  static bool32 g_Running = false;
  static IDXGISwapChain1* D3DSwapChain = nullptr;
  static ID3D11Device* D3DDevice = nullptr;
  static ID3D11DeviceContext* D3DContext = nullptr;
  static ID3D11Texture2D* D3DBackBuffer = nullptr;
  static ID3D11Texture2D* TransferTexture = nullptr;
}

PLATFORM_PRINT_CONSOLE(Win32PrintToConsole) {
  HANDLE HConsole = GetStdHandle(STD_OUTPUT_HANDLE);
  if (HConsole != INVALID_HANDLE_VALUE && Msg) {
    // Calculate string length manually or via a helper to avoid standard library string functions
    DWORD Length = 0;
    while (Msg[Length]) {
      Length++;
    }
    DWORD BytesWritten;
    WriteConsoleA(HConsole, Msg, Length, &BytesWritten, NULL);
  }
}

void Win32FreeFileMemory(void* Memory) {
  if (Memory) { VirtualFree(Memory, 0, MEM_RELEASE); }
}

platform_file_read_result Win32ReadEntireFile(char* Filename) {
  platform_file_read_result Result = {};

  HANDLE FileHandle = CreateFileA(Filename, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_ALWAYS, 0, 0);
  if (FileHandle != INVALID_HANDLE_VALUE) {
    LARGE_INTEGER FileSize;
    if (GetFileSizeEx(FileHandle, &FileSize)) {
      uint32_t FileSize32 = SafeTruncateUint64(FileSize.QuadPart);
      Result.Data = VirtualAlloc(0, FileSize32, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
      if (Result.Size) {
        DWORD BytesRead;
        if (ReadFile(FileHandle, Result.Data, FileSize32, &BytesRead, 0) &&
          (FileSize32 == BytesRead)
          ) {
          Result.Size = FileSize32;
        }
        else {
          Win32FreeFileMemory(Result.Data);
          Result.Data = 0; Result.Size = 0;
        }
      }
      else {
        // fail alloc
      }
    }
    else {
      // fail get file size
    }
    CloseHandle(FileHandle);
  }
  else {
    // fail open file
  }
  return Result;
}

bool32 Win32WriteEntireFile(char* Filename, void* Memory, uint32_t MemorySize) {
  bool32 Result = false;

  HANDLE FileHandle = CreateFileA(Filename, GENERIC_WRITE, 0, 0, CREATE_ALWAYS, 0, 0);
  if (FileHandle != INVALID_HANDLE_VALUE) {
    DWORD BytesWritten;
    if (WriteFile(FileHandle, Memory, (DWORD)MemorySize, &BytesWritten, 0)) {
      Result = (BytesWritten == MemorySize);
    }
    else {
      // fail write
    }
    CloseHandle(FileHandle);
  }
  else {
    // fail open
  }
  return Result;
}

#if DEV_BUILD
static void
CatStrings(
  size_t SourceACount, char* SourceA,
  size_t SourceBCount, char* SourceB,
  size_t DestCount, char* Dest
) {
  for (int i = 0; i < SourceACount; ++i) { *Dest++ = *SourceA++; }
  for (int j = 0; j < SourceBCount; ++j) { *Dest++ = *SourceB++; }
  *Dest++ = 0;
}

static int
StringLength(char* String) {
  int Count = 0;
  while (*String++) { ++Count; }
  return Count;
}

static void
Win32GetEXEFilename(win32_state* Win32State) {
  GetModuleFileNameA(0, Win32State->EXEFilename, sizeof(Win32State->EXEFilename));
  Win32State->OnePastLastEXEFilenameSlash = Win32State->EXEFilename;
  for (char* Scan = Win32State->EXEFilename; *Scan; ++Scan) {
    if (*Scan == '\\') {
      Win32State->OnePastLastEXEFilenameSlash = Scan + 1;
    }
  }
}

static void Win32BuildEXEPathFilename(win32_state* State, char* Filename, int DestCount, char* Dest) {
  CatStrings(
    State->OnePastLastEXEFilenameSlash - State->EXEFilename, State->EXEFilename,
    StringLength(Filename), Filename, DestCount, Dest
  );
}

inline FILETIME Win32GetLastWriteTime(char* Filename) {
  FILETIME LastWriteTime = {};
  WIN32_FILE_ATTRIBUTE_DATA Data;
  if (GetFileAttributesExA(Filename, GetFileExInfoStandard, &Data)) {
    LastWriteTime = Data.ftLastWriteTime;
  }
  return LastWriteTime;
}

static win32_engine_code Win32LoadEngineCode(char* SourceDLL, char* TempDLL) {
  win32_engine_code Result = {};
  Result.LastWriteTime = Win32GetLastWriteTime(SourceDLL);

  // Copy the engine DLL to a temp file before loading to unlock the original file for re-compilation
  CopyFileA(SourceDLL, TempDLL, FALSE);
  Result.Library = LoadLibraryA(TempDLL);

  if (Result.Library) {
    Result.Configure = (app_configuration_func)GetProcAddress(Result.Library, "AppConfigure");
    Result.Initialize = (app_initialization_func)GetProcAddress(Result.Library, "AppInitialize");
    Result.ReloadDll = (app_reload_dll_func)GetProcAddress(Result.Library, "AppReloadDll");
#if HEADLESS_BUILD
    Result.RunHeadless = (app_run_headless_func)GetProcAddress(Result.Library, "AppRunHeadless");
    Result.IsValid = (Result.Initialize && Result.RunHeadless && Result.Configure);
#else
    Result.UpdateAndRender = (app_update_and_render_func)GetProcAddress(Result.Library, "AppUpdateAndRender");
    Result.IsValid = (Result.Initialize && Result.UpdateAndRender && Result.Configure);
#endif
  }

  if (!Result.IsValid) {
    if (Result.Library) FreeLibrary(Result.Library);
    Result.Library = nullptr;
  }
  return Result;
}

static void Win32UnloadEngineCode(win32_engine_code* EngineCode) {
  if (EngineCode->Library) {
    FreeLibrary(EngineCode->Library);
  }
  EngineCode->Library = nullptr;
  EngineCode->Configure = nullptr;
  EngineCode->Initialize = nullptr;
  EngineCode->ReloadDll = nullptr;
#if HEADLESS_BUILD
  EngineCode->RunHeadless = nullptr;
#else
  EngineCode->UpdateAndRender = nullptr;
#endif
  EngineCode->IsValid = false;
}
#endif

PLATFORM_READ_ARGS(Win32ReadArgs) {
  if (!DstBuffer || DstSize == 0) { return false; }
  char* RawCmdLine = GetCommandLineA();
  if (!RawCmdLine) { return false; }
  char* Args = RawCmdLine;
  // exclude exe path using '"'
  if (*Args == '"') {
    Args++;
    while (*Args && *Args != '"') { Args++; }
    if (*Args == '"') { Args++; }
  }
  // otherwise skip to space after path
  else { while (*Args && *Args != ' ' && *Args != '\t') { Args++; } }
  // trim ws
  while (*Args == ' ' || *Args == '\t') { Args++; }
  if (strncpy_s(DstBuffer, (size_t)DstSize, Args, _TRUNCATE) != 0) { return false; }
  return true;
}

PLATFORM_READ_CONSOLE(Win32ConsoleReadLine) {
  if (!DstBuffer || DstSize == 0) { return false; }
  HANDLE hStdin = GetStdHandle(STD_INPUT_HANDLE);
  if (hStdin == INVALID_HANDLE_VALUE || hStdin == NULL) { return false; }
  // Read input data
  DWORD BytesRead;
  if (!ReadFile(hStdin, DstBuffer, DstSize - 1, &BytesRead, NULL)) { return false; }
  DstBuffer[BytesRead] = '\0';
  // clean up Newlines
  while (BytesRead > 0 && (DstBuffer[BytesRead - 1] == '\n' || DstBuffer[BytesRead - 1] == '\r')) {
    BytesRead--;
    DstBuffer[BytesRead] = '\0';
  }
  return true;
}

#if !HEADLESS_BUILD
LRESULT CALLBACK Win32MainCallback(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp) {
  platform_input* ActiveInput = (platform_input*)GetWindowLongPtrA(hWnd, GWLP_USERDATA);
  switch (msg) {
  case WM_KEYDOWN: {
    if (wp == VK_ESCAPE) { g_Running = false; return 0; }
    else if (wp < 256) {
      if (ActiveInput->Keys[wp].IsDown) { ActiveInput->Keys[wp].WasDown = true; }
      ActiveInput->Keys[wp].IsDown = true;
    }
    return 0;
  }
  case WM_KEYUP: {
    if (wp < 256) {
      ActiveInput->Keys[wp].WasDown = false;
      ActiveInput->Keys[wp].IsDown = false;
    }
    return 0;
  }
  case WM_MOUSEMOVE: { ActiveInput->MouseX = LOWORD(lp); ActiveInput->MouseY = HIWORD(lp); return 0; }
  case WM_CLOSE: { g_Running = false; return 0; }
  case WM_DESTROY: { g_Running = false; return 0; }
  }
  return DefWindowProc(hWnd, msg, wp, lp);
}

static bool32 Win32InitD3D(HWND hWnd, uint32_t Width, uint32_t Height) {
  IDXGIFactory2* Factory = nullptr;
  CreateDXGIFactory1(__uuidof(IDXGIFactory2), (void**)&Factory);
  D3D_FEATURE_LEVEL FL = D3D_FEATURE_LEVEL_11_0;

  HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, &FL, 1, D3D11_SDK_VERSION, &D3DDevice, nullptr, &D3DContext);
  if (FAILED(hr)) return false;

  DXGI_SWAP_CHAIN_DESC1 SD = {};
  SD.Width = Width; SD.Height = Height; SD.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
  SD.SampleDesc.Count = 1; SD.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  SD.BufferCount = 2; SD.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD; SD.Scaling = DXGI_SCALING_STRETCH;

  hr = Factory->CreateSwapChainForHwnd(D3DDevice, hWnd, &SD, nullptr, nullptr, &D3DSwapChain);
  Factory->Release();
  if (FAILED(hr)) return false;

  hr = D3DSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&D3DBackBuffer);
  if (FAILED(hr)) return false;

  D3D11_TEXTURE2D_DESC TD = {};
  TD.Width = Width; TD.Height = Height; TD.MipLevels = 1; TD.ArraySize = 1;
  TD.Format = DXGI_FORMAT_B8G8R8A8_UNORM; TD.SampleDesc.Count = 1;
  TD.Usage = D3D11_USAGE_DYNAMIC; TD.BindFlags = D3D11_BIND_SHADER_RESOURCE; TD.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
  return SUCCEEDED(D3DDevice->CreateTexture2D(&TD, nullptr, &TransferTexture));
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int CmdShow) {
  win32_state win32State = {};
#if DEV_BUILD
  Win32GetEXEFilename(&win32State);
  // Build absolute file paths for our live code swapper pipelines
  char SourceDLLPath[WIN32_STATE_FILENAME_COUNT];
  Win32BuildEXEPathFilename(&win32State, STR(DLL_NAME), sizeof(SourceDLLPath), SourceDLLPath);

  char TempDLLPath[WIN32_STATE_FILENAME_COUNT];
  Win32BuildEXEPathFilename(&win32State, STR(DLL_NAME_TEMP), sizeof(TempDLLPath), TempDLLPath);

  // Initial configuration handshake pass
  win32_engine_code EngineCode = Win32LoadEngineCode(SourceDLLPath, TempDLLPath);
#endif
  platform_services Services = {};
  Services.PlatformFreeFileMemory = Win32FreeFileMemory;
  Services.PlatformReadEntireFile = Win32ReadEntireFile;
  Services.PlatformWriteEntireFile = Win32WriteEntireFile;
  Services.PlatformPrintConsole = Win32PrintToConsole;
  Services.PlatformReadConsole = Win32ConsoleReadLine;
  Services.PlatformReadArgs = Win32ReadArgs;

  app_configuration Config = {};
  char ArgBuffer[ARG_BUFFER_SIZE];
  Config.ArgBufferSize = ARG_BUFFER_SIZE;
  Config.ArgBuffer = ArgBuffer;

#if DEV_BUILD
  if (EngineCode.IsValid) { EngineCode.Configure(&Config, &Services); }
#else
  AppConfigure(&Config, &Services);
#endif
  // Calculate memory budgets using page alignments
  app_memory AppMemory = {};
  uint64_t BufferSize = Config.Width * Config.Height * sizeof(uint32_t);
  uint64_t PageSize = 4096;
  uint64_t AlignedBuffer = (BufferSize + (PageSize - 1)) & ~(PageSize - 1);
  uint64_t AlignedPerm = (Config.PermanentStorageSize + (PageSize - 1)) & ~(PageSize - 1);
  uint64_t AlignedTrans = (Config.TransientStorageSize + (PageSize - 1)) & ~(PageSize - 1);
  
  win32State.TotalSize = AlignedBuffer + AlignedPerm + AlignedTrans;
  win32State.MasterMemoryBlock = VirtualAlloc(nullptr, win32State.TotalSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
  
  AppMemory.Config = Config;
  AppMemory.PlatformServices = Services;
  AppMemory.PermanentStorageSize = AlignedPerm;
  AppMemory.TransientStorageSize = AlignedTrans;
  AppMemory.PermanentStorage = (uint8_t*)win32State.MasterMemoryBlock + AlignedBuffer;
  AppMemory.TransientStorage = (uint8_t*)AppMemory.PermanentStorage + AlignedPerm;

  graphics_buffer AppGraphicsBuffer = {};
  AppGraphicsBuffer.Size = Config.Width * Config.Height * sizeof(uint32_t);
  AppGraphicsBuffer.Data = (uint32_t*)win32State.MasterMemoryBlock;
  AppGraphicsBuffer.Width = Config.Width;
  AppGraphicsBuffer.Height = Config.Height;
  AppGraphicsBuffer.Pitch = Config.Width * sizeof(uint32_t);
  AppGraphicsBuffer.BytesPerPixel = sizeof(uint32_t);

  WNDCLASSEXA WindowClass = { sizeof(WNDCLASSEXA) };
  WindowClass.style = CS_OWNDC | CS_HREDRAW | CS_VREDRAW;
  WindowClass.lpfnWndProc = Win32MainCallback;
  WindowClass.hInstance = hInst;
  WindowClass.lpszClassName = "Win32ApplicationWindowClass";

  if (!RegisterClassExA(&WindowClass)) return -1;

  HWND Window = CreateWindowExA(
    0, WindowClass.lpszClassName, Config.WindowTitle, WS_OVERLAPPEDWINDOW,
    CW_USEDEFAULT, CW_USEDEFAULT, Config.Width, Config.Height,
    0, 0, hInst, 0
  );

  if (!Win32InitD3D(Window, Config.Width, Config.Height)) return -1;
  ShowWindow(Window, CmdShow);
  LARGE_INTEGER TimerFrequency, FrameStart, FrameEnd;
  QueryPerformanceFrequency(&TimerFrequency);

  platform_input InputBuffers[2] = {};
  platform_input* OldInput = &InputBuffers[0];
  platform_input* NewInput = &InputBuffers[1];

  g_Running = true;
  float DeltaTime = 0.0f;

#if DEV_BUILD
  if (EngineCode.IsValid) { EngineCode.Initialize(&AppMemory); }
#else
  AppInitialize(&AppMemory);
#endif
  while (g_Running) {
#if DEV_BUILD
    FILETIME currentWriteTime = Win32GetLastWriteTime(SourceDLLPath);
    if (CompareFileTime(&currentWriteTime, &EngineCode.LastWriteTime) != 0) {
      Win32UnloadEngineCode(&EngineCode);
      EngineCode = Win32LoadEngineCode(SourceDLLPath, TempDLLPath);
      if (EngineCode.IsValid) {
        EngineCode.ReloadDll(&AppMemory);
      }
    }
#endif

    SetWindowLongPtrA(Window, GWLP_USERDATA, (LONG_PTR)NewInput);
    NewInput->DeltaTime = DeltaTime;
    for (int i = 0; i < 256; ++i) {
      NewInput->Keys[i].IsDown = OldInput->Keys[i].IsDown;
      NewInput->Keys[i].WasDown = OldInput->Keys[i].IsDown;
    }
    for (int i = 0; i < 5; ++i) {
      NewInput->MouseButtons[i].IsDown = OldInput->MouseButtons[i].IsDown;
      NewInput->MouseButtons[i].WasDown = OldInput->MouseButtons[i].IsDown;
    }

    MSG Message;
    while (PeekMessageA(&Message, 0, 0, 0, PM_REMOVE)) {
      if (Message.message == WM_QUIT) g_Running = false;
      TranslateMessage(&Message);
      DispatchMessage(&Message);
    }

    QueryPerformanceCounter(&FrameStart);

#if DEV_BUILD
    if (EngineCode.IsValid) {
      EngineCode.UpdateAndRender(&AppMemory, NewInput, &AppGraphicsBuffer);
    }
#else
    AppUpdateAndRender(&AppMemory, NewInput, &AppGraphicsBuffer);
#endif

    D3D11_MAPPED_SUBRESOURCE MappedResource;
    if (SUCCEEDED(D3DContext->Map(TransferTexture, 0, D3D11_MAP_WRITE_DISCARD, 0, &MappedResource))) {
      uint8_t* Dest = (uint8_t*)MappedResource.pData;
      uint8_t* Source = (uint8_t*)AppGraphicsBuffer.Data;
      for (uint32_t y = 0; y < AppGraphicsBuffer.Height; ++y) {
        memcpy(Dest + (y * MappedResource.RowPitch), Source + (y * AppGraphicsBuffer.Pitch), AppGraphicsBuffer.Pitch);
      }
      D3DContext->Unmap(TransferTexture, 0);
    }

    D3DContext->CopyResource(D3DBackBuffer, TransferTexture);
    D3DSwapChain->Present(1, 0);
    QueryPerformanceCounter(&FrameEnd);

    DeltaTime = (float)(FrameEnd.QuadPart - FrameStart.QuadPart) / (float)TimerFrequency.QuadPart;

    // swap input
    platform_input* Temp = OldInput;
    OldInput = NewInput;
    NewInput = Temp;
    }
  VirtualFree(win32State.MasterMemoryBlock, 0, MEM_RELEASE);
  return 0;
}
#else
int main(int argc, char** argv) {
  HANDLE HConsole = GetStdHandle(STD_OUTPUT_HANDLE);
  if (HConsole == INVALID_HANDLE_VALUE) { return -1; }

  LARGE_INTEGER PerfFrequency, TimeStart, TimeEnd;
  QueryPerformanceFrequency(&PerfFrequency);
  QueryPerformanceCounter(&TimeStart);

  win32_state win32State = {};
  char ArgBuffer[ARG_BUFFER_SIZE];
  app_configuration Config = {};
  Config.ArgBufferSize = ARG_BUFFER_SIZE;
  Config.ArgBuffer = ArgBuffer;

  platform_services Services = {};
  Services.PlatformFreeFileMemory = Win32FreeFileMemory;
  Services.PlatformReadEntireFile = Win32ReadEntireFile;
  Services.PlatformWriteEntireFile = Win32WriteEntireFile;
  Services.PlatformPrintConsole = Win32PrintToConsole;
  Services.PlatformReadConsole = Win32ConsoleReadLine;
  Services.PlatformReadArgs = Win32ReadArgs;

#if DEV_BUILD
  Win32GetEXEFilename(&win32State);
  // Build absolute file paths for our live code swapper pipelines
  char SourceDLLPath[WIN32_STATE_FILENAME_COUNT];
  Win32BuildEXEPathFilename(&win32State, STR(DLL_NAME), sizeof(SourceDLLPath), SourceDLLPath);
  char TempDLLPath[WIN32_STATE_FILENAME_COUNT];
  Win32BuildEXEPathFilename(&win32State, STR(DLL_NAME_TEMP), sizeof(TempDLLPath), TempDLLPath);
  // Initial configuration handshake pass
  win32_engine_code EngineCode = Win32LoadEngineCode(SourceDLLPath, TempDLLPath);
#endif

  g_Running = true;
  while (g_Running) {
#if DEV_BUILD
    FILETIME currentWriteTime = Win32GetLastWriteTime(SourceDLLPath);
    if (CompareFileTime(&currentWriteTime, &EngineCode.LastWriteTime) != 0) {
      Win32UnloadEngineCode(&EngineCode);
      EngineCode = Win32LoadEngineCode(SourceDLLPath, TempDLLPath);
      if (!EngineCode.IsValid) {
        Win32PrintToConsole("Failed to run, Invalide DLL Code");
      }
    }
    EngineCode.Configure(&Config, &Services);
#else
    AppConfigure(&Config, &Services);
#endif
    app_memory AppMemory = {};
    uint64_t PageSize = 4096;
    uint64_t AlignedPerm = (Config.PermanentStorageSize + (PageSize - 1)) & ~(PageSize - 1);
    uint64_t AlignedTrans = (Config.TransientStorageSize + (PageSize - 1)) & ~(PageSize - 1);

    win32State.TotalSize = AlignedPerm + AlignedTrans;
    win32State.MasterMemoryBlock = VirtualAlloc(nullptr, win32State.TotalSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);

    AppMemory.Config = Config;
    AppMemory.PlatformServices = Services;
    AppMemory.PermanentStorageSize = AlignedPerm;
    AppMemory.TransientStorageSize = AlignedTrans;
    AppMemory.PermanentStorage = (uint8_t*)win32State.MasterMemoryBlock;
    AppMemory.TransientStorage = (uint8_t*)AppMemory.PermanentStorage + AlignedPerm;

    QueryPerformanceCounter(&TimeEnd);
    double ConfigSeconds = (double)(TimeEnd.QuadPart - TimeStart.QuadPart) / (double)PerfFrequency.QuadPart;

    char LogBuffer[512];
    snprintf(LogBuffer, sizeof(LogBuffer),
      "Configured in %f seconds, allocated %llu permanent, %llu Transient ...\n",
      ConfigSeconds, AlignedPerm, AlignedTrans);
    Win32PrintToConsole(LogBuffer);

    Win32PrintToConsole("\nInitializing App\n");
    QueryPerformanceCounter(&TimeStart);

#if DEV_BUILD
    EngineCode.Initialize(&AppMemory);
#else
    AppInitialize(&AppMemory);
#endif

    QueryPerformanceCounter(&TimeEnd);
    double InitSeconds = (double)(TimeEnd.QuadPart - TimeStart.QuadPart) / (double)PerfFrequency.QuadPart;

    snprintf(LogBuffer, sizeof(LogBuffer), "\nInitialized in %f seconds\n", InitSeconds);
    Win32PrintToConsole(LogBuffer);

    Win32PrintToConsole("\nRunning program\n");
    QueryPerformanceCounter(&TimeStart);

#if DEV_BUILD
    EngineCode.RunHeadless(&AppMemory);
#else
    AppRunHeadless(&AppMemory);
#endif

    QueryPerformanceCounter(&TimeEnd);
    double RunSeconds = (double)(TimeEnd.QuadPart - TimeStart.QuadPart) / (double)PerfFrequency.QuadPart;

    snprintf(LogBuffer, sizeof(LogBuffer), "\nCompleted in %f seconds (%f ms)\n", RunSeconds, RunSeconds * 1000.0);
    Win32PrintToConsole(LogBuffer);

    Win32PrintToConsole("Restart program (Y / N)? \n");
    char InputBuffer[16] = {};
    if (Win32ConsoleReadLine(InputBuffer, sizeof(InputBuffer))) {
      char InputChar = InputBuffer[0];
      if (InputChar != 'y' && InputChar != 'Y') {
        g_Running = false;
      }
    }
    else {
      g_Running = false;
    }
    VirtualFree(win32State.MasterMemoryBlock, 0, MEM_RELEASE);
  }
  return 0;
  }
#endif