@echo off
setlocal enabledelayedexpansion

:: check env vars
if defined VSAPPIDDIR (
    set "VCVARS_PATH=%VSAPPIDDIR%..\..\VC\Auxiliary\Build\vcvarsall.bat"
    if exist "!VCVARS_PATH!" goto :found
)

:: check install paths
for %%P in ("%ProgramFiles%" "%ProgramFiles(x86)%") do (
    for %%V in (2022 2019 2017) do (
        for %%E in (Enterprise Professional Community BuildTools) do (
            set "VCVARS_PATH=%%~P\Microsoft Visual Studio\%%V\%%E\VC\Auxiliary\Build\vcvarsall.bat"
            if exist "!VCVARS_PATH!" goto :found
        )
    )
)
:: 3. VCVars path
set "VCVARS_PATH=D:\Visual Studio\VC\Auxiliary\Build\vcvarsall.bat"
if exist "%VCVARS_PATH%" goto :found

echo ERROR: vcvarsall.bat could not be found automatically - Enter Vars Path manually

:found
echo Found environment setup at: "%VCVARS_PATH%"
endlocal & call "%VCVARS_PATH%" x64
