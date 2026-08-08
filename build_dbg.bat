@echo off
setlocal EnableDelayedExpansion
title Necromancer - DEBUG build

set "ROOT=%~dp0"
set "BUILD=%ROOT%out\build\x64-debug"

where cl.exe >nul 2>&1
if not errorlevel 1 goto env_ready

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" goto no_vs

for /f "delims=" %%i in ('call "%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath') do set "VS_INSTALL=%%i"

if not defined VS_INSTALL goto no_vs

set "VCVARS=%VS_INSTALL%\VC\Auxiliary\Build\vcvars64.bat"
if not exist "%VCVARS%" goto no_vcvars

call "%VCVARS%" >nul
where cl.exe >nul 2>&1
if errorlevel 1 goto vcvars_fail

:env_ready
cd /d "%ROOT%"
cmake --preset x64-debug 2>&1
if errorlevel 1 goto cmake_fail
cmake --build "%BUILD%" --target Necromancer 2>&1
exit /b %ERRORLEVEL%

:no_vs
echo [ERROR] Visual Studio 2022+ with C++ workload not found.
exit /b 1

:no_vcvars
echo [ERROR] vcvars64.bat not found.
exit /b 1

:vcvars_fail
echo [ERROR] vcvars64.bat failed.
exit /b 1

:cmake_fail
echo [ERROR] CMake configure failed.
exit /b 1
