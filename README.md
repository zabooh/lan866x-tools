# LAN866x Tools – SOME/IP-Konsolen-Tools (RCP)

Minimaler **C**-Host, der LAN866x **Control-Endpoints** über das **Remote Control Protocol (RCP)** auf Basis des reinen **C-SOME/IP-Stacks (`libsomeip`)** fernsteuert. Zugriff über den **T1S-USB-Adapter** (EVB-LAN8670-USB) als Ethernet-Bridge.

> 📦 **Eigenständiges Paket:** Dieses Verzeichnis enthält **alle** zum Bauen nötigen Quellen. Entpacken → bauen, keine externen Pfade nötig.

## Inhaltsverzeichnis

1. [Überblick](#1-überblick)
2. [Systemvoraussetzungen](#2-systemvoraussetzungen)
3. [How to compile](#3-how-to-compile)
4. [Ausführen und Ausgabe](#4-ausführen-und-ausgabe)
5. [Wie funktioniert die Discovery?](#5-wie-funktioniert-die-discovery)
6. [Projektstruktur](#6-projektstruktur)
7. [Beispiel-Pinbelegung (LAN8660)](#7-beispiel-pinbelegung-lan8660)
8. [RCP-Method-IDs](#8-rcp-method-ids)
9. [C-Vorlage für MCU32 (Track B)](#9-c-vorlage-für-mcu32-track-b)

---

## 1. Überblick

**Zweck:** Windows-Prototyp → später 1:1 als Vorlage für ein **32-bit Embedded Device** (MCU32 + lwIP + FreeRTOS). Typischer **Control-Endpoint-Use-Case**: nur **GPIO / I²C / SPI** (+ UART), z. B. mit einem LAN8660.

Das Paket besteht aus zwei Teilen:

| Track | Datei(en) | Status | Zweck |
|---|---|---|---|
| **A – Tools (C++)** | `discovery.cpp`, `i2cscan.cpp` | ✅ **baut & läuft** | Sofort nutzbare PC-Tools auf dem fertigen Stack |
| **B – C-Host-Vorlage** | `src/*.c` | Vorlage | Portierbarer C-Code → MCU32 (lwIP/FreeRTOS), siehe [Kapitel 9](#9-c-vorlage-für-mcu32-track-b) |

**Track-A-Tools** (nutzen `libepmicrochip` über den T1S-USB-Adapter):
- **`lan866x-discovery`** – listet erreichbare Endpoints + Typ + RCP-Service `0xFF10` + vollständige `GetStatus`/`GetNetworkStatus`-Infos.
- **`lan866x-i2cscan`** – scannt den I²C-Bus eines Endpoints (à la `i2cdetect`).

---

## 2. Systemvoraussetzungen

Zum **Bauen** muss auf dem Rechner vorhanden sein:

### 2.1 CMake ≥ 3.10  *(getestet: 4.1)*
- Download: <https://cmake.org/download/> → „Windows x64 Installer".
- Bei der Installation **„Add CMake to the system PATH"** wählen.
- Prüfen: `cmake --version`

### 2.2 Ein C/C++-Compiler  *(eine der beiden Varianten)*

**Variante A – MinGW-w64 (GCC)** — empfohlen für die Kommandozeile *(getestet GCC 16.1)*
- Einfachste Quelle: **WinLibs** <https://winlibs.com/> (UCRT-Variante) – ZIP entpacken, den Ordner `…\mingw64\bin` zum **PATH** hinzufügen.
- Alternativ **MSYS2** <https://www.msys2.org/>: `pacman -S mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake`
- Prüfen: `gcc --version` und `mingw32-make --version`

**Variante B – Visual Studio 2022**
- Installer: <https://visualstudio.microsoft.com/> → Workload **„Desktop development with C++"** (enthält MSVC-Compiler **und** CMake).

### 2.3 Hardware & Treiber  *(nur zum Ausführen, nicht zum Bauen)*
- **EVB-LAN8670-USB** (T1S-USB-Adapter) – erscheint unter Windows als normale **Ethernet-NIC**.
- **Windows-Treiber** installieren (`EVB-LAN8670-USB_Drv_Setup.exe`, aus dem LAN866x-Remote-Demo-Paket / MicrochipDirect **EV08L38A**).
- LAN866x-Endpoint(s) am T1S-Bus, **Bus terminiert**, ggf. PoDL-Speisung.

### 2.4 Netzwerk  *(nur zum Ausführen)*
- Dem USB-T1S-NIC eine **statische IP** im Endpoint-Subnetz geben: **`192.168.0.100/24`**. Endpoints = `192.168.0.<NodeID>`.
- **SOME/IP-SD** nutzt Multicast `224.0.0.1`. Windows-**Firewall** für `lan866x-discovery.exe` freigeben.

---

## 3. How to compile

### 3.1 Schnellweg – Batch-Skript
Im Paketverzeichnis liegt **`build.bat`** (wählt den Compiler automatisch: MinGW, sonst VS2022):

```bat
build.bat            REM bauen (Compiler automatisch)
build.bat mingw      REM MinGW-w64 (GCC) erzwingen
build.bat vs         REM Visual Studio 2022 erzwingen
build.bat clean      REM Build-Ordner loeschen
```
Ergebnis: `out\lan866x-discovery.exe` (MinGW) bzw. `out\Release\lan866x-discovery.exe` (VS).

### 3.2 Manuell – CMake auf der Kommandozeile
**Immer den Generator (`-G …`) explizit angeben** – sonst bricht CMake mit „CMAKE_C(XX)_COMPILER not set" ab.

**MinGW-w64 (GCC):**
```bat
cmake -G "MinGW Makefiles" -B out
cmake --build out
```
**Visual Studio 2022 (MSVC):**
```bat
cmake -G "Visual Studio 17 2022" -A x64 -B out
cmake --build out --config Release
```

Der Ordner `out/` ist reiner Build-Output und kann jederzeit gelöscht werden (vor dem Packen des Pakets entfernen).

---

## 4. Ausführen und Ausgabe

```bat
out\lan866x-discovery.exe
```

Das Tool gibt pro Endpoint die **vollständigen** Status-Infos aus (wie der Microchip Remote Configurator) — via `GetStatus (0x1002)` + `GetNetworkStatus (0x1600)`. Beispiel (verifiziert am Trainings-Setup):
```
Devices available = 2

========================================================
Endpoint #0  -  192.168.0.54:6800  (SOME/IP Instance 0x0001, available=1)
========================================================
  Uptime:             0h 17m 17.897s
  Application:        main/app.bin
  Chip Identifier:    LAN8661B
  Main Name:          Endpoint Demo Display And Remote
  Bootloader Name:    Endpoint Demo Display
  Main Version:       LAN8661-main-ws2812_V1.3.2-64
  Root Version:       LAN866x-root_V1.2.0-53
  Bootloader Version: LAN866x-bootloader_V1.3.1-60
  COMO Version:       V2.1.1
  Service Version:    V1.8
  Keys Version:       V0.0.1
  StartupInformation:
        ..Software Reset
        ..Security Mode: 1
  MAC:                8C:71:12:2D:02:FA
  IPv4:               192.168.0.54
  IPv6:               FDF8:6EF3:E05A:0001:0000:0000:0000:0054
  Endpoint Status:    Link-Up
  OASPI Status:       Disabled
  Arbitration:        PLCA no fallback
  PLCA Node Id:       4
```

**Findet das Tool nichts?** Prüfen: Treiber installiert · NIC-IP `192.168.0.x` gesetzt · Bus terminiert · Endpoints versorgt · Firewall freigegeben.

### I²C-Bus-Scanner
```bat
out\lan866x-i2cscan.exe                 REM erster Endpoint, SDA=PA08 SCL=PA09, 400 kHz
out\lan866x-i2cscan.exe --ip 192.168.0.54
out\lan866x-i2cscan.exe --ep 1 --sda 8 --scl 9 --speed 1
```
Probt 0x08..0x77 per 1-Byte-Read und zeigt ein `i2cdetect`-Raster. Beispiel (Proximity-3-Click am Demoboard):
```
     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f
50: -- 51 -- -- -- -- -- -- -- -- -- -- -- -- -- --

1 Gerät(e) auf dem I2C-Bus gefunden.
```
> Pins müssen zur Board-Konfiguration passen (`--sda`/`--scl`, PA-Nummer 0–15). Das Tool entsperrt SDA/SCL vor `OpenI2C` automatisch (`ReleaseDigitalPins`).

---

## 5. Wie funktioniert die Discovery?

Die Tools kennen die Endpoint-IPs **nicht vorab** – sie lernen sie zur Laufzeit über **SOME/IP Service Discovery (SD)**:

1. **Multicast beitreten:** Das Tool tritt der SD-Gruppe **`224.0.0.1`** bei (die „Joined Multicast group"-Zeilen beim Start – eine je PC-Netzwerk-Interface).
2. **FindService senden:** Es fragt per SD-**`FindService`** nach dem RCP-Service **`0xFF10`**.
3. **Endpoints antworten:** Jeder Endpoint, der `0xFF10` anbietet, schickt ein **`OfferService`** zurück – **darin steht seine eigene IP/Port** (z. B. `192.168.0.102:6800`). Endpoints senden Offer auch periodisch von selbst.
4. **Liste bauen:** `libepmicrochip` sammelt die Offers → `GetAllClients()`. **Die IP kommt also vom Endpoint, nicht vom Tool.**

```
PC  --FindService(0xFF10)-->  224.0.0.1  (Multicast)
EP1 --OfferService: ich bin 192.168.0.101 -->  PC
EP2 --OfferService: ich bin 192.168.0.102 -->  PC
```

**Ziel-Endpoint:** Default ist `[0]` (erster gefundener). Wahl per `--ip <addr>` oder `--ep <index>`. Neu angesteckte Endpoints erscheinen automatisch.

> Mehrere PC-Interfaces: Das Tool joint auf allen; geantwortet wird nur über das **T1S-Interface** (`192.168.0.x`). Deshalb muss dessen NIC eine IP im Endpoint-Subnetz haben (Kapitel 2.4).

---

## 6. Projektstruktur

```
lan866x-tools/
├── build.bat            Windows-Build-Skript (Kapitel 3)
├── CMakeLists.txt       baut Targets "lan866x-discovery" + "lan866x-i2cscan"
├── discovery.cpp        Track A: Endpoint-/Service-Discovery (läuft sofort)
├── i2cscan.cpp          Track A: I²C-Bus-Scanner
├── include/             öffentliche Header (lan866x_client.hpp, ...)
├── libepmicrochip/      SOME/IP-Stack (C) + liblan866x + Windows-Plattform-Stub
├── src/                 Track B: portable C-Vorlage für MCU32
│   ├── main.c           Demo-Loop (Init → Discovery → GPIO/I²C/SPI)
│   ├── rcp.h            Method-IDs, Pin-Map, API
│   └── rcp.c            RCP-Wrapper über libsomeip
├── README.md
└── PORTING.md           MCU32-Port (lwIP/FreeRTOS)
```
Alle zum Bauen nötigen Quellen liegen **innerhalb** dieses Verzeichnisses.

---

## 7. Beispiel-Pinbelegung (LAN8660)

*(Beispiel-Konfiguration eines Control-Endpoints mit je 1× UART / I²C / SPI + GPIOs)*

| Funktion | SERCOM | Pins |
|---|---|---|
| UART | SER0 | TX = PA00, RX = PA03 |
| GPIO Out | – | PA02, PA06 |
| I²C | SER1 | SDA = PA04, SCL = PA05 |
| SPI | SER2 | SDI = PA08, SCK = PA09, CS_N = PA10, SDO = PA11 |

---

## 8. RCP-Method-IDs

Service `0xFF10`. Aus `lan866x_client.cpp` verifiziert.

| Methode | ID | | Methode | ID |
|---|---|---|---|---|
| GetStatus | `0x1002` | | OpenSpi | `0x1500` |
| OpenGpio | `0x1300` | | WriteAndReadSpi | `0x1508` |
| SetGpio | `0x1330` | | OpenUart | `0x1400` |
| GetGpio | `0x1332` | | WriteUart | `0x1404` |
| OpenI2C | `0x1200` | | ReadUart | `0x1420` |
| WriteAndReadI2C | `0x1208`* | | WakeupNetwork | `0x1601` |

\* I²C-Read/Write-Varianten liegen im Bereich `0x1203…0x1220` – exakte ID/Payload gegen `lan866x_client.cpp` prüfen.

---

## 9. C-Vorlage für MCU32 (Track B)

`src/main.c`, `rcp.c`, `rcp.h` sind die **portable C-Vorlage** für das Embedded-Ziel (reines C auf `libsomeip`). Sie ist gegen die echte C-API + verifizierte Method-IDs/Strukturen gebaut; offen sind nur die mit `[V3]/[V4]` markierten Parameter-Layouts und die RX-Dispatch-Funktion `on_data_received()` (Vorlage: `LAN866XClientImpl::OnDataReceived`).

Track B wird **nicht** von dieser Windows-CMake gebaut, sondern im MCU32-Projekt zusammen mit einer lwIP/FreeRTOS-Implementierung der `SOMEIP_CB_*`-Callbacks (Ersatz für den Windows-Stub).

➡️ Details: **[PORTING.md](PORTING.md)**.
