# LAN8651 Register Scanner GUI

## 📋 Übersicht

Grafische Benutzeroberfläche für die Analyse der LAN8651 10BASE-T1S MAC-PHY Register mit Tab-basierter Organisation und robuster serieller Kommunikation.

**Basiert auf**: `lan8651_complete_register_scan.py`  
**Framework**: tkinter (Standard Python GUI)  
**Features**: Prompt-basierte Synchronisation, Threading, Einzelgruppen-Scan  
**Version**: 1.0 (März 2026)

---

## 🚀 Quick Start

### **1. Starten der GUI**
```bash
# Empfohlen (aktueller Entwicklungsstand)
python lan8651_bitfield_gui.py

# Legacy-Variante
python lan8651_register_gui.py
```

### **2. Konfiguration**
1. Öffne den **"🔧 Configuration"** Tab
2. Setze den **COM Port** (normalerweise COM8)
3. Klicke **"Test Connection"** um die Verbindung zu prüfen
4. Warte auf **"✅ Connected - Communication OK"**

### **3. Register scannen**
1. Wähle den gewünschten **Register-Gruppen Tab**:
   - **MMS_0** - Open Alliance Standard + PHY Clause 22
   - **MMS_1** - MAC Registers  
   - **MMS_2** - PHY PCS Registers
   - **MMS_3** - PHY PMA/PMD Registers
   - **MMS_4** - PHY Vendor Specific
   - **MMS_10** - Miscellaneous Registers

2. Klicke **"📡 Scan [Gruppe] Registers"**
3. **Warte** auf den Scan-Abschluss (Progress wird angezeigt)
4. **Ergebnisse** werden in der Tabelle angezeigt

---

## 🖥️ GUI-Funktionen

### **Tab-basierte Organisation**
- **Configuration Tab**: COM-Port Setup und Verbindungstest
- **6 Register-Gruppen Tabs**: Individuelle Scans pro MMS-Gruppe
- **Status Bar**: Aktuelle Aktivität und Verbindungsstatus

### **Register-Tabellen**
| Spalte | Beschreibung |
|--------|--------------|
| **Address** | Hexadezimale Register-Adresse |
| **Name** | Register-Name (z.B. OA_ID, MAC_NCR) |
| **Description** | Detaillierte Beschreibung |
| **Expected** | Erwarteter Wert aus Datenblatt |
| **Actual** | Tatsächlich gelesener Wert |
| **Status** | ✅ OK / ❌ Abweichung / ℹ️ Variable |
| **Access** | R (Read) / R/W (Read/Write) |
| **Width** | 16-bit / 32-bit |

### **Farbkodierung**
- 🟢 **Grün**: Wert entspricht Erwartung (✅)
- 🔴 **Rot**: Wert weicht ab oder Lesefehler (❌)  
- 🔵 **Blau**: Variable Werte (ℹ️)

---

## ⚙️ Technische Details

### **Robuste Kommunikation**
- **Prompt-basierte Synchronisation**: Wartet auf '>' Zeichen
- **Timeout-Schutz**: 3 Sekunden pro Register
- **Race-Condition-frei**: Keine fixen Delays
- **Threading**: GUI bleibt responsiv während Scans

### **Register-Gruppen Überblick**

| MMS | Name | Register | Zweck |
|-----|------|----------|-------|
| **MMS_0** | Open Alliance + PHY | 14 | Basis-Funktionen, PHY Status |
| **MMS_1** | MAC Registers | 10 | Ethernet MAC Control |  
| **MMS_2** | PHY PCS | 6 | Physical Coding Sublayer |
| **MMS_3** | PHY PMA/PMD | 4 | Physical Medium Dependent |
| **MMS_4** | Vendor Specific | 2 | Microchip-spezifisch |
| **MMS_10** | Miscellaneous | 2 | Zusätzliche Funktionen |

### **Performance**
- **Pro Register**: ~200-500ms (Prompt-basiert)  
- **Typische Gruppe**: 5-15 Sekunden
- **Erfolgsquote**: >95% bei stabiler Verbindung

---

## 🔧 Systemanforderungen

### **Hardware**
- T1S 100BaseT Bridge Hardware
- USB-Verbindung (COM-Port)
- Windows/Linux/macOS

### **Software** 
```bash
# Python 3.7+
python --version

# Erforderliche Pakete installieren
pip install -r requirements.txt

# Hauptabhängigkeiten:
# - pyserial>=3.5 (serielle Kommunikation)
# - tkinter (GUI - normalerweise mit Python installiert)
```

---

## 🛠️ Troubleshooting

### **"Connection failed"**
```bash
# 1. COM-Port prüfen
# - Device Manager → Ports (COM & LPT)
# - Meist COM8, kann aber variieren

# 2. Brücken-Firmware prüfen  
# - T1S Bridge muss gestartet und erreichbar sein
# - Prompt ">" sollte im Terminal sichtbar sein

# 3. Zugriff prüfen
# - Kein anderes Programm darf COM-Port verwenden
# - Windows: Eventuell als Administrator ausführen
```

### **"No LAN865X response"**  
```bash
# 1. Verbindung OK, aber falsche Firmware
# - Überprüfe, ob T1S Bridge läuft
# - Terminal-Test: "lan_read 0x00000000"

# 2. Baudrate prüfen
# - Standard: 115200
# - Bei Problemen: 9600 oder 57600 testen
```

### **"Scan timeout" / Niedrige Erfolgsquote**
```bash
# 1. Serielle Verbindung langsam
# - USB-Kabel prüfen  
# - Andere COM-Ports testen

# 2. Hardware-Problem
# - LAN8651-Chip Kommunikationsproblem
# - SPI-Bus-Timing-Issues
```

---

## 📊 Vergleich zu Command-Line Tools

| Tool | Interface | Best For |
|------|-----------|----------|
| **`lan8651_bitfield_gui.py`** | GUI | Interaktive Analyse, Bitfelder, Test-Modi |
| **`lan8651_register_gui.py`** | GUI (Legacy) | Klassischer Register-Scan |
| **`lan8651_complete_register_scan.py`** | CLI | Vollständige Scans, Automation |
| **`lan8651_quick_status.py`** | CLI | Schnelle Status-Checks |

---

## 🎯 Anwendungsfälle

### **Entwicklung**
- **Register-Debugging**: Einzelne Gruppen gezielt analysieren  
- **Hardware-Verifikation**: Status-Register überwachen
- **Konfiguration**: R/W Register interaktiv testen

### **Testing**
- **Funktionstest**: Systematische Register-Prüfung
- **Performance**: Kommunikations-Latenz messen  
- **Regression**: Vergleich mit erwarteten Werten

### **Production**  
- **Qualitätskontrolle**: Hardware-Acceptance-Tests
- **Diagnose**: Field-Issue Debugging
- **Wartung**: Zustandsüberwachung

---

**🎉 Die GUI bietet eine benutzerfreundliche Alternative zur Command-Line für interaktive LAN8651 Register-Analyse!**

**Datum**: März 7, 2026  
**Version**: 1.0  
**Support**: T1S Bridge Development Team