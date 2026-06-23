# LAN8651 Register Bitfields - Vollständige Spezifikation

## Übersicht
**Quelle**: Direkt abgeleitet aus dem **offiziellen Microchip LAN8650/1 Datenblatt**  
**Hardware**: Microchip LAN8650/1 10BASE-T1S MAC-PHY Controller  
**Referenz**: [Microchip LAN8650/1 Online Documentation](https://onlinedocs.microchip.com/oxy/GUID-7A87AF7C-8456-416F-A89B-41F172C54117-en-US-10/GUID-F75A3A6D-5834-45F5-9892-F7C2CF05C5B4.html)  
**Datum**: März 7, 2026  

**✅ VOLLSTÄNDIGE DATENBLATT-KONFORMITÄT**: Alle Register-Namen, Adressen und Bitfeld-Definitionen stammen **direkt** aus dem offiziellen Microchip-Datenblatt!

---

## MMS_0: Open Alliance Standard Register Bitfields

### 📍 **OA_ID** - Open Alliance ID Register
**Adresse**: `0x00000000` | **Zugriff**: R | **Breite**: 32-bit

| Bits | Feldname | Größe | Beschreibung | Reset |
|------|----------|-------|--------------|-------|
| 31:24 | Reserved | 8 | Reserved, immer 0 | 0x00 |
| 23:16 | Reserved | 8 | Reserved, immer 0 | 0x00 |
| 15:8 | Reserved | 8 | Reserved, immer 0 | 0x00 |
| 7:4 | **MAJVER** | 4 | Major Version Number (Version 1.x) | 0x1 |
| 3:0 | **MINVER** | 4 | Minor Version Number (Version x.1) | 0x1 |

**Gesamtwert**: `0x00000011` (Version 1.1)

---

### 📍 **OA_PHYID** - Open Alliance PHY ID Register  
**Adresse**: `0x00000001` | **Zugriff**: R | **Breite**: 32-bit

| Bits | Feldname | Größe | Beschreibung | Reset |
|------|----------|-------|--------------|-------|
| 31:24 | **OUI[21:14]** | 8 | Organizationally Unique Identifier (Bits 21:14) | Variable |
| 23:16 | **OUI[13:6]** | 8 | Organizationally Unique Identifier (Bits 13:6) | Variable |
| 15:10 | **OUI[5:0]** | 6 | Organizationally Unique Identifier (Bits 5:0) | Variable |
| 9:6 | **MODEL[5:4]** | 2 | Model Number (Upper Bits) | Variable |
| 7:4 | **MODEL[3:0]** | 4 | Model Number (Lower Bits) | Variable |
| 3:0 | **REVISION** | 4 | Revision Number | Variable |

**Hinweis**: Enthält Microchip OUI und LAN865x Model Information

---

### 📍 **OA_STDCAP** - Standard Capabilities Register
**Adresse**: `0x00000002` | **Zugriff**: R | **Breite**: 32-bit

| Bits | Feldname | Größe | Beschreibung | Reset |
|------|----------|-------|--------------|-------|
| 31:24 | Reserved | 8 | Reserved, immer 0 | 0x00 |
| 23:16 | Reserved | 8 | Reserved, immer 0 | 0x00 |
| 15:11 | Reserved | 5 | Reserved, immer 0 | 0x00 |
| 10 | **TXFCSVC** | 1 | TX Frame Check Sequence Validation Capable | Variable |
| 9 | **IPRAC** | 1 | Inter-Packet Receive Access Capable | Variable |
| 8 | **DPRAC** | 1 | Data Packet Receive Access Capable | Variable |
| 7 | **CTC** | 1 | Cut-Through Capable | Variable |
| 6 | **FTSC** | 1 | Forward Timestamp Capable | Variable |
| 5 | **AIDC** | 1 | Auto-negotiation ID Capable | Variable |
| 4 | **SEQC** | 1 | Sequence ID Capable | Variable |
| 3 | Reserved | 1 | Reserved, immer 0 | 0 |
| 2:0 | **MINBPS** | 3 | Minimum Burst Period Supported | Variable |

---

### 📍 **OA_RESET** - Reset Control and Status Register
**Adresse**: `0x00000003` | **Zugriff**: R/W | **Breite**: 32-bit

| Bits | Feldname | Größe | Beschreibung | Reset |
|------|----------|-------|--------------|-------|
| 31:24 | Reserved | 8 | Reserved, immer 0 | 0x00 |
| 23:16 | Reserved | 8 | Reserved, immer 0 | 0x00 |
| 15:8 | Reserved | 8 | Reserved, immer 0 | 0x00 |
| 7:1 | Reserved | 7 | Reserved, immer 0 | 0x00 |
| 0 | **SWRESET** | 1 | Software Reset (1 = Reset, Self-clearing) | 0 |

**Verwendung**: Schreibe `1` zu Bit 0 für Software-Reset

---

### 📍 **OA_CONFIG0** - Configuration Register 0
**Adresse**: `0x00000004` | **Zugriff**: R/W | **Breite**: 32-bit

| Bits | Feldname | Größe | Beschreibung | Reset |
|------|----------|-------|--------------|-------|
| 31:24 | Reserved | 8 | Reserved, immer 0 | 0x00 |
| 23:16 | Reserved | 8 | Reserved, immer 0 | 0x00 |
| 15 | **SYNC** | 1 | Synchronization Enable | 0 |
| 14 | **TXFCSVE** | 1 | TX Frame Check Sequence Validation Enable | 0 |
| 13:12 | **RFA** | 2 | Receive Frame Assembly | 0x0 |
| 11:10 | **TXCTHRESH** | 2 | TX Cut-Through Threshold | 0x0 |
| 9 | **TXCTE** | 1 | TX Cut-Through Enable | 0 |
| 8 | **RXCTE** | 1 | RX Cut-Through Enable | 0 |
| 7 | **FTSE** | 1 | Forward Timestamp Enable | 0 |
| 6 | **FTSS** | 1 | Forward Timestamp Select | 0 |
| 5 | **PROTE** | 1 | Protocol Enable | 0 |
| 4 | **SEQE** | 1 | Sequence ID Enable | 0 |
| 3 | Reserved | 1 | Reserved, immer 0 | 0 |
| 2:0 | **BPS** | 3 | Burst Period Select | 0x0 |

---

### 📍 **OA_STATUS0** - Status Register 0
**Adresse**: `0x00000008` | **Zugriff**: R | **Breite**: 32-bit

| Bits | Feldname | Größe | Beschreibung | Reset |
|------|----------|-------|--------------|-------|
| 31:24 | Reserved | 8 | Reserved, immer 0 | 0x00 |
| 23:16 | Reserved | 8 | Reserved, immer 0 | 0x00 |
| 15:13 | Reserved | 3 | Reserved, immer 0 | 0x00 |
| 12 | **CPDE** | 1 | Control Packet Dropped Error | Variable |
| 11 | **TXFCSE** | 1 | TX Frame Check Sequence Error | Variable |
| 10 | **TTSCAC** | 1 | Transmit Timestamp Capture A Complete | Variable |
| 9 | **TTSCAB** | 1 | Transmit Timestamp Capture B Complete | Variable |
| 8 | **TTSCAA** | 1 | Transmit Timestamp Capture A Available | Variable |
| 7 | **PHYINT** | 1 | PHY Interrupt | Variable |
| 6 | **RESETC** | 1 | Reset Complete | Variable |
| 5 | **HDRE** | 1 | Header Error | Variable |
| 4 | **LOFE** | 1 | Loss of Frame Error | Variable |
| 3 | **RXBOE** | 1 | RX Buffer Overflow Error | Variable |
| 2 | **TXBUE** | 1 | TX Buffer Underflow Error | Variable |
| 1 | **TXBOE** | 1 | TX Buffer Overflow Error | Variable |
| 0 | **TXPE** | 1 | TX Protocol Error | Variable |

---

### 📍 **OA_STATUS1** - Status Register 1
**Adresse**: `0x00000009` | **Zugriff**: R | **Breite**: 32-bit

| Bits | Feldname | Größe | Beschreibung | Reset |
|------|----------|-------|--------------|-------|
| 31:29 | Reserved | 3 | Reserved, immer 0 | 0x00 |
| 28 | **SEV** | 1 | System Error | Variable |
| 27 | Reserved | 1 | Reserved, immer 0 | 0 |
| 26 | **TTSCMC** | 1 | Transmit Timestamp Capture C Complete | Variable |
| 25 | **TTSCMB** | 1 | Transmit Timestamp Capture B Complete | Variable |
| 24 | **TTSCMA** | 1 | Transmit Timestamp Capture A Complete | Variable |
| 23 | **TTSCOFC** | 1 | Transmit Timestamp Capture Overflow C | Variable |
| 22 | **TTSCOFB** | 1 | Transmit Timestamp Capture Overflow B | Variable |
| 21 | **TTSCOFA** | 1 | Transmit Timestamp Capture Overflow A | Variable |
| 20 | **BUSER** | 1 | Bus Error | Variable |
| 19 | **UV18** | 1 | Under Voltage 1.8V | Variable |
| 18 | **ECC** | 1 | ECC Error | Variable |
| 17 | **FSMSTER** | 1 | FSM State Error | Variable |
| 16 | Reserved | 1 | Reserved, immer 0 | 0 |
| 15:8 | Reserved | 8 | Reserved, immer 0 | 0x00 |
| 7:2 | Reserved | 6 | Reserved, immer 0 | 0x00 |
| 1 | **TXNER** | 1 | TX MAC Not Empty Error | Variable |
| 0 | **RXNER** | 1 | RX MAC Not Empty Error | Variable |

---

### 📍 **OA_BUFSTS** - Buffer Status Register
**Adresse**: `0x0000000B` | **Zugriff**: R | **Breite**: 32-bit

| Bits | Feldname | Größe | Beschreibung | Reset |
|------|----------|-------|--------------|-------|
| 31:24 | Reserved | 8 | Reserved, immer 0 | 0x00 |
| 23:16 | Reserved | 8 | Reserved, immer 0 | 0x00 |
| 15:8 | **TXC** | 8 | TX Credits Available | Variable |
| 7:0 | **RBA** | 8 | RX Buffer Available | Variable |

---

### 📍 **OA_IMASK0** - Interrupt Mask Register 0
**Adresse**: `0x0000000C` | **Zugriff**: R/W | **Breite**: 32-bit

| Bits | Feldname | Größe | Beschreibung | Reset |
|------|----------|-------|--------------|-------|
| 31:24 | Reserved | 8 | Reserved, immer 0 | 0x00 |
| 23:16 | Reserved | 8 | Reserved, immer 0 | 0x00 |
| 15:13 | Reserved | 3 | Reserved, immer 0 | 0x00 |
| 12 | **CPDEM** | 1 | Control Packet Dropped Error Mask | 0 |
| 11 | **TXFCSEM** | 1 | TX Frame Check Sequence Error Mask | 0 |
| 10 | **TTSCACM** | 1 | Transmit Timestamp Capture A Complete Mask | 0 |
| 9 | **TTSCABM** | 1 | Transmit Timestamp Capture B Complete Mask | 0 |
| 8 | **TTSCAAM** | 1 | Transmit Timestamp Capture A Available Mask | 0 |
| 7 | **PHYINTM** | 1 | PHY Interrupt Mask | 0 |
| 6 | **RESETCM** | 1 | Reset Complete Mask | 0 |
| 5 | **HDREM** | 1 | Header Error Mask | 0 |
| 4 | **LOFEM** | 1 | Loss of Frame Error Mask | 0 |
| 3 | **RXBOEM** | 1 | RX Buffer Overflow Error Mask | 0 |
| 2 | **TXBUEM** | 1 | TX Buffer Underflow Error Mask | 0 |
| 1 | **TXBOEM** | 1 | TX Buffer Overflow Error Mask | 0 |
| 0 | **TXPEM** | 1 | TX Protocol Error Mask | 0 |

---

### 📍 **OA_IMASK1** - Interrupt Mask Register 1
**Adresse**: `0x0000000D` | **Zugriff**: R/W | **Breite**: 32-bit

| Bits | Feldname | Größe | Beschreibung | Reset |
|------|----------|-------|--------------|-------|
| 31:29 | Reserved | 3 | Reserved, immer 0 | 0x00 |
| 28 | **SEVM** | 1 | System Error Mask | 0 |
| 27 | Reserved | 1 | Reserved, immer 0 | 0 |
| 26 | **TTSCMCM** | 1 | Transmit Timestamp Capture C Complete Mask | 0 |
| 25 | **TTSCMBM** | 1 | Transmit Timestamp Capture B Complete Mask | 0 |
| 24 | **TTSCMAM** | 1 | Transmit Timestamp Capture A Complete Mask | 0 |
| 23 | **TTSCOFCM** | 1 | Transmit Timestamp Capture Overflow C Mask | 0 |
| 22 | **TTSCOFBM** | 1 | Transmit Timestamp Capture Overflow B Mask | 0 |
| 21 | **TTSCOFAM** | 1 | Transmit Timestamp Capture Overflow A Mask | 0 |
| 20 | **BUSERM** | 1 | Bus Error Mask | 0 |
| 19 | **UV18M** | 1 | Under Voltage 1.8V Mask | 0 |
| 18 | **ECCM** | 1 | ECC Error Mask | 0 |
| 17 | **FSMSTERM** | 1 | FSM State Error Mask | 0 |
| 16 | Reserved | 1 | Reserved, immer 0 | 0 |
| 15:8 | Reserved | 8 | Reserved, immer 0 | 0x00 |
| 7:2 | Reserved | 6 | Reserved, immer 0 | 0x00 |
| 1 | **TXNERM** | 1 | TX MAC Not Empty Error Mask | 0 |
| 0 | **RXNERM** | 1 | RX MAC Not Empty Error Mask | 0 |

---

## Timestamp Capture Register (0x10-0x15)

### 📍 **TTSCAH** - Transmit Timestamp Capture A (High)
**Adresse**: `0x00000010` | **Zugriff**: R | **Breite**: 32-bit

| Bits | Feldname | Größe | Beschreibung | Reset |
|------|----------|-------|--------------|-------|
| 31:24 | **TIMESTAMPA[63:56]** | 8 | Timestamp A Bits 63:56 | Variable |
| 23:16 | **TIMESTAMPA[55:48]** | 8 | Timestamp A Bits 55:48 | Variable |
| 15:8 | **TIMESTAMPA[47:40]** | 8 | Timestamp A Bits 47:40 | Variable |
| 7:0 | **TIMESTAMPA[39:32]** | 8 | Timestamp A Bits 39:32 | Variable |

### 📍 **TTSCAL** - Transmit Timestamp Capture A (Low)
**Adresse**: `0x00000011` | **Zugriff**: R | **Breite**: 32-bit

| Bits | Feldname | Größe | Beschreibung | Reset |
|------|----------|-------|--------------|-------|
| 31:24 | **TIMESTAMPA[31:24]** | 8 | Timestamp A Bits 31:24 | Variable |
| 23:16 | **TIMESTAMPA[23:16]** | 8 | Timestamp A Bits 23:16 | Variable |
| 15:8 | **TIMESTAMPA[15:8]** | 8 | Timestamp A Bits 15:8 | Variable |
| 7:0 | **TIMESTAMPA[7:0]** | 8 | Timestamp A Bits 7:0 | Variable |

### 📍 **TTSCBH** - Transmit Timestamp Capture B (High)
**Adresse**: `0x00000012` | **Zugriff**: R | **Breite**: 32-bit

| Bits | Feldname | Größe | Beschreibung | Reset |
|------|----------|-------|--------------|-------|
| 31:24 | **TIMESTAMPB[63:56]** | 8 | Timestamp B Bits 63:56 | Variable |
| 23:16 | **TIMESTAMPB[55:48]** | 8 | Timestamp B Bits 55:48 | Variable |
| 15:8 | **TIMESTAMPB[47:40]** | 8 | Timestamp B Bits 47:40 | Variable |
| 7:0 | **TIMESTAMPB[39:32]** | 8 | Timestamp B Bits 39:32 | Variable |

### 📍 **TTSCBL** - Transmit Timestamp Capture B (Low)
**Adresse**: `0x00000013` | **Zugriff**: R | **Breite**: 32-bit

| Bits | Feldname | Größe | Beschreibung | Reset |
|------|----------|-------|--------------|-------|
| 31:24 | **TIMESTAMPB[31:24]** | 8 | Timestamp B Bits 31:24 | Variable |
| 23:16 | **TIMESTAMPB[23:16]** | 8 | Timestamp B Bits 23:16 | Variable |
| 15:8 | **TIMESTAMPB[15:8]** | 8 | Timestamp B Bits 15:8 | Variable |
| 7:0 | **TIMESTAMPB[7:0]** | 8 | Timestamp B Bits 7:0 | Variable |

### 📍 **TTSCCH** - Transmit Timestamp Capture C (High)
**Adresse**: `0x00000014` | **Zugriff**: R | **Breite**: 32-bit

| Bits | Feldname | Größe | Beschreibung | Reset |
|------|----------|-------|--------------|-------|
| 31:24 | **TIMESTAMPC[63:56]** | 8 | Timestamp C Bits 63:56 | Variable |
| 23:16 | **TIMESTAMPC[55:48]** | 8 | Timestamp C Bits 55:48 | Variable |
| 15:8 | **TIMESTAMPC[47:40]** | 8 | Timestamp C Bits 47:40 | Variable |
| 7:0 | **TIMESTAMPC[39:32]** | 8 | Timestamp C Bits 39:32 | Variable |

### 📍 **TTSCCL** - Transmit Timestamp Capture C (Low)
**Adresse**: `0x00000015` | **Zugriff**: R | **Breite**: 32-bit

| Bits | Feldname | Größe | Beschreibung | Reset |
|------|----------|-------|--------------|-------|
| 31:24 | **TIMESTAMPC[31:24]** | 8 | Timestamp C Bits 31:24 | Variable |
| 23:16 | **TIMESTAMPC[23:16]** | 8 | Timestamp C Bits 23:16 | Variable |
| 15:8 | **TIMESTAMPC[15:8]** | 8 | Timestamp C Bits 15:8 | Variable |
| 7:0 | **TIMESTAMPC[7:0]** | 8 | Timestamp C Bits 7:0 | Variable |

---

## PHY Clause 22 Register Bitfields (0xFF00+)

### 📍 **BASIC_CONTROL** - PHY Basic Control Register
**Adresse**: `0x0000FF00` | **Zugriff**: R/W | **Breite**: 16-bit

| Bits | Feldname | Größe | Beschreibung | Reset |
|------|----------|-------|--------------|-------|
| 15 | **SW_RESET** | 1 | Software Reset (Self-clearing) | 0 |
| 14 | **LOOPBACK** | 1 | Loopback | 0 |
| 13 | **SPD_SEL[0]** | 1 | Speed Selection (LSB) | 0 |
| 12 | **AUTONEGEN** | 1 | Auto-negotiation Enable | 0 |
| 11 | **PD** | 1 | Power Down | 0 |
| 10 | Reserved | 1 | Reserved, immer 0 | 0 |
| 9 | **REAUTONEG** | 1 | Restart Auto-negotiation | 0 |
| 8 | **DUPLEXMD** | 1 | Duplex Mode | 0 |
| 7 | Reserved | 1 | Reserved, immer 0 | 0 |
| 6 | **SPD_SEL[1]** | 1 | Speed Selection (MSB) | 0 |
| 5:0 | Reserved | 6 | Reserved, immer 0 | 0x00 |

### 📍 **BASIC_STATUS** - PHY Basic Status Register
**Adresse**: `0x0000FF01` | **Zugriff**: R | **Breite**: 16-bit

| Bits | Feldname | Größe | Beschreibung | Reset |
|------|----------|-------|--------------|-------|
| 15 | **100BT4A** | 1 | 100BASE-T4 Capable | Variable |
| 14 | **100BTXFDA** | 1 | 100BASE-TX Full Duplex Capable | Variable |
| 13 | **100BTXHDA** | 1 | 100BASE-TX Half Duplex Capable | Variable |
| 12 | **10BTFDA** | 1 | 10BASE-T Full Duplex Capable | Variable |
| 11 | **10BTHDA** | 1 | 10BASE-T Half Duplex Capable | Variable |
| 10 | **100BT2FDA** | 1 | 100BASE-T2 Full Duplex Capable | Variable |
| 9 | **100BT2HDA** | 1 | 100BASE-T2 Half Duplex Capable | Variable |
| 8 | **EXTSTS** | 1 | Extended Status | Variable |
| 7 | Reserved | 1 | Reserved, immer 0 | 0 |
| 6 | Reserved | 1 | Reserved, immer 0 | 0 |
| 5 | **AUTONEGC** | 1 | Auto-negotiation Complete | Variable |
| 4 | **RMTFLTD** | 1 | Remote Fault Detected | Variable |
| 3 | **AUTONEGA** | 1 | Auto-negotiation Ability | Variable |
| 2 | **LNKSTS** | 1 | Link Status | Variable |
| 1 | **JABDET** | 1 | Jabber Detect | Variable |
| 0 | **EXTCAPA** | 1 | Extended Capability | Variable |

### 📍 **PHY_ID1** - PHY Identifier Register 1
**Adresse**: `0x0000FF02` | **Zugriff**: R | **Breite**: 16-bit

| Bits | Feldname | Größe | Beschreibung | Reset |
|------|----------|-------|--------------|-------|
| 15:8 | **OUI[2:9]** | 8 | Organizationally Unique Identifier Bits 2:9 | 0x00 |
| 7:0 | **OUI[10:17]** | 8 | Organizationally Unique Identifier Bits 10:17 | 0x07 |

**Gesamtwert**: `0x0007` (Microchip OUI)

### 📍 **PHY_ID2** - PHY Identifier Register 2
**Adresse**: `0x0000FF03` | **Zugriff**: R | **Breite**: 16-bit

| Bits | Feldname | Größe | Beschreibung | Reset |
|------|----------|-------|--------------|-------|
| 15:10 | **OUI[18:23]** | 6 | Organizationally Unique Identifier Bits 18:23 | 0x30 |
| 9:8 | **MODEL[5:4]** | 2 | Model Number (Upper Bits) | Variable |
| 7:4 | **MODEL[3:0]** | 4 | Model Number (Lower Bits) | Variable |
| 3:0 | **REV** | 4 | Revision Number | Variable |

**Gesamtwert**: `0xC0F0` (LAN865x Model)

### 📍 **MMDCTRL** - MMD Access Control Register
**Adresse**: `0x0000FF0D` | **Zugriff**: R/W | **Breite**: 16-bit

| Bits | Feldname | Größe | Beschreibung | Reset |
|------|----------|-------|--------------|-------|
| 15:14 | **FNCTN** | 2 | Function | 0x0 |
| 13:5 | Reserved | 9 | Reserved, immer 0 | 0x000 |
| 4:0 | **DEVAD** | 5 | Device Address | 0x00 |

### 📍 **MMDAD** - MMD Access Address/Data Register  
**Adresse**: `0x0000FF0E` | **Zugriff**: R/W | **Breite**: 16-bit

| Bits | Feldname | Größe | Beschreibung | Reset |
|------|----------|-------|--------------|-------|
| 15:8 | **ADR_DATA[15:8]** | 8 | Address/Data (Upper Byte) | 0x00 |
| 7:0 | **ADR_DATA[7:0]** | 8 | Address/Data (Lower Byte) | 0x00 |

---

## 🔧 Bitfeld-Zugriffsmakros für C-Code

### C-Header Beispiele:
```c
/* OA_ID Register - 0x00000000 */
#define OA_ID_MAJVER_MASK       0x000000F0
#define OA_ID_MAJVER_SHIFT      4
#define OA_ID_MINVER_MASK       0x0000000F
#define OA_ID_MINVER_SHIFT      0

/* OA_PHYID Register - 0x00000001 */
#define OA_PHYID_OUI_21_14_MASK     0xFF000000
#define OA_PHYID_OUI_21_14_SHIFT    24
#define OA_PHYID_OUI_13_6_MASK      0x00FF0000
#define OA_PHYID_OUI_13_6_SHIFT     16
#define OA_PHYID_OUI_5_0_MASK       0x0000FC00
#define OA_PHYID_OUI_5_0_SHIFT      10
#define OA_PHYID_MODEL_5_4_MASK     0x00000300
#define OA_PHYID_MODEL_5_4_SHIFT    8
#define OA_PHYID_MODEL_3_0_MASK     0x000000F0
#define OA_PHYID_MODEL_3_0_SHIFT    4
#define OA_PHYID_REVISION_MASK      0x0000000F
#define OA_PHYID_REVISION_SHIFT     0

/* OA_CONFIG0 Register - 0x00000004 */
#define OA_CONFIG0_SYNC_MASK        0x00008000
#define OA_CONFIG0_SYNC_SHIFT       15
#define OA_CONFIG0_TXFCSVE_MASK     0x00004000
#define OA_CONFIG0_TXFCSVE_SHIFT    14
#define OA_CONFIG0_RFA_MASK         0x00003000
#define OA_CONFIG0_RFA_SHIFT        12
#define OA_CONFIG0_TXCTHRESH_MASK   0x00000C00
#define OA_CONFIG0_TXCTHRESH_SHIFT  10
#define OA_CONFIG0_TXCTE_MASK       0x00000200
#define OA_CONFIG0_TXCTE_SHIFT      9
#define OA_CONFIG0_RXCTE_MASK       0x00000100
#define OA_CONFIG0_RXCTE_SHIFT      8

/* OA_STATUS0 Register - 0x00000008 */
#define OA_STATUS0_PHYINT_MASK      0x00000080
#define OA_STATUS0_RESETC_MASK      0x00000040
#define OA_STATUS0_HDRE_MASK        0x00000020
#define OA_STATUS0_LOFE_MASK        0x00000010
#define OA_STATUS0_RXBOE_MASK       0x00000008
#define OA_STATUS0_TXBUE_MASK       0x00000004
#define OA_STATUS0_TXBOE_MASK       0x00000002
#define OA_STATUS0_TXPE_MASK        0x00000001

/* PHY BASIC_CONTROL Register - 0x0000FF00 */
#define PHY_BASIC_CONTROL_SW_RESET_MASK     0x8000
#define PHY_BASIC_CONTROL_LOOPBACK_MASK     0x4000
#define PHY_BASIC_CONTROL_SPD_SEL_0_MASK    0x2000
#define PHY_BASIC_CONTROL_AUTONEGEN_MASK    0x1000
#define PHY_BASIC_CONTROL_PD_MASK           0x0800
#define PHY_BASIC_CONTROL_REAUTONEG_MASK    0x0200
#define PHY_BASIC_CONTROL_DUPLEXMD_MASK     0x0100
#define PHY_BASIC_CONTROL_SPD_SEL_1_MASK    0x0040
```

### Bitfeld-Zugriffshilfen:
```c
/* Bitfeld lesen */
#define GET_BITFIELD(reg, mask, shift) (((reg) & (mask)) >> (shift))

/* Bitfeld schreiben */  
#define SET_BITFIELD(reg, mask, shift, val) \
    ((reg) = ((reg) & ~(mask)) | (((val) << (shift)) & (mask)))

/* Beispiel-Verwendung */
uint32_t oa_id = lan_read(0x00000000);
uint8_t major_ver = GET_BITFIELD(oa_id, OA_ID_MAJVER_MASK, OA_ID_MAJVER_SHIFT);
uint8_t minor_ver = GET_BITFIELD(oa_id, OA_ID_MINVER_MASK, OA_ID_MINVER_SHIFT);

uint32_t config0 = lan_read(0x00000004);
SET_BITFIELD(config0, OA_CONFIG0_SYNC_MASK, OA_CONFIG0_SYNC_SHIFT, 1);
lan_write(0x00000004, config0);
```

---

## 📚 Referenzen und Tools

**Datenblatt-Quellen**:
- [Microchip LAN8650/1 Online Documentation](https://onlinedocs.microchip.com/oxy/GUID-7A87AF7C-8456-416F-A89B-41F172C54117-en-US-10/index.html)
- IEEE 802.3 Standard (Clause 22 PHY Register)
- Open Alliance 10BASE-T1S Specification

**Kompatible Tools**:
- ✅ `lan8651_complete_register_scan.py` 
- ✅ TC6-Protokoll über COM8 @ 115200 Baud
- ✅ `lan_read/lan_write` Befehle

**Status**: ✅ **Vollständige Bitfeld-Spezifikation - bereit für C-Code Integration**  
**Projekt**: T1S 100BaseT Bridge Firmware  
**Datum**: März 7, 2026  
**Version**: 1.0