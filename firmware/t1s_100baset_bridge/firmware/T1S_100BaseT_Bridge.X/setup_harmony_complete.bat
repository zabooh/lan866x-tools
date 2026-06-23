@echo off
REM T1S 100BaseT Bridge - Complete Harmony 3 Repository Setup for MCC
REM This script clones ALL standard Harmony 3 repositories to ensure MCC compatibility
REM Based on standard MCC Content Manager repository set

setlocal EnableDelayedExpansion

REM Configuration
set HARMONY_ROOT=C:\Users\M91221\.mcc\HarmonyContent
set PROJECT_NAME=T1S 100BaseT Bridge - Complete Setup

echo ========================================
echo %PROJECT_NAME%
echo ========================================
echo.
echo This script clones ALL standard Harmony 3 repositories for full MCC compatibility.
echo Target: %HARMONY_ROOT%
echo.

REM Check if Git is installed
git --version >nul 2>&1
if !ERRORLEVEL! neq 0 (
    echo ERROR: Git is not installed or not in PATH!
    echo Please install Git from https://git-scm.com/
    goto :error
)

REM Create main Harmony 3 directory structure
if not exist "%HARMONY_ROOT%" (
    echo Creating Harmony 3 root directory: %HARMONY_ROOT%
    mkdir "%HARMONY_ROOT%"
)

cd /d "%HARMONY_ROOT%"
echo Working in: %CD%
echo.

REM Define ALL standard Harmony 3 repositories
echo Defining repository list...

REM Core Framework Repositories
set REPO_LIST[0].name=core
set REPO_LIST[0].url=https://github.com/Microchip-MPLAB-Harmony/core.git
set REPO_LIST[0].version=v3.13.4
set REPO_LIST[0].priority=REQUIRED

set REPO_LIST[1].name=csp
set REPO_LIST[1].url=https://github.com/Microchip-MPLAB-Harmony/csp.git
set REPO_LIST[1].version=v3.18.5
set REPO_LIST[1].priority=REQUIRED

set REPO_LIST[2].name=dev_packs
set REPO_LIST[2].url=https://github.com/Microchip-MPLAB-Harmony/dev_packs.git
set REPO_LIST[2].version=v3.18.1
set REPO_LIST[2].priority=REQUIRED

REM Networking Repositories (Required for T1S project)
set REPO_LIST[3].name=net
set REPO_LIST[3].url=https://github.com/Microchip-MPLAB-Harmony/net.git
set REPO_LIST[3].version=v3.11.1
set REPO_LIST[3].priority=REQUIRED

set REPO_LIST[4].name=net_10base_t1s
set REPO_LIST[4].url=https://github.com/Microchip-MPLAB-Harmony/net_10base_t1s.git
set REPO_LIST[4].version=v1.3.2
set REPO_LIST[4].priority=REQUIRED

REM Security Repositories (Required for T1S project)
set REPO_LIST[5].name=crypto
set REPO_LIST[5].url=https://github.com/Microchip-MPLAB-Harmony/crypto.git
set REPO_LIST[5].version=v3.8.1
set REPO_LIST[5].priority=REQUIRED

set REPO_LIST[6].name=wolfssl
set REPO_LIST[6].url=https://github.com/Microchip-MPLAB-Harmony/wolfssl.git
set REPO_LIST[6].version=v5.4.0
set REPO_LIST[6].priority=REQUIRED

REM Additional Standard Repositories (for full MCC compatibility)
set REPO_LIST[7].name=freertos
set REPO_LIST[7].url=https://github.com/Microchip-MPLAB-Harmony/freertos.git
set REPO_LIST[7].version=v10.4.6
set REPO_LIST[7].priority=STANDARD

set REPO_LIST[8].name=bsp
set REPO_LIST[8].url=https://github.com/Microchip-MPLAB-Harmony/bsp.git
set REPO_LIST[8].version=latest
set REPO_LIST[8].priority=STANDARD

set REPO_LIST[9].name=usb
set REPO_LIST[9].url=https://github.com/Microchip-MPLAB-Harmony/usb.git
set REPO_LIST[9].version=latest
set REPO_LIST[9].priority=STANDARD

set REPO_LIST[10].name=touch
set REPO_LIST[10].url=https://github.com/Microchip-MPLAB-Harmony/touch.git
set REPO_LIST[10].version=latest
set REPO_LIST[10].priority=STANDARD

set REPO_LIST[11].name=gfx
set REPO_LIST[11].url=https://github.com/Microchip-MPLAB-Harmony/gfx.git
set REPO_LIST[11].version=latest
set REPO_LIST[11].priority=STANDARD

set REPO_LIST[12].name=gfx_apps_sam_e70_s70_v70_v71
set REPO_LIST[12].url=https://github.com/Microchip-MPLAB-Harmony/gfx_apps_sam_e70_s70_v70_v71.git
set REPO_LIST[12].version=latest
set REPO_LIST[12].priority=OPTIONAL

set REPO_LIST[13].name=bluetooth
set REPO_LIST[13].url=https://github.com/Microchip-MPLAB-Harmony/bluetooth.git
set REPO_LIST[13].version=latest
set REPO_LIST[13].priority=OPTIONAL

set REPO_LIST[14].name=wireless
set REPO_LIST[14].url=https://github.com/Microchip-MPLAB-Harmony/wireless.git
set REPO_LIST[14].version=latest
set REPO_LIST[14].priority=OPTIONAL

set REPO_LIST[15].name=wireless_wifi
set REPO_LIST[15].url=https://github.com/Microchip-MPLAB-Harmony/wireless_wifi.git
set REPO_LIST[15].version=latest
set REPO_LIST[15].priority=OPTIONAL

set REPO_LIST[16].name=audio
set REPO_LIST[16].url=https://github.com/Microchip-MPLAB-Harmony/audio.git
set REPO_LIST[16].version=latest
set REPO_LIST[16].priority=OPTIONAL

set REPO_LIST[17].name=motor_control
set REPO_LIST[17].url=https://github.com/Microchip-MPLAB-Harmony/motor_control.git
set REPO_LIST[17].version=latest
set REPO_LIST[17].priority=OPTIONAL

set REPO_LIST[18].name=bootloader
set REPO_LIST[18].url=https://github.com/Microchip-MPLAB-Harmony/bootloader.git
set REPO_LIST[18].version=latest
set REPO_LIST[18].priority=STANDARD

set REPO_LIST[19].name=bootloader_apps_uart
set REPO_LIST[19].url=https://github.com/Microchip-MPLAB-Harmony/bootloader_apps_uart.git
set REPO_LIST[19].version=latest
set REPO_LIST[19].priority=OPTIONAL

set TOTAL_REPOS=20

echo.
echo Repository Installation Plan:
echo - REQUIRED repositories: 7 (essential for T1S project)
echo - STANDARD repositories: 4 (typical MCC installation)
echo - OPTIONAL repositories: 9 (additional functionality)
echo Total: %TOTAL_REPOS% repositories
echo.

set /p install_mode="Select installation mode: [R]equired only, [S]tandard (recommended), [F]ull: "

set INSTALL_REQUIRED=1
set INSTALL_STANDARD=0
set INSTALL_OPTIONAL=0

if /i "%install_mode%"=="S" (
    set INSTALL_STANDARD=1
    echo Installing Required + Standard repositories...
) else if /i "%install_mode%"=="F" (
    set INSTALL_STANDARD=1
    set INSTALL_OPTIONAL=1
    echo Installing ALL repositories...
) else (
    echo Installing Required repositories only...
)

echo.
echo Starting installation...
echo.

set CLONED_COUNT=0
set SKIPPED_COUNT=0
set ERROR_COUNT=0

for /L %%i in (0,1,19) do (
    set SHOULD_INSTALL=0
    
    if "!REPO_LIST[%%i].priority!"=="REQUIRED" set SHOULD_INSTALL=1
    if "!REPO_LIST[%%i].priority!"=="STANDARD" if %INSTALL_STANDARD% equ 1 set SHOULD_INSTALL=1
    if "!REPO_LIST[%%i].priority!"=="OPTIONAL" if %INSTALL_OPTIONAL% equ 1 set SHOULD_INSTALL=1
    
    if !SHOULD_INSTALL! equ 1 (
        echo [!CLONED_COUNT!/??] Processing !REPO_LIST[%%i].name! ^(!REPO_LIST[%%i].priority!^)...
        
        if exist "!REPO_LIST[%%i].name!" (
            echo   → Repository already exists, skipping clone...
            set /a SKIPPED_COUNT+=1
        ) else (
            echo   → Cloning !REPO_LIST[%%i].name!...
            git clone !REPO_LIST[%%i].url! >nul 2>&1
            if !ERRORLEVEL! equ 0 (
                set /a CLONED_COUNT+=1
            ) else (
                echo   ✗ Failed to clone !REPO_LIST[%%i].name!
                set /a ERROR_COUNT+=1
            )
        )
        
        if exist "!REPO_LIST[%%i].name!" (
            if not "!REPO_LIST[%%i].version!"=="latest" (
                cd !REPO_LIST[%%i].name!
                echo   → Checking out !REPO_LIST[%%i].version!...
                git fetch --tags >nul 2>&1
                git checkout !REPO_LIST[%%i].version! >nul 2>&1
                if !ERRORLEVEL! neq 0 (
                    echo   ⚠ Warning: Could not checkout !REPO_LIST[%%i].version!, staying on default branch
                )
                cd ..
            )
        )
        echo.
    )
)

echo ========================================
echo Installation Complete!
echo ========================================
echo Cloned: %CLONED_COUNT%
echo Skipped (already exist): %SKIPPED_COUNT%
echo Errors: %ERROR_COUNT%
echo.

if %ERROR_COUNT% equ 0 (
    echo ✓ Harmony 3 installation completed successfully!
    echo.
    echo Your MCC HarmonyContent directory is now ready at:
    echo %HARMONY_ROOT%
    echo.
    echo This installation includes:
    if %INSTALL_REQUIRED% equ 1 echo - All repositories required for T1S 100BaseT Bridge project
    if %INSTALL_STANDARD% equ 1 echo - Standard Harmony 3 libraries for typical MCC usage
    if %INSTALL_OPTIONAL% equ 1 echo - Optional libraries for extended functionality
    echo.
    echo You can now use MPLAB X MCC with full library support!
) else (
    echo ⚠ Installation completed with %ERROR_COUNT% errors.
    echo Some repositories could not be cloned. Check your internet connection.
    echo The essential repositories for your T1S project should still work.
)

echo.
pause
exit /b %ERROR_COUNT%

:error
echo.
echo Installation failed! Please resolve the issues above.
pause
exit /b 1