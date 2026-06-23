@echo off
REM ---------------------------------------------------------------------------
REM build_all.bat  —  Firmware bauen und beide Boards flashen
REM
REM Aufruf:
REM   build_all.bat          → nur bauen
REM   build_all.bat flash    → bauen + beide Boards flashen
REM ---------------------------------------------------------------------------

setlocal

set "MAKE=C:\Program Files\Microchip\MPLABX\v6.25\gnuBins\GnuWin32\bin\make.exe"
set "TEE=C:\Program Files\Microchip\MPLABX\v6.25\gnuBins\GnuWin32\bin\tee.exe"
set "MAKEFILE=nbproject/Makefile-default.mk"
set "HEX=dist/default/production/T1S_100BaseT_Bridge.X.production.hex"
set "MAKE_JOBS=10"
set "BUILD_LOG=%~dp0build_output_new.txt"
set "PROJ_DIR=%~dp0"

echo.
echo === Build T1S_100BaseT_Bridge ===
echo Verzeichnis : %PROJ_DIR%
echo Make        : %MAKE%
echo Jobs        : %MAKE_JOBS%
echo.

cd /d "%PROJ_DIR%"

echo === Build ===
"%MAKE%" -j%MAKE_JOBS% -f "%MAKEFILE%" "%HEX%" 2>&1 | "%TEE%" "%BUILD_LOG%"

if errorlevel 1 (
    echo.
    echo *** BUILD FEHLGESCHLAGEN — siehe %BUILD_LOG% ***
    exit /b 1
)

echo.
echo === BUILD ERFOLGREICH ===
echo HEX: %PROJ_DIR%%HEX%
echo.

if /i "%1"=="flash" (
    echo === Flash beide Boards ===
    python "%PROJ_DIR%flash_all.py"
    if errorlevel 1 (
        echo *** FLASH FEHLGESCHLAGEN ***
        exit /b 1
    )
)

exit /b 0
