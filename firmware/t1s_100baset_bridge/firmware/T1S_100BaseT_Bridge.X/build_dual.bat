@echo off
REM ---------------------------------------------------------------------------
REM build_dual.bat  —  Baut zwei Firmware-Varianten:
REM                    GM       (PLCA nodeId=0)  -> dist/gm/
REM                    Follower (PLCA nodeId=1)  -> dist/follower/
REM
REM Aufruf:  build_dual.bat
REM ---------------------------------------------------------------------------

setlocal

set "MAKE=C:\Program Files\Microchip\MPLABX\v6.25\gnuBins\GnuWin32\bin\make.exe"
set "TEE=C:\Program Files\Microchip\MPLABX\v6.25\gnuBins\GnuWin32\bin\tee.exe"
set "MAKEFILE=nbproject/Makefile-default.mk"
set "HEX_SRC=dist/default/production/T1S_100BaseT_Bridge.X.production.hex"
set "HEX_SRC_WIN=dist\default\production\T1S_100BaseT_Bridge.X.production.hex"
set "MAKE_JOBS=10"
set "PROJ_DIR=%~dp0"
set "INIT_C=%PROJ_DIR%..\src\config\default\initialization.c"

set "HEX_GM=%PROJ_DIR%dist\gm\T1S_100BaseT_Bridge.X.production.hex"
set "HEX_FOLLOWER=%PROJ_DIR%dist\follower\T1S_100BaseT_Bridge.X.production.hex"

echo.
echo === Build T1S_100BaseT_Bridge (DUAL: GM + Follower) ===
echo Verzeichnis : %PROJ_DIR%
echo Make        : %MAKE%
echo Jobs        : %MAKE_JOBS%
echo INIT        : %INIT_C%
echo.

cd /d "%PROJ_DIR%"

REM --- Ausgabeverzeichnisse anlegen ----------------------------------------
if not exist "%PROJ_DIR%dist\gm"       mkdir "%PROJ_DIR%dist\gm"
if not exist "%PROJ_DIR%dist\follower" mkdir "%PROJ_DIR%dist\follower"

REM --- Alte HEX-Dateien loeschen, damit nur neue gueltige Artefakte bleiben --
if exist "%HEX_SRC_WIN%" del /f /q "%HEX_SRC_WIN%"
if exist "%HEX_GM%" del /f /q "%HEX_GM%"
if exist "%HEX_FOLLOWER%" del /f /q "%HEX_FOLLOWER%"

REM =========================================================================
REM  1) GM-Firmware  (nodeId=0)
REM =========================================================================
echo === [1/2] Patche nodeId -> 0 (Grandmaster / PLCA-Koordinator) ===
python "%PROJ_DIR%patch_nodeid.py" "%INIT_C%" 0
if errorlevel 1 goto :patch_error

echo === [1/2] Build GM ===
"%MAKE%" -j%MAKE_JOBS% -f "%MAKEFILE%" "%HEX_SRC%" 2>&1 | "%TEE%" "%PROJ_DIR%build_gm.txt"
if errorlevel 1 (
    echo.
    echo *** GM-BUILD FEHLGESCHLAGEN — siehe %PROJ_DIR%build_gm.txt ***
    goto :restore_node1
)
copy /Y "%HEX_SRC_WIN%" "%HEX_GM%"
if errorlevel 1 ( echo *** Kopieren GM-HEX fehlgeschlagen *** & goto :restore_node1 )
echo   GM-HEX: %HEX_GM%

REM =========================================================================
REM  2) Follower-Firmware  (nodeId=1)
REM =========================================================================
echo.
echo === [2/2] Patche nodeId -> 1 (Follower) ===
python "%PROJ_DIR%patch_nodeid.py" "%INIT_C%" 1
if errorlevel 1 goto :patch_error

echo === [2/2] Build Follower ===
"%MAKE%" -j%MAKE_JOBS% -f "%MAKEFILE%" "%HEX_SRC%" 2>&1 | "%TEE%" "%PROJ_DIR%build_follower.txt"
if errorlevel 1 (
    echo.
    echo *** FOLLOWER-BUILD FEHLGESCHLAGEN — siehe %PROJ_DIR%build_follower.txt ***
    goto :end
)
copy /Y "%HEX_SRC_WIN%" "%HEX_FOLLOWER%"
if errorlevel 1 ( echo *** Kopieren Follower-HEX fehlgeschlagen *** & goto :end )
echo   Follower-HEX: %HEX_FOLLOWER%

REM =========================================================================
echo.
echo === BEIDE BUILDS ERFOLGREICH ===
echo   GM       (nodeId=0): %HEX_GM%
echo   Follower (nodeId=1): %HEX_FOLLOWER%
echo   initialization.c bleibt auf nodeId=1 (Follower-Default).
echo.

exit /b 0

:patch_error
echo *** Python-Patch fehlgeschlagen ***
exit /b 1

:restore_node1
echo Stelle nodeId=1 wieder her ...
python "%PROJ_DIR%patch_nodeid.py" "%INIT_C%" 1
exit /b 1

:end
exit /b 0
