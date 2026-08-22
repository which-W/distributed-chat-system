@echo off
setlocal
cd /d "%~dp0"

set "BUILD_DIR=%~1"
if "%BUILD_DIR%"=="" set "BUILD_DIR=build\windows-server-release"

if not exist "%BUILD_DIR%\CMakeCache.txt" (
    echo CMake build directory not found: %BUILD_DIR%
    echo Configure first with: cmake --preset windows-server-release
    exit /b 1
)

cmake --build "%BUILD_DIR%" --target run_all
endlocal
