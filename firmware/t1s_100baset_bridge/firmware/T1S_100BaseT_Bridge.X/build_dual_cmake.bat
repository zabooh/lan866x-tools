@echo off
REM ---------------------------------------------------------------------------
REM build_dual_cmake.bat  —  Baut zwei Firmware-Varianten mit CMake/Ninja:
REM                          GM       (PLCA nodeId=0)  -> dist/gm/
REM                          Follower (PLCA nodeId=1)  -> dist/follower/
REM
REM Aufruf:
REM   build_dual_cmake.bat                  – normaler inkrementeller Build
REM   build_dual_cmake.bat clean            – Build-Verzeichnis loeschen + neu konfigurieren + bauen
REM   build_dual_cmake.bat rebuild          – wie clean, aber explizit
REM   build_dual_cmake.bat gm               – nur GM-Variante (nodeId=0)
REM   build_dual_cmake.bat follower         – nur Follower-Variante (nodeId=1)
REM   build_dual_cmake.bat help             – diese Hilfe anzeigen
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
if /i "%~1"=="gm"       set "MODE=gm"
if /i "%~1"=="follower" set "MODE=follower"
if /i "%~1"=="help"     goto :show_help
if /i "%~1"=="/?"       goto :show_help

set "TEE=C:\Program Files\Microchip\MPLABX\v6.25\gnuBins\GnuWin32\bin\tee.exe"
set "PROJ_DIR=%~dp0"
set "CMAKE_DIR=%PROJ_DIR%cmake\T1S_100BaseT_Bridge\default"
set "BUILD_DIR=%PROJ_DIR%_build\T1S_100BaseT_Bridge\default"
set "OUT_DIR=%PROJ_DIR%out\T1S_100BaseT_Bridge"
set "INIT_C=%PROJ_DIR%..\src\config\default\initialization.c"

set "HEX_SRC=%OUT_DIR%\default.hex"
set "HEX_SRC_WIN=%OUT_DIR%\default.hex"
set "HEX_GM=%PROJ_DIR%dist\gm\T1S_100BaseT_Bridge.X.production.hex"
set "HEX_FOLLOWER=%PROJ_DIR%dist\follower\T1S_100BaseT_Bridge.X.production.hex"

echo.
echo === Build T1S_100BaseT_Bridge mit CMake (DUAL: GM + Follower) ===
echo Projektverzeichnis : %PROJ_DIR%
echo CMake-Preset-Dir   : %CMAKE_DIR%
echo Build-Dir          : %BUILD_DIR%
echo INIT               : %INIT_C%
echo Modus              : %MODE%
echo.

cd /d "%PROJ_DIR%"

REM --- Ausgabeverzeichnisse anlegen -----------------------------------------
if not exist "%PROJ_DIR%dist\gm"            mkdir "%PROJ_DIR%dist\gm"
if not exist "%PROJ_DIR%dist\follower"      mkdir "%PROJ_DIR%dist\follower"

REM --- Alte HEX-Dateien loeschen -------------------------------------------
if /i "%MODE%"=="all"      ( if exist "%HEX_GM%"       del /f /q "%HEX_GM%" )
if /i "%MODE%"=="all"      ( if exist "%HEX_FOLLOWER%" del /f /q "%HEX_FOLLOWER%" )
if /i "%MODE%"=="gm"       ( if exist "%HEX_GM%"       del /f /q "%HEX_GM%" )
if /i "%MODE%"=="follower" ( if exist "%HEX_FOLLOWER%" del /f /q "%HEX_FOLLOWER%" )
if /i "%MODE%"=="clean"    ( if exist "%HEX_GM%"       del /f /q "%HEX_GM%" )
if /i "%MODE%"=="clean"    ( if exist "%HEX_FOLLOWER%" del /f /q "%HEX_FOLLOWER%" )

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
    echo [0/2] CMake konfigurieren ^(XC32 %XC32_VERSION%^) ...
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
    echo [0/2] CMake bereits konfiguriert. ^(XC32 %XC32_VERSION% - bei Versionswechsel 'rebuild' aufrufen^)
)

REM =========================================================================
REM  1) GM-Firmware  (nodeId=0)
REM =========================================================================
if /i "%MODE%"=="follower" goto :skip_gm

echo === [1/2] Patche nodeId -> 0 (Grandmaster / PLCA-Koordinator) ===
python "%PROJ_DIR%patch_nodeid.py" "%INIT_C%" 0
if errorlevel 1 goto :patch_error

echo === [1/2] Build GM ===
if exist "%TEE%" (
    cmake --build "%BUILD_DIR%" 2>&1 | "%TEE%" "%PROJ_DIR%build_gm.txt"
) else (
    cmake --build "%BUILD_DIR%" > "%PROJ_DIR%build_gm.txt" 2>&1
    type "%PROJ_DIR%build_gm.txt"
)
if errorlevel 1 (
    echo.
    echo *** GM-BUILD FEHLGESCHLAGEN — siehe %PROJ_DIR%build_gm.txt ***
    goto :restore_node1
)

if not exist "%HEX_SRC_WIN%" (
    echo *** HEX-Datei nicht gefunden: %HEX_SRC_WIN% ***
    goto :restore_node1
)
copy /Y "%HEX_SRC_WIN%" "%HEX_GM%"
if errorlevel 1 ( echo *** Kopieren GM-HEX fehlgeschlagen *** & goto :restore_node1 )
echo   GM-HEX: %HEX_GM%

if /i "%MODE%"=="gm" goto :done_gm

:skip_gm
REM =========================================================================
REM  2) Follower-Firmware  (nodeId=1)
REM =========================================================================
echo.
echo === [2/2] Patche nodeId -> 1 (Follower) ===
python "%PROJ_DIR%patch_nodeid.py" "%INIT_C%" 1
if errorlevel 1 goto :patch_error

echo === [2/2] Build Follower ===
if exist "%TEE%" (
    cmake --build "%BUILD_DIR%" 2>&1 | "%TEE%" "%PROJ_DIR%build_follower.txt"
) else (
    cmake --build "%BUILD_DIR%" > "%PROJ_DIR%build_follower.txt" 2>&1
    type "%PROJ_DIR%build_follower.txt"
)
if errorlevel 1 (
    echo.
    echo *** FOLLOWER-BUILD FEHLGESCHLAGEN — siehe %PROJ_DIR%build_follower.txt ***
    goto :end
)

if not exist "%HEX_SRC_WIN%" (
    echo *** HEX-Datei nicht gefunden: %HEX_SRC_WIN% ***
    goto :end
)
copy /Y "%HEX_SRC_WIN%" "%HEX_FOLLOWER%"
if errorlevel 1 ( echo *** Kopieren Follower-HEX fehlgeschlagen *** & goto :end )
echo   Follower-HEX: %HEX_FOLLOWER%

REM =========================================================================
echo.
if /i "%MODE%"=="follower" goto :done_follower_only
echo === BEIDE BUILDS ERFOLGREICH ===
echo   GM       (nodeId=0): %HEX_GM%
echo   Follower (nodeId=1): %HEX_FOLLOWER%
goto :done_finish

:done_follower_only
echo === FOLLOWER-BUILD ERFOLGREICH ===
echo   Follower (nodeId=1): %HEX_FOLLOWER%

:done_finish
echo   initialization.c bleibt auf nodeId=1 (Follower-Default).
echo.

exit /b 0

:done_gm
echo.
echo === GM-BUILD ERFOLGREICH ===
echo   GM (nodeId=0): %HEX_GM%
echo   initialization.c bleibt auf nodeId=0.
echo.
exit /b 0

:patch_error
echo *** Python-Patch fehlgeschlagen ***
exit /b 1

:restore_node1
echo Stelle nodeId=1 wieder her ...
python "%PROJ_DIR%patch_nodeid.py" "%INIT_C%" 1
exit /b 1

:show_help
echo.
echo Verwendung: build_dual_cmake.bat [Modus]
echo.
echo   (kein Modus)   Inkrementeller Dual-Build: GM + Follower
echo   clean          Build-Dir loeschen, neu konfigurieren + Dual-Build
echo   rebuild        Alias fuer clean
echo   gm             Nur GM-Variante bauen (nodeId=0)
echo   follower       Nur Follower-Variante bauen (nodeId=1)
echo   help           Diese Hilfe anzeigen
echo.
exit /b 0

:end
exit /b 0
