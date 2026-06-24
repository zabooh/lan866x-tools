@echo off
setlocal enabledelayedexpansion

set "SRC=%~dp0"
set "TARGET=%APPDATA%\Wireshark"

if not exist "%TARGET%" (
    echo Creating Wireshark config directory: %TARGET%
    mkdir "%TARGET%" || (
        echo ERROR: could not create "%TARGET%"
        exit /b 1
    )
)

echo Installing LAN866X SOME/IP dissector files to %TARGET%
for %%F in (SOMEIP_service_identifiers SOMEIP_method_event_identifiers SOMEIP_eventgroup_identifiers SOMEIP_parameter_base_types SOMEIP_parameter_strings SOMEIP_parameter_arrays SOMEIP_parameter_structs SOMEIP_parameter_list) do (
    copy /Y "%SRC%%%F" "%TARGET%\" >nul
    if errorlevel 1 (
        echo ERROR: failed to copy %%F
        exit /b 1
    )
    echo   %%F
)

echo.
echo Done. Restart Wireshark to pick up the new dissector configuration.
echo Then open a LAN866X SOME/IP capture and payload fields will be
echo rendered by name instead of "Unparsed Payload".
