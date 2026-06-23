# T1S 100BaseT Bridge Firmware Documentation

## 📋 **Projekt Überblick**
**Projekt**: T1S 100BaseT Bridge  
**Framework**: MPLAB Harmony 3  
**Mikrocontroller**: SAME54P20A (ARM Cortex-M4F)  
**Funktion**: Ethernet Bridge zwischen 10BASE-T1S und 100BASE-T Netzwerken  
**Datum**: März 7, 2026  
**Version**: 1.0  
**Datenblatt**: [LAN8651 HTML Documentation](https://onlinedocs.microchip.com/oxy/GUID-F5813793-E016-46F5-A9E2-718D8BCED496-en-US-14/index.html)

### **Bridge-Funktionalität**
- **T1S Interface**: LAN8651 10BASE-T1S MAC-PHY Controller über SPI
- **100BaseT Interface**: LAN8740 PHY über integrierte GMAC
- **MAC Bridge**: Automatisches Packet-Forwarding zwischen beiden Schnittstellen
- **Management**: Telnet-Zugriff, Command Interface, Register-Debugging

---

## 🏗️ **Hardware Architektur**

### **Hauptkomponenten**
```
SAME54P20A Mikrocontroller
├── LAN8651 (10BASE-T1S MAC-PHY)
│   ├── SPI Interface (SERCOM0) - TC6 Protokoll
│   ├── Interrupt Pin (GPIO)
│   └── Reset Pin (GPIO)
├── LAN8740 (100BASE-T PHY) 
│   ├── GMAC Interface (Integriert)
│   └── MIIM Management Interface
└── Debug Interface
    ├── USART (SERCOM1) - Console/Telnet
    └── SWD Programming Interface
```

### **Pin-Konfiguration**
| Funktion | Pin | Beschreibung |
|----------|-----|--------------|
| **LAN8651 SPI** | SERCOM0 | MOSI, MISO, SCK, CS |
| **LAN8651 INT** | GPIO | Interrupt vom LAN8651 |
| **LAN8651 RST** | GPIO | Reset Control |
| **LAN8740 GMAC** | Integriert | TX/RX Data, CLK |
| **LAN8740 MIIM** | GMAC_MIIM | Management Interface |
| **Debug UART** | SERCOM1 | Console/Command Interface |

---

## 💻 **Software Architektur**

### **Schichtenmodell**
```
┌─────────────────────────────────┐
│         Application Layer       │ ← Bridge Logic, Command Interface
├─────────────────────────────────┤
│         TCP/IP Stack           │ ← Harmony TCP/IP with Bridge Module
├─────────────────────────────────┤
│      MAC/PHY Drivers          │ ← LAN865X, GMAC, ETHPHY Drivers
├─────────────────────────────────┤
│       System Services         │ ← Time, Console, Debug, Command
├─────────────────────────────────┤
│         PLIB Layer            │ ← SPI, USART, GMAC, Timer, GPIO
└─────────────────────────────────┘
```

### **Firmware Module**

#### **🌐 Network Stack** (`/src/config/default/`)
- **TCP/IP Stack**: `library/tcpip/` - Harmony TCP/IP mit Bridge Support
- **MAC Bridge**: Automatisches Forwarding zwischen T1S ↔ 100BaseT
- **Network Services**: ARP, ICMP, UDP, TCP, Telnet, Command Interface

#### **🔧 Hardware Drivers** (`/src/config/default/driver/`)
- **LAN865X Driver**: `driver/lan865x/` - T1S MAC-PHY Treiber mit TC6 Protokoll
- **GMAC Driver**: `driver/gmac/` - Integrierte Ethernet MAC für LAN8740  
- **SPI Driver**: `driver/spi/` - SPI-Kommunikation für LAN8651
- **MIIM Driver**: `driver/miim/` - PHY Management Interface
- **ETHPHY Driver**: `driver/ethphy/` - LAN8740 PHY Control

#### **⚙️ System Services** (`/src/config/default/system/`)
- **SYS_TIME**: System Timer Service für Timeouts
- **SYS_CONSOLE**: Debug Console über UART
- **SYS_CMD**: Command Line Interface  
- **SYS_DEBUG**: Debug Output System

#### **📱 Application Layer** (`/src/`)
- **app.c/app.h**: Hauptanwendungslogik
- **main.c**: System Entry Point

---

## 🚀 **Initialisierungs-Sequenz**

### **SYS_Initialize() Reihenfolge**

#### **1. Hardware/PLIB Initialisierung**
```c
NVMCTRL_Initialize();      // Non-Volatile Memory Controller
PORT_Initialize();         // GPIO Configuration
CLOCK_Initialize();        // System Clock (120MHz)
TC0_TimerInitialize();     // System Timer
SERCOM1_USART_Initialize(); // Debug UART (115200 baud)
SERCOM0_SPI_Initialize();   // SPI für LAN8651 (TC6 Protokoll)
EVSYS_Initialize();        // Event System
DMAC_Initialize();         // DMA Controller
```

#### **2. System Services**
```c
SYS_TIME_Initialize();     // System Timer Service
SYS_CONSOLE_Initialize();  // Console Service  
SYS_CMD_Initialize();      // Command Processor
SYS_DEBUG_Initialize();    // Debug Service
```

#### **3. Communication Drivers**  
```c
DRV_SPI_Initialize();      // SPI Driver für LAN8651
DRV_MIIM_Initialize();     // MIIM Driver für LAN8740
```

#### **4. Network Stack**
```c
NET_PRES_Initialize();     // Network Presentation Layer
TCPIP_STACK_Init();        // TCP/IP Stack mit MAC Bridge
```

#### **5. Application**
```c
CRYPT_WCCB_Initialize();   // Cryptographic Library
APP_Initialize();          // Application Logic
NVIC_Initialize();         // Interrupt Controller
```

---

## 🌉 **Bridge-Konfiguration**

### **Network Configuration** (`TCPIP_HOSTS_CONFIGURATION[]`)

#### **Interface 0: LAN8651 (T1S)**
```c
.interface = "LAN865X",
.hostName = "T1SBridge_LAN865X", 
.macAddr = "00:04:A3:XX:XX:X0",
.ipAddr = "192.168.1.10",
.ipMask = "255.255.255.0", 
.gateway = "192.168.1.1",
.pMacObject = &TCPIP_NETWORK_DEFAULT_MAC_DRIVER_IDX0, // LAN865X
```

#### **Interface 1: LAN8740 (100BaseT)**
```c
.interface = "GMAC",
.hostName = "T1SBridge_GMAC",
.macAddr = "00:04:A3:XX:XX:X1", 
.ipAddr = "192.168.1.11",
.ipMask = "255.255.255.0",
.gateway = "192.168.1.1", 
.pMacObject = &TCPIP_NETWORK_DEFAULT_MAC_DRIVER_IDX1, // GMAC
```

### **MAC Bridge Table** (`tcpipMacbridgeTable[]`)
```c
TCPIP_MAC_BRIDGE_ENTRY_BIN tcpipMacbridgeTable[2] = {
    {0}, // Bridge Eintrag für Interface 0 (LAN865X)
    {1}, // Bridge Eintrag für Interface 1 (GMAC)  
};
```

### **PLCA Configuration** (T1S Multi-Drop)
```c
.nodeId = DRV_LAN865X_PLCA_NODE_ID_IDX0,     // Node ID im T1S Netzwerk
.nodeCount = DRV_LAN865X_PLCA_NODE_COUNT_IDX0, // Anzahl Nodes
.burstCount = DRV_LAN865X_PLCA_BURST_COUNT_IDX0, // Burst Count
.burstTimer = DRV_LAN865X_PLCA_BURST_TIMER_IDX0, // Burst Timer
.plcaEnable = DRV_LAN865X_PLCA_ENABLE_IDX0,   // PLCA Enable
```

---

## 📡 **Network Services**

### **TCP/IP Stack Module** (`TCPIP_STACK_MODULE_CONFIG_TBL[]`)
```c
{TCPIP_MODULE_IPV4,             &tcpipIPv4InitData},      // IPv4 Protocol
{TCPIP_MODULE_ICMP,             0},                      // ICMP (Ping)
{TCPIP_MODULE_ARP,              &tcpipARPInitData},      // Address Resolution  
{TCPIP_MODULE_UDP,              &tcpipUDPInitData},      // UDP Sockets
{TCPIP_MODULE_TCP,              &tcpipTCPInitData},      // TCP Sockets
{TCPIP_MODULE_TELNET_SERVER,    &tcpipTelnetInitData},   // Telnet Server
{TCPIP_MODULE_COMMAND,          0},                      // Command Interface
{TCPIP_MODULE_IPERF,            0},                      // iPerf Network Test
{TCPIP_MODULE_MAC_PIC32C,       &tcpipGMACInitData},     // GMAC Driver
{TCPIP_MODULE_MAC_BRIDGE,       &tcpipBridgeInitData},   // MAC Bridge
```

### **Services Configuration**
- **Telnet Server**: Port 23, 4 gleichzeitige Verbindungen
- **Command Interface**: Über Telnet und UART Console
- **ARP Cache**: 50 Einträge, aging timeouts
- **UDP/TCP Sockets**: Konfigurierbare Pool-Größen

---

## 🔧 **LAN8651 Register Management**

### **Register Access Framework** (`lan8651_regs.h`)

#### **Register-Adressen**
```c
#define LAN8651_OA_ID                   0x00000000U  // Device ID
#define LAN8651_OA_CONFIG0              0x00000004U  // Configuration   
#define LAN8651_OA_STATUS0              0x00000008U  // Status Register
#define LAN8651_PHY_BASIC_CONTROL       0x0000FF00U  // PHY Control
// ... vollständige Register-Map verfügbar
```

#### **Bitfeld-Zugriff Makros**
```c
// Configuration Register Bitfelder
#define LAN8651_OA_CONFIG0_SYNC_MASK        0x00008000U
#define LAN8651_OA_CONFIG0_SYNC_SHIFT       15U
#define LAN8651_OA_CONFIG0_PROTE_MASK       0x00000020U  
#define LAN8651_OA_CONFIG0_PROTE_SHIFT      5U

// Utility Makros
#define LAN8651_GET_BITFIELD(reg, mask, shift)     (((reg) & (mask)) >> (shift))
#define LAN8651_SET_BITFIELD(reg, mask, shift, val) \
    ((reg) = ((reg) & ~(mask)) | (((val) << (shift)) & (mask)))
```

#### **Register-Zugriff Beispiel**
```c
// Device ID lesen
uint32_t device_id = lan_read(LAN8651_OA_ID);
uint8_t major_ver = LAN8651_GET_BITFIELD(device_id, LAN8651_OA_ID_MAJVER_MASK, LAN8651_OA_ID_MAJVER_SHIFT);

// Synchronization aktivieren
uint32_t config0 = lan_read(LAN8651_OA_CONFIG0);
LAN8651_SET_BITFIELD(config0, LAN8651_OA_CONFIG0_SYNC_MASK, LAN8651_OA_CONFIG0_SYNC_SHIFT, 1);
lan_write(LAN8651_OA_CONFIG0, config0);

// Link Status prüfen  
uint16_t phy_status = (uint16_t)lan_read(LAN8651_PHY_BASIC_STATUS);
bool link_up = LAN8651_IS_BIT_SET(phy_status, LAN8651_PHY_BASIC_STATUS_LNKSTS_MASK);
```

---

## 🐍 **Development Tools**

### **Python Register Analysis Tools**
```
├── lan8651_complete_register_scan.py    // Vollständiger Register-Scan
├── analyze_register_addressing_pattern.py // Adress-Pattern Analyse  
├── test_lan8651.py                      // LAN8651 Funktions-Tests
├── test_device_id.py                    // Device ID Verification
├── quick_test.py                        // Schnelle Verbindungstests
└── requirements.txt                     // Python Dependencies
```

### **Register Documentation**
```  
├── README_REGISTER.md                   // Register-Analyse Dokumentation
├── README_ADDRESSES.md                  // Vollständige Adress-Map
├── README_BITFIELDS.md                  // Detaillierte Bitfeld-Spezifikation  
└── lan8651_regs.h                      // C Header für Firmware
```

### **Communication Interface**
- **Serial**: COM8 @ 115200 Baud (UART Debug Interface)
- **TC6 Protocol**: TC6-over-SPI für LAN8651 Register-Zugriff  
- **Command Interface**: `lan_read <addr>`, `lan_write <addr> <value>`

---

## 🔨 **Build Configuration**

### **MPLAB X IDE Project** (`T1S_100BaseT_Bridge.X/`)
- **Compiler**: XC32 (ARM GCC)
- **Configuration**: Default Release/Debug
- **MCC**: MPLAB Code Configurator für Pin/Peripheral Setup
- **Harmony**: Version 3 Framework mit TCP/IP Stack

### **Memory Layout** (SAME54P20A)
- **Flash**: 1MB Program Memory
- **SRAM**: 256KB Data Memory  
- **TCP/IP Heap**: Konfigurierbar (`TCPIP_STACK_DRAM_SIZE`)
- **DMA Buffers**: Ethernet TX/RX Descriptor Rings

### **Build Dependencies**
```
MPLAB X IDE v6.x
XC32 Compiler v4.x  
MPLAB Harmony 3
├── CSP (Chip Support Package)
├── DEV_PACKS (Device Family Packs)
├── TCP/IP Stack 
└── Peripheral Libraries (PLIB)
```

---

## 🐛 **Debug & Testing**

### **Debug Interfaces**
1. **UART Console** (SERCOM1 @ 115200)
   - System Boot Messages
   - Debug Output  
   - Command Line Interface

2. **Telnet Interface** (Port 23)  
   - Remote Command Access
   - Register Debugging
   - Network Statistics

3. **SWD Programming** 
   - MPLAB X Debugger Support
   - Real-time Debugging
   - Breakpoint Support

### **Command Interface Kommandos**
```bash
# LAN8651 Register-Zugriff
lan_read 0x00000000        # Device ID lesen
lan_write 0x00000004 0x1   # Konfiguration schreiben

# Netzwerk Debugging  
ping 192.168.1.1          # ICMP Ping
iperf -c 192.168.1.100    # Netzwerk Performance Test
bridge                    # Bridge Status anzeigen

# System Information
help                      # Verfügbare Kommandos
ver                       # Firmware Version
stats                     # System Statistics
```

### **Monitoring & Diagnostics**
- **LED Indicators**: Link Status, Activity, Error Status
- **Register Monitoring**: Kontinuierliche Status-Register Überwachung  
- **Performance Metrics**: Packet Count, Error Rates, Bridge Statistics
- **Error Handling**: Comprehensive Error Logging und Recovery

---

## 📚 **Referenzen & Standards**

### **Hardware Dokumentation**
- [Microchip LAN8650/1 Datasheet](https://onlinedocs.microchip.com/oxy/GUID-7A87AF7C-8456-416F-A89B-41F172C54117-en-US-10/)
- [SAME54 Family Datasheet](https://www.microchip.com/en-us/product/ATSAME54P20A)  
- [LAN8740 PHY Datasheet](https://www.microchip.com/en-us/product/LAN8740A)

### **Software Frameworks**
- [MPLAB Harmony 3 Documentation](https://onlinedocs.microchip.com/v2/keyword-lookup?keyword=MH3&redirect=true)
- [TC6 Protocol Specification](https://www.opensig.org/) - Open Alliance TC6 Standard
- [IEEE 802.3 Standard](https://standards.ieee.org/ieee/802.3/7071/) - Ethernet PHY Standards

### **Development Standards**
- **MISRA C-2012**: Code Quality Standards (mit dokumentierten Abweichungen)
- **Safety Standards**: Anwendbar für Automotive/Industrial Applications
- **Coding Style**: Microchip Harmony 3 Konventionen

---

## 🚀 **Getting Started**

### **1. Hardware Setup**
1. **Board programieren** mit MPLAB X IDE 
2. **Serial Console** mit 115200 Baud verbinden
3. **Ethernet Kabel** an beide Ports anschließen  
4. **Power-On** und Boot Messages beobachten

### **2. First Boot Verification**
```bash
# Console Output erwarten:
T1S Bridge Firmware v1.0
Initializing LAN8651...  
Initializing LAN8740...
Bridge Ready - Telnet Port 23

# Telnet Verbindung testen:
telnet 192.168.1.10 23

# Register Test:
lan_read 0x00000000
> Expected: 0x00000011 (Version 1.1)
```

### **3. Network Configuration**
1. **IP Configuration** falls erforderlich anpassen
2. **PLCA Settings** für Multi-Drop T1S konfigurieren  
3. **Bridge Table** für spezifische MAC Filtering einrichten

---

**Status**: ✅ **Production Ready Firmware**  
**Datum**: März 7, 2026  
**Version**: 1.0  
**Support**: T1S Bridge Development Team