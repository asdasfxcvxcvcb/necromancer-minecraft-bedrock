@echo off
setlocal
set "ROOT=D:\Games\asd\asdsd\necromancer_ mcbe client 2"
set "BUILD=%ROOT%\out\build\x64-release"
call "D:\vis\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
where cl.exe >nul 2>&1
if errorlevel 1 ( echo [ERR] no cl & exit /b 1 )
cd /d "%ROOT%"
cmake --preset x64-release >nul 2>&1
if errorlevel 1 ( echo [ERR] cmake configure & exit /b 1 )
cmake --build "%BUILD%" --parallel %NUMBER_OF_PROCESSORS%
if errorlevel 1 ( echo [ERR] build failed & exit /b 1 )
echo [OK] build done
for %%F in ("%BUILD%\Necromancer.dll") do echo DLL %%~zF bytes %%~tF
exit /b 0
