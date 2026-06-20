# Firmware trains — V1.3.2 vs V1.4.0 (what changed, and the flash implications)

Facts-only comparison of the two LAN866x endpoint firmware **release trains** that
matter for this board, reconstructed from the package metadata (`package.pdsc`,
config XML, image hashes), the **SOME/IP client SDK v1.10.0** and the **V1.4.x
Endpoint User's Guide** (`60001925a`).

> **No formal changelog exists.** The V1.4.x User's Guide is a *first edition*
> ("Initial version for V1.4.X"); it describes the 1.4.x feature set but **not** as a
> delta from 1.3.x, and the packages contain no release notes. Everything below is
> derived from comparing the two packages + the SDK proto. NDA sources live locally
> under `EVB/` (git-ignored) — only versions/IDs are repeated here, no datasheet text.

The board ships on the **1.3.x** train; all tools/examples in this repo target it.
Current state (live): `LAN8661-ws2812 V1.3.2-64`, bootloader `V1.3.1-60`, COMO
`V2.1.1`, SOME/IP service **V1.8** (`0x00010800`), IP `.54` / PLCA node 4.

---

## 1. Version delta (LAN8660 Control package as the example)

| Component | **V1.3.2** | **V1.4.0** |
|---|---|---|
| Main app | `LAN8660-main_V1.3.2-64` | `LAN8660-main_V1.4.0-70` |
| **Bootloader** | `LAN866x-bootloader_V1.3.1-60` | `LAN866x-bootloader_V1.4.0-66` |
| Updater | `LAN866x-updater_V1.3.0-58` | `LAN866x-updater_V1.4.0-63` |
| COMO (main / boot) | `V2.1.1` / `V2.1.0` | `V2.4.0` / `V2.4.0` |
| SOME/IP service interface | **V1.8** | **V1.10.0** |
| Package keywords | `MCHP, LAN8660` | `MCHP, LAN8660, `**`SIGNATURE-48`** |
| Upgrade package keywords | — | `…, `**`SIGNATURE-48, NON-REVERSIBLE, BOOT-SIGNATURE-32`** |

**Binary content** (SHA-256): every image differs (main app, config, bootloader,
updater) as expected. **Config encryption key: identical**; **config signature key
(`.pem`): different** → the signing material changed in 1.4.0.

**Default config: identical.** `configuration/main/config.xml` and
`configuration/bootloader/config.xml` are byte-for-byte the same in both Control
packages (IP `192.168.0.50`, PLCA node 1, empty `<Device/>`, `SomeIpEndpointService`).
So 1.4.0 brings **no different default pins/peripherals/identity** — peripheral access
stays fully dynamic via the RCP `Open*` methods.

---

## 2. What 1.4.0 adds / improves

### 2.1 SOME/IP service V1.8 → V1.10 (new RCP methods & events)
The most tangible change. Beyond the 1.3.x method set this repo wraps, the V1.10.0
service adds (IDs and the verified-vs-proto caveat: see
[INTEGRATION_NOTES.md](INTEGRATION_NOTES.md#additional-method-ids--from-the-v1100-client-sdk-proto)):

- **GPIO events / capture / debounce / timestamping:** `OpenDebouncedGpio`,
  `GetGpioEvents`, `EnableGpioPulseEvent` / `EnableGpioCaptureEvent` /
  `DisableGpioEvent`, `SetAndGetGpio` — i.e. the "event triggering and time stamping"
  the User's Guide overview calls out.
- **ADC threshold events:** `EnableAdcEvent` / `DisableAdcEvent` + event `OnAdcEvent`.
- **Fire-and-forget (no-response, lower latency) writes:** `WriteI2CFireAndForget`,
  `WriteUartFireAndForget`, `WritePwmFireAndForget`, `SetGpioFireAndForget`.
- **Robustness:** `ClearI2CBus` (free a stuck SDA), `WriteAndReadSpiTimeout`.
- **Diagnostics:** `GetHealthStatus`, `ClearNetworkCounters`, TD-measurement event
  `OnTDMeasurementCompleted`.

### 2.2 Security / boot
- New **bootloader** (`V1.4.0-66`), new **config-signature key**, and the new
  **`SIGNATURE-48`** + **`BOOT-SIGNATURE-32`** schemes → changed image/boot signing.
- The upgrade is flagged **`NON-REVERSIBLE`** → anti-rollback: once on 1.4.0 you
  **cannot return to 1.3.x**.

### 2.3 Configuration model
- COMO **V2.1 → V2.4**; the `como.xml`/`xsd` grew ~60 % → more configuration options
  (new peripheral/feature parameters).

### 2.4 Silicon family coverage
- The V1.4.x guide covers **LAN8660/1/2 *and* LAN8660X/1X/2X** — the integrated-PHY
  variants (no external PMD/LAN8680). 1.4.x is the first guide edition to include the
  `…X` family.

---

## 3. Flashing implications (important)

**To run the 1.4.0 app you must also upgrade the bootloader.** A 1.4.0-signed
app/config will not verify against the current `V1.3.1-60` bootloader (changed signing
→ `FinishUpdate` reject / fallback to bootloader). Microchip ships a dedicated
**`LAN866x_UPGRADE.mchpkg`** that updates bootloader + updater + keys + main app for
the 1.3.x → 1.4.0 transition.

- ⚠️ **`NON-REVERSIBLE`** — after the upgrade you can never flash 1.3.x again; the
  current working Lighting V1.3.2 demo state would be **permanently lost**.
- **`lan866x-flashpkg` cannot do it** — it flashes only `main/app` + `main/config`,
  never `updater/*` (bootloader). The bootloader/key upgrade needs the official
  upgrade tooling (or a custom `flashimg` flow over the `updater/*` images).
- Flashing **just** `LAN8660_V1.4.0_RELEASE.mchpkg` (app only) onto the 1.3.1
  bootloader is expected to fail verification.

**Recommendation: stay on V1.3.2 for this board** unless a specific 1.4.0 feature is
required and the one-way bootloader upgrade is acceptable.

> Related trap (already fixed in the tools): the **bootloader IP can differ from the
> main-app IP** — see [INTEGRATION_NOTES.md](INTEGRATION_NOTES.md) and the SD re-acquire
> in [flashpkg.c](../flashpkg.c) / [boot.c](../boot.c) ([tool_common.h](../src/tool_common.h)).

---

## 4. Sources

- Package metadata: `package.pdsc`, `configuration/*/config.xml` inside the `.mchpkg`
  files under `EVB/.../Firmware/reference-configs/{v1.3.2,v1.4.0}/` (NDA, local).
- SOME/IP method/event IDs: [docs/INTEGRATION_NOTES.md](INTEGRATION_NOTES.md) (from the
  `lan866x.proto` of the v1.10.0 SDK) and [docs/RCP_API.md](RCP_API.md).
- **V1.4.x Endpoint User's Guide** `60001925a` (NDA, `EVB/Documentation/`).
- Live `lan866x-discovery` / `lan866x-servicetest` output against the board.
