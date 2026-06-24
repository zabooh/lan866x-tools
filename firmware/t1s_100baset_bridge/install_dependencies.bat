@echo off
:: ============================================================
::  install_dependencies.bat
::  Installs all Python packages listed in requirements.txt.
::
::  Usage:
::    1. Run analyze_dependencies.py first to generate requirements.txt
::    2. Double-click this file (or run it from a command prompt)
::
::  Compatible with Windows 10 / Windows 11
:: ============================================================

setlocal enabledelayedexpansion

echo.
echo ============================================================
echo   Python Dependency Installer
echo ============================================================
echo.

:: ------------------------------------------------------------
:: 1. Check if Python is installed
:: ------------------------------------------------------------
python --version >nul 2>&1
if %errorlevel% neq 0 (
    echo [ERROR] Python is not installed or not found in PATH.
    echo.
    echo Please install Python from https://www.python.org/downloads/
    echo Make sure to check "Add Python to PATH" during installation.
    echo.
    goto :error_exit
)

for /f "tokens=*" %%v in ('python --version 2^>^&1') do set PYTHON_VERSION=%%v
echo [OK]    Found: %PYTHON_VERSION%

:: ------------------------------------------------------------
:: 2. Check if pip is available
:: ------------------------------------------------------------
python -m pip --version >nul 2>&1
if %errorlevel% neq 0 (
    echo [ERROR] pip is not available.
    echo.
    echo Try running: python -m ensurepip --upgrade
    echo.
    goto :error_exit
)

for /f "tokens=*" %%p in ('python -m pip --version 2^>^&1') do set PIP_VERSION=%%p
echo [OK]    Found: %PIP_VERSION%

:: ------------------------------------------------------------
:: 3. Check if requirements.txt exists
:: ------------------------------------------------------------
set REQUIREMENTS=%~dp0requirements.txt

if not exist "%REQUIREMENTS%" (
    echo.
    echo [ERROR] requirements.txt not found at:
    echo         %REQUIREMENTS%
    echo.
    echo Please run analyze_dependencies.py first:
    echo         python analyze_dependencies.py
    echo.
    goto :error_exit
)

echo [OK]    Found: %REQUIREMENTS%
echo.

:: ------------------------------------------------------------
:: 4. Upgrade pip to the latest version
:: ------------------------------------------------------------
echo Upgrading pip ...
python -m pip install --upgrade pip
if %errorlevel% neq 0 (
    echo [WARN]  Could not upgrade pip. Continuing with current version.
)
echo.

:: ------------------------------------------------------------
:: 5. Install packages from requirements.txt
:: ------------------------------------------------------------
echo Installing packages from requirements.txt ...
echo.
python -m pip install -r "%REQUIREMENTS%"

if %errorlevel% neq 0 (
    echo.
    echo [ERROR] One or more packages failed to install.
    echo.
    echo Possible causes:
    echo   - No internet connection or network timeout
    echo   - A package name in requirements.txt is incorrect
    echo   - A package requires a compiler that is not installed
    echo.
    echo Tip: Try running the following command manually for more details:
    echo         python -m pip install -r "%REQUIREMENTS%" -v
    echo.
    goto :error_exit
)

:: ------------------------------------------------------------
:: 6. Success
:: ------------------------------------------------------------
echo.
echo ============================================================
echo   All packages installed successfully!
echo ============================================================
echo.
echo You can now run the Python scripts in this repository.
echo.
goto :end

:error_exit
echo ============================================================
echo   Installation failed. See error messages above.
echo ============================================================
echo.
pause
exit /b 1

:end
pause
exit /b 0
