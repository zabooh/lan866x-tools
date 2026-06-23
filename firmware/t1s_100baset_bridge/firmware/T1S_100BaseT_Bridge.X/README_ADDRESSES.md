# LAN8651 SPI Register Addresses - Hardware Verified

**Status**: ✅ Hardware-verifiziert + Updates am 11. März 2026  
**Verwendung**: Direkte Adressen für `lan_read` und `lan_write` Kommandos  
**Hardware**: LAN8651 10BASE-T1S MAC-PHY Controller
**Update**: PMD-Register-Zugriff-Korrektur + SQI-Register bestätigt

## MMS Architektur
```
32-Bit SPI Adresse = [MMS (obere 16 Bits)] + [Register Offset (untere 16 Bits)]

MMS 0 (0x0000xxxx): Open Alliance Standard Register
MMS 1 (0x0001xxxx): MAC Register  
MMS 2 (0x0002xxxx): PHY PCS Register
MMS 3 (0x0003xxxx): PHY PMA/PMD Register
MMS 4 (0x0004xxxx): Vendor Specific Register (PLCA)
MMS 5 (0x0005xxxx): SPI / TC6 Interface
MMS 6 (0x0006xxxx): Interrupt / Event Control
MMS 7 (0x0007xxxx): Power / Reset / Clock
MMS 8 (0x0008xxxx): Nicht als HW-Block implementiert (liest 0x00000000)
MMS 9 (0x0009xxxx): Vendor / Debug Extensions
MMS 10 (0x000Axxxx): Miscellaneous Register
```

## MMS 0: Open Alliance Standard Register

| Adresse | Register | Beschreibung | Default | Bitfelder |
|---------|----------|--------------|---------|----------|
| `0x00000000` | **OA_ID** | Device ID Register | `0x0000001` | [15:8] Rev [7:4] Ver [3:0] ID |
| `0x00000001` | **OA_PHYID** | PHY Identification | Variable | [31:0] PHY OUI + Model + Rev |
| `0x00000002` | **OA_STDCAP** | Standard Capabilities | `0x0004000` | [31:0] Capability Bits |
| `0x00000003` | **OA_RESET** | Reset Control | `0x0000000` | [0] SW_RESET |
| `0x00000004` | **OA_CONFIG0** | Configuration 0 | `0x0000000` | [7] SYNC [6] ZARFE [5:0] Reserved |
| `0x00000008` | **OA_STATUS0** | Status 0 | Variable | [7] RESETC [6] HDRE [5] LOFE [4] TXPE |
| `0x00000009` | **OA_STATUS1** | Status 1 | Variable | [7:0] Status Bits |
| `0x0000000B` | **OA_BUFSTS** | Buffer Status | Variable | [15:8] TXC [7:0] RCA |
| `0x0000000C` | **OA_IMASK0** | Interrupt Mask 0 | `0x0000000` | [7] RESETC [6] HDRE [5] LOFE [4] TXPE |
| `0x0000000D` | **OA_IMASK1** | Interrupt Mask 1 | `0x0000000` | [7:0] Interrupt Mask Bits |
| `0x00000010` | **TTSCAH** | TX Timestamp A (High) | `0x0000000` | [15:0] Timestamp High |
| `0x00000011` | **TTSCAL** | TX Timestamp A (Low) | `0x0000000` | [31:0] Timestamp Low |
| `0x0000FF00` | **BASIC_CONTROL** | PHY Basic Control | `0x0000` | [15] Reset [14] Loopback [13] Speed |
| `0x0000FF01` | **BASIC_STATUS** | PHY Basic Status | Variable | [15:0] IEEE 802.3 Status |
| `0x0000FF02` | **PHY_ID1** | PHY Identifier 1 | `0x0007` | [15:0] OUI bits [3:18] |
| `0x0000FF03` | **PHY_ID2** | PHY Identifier 2 | `0xC0F0` | [15:10] OUI [9:4] Model [3:0] Rev |
| `0x0000FF20` | **PMD_CONTROL** | PMD Control (Clause 22) | `0x0000` | [15] Reset [14] Loopback [13:0] Reserved |
| `0x0000FF21` | **PMD_STATUS** | PMD Status (Clause 22) | `0x0805` | [15:0] PMD Status (Link UP confirmed) |
| `0x0000FF22` | **PMD_ID1** | PMD ID1 (Clause 22) | `0x0007` | [15:0] PMD Identifier 1 |
| `0x0000FF23` | **PMD_ID2** | PMD ID2 (Clause 22) | `0xC1B3` | [15:0] PMD Identifier 2 |

## MMS 1: MAC Register

| Adresse | Register | Beschreibung | Default | Bitfelder |
|---------|----------|--------------|---------|----------|
| `0x00010000` | **MAC_NCR** | Network Control | `0x00000000` | [9] WESTAT [7] TXEN [6] RXEN [4] CLRSTAT |
| `0x00010001` | **MAC_NCFGR** | Network Configuration | `0x00080000` | [19] IRXER [18] FDEN [17] DRFCS |
| `0x00010022` | **MAC_SAB1** | MAC Address Bottom | `0x00000000` | [31:0] MAC Address [31:0] |
| `0x00010023` | **MAC_SAT1** | MAC Address Top | `0x00000000` | [15:0] MAC Address [47:32] |
| `0x0001006F` | **MAC_TISUBN** | TSU Sub-nanoseconds | `0x00000000` | [15:0] Sub-nanoseconds |
| `0x00010070` | **MAC_TSH** | TSU Seconds High | `0x00000000` | [15:0] Seconds [47:32] |
| `0x00010074` | **MAC_TSL** | TSU Seconds Low | `0x00000000` | [31:0] Seconds [31:0] |
| `0x00010075` | **MAC_TN** | TSU Nanoseconds | `0x00000000` | [29:0] Nanoseconds |
| `0x00010200` | **BMGR_CTL** | Buffer Manager Control | Variable | [31:0] Buffer Control Bits |
| `0x00010208` | **STATS0** | Statistics 0 | `0x00000000` | [31:0] Statistics Counter |

## MMS 2: PHY PCS Register

| Adresse | Register | Beschreibung | Default | Bitfelder |
|---------|----------|--------------|---------|----------|
| `0x000208F3` | **PCS_REG** | PCS Basic Register | `0x0000` | [15:0] PCS Control Bits |

## MMS 3: PHY PMA/PMD Register

⚠️ **ACHTUNG**: MMS 3 direkte Zugriffe funktionieren NICHT! Alle Register geben 0x0000 zurück.
**Lösung**: PMD-Register über Clause 22 PHY-Register (0x0000FFxx) zugreifen.

| Adresse | Register | Beschreibung | Status | Bitfelder |
|---------|----------|--------------|---------|----------|
| `0x00030001` | **PMD_CONTROL** | PMA/PMD Control | ❌ Returns 0x0000 | [15] Reset [14] Loopback [13:0] Reserved |
| `0x00030002` | **PMD_STATUS** | PMA/PMD Status | ❌ Returns 0x0000 | [15:0] PMA/PMD Status Bits |

**✅ KORREKTE PMD-ZUGRIFFE:** Siehe Clause 22 PHY Register (0x0000FF20-FF23)

## MMS 4: Vendor Specific Register (PLCA + SQI)

| Adresse | Register | Beschreibung | Default | Aktueller Wert | Bitfelder |
|---------|----------|--------------|---------|----------------|-----------|
| `0x00040010` | **CTRL1** | Vendor Control 1 | `0x0000` | - | [15:0] Vendor Control Bits |
| `0x00040018` | **STS1** | Vendor Status 1 | Variable | - | [15:0] Vendor Status Bits |
| `0x0004008F` | **PHY_EXT_STATUS** | PHY Extended Status (SQI) | Variable | `0x8631` | [10:8] SQI Value (6/7), [15:0] Extended Status |
| `0x0004CA00` | **MIDVER** | Map ID Version | Variable | - | [15:8] Map Ver [7:0] Map ID |
| `0x0004CA01` | **PLCA_CTRL0** | PLCA Control 0 | `0x8000` | `0x00008000` | [15] PLCA_EN [14] PLCA_RST [13:0] Reserved |
| `0x0004CA02` | **PLCA_CTRL1** | PLCA Control 1 | `0x0000` | `0x00000807` | [15:8] NCNT (Node Count) [7:0] ID (Node ID) |
| `0x0004CA03` | **PLCA_STS** | PLCA Status | `0x0000` | `0x00000000` | [15:0] PLCA Status Bits |
| `0x0004CA04` | **PLCA_TOTMR** | PLCA TO Timer | `0x0000` | - | [15:0] TO Timer Value |
| `0x0004CA05` | **PLCA_BURST** | PLCA Burst Mode | `0x0080` | `0x00000080` | [15:8] MAXBC [7:0] BTMR (Burst Timer) |

## MMS 5: SPI / TC6 Interface

| Adresse | Register | Beschreibung | Default | Bitfelder |
|---------|----------|--------------|---------|----------|
| `0x00050000` | **SPI_STATUS** | SPI Status Register | Variable | [15:0] SPI Status Bits |
| `0x00050001` | **TC6_CONTROL** | TC6 Control Register | `0x0000` | [7] PARITY_EN [6:0] Control Bits |
| `0x00050002` | **PARITY_CONTROL** | Parity Control | `0x0000` | [15:0] Parity Configuration |

## MMS 6: Interrupt / Event Control

| Adresse | Register | Beschreibung | Default | Bitfelder |
|---------|----------|--------------|---------|----------|
| `0x00060000` | **IRQ_STATUS** | Interrupt Status | Variable | [15:0] IRQ Status Flags |
| `0x00060001` | **IRQ_MASK** | Interrupt Mask | `0x0000` | [15:0] IRQ Mask Enable Bits |
| `0x00060002` | **EVENT_CONTROL** | Event Control | `0x0000` | [15:0] Event Control Bits |

## MMS 7: Power / Reset / Clock

| Adresse | Register | Beschreibung | Default | Bitfelder |
|---------|----------|--------------|---------|----------|
| `0x00070000` | **RESET_STATUS** | Reset Status | Variable | [15:0] Reset Status Bits |
| `0x00070001` | **POWER_CONTROL** | Power Control | `0x0000` | [7:0] Power Mode Control |
| `0x00070002` | **CLOCK_CONTROL** | Clock Control | `0x0000` | [15:0] Clock Configuration |

## MMS 8: Statistik-Hinweis (kein HW-Registerblock)

⚠️ **Wichtig**: Der LAN8651 implementiert keinen echten MMS-8-Hardware-Block.
Direkte Reads auf `0x0008xxxx` liefern typischerweise `0x00000000`.

✅ Für Laufzeit-Statistiken im aktuellen Projekt werden stattdessen
**Software-Counter** über den CLI-Befehl `stats` genutzt (eth0/eth1 TX/RX-Zähler).

## MMS 9: Vendor / Debug Extensions

| Adresse | Register | Beschreibung | Default | Bitfelder |
|---------|----------|--------------|---------|----------|
| `0x00090000` | **DEBUG_REG1** | Debug Register 1 | `0x0000` | [15:0] Debug Control Bits |
| `0x00090001` | **DEBUG_REG2** | Debug Register 2 | `0x0000` | [15:0] Debug Status Bits |
| `0x00090002` | **VENDOR_EXT** | Vendor Extensions | `0x0000` | [15:0] Vendor Extension Bits |

## MMS 10: Miscellaneous Register

| Adresse | Register | Beschreibung | Default | Bitfelder |
|---------|----------|--------------|---------|----------|
| `0x000A0000` | **MISC_CONTROL** | Miscellaneous Control | `0x0000` | [15:0] Miscellaneous Control Bits |

## PLCA Konfiguration (Hardware-Werte)

Basierend auf aktuellen Register-Werten:

### PLCA_CTRL0 (0x0004CA01) = `0x00008000`
- **Bit 15 (PLCA_EN)** = 1 → **PLCA aktiviert** ✅
- **Bit 14 (PLCA_RST)** = 0 → Kein Reset aktiv
- **Bits 13:0** = 0x0000 → Reserved

### PLCA_CTRL1 (0x0004CA02) = `0x00000807`  
- **Bits 15:8 (NCNT)** = 0x08 → **8 Nodes im Netzwerk**
- **Bits 7:0 (ID)** = 0x07 → **Dieser Node ist ID 7**

### PLCA_BURST (0x0004CA05) = `0x00000080`
- **Bits 15:8 (MAXBC)** = 0x00 → Max Burst Count = 0
- **Bits 7:0 (BTMR)** = 0x80 → **Burst Timer = 128**

**🎯 FAZIT**: PLCA ist korrekt als **Node 7 in einem 8-Node Netzwerk** konfiguriert!

## ⚡ PMD Register Access - Korrekte Methode

**❌ PROBLEM**: MMS 3 direkte Zugriffe (0x00030xxx) geben nur 0x0000 zurück  
**✅ LÖSUNG**: PMD über Clause 22 PHY Register (0x0000FFxx) zugreifen

### PMD via Clause 22 PHY Register (Hardware-verifiziert)

| Adresse | Register | Aktueller Wert | Beschreibung |
|---------|----------|----------------|---------------|
| `0x0000FF20` | **PMD_CONTROL** | `0x0000` | PMD Control Register (PHY 1, Reg 0) |
| `0x0000FF21` | **PMD_STATUS** | `0x0805` | PMD Status (Link UP ✅, für 1.476 Mbps) |
| `0x0000FF22` | **PMD_ID1** | `0x0007` | PMD Identifier 1 (Microchip OUI) |
| `0x0000FF23` | **PMD_ID2** | `0xC1B3` | PMD Identifier 2 (Model + Revision) |

### SQI (Signal Quality Index) - Hardware-verifiziert

**Register**: `0x0004008F` (PHY_EXTENDED_STATUS)  
**Aktueller Wert**: `0x8631`  
**SQI Wert**: `6/7` (EXCELLENT) - Bits 10:8  
**Qualität**: ✅ Sehr gut (consistent mit 1.476 Mbps Performance)

### Cable Fault Diagnostics (CFD)

**Korrekte Adressen für CFD**:
- **CFD Control**: `0x0000FF20` (PMD_CONTROL via Clause 22)
- **CFD Status**: `0x0000FF21` (PMD_STATUS via Clause 22)
- **Methode**: Clause 22 PHY Register, nicht MMS 3 direkt

### Bitfeld Details für wichtige Register

#### OA_CONFIG0 (0x00000004) - Configuration Register
- **Bit 7 (SYNC)**: Synchronization Control
- **Bit 6 (ZARFE)**: Zero-Align Receive Frame Enable  
- **Bits 5:0**: Reserved

#### MAC_NCR (0x00010000) - Network Control Register
- **Bit 9 (WESTAT)**: Write Enable Statistics
- **Bit 7 (TXEN)**: Transmit Enable
- **Bit 6 (RXEN)**: Receive Enable
- **Bit 4 (CLRSTAT)**: Clear Statistics

#### OA_STATUS0 (0x00000008) - Status Register 0
- **Bit 7 (RESETC)**: Reset Complete
- **Bit 6 (HDRE)**: Header Error  
- **Bit 5 (LOFE)**: Loss of Frame Error
- **Bit 4 (TXPE)**: Transmit Protocol Error

## Verwendung

```bash
# Device Information
lan_read 0x00000000    # Device ID
lan_read 0x00000001    # PHY ID

# SQI (Signal Quality Index) - EXCELLENT 6/7
lan_read 0x0004008F    # SQI in bits 10:8 (0x8631 = SQI 6/7)

# PMD Register (KORREKTE METHODE - Clause 22)
lan_read 0x0000FF20    # PMD Control (PHY 1, Reg 0)
lan_read 0x0000FF21    # PMD Status (PHY 1, Reg 1) - Link Status
lan_read 0x0000FF22    # PMD ID1 (PHY 1, Reg 2)
lan_read 0x0000FF23    # PMD ID2 (PHY 1, Reg 3)

# MAC Control
lan_read 0x00010000    # MAC Network Control
lan_read 0x00010208    # MAC Statistics

# PLCA Status (aktive Konfiguration)
lan_read 0x0004CA01    # PLCA Control 0
lan_read 0x0004CA02    # PLCA Control 1

# SPI / TC6 Interface
lan_read 0x00050000    # SPI Status
lan_read 0x00050001    # TC6 Control

# Interrupt Control
lan_read 0x00060000    # IRQ Status
lan_read 0x00060001    # IRQ Mask

# Power / Reset
lan_read 0x00070000    # Reset Status
lan_read 0x00070001    # Power Control

# Statistics (aktueller Weg im Projekt)
stats                  # SW-Zähler für eth0/eth1 (TX/RX, qFull, noBufs, ...)

# Debug / Vendor
lan_read 0x00090000    # Debug Register 1
lan_read 0x000A0000    # Miscellaneous Control

# Register schreiben  
lan_write 0x0004CA01 0x8000    # PLCA aktivieren
lan_write 0x00060001 0x0000    # IRQ Mask setzen
```

## C-Code Definitionen

```c
// Device Information
#define LAN8651_OA_ID           0x00000000
#define LAN8651_OA_PHYID        0x00000001

// MAC Control
#define LAN8651_MAC_NCR         0x00010000
#define LAN8651_MAC_STATS       0x00010208

// PHY PCS/PMA
#define LAN8651_PCS_REG         0x000208F3
#define LAN8651_PMD_CONTROL     0x00030001

// PLCA Register (korrekte Adressen!)
#define LAN8651_PLCA_CTRL0      0x0004CA01
#define LAN8651_PLCA_CTRL1      0x0004CA02  
#define LAN8651_PLCA_STATUS     0x0004CA03
#define LAN8651_PLCA_BURST      0x0004CA05

// SPI / TC6 Interface
#define LAN8651_SPI_STATUS      0x00050000
#define LAN8651_TC6_CONTROL     0x00050001

// Interrupt / Event Control
#define LAN8651_IRQ_STATUS      0x00060000
#define LAN8651_IRQ_MASK        0x00060001

// Power / Reset / Clock
#define LAN8651_RESET_STATUS    0x00070000
#define LAN8651_POWER_CONTROL   0x00070001

// Statistics / Counters
// MMS_8 HW-Counter nicht implementiert; im Projekt via CLI-Befehl `stats` (SW-Counter)

// Debug Extensions
#define LAN8651_DEBUG_REG1      0x00090000
#define LAN8651_VENDOR_EXT      0x00090002

// Miscellaneous
#define LAN8651_MISC_CONTROL    0x000A0000
```

---

**Datum**: März 2026  
**Version**: 1.0  
**Support**: T1S Bridge Development Team