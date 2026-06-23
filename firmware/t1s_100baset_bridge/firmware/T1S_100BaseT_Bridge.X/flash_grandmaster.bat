@echo off
setlocal

set SERIAL=ATML3264031800001290
set HEX=%~dp0out\T1S_100BaseT_Bridge\default.hex

python "%~dp0mdb_flash.py" --hex "%HEX%" --serial %SERIAL% --label GRANDMASTER

exit /b %ERRORLEVEL%
