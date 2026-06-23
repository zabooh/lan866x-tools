# LAN8651 Test-Modi - Vollständige Dokumentation

## Übersicht

Dieses Dokument beschreibt alle verfügbaren Test-Modi für den **LAN8651 10BASE-T1S MAC-PHY Controller** basierend auf dem offiziellen Microchip-Datenblatt und den IEEE 802.3-2022 Standards.

**Hardware**: Microchip LAN8650/1 10BASE-T1S Ethernet MAC-PHY Controller  
**Kommunikation**: TC6-Protokoll über SPI  
**Standards**: IEEE Std 802.3-2022, Clause 147.5.2  
**Datum**: März 10, 2026  

---

## 🔬 IEEE Test-Modi (T1STSTCTL Register)

### Register-Details
- **Register-Name**: T1STSTCTL (10BASE-T1S Test Mode Control)
- **MMS**: 3 (PHY PMA/PMD Registers)
- **Offset**: 0x08FB
- **Vollständige Adresse**: `0x000308FB`
- **Bitfeld**: 15:13 (TSTCTL[2:0])
- **Access**: R/W
- **Reset-Wert**: 0x0000

### Verfügbare Test-Modi

| TSTCTL[2:0] | Modus | Beschreibung | IEEE Reference |
|-------------|-------|--------------|----------------|
| **000** | Normal Operation | Normaler Betrieb (kein Test) | Standard |
| **001** | Test Mode 1 | Transmitter output voltage, timing jitter | IEEE 802.3-2022 §147.5.2 |
| **010** | Test Mode 2 | Transmitter output droop | IEEE 802.3-2022 §147.5.2 |
| **011** | Test Mode 3 | Transmitter PSD mask | IEEE 802.3-2022 §147.5.2 |
| **100** | Test Mode 4 | Transmitter high impedance mode | IEEE 802.3-2022 §147.5.2 |
| **101** | Reserved | - | - |
| **110** | Reserved | - | - |
| **111** | Reserved | - | - |

### Test-Modi Beschreibung

#### **Test Mode 1 - Transmitter Output Voltage & Timing Jitter**
- **Zweck**: Überprüfung der Ausgangsspannung und Timing-Jitter
- **Anwendung**: Compliance-Tests für Transmitter-Eigenschaften
- **Messungen**: Spannungspegel, Signal-Timing

#### **Test Mode 2 - Transmitter Output Droop**
- **Zweck**: Messung der Transmitter-Output-Droop-Charakteristik
- **Anwendung**: Signalintegrität-Tests
- **Messungen**: Langzeit-Signalstabilität

#### **Test Mode 3 - Transmitter PSD Mask**
- **Zweck**: Power Spectral Density (PSD) Mask-Compliance
- **Anwendung**: EMV/EMI-Tests, Spektrum-Compliance
- **Messungen**: Frequenzspektrum, Störaussendung

#### **Test Mode 4 - Transmitter High Impedance**
- **Zweck**: Transmitter in High-Impedance-Zustand
- **Anwendung**: Kabeldiagnose, Multi-Drop-Tests
- **Verhalten**: Transmitter-Ausgänge hochohmig

---

## 🔄 PMA Loopback-Modus (T1SPMACTL Register)

### Register-Details
- **Register-Name**: T1SPMACTL (10BASE-T1S PMA Control)
- **MMS**: 3 (PHY PMA/PMD Registers)
- **Offset**: 0x08F9
- **Vollständige Adresse**: `0x000308F9`
- **Bit**: 0 (LBE - PMA Loopback Enable)
- **Access**: R/W
- **Reset-Wert**: 0

### Loopback-Funktionalität

**Datenpath beim PMA Loopback**:
```
MAC → PCS Scrambler/Descrambler → 4B/5B Encoder/Decoder → 
PMA Differential Manchester Encoder/Decoder → zurück zum MAC
```

**Anwendungsfälle**:
- **Interne Hardware-Tests**: Überprüfung der PMA-Funktionalität
- **Systemdiagnose**: Isolierung von PHY-Problemen
- **Entwicklungs-Tests**: Verifikation der Datenpath-Integrität
- **Automated Testing**: Self-Test-Routinen

### ⚠️ Wichtige Einschränkungen
- **PLCA muss deaktiviert werden** ODER als **PLCA Coordinator (Local ID = 0)** konfiguriert sein
- **Keine externe Kommunikation** während Loopback aktiv
- **Reset erforderlich** nach Loopback-Deaktivierung für normale Operation

---

## 🛠️ Zusätzliche Test/Debug-Funktionen

### T1SPMACTL Register - Vollständige Beschreibung

| Bit | Name | Funktion | Access | Reset |
|-----|------|----------|--------|-------|
| **15** | RST | PMA Reset (self-clearing) | R/W SC | 0 |
| **14** | TXD | Transmit Disable | R/W | 0 |
| **13:12** | Reserved | - | RO | 00 |
| **11** | LPE | Low Power Enable | R/W | 0 |
| **10** | MDE | Multidrop Enable | R/W | 0 |
| **9:1** | Reserved | - | RO | 000000000 |
| **0** | LBE | PMA Loopback Enable | R/W | 0 |

#### Funktionsbeschreibungen

**RST (Bit 15) - PMA Reset**
- Setzt das PMA zurück (self-clearing)
- ⚠️ **Nicht zusammen mit anderen Bits setzen**

**TXD (Bit 14) - Transmit Disable**
- Deaktiviert den PMA Transmit-Path
- Muss für normalen Betrieb auf 0 stehen

**LPE (Bit 11) - Low Power Enable**  
- Aktiviert Low-Power-Modus des PMA
- Entspricht Power Down Bit im BASIC_CONTROL Register

**MDE (Bit 10) - Multidrop Enable**
- Aktiviert Multidrop-Operation auf Mixing Segment
- ℹ️ **Hat keinen Effekt auf Device-Operation**

---

## 💻 Praktische Anwendung

### Basis-Commands

```bash
# Register lesen
lan_read 0x000308FB   # Test Mode Control Status
lan_read 0x000308F9   # PMA Control Status

# Test Mode aktivieren
lan_write 0x000308FB 0x2000   # Test Mode 1 (TSTCTL = 001)
lan_write 0x000308FB 0x4000   # Test Mode 2 (TSTCTL = 010)  
lan_write 0x000308FB 0x6000   # Test Mode 3 (TSTCTL = 011)
lan_write 0x000308FB 0x8000   # Test Mode 4 (TSTCTL = 100)

# PMA Loopback aktivieren
lan_write 0x000308F9 0x0001   # LBE = 1

# Zurück zum normalen Betrieb
lan_write 0x000308FB 0x0000   # Normal Operation
lan_write 0x000308F9 0x0000   # Loopback deaktiviert
```

### Erweiterte Konfiguration

```bash
# Kombinierte Konfiguration (Beispiel)
lan_write 0x000308F9 0x4000   # Transmit Disable (TXD = 1)
lan_write 0x000308F9 0x0800   # Low Power Enable (LPE = 1)
lan_write 0x000308F9 0x8000   # PMA Reset (RST = 1)
```

---

## 🧪 Test-Szenarien

### 1. **IEEE Compliance Tests**

```bash
# Test Mode 1 - Voltage & Jitter
lan_write 0x000308FB 0x2000
# → Messungen mit Oszilloskop durchführen

# Test Mode 3 - PSD Mask  
lan_write 0x000308FB 0x6000
# → Spektrumanalyser-Messungen durchführen
```

### 2. **Interne Hardware-Verifikation**

```bash
# PLCA deaktivieren (falls erforderlich)
lan_write 0x0200004A 0x0000   # PLCA_CTRL_STS deaktiviert

# PMA Loopback aktivieren
lan_write 0x000308F9 0x0001
# → Datenverkehr-Tests durchführen

# Loopback deaktivieren
lan_write 0x000308F9 0x0000
```

### 3. **Produktions-Test-Sequenz**

```bash
# 1. System Reset
lan_write 0x000308F9 0x8000   # PMA Reset

# 2. Loopback-Test
lan_write 0x000308F9 0x0001   # Loopback aktivieren
# → Automatische Datenintegritäts-Tests

# 3. IEEE Test-Modi durchlaufen
lan_write 0x000308FB 0x2000   # Test Mode 1
# → Automatische Messungen
lan_write 0x000308FB 0x4000   # Test Mode 2
# → Automatische Messungen

# 4. Zurück zu Normal
lan_write 0x000308FB 0x0000
lan_write 0x000308F9 0x0000
```

---

## 🔧 Entwickelte Test-Tools

### Kommandozeilen-Tools
- **lan8651_complete_register_scan.py** - Kann Register-Status vor/nach Tests überprüfen
- **lan8651_quick_status.py** - Schnelle Status-Überprüfung
- **test_register_types.py** - Register-Kategorien-Analyse

### Empfohlene neue Test-Tools

#### **test_modes_control.py** (zu entwickeln)
```python
# Beispiel-Struktur
def activate_test_mode(mode):
    """Aktiviert IEEE Test Mode 1-4"""
    
def activate_pma_loopback():
    """Aktiviert PMA Loopback mit PLCA-Check"""
    
def run_compliance_test_suite():
    """Führt alle IEEE Compliance Tests durch"""
```

#### **loopback_diagnostic.py** (zu entwickeln)
```python
def run_loopback_test():
    """Vollständiger PMA Loopback-Test mit Datenintegrity"""
    
def measure_loopback_performance():
    """Performance-Metriken während Loopback"""
```

---

## ⚠️ Sicherheitshinweise

### **Kritische Warnungen**
1. **⛔ Test-Modi nur in kontrollierten Umgebungen verwenden**
2. **⚡ IEEE Test-Modi können Störaussendungen erzeugen**
3. **🔗 PMA Loopback unterbricht externe Kommunikation**
4. **🔄 Nach Tests immer auf Normal Operation zurücksetzen**
5. **📡 Test Mode 4 (High-Z) kann Netzwerk-Kommunikation stören**

### **Best Practices**
- **Isolation**: Tests in isolierten Netzwerksegmenten durchführen
- **Dokumentation**: Alle Test-Konfigurationen dokumentieren
- **Reset**: Vollständigen Reset nach Test-Sequenzen
- **Monitoring**: Kontinuierliches Monitoring während Tests
- **EMV**: Beachtung von EMV-Vorschriften bei Compliance-Tests

---

## 📊 Register-Referenz Tabelle

| Register | Name | MMS | Offset | Vollst. Adresse | Funktion |
|----------|------|-----|--------|-----------------|----------|
| T1STSTCTL | Test Mode Control | 3 | 0x08FB | 0x000308FB | IEEE Test-Modi 1-4 |
| T1SPMACTL | PMA Control | 3 | 0x08F9 | 0x000308F9 | PMA Loopback & Control |
| T1SPMASTS | PMA Status | 3 | 0x08FA | 0x000308FA | PMA Status (Read-Only) |
| PLCA_CTRL_STS | PLCA Control | 2 | 0x004A | 0x0200004A | PLCA für Loopback |

---

## 📚 Referenzen

- **LAN8650/1 Datasheet**: Microchip Technology, Kapitel 11
- **IEEE Std 802.3-2022**: Clause 147.5.2 - 10BASE-T1S Test Modes
- **Open Alliance 10BASE-T1x MAC-PHY**: Serial Interface Specification
- **TC6 Specification**: 10BASE-T1x MACPHY Serial Interface

---

## 🚀 Quick Start für Test-Modi

### Für IEEE Compliance Tests:
```bash
cd C:\work\t1s\t1s_100baset_bridge\firmware\T1S_100BaseT_Bridge.X

# Status prüfen
python lan8651_quick_status.py

# Test Mode aktivieren (Beispiel: Mode 1)
# COM-Terminal öffnen oder direkt:
# lan_write 0x000308FB 0x2000
```

### Für PMA Loopback Tests:
```bash
# PLCA-Status prüfen
# lan_read 0x0200004A

# Loopback aktivieren  
# lan_write 0x000308F9 0x0001

# Tests durchführen...

# Loopback deaktivieren
# lan_write 0x000308F9 0x0000
```

---

**Datum**: März 10, 2026  
**Autor**: T1S Bridge Development Team  
**Version**: 1.0

**Status**: ✅ **Test-Modi identifiziert und dokumentiert - bereit für Implementation**

---

> **💡 Nächste Schritte:**  
> 1. **Test-Scripts entwickeln** basierend auf dieser Dokumentation
> 2. **Automated Test Suite** für alle Test-Modi implementieren  
> 3. **Compliance Test Procedures** für IEEE Standards definieren
> 4. **Production Test Integration** für Qualitätssicherung