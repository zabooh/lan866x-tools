# T1S Bridge CLI Command Reference

## 📋 **Übersicht**
**Projekt**: T1S 100BaseT Bridge  
**CLI Framework**: Harmony 3 System Command Processor  
**Zugriff**: UART Console (115200) + Telnet (Port 23)  
**Datum**: März 7, 2026  
**Version**: 1.0  

Das T1S Bridge CLI bietet vollständigen Zugriff auf alle Firmware-Funktionen für Entwicklung, Debugging und Produktionsdiagnostik.

---

## 🚀 **CLI Zugriff**

### **UART Console (Empfohlen für Development)**
- **Port**: SERCOM1 @ 115200 Baud, 8N1
- **Pin**: Debug UART
- **Terminal**: PuTTY, TeraTerm, MPLAB X IDE Terminal

### **Telnet Remote Access**
- **Port**: 23 (Standard Telnet)
- **IP Adressen**:
  - `192.168.1.10` (LAN865X T1S Interface)  
  - `192.168.1.11` (GMAC 100BaseT Interface)
- **Verbindung**: `telnet 192.168.1.10 23`

### **Command Syntax**
```bash
# Alle Commands werden direkt ausgeführt (KEINE Prefixes erforderlich!)
help                          # Zeigt alle verfügbaren Commands

# Direct Command Execution:
<kommando> [parameter...]     # Direkte Kommando-Ausführung

# Command Historie (UP/DOWN Pfeil-Tasten unterstützt)
# Tab Completion verfügbar für Kommando-Namen
```

---

## 🌐 **TCP/IP Stack Commands** *(Kein Prefix erforderlich!)*

### **Network Information & Status**
```bash
# Network Status anzeigen (wichtigstes Kommando!)
netinfo                 # Zeigt alle Interfaces: IP, MAC, Link Status, Statistics

# MAC Layer Statistiken  
macinfo                 # TX/RX Packets, Errors, Buffer Status

# Default Interface Management
defnet                  # Aktuelles Default Interface anzeigen
defnet lan865x         # LAN865X als Default setzen
defnet gmac            # GMAC als Default setzen
```

### **Network Configuration**
```bash
# IP-Konfiguration
setip <interface> <ip> <netmask>
setip lan865x 192.168.1.10 255.255.255.0
setip gmac 192.168.1.11 255.255.255.0

# Gateway und DNS
setgw <gateway_ip>      # Gateway setzen  
setgw 192.168.1.1
setdns <dns_ip>         # DNS Server setzen
setdns 8.8.8.8

# MAC Address (falls erforderlich)
setmac <interface> <mac>
setmac lan865x 00:04:A3:12:34:56

# NetBIOS Name
setbios T1SBridge       # Hostname setzen
```

### **Interface Management**
```bash
# Interface Up/Down Control
if up <interface>       # Interface aktivieren
if down <interface>     # Interface deaktivieren
if up lan865x          # T1S Interface aktivieren
if down gmac           # 100BaseT Interface deaktivieren

# Stack Control  
stack up               # Gesamten TCP/IP Stack aktivieren
stack down             # Stack deaktivieren (für Wartung)
```

### **DHCP Services**
```bash
# DHCP Client
dhcp start <interface> # DHCP Client starten
dhcp stop <interface>  # DHCP Client stoppen
dhcp info <interface>  # DHCP Client Status
dhcp renew <interface> # DHCP Lease erneuern

# DHCP Server (falls aktiviert)
dhcps start            # DHCP Server starten
dhcps stop             # DHCP Server stoppen
dhcpsinfo              # Lease-Informationen anzeigen
```

### **🌉 Bridge Commands (WICHTIG!)**
```bash
# MAC Bridge Status und Statistiken
bridge                 # Bridge Table, Statistics, Forward Count
bridge flush           # Bridge Table leeren  
bridge aging <seconds> # Aging Time setzen
bridge stats           # Detaillierte Bridge Statistiken

# Beispiel Output:
# Bridge Status: ENABLED
# Interface 0 (lan865x): TX: 1234, RX: 5678
# Interface 1 (gmac): TX: 5678, RX: 1234  
# FDB Entries: 15/500
```

### **Network Diagnostics**
```bash
# ICMP Ping Tests
ping <ip_address>       # IPv4 Ping
ping 192.168.1.1       # Gateway ping
ping 8.8.8.8           # Internet-Konnektivität testen
ping6 <ipv6_address>   # IPv6 Ping (falls IPv6 aktiviert)

# ARP Table Management
arp                    # ARP Cache anzeigen
arp flush              # ARP Cache leeren
arp add <ip> <mac>     # Statischen ARP-Eintrag hinzufügen
arp del <ip>           # ARP-Eintrag löschen
```

### **DNS Services**
```bash
# DNS Client (Auflösung)
dnsc lookup <hostname> # DNS Auflösung durchführen  
dnsc lookup google.com # Beispiel: google.com auflösen
dnsc cache             # DNS Cache anzeigen
dnsc flush             # DNS Cache leeren

# DNS Server (falls aktiviert)
dnss start             # DNS Server starten
dnss stop              # DNS Server stoppen  
dnss add <name> <ip>   # DNS-Eintrag hinzufügen
```

### **Advanced Protocol Commands**
```bash
# TCP Socket Diagnostics
tcp                    # Aktive TCP Verbindungen
tcptrace <on|off>      # TCP Trace aktivieren/deaktivieren

# UDP Socket Diagnostics  
udp                    # Aktive UDP Sockets

# MIIM PHY Management (für LAN8740)
miim scan              # PHY Scan durchführen
miim read <phy_addr> <reg> # PHY Register lesen
miim write <phy_addr> <reg> <value> # PHY Register schreiben
```

### **Memory & Performance**
```bash
# Heap Information
heapinfo               # TCP/IP Stack Heap Status
heaplist               # Heap Block Liste (falls verfügbar)

# Packet Management
pktinfo                # Packet Allocation Info
plog                   # Packet Flight Log (Debug)
```

---

## 🔧 **LAN8651 Commands** *(Kein Prefix erforderlich!)*

### **🎯 LAN8651 Register Access (Wichtigste Kommandos!)**
```bash
# Register lesen (Hex-Adressen)
lan_read <address_hex>

# Wichtige Register-Beispiele:
lan_read 0x00000000     # OA_ID - Device ID (erwartet: 0x00000011)
lan_read 0x00000001     # OA_PHYID - PHY Identifier  
lan_read 0x00000002     # OA_STDCAP - Standard Capabilities
lan_read 0x00000003     # OA_RESET - Reset Control
lan_read 0x00000004     # OA_CONFIG0 - Configuration Register 0
lan_read 0x00000008     # OA_STATUS0 - Status Register 0
lan_read 0x00000009     # OA_STATUS1 - Status Register 1
lan_read 0x0000000B     # OA_BUFSTS - Buffer Status

# PHY Clause 22 Register
lan_read 0x0000FF00     # PHY_BASIC_CONTROL - PHY Control
lan_read 0x0000FF01     # PHY_BASIC_STATUS - PHY Status (Link!)
lan_read 0x0000FF02     # PHY_ID1 - PHY Identifier 1  
lan_read 0x0000FF03     # PHY_ID2 - PHY Identifier 2

# Register schreiben
lan_write <address_hex> <value_hex>

# Konfiguration Beispiele:
lan_write 0x00000003 0x1    # Software Reset
lan_write 0x00000004 0x8020 # SYNC + PROTE aktivieren
lan_write 0x0000FF00 0x8000 # PHY Software Reset
```

### **Bridge Debugging & Control**
```bash
# Packet Forwarding Control
fwd                     # Forwarding Status anzeigen  
fwd 1                   # Packet Forwarding aktivieren
fwd 0                   # Packet Forwarding deaktivieren

# RX Packet Monitoring
ipdump                  # Aktuelle Einstellung anzeigen
ipdump 0                # Packet Dump deaktivieren
ipdump 1                # Nur eth0 (LAN865X) überwachen
ipdump 2                # Nur eth1 (GMAC) überwachen  
ipdump 3                # Beide Interfaces überwachen
```

### **System Information**
```bash
# Development Info
help                    # Verfügbare Commands anzeigen
timestamp               # Build Timestamp und Version anzeigen
```

---

## ⚡ **iPerf Network Performance Testing** *(Kein Prefix erforderlich!)*

### **Performance Tests**
```bash
# iPerf Server starten (auf Bridge)
iperf -s                     # Server Mode auf Default Interface
iperf -s -p 5001            # Server auf Port 5001

# iPerf Client (von Bridge zu Remote Host)  
iperf -c <server_ip>         # Client Mode zu Server
iperf -c 192.168.1.100      # Test zu Remote Host
iperf -c 192.168.1.100 -t 30 # 30 Sekunden Test
iperf -c 192.168.1.100 -u   # UDP Test

# Interface spezifizieren
iperfi <ip_address>          # Interface für iPerf setzen
iperfi 192.168.1.10         # LAN865X Interface verwenden
iperfi 192.168.1.11         # GMAC Interface verwenden

# Buffer Größen konfigurieren
iperfs tx 1500              # TX Buffer Size setzen
iperfs rx 1500              # RX Buffer Size setzen

# Test stoppen
iperfk                      # Laufenden iPerf Test beenden
```

---

## 🛠️ **Praktische Anwendungsfälle**

### **🚀 Erstes Boot und System-Verifikation**
```bash
# 1. System Status prüfen
netinfo               # Interface Status, IPs, Link Status

# 2. LAN8651 Hardware verifizieren  
lan_read 0x00000000   # Device ID (sollte 0x00000011 sein)
lan_read 0x0000FF01   # PHY Status (Bit 2 = Link Status)

# 3. Bridge Funktionalität testen
bridge               # Bridge Status und Statistics
fwd 1                 # Forwarding aktivieren

# 4. Konnektivität testen
ping 192.168.1.1    # Gateway erreichbar?
```

### **🔍 Network Troubleshooting**
```bash
# Link Status diagnostizieren
netinfo              # Beide Interface Link Status
lan_read 0x0000FF01   # T1S PHY Link Status (Bit 2)
miim read 1 1        # 100BaseT PHY Status (MIIM)

# Packet Loss analysieren
macinfo              # Interface Statistics  
bridge               # Bridge Forward Statistics
ipdump 3              # Packet Flow überwachen

# ARP Probleme lösen
arp                  # ARP Table prüfen
ping <target>        # Connectivity testen
arp flush            # ARP Cache zurücksetzen
```

### **⚙️ LAN8651 Configuration & Tuning**
```bash
# Standard Configuration anwenden
lan_write 0x00000004 0x8020  # SYNC + PROTE aktivieren
lan_write 0x0000000C 0x0     # Alle Interrupts maskieren  
lan_write 0x0000000D 0x0     # Alle Interrupts maskieren

# PLCA Configuration (Multi-Drop T1S)
# (Erfordert spezifische MMS_2 Register - siehe Datenblatt)

# Cut-Through Mode aktivieren
lan_write 0x00000004 0x8320  # SYNC + PROTE + TXCTE + RXCTE

# Status monitoring
lan_read 0x00000008          # STATUS0 - Error Flags
lan_read 0x00000009          # STATUS1 - Extended Status
```

### **📊 Performance Monitoring & Testing**
```bash
# Baseline Performance messen
iperf -c 192.168.1.100 -t 60     # 60s Durchsatz-Test

# Bridge Performance überwachen  
bridge                      # Forward Rate
macinfo                     # Interface Statistics
heapinfo                    # Memory Usage

# Beide Schnittstellen parallel testen
ipdump 3                     # Traffic Monitoring aktivieren
# Terminal 2: iperf Tests auf beiden Interfaces
```

### **🐛 Debug und Development**
```bash
# Register Monitoring Script (wiederhole in Loop)
lan_read 0x00000008    # STATUS0 errors
lan_read 0x00000009    # STATUS1 extended status  
lan_read 0x0000000B    # Buffer Status

# TCP/IP Debug aktivieren
tcptrace on           # TCP Flow Debug
plog                  # Packet Flight Recorder

# Interface Reset bei Problemen  
if down lan865x       # Interface down
# wait 2 seconds
if up lan865x         # Interface up
```

---

## 🚨 **Error Codes und Troubleshooting**

### **Häufige LAN8651 Status Codes**
```bash
# STATUS0 Register (0x00000008) Bits:
# Bit 0: TXPE (TX Protocol Error)
# Bit 1: TXBOE (TX Buffer Overflow Error)  
# Bit 2: TXBUE (TX Buffer Underflow Error)
# Bit 3: RXBOE (RX Buffer Overflow Error)
# Bit 4: LOFE (Loss of Frame Error)
# Bit 5: HDRE (Header Error)
# Bit 6: RESETC (Reset Complete)
# Bit 7: PHYINT (PHY Interrupt)

# Beispiel Diagnose:
lan_read 0x00000008
# Output: 0x00000010 = LOFE (Loss of Frame Error)
# → T1S Link Problem, Kabel prüfen!
```

### **Bridge Troubleshooting**
```bash
# Bridge nicht forwarding:
bridge                # Bridge Status prüfen
fwd                    # Forwarding aktiviert?
if                    # Beide Interfaces up?

# Performance Problems:
heapinfo              # Memory shortage?
macinfo               # Interface errors?
iperf -c target             # Actual throughput?
```

---

## 📚 **Command Quick Reference**

### **Must-Know Commands (Top 10)**
```bash
netinfo               # Complete network status  
bridge                # Bridge statistics
lan_read 0x00000000   # LAN8651 Device ID
lan_read 0x0000FF01   # T1S PHY Link Status  
ping <ip>             # Connectivity test
fwd 1                  # Enable forwarding
ipdump 3              # Monitor packet flow
macinfo              # Interface statistics
iperf -c <server>          # Performance test
help                       # Show all available commands
```

### **Most Used LAN8651 Registers**
```bash
0x00000000  # OA_ID - Device Identification
0x00000004  # OA_CONFIG0 - Main Configuration  
0x00000008  # OA_STATUS0 - Error Status
0x0000FF01  # PHY_BASIC_STATUS - Link Status
0x0000000B  # OA_BUFSTS - Buffer Status
```

---

## 💡 **Tips & Best Practices**

### **Development Workflow**
1. **Start**: `netinfo` - System Status prüfen
2. **Verify**: `lan_read 0x00000000` - Hardware OK?
3. **Configure**: Bridge und Interface Settings anpassen
4. **Test**: `ping` + `iperf` für Funktionalität + Performance
5. **Monitor**: `bridge` + `ipdump` für kontinuierliches Monitoring

### **Production Diagnostics**
1. **`netinfo`** - Kompletter System Status
2. **`bridge`** - Bridge Funktionalität OK?  
3. **`lan_read 0x0000FF01`** - T1S Link Status
4. **`macinfo`** - Interface Error Counters
5. **`ping <gateway>`** - Basic Connectivity

### **Remote Access**
- **Primär**: Telnet für Remote-Zugriff
- **Backup**: UART für Hardware-Debug
- **Security**: Telnet nur in vertrauenswürdigen Netzwerken verwenden

---

## ✅ **Command Availability Matrix**

| Command Category | UART Console | Telnet | Beschreibung |
|------------------|--------------|---------|--------------|  
| Network Commands | ✅ | ✅ | Network Stack Commands |
| LAN8651 Commands | ✅ | ✅ | LAN8651 & Bridge Debug |
| Performance Commands | ✅ | ✅ | Performance Testing |  
| Global Commands | ✅ | ✅ | Global Help System |

**Status**: ✅ **Vollständige CLI-Referenz - Production Ready**  
**Datum**: März 7, 2026  
**Version**: 1.0  
**Support**: T1S Bridge Development Team