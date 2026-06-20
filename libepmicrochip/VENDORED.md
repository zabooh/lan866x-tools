# Vendored Microchip sources — provenance & local changes

`libepmicrochip/` holds **Microchip vendor code** (SOME/IP stack), obtained as a
package from Microchip's **Secure Document Platform** under the **SLA001** license.
This is why the repo must stay private (see `CLAUDE.md`).

These sources are **not pristine**: the C++ portion was removed and one config
header was retuned for an MCU footprint. This file records where the code came
from and exactly what was changed, so the snapshot can be re-diffed against a
future Microchip release.

## Origin

| Field | Value |
|---|---|
| Vendor | Microchip Technology Inc. |
| License | SLA001 (NDA — keep repo private) |
| Copyright year in headers | 2025 |
| Source package name | **TODO — fill in** (the `*.mchpkg`/zip name from the Secure Document Platform) |
| Package version | **TODO — fill in** (e.g. the Endpoint firmware / libsomeip release this matches) |
| Download date | **TODO — fill in** |

> The exact package name/version is **not** embedded in the sources (headers carry
> only the 2025 copyright). The developer who imported the package should fill in
> the three `TODO` rows above from the Secure-Document-Platform download.

## What is in the tree (after local changes)

Only the **C SOME/IP core** remains:

```
libepmicrochip/
  CMakeLists.txt
  libsomeip/
    inc/someip.h                       # public API (unmodified)
    src/someip-client.c                # core engine (unmodified)
    src/someip-gen.c   / .h            # message generator
    src/someip-pars.c  / .h            # parser
    src/someip-timer.c / .h            # timers
    src/someip-transmit.c              # transmit layer
    src/someip-common.h
    stub/someip-cfg.h                  # compile-time sizing  (LOCALLY MODIFIED)
    stub/windows-udp-handler.c / .h    # on disk, NOT built (old threaded path)
```

The build compiles `libsomeip/src/*.c` only (see top-level `CMakeLists.txt`,
`SOMEIP_C_CORE` glob). `windows-udp-handler.*` is left on disk but is not built —
the single-thread `src/plat_win.c` + `src/someip_stub.c` replace it.

## Local modifications vs. the original package

1. **C++ vendor sources removed** — commit `63ab27f`
   (*"Remove unused C++ vendor sources: repo is now 100% C"*). Deleted 5,274 lines:
   - `liblan866x/lan866x_client.cpp` + `lan866x_client_impl.hpp`
   - `librtp/rtp4175.cpp`
   - `libsomeip/stub/someip-stub.cpp`
   - the C++ headers `include/*.hpp`

   None were built after the C-only conversion. The toolset uses only the C
   libsomeip core via `src/rcp.c` + the C platform stub.

2. **`libsomeip/stub/someip-cfg.h` retuned for an MCU** — commit `070a979`
   (*"MCU-friendly default RAM sizing"*). The transmit buffer pool dominated
   static RAM (4 × 64 × 1472 B ≈ 368 kB). Reduced to MCU-appropriate values:
   - `SOMEIP_TRANSMIT_MAX_INSTANCES`     4 → 1
   - `SOMEIP_TRANSMIT_MAX_QUEUE_ENTRIES` 64 → 8
   - `MAX_CONNECTIONS_CLIENT`            16 → 8

   Payload stays 1440 B (flashpkg sends 1200-byte WriteImage chunks). Core static
   RAM drops from ~375 kB to ~15 kB. See `PORTING.md` "Footprint".

The core `libsomeip/src/*.c` and `inc/someip.h` are **unmodified** since the
initial import (`015c412`).

## Restoring the original snapshot

The full original package contents (including the removed C++ files) are
recoverable from git history — no need to keep dead code in the working tree:

```sh
# inspect what the C++-removal commit deleted
git show 63ab27f

# restore the original (pre-tuning) someip-cfg.h
git show 015c412:libepmicrochip/libsomeip/stub/someip-cfg.h

# restore a deleted C++ file if needed for diffing against a new release
git show 63ab27f^:libepmicrochip/liblan866x/lan866x_client.cpp
```
