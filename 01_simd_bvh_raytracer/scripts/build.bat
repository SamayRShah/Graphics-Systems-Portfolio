@echo off
setlocal EnableDelayedExpansion

:: Quick change src / env
SET CC=cl
SET CORE=..\src\platform\win32_app.cpp
SET APP=..\src\simd_bvh_ray_tracer\ray_tracer.cpp
SET INC_FLAGS=/I ..\src\platform
SET APP_NAME=ray_tracer
SET DLL_NAME=%APP_NAME%.dll
SET TEMP_DLL_NAME=%APP_NAME%_temp.dll

:: Shared flags
:: -Gm- non incremental, -GR- no RTTI, -EHa no exept
:: -Oi intrinsics, wd4201 - nameless struct unions
:: wd4100 unused function params, 
::-GL Program optimization across files
SET BASE_COMMON=-nologo -Gm- -GR- -EHa- -Oi -WX -W4 -wd4201 -wd4100
SET BASE_SIMD=/arch:AVX /fp:fast
SET BASE_LINK=-incremental:no -opt:ref user32.lib

:: Default = debug
SET BuildMode=DEBUG
SET DEFINES=-DDEV_BUILD=1 -DAPP_WIN32=1 -DHEADLESS_BUILD=0
SET DLL_DEFINES=-DDLL_NAME=%DLL_NAME% -DDLL_NAME_TEMP=%TEMP_DLL_NAME%
SET CommonFlags=-MTd %BASE_COMMON% -Z7
SET MathSimdFlags=%BASE_SIMD% /O2
SET LinkFlags=%BASE_LINK% d3d11.lib dxgi.lib

:: set config
if "%1"=="-r" (
  SET BuildMode=RELEASE
  SET DEFINES=-DDEV_BUILD=0 -DAPP_WIN32=1 -DHEADLESS_BUILD=0
  SET CommonFlags=-MT %BASE_COMMON% -O2 -GL
  SET MathSimdFlags=%BASE_SIMD%
  SET LinkFlags=%BASE_LINK% -opt:icf -ltcg gdi32.lib winmm.lib d3d11.lib dxgi.lib
  SET APP_NAME=%APP_NAME%_release
) else if "%1"=="-h" (
  SET BuildMode=HEADLESS_DEBUG
  SET DEFINES=-DDEV_BUILD=1 -DAPP_WIN32=1 -DHEADLESS_BUILD=1
  SET DLL_NAME=%APP_NAME%.dll
  SET TEMP_DLL_NAME=%APP_NAME%_temp.dll
  SET DLL_DEFINES=-DDLL_NAME=%DLL_NAME% -DDLL_NAME_TEMP=%TEMP_DLL_NAME%
) else if "%1"=="-hr" (
  SET BuildMode=HEADLESS_RELEASE
  SET DEFINES=-DDEV_BUILD=0 -DAPP_WIN32=1 -DHEADLESS_BUILD=1
  SET CommonFlags=-MT %BASE_COMMON% -O2 -GL
  SET MathSimdFlags=%BASE_SIMD%
  SET LinkFlags=%BASE_LINK% -opt:icf -ltcg
  SET APP_NAME=%APP_NAME%_release_headless
)

SET OutExe=%APP_NAME%.exe

echo Building in [%BuildMode%] mode...

IF NOT EXIST ..\build mkdir ..\build
pushd ..\build

:: BUILD
if "%BuildMode%"=="RELEASE" (
  %CC% %INC_FLAGS% %CommonFlags% %MathSimdFlags% %DEFINES% %CORE% %APP% /link %LinkFlags% /out:%OutExe%
) else if "%BuildMode%"=="HEADLESS_RELEASE" (
  %CC% %INC_FLAGS% %CommonFlags% %MathSimdFlags% %DEFINES% %CORE% %APP% /link %LinkFlags% /out:%OutExe%
) else if "%BuildMode%"=="HEADLESS_DEBUG" (
  del app_*.pdb > NUL 2> NUL
  %CC% %INC_FLAGS% %CommonFlags% %MathSimdFlags% %DEFINES% %APP% /LD /link /DLL /incremental:no /PDB:app_%random%.pdb /out:%DLL_NAME%
  %CC% %INC_FLAGS% %CommonFlags% %DLL_DEFINES% %DEFINES% %CORE% /link %LinkFlags% /out:%OutExe%
) else (
  del app_*.pdb > NUL 2> NUL
  %CC% %INC_FLAGS% %CommonFlags% %MathSimdFlags% %DEFINES% %APP% /LD /link /DLL /incremental:no /PDB:app_%random%.pdb /out:%DLL_NAME%
  %CC% %INC_FLAGS% %CommonFlags% %DLL_DEFINES% %DEFINES% %CORE% /link %LinkFlags% /out:%OutExe%
)

popd
