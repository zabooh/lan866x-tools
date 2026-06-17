@echo off
REM ============================================================================
REM  build.bat  -  Baut das Discovery-Tool (lan866x-discovery.exe) unter Windows.
REM
REM  Aufruf:
REM    build.bat          Compiler automatisch waehlen (MinGW, sonst VS2022)
REM    build.bat mingw    MinGW-w64 (GCC) erzwingen
REM    build.bat vs       Visual Studio 2022 (MSVC) erzwingen
REM    build.bat clean    Build-Ordner loeschen
REM
REM  Voraussetzungen: siehe README.md, Kapitel "2. Systemvoraussetzungen".
REM ============================================================================
setlocal
cd /d "%~dp0"
set "BUILD_DIR=out"

if /I "%~1"=="clean" (
    echo [build] Loesche %BUILD_DIR% ...
    if exist "%BUILD_DIR%" rmdir /s /q "%BUILD_DIR%"
    echo [build] Fertig.
    goto :eof
)

where cmake >nul 2>nul
if errorlevel 1 (
    echo [build] FEHLER: 'cmake' nicht im PATH. Siehe README Kapitel 2.
    exit /b 1
)

REM --- Generatorwahl --------------------------------------------------------
if /I "%~1"=="mingw" goto :mingw
if /I "%~1"=="vs"    goto :vs
where gcc >nul 2>nul
if errorlevel 1 (goto :vs) else (goto :mingw)

:mingw
echo [build] Generator: MinGW Makefiles ^(GCC^)
where gcc >nul 2>nul
if errorlevel 1 (
    echo [build] FEHLER: 'gcc' nicht im PATH. MinGW-w64 installieren ^(README Kap. 2^).
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
echo [build] FEHLER bei der CMake-Konfiguration.
exit /b 1

:builderr
echo [build] FEHLER beim Bauen.
exit /b 1

:ok
REM --- alle erzeugten exes nach release\ kopieren ---------------------------
if not exist "release" mkdir "release"
set "BINDIR=%BUILD_DIR%"
if exist "%BUILD_DIR%\Release" set "BINDIR=%BUILD_DIR%\Release"
for %%T in (discovery i2cscan gpio spi) do (
    copy /Y "%BINDIR%\lan866x-%%T.exe" "release\" >nul 2>nul && echo [build] -^> release\lan866x-%%T.exe
)

echo.
echo [build] OK. Binaries in release\
goto :eof
