@echo off
echo Starting MPLAB X IDE for MCC...
cd /d "%~dp0"
start "" "C:\Program Files\Microchip\MPLABX\v6.25\mplab_platform\bin\mplab_ide64.exe" --open "T1S_100BaseT_Bridge.mc3"
echo MPLAB X started. Open MCC via: Tools -> Embedded -> MPLAB Code Configurator
pause