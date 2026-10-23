@echo off
setlocal
cd /d "%~dp0"
if not exist build mkdir build
pushd src
rc /nologo /fo ..\build\resources.res resources.rc
if errorlevel 1 exit /b 1
popd
cl /nologo /std:c++17 /utf-8 /EHsc /O2 /MT /W4 /D_WIN32_WINNT=0x0602 src\main.cpp build\resources.res /Fe:build\RobloxIconFix.exe /Fo:build\ /link /SUBSYSTEM:WINDOWS /MANIFEST:NO user32.lib kernel32.lib gdi32.lib comctl32.lib shell32.lib ole32.lib uuid.lib
if errorlevel 1 exit /b 1
echo Built: build\RobloxIconFix.exe
