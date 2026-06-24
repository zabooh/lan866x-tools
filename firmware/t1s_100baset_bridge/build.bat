@echo off
setlocal EnableDelayedExpansion

:: ===========================================================================
:: build.bat - Shell build for the T1S<->100BASE-T bridge firmware (cmake/Ninja).
::
:: Tool setup (once per machine), like net_10base_t1s:
::     install_dependencies.bat       (pyserial for the python tools)
::     python setup_compiler.py       (pick the installed XC32 version)
::     python setup_flasher.py        (assign the bridge board's debugger)
::     python setup_debug.py          (SAME54_DFP tool-pack fix for VS Code)
:: Then:
::     build.bat [incremental|clean|rebuild|help]
::     python flash.py
:: ===========================================================================

set "SCRIPT_DIR=%~dp0"
set "MPLAB_DIR=%SCRIPT_DIR%firmware\T1S_100BaseT_Bridge.X"
set "CMAKE_DIR=%MPLAB_DIR%\cmake\T1S_100BaseT_Bridge\default"
set "BUILD_DIR=%MPLAB_DIR%\_build\T1S_100BaseT_Bridge\default"
set "PRESET=T1S_100BaseT_Bridge_default_conf"
set "ELF_PATH=%MPLAB_DIR%\out\T1S_100BaseT_Bridge\default.elf"
set "HEX_PATH=%MPLAB_DIR%\out\T1S_100BaseT_Bridge\default.hex"
set "COMPILER_CONFIG=%SCRIPT_DIR%setup_compiler.config"

:: ---------------------------------------------------------------------------
:: Read the XC32 selection written by setup_compiler.py (JSON via PowerShell).
:: ---------------------------------------------------------------------------
if not exist "%COMPILER_CONFIG%" (
    echo ERROR: No compiler configured.
    echo        Run "python setup_compiler.py" first to select an XC32 version.
    exit /b 1
)
for /f "usebackq delims=" %%V in (`powershell -NoProfile -Command "(Get-Content '%COMPILER_CONFIG%' | ConvertFrom-Json).version"`)  do set "XC32_VERSION=%%V"
for /f "usebackq delims=" %%P in (`powershell -NoProfile -Command "(Get-Content '%COMPILER_CONFIG%' | ConvertFrom-Json).compiler"`) do set "XC32_COMPILER=%%P"
for /f "usebackq delims=" %%D in (`powershell -NoProfile -Command "(Get-Content '%COMPILER_CONFIG%' | ConvertFrom-Json).bin_dir"`)  do set "XC32_BIN_DIR=%%D"

if not exist "%XC32_COMPILER%" (
    echo ERROR: Selected compiler not found: %XC32_COMPILER%
    echo        XC32 %XC32_VERSION% does not appear to be installed. Re-run setup_compiler.py.
    exit /b 1
)
echo Compiler  : XC32 %XC32_VERSION%  (%XC32_COMPILER%)

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
echo   clean          Delete all temporary build artifacts
echo   rebuild        Clean then perform a full build
exit /b 0

:clean
echo Cleaning build directory...
if exist "%BUILD_DIR%" ( rmdir /s /q "%BUILD_DIR%" & echo Deleted: %BUILD_DIR% ) else ( echo Nothing to clean. )
exit /b 0

:rebuild
echo [0/2] Cleaning before rebuild...
if exist "%BUILD_DIR%" rmdir /s /q "%BUILD_DIR%"
goto :build

:incremental
:build
echo [1/2] Configuring with CMake (preset %PRESET%)...
cmake --preset %PRESET% -S "%CMAKE_DIR%" -B "%BUILD_DIR%" -DPACK_REPO_PATH="%USERPROFILE%/.mchp_packs" -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
if errorlevel 1 ( echo ERROR: CMake configure failed. & exit /b 1 )

if exist "%BUILD_DIR%\compile_commands.json" copy /Y "%BUILD_DIR%\compile_commands.json" "%SCRIPT_DIR%compile_commands.json" >nul

echo [2/2] Building with Ninja...
cmake --build "%BUILD_DIR%"
if errorlevel 1 ( echo ERROR: Build failed. & exit /b 1 )

echo.
echo BUILD SUCCESSFUL.
if exist "%HEX_PATH%" (
    echo HEX: %HEX_PATH%
    rem Copy the HEX into a tracked release dir so a fresh clone can flash
    rem without building - out is gitignored.
    if not exist "%SCRIPT_DIR%release" mkdir "%SCRIPT_DIR%release"
    copy /Y "%HEX_PATH%" "%SCRIPT_DIR%release\T1S_100BaseT_Bridge.hex" >nul
    echo Released: %SCRIPT_DIR%release\T1S_100BaseT_Bridge.hex
) else (
    echo WARNING: expected HEX not found at %HEX_PATH%
)

rem Post-build memory / interrupt summary (flash/RAM, heap, IRQ handlers).
if exist "%ELF_PATH%" python "%SCRIPT_DIR%build_summary.py" "%BUILD_DIR%" "%ELF_PATH%" "%XC32_BIN_DIR%"
endlocal
