@echo off
setlocal EnableDelayedExpansion
title Necromancer - NIGHTLY build

set "ROOT=%~dp0"
set "BUILD=%ROOT%out\build\x64-nightly"

if defined INCLUDE if defined LIB if defined PATH ( echo [*] MSVC environment already active & goto env_ready )

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" ( echo [ERROR] vswhere not found & exit /b 1 )

for /f "delims=" %%i in ('call "%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath') do set "VS_INSTALL=%%i"

if not defined VS_INSTALL ( echo [ERROR] Visual Studio not found & exit /b 1 )

set "VCVARS=%VS_INSTALL%\VC\Auxiliary\Build\vcvars64.bat"
if not exist "%VCVARS%" ( echo [ERROR] vcvars64.bat not found & exit /b 1 )

echo [*] Loading MSVC environment from %VS_INSTALL%...
call "%VCVARS%" 

:env_ready
where cl.exe >nul 2>&1
if errorlevel 1 ( echo [ERROR] cl.exe not found after vcvars & exit /b 1 )

where cmake.exe >nul 2>&1
if errorlevel 1 ( echo [ERROR] cmake not found & exit /b 1 )

where ninja.exe >nul 2>&1
if errorlevel 1 ( echo [ERROR] ninja not found & exit /b 1 )

cd /d "%ROOT%"
cmake --preset x64-nightly 2>&1
if errorlevel 1 ( echo [ERROR] CMake configure failed & exit /b 1 )

ninja -C "%BUILD%" Necromancer 2>&1
exit /b %ERRORLEVEL%
