@echo off
REM ============================================================================
REM MCC Harmony Repository Restore Script
REM Generated on 2026-03-08
REM Restores all repositories to their original state as of backup creation
REM ============================================================================

echo.
echo Restoring MCC Harmony Repositories to Original State...
echo ============================================================================
set HARMONY_PATH=C:\Users\M91221\.mcc\HarmonyContent
set ERROR_COUNT=0

REM Enable delayed variable expansion
setlocal enabledelayedexpansion

echo.
echo WARNING: This will reset all repositories to their backed-up state!
echo Any uncommitted changes will be lost!
echo.
set /p CONFIRM=Continue? (y/N): 
if /i "!CONFIRM!" neq "y" (
    echo Operation cancelled by user.
    exit /b 0
)

echo.
echo Proceeding with repository restore...
echo ============================================================================

echo [1/11] Restoring CMSIS_5
cd /d "%HARMONY_PATH%\CMSIS_5"
git checkout --detach 2b7495b8535bdcb306dac29b9ded4cfb679d7e5c
if %errorlevel% neq 0 (
    echo   ERROR: Failed to restore CMSIS_5
    set /a ERROR_COUNT+=1
) else (
    echo   OK: CMSIS_5 restored to commit 2b7495b8 (tag: 5.9.0)
)

echo [2/11] Restoring core
cd /d "%HARMONY_PATH%\core"
git checkout --detach 8b89198006778f4580d4c027759d6ab14dd9f939
if %errorlevel% neq 0 (
    echo   ERROR: Failed to restore core
    set /a ERROR_COUNT+=1
) else (
    echo   OK: core restored to commit 8b891980 (tag: v3.13.4)
)

echo [3/11] Restoring crypto
cd /d "%HARMONY_PATH%\crypto"
git checkout --detach fbbfcea289720d518450e8135db691841cd1c5bb
if %errorlevel% neq 0 (
    echo   ERROR: Failed to restore crypto
    set /a ERROR_COUNT+=1
) else (
    echo   OK: crypto restored to commit fbbfcea2 (tag: v3.8.1)
)

echo [4/11] Restoring csp
cd /d "%HARMONY_PATH%\csp"
git checkout --detach 436970f8d4b9b49fdbba0b4c53f4b188530c62ca
if %errorlevel% neq 0 (
    echo   ERROR: Failed to restore csp
    set /a ERROR_COUNT+=1
) else (
    echo   OK: csp restored to commit 436970f8 (tag: v3.18.5)
)

echo [5/11] Restoring Devices
cd /d "%HARMONY_PATH%\Devices"
git checkout master
if %errorlevel% neq 0 (
    echo   ERROR: Failed to checkout master branch for Devices
    set /a ERROR_COUNT+=1
) else (
    git reset --hard 9058a396b647ec87618330bf3f611262c0cab919
    if !errorlevel! neq 0 (
        echo   ERROR: Failed to reset Devices to commit 9058a396
        set /a ERROR_COUNT+=1
    ) else (
        echo   OK: Devices restored to master at commit 9058a396
    )
)

echo [6/11] Restoring dev_packs
cd /d "%HARMONY_PATH%\dev_packs"
git checkout --detach 345dc12d42a4fdec72117b64a8c3527023bdeca9
if %errorlevel% neq 0 (
    echo   ERROR: Failed to restore dev_packs
    set /a ERROR_COUNT+=1
) else (
    echo   OK: dev_packs restored to commit 345dc12d (tag: v3.18.1)
)

echo [7/11] Restoring harmony-services
cd /d "%HARMONY_PATH%\harmony-services"
git checkout --detach 98e521c5cd9aea14a41c276d87a17e93a33b6882
if %errorlevel% neq 0 (
    echo   ERROR: Failed to restore harmony-services
    set /a ERROR_COUNT+=1
) else (
    echo   OK: harmony-services restored to commit 98e521c5 (tag: v1.5.0)
)

echo [8/11] Restoring net
cd /d "%HARMONY_PATH%\net"
git checkout --detach db2cda9da57d5f5d1a65152941ed1267253c4283
if %errorlevel% neq 0 (
    echo   ERROR: Failed to restore net
    set /a ERROR_COUNT+=1
) else (
    echo   OK: net restored to commit db2cda9d (tag: v3.11.1)
)

echo [9/11] Restoring net_10base_t1s
cd /d "%HARMONY_PATH%\net_10base_t1s"
git checkout --detach 3e0658a53c1580e701219649a8c8c8830d5cc246
if %errorlevel% neq 0 (
    echo   ERROR: Failed to restore net_10base_t1s
    set /a ERROR_COUNT+=1
) else (
    echo   OK: net_10base_t1s restored to commit 3e0658a5 (tag: v1.3.2)
)

echo [10/11] Restoring quick_docs
cd /d "%HARMONY_PATH%\quick_docs"
git checkout --detach bc5d51419e57144a7c105e70e457d08e8628f834
if %errorlevel% neq 0 (
    echo   ERROR: Failed to restore quick_docs
    set /a ERROR_COUNT+=1
) else (
    echo   OK: quick_docs restored to commit bc5d5141 (tag: v1.8.0)
)

echo [11/11] Restoring wolfssl
cd /d "%HARMONY_PATH%\wolfssl"
git checkout --detach b222861401aedd251e72498c3e000ed4686a5717
if %errorlevel% neq 0 (
    echo   ERROR: Failed to restore wolfssl
    set /a ERROR_COUNT+=1
) else (
    echo   OK: wolfssl restored to commit b2228614 (tag: v5.4.0)
)

echo.
echo ============================================================================
if %ERROR_COUNT% equ 0 (
    echo SUCCESS: All repositories have been restored to original state!
    echo.
    echo Repository Versions:
    echo   CMSIS_5:           5.9.0
    echo   core:              v3.13.4
    echo   crypto:            v3.8.1
    echo   csp:               v3.18.5
    echo   Devices:           master (9058a396)
    echo   dev_packs:         v3.18.1   
    echo   harmony-services:  v1.5.0
    echo   net:               v3.11.1
    echo   net_10base_t1s:    v1.3.2
    echo   quick_docs:        v1.8.0
    echo   wolfssl:           v5.4.0
    echo.
    exit /b 0
) else (
    echo ERRORS: %ERROR_COUNT% repositories could not be restored!
    echo Check the error messages above for details.
    exit /b 1
)

pause