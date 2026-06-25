@echo off
REM ============================================================================
REM  build.bat  -  Builds the tools (lan866x-*.exe) on Windows.
REM
REM  Usage:
REM    build.bat          choose compiler automatically (MinGW, else VS2022)
REM    build.bat mingw    force MinGW-w64 (GCC)
REM    build.bat vs       force Visual Studio 2022 (MSVC)
REM    build.bat clean    delete the build folder
REM
REM  Requirements: see README.md, chapter "2. System requirements".
REM ============================================================================
setlocal
cd /d "%~dp0"
set "BUILD_DIR=out"

if /I "%~1"=="clean" (
    echo [build] Deleting %BUILD_DIR% ...
    if exist "%BUILD_DIR%" rmdir /s /q "%BUILD_DIR%"
    echo [build] Done.
    goto :eof
)

where cmake >nul 2>nul
if errorlevel 1 (
    echo [build] ERROR: 'cmake' not in PATH. See README chapter 2.
    exit /b 1
)

REM --- generator selection --------------------------------------------------
if /I "%~1"=="mingw" goto :mingw
if /I "%~1"=="vs"    goto :vs
where gcc >nul 2>nul
if errorlevel 1 (goto :vs) else (goto :mingw)

:mingw
echo [build] Generator: MinGW Makefiles ^(GCC^)
where gcc >nul 2>nul
if errorlevel 1 (
    echo [build] ERROR: 'gcc' not in PATH. Install MinGW-w64 ^(README ch. 2^).
    exit /b 1
)
cmake -G "MinGW Makefiles" -B "%BUILD_DIR%" || goto :cfgerr
cmake --build "%BUILD_DIR%" || goto :builderr
set "EXE=%BUILD_DIR%\lan866x-discovery.exe"
goto :ok

:vs
echo [build] Generator: Visual Studio 17 2022 ^(MSVC^)
cmake -G "Visual Studio 17 2022" -A x64 -B "%BUILD_DIR%" || goto :cfgerr
cmake --build "%BUILD_DIR%" --config Release || goto :builderr
set "EXE=%BUILD_DIR%\Release\lan866x-discovery.exe"
goto :ok

:cfgerr
echo [build] ERROR during CMake configuration.
exit /b 1

:builderr
echo [build] ERROR during build.
exit /b 1

:ok
REM --- copy all built exes to release\ --------------------------------------
if not exist "release" mkdir "release"
set "BINDIR=%BUILD_DIR%"
if exist "%BUILD_DIR%\Release" set "BINDIR=%BUILD_DIR%\Release"
for %%T in (discovery servicetest i2cscan i2cid proxmon lan8680 gpio gpioevents ledscan ledblink ledtoggle gpiomax ledpwm proxled spi spiid thumbmon adc pwm uart boot flashimg flashpkg clickdemo diag video dncpmon dncpdisc ntpsync) do (
    copy /Y "%BINDIR%\lan866x-%%T.exe" "release\" >nul 2>nul && echo [build] -^> release\lan866x-%%T.exe
)

echo.
echo [build] OK. Binaries in release\
goto :eof
