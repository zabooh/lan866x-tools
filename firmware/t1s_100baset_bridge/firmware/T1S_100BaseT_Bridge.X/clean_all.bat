@echo off
REM ---------------------------------------------------------------------------
REM clean_all.bat  —  Entfernt alle Build-Artefakte fuer Full-Rebuild
REM
REM Aufruf:
REM   clean_all.bat
REM ---------------------------------------------------------------------------

setlocal

set "MAKE=C:\Program Files\Microchip\MPLABX\v6.25\gnuBins\GnuWin32\bin\make.exe"
set "MAKEFILE=nbproject/Makefile-default.mk"
set "PROJ_DIR=%~dp0"

echo.
echo === Clean T1S_100BaseT_Bridge ===
echo Verzeichnis : %PROJ_DIR%
echo Make        : %MAKE%
echo.

cd /d "%PROJ_DIR%"

echo === make clean ===
"%MAKE%" -f "%MAKEFILE%" clean
if errorlevel 1 (
    echo WARNUNG: make clean meldete einen Fehler. Versuche manuelles Aufraeumen...
)

echo.
echo === Zusatzaeuberung build/dist ===
if exist "build" rmdir /s /q "build"
if exist "dist"  rmdir /s /q "dist"

echo.
echo === CLEAN FERTIG ===
echo Naechster Schritt: build_all.bat
echo.

exit /b 0
