@echo off
REM ---------------------------------------------------------------------------
REM build_single_cmake.bat  —  Baut eine einzige Firmware-Variante mit CMake/Ninja.
REM                            NodeId bleibt unveraendert (Default: 1 aus initialization.c).
REM                            Zur Laufzeit via CLI-Befehl 'plca_node' umstellbar.
REM
REM Aufruf:
REM   build_single_cmake.bat                  – normaler inkrementeller Build
REM   build_single_cmake.bat clean            – Build-Verzeichnis loeschen + neu konfigurieren + bauen
REM   build_single_cmake.bat rebuild          – wie clean, aber explizit
REM   build_single_cmake.bat help             – diese Hilfe anzeigen
REM
REM ---------------------------------------------------------------------------
REM  TOOLCHAIN-AUSWAHL  (bei Aenderung: rebuild ausfuehren!)
REM  Verfuegbare Versionen: v4.60  v5.10
REM ---------------------------------------------------------------------------
set "XC32_VERSION=v4.60"
REM ---------------------------------------------------------------------------

setlocal

REM --- Parameter auswerten --------------------------------------------------
set "MODE=all"
if /i "%~1"=="clean"    set "MODE=clean"
if /i "%~1"=="rebuild"  set "MODE=clean"
if /i "%~1"=="help"     goto :show_help
if /i "%~1"=="/?"       goto :show_help

set "TEE=C:\Program Files\Microchip\MPLABX\v6.25\gnuBins\GnuWin32\bin\tee.exe"
set "PROJ_DIR=%~dp0"
set "CMAKE_DIR=%PROJ_DIR%cmake\T1S_100BaseT_Bridge\default"
set "BUILD_DIR=%PROJ_DIR%_build\T1S_100BaseT_Bridge\default"
set "OUT_DIR=%PROJ_DIR%out\T1S_100BaseT_Bridge"

set "HEX_SRC=%OUT_DIR%\default.hex"
set "HEX_SRC_WIN=%OUT_DIR%\default.hex"
set "HEX_OUT=%PROJ_DIR%dist\single\T1S_100BaseT_Bridge.X.production.hex"

echo.
echo === Build T1S_100BaseT_Bridge mit CMake (SINGLE Firmware) ===
echo Projektverzeichnis : %PROJ_DIR%
echo CMake-Preset-Dir   : %CMAKE_DIR%
echo Build-Dir          : %BUILD_DIR%
echo HEX-Ausgabe        : %HEX_OUT%
echo Modus              : %MODE%
echo.

cd /d "%PROJ_DIR%"

REM --- Ausgabeverzeichnis anlegen -------------------------------------------
if not exist "%PROJ_DIR%dist\single" mkdir "%PROJ_DIR%dist\single"

REM --- Alte HEX-Datei loeschen ----------------------------------------------
if exist "%HEX_OUT%" del /f /q "%HEX_OUT%"

REM --- Clean: Build-Verzeichnis komplett loeschen ---------------------------
if /i "%MODE%"=="clean" (
    echo [clean] Loesche Build-Verzeichnis: %BUILD_DIR%
    if exist "%BUILD_DIR%" rmdir /s /q "%BUILD_DIR%"
    echo [clean] Build-Verzeichnis geloescht.
    echo.
)

REM --- CMake konfigurieren (nur wenn noch kein build.ninja vorhanden) -------
set "XC32_BIN=C:/Program Files/Microchip/xc32/%XC32_VERSION%/bin"
if not exist "%BUILD_DIR%\build.ninja" (
    echo [0/1] CMake konfigurieren ^(XC32 %XC32_VERSION%^) ...
    pushd "%CMAKE_DIR%"
    cmake --preset T1S_100BaseT_Bridge_default_conf ^
        -DXCV32_VERSION=%XC32_VERSION% ^
        -DCMAKE_C_COMPILER="%XC32_BIN%/xc32-gcc.exe" ^
        -DCMAKE_CXX_COMPILER="%XC32_BIN%/xc32-g++.exe" ^
        -DCMAKE_ASM_COMPILER="%XC32_BIN%/xc32-gcc.exe"
    if errorlevel 1 (
        echo *** CMAKE CONFIGURE FEHLGESCHLAGEN ***
        popd
        exit /b 1
    )
    popd
    echo.
) else (
    echo [0/1] CMake bereits konfiguriert. ^(XC32 %XC32_VERSION% - bei Versionswechsel 'rebuild' aufrufen^)
)

REM =========================================================================
REM  Build Single Firmware
REM =========================================================================
echo === [1/1] Build Single Firmware ===
if exist "%TEE%" (
    cmake --build "%BUILD_DIR%" 2>&1 | "%TEE%" "%PROJ_DIR%build_single.txt"
) else (
    cmake --build "%BUILD_DIR%" > "%PROJ_DIR%build_single.txt" 2>&1
    type "%PROJ_DIR%build_single.txt"
)
if errorlevel 1 (
    echo.
    echo *** BUILD FEHLGESCHLAGEN — siehe %PROJ_DIR%build_single.txt ***
    exit /b 1
)

if not exist "%HEX_SRC_WIN%" (
    echo *** HEX-Datei nicht gefunden: %HEX_SRC_WIN% ***
    exit /b 1
)

copy /Y "%HEX_SRC_WIN%" "%HEX_OUT%"
if errorlevel 1 ( echo *** Kopieren HEX fehlgeschlagen *** & exit /b 1 )

echo.
echo === BUILD ERFOLGREICH ===
echo   Single Firmware: %HEX_OUT%
echo   (NodeId=1 per Default — zur Laufzeit via 'plca_node 0' auf GM umstellen)
echo.

exit /b 0

:show_help
echo.
echo Verwendung: build_single_cmake.bat [Modus]
echo.
echo   (kein Modus)   Inkrementeller Build
echo   clean          Build-Dir loeschen, neu konfigurieren + bauen
echo   rebuild        Alias fuer clean
echo   help           Diese Hilfe anzeigen
echo.
echo Hinweis: Diese Firmware verwendet NodeId=1 als Default.
echo   GM-Board:       'plca_node 0' + 'ptp_mode master'
echo   Follower-Board: 'plca_node 1' + 'ptp_mode follower'
echo.
exit /b 0
