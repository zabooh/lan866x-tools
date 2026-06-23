#!/usr/bin/env python3
r"""
patch_nodeid.py  <config_h_path> <node_id>

Setzt die PLCA Node-ID auf den angegebenen Wert.

Unterstuetzte Ziele:
- configuration.h: #define DRV_LAN865X_PLCA_NODE_ID_IDX0 <n>
- initialization.c: .nodeId = <expr>,

Beispiel:
  python patch_nodeid.py ..\src\config\default\configuration.h 0
    python patch_nodeid.py ..\src\config\default\initialization.c 1
  python patch_nodeid.py ..\src\config\default\configuration.h 1
"""
import re
import sys

if len(sys.argv) != 3:
    print(f"Verwendung: {sys.argv[0]} <configuration.h> <node_id>")
    sys.exit(1)

path    = sys.argv[1]
node_id = sys.argv[2]

with open(path, encoding="utf-8") as f:
    txt = f.read()

new = txt

if re.search(r"#define\s+DRV_LAN865X_PLCA_NODE_ID_IDX0\s+\d+", txt):
    new = re.sub(
        r"(#define\s+DRV_LAN865X_PLCA_NODE_ID_IDX0\s+)\d+",
        rf"\g<1>{node_id}",
        txt,
    )
elif re.search(r"(\.nodeId\s*=\s*)([^,]+)(,)", txt):
    new = re.sub(
        r"(\.nodeId\s*=\s*)([^,]+)(,)",
        rf"\g<1>{node_id}\g<3>",
        txt,
        count=1,
    )
else:
    print(f"[ERROR] Kein passendes Node-ID-Feld gefunden in {path}")
    sys.exit(1)

if new != txt:
    with open(path, "w", encoding="utf-8") as f:
        f.write(new)

print(f"  Node-ID -> {node_id}")
