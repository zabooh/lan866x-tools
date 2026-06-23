@echo off
REM T1S 100BaseT Bridge - Advanced Harmony 3 Repository Setup
REM This script provides advanced options for managing Harmony 3 repositories

setlocal EnableDelayedExpansion

REM Configuration
set HARMONY_ROOT=C:\Users\M91221\.mcc\HarmonyContent
set PROJECT_NAME=T1S 100BaseT Bridge

REM Repository definitions from harmony-manifest-success.yml
set REPOS[1].name=core
set REPOS[1].version=v3.13.4
set REPOS[1].url=https://github.com/Microchip-MPLAB-Harmony/core.git

set REPOS[2].name=csp
set REPOS[2].version=v3.18.5
set REPOS[2].url=https://github.com/Microchip-MPLAB-Harmony/csp.git

set REPOS[3].name=dev_packs
set REPOS[3].version=v3.18.1
set REPOS[3].url=https://github.com/Microchip-MPLAB-Harmony/dev_packs.git

set REPOS[4].name=net
set REPOS[4].version=v3.11.1
set REPOS[4].url=https://github.com/Microchip-MPLAB-Harmony/net.git

set REPOS[5].name=net_10base_t1s
set REPOS[5].version=v1.3.2
set REPOS[5].url=https://github.com/Microchip-MPLAB-Harmony/net_10base_t1s.git

set REPOS[6].name=crypto
set REPOS[6].version=v3.8.1
set REPOS[6].url=https://github.com/Microchip-MPLAB-Harmony/crypto.git

set REPOS[7].name=wolfssl
set REPOS[7].version=v5.4.0
set REPOS[7].url=https://github.com/Microchip-MPLAB-Harmony/wolfssl.git

set REPO_COUNT=7

echo ========================================
echo %PROJECT_NAME% - Harmony 3 Setup
echo ========================================
echo.

REM Check if Git is installed
git --version >nul 2>&1
if !ERRORLEVEL! neq 0 (
    echo ERROR: Git is not installed or not in PATH!
    echo Please install Git from https://git-scm.com/
    goto :error
)

REM Show menu
:menu
echo Select an option:
echo.
echo 1. Initial setup (clone all repositories)
echo 2. Update existing repositories
echo 3. Check repository status
echo 4. Reset all repositories to required versions
echo 5. Clean setup (delete and re-clone everything)
echo 6. Show repository information
echo 0. Exit
echo.
set /p choice="Enter your choice (0-6): "

if "%choice%"=="1" goto :initial_setup
if "%choice%"=="2" goto :update_repos
if "%choice%"=="3" goto :check_status
if "%choice%"=="4" goto :reset_repos
if "%choice%"=="5" goto :clean_setup
if "%choice%"=="6" goto :show_info
if "%choice%"=="0" goto :exit
echo Invalid choice. Please try again.
goto :menu

:initial_setup
echo.
echo ========================================
echo Initial Setup - Cloning All Repositories
echo ========================================

REM Create Harmony root directory
if not exist "%HARMONY_ROOT%" (
    echo Creating Harmony 3 root directory: %HARMONY_ROOT%
    mkdir "%HARMONY_ROOT%"
)

cd /d "%HARMONY_ROOT%"
echo Working in: %CD%

for /L %%i in (1,1,%REPO_COUNT%) do (
    echo.
    echo [%%i/%REPO_COUNT%] Processing !REPOS[%%i].name! !REPOS[%%i].version!...
    
    if exist "!REPOS[%%i].name!" (
        echo Repository !REPOS[%%i].name! already exists, skipping clone...
    ) else (
        echo Cloning !REPOS[%%i].name!...
        git clone !REPOS[%%i].url!
        if !ERRORLEVEL! neq 0 (
            echo ERROR: Failed to clone !REPOS[%%i].name!
            goto :error
        )
    )
    
    cd !REPOS[%%i].name!
    echo Checking out !REPOS[%%i].version!...
    git checkout !REPOS[%%i].version!
    if !ERRORLEVEL! neq 0 (
        echo ERROR: Failed to checkout !REPOS[%%i].name! !REPOS[%%i].version!
        goto :error
    )
    cd ..
)

echo.
echo ✓ Initial setup completed successfully!
goto :menu

:update_repos
echo.
echo ========================================
echo Updating Existing Repositories  
echo ========================================

if not exist "%HARMONY_ROOT%" (
    echo ERROR: Harmony 3 directory does not exist: %HARMONY_ROOT%
    echo Please run initial setup first.
    goto :menu
)

cd /d "%HARMONY_ROOT%"

for /L %%i in (1,1,%REPO_COUNT%) do (
    echo.
    echo [%%i/%REPO_COUNT%] Updating !REPOS[%%i].name!...
    
    if not exist "!REPOS[%%i].name!" (
        echo Repository !REPOS[%%i].name! not found, cloning...
        git clone !REPOS[%%i].url!
        if !ERRORLEVEL! neq 0 (
            echo ERROR: Failed to clone !REPOS[%%i].name!
            goto :error
        )
    )
    
    cd !REPOS[%%i].name!
    git fetch --tags
    git checkout !REPOS[%%i].version!
    if !ERRORLEVEL! neq 0 (
        echo WARNING: Could not checkout !REPOS[%%i].version! for !REPOS[%%i].name!
    )
    cd ..
)

echo.
echo ✓ Update completed!
goto :menu

:check_status
echo.
echo ========================================
echo Repository Status Check
echo ========================================

if not exist "%HARMONY_ROOT%" (
    echo ERROR: Harmony 3 directory does not exist: %HARMONY_ROOT%
    goto :menu
)

cd /d "%HARMONY_ROOT%"

echo Repository Status:
echo.

for /L %%i in (1,1,%REPO_COUNT%) do (
    if exist "!REPOS[%%i].name!" (
        cd !REPOS[%%i].name!
        for /f "tokens=*" %%a in ('git describe --tags --exact-match HEAD 2^>nul') do set current_tag=%%a
        if "!current_tag!"=="!REPOS[%%i].version!" (
            echo ✓ !REPOS[%%i].name!: !current_tag! ^(correct^)
        ) else (
            echo ✗ !REPOS[%%i].name!: !current_tag! ^(expected !REPOS[%%i].version!^)
        )
        cd ..
        set current_tag=
    ) else (
        echo ✗ !REPOS[%%i].name!: NOT FOUND
    )
)

echo.
goto :menu

:reset_repos
echo.
echo ========================================
echo Reset All Repositories
echo ========================================
echo WARNING: This will reset all repositories to the required versions.
echo Any local changes will be lost!
echo.
set /p confirm="Are you sure? (y/N): "
if /i not "%confirm%"=="y" goto :menu

if not exist "%HARMONY_ROOT%" (
    echo ERROR: Harmony 3 directory does not exist: %HARMONY_ROOT%
    goto :menu
)

cd /d "%HARMONY_ROOT%"

for /L %%i in (1,1,%REPO_COUNT%) do (
    if exist "!REPOS[%%i].name!" (
        echo Resetting !REPOS[%%i].name! to !REPOS[%%i].version!...
        cd !REPOS[%%i].name!
        git reset --hard HEAD
        git clean -fd
        git checkout !REPOS[%%i].version!
        cd ..
    )
)

echo.
echo ✓ All repositories reset to required versions!
goto :menu

:clean_setup
echo.
echo ========================================
echo Clean Setup
echo ========================================
echo WARNING: This will delete all existing repositories and re-clone them!
echo.
set /p confirm="Are you sure? (y/N): "
if /i not "%confirm%"=="y" goto :menu

if exist "%HARMONY_ROOT%" (
    echo Removing existing Harmony 3 directory...
    rmdir /s /q "%HARMONY_ROOT%"
)

goto :initial_setup

:show_info
echo.
echo ========================================
echo Repository Information
echo ========================================
echo Project: %PROJECT_NAME%
echo Harmony Root: %HARMONY_ROOT%
echo.
echo Required Repositories:
echo.

for /L %%i in (1,1,%REPO_COUNT%) do (
    echo %%i. !REPOS[%%i].name! (!REPOS[%%i].version!)
    echo    URL: !REPOS[%%i].url!
    echo.
)

echo Based on: harmony-manifest-success.yml
echo Generated: 2024-10-02T14:05:08.136+02:00
echo.
goto :menu

:error
echo.
echo Setup failed! Check the error messages above.
goto :menu

:exit
echo.
echo Goodbye!
exit /b 0