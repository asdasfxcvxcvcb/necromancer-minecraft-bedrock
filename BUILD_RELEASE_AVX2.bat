@echo off
setlocal EnableDelayedExpansion
title Necromancer - RELEASE AVX2 build

set "ROOT=%~dp0"
set "BUILD=%ROOT%out\build\x64-release-avx2"

echo ============================================
echo   NECROMANCER RELEASE AVX2 BUILD  (/arch:AVX2, max optimized, no PDB)
echo ============================================
echo.

where cl.exe >nul 2>&1
if not errorlevel 1 goto env_ready

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" goto no_vs

for /f "delims=" %%i in ('call "%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath') do set "VS_INSTALL=%%i"

if not defined VS_INSTALL goto no_vs

set "VCVARS=%VS_INSTALL%\VC\Auxiliary\Build\vcvars64.bat"
if not exist "%VCVARS%" goto no_vcvars

echo [*] Found Visual Studio: %VS_INSTALL%
echo [*] Loading MSVC environment...
call "%VCVARS%" >nul
where cl.exe >nul 2>&1
if errorlevel 1 goto vcvars_fail

:env_ready
where cl.exe >nul 2>&1
if errorlevel 1 goto no_cl

where cmake.exe >nul 2>&1
if errorlevel 1 goto no_cmake

where ninja.exe >nul 2>&1
if errorlevel 1 goto no_ninja

where ld.exe >nul 2>&1
if errorlevel 1 goto no_ld

cd /d "%ROOT%"

echo [*] Configuring x64-release-avx2...
cmake --preset x64-release-avx2
if errorlevel 1 goto cmake_fail

echo.
echo [*] Building AVX2 Release DLL (/arch:AVX2, slow on purpose)...
echo.
cmake --build "%BUILD%" --parallel %NUMBER_OF_PROCESSORS%
if errorlevel 1 goto build_fail

echo.
echo [DONE] Release AVX2 build OK.
echo   DLL: %BUILD%\Necromancer.dll
echo   NOTE: Requires AVX2 CPU (Haswell 2013+). Use regular build for old machines.
pause
exit /b 0

:no_vs
echo [ERROR] Visual Studio 2022+ with C++ workload not found.
pause
exit /b 1

:no_vcvars
echo [ERROR] vcvars64.bat not found. Check VS installation.
pause
exit /b 1

:vcvars_fail
echo [ERROR] vcvars64.bat failed to initialize.
pause
exit /b 1

:no_cl
echo [ERROR] cl.exe not found after vcvars. VS installation corrupt?
pause
exit /b 1

:no_cmake
echo [ERROR] CMake not on PATH. Install CMake or VS "C++ CMake tools".
pause
exit /b 1

:no_ninja
echo [ERROR] Ninja not on PATH. Install Ninja or VS "C++ CMake tools".
pause
exit /b 1

:no_ld
echo [ERROR] MinGW ld.exe not on PATH (needed for assets).
echo         Install MSYS2: https://www.msys2.org/
echo         Then: pacman -S mingw-w64-x86_64-toolchain
pause
exit /b 1

:cmake_fail
echo [ERROR] CMake configure failed.
pause
exit /b 1

:build_fail
echo.
echo [ERROR] BUILD FAILED - scroll up for errors.
pause
exit /b 1
