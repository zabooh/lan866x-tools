@echo off
REM EXPERIMENTAL: Versucht MCC ohne MPLAB X IDE zu starten
REM WARNUNG: Nicht für Produktivumgebung geeignet!

echo === MCC EXTRACTION EXPERIMENT ===
echo WARNUNG: Experimentell - kann Probleme verursachen!
echo.

REM Versuche minimales MCC ohne komplette IDE
set MCC_HOME=%USERPROFILE%\.mcc
set JAVA_HOME=C:\Program Files\Microchip\MPLABX\v6.25\sys\java\zulu11.52.13-fx-jdk11.0.13-win_x64
set MCC_CORE=%MCC_HOME%\cores\5.7.1

if not exist "%MCC_CORE%" (
    echo FEHLER: MCC Core nicht gefunden in %MCC_CORE%
    echo Bitte zuerst MPLAB X starten und Content downloaden!
    pause
    exit /b 1
)

echo Starte experimentellen MCC Core...
echo Content: %MCC_HOME%\HarmonyContent
echo Core: %MCC_CORE%
echo.

REM Das wird wahrscheinlich fehlschlagen, da Dependencies fehlen
"%JAVA_HOME%\bin\java.exe" -Dfile.encoding=UTF-8 -jar "%MCC_CORE%\mcc-core.jar" 2>nul

if %ERRORLEVEL% neq 0 (
    echo.
    echo FEHLGESCHLAGEN - Dependencies fehlen
    echo MCC benötigt MPLAB X Platform
    echo.
    echo Alternativen:
    echo 1. MPLAB X minimal nutzen
    echo 2. VM-basierte Lösung
    echo 3. Auf Web-MCC warten
)

pause