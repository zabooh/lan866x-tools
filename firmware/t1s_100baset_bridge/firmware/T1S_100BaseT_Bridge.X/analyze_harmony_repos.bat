@echo off
REM T1S 100BaseT Bridge - Complete Harmony 3 Version Analysis
REM This script analyzes what's actually installed vs what MCC typically expects

setlocal EnableDelayedExpansion

REM Configuration
set HARMONY_ROOT=C:\Users\M91221\.mcc\HarmonyContent
set PROJECT_NAME=T1S 100BaseT Bridge - Repository Analysis

echo ========================================
echo %PROJECT_NAME%
echo ========================================
echo.
echo Analyzing Harmony 3 installation at:
echo %HARMONY_ROOT%
echo.

REM Check if Git is installed
git --version >nul 2>&1
if !ERRORLEVEL! neq 0 (
    echo ERROR: Git is not installed or not in PATH!
    goto :error
)

REM Check if Harmony root exists
if not exist "%HARMONY_ROOT%" (
    echo ERROR: Harmony 3 directory does not exist: %HARMONY_ROOT%
    echo Please run setup_harmony_complete.bat first.
    goto :error
)

cd /d "%HARMONY_ROOT%"

echo Repository Analysis Results:
echo.
echo ========================================
echo PROJECT-REQUIRED Repositories
echo ========================================
echo These repositories are essential for T1S 100BaseT Bridge:
echo.

REM Project-required repositories from harmony-manifest-success.yml
set REQUIRED_REPOS=core csp dev_packs net net_10base_t1s crypto wolfssl

for %%r in (%REQUIRED_REPOS%) do (
    if exist "%%r" (
        cd %%r
        for /f "tokens=*" %%a in ('git describe --tags --exact-match HEAD 2^>nul') do set current_version=%%a
        if "!current_version!"=="" (
            for /f "tokens=*" %%a in ('git rev-parse --abbrev-ref HEAD 2^>nul') do set current_version=branch: %%a
        )
        echo ✓ %%r: !current_version!
        cd ..
        set current_version=
    ) else (
        echo ✗ %%r: NOT FOUND ^(CRITICAL - project will fail^)
    )
)

echo.
echo ========================================
echo STANDARD MCC Repositories
echo ========================================
echo These repositories are typically expected by MCC:
echo.

REM Standard MCC repositories
set STANDARD_REPOS=freertos bsp usb bootloader touch

for %%r in (%STANDARD_REPOS%) do (
    if exist "%%r" (
        cd %%r
        for /f "tokens=*" %%a in ('git describe --tags --exact-match HEAD 2^>nul') do set current_version=%%a
        if "!current_version!"=="" (
            for /f "tokens=*" %%a in ('git rev-parse --abbrev-ref HEAD 2^>nul') do set current_version=branch: %%a
        )
        echo ✓ %%r: !current_version!
        cd ..
        set current_version=
    ) else (
        echo - %%r: Not installed ^(MCC might expect this^)
    )
)

echo.
echo ========================================
echo OPTIONAL Repositories
echo ========================================
echo Extended functionality repositories:
echo.

REM Optional repositories
set OPTIONAL_REPOS=gfx audio bluetooth wireless wireless_wifi motor_control

for %%r in (%OPTIONAL_REPOS%) do (
    if exist "%%r" (
        cd %%r
        for /f "tokens=*" %%a in ('git describe --tags --exact-match HEAD 2^>nul') do set current_version=%%a
        if "!current_version!"=="" (
            for /f "tokens=*" %%a in ('git rev-parse --abbrev-ref HEAD 2^>nul') do set current_version=branch: %%a
        )
        echo ✓ %%r: !current_version!
        cd ..
        set current_version=
    ) else (
        echo - %%r: Not installed ^(optional^)
    )
)

echo.
echo ========================================
echo UNKNOWN Repositories
echo ========================================
echo Other repositories found (not in standard list):
echo.

set FOUND_UNKNOWN=0
for /d %%d in (*) do (
    set IS_KNOWN=0
    
    REM Check if this directory is in our known lists
    for %%k in (%REQUIRED_REPOS% %STANDARD_REPOS% %OPTIONAL_REPOS%) do (
        if /i "%%d"=="%%k" set IS_KNOWN=1
    )
    
    if !IS_KNOWN! equ 0 (
        if exist "%%d\.git" (
            echo ? %%d: Additional repository ^(unknown to this script^)
            set FOUND_UNKNOWN=1
        )
    )
)

if %FOUND_UNKNOWN% equ 0 (
    echo - No additional repositories found
)

echo.
echo ========================================
echo RECOMMENDATIONS
echo ========================================

REM Count missing required repos
set MISSING_REQUIRED=0
for %%r in (%REQUIRED_REPOS%) do (
    if not exist "%%r" set /a MISSING_REQUIRED+=1
)

REM Count missing standard repos
set MISSING_STANDARD=0
for %%r in (%STANDARD_REPOS%) do (
    if not exist "%%r" set /a MISSING_STANDARD+=1
)

if %MISSING_REQUIRED% gtr 0 (
    echo ⚠ CRITICAL: %MISSING_REQUIRED% required repositories are missing!
    echo   → Your T1S 100BaseT Bridge project will likely fail to build
    echo   → Run: setup_harmony_complete.bat -R
) else (
    echo ✓ All required repositories are present
)

if %MISSING_STANDARD% gtr 0 (
    echo ⚠ WARNING: %MISSING_STANDARD% standard repositories are missing
    echo   → MCC might not function optimally
    echo   → Run: setup_harmony_complete.bat -S ^(recommended^)
) else (
    echo ✓ All standard repositories are present
)

echo.
echo ========================================
echo SUMMARY
echo ========================================

REM Generate content summary  
echo Harmony 3 Content Analysis:
echo.
echo PROJECT COMPATIBILITY:
if %MISSING_REQUIRED% equ 0 (
    echo ✓ T1S 100BaseT Bridge project: READY
) else (
    echo ✗ T1S 100BaseT Bridge project: MISSING DEPENDENCIES
)

echo.
echo MCC COMPATIBILITY:
if %MISSING_STANDARD% equ 0 (
    echo ✓ MCC Content Manager: FULLY SUPPORTED
) else if %MISSING_STANDARD% leq 2 (
    echo ⚠ MCC Content Manager: MOSTLY SUPPORTED
) else (
    echo ✗ MCC Content Manager: LIMITED SUPPORT
)

echo.
echo RECOMMENDED ACTIONS:
if %MISSING_REQUIRED% gtr 0 (
    echo 1. Install missing required repositories immediately
) else if %MISSING_STANDARD% gtr 0 (
    echo 1. Consider installing standard repositories for full MCC support
) else (
    echo 1. Your installation looks complete!
)

if %MISSING_REQUIRED% gtr 0 (
    echo 2. Run version checker after installation: check_harmony_versions.bat
)

echo.
pause
exit /b %MISSING_REQUIRED%

:error
echo Repository analysis failed!
pause
exit /b 1