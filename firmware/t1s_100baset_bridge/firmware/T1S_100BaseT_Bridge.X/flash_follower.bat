@echo off
setlocal

set SERIAL=ATML3264031800001049
set HEX=%~dp0out\T1S_100BaseT_Bridge\default.hex

python "%~dp0mdb_flash.py" --hex "%HEX%" --serial %SERIAL% --label FOLLOWER

exit /b %ERRORLEVEL%

endlocal
pause
