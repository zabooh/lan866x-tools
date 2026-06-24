# Static code analysis — lan866x-tools

_Date: 2026-06-25_

Static analysis of the **first-party C** in this repository: the host tools
(`*.c` at the repo root), the shared SOME/IP client core (`src/`), and the bridge
firmware's own sources (`firmware/t1s_100baset_bridge/firmware/src/*.c`).

**Out of scope** (not analyzed — third-party / generated, and not maintained here):
`libepmicrochip/` (Microchip SLA001 SOME/IP stack), `third-party/minizip/`, the
Harmony-generated `firmware/**/config/default/**`, and `firmware/**/packs/**`.

## Tools

| Tool | Version | Use |
|---|---|---|
| **cppcheck** | 2.20.0 | primary static analyzer (`warning,performance,portability,style`) |
| **gcc `-fanalyzer`** | MinGW-w64 GCC 16.1.0 | secondary data-flow check on the shared core |

Commands:
```sh
# Host tools + shared core
cppcheck --enable=warning,performance,portability,style --std=c99 --inline-suppr \
  --suppress=missingInclude --suppress=missingIncludeSystem \
  -I src -I include -I . *.c src/rcp.c src/someip_stub.c src/plat_win.c

# Firmware first-party (Harmony headers unavailable to cppcheck -> best-effort)
cppcheck ... -I src -I firmware/t1s_100baset_bridge/firmware/src \
  firmware/t1s_100baset_bridge/firmware/src/{app,plat_h3tcpip,lan866x_cli,clickdemo_cli,gpio_cli,i2c_cli,spi_cli,sys_cli,dncp_cli}.c

# Shared core data-flow
gcc -fanalyzer -fsyntax-only -std=c99 -Wall -I src -I include \
  -I libepmicrochip/libsomeip/inc -I libepmicrochip/libsomeip/stub \
  src/rcp.c src/someip_stub.c src/plat_win.c
```

## Summary

| Severity | Host + core | Firmware | Notes |
|---|---|---|---|
| **error** | 0 | 1 → **fixed** | pointer subtraction of two linker symbols (UB) |
| **warning** | 0 | 1 | division-by-zero condition — **false positive** (value is clamped) |
| performance | 0 | 0 | — |
| portability | 1 | 0 | `intToPointerCast` in the stub — by design |
| style | ~25 | ~20 | const-correctness, variable scope, shadowing — cosmetic |

No memory-safety defects (no buffer overruns, use-after-free, leaks, null
dereferences, or uninitialised reads) were reported by either tool. `gcc
-fanalyzer` reported no data-flow issues on the shared core (only one `-Wcomment`
nit: a `/*` inside a comment at `src/rcp.c:857`).

## Errors

### 1. `app.c:974` — `subtractPointers` (FIXED)
```c
size_t total = (size_t)(&_eheap - &_heap);   // before
```
Subtracting two distinct linker symbols is, per the C standard, undefined
behaviour (the pointers address different "objects"). It works in practice
(both bound the C-runtime heap region) but is non-portable. **Fixed** by going
through integer addresses:
```c
size_t total = (size_t)((uintptr_t)&_eheap - (uintptr_t)&_heap);
```
(Introduced with the `meminfo` command; re-running cppcheck confirms it is gone.)

## Warnings

### 1. `lan866x_cli.c:246` — `zerodivcond` (false positive)
`100u * lost / probeN` — cppcheck cannot see that `probeN` is clamped to
`[1, 500]` a few lines earlier (`if (probeN < 1u) probeN = 1u;`), so it flags a
possible divide-by-zero. The divisor can never be 0; **no action needed**.

## Portability / by-design

- **`someip_stub.c:238` — `intToPointerCast`**: casting a non-zero integer literal
  to a pointer. This is in the platform-neutral SOME/IP stub by design (an opaque
  context/handle value); benign on the supported targets.

## Style (informational, low priority)

Cosmetic only — no behavioural impact. Representative items:

- **const-correctness** (`constVariable`, `constVariablePointer`,
  `constParameterPointer`): local arrays/pointers that could be `const`
  (e.g. `spi.c:63 pins`, `clickdemo.c:343 c0/c1`, `spi_cli.c` MCP3204 command
  arrays, `rcp.c` callback params). Tightening these documents intent.
- **`constParameterCallback`**: SYS_CMD / SOME/IP callback parameters flagged as
  const-able — *not* applied, because they must match the library's function-
  pointer signatures (changing them would require casting the function pointers).
- **`variableScope`**: a handful of locals (`diag.c`, `video.c`, `dncp*.c`,
  `sys_cli.c`, `clickdemo_cli.c`) could be declared in a tighter scope.
- **`shadowVariable`** (`app.c:622/637`): a local `frame` shadows the global NoIP
  `frame[60]` buffer — harmless but worth renaming for clarity.
- **`unreadVariable`** (`clickdemo.c:59`, `diag.c:72/93`, `ledscan.c:152`): values
  assigned then overwritten/unused.
- **`badBitmaskCheck`** (`clickdemo.c:246`), **`redundantAssignment`**
  (`app.c:720`, a duplicated `puc = (uint8_t*)addr;` in `DumpMem`): the duplicate
  assignment was **removed**.

## Fixes applied in this pass

1. `app.c` — pointer subtraction → `uintptr_t` arithmetic (clears the only
   `error`-severity finding).
2. `app.c` `DumpMem` — removed a duplicated `puc = (uint8_t*)addr;` assignment.

Both verified: firmware rebuilds cleanly (`build.bat`), and cppcheck no longer
reports any `error`-severity finding.

## Conclusion

The first-party code is in good shape: **zero memory-safety defects**, one real
(now fixed) portability bug, one false-positive warning, and an otherwise
style-only result. The remaining style items (const-correctness, variable scope,
shadowing) are safe to address incrementally and have no functional impact.

Some flagged patterns are intentional and documented in `CLAUDE.md` (single-thread
model, no `volatile` shared state, callback signatures fixed by the SOME/IP /
SYS_CMD APIs) — these should not be "corrected" blindly.
