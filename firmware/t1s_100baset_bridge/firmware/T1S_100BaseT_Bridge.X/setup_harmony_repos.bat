@echo off
REM T1S 100BaseT Bridge - Harmony 3 Repository Setup
REM This script clones all required Harmony 3 repositories with their specific versions
REM Based on harmony-manifest-success.yml

echo Setting up Harmony 3 repositories for T1S 100BaseT Bridge project...

REM Create main Harmony 3 directory structure
set HARMONY_ROOT=C:\Users\M91221\.mcc\HarmonyContent
if not exist "%HARMONY_ROOT%" (
    echo Creating Harmony 3 root directory: %HARMONY_ROOT%
    mkdir "%HARMONY_ROOT%"
)

cd /d "%HARMONY_ROOT%"
echo Working in: %CD%

REM Function to clone and checkout specific version
setlocal EnableDelayedExpansion

echo.
echo ========================================
echo Cloning Harmony 3 Core Libraries
echo ========================================

REM Core (v3.13.4)
echo.
echo [1/7] Cloning core v3.13.4...
if not exist "core" (
    git clone https://github.com/Microchip-MPLAB-Harmony/core.git
    if !ERRORLEVEL! neq 0 (
        echo ERROR: Failed to clone core repository
        goto :error
    )
)
cd core
git checkout v3.13.4
if !ERRORLEVEL! neq 0 (
    echo ERROR: Failed to checkout core v3.13.4
    goto :error
)
cd ..

REM CSP - Chip Support Package (v3.18.5)
echo.
echo [2/7] Cloning csp v3.18.5...
if not exist "csp" (
    git clone https://github.com/Microchip-MPLAB-Harmony/csp.git
    if !ERRORLEVEL! neq 0 (
        echo ERROR: Failed to clone csp repository
        goto :error
    )
)
cd csp
git checkout v3.18.5
if !ERRORLEVEL! neq 0 (
    echo ERROR: Failed to checkout csp v3.18.5
    goto :error
)
cd ..

REM Dev Packs (v3.18.1)
echo.
echo [3/7] Cloning dev_packs v3.18.1...
if not exist "dev_packs" (
    git clone https://github.com/Microchip-MPLAB-Harmony/dev_packs.git
    if !ERRORLEVEL! neq 0 (
        echo ERROR: Failed to clone dev_packs repository
        goto :error
    )
)
cd dev_packs
git checkout v3.18.1
if !ERRORLEVEL! neq 0 (
    echo ERROR: Failed to checkout dev_packs v3.18.1
    goto :error
)
cd ..

echo.
echo ========================================
echo Cloning Networking Libraries
echo ========================================

REM Net - Networking (v3.11.1)
echo.
echo [4/7] Cloning net v3.11.1...
if not exist "net" (
    git clone https://github.com/Microchip-MPLAB-Harmony/net.git
    if !ERRORLEVEL! neq 0 (
        echo ERROR: Failed to clone net repository
        goto :error
    )
)
cd net
git checkout v3.11.1
if !ERRORLEVEL! neq 0 (
    echo ERROR: Failed to checkout net v3.11.1
    goto :error
)
cd ..

REM Net 10BASE-T1S (v1.3.2)
echo.
echo [5/7] Cloning net_10base_t1s v1.3.2...
if not exist "net_10base_t1s" (
    git clone https://github.com/Microchip-MPLAB-Harmony/net_10base_t1s.git
    if !ERRORLEVEL! neq 0 (
        echo ERROR: Failed to clone net_10base_t1s repository
        goto :error
    )
)
cd net_10base_t1s
git checkout v1.3.2
if !ERRORLEVEL! neq 0 (
    echo ERROR: Failed to checkout net_10base_t1s v1.3.2
    goto :error
)
cd ..

echo.
echo ========================================
echo Cloning Security Libraries
echo ========================================

REM Crypto (v3.8.1)
echo.
echo [6/7] Cloning crypto v3.8.1...
if not exist "crypto" (
    git clone https://github.com/Microchip-MPLAB-Harmony/crypto.git
    if !ERRORLEVEL! neq 0 (
        echo ERROR: Failed to clone crypto repository
        goto :error
    )
)
cd crypto
git checkout v3.8.1
if !ERRORLEVEL! neq 0 (
    echo ERROR: Failed to checkout crypto v3.8.1
    goto :error
)
cd ..

REM WolfSSL (v5.4.0)
echo.
echo [7/7] Cloning wolfssl v5.4.0...
if not exist "wolfssl" (
    git clone https://github.com/Microchip-MPLAB-Harmony/wolfssl.git
    if !ERRORLEVEL! neq 0 (
        echo ERROR: Failed to clone wolfssl repository
        goto :error
    )
)
cd wolfssl
git checkout v5.4.0
if !ERRORLEVEL! neq 0 (
    echo ERROR: Failed to checkout wolfssl v5.4.0
    goto :error
)
cd ..

echo.
echo ========================================
echo Setup Complete!
echo ========================================
echo.
echo All Harmony 3 repositories have been successfully cloned to:
echo %CD%
echo.
echo Repository Status:
echo - core:           v3.13.4
echo - csp:            v3.18.5 
echo - dev_packs:      v3.18.1
echo - net:            v3.11.1
echo - net_10base_t1s: v1.3.2
echo - crypto:         v3.8.1
echo - wolfssl:        v5.4.0
echo.
echo You can now build the T1S 100BaseT Bridge project.
echo.
pause
exit /b 0

:error
echo.
echo ========================================
echo ERROR: Setup failed!
echo ========================================
echo Please check your internet connection and Git installation.
echo Make sure you have access to the Microchip-MPLAB-Harmony GitHub repositories.
echo.
pause
exit /b 1