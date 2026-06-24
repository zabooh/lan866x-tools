#!/usr/bin/env sh
# Install the LAN866X SOME/IP dissector config into the user's Wireshark
# configuration directory. Override the target with WIRESHARK_CONFIG_DIR.

set -eu

SRC="$(cd "$(dirname "$0")" && pwd)"

if [ -n "${WIRESHARK_CONFIG_DIR:-}" ]; then
    TARGET="$WIRESHARK_CONFIG_DIR"
elif [ "$(uname)" = "Darwin" ]; then
    TARGET="$HOME/.config/wireshark"
else
    TARGET="$HOME/.config/wireshark"
fi

if [ ! -d "$TARGET" ]; then
    echo "Creating Wireshark config directory: $TARGET"
    mkdir -p "$TARGET"
fi

echo "Installing LAN866X SOME/IP dissector files to $TARGET"
for f in SOMEIP_service_identifiers \
         SOMEIP_method_event_identifiers \
         SOMEIP_eventgroup_identifiers \
         SOMEIP_parameter_base_types \
         SOMEIP_parameter_strings \
         SOMEIP_parameter_arrays \
         SOMEIP_parameter_structs \
         SOMEIP_parameter_list; do
    cp -f "$SRC/$f" "$TARGET/"
    echo "  $f"
done

echo
echo "Done. Restart Wireshark to pick up the new dissector configuration."
echo "Then open a LAN866X SOME/IP capture and payload fields will be"
echo "rendered by name instead of \"Unparsed Payload\"."
