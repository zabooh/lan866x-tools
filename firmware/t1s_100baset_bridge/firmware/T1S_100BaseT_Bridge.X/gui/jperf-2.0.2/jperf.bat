@echo off
setlocal

set "SCRIPT_DIR=%~dp0"
set "JAVA_EXE=c:\Program Files\Microchip\MPLABX\v6.25\sys\java\zulu8.64.0.19-ca-fx-jre8.0.345-win_x64\bin\javaw.exe"

if not exist "%JAVA_EXE%" set "JAVA_EXE=javaw.exe"

pushd "%SCRIPT_DIR%"
"%JAVA_EXE%" -classpath "jperf.jar;lib\forms-1.1.0.jar;lib\jcommon-1.0.10.jar;lib\jfreechart-1.0.6.jar;lib\swingx-0.9.6.jar" net.nlanr.jperf.JPerf
popd