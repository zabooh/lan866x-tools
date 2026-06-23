# iperf CLI — MCU Firmware (ATSAME54P20A / Microchip Harmony 3)

**Quelle:** `src/config/default/library/tcpip/src/iperf.c`  
**Konfiguration:** `src/config/default/configuration.h`  
**Firmware:** T1S_100BaseT_Bridge, TCPIP-Stack (Harmony 3)  
**Stand:** 2026-03-17  

---

## Übersicht

Der MCU-iperf ist ein Bestandteil des Microchip Harmony 3 TCP/IP-Stacks (`TCPIP_STACK_USE_IPERF`).  
Er wird über die serielle CLI des MCU (COM8, 115200 Baud, Prompt `>`) bedient.

**Verfügbare CLI-Befehle:**

| Befehl   | Funktion                          |
|----------|-----------------------------------|
| `iperf`  | Session starten (Client / Server) |
| `iperfk` | Laufende Session stoppen (Kill)   |
| `iperfi` | Netzwerkinterface konfigurieren   |
| `iperfs` | TX/RX-Buffergröße setzen          |

---

## `iperf` — Session starten

```
iperf [Optionen]
```

### Modus-Optionen

| Option | Langform   | Argument | Beschreibung                                          |
|--------|------------|----------|-------------------------------------------------------|
| `-s`   | `--server` | —        | **Server-Modus**: Wartet auf eingehende Verbindungen  |
| `-c`   | `--client` | `<ip>`   | **Client-Modus**: Verbindet zu angegebener Server-IP  |

> Genau eine der beiden Optionen muss angegeben werden.

### Protokoll-Optionen

| Option | Langform      | Argument | Beschreibung                                               | Default |
|--------|---------------|----------|------------------------------------------------------------|---------|
| `-u`   | `--udp`       | —        | UDP-Modus                                                  | TCP     |
| `-b`   | `--bandwidth` | `<rate>` | UDP-Zielrate (z.B. `1M`, `2M`, `10M`); **setzt implizit `-u`** | `10M`   |

> **Achtung:** `-b` ohne `-u` schaltet dennoch in den UDP-Modus.  
> Format: `<zahl>M` (Mbit/s) oder `<zahl>K` (kbit/s) oder reine Zahl (bps).

### Dauer / Umfang

| Option | Langform   | Argument  | Beschreibung                                      | Default |
|--------|------------|-----------|---------------------------------------------------|---------|
| `-t`   | `--time`   | `<sec>`   | Testdauer in Sekunden                             | `10`    |
| `-n`   | `--num`    | `<bytes>` | Transfergröße in Bytes (Alternative zu `-t`; setzt `-t` auf 0) | `0` |

> `-t` und `-n` schließen sich gegenseitig aus. Bei Angabe von `-n` wird `-t` auf 0 gesetzt.

### Verbindungs-/Port-Optionen

| Option | Langform    | Argument  | Beschreibung                     | Default |
|--------|-------------|-----------|----------------------------------|---------|
| `-p`   | `--port`    | `<port>`  | Server-Port                      | `5001`  |
| `-i`   | `--interval`| `<sec>`   | Report-Intervall in Sekunden     | `1`     |

### UDP-spezifische Optionen

| Option | Langform  | Argument  | Beschreibung                                          | Default     |
|--------|-----------|-----------|-------------------------------------------------------|-------------|
| `-l`   | `--len`   | `<bytes>` | UDP-Datagram-Größe (Minimum: Größe des iperf-Headers) | `1470` Byte |

### TCP-spezifische Optionen

| Option | Langform      | Argument  | Beschreibung                                                     | Default                      |
|--------|---------------|-----------|------------------------------------------------------------------|------------------------------|
| `-M`   | `--mss`       | `<bytes>` | Maximum Segment Size                                             | `TCPIP_TCP_MAX_SEG_SIZE_TX`  |
| `-x`   | `--xmitrate`  | `<bps>`   | *(nicht-standard)* Maximale TCP-TX-Rate in bps (reine Zahl, kein `M`/`K`) | `TCPIP_IPERF_TX_BW_LIMIT` |

### QoS-Option

| Option | Langform  | Argument  | Beschreibung                                          | Default |
|--------|-----------|-----------|-------------------------------------------------------|---------|
| `-S`   | `--tos`   | `<wert>`  | IP Type of Service (hex: `0xB8`, dezimal: `184`) — Mapping: VO, VI, BK, BE | `0` (BestEffort) |

### Beispiele

```
# UDP-Server starten (wartet auf eingehende UDP-Pakete)
iperf -s -u

# TCP-Server starten
iperf -s

# UDP-Client: sende 10 Sekunden mit 2 Mbit/s an 192.168.0.5
iperf -c 192.168.0.5 -u -b 2M -t 10

# TCP-Client: sende 10 Sekunden an 192.168.0.5
iperf -c 192.168.0.5 -t 10

# UDP-Client: sende mit 1 Mbit/s, Datagram 512 Byte, 30 Sekunden
iperf -c 192.168.0.5 -u -b 1M -l 512 -t 30

# UDP-Client auf nicht-Standard-Port 5010
iperf -c 192.168.0.5 -u -b 4M -p 5010
```

---

## `iperfk` — Laufende Session stoppen

```
iperfk [-i <index>]
```

| Option | Langform    | Argument | Beschreibung                           | Default |
|--------|-------------|----------|----------------------------------------|---------|
| `-i`   | `--index`   | `<n>`    | Index der zu stoppenden Session        | `0`     |

> Da `TCPIP_IPERF_MAX_INSTANCES = 1`, ist immer nur Session 0 vorhanden.

### Beispiele

```
# Session 0 stoppen (einzige verfügbare Session)
iperfk

# Session 0 explizit stoppen
iperfk -i 0
```

---

## `iperfi` — Netzwerkinterface konfigurieren

```
iperfi -a <ip> [-i <index>]
```

| Option | Argument  | Beschreibung                                                      | Pflicht |
|--------|-----------|-------------------------------------------------------------------|---------|
| `-a`   | `<ip>`    | Lokale IP-Adresse für die Session (für Server: `0` = alle Interfaces) | ja   |
| `-i`   | `<n>`     | Session-Index                                                     | nein    |

> Muss **vor** dem `iperf`-Start aufgerufen werden. Kann nicht während einer laufenden Session geändert werden.

### Beispiele

```
# Lokale Adresse auf 192.168.0.200 setzen (Session 0)
iperfi -a 192.168.0.200

# Server auf allen Interfaces listen
iperfi -a 0
```

---

## `iperfs` — TX/RX-Buffergröße setzen

```
iperfs [-tx <bytes>] [-rx <bytes>] [-i <index>]
```

| Option | Argument  | Beschreibung                              | Gültigkeitsbereich |
|--------|-----------|-------------------------------------------|--------------------|
| `-tx`  | `<bytes>` | TX-Socket-Buffergröße                     | 1 … 65535          |
| `-rx`  | `<bytes>` | RX-Socket-Buffergröße                     | 1 … 65535          |
| `-i`   | `<n>`     | Session-Index                             | —                  |

### Beispiele

```
# TX- und RX-Buffer auf je 8192 Byte setzen
iperfs -tx 8192 -rx 8192

# Nur TX-Buffer vergrößern
iperfs -tx 16384
```

---

## Konfigurationskonstanten (`configuration.h`)

| Konstante                      | Wert       | Bedeutung                                      |
|--------------------------------|------------|------------------------------------------------|
| `TCPIP_IPERF_MAX_INSTANCES`    | `1`        | Maximal eine gleichzeitige Session             |
| `TCPIP_IPERF_SERVER_PORT`      | `5001`     | Default-Port                                   |
| `TCPIP_IPERF_TX_BW_LIMIT`      | `10` Mbit/s | Default-Senderate (entspricht `-b 10M`)       |
| `TCPIP_IPERF_TX_BUFFER_SIZE`   | `4096` Byte | TX-Socket-Buffer (initial)                   |
| `TCPIP_IPERF_RX_BUFFER_SIZE`   | `4096` Byte | RX-Socket-Buffer (initial)                   |

---

## Bekannte Einschränkungen

### 1. Timer-Floor: 1 ms — Senderate ≥ ~2 Mbit/s nicht präzise steuerbar

Die Inter-Packet-Gap wird als ganze Anzahl von Systemticks berechnet:

```c
pktRate = (float)(mTxRate / 8) / (float)payloadSize;
mPktPeriod = (uint32_t)((float)tickFreq / pktRate);
```

Bei einem 1-ms-Systemtick ergibt sich:

| `-b` Zielrate | Berechnete IPG | Tatsächliche Senderate |
|---:|---:|---:|
| 1 Mbit/s  | ~11 ms | ~1,06 Mbit/s ✓ |
| 2 Mbit/s  | ~5 ms  | ~2,32 Mbit/s ✓ |
| 4 Mbit/s  | ~2 ms  | **~5,80 Mbit/s** (×1,45 Overshoot) |
| 6 Mbit/s  | **1 ms** | **~9,29 Mbit/s** (Maximum) |
| 8 Mbit/s  | **1 ms** | **~9,29 Mbit/s** (identisch zu 6M) |
| 10 Mbit/s | **1 ms** | **~9,29 Mbit/s** (identisch zu 6M) |

Ab 6 Mbit/s ist der Timer-Boden erreicht — `-b` hat **keine Wirkung** mehr.  
**Zuverlässig steuerbar: `-b 1M` und `-b 2M`.**

**Fix:** Sub-Millisekunden-Timer (Hardware TC-Peripheral auf SAME54) für `mPktPeriod`-Berechnung verwenden.

### 2. Nur eine Session gleichzeitig

`TCPIP_IPERF_MAX_INSTANCES = 1` — ein zweites `iperf` vor `iperfk` liefert:
```
iperf: All instances busy. Retry later!
```

### 3. `-b` setzt implizit UDP-Modus

Auch ohne `-u` wechselt `-b` in den UDP-Modus. TCP mit Ratenbegrenzung erfordert `-x` (nicht-standard, reine bps-Angabe).

### 4. Kleine Socket-Buffer (4 KB)

Die Default-Buffer von 4 KB können bei hohen Datenraten zum Engpass werden.  
Vor dem Test vergrößern:
```
iperfs -tx 16384 -rx 16384
```

---

## Typischer Testablauf (MCU als Server, MPU als Client)

```
# 1. MCU: UDP-Server starten
iperf -s -u

# 2. MPU (Linux): UDP-Client
iperf -c 192.168.0.200 -u -b 2M -t 10

# 3. MCU: Server stoppen
iperfk
```

## Typischer Testablauf (MCU als Client, MPU als Server)

```
# 1. MPU (Linux): UDP-Server starten
iperf -s -u -B 192.168.0.5

# 2. MCU: UDP-Client
iperf -c 192.168.0.5 -u -b 2M -t 10

# 3. MCU stoppt automatisch nach Ablauf von -t
```

---

## Referenzen

- Firmware-Quelle: `src/config/default/library/tcpip/src/iperf.c`
- Konfiguration: `src/config/default/configuration.h`
- Performancemessungen: `README_PERFORMANCE.md`
