@echo off
setlocal EnableDelayedExpansion

:: ===========================================================================
:: build.bat - Shell build for the T1S<->100BASE-T bridge firmware.
::
:: Wraps the MPLAB Harmony CMake/Ninja build so the project can be built from a
:: plain shell (no MPLAB X IDE).  Adapted from the net_10base_t1s tooling for
:: this single project.  The XC32 version is the one baked into the generated
:: toolchain.cmake (currently v4.60); run setup_compiler.py to switch.
::
:: Usage: build.bat [incremental|clean|rebuild|help]
:: ===========================================================================

set "SCRIPT_DIR=%~dp0"
set "MPLAB_DIR=%SCRIPT_DIR%firmware\T1S_100BaseT_Bridge.X"
set "CMAKE_DIR=%MPLAB_DIR%\cmake\T1S_100BaseT_Bridge\default"
set "BUILD_DIR=%MPLAB_DIR%\_build\T1S_100BaseT_Bridge\default"
set "PRESET=T1S_100BaseT_Bridge_default_conf"
set "ELF_PATH=%MPLAB_DIR%\out\T1S_100BaseT_Bridge\default.elf"
set "HEX_PATH=%MPLAB_DIR%\out\T1S_100BaseT_Bridge\default.hex"

set "MODE=incremental"
if not "%~1"=="" set "MODE=%~1"

if /i "%MODE%"=="help"        goto :help
if /i "%MODE%"=="clean"       goto :clean
if /i "%MODE%"=="rebuild"     goto :rebuild
if /i "%MODE%"=="incremental" goto :incremental

echo ERROR: Unknown parameter "%~1"
goto :help

:help
echo Usage: build.bat [incremental^|clean^|rebuild^|help]
echo   (no argument)  Incremental build (default)
echo   incremental    Incremental build - only recompiles changed files
echo   clean          Delete all temporary build artifacts
echo   rebuild        Clean then perform a full build
echo   help           Show this help
exit /b 0

:clean
echo Cleaning build directory...
if exist "%BUILD_DIR%" (
    rmdir /s /q "%BUILD_DIR%"
    echo Deleted: %BUILD_DIR%
) else (
    echo Nothing to clean.
)
exit /b 0

:rebuild
echo [0/2] Cleaning before rebuild...
if exist "%BUILD_DIR%" rmdir /s /q "%BUILD_DIR%"
goto :build

:incremental
:build
echo [1/2] Configuring with CMake (preset %PRESET%)...
cmake --preset %PRESET% -S "%CMAKE_DIR%" -B "%BUILD_DIR%" -DPACK_REPO_PATH="%USERPROFILE%/.mchp_packs" -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
if errorlevel 1 (
    echo ERROR: CMake configure failed.
    exit /b 1
)

:: Surface compile_commands.json at the bridge root for editor IntelliSense.
if exist "%BUILD_DIR%\compile_commands.json" (
    copy /Y "%BUILD_DIR%\compile_commands.json" "%SCRIPT_DIR%compile_commands.json" >nul
)

echo [2/2] Building with Ninja...
cmake --build "%BUILD_DIR%"
if errorlevel 1 (
    echo ERROR: Build failed.
    exit /b 1
)

echo.
echo BUILD SUCCESSFUL.
if exist "%HEX_PATH%" (
    echo HEX: %HEX_PATH%
) else (
    echo WARNING: expected HEX not found at %HEX_PATH%
)
endlocal
