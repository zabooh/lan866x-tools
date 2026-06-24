@echo off
setlocal
:: ===========================================================================
:: setup.bat - one-time, per-machine setup after cloning.
::
:: Anyone who clones this repo runs this ONCE. It adapts the project to the
:: local machine: Python deps, the installed XC32 compiler version, and the
:: connected board's debugger/COM port. (The MPLAB X version for flashing is
:: auto-discovered by mdb_flash.py, so it needs no setup step.)
::
::   git clone ...
::   cd firmware\t1s_100baset_bridge
::   setup.bat            <-- this script
::   build.bat
::   python flash.py
::
:: Connect the board via its USB debugger port BEFORE running this so the
:: flasher step can detect it. Steps are independent: a failure in one is
:: reported but does not abort the rest.
:: ===========================================================================
set "SCRIPT_DIR=%~dp0"
set "RC=0"

echo ============================================================
echo   T1S 100BaseT Bridge - one-time machine setup
echo ============================================================

where python >nul 2>&1
if errorlevel 1 (
    echo [ERROR] Python not found in PATH. Install Python 3.9+ and re-run.
    exit /b 1
)

echo.
echo [1/4] Python dependencies (pyserial) ...
python -m pip install -r "%SCRIPT_DIR%requirements.txt"
if errorlevel 1 ( echo [WARN] pip install failed - check your network/pip. & set "RC=1" )

echo.
echo [2/4] Compiler selection (XC32) ...
python "%SCRIPT_DIR%setup_compiler.py"
if errorlevel 1 ( echo [WARN] setup_compiler.py failed - run it manually. & set "RC=1" )

echo.
echo [3/4] Flasher / board detection (EDBG) ...
python "%SCRIPT_DIR%setup_flasher.py"
if errorlevel 1 ( echo [WARN] setup_flasher.py failed - connect the board, then run it manually. & set "RC=1" )

echo.
echo [4/4] VS Code debug fix (SAME54_DFP tool pack) ...
python "%SCRIPT_DIR%setup_debug.py"
if errorlevel 1 ( echo [WARN] setup_debug.py failed - only needed for VS Code debugging. & set "RC=1" )

echo.
echo ============================================================
if "%RC%"=="0" (
    echo   Setup complete. Now build and flash:
) else (
    echo   Setup finished with warnings ^(see above^). You can still:
)
echo     build.bat
echo     python flash.py
echo ============================================================
endlocal & exit /b %RC%
