@echo off
setlocal

set CMAKE_DIR=%~dp0cmake\T1S_100BaseT_Bridge\default
set BUILD_DIR=%~dp0_build\T1S_100BaseT_Bridge\default

echo === T1S_100BaseT_Bridge CMake Build ===

:: Configure if build directory does not exist yet
if not exist "%BUILD_DIR%\build.ninja" (
    echo [1/2] Configuring...
    pushd "%CMAKE_DIR%"
    cmake --preset T1S_100BaseT_Bridge_default_conf
    if errorlevel 1 (
        echo ERROR: Configure failed.
        popd
        exit /b 1
    )
    popd
) else (
    echo [1/2] Already configured, skipping.
)

:: Build
echo [2/2] Building...
cmake --build "%BUILD_DIR%"
if errorlevel 1 (
    echo ERROR: Build failed.
    exit /b 1
)

echo.
echo Build successful. Output: %~dp0out\T1S_100BaseT_Bridge\default.elf
endlocal
