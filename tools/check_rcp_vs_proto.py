#!/usr/bin/env python3
"""check_rcp_vs_proto.py - drift checker, NOT a code generator.

Compares what the toolset actually sends/declares against the authoritative
SOME/IP service definition `lan866x.proto`:

  * method IDs used in   src/rcp.c                  (rcp_xfer/rcp_async_request)
  * request/reply structs in include/lan866x_common.h

against the `rpc ... { method_id: 0x.... }` and `message { ... }` blocks in the
proto. It only *reports* mismatches - it never writes C. The proto is NDA and
lives outside the repo (under EVB/, gitignored), so its path must be passed in.

Why a checker and not a generator: rcp.c is ~780 hand-tuned lines (async API,
reply-drop pacing, the host-side gotchas) that a generator can't produce. The
only thing that silently drifts is the mechanical method-ID / struct layer -
this catches that drift at a fraction of a generator's cost. See
docs/comparision.md for the rationale.

Usage:
    python tools/check_rcp_vs_proto.py --proto <path-to-lan866x.proto> [-v]
    python tools/check_rcp_vs_proto.py            # uses $LAN866X_PROTO if set

Exit code 0 = clean, 1 = drift found (suitable for CI / pre-commit).
"""
import argparse
import os
import re
import sys

# proto (someip).int_type  ->  expected C scalar type in lan866x_common.h
INT_TYPE_TO_C = {
    "UINT8": "uint8_t", "UINT16": "uint16_t", "UINT32": "uint32_t",
    "UINT64": "uint64_t", "INT8": "int8_t", "INT16": "int16_t",
    "INT32": "int32_t", "INT64": "int64_t", "BOOL": "uint8_t",
}

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Method IDs the toolset deliberately sends that are NOT in the v1.10.0 proto:
# the toolset targets firmware V1.3.2/V1.4.0, which numbers ADC and the explicit
# Close* methods differently. This is documented in docs/INTEGRATION_NOTES.md
# ("DIVERGENCE - the v1.10.0 proto renumbers ADC and drops the explicit Close*").
# These are baseline-known, so they do NOT fail the gate; a *new* unknown ID will.
KNOWN_EXTRA_IDS = {
    0x1202: "CloseI2C (firmware V1.3.2/V1.4.0; absent from v1.10 proto)",
    0x1502: "CloseSpi (firmware V1.3.2/V1.4.0; absent from v1.10 proto)",
    0x1720: "ReadAdc  (firmware V1.3.2/V1.4.0; v1.10 proto renumbers it to 0x1702)",
    0x1802: "ClosePwm (firmware V1.3.2/V1.4.0; absent from v1.10 proto)",
}


# --------------------------------------------------------------------------- #
#  parsers
# --------------------------------------------------------------------------- #
def parse_proto(text):
    """-> (methods{name:{id,in,out}}, id_to_name{int:name}, messages{name:[field]})."""
    methods, id_to_name = {}, {}
    rpc_re = re.compile(
        r"rpc\s+(\w+)\s*\(\s*(\w+)\s*\)\s*returns\s*\(\s*(\w+)\s*\)\s*\{(.*?)\}",
        re.S)
    for name, invar, outreply, body in rpc_re.findall(text):
        idm = re.search(r"method_id:\s*(0x[0-9A-Fa-f]+|\d+)", body)
        mid = int(idm.group(1), 0) if idm else None
        methods[name] = {"id": mid, "in": invar, "out": outreply}
        if mid is not None:
            id_to_name[mid] = name

    messages = {}
    field_re = re.compile(
        r"(repeated\s+)?(\w+)\s+(\w+)\s*=\s*(\d+)\s*\[(.*)\]")
    for mname, body in re.findall(r"message\s+(\w+)\s*\{(.*?)\}", text, re.S):
        fields = []
        for line in body.splitlines():
            fm = field_re.search(line.strip())
            if not fm:
                continue
            rep, _ptype, fname, fnum, attrs = fm.groups()
            it = re.search(r"int_type\s*=\s*(\w+)", attrs)
            asz = re.search(r"array_size\s*=\s*(\d+)", attrs)
            fields.append({
                "name": fname, "num": int(fnum),
                "int_type": it.group(1) if it else None,
                "repeated": bool(rep),
                "array_size": int(asz.group(1)) if asz else None,
            })
        messages[mname] = fields
    return methods, id_to_name, messages


def parse_rcp_ids(text):
    """-> list of (method_id:int, enclosing_func:str, lineno:int).

    Function *definitions* start at column 0 (`ReturnCode_t rcp_x(...)`); calls
    are indented - that distinction keeps `return rcp_xfer(...)` from being
    mistaken for a definition."""
    used, func = [], None
    def_re = re.compile(r"^(?:static\s+)?[A-Za-z_][\w \*]*\s+(rcp_\w+)\s*\(")
    call_re = re.compile(r"rcp_(?:xfer|async_request)\s*\(\s*(0x[0-9A-Fa-f]+)")
    for i, line in enumerate(text.splitlines(), 1):
        if line[:1] not in (" ", "\t"):
            dm = def_re.match(line)
            if dm:
                func = dm.group(1)
        for cm in call_re.finditer(line):
            used.append((int(cm.group(1), 0), func, i))
    return used


def parse_structs(text):
    """-> {struct_name_without_t: [ {ctype,name,array} ]}."""
    structs = {}
    fld_re = re.compile(r"([A-Za-z_]\w*)\s+([A-Za-z_]\w*)\s*(?:\[(\d+)\])?$")
    for body, name in re.findall(
            r"typedef\s+struct\s*\{(.*?)\}\s*(\w+)_t\s*;", text, re.S):
        fields = []
        for line in body.splitlines():
            line = line.split("///")[0].split("/*")[0].strip().rstrip(";").strip()
            if not line:
                continue
            fm = fld_re.match(line)
            if not fm:
                continue
            ctype, fname, arr = fm.groups()
            fields.append({"ctype": ctype, "name": fname,
                           "array": int(arr) if arr else None})
        structs[name] = fields
    return structs


# --------------------------------------------------------------------------- #
#  checks
# --------------------------------------------------------------------------- #
def check_method_ids(used, id_to_name):
    """C1: every method ID rcp.c sends must exist in the proto.

    Returns (unknown, known): unknown IDs fail the gate; IDs on the
    KNOWN_EXTRA_IDS baseline are reported informationally."""
    unknown, known = [], []
    valid = set(id_to_name)
    for mid, func, line in sorted(set(used)):
        if mid in valid:
            continue
        if mid in KNOWN_EXTRA_IDS:
            known.append(f"0x{mid:04X} in {func or '?'}() - {KNOWN_EXTRA_IDS[mid]}")
        else:
            unknown.append(
                f"rcp.c:{line}  0x{mid:04X} in {func or '?'}() "
                f"is NOT in the proto and NOT on the known-extras baseline "
                f"(new drift? firmware-specific? typo?)")
    return unknown, known


def check_structs(structs, messages):
    """C2: compare each <Name>_t struct against its proto message.

    Returns (hard, skew):
      * hard - type / array-size mismatches on a *shared* field name. These are
        the dangerous ones: a field that changed type silently breaks the WTLV
        encoding (gotcha #1 -> E_MALFORMED_MESSAGE). Part of the exit gate.
      * skew - field-set differences (a name on one side only). Mostly expected
        version skew: include/lan866x_common.h tracks firmware V1.3.2/V1.4.0,
        the proto is SDK v1.10.0. Informational, not gated.
    """
    hard, skew = [], []
    for sname, sfields in sorted(structs.items()):
        if sname not in messages:
            continue  # helper struct with no proto message - not our concern
        pf = messages[sname]
        by_name = {f["name"]: f for f in sfields}
        absent = 0
        for p in pf:
            # nested sub-message fields (repeated <Message>, no int_type) are
            # beyond this flat parser - skip rather than false-flag.
            if p["int_type"] is None and p["array_size"] is None:
                continue
            cf = by_name.get(p["name"])
            if cf is None:
                absent += 1
                continue
            if p["repeated"]:
                if cf["array"] is None:
                    hard.append(
                        f"{sname}_t.{p['name']}: proto is an array "
                        f"(array_size={p['array_size']}) but member is scalar")
                elif p["array_size"] and cf["array"] != p["array_size"]:
                    hard.append(
                        f"{sname}_t.{p['name']}: array size {cf['array']} "
                        f"!= proto array_size {p['array_size']}")
            else:
                want = INT_TYPE_TO_C.get(p["int_type"])
                if want and cf["ctype"] != want:
                    hard.append(
                        f"{sname}_t.{p['name']}: type '{cf['ctype']}' "
                        f"!= proto int_type {p['int_type']} (want {want})")
        if absent:
            skew.append(f"{sname}_t: {absent} proto field(s) absent from struct "
                        f"(older-firmware subset?)")
        # struct members the proto doesn't know (ignore the generated *Length
        # companions of repeated fields)
        pnames = {f["name"] for f in pf}
        plens = {f["name"] + "Length" for f in pf if f["repeated"]}
        extra = [cf["name"] for cf in sfields
                 if cf["name"] not in pnames and cf["name"] not in plens]
        if extra:
            skew.append(f"{sname}_t: member(s) not in proto: {', '.join(extra)}")
    return hard, skew


# --------------------------------------------------------------------------- #
#  main
# --------------------------------------------------------------------------- #
def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--proto", default=os.environ.get("LAN866X_PROTO"),
                    help="path to lan866x.proto (NDA, under EVB/). "
                         "Defaults to $LAN866X_PROTO.")
    ap.add_argument("--rcp", default=os.path.join(REPO, "src", "rcp.c"))
    ap.add_argument("--common", default=os.path.join(REPO, "include",
                                                     "lan866x_common.h"))
    ap.add_argument("-v", "--verbose", action="store_true",
                    help="also list proto methods the toolset does not use")
    args = ap.parse_args()

    if not args.proto or not os.path.isfile(args.proto):
        sys.exit("error: proto not found. Pass --proto <path> or set "
                 "$LAN866X_PROTO (it lives under EVB/, outside the repo).")

    proto_txt = open(args.proto, encoding="utf-8", errors="replace").read()
    rcp_txt = open(args.rcp, encoding="utf-8", errors="replace").read()
    common_txt = open(args.common, encoding="utf-8", errors="replace").read()

    methods, id_to_name, messages = parse_proto(proto_txt)
    used = parse_rcp_ids(rcp_txt)
    structs = parse_structs(common_txt)

    print(f"proto:   {args.proto}")
    print(f"         {len(methods)} methods, {len(messages)} messages")
    print(f"rcp.c:   {len(set(m for m, _, _ in used))} distinct method IDs sent")
    print(f"common.h:{len(structs)} structs\n")

    unknown, known = check_method_ids(used, id_to_name)
    hard, skew = check_structs(structs, messages)

    if unknown:
        print(f"[C1] method-ID drift ({len(unknown)}):")
        for p in unknown:
            print("   - " + p)
    else:
        print("[C1] method IDs: no unknown IDs (all are in the proto or on the "
              "known-extras baseline) - OK")
    if known:
        print(f"[C1-known] {len(known)} documented firmware-vs-proto divergence(s):")
        for p in known:
            print("   . " + p)
    print()
    if hard:
        print(f"[C2] struct TYPE drift ({len(hard)}) - dangerous (gotcha #1):")
        for p in hard:
            print("   - " + p)
    else:
        print("[C2] struct types: every shared field agrees with the proto - OK")
    if skew:
        print(f"\n[C2-info] {len(skew)} struct(s) differ in field SET "
              f"(expected version skew, not gated):")
        for p in skew:
            print("   . " + p)

    if args.verbose:
        used_ids = {m for m, _, _ in used}
        unused = sorted((mid, n) for mid, n in id_to_name.items()
                        if mid not in used_ids)
        print(f"\n[info] {len(unused)} proto methods not used by the toolset "
              f"(expected - it uses a subset):")
        for mid, n in unused:
            print(f"   0x{mid:04X}  {n}")

    gated = len(unknown) + len(hard)
    print(f"\n{'DRIFT: ' + str(gated) + ' gated finding(s)' if gated else 'clean'}"
          f" ({len(known)} known divergence(s), {len(skew)} skew note(s))")
    sys.exit(1 if gated else 0)


if __name__ == "__main__":
    main()
