@echo off
echo Resetting MCC Content Manager...
taskkill /f /im "mplab_ide.exe" 2>nul
taskkill /f /im "mcc.exe" 2>nul
timeout /t 2
rmdir /s /q "%USERPROFILE%\.mcc\temp" 2>nul
del "%USERPROFILE%\.mcc\*.lock" 2>nul
echo Done. Please restart MPLAB X and try opening MCC again.
pause