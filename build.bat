@echo off
rem Builds the Decoherence VST3 + Standalone app (Release).
rem Needs Visual Studio 2022 (or Build Tools) with the "Desktop development with C++" workload.
rem Run install.bat afterwards to copy the VST3 where DAWs look for it.
setlocal

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "VSDIR="
if exist "%VSWHERE%" (
    for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
)
if not defined VSDIR (
    echo Could not find a Visual Studio C++ toolchain.
    exit /b 1
)

call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
set "CMAKE=%VSDIR%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"

cd /d "%~dp0"
"%CMAKE%" -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release || exit /b 1
"%CMAKE%" --build build || exit /b 1

echo.
echo Done:
echo   build\Decoherence_artefacts\Release\VST3\Decoherence.vst3
echo   build\Decoherence_artefacts\Release\Standalone\Decoherence.exe
endlocal
