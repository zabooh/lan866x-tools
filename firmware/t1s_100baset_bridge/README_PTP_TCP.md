# PTP-Integration in das T1S 100BaseT Bridge Projekt

> **Ziel dieses Dokuments:** Analyse, warum das noIP-PTP-Projekt keinen TCP/IP-Stack hat,
> warum das für echte Anwendungen unzureichend ist, und wie PTP-Funktionalität sauber
> in das Bridge-Projekt integriert werden kann.

---

## Inhaltsverzeichnis

1. [Rückblick: Warum kein TCP/IP im noIP-Projekt?](#1-rückblick-warum-kein-tcpip-im-noip-projekt)
2. [Warum TCP/IP in einer echten Anwendung notwendig ist](#2-warum-tcpip-in-einer-echten-anwendung-notwendig-ist)
3. [Das Bridge-Projekt als ideale Basis](#3-das-bridge-projekt-als-ideale-basis)
4. [Die technische Kernherausforderung: RX-Timestamp-Zugriff](#4-die-technische-kernherausforderung-rx-timestamp-zugriff)
5. [TX-Timestamp: Bereits verfügbar](#5-tx-timestamp-bereits-verfügbar)
6. [Lösungsansatz: Minimaler Treiber-Patch](#6-lösungsansatz-minimaler-treiber-patch)
7. [Gesamtarchitektur PTP im Bridge-Projekt](#7-gesamtarchitektur-ptp-im-bridge-projekt)
8. [Konkrete Integrationsstellen in app.c](#8-konkrete-integrationsstellen-in-appc)
9. [Anwendungsszenarien](#9-anwendungsszenarien)
10. [Implementierungsstatus (Stand 30. März 2026)](#10-implementierungsstatus-stand-30-märz-2026)

---

## 1. Rückblick: Warum kein TCP/IP im noIP-Projekt?

Das Repository `noIP-SAM-E54-Curiosity-PTP-Follower` (und der dazugehörige Grandmaster)
enthält eine vollständige Nanosekunden-genaue PTP-Implementierung — **bewusst ohne TCP/IP-Stack**.
Die `readme_description.md` benennt dafür drei Gründe:

### Grund 1 — PTP ist ein reines Layer-2-Protokoll

PTP (IEEE 1588 / IEEE 802.1AS) verwendet EtherType `0x88F7` und arbeitet direkt auf
Ethernet-Ebene. Weder IP noch UDP sind beteiligt. Ein TCP/IP-Stack hätte **keine inhaltliche
Funktion** — er müsste nur mit erheblichem Aufwand umgangen werden, um Frames mit
fremdem EtherType durchzulassen.

### Grund 2 — Der RX-Timestamp steckt im SPI-Footer

Der LAN8650/1 liefert den Hardware-RX-Timestamp als Teil des OA TC6 SPI-Footers.
Die TC6-Bibliothek extrahiert ihn aus dem SPI-Footer und übergibt ihn direkt als
`uint64_t *rxTimestamp` an den Callback `TC6_CB_OnRxEthernetPacket()`. Ein
zwischengeschalteter TCP/IP-Stack sieht diesen Footer nie.

### Grund 3 — Ressourceneffizienz

Ein vollständiger TCP/IP-Stack benötigt mehrere Dutzend Kilobyte RAM für Puffer und
Verbindungszustände. Für eine Anwendung, die ausschließlich zwei Ethernet-Frame-Typen
verarbeitet (Sync, Follow_Up), ist das reiner Overhead.

**Fazit:** Für die isolierte Demonstration der PTP-Hardware-Features ist der noIP-Ansatz
optimal. Für eine echte industrielle Anwendung ist er jedoch **ein Proof-of-Concept**,
kein produktionsfähiges Design.

---

## 2. Warum TCP/IP in einer echten Anwendung notwendig ist

Die noIP-Implementierung kann:
- Ethernet-Frames senden und empfangen
- PTP-Hardware-Timestamps lesen
- Die Uhr des Followers auf den Grandmaster einregeln

Die noIP-Implementierung **kann nicht**:
- Remote-Konfiguration über das Netzwerk (keine IP-Adresse, kein UDP/TCP)
- Statusüberwachung via Telnet, SNMP oder HTTP
- Software-Updates via TFTP/FTP
- Parallelen Nutzdatenverkehr (z.B. MQTT-Sensordaten) auf demselben Interface
- Integration in eine Netzwerkmanagement-Infrastruktur
- Reachability-Tests (kein Ping/ICMP)
- iperf-Bandbreitenmessungen
- Konfiguration der PLCA-Parameter ohne Neucompilierung

Für ein **Gateway zwischen T1S-Sensor-Netzwerk und 100BaseT-Backbone** mit
PTP-Zeitverteilung sind all diese Funktionen zwingend notwendig. Das Bridge-Projekt
ist der richtige Ort für diese Integration.

---

## 3. Das Bridge-Projekt als ideale Basis

Das Bridge-Projekt bietet bereits:

| Feature | Status | Relevanz für PTP |
|---|---|---|
| Harmony TCP/IP-Stack | ✅ vorhanden | Management-Zugriff, paralleles IP |
| eth0: LAN865x (T1S, eth0 = 192.168.0.200) | ✅ vorhanden | T1S-Seite = PTP-Empfang vom GM |
| eth1: GMAC (100BaseT, eth1 = 192.168.0.210) | ✅ vorhanden | 100BaseT-Seite = Uhrverteilung |
| `TCPIP_STACK_PacketHandlerRegister()` | ✅ genutzt | PTP-Frame-Hook (EtherType 0x88F7) |
| `DRV_LAN865X_ReadRegister()` | ✅ implementiert & getestet | TX-Timestamp-Register lesen |
| `DRV_LAN865X_WriteRegister()` | ✅ implementiert & getestet | Uhrkonfiguration, FTSE |
| FreeRTOS | ✅ vorhanden | Dedizierter PTP-Task möglich |
| Telnet (Port 23) | ✅ aktiv | PTP-Statusabfrage per CLI |
| PLCA-Konfiguration | ✅ vorhanden (Node ID 7) | Deterministischer Medienzugang |

Die Bridging-Grundfunktion (`fwd 1`: Layer-2-Forwarding von T1S → GMAC via
`DRV_GMAC_PacketTx()`) läuft bereits. PTP ist eine **Erweiterung** dieser Infrastruktur,
kein Neuanfang.

---

## 4. Die technische Kernherausforderung: RX-Timestamp-Zugriff

### Der kritische Fund

Der LAN865x Harmony-Treiber in diesem Projekt enthält folgende Implementierung in
`firmware/src/config/default/driver/lan865x/src/dynamic/drv_lan865x_api.c`:

```c
// Zeile ~1334
void TC6_CB_OnRxEthernetPacket(TC6_t *pInst, bool success, uint16_t len,
                                uint64_t *rxTimestamp, void *pGlobalTag)
{
    (void)pInst;
    (void)rxTimestamp;   // <-- TIMESTAMP WIRD HIER VERWORFEN
    TCPIP_MAC_PACKET *macPkt = NULL;
    // ... restliche Verarbeitung ohne Timestamp
}
```

**Der RX-Timestamp wird von der TC6-Bibliothek korrekt aus dem SPI-Footer extrahiert
und übergeben — aber der Harmony-Treiber ignoriert ihn vollständig.**

### Was die TC6-Bibliothek intern macht (tc6.c)

Die TC6-Bibliothek im Bridge-Projekt erkennt das RTSA-Bit im SPI-Footer und
baut den 64-Bit-Timestamp bereits korrekt zusammen:

```c
// tc6.c, on_rx_slice():
if (rtsa) {
    g->ts = ((uint64_t)buff[0] << 56) |
            ((uint64_t)buff[1] << 48) |
            // ... (8 Byte, Big-Endian)
            ((uint64_t)buff[7]);
    buff   = &buff[8];
    buf_len -= 8u;
}

// on_rx_done(): Timestamp wird übergeben...
uint64_t *pTS = (0u != g->ts) ? &g->ts : NULL;
TC6_CB_OnRxEthernetPacket(g, true, g->buf_len, pTS, g->gTag);
// ...und im Harmony-Treiber sofort wieder verworfen.
```

Die TC6-Bibliothek tut bereits alles richtig. Das Problem ist ausschließlich in
`drv_lan865x_api.c` lokalisiert — **eine einzige Funktion, wenige Zeilen Code**.

### Warum ist der Timestamp 64 Bit breit?

```
Bit [63:32]  — 32 Bit: Sekunden (entspricht MAC_TSH-Ausschnitt)
Bit [31: 0]  — 32 Bit: Nanosekunden
```

PTP-Präzision im Nanosekundenbereich erfordert beide Felder. Der `TCPIP_MAC_PACKET`
Standard-Header in Harmony enthält nur ein 32-Bit `tStamp`-Feld, das unzureichend
ist. Daher wird ein separater Mechanismus benötigt (siehe Abschnitt 6).

---

## 5. TX-Timestamp: Bereits verfügbar

Im Gegensatz zum RX-Timestamp ist der TX-Timestamp **ohne Treiber-Eingriff** zugänglich:

```
1. PTP-Frame senden (via Packet-Handler oder direkte GMAC/LAN865x TX)
2. TSC-Feld im TC6 Data-Header auf 0x01 setzen → LAN865x erfasst TX-Timestamp
3. LAN865x setzt TTSCAA-Bit in OA_STATUS0
4. App liest via DRV_LAN865X_ReadRegister():
   - OA_TTSCAH (0x00000011) → Sekunden-Anteil des TX-Timestamps
   - OA_TTSCAL (0x00000012) → Nanosekunden-Anteil des TX-Timestamps
5. App bestätigt durch Write-1-to-Clear auf OA_STATUS0.TTSCAA
```

Diese Register-Lese-API ist in `app.c` bereits implementiert und getestet
(`lan_read` / `lan_write` CLI-Kommandos, `DRV_LAN865X_ReadRegister()`-Callbacks).

Das schwierigere Problem ist nicht der TX-Timestamp, **sondern der RX-Timestamp.**

---

## 6. Lösungsansatz: Minimaler Treiber-Patch

### Änderung in drv_lan865x_api.c

Die minimale Änderung besteht darin, den verworfenen `rxTimestamp` in einer dedizierten
globalen Struktur zu speichern, die vom PTP-Task ausgelesen werden kann.

**Schritt 1: Globale Timestamp-Übergabe-Struktur** (neue Datei oder in app.h):

```c
// ptp_ts_ipc.h  — Inter-Prozess-Kommunikation für RX-Timestamps
#ifndef PTP_TS_IPC_H
#define PTP_TS_IPC_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint64_t rxTimestamp;   // Sekunden[63:32] | Nanosekunden[31:0]
    bool     valid;
} PTP_RxTimestampEntry_t;

// Wird von drv_lan865x_api.c gesetzt, vom PTP-Task gelesen
extern volatile PTP_RxTimestampEntry_t g_ptp_rx_ts;

#endif
```

**Schritt 2: Patch in drv_lan865x_api.c** (~5 Zeilen):

```c
// Alt:
void TC6_CB_OnRxEthernetPacket(TC6_t *pInst, bool success, uint16_t len,
                                uint64_t *rxTimestamp, void *pGlobalTag)
{
    (void)pInst;
    (void)rxTimestamp;   // <-- entfernen

// Neu:
void TC6_CB_OnRxEthernetPacket(TC6_t *pInst, bool success, uint16_t len,
                                uint64_t *rxTimestamp, void *pGlobalTag)
{
    (void)pInst;
    // RX-Timestamp für PTP-Task bereitstellen
    if (rxTimestamp != NULL) {
        g_ptp_rx_ts.rxTimestamp = *rxTimestamp;
        g_ptp_rx_ts.valid       = true;
    }
```

**Schritt 3: Im pktEth0Handler (app.c)** — PTP-Frame erkennen und Timestamp abholen:

```c
bool pktEth0Handler(TCPIP_NET_HANDLE hNet, TCPIP_MAC_PACKET *rxPkt,
                    uint16_t frameType, const void *hParam)
{
    // EtherType aus MAC-Layer lesen (Byte 12-13)
    uint8_t *pEtherType = rxPkt->pMacLayer + 12;
    bool isPTP = (pEtherType[0] == 0x88) && (pEtherType[1] == 0xF7);

    if (isPTP && g_ptp_rx_ts.valid) {
        uint64_t rxTs = g_ptp_rx_ts.rxTimestamp;
        g_ptp_rx_ts.valid = false;
        // → PTP-Task benachrichtigen (Queue, Event-Flag oder direkte Verarbeitung)
        PTP_Task_OnSyncReceived(rxPkt, rxTs);
        return true;  // Frame nicht an TCP/IP-Stack weitergeben
    }

    // ... rest of handler
}
```

### Kein FTSE-Eingriff notwendig

Die TC6-Bibliothek extrahiert den Timestamp bereits transparent aus dem SPI-Footer und
entfernt die 8 Timestamp-Bytes aus dem Ethernet-Payload, bevor der Frame an höhere
Schichten weitergegeben wird. Der `TCPIP_MAC_PACKET` enthält also immer das saubere
Ethernet-Frame. Das FTSE-Feature in `OA_CONFIG0` muss aktiviert sein (wird typischerweise
von `drv_lan865x_api.c` bei der Initialisierung gesetzt).

Status prüfen:
```c
// Im Startup oder in PTP_Task_Init():
DRV_LAN865X_ReadRegister(0, 0x00000004 /*OA_CONFIG0*/, false, config0_cb, NULL);
// Erwartungswert: Bit 19 (FTSE) muss gesetzt sein
```

---

## 7. Gesamtarchitektur PTP im Bridge-Projekt

```
                    ┌─────────────────────────────────────────────────────┐
                    │              SAM E54 (Bridge-Projekt)               │
                    │                                                      │
                    │  ┌─────────────┐      ┌──────────────────────────┐  │
                    │  │  app.c      │      │  PTP_FOL_task.c (neu) │  │
                    │  │             │      │                          │  │
                    │  │ pktEth0 ────┼──────┼►  PTP_Task_OnSync()     │  │
                    │  │ Handler     │ PTP  │  PTP_Task_OnFollowUp()  │  │
                    │  │             │Frame │  Clock-Servo             │  │
                    │  │ fwd_mode ───┼──────┼► Layer-2 Bridge          │  │
                    │  │ (non-PTP)   │      │                          │  │
                    │  └─────────────┘      └──────────┬───────────────┘  │
                    │                                  │                  │
                    │  ┌─────────────────────────────  │  ─────────────┐  │
                    │  │  Harmony TCP/IP Stack          │               │  │
                    │  │  (ICMP, TCP, ARP, Telnet)      │               │  │
                    │  │                                │               │  │
                    │  │  eth0 (LAN865x)   ◄────────────┘               │  │
                    │  │  192.168.0.200    Packet-Handler-Hook          │  │
                    │  │                                                │  │
                    │  │  eth1 (GMAC)                                  │  │
                    │  │  192.168.0.210                                │  │
                    │  └───────────┬────────────────────┬──────────────┘  │
                    │              │                    │                  │
                    │  ┌───────────▼──────────┐         │                  │
                    │  │ drv_lan865x_api.c    │         │                  │
                    │  │                      │  PATCH  │                  │
                    │  │ TC6_CB_OnRxEthPkt()  │ ──────► │ g_ptp_rx_ts     │
                    │  │ (void)rxTimestamp ──►│  statt  │ (64-Bit TS)     │
                    │  │          VERWORFEN   │ verwerfen│                  │
                    │  └──────────────────────┘         │                  │
                    │                                   │                  │
                    │  ┌──────────────────────────────  │  ─────────────┐  │
                    │  │  Treiber-Schicht               │               │  │
                    │  │  DRV_LAN865X_ReadRegister() ◄──┘               │  │
                    │  │  (TX-Timestamp: OA_TTSCAH/OA_TTSCAL)          │  │
                    │  └────────────────────────────────────────────────┘  │
                    └──────────────────────────────────────────────────────┘
                              │                          │
                     ┌────────▼────────┐      ┌──────────▼────────────┐
                     │  LAN865x        │      │  GMAC (100BaseT)      │
                     │  10BASE-T1S Bus │      │  Standard-Ethernet    │
                     │  PLCA Node 7    │      │                       │
                     │  192.168.0.200  │      │  192.168.0.210        │
                     └─────────────────┘      └───────────────────────┘
                              │
                    ── T1S-Bus ──────────── PTP Grandmaster (noIP-Projekt)
```

### Datenfluss PTP-Synchronisierung

```
PTP Grandmaster (noIP, anderes Board)
  │
  │  Sync (EtherType 0x88F7, T1S-Bus)
  ▼
LAN8650/1 PHY empfängt Frame
  │  SPI-Footer: RTSA=1, rxTimestamp (64-Bit) eingebettet
  ▼
TC6-Bibliothek (tc6.c)
  │  on_rx_slice(): extrahiert 8-Byte-Timestamp, bereinigt Payload
  │  on_rx_done():  ruft TC6_CB_OnRxEthernetPacket(…, pTS=&ts, …)
  ▼
drv_lan865x_api.c: TC6_CB_OnRxEthernetPacket()
  │  [PATCH]: g_ptp_rx_ts.rxTimestamp = *rxTimestamp;
  │           g_ptp_rx_ts.valid = true;
  │  TCPIP_MAC_PACKET→ in Harmony RX-Queue einreihen
  ▼
Harmony TCP/IP Stack: pktEth0Handler() aufgerufen
  │  frameType == 0x88F7 → isPTP = true
  │  g_ptp_rx_ts.valid → rxTs = g_ptp_rx_ts.rxTimestamp
  │  return true  (kein Forwarding an IP-Stack)
  ▼
PTP_FOL_task.c: PTP_Task_OnSyncReceived(rxPkt, rxTs)
  │  Extrahiere correctionField, sequenceId aus PTP-Payload
  │  Warte auf Follow_Up für exakten t2=preciseOriginTimestamp
  │  Berechne: offset = t2 - rxTs
  ▼
Clock-Servo-Algorithmus (analog zu ptp_task.c aus noIP-Projekt)
  │  Filter (Median, IIR)
  │  Schreibe Korrektur via DRV_LAN865X_WriteRegister():
  │    MAC_TA (0x00010076) → einmalige Phasenkorrektur
  │    MAC_TI (0x00010077) → Frequenz-Trim
  ▼
LAN865x Wall Clock synchronisiert auf Grandmaster
```

---

## 8. Konkrete Integrationsstellen in app.c

### 8.1 PTP-Frame-Filter im pktEth0Handler

Die bestehende Funktion muss um die PTP-Filterung erweitert werden:

```c
bool pktEth0Handler(TCPIP_NET_HANDLE hNet, struct _tag_TCPIP_MAC_PACKET *rxPkt,
                    uint16_t frameType, const void *hParam)
{
    static uint32_t packet_counter = 0;
    packet_counter++;

    // Neu: PTP-Erkennung (EtherType 0x88F7)
    if (frameType == 0x88F7u) {
        uint64_t rxTs = 0;
        if (g_ptp_rx_ts.valid) {
            rxTs = g_ptp_rx_ts.rxTimestamp;
            g_ptp_rx_ts.valid = false;
        }
        PTP_Task_OnFrame(rxPkt, rxTs);
        return true;  // nicht an TCP/IP-Stack übergeben
    }

    // ... bestehende ipdump/fwd Logik unverändert
```

### 8.2 PTP-Task-Init in APP_Initialize

```c
void APP_Initialize(void) {
    appData.state = APP_STATE_INIT;
    TCPIP_TELNET_AuthenticationRegister(TelnetAuthenticationHandler, &TelnetHandlerParam);
    timerHandle = SYS_TIME_TimerCreate(0, SYS_TIME_MSToCount(1000),
                                       &BRIDGE_TimerCallback, 0, SYS_TIME_PERIODIC);
    SYS_TIME_TimerStart(timerHandle);
    Command_Init();

    // Neu:
    PTP_Task_Init();   // LAN865x-Uhrkonfiguration (MAC_TI, PADCTRL, 1PPS)
}
```

### 8.3 PTP-Task-Service in APP_Tasks (IDLE-Zustand)

```c
case APP_STATE_IDLE:
    PTP_Task_Service();   // periodischer Aufruf, entspricht TC6NoIP_Service()
    break;
```

### 8.4 CLI-Kommandos ✅ Implementiert

Alle PTP-Kommandos sind über Telnet erreichbar (`telnet 192.168.0.200 23`) und in
`msd_cmd_tbl[]` in `app.c` registriert.  
Übersicht:

| Kommando | Syntax | Beschreibung |
|---|---|---|
| `ptp_mode` | `ptp_mode [off\|follower\|master]` | PTP-Modus umschalten |
| `ptp_status` | `ptp_status` | Modus, GM-Sync-Zähler und GM-State ausgeben |
| `ptp_interval` | `ptp_interval <ms>` | GM Sync-Periode zur Laufzeit ändern |
| `ptp_offset` | `ptp_offset` | Follower-Zeitoffset in Nanosekunden anzeigen |
| `ptp_reset` | `ptp_reset` | Follower-Servo auf UNINIT zurücksetzen |

---

#### `ptp_mode [off|follower|master]`

Schaltet den PTP-Betriebsmodus der Bridge um.

| Argument | Wirkung |
|---|---|
| `off` | PTP deaktivieren (`PTP_DISABLED`); Sync-Frames werden ignoriert |
| `follower` | Follower-Modus aktivieren (`PTP_SLAVE`); Servo synchronisiert lokalen Takt auf eingehende Sync-Nachrichten |
| `master` | Grandmaster-Modus aktivieren: ruft zuerst `PTP_GM_Init()` (Initialisiert LAN865x TX-Timestamp-Engine, PPS-Ausgang, MAC-Zeitgeber) dann `PTP_FOL_SetMode(PTP_MASTER)` |

Ausgabe-Beispiele:
```
> ptp_mode follower
[PTP] follower mode

> ptp_mode master
[PTP] grandmaster mode

> ptp_mode off
[PTP] disabled
```

---

#### `ptp_status`

Gibt den aktuellen PTP-Zustand in einer Zeile aus.

```
> ptp_status
[PTP] mode=follower gmSyncs=42 gmState=3
```

| Feld | Bedeutung |
|---|---|
| `mode` | Aktueller Modus: `disabled`, `master` oder `slave` |
| `gmSyncs` | Anzahl der bisher verarbeiteten Sync-Frames (Grandmaster-Zähler, `PTP_GM_GetStatus`) |
| `gmState` | Interner Zustand des GM-Statemachine (0 = IDLE, steigt beim Senden von Sync-Frames) |

> **Tipp:** Im Follower-Betrieb steigen `gmSyncs` nicht an (kein GM aktiv auf dieser Seite).  
> Zum Prüfen der Synchronisationsgenauigkeit `ptp_offset` verwenden.

---

#### `ptp_interval <ms>`

Setzt die Periode, in der der Grandmaster Sync-Nachrichten aussendet.

```
> ptp_interval 250
[PTP-GM] sync interval set to 250 ms

> ptp_interval 125
[PTP-GM] sync interval set to 125 ms
```

- Standardwert: **125 ms** (8 Sync-Frames/s, entspricht PTP-Profil `logMessageInterval = -3`)
- Wirkt nur im Grandmaster-Modus (`ptp_mode master`)
- Kleinere Werte erhöhen die Genauigkeit, belasten aber den Bus; größere Werte reduzieren die Last auf langsamen T1S-Segmenten

---

#### `ptp_offset`

Zeigt den aktuellen Zeitoffset des Follower-Servos.

```
> ptp_offset
[PTP] offset=-1234 ns  abs=1234 ns
```

| Feld | Typ | Bedeutung |
|---|---|---|
| `offset` | `int64_t` | Vorzeichenbehafteter Offset in Nanosekunden. Negativ: lokaler Takt läuft vor; Positiv: läuft nach |
| `abs` | `uint64_t` | Betrag des Offsets (unabhängig vom Vorzeichen); nützlich für Konvergenzüberwachung |

**Konvergenzphasen:**

| abs-Wert | Bedeutung |
|---|---|
| > 100.000 ns | Servo noch in Grobsynchronisation (COARSELOCKED oder früher) |
| 1.000 – 100.000 ns | Servo nähert sich (LOCKEDFREQ / MATCHFREQ) |
| < 1.000 ns | Servo eingeschwungen (FINE) — gute PTP-Qualität |

---

#### `ptp_reset`

Setzt den Follower-Servo auf den Anfangszustand zurück.

```
> ptp_reset
[PTP] follower servo reset to UNINIT
```

Ruft `PTP_FOL_Reset()` auf, das den internen Servo-Zustand auf `UNINIT` zurücksetzt,
ohne den PTP-Modus selbst zu ändern.  
Der Servo beginnt direkt im nächsten Task-Zyklus neu einzuschwingen.

**Anwendungsfälle:**
- Nach einem vorübergehenden Signalverlust auf dem T1S-Segment
- Wenn `ptp_offset abs` trotz langer Laufzeit nicht konvergiert
- Nach einer manuellen Taktänderung auf dem Grandmaster

---

#### Typischer Workflow (Telnet)

```
telnet 192.168.0.200 23

> ptp_mode follower          # Follower-Modus aktivieren
[PTP] follower mode

> ptp_offset                 # Offset beim Einschwingen beobachten
[PTP] offset=-45230 ns  abs=45230 ns

> ptp_offset                 # Wiederholen bis abs < 1000 ns
[PTP] offset=-312 ns  abs=312 ns

> ptp_status                 # Zustand prüfen
[PTP] mode=slave gmSyncs=0 gmState=0

> ptp_reset                  # Servo bei Bedarf neu starten
[PTP] follower servo reset to UNINIT

> ptp_mode master            # Optional: Bridge als Grandmaster
[PTP] grandmaster mode

> ptp_interval 250           # Optional: GM-Rate reduzieren
[PTP-GM] sync interval set to 250 ms
```

---

#### Öffentliche API (`PTP_FOL_task.h`)

```c
void      PTP_FOL_Init(void);
ptpMode_t PTP_FOL_GetMode(void);
void      PTP_FOL_SetMode(ptpMode_t mode);
void      PTP_FOL_OnFrame(const uint8_t *pData, uint16_t len, NET_IF_t iface);
void      PTP_FOL_GetOffset(int64_t *pOffset, uint64_t *pOffsetAbs);
void      PTP_FOL_Reset(void);
```

---

## 9. Anwendungsszenarien

### Szenario A: Bridge als PTP-Follower (empfohlenes Startszenario)

```
T1S-Netzwerk                    Bridge (dieses Projekt)         100BaseT-Netzwerk
─────────────────               ──────────────────────          ─────────────────
PTP Grandmaster                 SAM E54                         PC / SPS / Server
  (noIP-Projekt)                                
  192.168.0.100  ──Sync(0x88F7)──► eth0 pktEth0Handler          
                                     │ PTP-Task: Servo           
                                     │ MAC_TA/MAC_TI              
                                     │ Wall Clock                 
                                     │                            
                                  eth1 GMAC ──────────────────► 192.168.0.x
                                  192.168.0.210   (TCP/IP-Dienste:
                                                   Telnet, iperf,
                                                   Ping, SNMP)
```

Die Bridge synchronisiert ihre interne LAN865x-Uhr auf den T1S-Grandmaster
und stellt TCP/IP-Dienste auf dem 100BaseT-Interface bereit. Die Uhrzeit kann
über SNMP oder einen HTTP-Server weiterverteilt werden.

### Szenario B: Transparente Uhr (Transparent Clock)

Die Bridge empfängt PTP-Frames von T1S, korrigiert das `correctionField` um die
Eigenlatenz (Residence Time = Eingangszeit − Ausgangszeit) und leitet den Frame
auf dem 100BaseT-Interface weiter. Der PTP-Grandmaster und -Follower auf der
100BaseT-Seite können so extreme Präzision erreichen, als ob sie direkt im T1S-Netz wären.

**Resident Time = t_GMAC_TX − t_LAN865x_RX**

Der TX-Timestamp des GMAC ist hierbei die Herausforderung: der SAM E54 GMAC
unterstützt IEEE 1588 Hardware-Timestamps nativ (`GEM_TMR`-Register). Diese sind
von Harmony über das GMAC-Treiber-Interface zugänglich.

### Szenario C: Boundary Clock

Die Bridge betreibt zwei unabhängige PTP-Instanzen:
- **Port 1 (T1S/eth0)**: Follower — synchronisiert LAN865x-Uhr auf den T1S-GM
- **Port 2 (100BaseT/eth1)**: Grandmaster — sendet eigene Sync-Frames ins 100BaseT-Netz

Hierfür wird ein vollständiges PTP-Stack-Modul benötigt. Als Open-Source-Option
eignet sich **ptpd** (als Bibliothek einbindbar) oder das
**Harmony 3 PTP-Middleware**-Modul aus dem offiziellen `net_10base_t1s`-Repository.

---

## 10. Implementierungsstatus (Stand 30. März 2026)

### Alle geplanten Änderungen wurden umgesetzt

| Datei | Änderung | Status |
|---|---|---|
| `firmware/src/ptp_ts_ipc.h` | IPC-Struct `PTP_RxTimestampEntry_t` + `extern volatile g_ptp_rx_ts` | ✅ neu erstellt |
| `firmware/src/filters.h` + `filters.c` | FIR/IIR-Filterbibliothek — direkt aus noIP portiert | ✅ neu erstellt |
| `firmware/src/PTP_FOL_task.h` | Alle PTP-Typen, Register-Adressen, State-Machine-Konstanten, API | ✅ neu erstellt |
| `firmware/src/PTP_FOL_task.c` | Vollständiger Clock-Servo: UNINIT→MATCHFREQ→HARDSYNC→COARSE→FINE | ✅ neu erstellt |
| `drv_lan865x_api.c` | `(void)rxTimestamp` ersetzt durch Capture in `g_ptp_rx_ts` | ✅ gepatcht |
| `firmware/src/app.c` | `#include ptp_ts_ipc.h / PTP_FOL_task.h`, `PTP_FOL_Init()`, 0x88F7-Filter | ✅ gepatcht |
| `nbproject/configurations.xml` | `filters.c` + `PTP_FOL_task.c` als `<itemPath>` eingetragen | ✅ aktualisiert |
| `nbproject/Makefile-default.mk` | 5 Variablen-Listen + 4 Compile-Rules (DEBUG + non-DEBUG) ergänzt | ✅ aktualisiert |
| `cmake/.generated/file.cmake` | `filters.c` + `PTP_FOL_task.c` in CMake-Quelldateiliste ergänzt | ✅ aktualisiert |

### Abweichungen vom ursprünglichen Plan

Die ursprüngliche Planung sah `PTP_Task_OnSyncReceived()` als Einstiegspunkt vor.
Da der gesamte Clock-Servo aus `ptp_task.c` portiert wurde, lautet die tatsächliche API:

```c
void PTP_FOL_Init(void);                                      // Initialisierung
void PTP_FOL_OnFrame(const uint8_t *pData, uint16_t len,     // Einstiegspunkt
                        uint64_t rxTimestamp);                   // aus pktEth0Handler
```

`PTP_FOL_OnFrame()` übernimmt intern die Sync/Follow_Up-Erkennung und ruft
`processSync()` / `processFollowUp()` auf — entspricht dem Ablauf in `ptp_task.c`.

### Wesentliche Portierungs-Unterschiede gegenüber noIP

| Aspekt | noIP-Projekt | Bridge-Projekt (dieses) |
|---|---|---|
| Register-Schreib-API | `TC6_WriteRegister(macPhy, addr, ...)` | `DRV_LAN865X_WriteRegister(0u, addr, ...)` |
| Service-Schleife | `TC6_Service()` blocking | entfernt — Harmony-Treiber macht das intern |
| PHY-Instanz-Zugriff | `get_macPhy_inst()` / `TC6_t *macPhy` | Treiber-Index `0u` |
| Ausgabe | `printf(...)` | `SYS_CONSOLE_PRINT(...)` |
| CMSIS-Byte-Swap | `__REV` / `__REV16` | `__builtin_bswap32` (pure C, kein CMSIS-Header) |
| Build-System | CMakeLists (noIP) | MPLAB X (Makefile) + CMake (via `.generated/file.cmake`) |

### Offene Punkte / nächste Schritte

1. **FTSE-Bit prüfen**: `OA_CONFIG0` (Register 0x00000004) — Bit 19 (FTSE) muss gesetzt
   sein, damit die TC6-Bibliothek den RX-Timestamp aus dem SPI-Footer extrahiert.
   Dies bei der Inbetriebnahme verifizieren: `lan_read 0x00000004`.

2. **Race Condition**: `g_ptp_rx_ts.valid` wird aus dem Treiber-Callback und
   `pktEth0Handler()` gelesen/geschrieben. Da beide im gleichen FreeRTOS-Task-Kontext
   laufen ist dies aktuell unkritisch — bei Umstieg auf ISR-basierten RX Atomic-Zugriff
   ergänzen.

3. **CLI-Kommandos** ✅: `ptp_mode`, `ptp_status`, `ptp_interval`, `ptp_offset`,
   `ptp_reset` sind vollständig implementiert und erfolgreich gebaut (2026-03-30).

4. **TX-Timestamp / Grandmaster-Modus** ✅: `ptp_gm_task.c` implementiert,
   CLI-Kommando `ptp_mode master` aktiviert den GM-Modus mit Hardware-Timestamp
   über `DRV_LAN865X_SendRawEthFrame(tsc=0x01)`. Build verifiziert (2026-03-30).

---

*Erstellt: 29. März 2026 | Aktualisiert: 30. März 2026*
*Basierend auf: AN1847 LAN865x-TimeSync (noIP-Projekt) und T1S 100BaseT Bridge*
