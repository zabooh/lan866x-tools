#!/usr/bin/env bash
#
# mcc_fixup.sh - reconcile the project after an MPLAB Harmony MCC regeneration.
#
# MCC overwrites the generated config/default tree and drops in build-breaking
# artifacts. This restores the two hand-edits the build needs and removes the junk:
#   1. MAX_CMD_GROUP 8 -> 16          (system/command/sys_command.h)
#   2. the eth0 mirror/SPAN hook       (DRV_LAN865X_PacketTx in drv_lan865x_api.c)
#   3. delete MCC's local linker script (breaks the TCP/IP heap under MPLAB X),
#      the mcc-manifest*.yml and harmony-templates/ junk.
# Then it sanity-checks configurations.xml still lists the SOME/IP sources.
#
# Usage:
#   ./mcc_fixup.sh            # apply the fixups + checks (no build)
#   ./mcc_fixup.sh --build    # also rebuild via build.bat (CMake; immune to the local .ld)
#   ./mcc_fixup.sh --build --flash
#
# Always build via build.bat (CMake), NOT MPLAB X: the CMake build uses the DFP
# linker script and ignores MCC's local config/default/ATSAME54P20A.ld.

set -u
FW="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"   # firmware/t1s_100baset_bridge
SRC="$FW/firmware/src/config/default"
XDIR="$FW/firmware/T1S_100BaseT_Bridge.X"
ok=0; warn=0
say() { printf '  %s\n' "$1"; }
WARN() { printf '  ! %s\n' "$1"; warn=$((warn+1)); }

echo "== mcc_fixup: reconciling after MCC regeneration =="

# --- 1) MAX_CMD_GROUP -> 16 -------------------------------------------------------
H="$SRC/system/command/sys_command.h"
if [ -f "$H" ]; then
    cur=$(grep -oE '#define[[:space:]]+MAX_CMD_GROUP[[:space:]]+[0-9]+' "$H" | grep -oE '[0-9]+$' | head -1)
    if [ "${cur:-}" = "16" ]; then
        say "MAX_CMD_GROUP already 16"
    else
        sed -i -E 's/(#define[[:space:]]+MAX_CMD_GROUP[[:space:]]+)[0-9]+/\1''16/' "$H"
        say "MAX_CMD_GROUP ${cur:-?} -> 16 (re-applied)"; ok=$((ok+1))
    fi
else
    WARN "sys_command.h not found ($H)"
fi

# --- 2) eth0 mirror/SPAN hook in DRV_LAN865X_PacketTx -----------------------------
D="$SRC/driver/lan865x/src/dynamic/drv_lan865x_api.c"
if [ -f "$D" ]; then
    if grep -q "mirror_eth0_tx_hook" "$D"; then
        say "mirror hook already present"
    elif grep -q 'SYS_ASSERT(ptrPacket, "Packet pointer invalid");' "$D"; then
        awk '
          /SYS_ASSERT\(ptrPacket, "Packet pointer invalid"\);/ && !done {
            print
            print "    /* T1S->eth1 port mirror (SPAN) hook - re-applied by mcc_fixup.sh after MCC regen. */"
            print "    {"
            print "        extern void mirror_eth0_tx_hook(TCPIP_MAC_PACKET *txPkt);"
            print "        mirror_eth0_tx_hook(ptrPacket);"
            print "    }"
            done=1; next
          }
          { print }
        ' "$D" > "$D.tmp" && mv "$D.tmp" "$D"
        say "mirror hook re-inserted into DRV_LAN865X_PacketTx"; ok=$((ok+1))
    else
        WARN "could not find the insertion anchor in drv_lan865x_api.c - re-apply the mirror hook by hand"
    fi
else
    WARN "drv_lan865x_api.c not found ($D)"
fi

# --- 3) remove MCC's build-breaking / junk artifacts ------------------------------
for f in \
    "$SRC/ATSAME54P20A.ld" \
    "$XDIR/mcc-manifest-autosave.yml" \
    "$XDIR/mcc-manifest-generated-success.yml" \
    "$FW/harmony-templates"
do
    if [ -e "$f" ]; then rm -rf "$f"; say "removed $(basename "$f")"; ok=$((ok+1)); fi
done

# --- 4) sanity: configurations.xml still has the SOME/IP sources ------------------
CX="$XDIR/nbproject/configurations.xml"
if [ -f "$CX" ]; then
    n=$(grep -c "someip-client.c\|someip_stub.c\|ntp_sync.c\|gpio_cli.c\|i2c_cli.c\|spi_cli.c\|sys_cli.c\|dncp_cli.c" "$CX")
    if [ "$n" -ge 8 ]; then
        say "configurations.xml: SOME/IP + CLI sources present ($n refs)"
    else
        WARN "configurations.xml seems to be MISSING SOME/IP/CLI sources ($n found) - check the lan866x_someip folder + include dirs"
    fi
else
    WARN "configurations.xml not found"
fi

echo "== done: $ok fix(es) applied, $warn warning(s) =="

# --- 5) optional rebuild / flash --------------------------------------------------
case " $* " in
  *" --build "*)
    echo "== build (build.bat incremental) =="
    ( cd "$FW" && ./build.bat incremental ) || { echo "BUILD FAILED"; exit 1; }
    case " $* " in *" --flash "*) ( cd "$FW" && python flash.py );; esac
    ;;
esac
[ "$warn" -eq 0 ] || echo "Review the warnings above."
