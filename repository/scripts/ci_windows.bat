@echo off
setlocal
cd /d "%~dp0.."
set "RIF_VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%RIF_VSWHERE%" (
  echo Visual Studio Installer vswhere.exe was not found.
  exit /b 1
)
for /f "usebackq tokens=*" %%i in (`"%RIF_VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "RIF_VSROOT=%%i"
if not defined RIF_VSROOT (
  echo An x64 MSVC toolchain was not found.
  exit /b 1
)
call "%RIF_VSROOT%\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64
if errorlevel 1 exit /b 1
if not exist build mkdir build
cl /nologo /std:c++17 /utf-8 /EHsc /W4 /Od /MT tests\core_tests.cpp /Fe:build\core_tests.exe /Fo:build\
if errorlevel 1 exit /b 1
build\core_tests.exe
if errorlevel 1 exit /b 1
call build_msvc.bat
if errorlevel 1 exit /b 1
exit /b 0
