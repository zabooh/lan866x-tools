#!/bin/bash
# MCC-focused MPLAB X Setup
# Ziel: Minimaler MPLAB X Fußabdruck

# 1. MPLAB X installieren (unvermeidbar)
# 2. Content downloaden (einmalig)
# 3. MCC Shortcuts erstellen

# Windows Batch equivalent:
# start_mcc_only.bat
@echo off
echo Starting MCC-focused environment...
cd /d "C:\work\t1s\t1s_100baset_bridge\firmware\T1S_100BaseT_Bridge.X"
start "" "C:\Program Files\Microchip\MPLABX\v6.25\mplab_platform\bin\mplab_ide64.exe" --open "T1S_100BaseT_Bridge.mc3"
timeout /t 5
echo Close MPLAB X after MCC configuration
echo Continue development in VS Code
pause