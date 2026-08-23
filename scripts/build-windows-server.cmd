@echo off
setlocal
set "CHAT_REQUESTED_VCPKG_ROOT=%VCPKG_ROOT%"

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
  echo Visual Studio Installer vswhere.exe was not found.
  exit /b 1
)

for /f "usebackq tokens=*" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSINSTALL=%%I"
if not defined VSINSTALL (
  echo MSVC build tools were not found.
  exit /b 1
)

call "%VSINSTALL%\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64
if errorlevel 1 exit /b %errorlevel%
if defined CHAT_REQUESTED_VCPKG_ROOT set "VCPKG_ROOT=%CHAT_REQUESTED_VCPKG_ROOT%"

cmake --fresh --preset windows-server-release
if errorlevel 1 exit /b %errorlevel%
cmake --build --preset windows-server-release
exit /b %errorlevel%
