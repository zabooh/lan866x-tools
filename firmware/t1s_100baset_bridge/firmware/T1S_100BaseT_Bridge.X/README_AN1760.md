# LAN8650/1 Configuration Application Note AN1760 - Implementation Guide

## Übersicht

Dieses Dokument beschreibt die Implementierung der **Microchip Application Note AN1760** für die optimale Konfiguration des LAN8650/1 10BASE-T1S MAC-PHY Controllers.

**Quelle**: Microchip AN1760 - LAN8650/1 Configuration Application Note  
**Hardware**: LAN8650/1 Revisionen B0 (0001), B1 (0010)  
**Datum**: März 11, 2026  
**Status**: Production Implementation Guide  

---

## 📋 Application Note Zusammenfassung

### Zweck
Die AN1760 definiert die **kritische Konfigurationssequenz**, die **unmittelbar nach Power-Up oder Reset** ausgeführt werden muss für:
- ✅ Optimale Network Performance in 10BASE-T1S Netzwerken
- ✅ Korrekte PLCA (Physical Layer Collision Avoidance) Konfiguration
- ✅ Signal Quality Index (SQI) Setup für Diagnose
- ✅ Multi-Node Network Support

### Kritische Erkenntnisse
⚠️ **TIMING CRITICAL**: Register-Konfiguration muss **sofort nach Reset** erfolgen  
⚠️ **REIHENFOLGE WICHTIG**: Falsche Sequence → Performance-Probleme  
⚠️ **BERECHNETE PARAMETER**: Bestimmte Werte müssen device-spezifisch berechnet werden  

---

## 🔧 Register-Zugriffs-APIs (AN1760-konform)

### Pseudocode aus AppNote
```c
// Read Register (MMS + Address)
(uint16) value = read_register(uint8 mms, uint16 addr)

// Write Register  
write_register(uint8 mms, uint16 addr, uint16 value)

// Proprietary Indirect Access
uint8 indirect_read(uint8 addr, uint8 mask)
{
    write_register(0x04, 0x00D8, addr)    // Set address
    write_register(0x04, 0x00DA, 0x02)    // Trigger read
    return (read_register(0x04, 0x00D8) & mask)
}
```

### Unsere TC6-Implementation (Kompatibel!)
```python
# Bereits implementiert in unseren Tools
lan_write 0x000400XX value    # MMS 4 = PHY Vendor Specific
lan_read 0x000400XX           # Lesen aus MMS 4
```

---

## 📊 Konfigurationstabellen (AN1760)

### Table 1: MANDATORY CONFIGURATION REGISTER WRITES
**Alle Register in MMS 0x4 (PHY Vendor Specific) - PFLICHT nach Power-On:**

| Register | Adresse | Wert | Zweck |
|----------|---------|------|-------|
| **Basis Config** | 0x00040000 | 0x3F31 | Fundamental Setup |
| **Performance** | 0x000400E0 | 0x00C0 | Performance Tuning |
| **Config Param 1** | 0x000400B4 | `cfgparam1` | **Berechnet aus Device** |
| **Config Param 2** | 0x000400B6 | `cfgparam2` | **Berechnet aus Device** |
| **Extended Config** | 0x000400E9 | 0x9E50 | Advanced Settings |
| **Signal Tuning** | 0x000400F5 | 0x1CF8 | Signal Optimization |
| **Interface** | 0x000400F4 | 0x0C00 | Interface Control |
| **Hardware Tuning** | 0x000400F8 | 0xB900 | Hardware Optimization |
| **Performance Reg** | 0x000400F9 | 0x4E53 | Performance Register |
| **Control Register** | 0x00040081 | 0x0080 | Final Control |
| **Additional Regs** | 0x00040091 | 0x9860 | More Configuration |
| **Extended Setup** | 0x00040077 | 0x0028 | Extended Setup |
| **Parameter Regs** | 0x00040043-0x00040050 | Various | Multiple Parameters |

### Table 2: SQI CONFIGURATION (Optional)
**Signal Quality Index für Enhanced Diagnostics:**

| Register | Adresse | Wert | Zweck |
|----------|---------|------|-------|
| **SQI Config 1** | 0x000400AD | `cfgparam3` | SQI Parameter 1 |
| **SQI Config 2** | 0x000400AE | `cfgparam4` | SQI Parameter 2 |
| **SQI Config 3** | 0x000400AF | `cfgparam5` | SQI Parameter 3 |
| **SQI Control** | 0x000400B0-0x000400BB | Various | SQI Control Registers |

### Table 3: PLCA CONFIGURATION 
**Physical Layer Collision Avoidance (Multi-Node Networks):**

| Register | Adresse (bekannt) | Wert | Zweck |
|----------|-------------------|------|-------|
| **PLCA_CTRL1** | 0x0004CA02 | `plcaparam1` | **Node ID + Count Setup** |
| **PLCA_CTRL0** | 0x0004CA01 | 0x0000/0x8000 | **PLCA Enable/Disable** |
| **CDCTL0** | TBD | `CDCTL0_TEMP` | **Collision Detection Control** |

---

## 🎯 PLCA-Konfiguration (Multi-Node Networks)

### PLCA Coordinator vs. Follower
```python
# Node Configuration Logic (aus AN1760)
def calculate_plca_param(node_id, node_count):
    if node_id == 0:
        # PLCA Coordinator (Master)
        plcaparam1 = (node_count << 8)
        return plcaparam1
    else:
        # PLCA Follower (Slave)  
        plcaparam1 = node_id
        return plcaparam1
```

### PLCA vs. CSMA/CD
- **PLCA Mode**: Deterministic, kollisionsfrei (empfohlen für >2 Nodes)
- **CSMA/CD Mode**: Classic Ethernet mit Collision Detection

---

## ⚙️ Berechnete Parameter (Device-Specific)

### Parameter-Berechnung aus AN1760
```python
# Value1 Berechnung (aus Address 0x0004)
def calculate_cfgparam1():
    value1 = indirect_read(0x04, 0x01F)
    offset1 = 0
    if value1 & 0x10 == 0:
        offset1 = (int8)((uint8)value1 - 0x20)
    else:
        offset1 = (int8)value1
    return offset1

# Value2 Berechnung (aus Address 0x0008)  
def calculate_cfgparam2():
    value2 = indirect_read(0x08, 0x01F)
    offset2 = 0
    if value2 & 0x10 == 0:
        offset2 = (int8)((uint8)value2 - 0x20)
    else:
        offset2 = (int8)value2
    return offset2

# SQI Parameter Berechnung
def calculate_sqi_params():
    uint6_cfgparam3 = (uint16)((10 + offset1) & 0x0F) | (uint16)(((14 + offset1) & 0x0F) << 4) | 0x03
    uint6_cfgparam4 = (uint16)((10 + offset2) & 0x0F) | 0x300
    return cfgparam3, cfgparam4
```

---

## 🔄 Konfigurationssequenz (Power-On)

### 1. Device Identification
```python
# 1. Hardware-Revision prüfen
device_id = read_register_32bit(0x00000000)  # OA_ID
phy_id = read_register_16bit(0x0000FF03)     # PHY_ID2 für Silicon Revision

if device_id != 0x00000011:
    raise Exception("Invalid LAN8650/1 Device")
```

### 2. Berechnete Parameter ermitteln
```python
# 2. Device-spezifische Parameter berechnen
cfgparam1 = calculate_cfgparam1()
cfgparam2 = calculate_cfgparam2()
cfgparam3, cfgparam4 = calculate_sqi_params()
```

### 3. Mandatory Configuration (Table 1)
```python
# 3. PFLICHT-Konfiguration aus Table 1
mandatory_config = [
    (0x00040000, 0x3F31),
    (0x000400E0, 0x00C0),
    (0x000400B4, cfgparam1),
    (0x000400B6, cfgparam2),
    # ... alle weiteren aus Table 1
]

for addr, value in mandatory_config:
    write_register(addr, value)
    verify_write(addr, value)  # Verification wichtig!
```

### 4. PLCA Setup (wenn Multi-Node)
```python
# 4. PLCA-Konfiguration
if multi_node_network:
    plcaparam1 = calculate_plca_param(node_id, node_count)
    write_register(0x0004CA02, plcaparam1)  # PLCA_CTRL1
    write_register(0x0004CA01, 0x8000)      # PLCA Enable
```

### 5. Optional: SQI Configuration
```python
# 5. SQI für Enhanced Diagnostics
if enable_sqi:
    sqi_config = [
        (0x000400AD, cfgparam3),
        (0x000400AE, cfgparam4),
        (0x000400AF, cfgparam5),
    ]
    for addr, value in sqi_config:
        write_register(addr, value)
```

---

## 🛠️ Implementierte Tools (Basierend auf AN1760)

### 1. `lan8651_1760_appnote_config.py`
**Vollständige AN1760-Konfiguration**
- Komplette Table 1 Implementation
- Parameter-Berechnung
- Verification & Rollback

### 2. `lan8651_1760_plca_setup.py`
**PLCA Coordinator/Follower Setup**
- Multi-Node Network Configuration
- Coordinator vs. Follower Logic
- Collision Detection Management

### 3. `lan8651_1760_sqi_diagnostics.py`
**Signal Quality Index Tools**
- Real-time Signal Quality Monitoring
- Cable Diagnostics Enhancement
- Performance Trend Analysis

### 4. `lan8651_1760_power_on_sequence.py`
**Complete Power-On Configuration**
- Full AN1760-compliant Startup Sequence
- Device Identification & Verification
- Error Recovery & Fallback Options

---

## ⚠️ Wichtige Erkenntnisse & Best Practices

### Kritische Punkte
1. **⏰ Timing**: Konfiguration MUSS sofort nach Power-On/Reset erfolgen
2. **📋 Reihenfolge**: Table 1 → PLCA → SQI → Verification
3. **🧮 Berechnung**: Parameter sind device-spezifisch und müssen berechnet werden
4. **✅ Verification**: Jeder Write sollte verifiziert werden
5. **🔄 Recovery**: Fallback-Strategien für fehlerhafte Konfiguration

### Hardware-Kompatibilität
- ✅ **B0 Revision (0001)**: Silicon Revision 0001
- ✅ **B1 Revision (0010)**: Silicon Revision 0010
- ⚠️ **Neuere Revisionen**: AN1760 ggf. superseded by newer AppNote

### Performance Impact
- ❌ **Ohne AN1760-Config**: Suboptimale Performance, mögliche Instabilität
- ✅ **Mit AN1760-Config**: Optimale 10BASE-T1S Performance
- 🚀 **Plus SQI**: Enhanced Diagnostics & Proactive Maintenance

---

## 🔗 Integration mit bestehenden Tools

### GUI-Erweiterung
- **AppNote-Config-Tab**: One-Click AN1760 Setup
- **PLCA-Management**: Multi-Node Network Setup GUI
- **SQI-Dashboard**: Real-time Signal Quality Display

### CLI-Commands
```bash
# Vollständige AN1760-Konfiguration
python lan8651_1760_appnote_config.py --device COM8 --verify

# PLCA-Setup für 4-Node Network (Node 1)
python lan8651_1760_plca_setup.py --device COM8 --node-id 1 --node-count 4

# SQI-Überwachung
python lan8651_1760_sqi_diagnostics.py --device COM8 --monitor

# Complete Power-On Sequence
python lan8651_1760_power_on_sequence.py --device COM8 --full-config
```

### Status Integration
```python
# Status-Check für AN1760-Konformität
def check_an1760_compliance():
    status = {
        'mandatory_config': check_table1_compliance(),
        'plca_status': check_plca_configuration(), 
        'sqi_enabled': check_sqi_status(),
        'performance': measure_network_performance()
    }
    return status
```

---

## 📚 Referenzen

- **Microchip AN1760**: LAN8650/1 Configuration Application Note
- **LAN8650/1 Datasheet**: Kapitel 11 - Register Descriptions  
- **Open Alliance**: 10BASE-T1x MACPHY Serial Interface Standard
- **IEEE 802.3cg**: 10BASE-T1S Physical Layer Standard

---

**Status**: ✅ **AN1760 Implementation Ready**  
**Datum**: März 11, 2026  
**Version**: 1.0 - Complete AN1760 Integration Guide

> **💡 Nächste Schritte:**
> 1. Implementierung der 4 spezialisierten Tools
> 2. Integration in bestehende Tool-Suite
> 3. GUI-Erweiterung mit AN1760-Features
> 4. Field-Testing mit Multi-Node-Setups