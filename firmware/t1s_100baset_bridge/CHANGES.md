# Changelog

All changes since commit [`a40a78c`](../../commit/a40a78c344e9838247ceb890a7cc25c12959e8bb)
„follower added" — 2026-03-30

---

## [working dir] 2026-03-30 — PLCA node ID zur Laufzeit per PTP CLI konfigurierbar

### Hintergrund
In einer T1S PLCA-Konfiguration muss **Node 0** den Beacon senden (PLCA Coordinator).
Der PTP-Grandmaster übernimmt diese Rolle; der Follower läuft als **Node 1**.
Da die Rolle (Master / Follower) über das CLI zur Laufzeit gesetzt wird, muss
auch die PLCA-Node-ID zur Laufzeit umprogrammiert werden.

### firmware/src/app.c
- Neue statische Hilfsfunktion `plca_set_node(uint8_t nodeId)`:
  - **Schritt 1** – `PLCA_CONTROL_0` (0x0004CA01) = 0 → PLCA deaktivieren
  - **Schritt 2** – `PLCA_CONTROL_1` (0x0004CA02) = `(nodeCount << 8) | nodeId` → neue Node-ID setzen;
    `nodeCount` bleibt unverändert (`DRV_LAN865X_PLCA_NODE_COUNT_IDX0` aus `configuration.h`)
  - **Schritt 3** – `PLCA_CONTROL_0` = `(1 << 15)` → PLCA wieder aktivieren
- `cmd_ptp_mode` — `plca_set_node()` vor dem PTP-Moduswechsel aufgerufen:
  - `ptp_mode master`   → `plca_set_node(0)` → Coordinator / Beacon-Sender → `PTP_GM_Init()` → `PTP_MASTER`
  - `ptp_mode follower` → `plca_set_node(1)` → normaler PLCA-Teilnehmer → `PTP_SLAVE`
  - `ptp_mode off`      → keine PLCA-Änderung
- CLI-Hilfetext von `ptp_mode` ergänzt: `master=PLCA node 0, follower=PLCA node 1`

---

## [cbc539f] 2026-03-30 — cli added

### firmware/src/app.c
- `#include "ptp_gm_task.h"` ergänzt.
- Statischer `gmTimerHandle` (1 ms Periode) hinzugefügt; ruft `PTP_GM_Service()` auf, wenn der Bridge-Mode `PTP_MASTER` ist.
- Fünf neue CLI-Befehle registriert und implementiert:

| Befehl | Beschreibung |
|---|---|
| `ptp_mode [off\|follower\|master]` | Setzt PTP-Betriebsmodus; im Master-Modus wird `PTP_GM_Init()` aufgerufen |
| `ptp_status` | Zeigt aktuellen Modus, GM-Sync-Zähler und GM-State-Nummer |
| `ptp_interval <ms>` | Setzt das Sync-Sendeintervall des Grandmaster (Standard 125 ms) |
| `ptp_offset` | Zeigt den Follower-Zeitversatz (signed + absolut) in Nanosekunden |
| `ptp_reset` | Setzt den Follower-Servo auf den Zustand `UNINIT` zurück |

### firmware/src/PTP_FOL_task.c
- `PTP_FOL_GetMode()` — gibt aktuellen `ptpMode_t`-Wert zurück.
- `PTP_FOL_SetMode(mode)` — setzt den PTP-Modus; bei `PTP_SLAVE` wird der Servo automatisch zurückgesetzt.
- `PTP_FOL_GetOffset(pOffset, pOffsetAbs)` — liefert signierten und absoluten Offset in Nanosekunden.
- `PTP_FOL_Reset()` — ruft `resetSlaveNode()` auf (Wrapper).
- `PTP_FOL_OnFrame()` — früher Return-Guard eingebaut: Frames werden ignoriert, wenn `ptpMode != PTP_SLAVE`.

### firmware/src/PTP_FOL_task.h
- Neue öffentliche Deklarationen: `PTP_FOL_GetMode()`, `PTP_FOL_SetMode()`, `PTP_FOL_GetOffset()`, `PTP_FOL_Reset()`.
- Doku-Kommentar zu `PTP_FOL_OnFrame()` aktualisiert (Hinweis auf Mode-Guard).

### firmware/src/config/default/driver/lan865x/drv_lan865x.h
- Neuer Callback-Typ `DRV_LAN865X_RawTxCallback_t` — TX-Done-Callback für Rohrahmen.
- Neue Funktion `DRV_LAN865X_SendRawEthFrame(idx, pBuf, len, tsc, cb, pTag)` deklariert:
  - Sendet einen vollständigen Ethernet-Rahmen direkt über TC6.
  - Parameter `tsc = 0x01` aktiviert Transmit-Timestamp-Capture A (benötigt für PTP Sync).

### firmware/src/config/default/driver/lan865x/src/dynamic/drv_lan865x_api.c
- Implementierung von `DRV_LAN865X_SendRawEthFrame()` ergänzt:
  - Prüft Treiber-Index und Status (`SYS_STATUS_READY`) vor dem Senden.
  - Delegiert an `TC6_SendRawEthernetPacket()`.

---

## [7b1c94c] 2026-03-30 — readme grandmaster added

### firmware/src/ptp_gm_task.c  *(neue Datei)*
Vollständige, nicht-blockierende PTP Grandmaster (GM) Implementierung als Harmony-3-kompatibler State-Machine-Task.

**Portierungsbasis:** `noIP-SAM-E54-Curiosity-PTP-Grandmaster/firmware/src/main.c`

**Wesentliche Unterschiede zum noIP-Referenzprojekt:**

| noIP-Original | Diese Implementierung |
|---|---|
| Blockierender TC6-Registerzugriff | `DRV_LAN865X_ReadRegister()` + Callback-Flag |
| `TC6NoIP_SendEthernetPacket_TimestampA()` | `DRV_LAN865X_SendRawEthFrame(tsc=1)` |
| Systick-Timer | 1 ms Service-Calls aus Harmony `SYS_TIME` Periodic-Callback |
| Statische MAC-Adresse | Dynamisch per `TCPIP_STACK_NetAddressMac()` |

**State Machine (`gmState_t`):**

```
IDLE → WAIT_PERIOD → SEND_SYNC
  → READ_TXMCTL / WAIT_TXMCTL (TXPMDET-Polling)
  → READ_STATUS0 / WAIT_STATUS0 (TTSCA-Bit-Check)
  → READ_TTSCA_H / WAIT_TTSCA_H (Timestamp-Sekunden)
  → READ_TTSCA_L / WAIT_TTSCA_L (Timestamp-Nanosekunden)
  → WRITE_CLEAR / WAIT_CLEAR (Status-Flag löschen)
  → SEND_FOLLOWUP → WAIT_PERIOD
```

**Öffentliche API:**

| Funktion | Beschreibung |
|---|---|
| `PTP_GM_Init()` | Initialisiert Grandmaster: liest MAC, konfiguriert TX-Match-Register, MAC-Zeitinkrement, OA_CONFIG0, PADCTRL, PPS |
| `PTP_GM_Service()` | Muss 1× pro Millisekunde aufgerufen werden; treibt die State Machine |
| `PTP_GM_SetSyncInterval(ms)` | Sync-Periode dynamisch ändern (Standard `PTP_GM_SYNC_PERIOD_MS` = 125 ms) |
| `PTP_GM_GetStatus(pCnt, pState)` | Liefert Sync-Zähler und aktuellen State-Wert |

**Rahmenaufbau:**
- Sync-Rahmen: 14 Byte Ethernet-Header + 44 Byte `syncMsg_t`, TwoStep-Flag gesetzt.
- FollowUp-Rahmen: 14 Byte Ethernet-Header + 76 Byte `followUpMsg_t` inkl. Organization-Specific TLV (Typ `0x0003`, CumulativeRateRatio).
- Clock Identity: EUI-64 aus Board-MAC (`MAC[0..2] + FF FE + MAC[3..5]`).
- Ziel-MAC: PTP Layer-2 Multicast `01:80:C2:00:00:0E`, EtherType `0x88F7`.

**LAN865x Register-Konstanten** (in `ptp_gm_task.h`):

| Konstante | Adresse | Bedeutung |
|---|---|---|
| `GM_TXMCTL` | `0x000A0014` | TX Match Control |
| `GM_TXMLOC` | `0x000A0015` | TX Match Byte Offset |
| `GM_TXMPATH` / `GM_TXMPATL` | `0x000A0016/17` | TX Match Pattern |
| `GM_TXMMSKH` / `GM_TXMMSKL` | `0x000A0018/19` | TX Match Mask |
| `GM_OA_STATUS0` | `0x000F0008` | Open Alliance Status 0 |
| `GM_OA_CONFIG0` | `0x000F0004` | Open Alliance Config 0 |
| `GM_STS0_TTSCAA/B/C` | Bits 20/19/18 | Timestamp Capture Available |
| `GM_TXMCTL_TXPMDET` | Bit 1 | TX Pattern Match Detected |
| `GM_TTSCA_SEC` | `0x000A004C` | TX Timestamp Seconds |
| `GM_TTSCA_NSEC` | `0x000A004D` | TX Timestamp Nanoseconds |
| `MAC_TI` | `0x000A001A` | MAC Time Increment (ns/Takt) |
| `PADCTRL` | `0x000A0021` | Pad Timing Control |
| `PPSCTL` | `0x000A0024` | PPS Output Control |

### firmware/src/ptp_gm_task.h  *(neue Datei)*
Header mit:
- Typ-Definitionen für PTP-Nachrichtenstrukturen (`ptpHeader_t`, `syncMsg_t`, `followUpMsg_t`, `tlvField_t`).
- Register-Adress-Konstanten für den LAN865x GM-Betrieb.
- Konfigurations-Makros: `PTP_GM_SYNC_PERIOD_MS` (125), `PTP_GM_MAX_RETRIES` (20).
- Deklarationen der öffentlichen API.

### README_PTP_GRANDMASTER.md  *(aktualisiert)*
Dokumentation des PTP Grandmaster ergänzt und erweitert.

### README_PTP_TCP.md  *(aktualisiert)*
PTP TCP/Telnet-Dokumentation aktualisiert.

### ptp_architecture.dot / .svg / .png  *(neue Dateien)*
Grafische Architektur-Übersicht der PTP-Bridge (Grandmaster + Follower) als Graphviz-Quellcode und gerenderte Ausgaben.

---

## Zusammenfassung der Änderungen

| Kategorie | Details |
|---|---|
| **Neue Quelldateien** | `ptp_gm_task.c`, `ptp_gm_task.h` |
| **Erweiterte Quelldateien** | `app.c`, `PTP_FOL_task.c`, `PTP_FOL_task.h`, `drv_lan865x.h`, `drv_lan865x_api.c` |
| **Neue Funktionalität** | PTP Grandmaster (non-blocking State Machine), CLI-Befehle für PTP-Steuerung |
| **Neue Treiber-API** | `DRV_LAN865X_SendRawEthFrame()` mit TSC-Flag |
| **Dokumentation** | `README_PTP_GRANDMASTER.md`, `README_PTP_TCP.md`, Architekturdiagramme |
