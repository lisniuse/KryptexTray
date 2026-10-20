@echo off
setlocal
cd /d "%~dp0"

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "VSPATH="
if exist "%VSWHERE%" (
  for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSPATH=%%i"
)
if not defined VSPATH set "VSPATH=C:\Program Files\Microsoft Visual Studio\2022\Community"
if not exist "%VSPATH%\VC\Auxiliary\Build\vcvars64.bat" (
  echo Could not find Visual Studio C++ tools.
  echo Install "Desktop development with C++" or edit VSPATH in build.bat.
  exit /b 1
)

call "%VSPATH%\VC\Auxiliary\Build\vcvars64.bat" >nul
if not exist build mkdir build

cl /nologo /std:c++17 /utf-8 /EHsc /O2 /W3 ^
  src\main.cpp /Fe:build\KryptexTray.exe /Fo:build\ ^
  /link /SUBSYSTEM:WINDOWS

if errorlevel 1 exit /b 1
echo.
echo Built: build\KryptexTray.exe
