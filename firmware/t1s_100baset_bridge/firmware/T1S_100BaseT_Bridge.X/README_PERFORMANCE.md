# Performance Analysis — T1S/100BaseT Bridge (UDP + TCP)

**Datum:** 2026-03-17 (Basis) + Update 2026-03-18  
**Hardware:** ATSAME54P20A (MCU, COM8, 192.168.0.200) ↔ LAN8651 10BASE-T1S ↔ Linux/MPU (COM9, 192.168.0.5)  
**Protokolle:** UDP und TCP / iperf, Port 5001, Datagramme 1470 Byte  
**Testdauer:** 10 s pro Messung  

---

## Update 2026-03-18 - UDP Sweep nach MCU-Reflash

**Quelle:**
- `bandwidth_sweep_report_20260318_135711.txt`
- `bandwidth_sweep_report_20260318_135711.json`

### Kurzfazit

- **MPU -> MCU** bleibt stabil und verlustfrei bis 6 Mbit/s.
- **MCU -> MPU** ist weiterhin nicht ratenstabil und zeigt starke Uebersendung sowie hohen Loss.
- Ab Zielrate 3M sendet die MCU effektiv fast immer bei ~9.27 Mbit/s und der MPU-Server sieht ~85 % Verlust.

### Messergebnisse 1M bis 6M (beide Richtungen)

| Zielrate | Richtung | Tat. Client BW | Tat. Server BW | Verlust |
|---:|---|---:|---:|---|
| 1 Mbit/s | MPU -> MCU | 1.05 Mbit/s | 1.05 Mbit/s | 0/894 (0 %) |
| 1 Mbit/s | MCU -> MPU | 3.64 Mbit/s | 3.70 Mbit/s | 0/3137 (0 %) |
| 2 Mbit/s | MPU -> MCU | 2.10 Mbit/s | 2.09 Mbit/s | 0/1786 (0 %) |
| 2 Mbit/s | MCU -> MPU | 7.22 Mbit/s | 4.70 Mbit/s | 2239/6232 (36 %) |
| 3 Mbit/s | MPU -> MCU | 3.15 Mbit/s | 3.14 Mbit/s | 0/2677 (0 %) |
| 3 Mbit/s | MCU -> MPU | 9.28 Mbit/s | 1.41 Mbit/s | 6816/8012 (85 %) |
| 4 Mbit/s | MPU -> MCU | 4.20 Mbit/s | 4.19 Mbit/s | 0/3569 (0 %) |
| 4 Mbit/s | MCU -> MPU | 9.29 Mbit/s | 1.40 Mbit/s | 6825/8013 (85 %) |
| 5 Mbit/s | MPU -> MCU | 5.24 Mbit/s | 5.23 Mbit/s | 0/4460 (0 %) |
| 5 Mbit/s | MCU -> MPU | 9.26 Mbit/s | 1.39 Mbit/s | 6830/8011 (85 %) |
| 6 Mbit/s | MPU -> MCU | 6.09 Mbit/s | 6.03 Mbit/s | 0/5196 (0 %) |
| 6 Mbit/s | MCU -> MPU | 9.27 Mbit/s | 1.43 Mbit/s | 6795/8011 (85 %) |

### Interpretation (Update)

- Das vorherige 1ms-Timer-Floor-Bild bleibt in der Grobtendenz sichtbar (Plateau bei ~9.3 Mbit/s),
    aber die neue Firmware zeigt zusaetzlich bereits bei 1M und 2M massive Uebersendung.
- Die ausgegebene MCU-IPG passt nicht zur effektiven Datengroesse von 1470 Byte
    (z. B. 1M -> period 3.170 ms ergibt ~3.7 Mbit/s effektiv). Das deutet auf eine falsche
    Rate-/IPG-Berechnung (vermutlich falsche Paketgroesse in der Formel) hin.
- Auf MPU-Seite bleibt der Verlustpfad unterhalb des UDP-Socket-Layers konsistent
    (hoher Loss bei hoher Eingangslast, UDP-Anwendung nicht der primaere Engpass).

### Sofort nutzbare Betriebsgrenzen (Update)

- **MPU -> MCU (UDP):** bis 6M stabil (0 % Loss in diesem Sweep)
- **MCU -> MPU (UDP):**
    - 1M: stabil, aber falsche reale Rate (~3.7M)
    - 2M: bereits kritisch (36 % Loss)
    - >=3M: nicht verwendbar (~85 % Loss)

### Empfehlung (Update)

1. MCU-iperf Rate-Control erneut im Code pruefen: verwendete Payload-Bits in der IPG-Formel,
     Einheitenumrechnung und Tick-/Timer-Pfad.
2. Guardrail im MCU-Client einfuehren: Ist/Soll-Abweichung > 10 % als Fehler melden.
3. Bis Fix: fuer MCU->MPU UDP keine Raten >=2M fuer belastbare Messungen verwenden.
4. Verbindliche Alternativen fuer Durchsatztests: MPU->MCU UDP oder TCP in beide Richtungen.

---

## Schnellstart — Testskripte

Alle Skripte laufen im venv (`\.venv\Scripts\Activate.ps1`) aus dem Verzeichnis `T1S_100BaseT_Bridge.X\`.  
Gemeinsame Parameter (alle vier Skripte): `--mcu-port COM8  --mpu-port COM9  --baudrate 115200  --mcu-ip 192.168.0.200  --mpu-ip 192.168.0.5`

---

### 1. `dual_target_iperf_serial_test.py` — UDP bidirektionaler Einzeltest

```powershell
python dual_target_iperf_serial_test.py                    # Standardlauf (10M UDP, beide Richtungen)
python dual_target_iperf_serial_test.py --udp-bandwidth 4M # andere Zielrate
python dual_target_iperf_serial_test.py --iperf-duration 30
python dual_target_iperf_serial_test.py --ramp-test        # zusätzlicher MCU→MPU Ramp-Test
python dual_target_iperf_serial_test.py --ramp-rates 1M,2M,4M,6M
python dual_target_iperf_serial_test.py --out-dir results\
```

| Parameter | Standard | Beschreibung |
|---|---|---|
| `--udp-bandwidth` | `10M` | UDP-Zielrate für Linux-Client (`-b`); MCU Timer-Floor: > 2M unzuverlässig |
| `--iperf-duration` | `10.0` | Testdauer in Sekunden |
| `--client-timeout` | `40.0` | Max. Wartezeit für iperf-Client-Abschluss |
| `--client-idle-timeout` | `12.0` | Idle-Timeout für Linux-Output-Erfassung |
| `--ramp-test` | aus | Aktiviert optionalen MCU→MPU Ramp-Test |
| `--ramp-rates` | `1M,2M,4M,6M,8M,10M` | Raten für Ramp-Test |
| `--out-dir` | `.` | Ausgabeverzeichnis für JSON/TXT-Report |

---

### 2. `bandwidth_sweep_iperf_test.py` — UDP Rate-Sweep

```powershell
python bandwidth_sweep_iperf_test.py                             # Sweep 1M–10M, beide Richtungen
python bandwidth_sweep_iperf_test.py --rates 1M,2M,4M
python bandwidth_sweep_iperf_test.py --rates 1M,2M --iperf-duration 30
python bandwidth_sweep_iperf_test.py --out-dir results\
```

| Parameter | Standard | Beschreibung |
|---|---|---|
| `--rates` | `1M,2M,4M,6M,8M,10M` | Komma-getrennte Zielraten für den Sweep |
| `--iperf-duration` | `10.0` | Testdauer je Rate in Sekunden |
| `--client-timeout` | `40.0` | Max. Wartezeit für iperf-Client-Abschluss |
| `--client-idle-timeout` | `12.0` | Idle-Timeout für Linux-Output-Erfassung |
| `--out-dir` | `.` | Ausgabeverzeichnis für JSON/TXT-Report |

---

### 3. `tcp_dual_target_iperf_test.py` — TCP bidirektionaler Einzeltest (Baseline)

```powershell
python tcp_dual_target_iperf_test.py                       # Standardlauf, kein Fenster-Tuning
python tcp_dual_target_iperf_test.py --tcp-window 32K      # Linux-Sendefenster explizit setzen
python tcp_dual_target_iperf_test.py --parallel 2          # 2 parallele iperf-Streams
python tcp_dual_target_iperf_test.py --iperf-duration 30
python tcp_dual_target_iperf_test.py --out-dir results\
```

| Parameter | Standard | Beschreibung |
|---|---|---|
| `--iperf-duration` | `10.0` | Testdauer in Sekunden |
| `--tcp-window` | _(kein)_ | Linux-iperf `-w`-Wert, z. B. `64K` (kein Tuning = Baseline) |
| `--parallel` | `1` | Anzahl paralleler iperf-Streams (`-P`) |
| `--client-timeout` | `40.0` | Max. Wartezeit für iperf-Client-Abschluss |
| `--client-idle-timeout` | `12.0` | Idle-Timeout für Linux-Output-Erfassung |
| `--out-dir` | `.` | Ausgabeverzeichnis für JSON/TXT-Report |

---

### 4. `tcp_optimized_iperf_test.py` — TCP mit Puffer-/Fenster-Optimierung

```powershell
python tcp_optimized_iperf_test.py                              # Standardlauf (-w 16K, MCU RX=16384)
python tcp_optimized_iperf_test.py --tcp-window 32K
python tcp_optimized_iperf_test.py --mcu-rx-buffer 32768        # MCU RX-Puffer für Phase 1
python tcp_optimized_iperf_test.py --tcp-window 32K --mcu-rx-buffer 32768
python tcp_optimized_iperf_test.py --parallel 2
python tcp_optimized_iperf_test.py --out-dir results\
```

| Parameter | Standard | Beschreibung |
|---|---|---|
| `--iperf-duration` | `10.0` | Testdauer in Sekunden |
| `--tcp-window` | `16K` | Linux-iperf `-w`-Wert; Kernel verdoppelt (16K→32K effektiv) |
| `--mcu-rx-buffer` | `16384` | MCU `iperfs -rx N` für Phase 1 (MCU als Server); wirksam nur wenn `TCPIP_TCP_DYNAMIC_OPTIONS=1` |
| `--mcu-tx-buffer` | `16384` | Nur im `args`-Objekt gespeichert; Phase 2 setzt MCU TX **immer** auf 4096 B zurück (> 4096 verursacht TCP-Regression) |
| `--parallel` | `1` | Anzahl paralleler iperf-Streams (`-P`) |
| `--client-timeout` | `40.0` | Max. Wartezeit für iperf-Client-Abschluss |
| `--client-idle-timeout` | `12.0` | Idle-Timeout für Linux-Output-Erfassung |
| `--out-dir` | `.` | Ausgabeverzeichnis für JSON/TXT-Report |

> **Wichtig:** `--mcu-tx-buffer > 4096` hat keinen Effekt — das Skript setzt Phase-2-TX automatisch auf 4096 B (Firmware-Default) zurück. Wert > 4096 würde TCP-Regression (~65 Kbps) verursachen.

---

## 1. Messergebnisse — Bandwidth Sweep

Beide Richtungen wurden für jede Rate separat gemessen (`bandwidth_sweep_iperf_test.py`).

| Zielrate | Richtung | Tat. Client BW | Tat. Server BW | Datagrams | Verlust |
|---:|---|---:|---:|---:|---|
| 1 Mbit/s | MPU → MCU | 1,05 Mbit/s | 1,05 Mbit/s | 895 | **0/894 (0 %)** |
| 1 Mbit/s | MCU → MPU | 1,06 Mbit/s | 1,07 Mbit/s | 919 | **0/910 (0 %)** |
| 2 Mbit/s | MPU → MCU | 2,10 Mbit/s | 2,10 Mbit/s | 1787 | **0/1786 (0 %)** |
| 2 Mbit/s | MCU → MPU | 2,32 Mbit/s | 2,35 Mbit/s | 2010 | **0/2001 (0 %)** |
| 4 Mbit/s | MPU → MCU | 4,20 Mbit/s | 4,19 Mbit/s | 3570 | **0/3569 (0 %)** |
| 4 Mbit/s | MCU → MPU | **5,80 Mbit/s** | 5,88 Mbit/s | 5010 | **2/5001 (0,04 %)** ← Grenzbereich |
| 6 Mbit/s | MPU → MCU | 6,08 Mbit/s | 6,03 Mbit/s | 5178 | **0/5177 (0 %)** |
| 6 Mbit/s | MCU → MPU | **9,29 Mbit/s** | 1,39 Mbit/s | 8026 | **6840/8017 (85 %)** ← Kollaps |
| 8 Mbit/s | MPU → MCU | 6,11 Mbit/s | 6,05 Mbit/s | 5230 | **0/5229 (0 %)** |
| 8 Mbit/s | MCU → MPU | **9,30 Mbit/s** | 1,43 Mbit/s | 8025 | **6803/8016 (85 %)** |
| 10 Mbit/s | MPU → MCU | 6,10 Mbit/s | 6,05 Mbit/s | 5193 | **0/5192 (0 %)** |
| 10 Mbit/s | MCU → MPU | **9,27 Mbit/s** | 1,37 Mbit/s | 8026 | **6850/8017 (85 %)** |

> **Ergänzender Einzeltest** (`dual_target_iperf_serial_test.py`, `-b 10M`):  
> MPU→MCU: 6,10 Mbit/s, Loss **0 %** — MCU→MPU: 9,29 Mbit/s, Loss **85 %** — vollständig reproduzierbar.

---

## 2. Befundbeschreibung

### 2.1 MPU → MCU: verlustfrei bis zum physikalischen Plafond

Die Empfangsseite des MCU arbeitet einwandfrei. Kein einziges Paket wurde verloren.  
Bei Zielraten ≥ 8 Mbit/s liefert die MPU-Seite konstant nur **~6,1 Mbit/s** — das ist der physikalische UDP-Nutzlast-Plafond des 10BASE-T1S-Links (10 Mbit/s Brutto minus MAC/IP/UDP-Overhead plus PLCA-Koordination).

```
Effektiver 10BASE-T1S UDP-Durchsatz (MPU→MCU):  ~6,1 Mbit/s  (erwartet, kein Fehler)
```

### 2.2 MCU → MPU: Timer-Floor im MCU-iperf, Empfangskollaps ab ~5,8 Mbit/s

#### 2.2.1 MCU sendet mehr als angefordert — 1-ms-Timer-Floor

Der MCU-iperf-Client berechnet die Inter-Packet-Gap ganzzahlig in Millisekunden:

| Zielrate | Berechnete Period | Tat. Senderate |
|---:|---:|---:|
| 1 Mbit/s | 11 ms | ~1,06 Mbit/s ✓ |
| 2 Mbit/s | 5 ms | ~2,32 Mbit/s ✓ |
| 4 Mbit/s | 2 ms | **~5,80 Mbit/s** (Overshoot ×1,45) |
| 6 Mbit/s | **1 ms** | **~9,29 Mbit/s** (Maximum) |
| 8 Mbit/s | **1 ms** | **~9,30 Mbit/s** (identisch zu 6M) |
| 10 Mbit/s | **1 ms** | **~9,27 Mbit/s** (identisch zu 6M) |

Ab 4 Mbit/s Zielrate schiesst der MCU über die angeforderte Rate hinaus.  
Ab 6 Mbit/s ist der Timer-Boden bei 1 ms erreicht — die MCU sendet immer mit **~9,3 Mbit/s**, unabhängig vom `-b`-Parameter.  
**Dies ist ein Firmware-Bug:** Die Ratenkontrolle benötigt Sub-Millisekunden-Timer-Auflösung.

#### 2.2.2 MPU-Empfang kollabiert bei > ~5,8 Mbit/s — Kernel-Drops

Die Diagnose-Snapshots aus dem Einzeltest (`ip -s link`, `ifconfig -a`, `/proc/net/snmp`) zeigen den genauen Verlustpfad:

```
MCU sendet:            8025 Pakete  (eth0 TX +8026 ✓)
                          │
          ┌───────────────┴──────────────────────┐
          │ NIC-FIFO-Overflow (kein Counter)      │
          │ ~4307 Pakete spurlos                  │
          └───────────────┬──────────────────────┘
                          ▼
NIC DMA-Ring sieht:    ~3718 Pakete
                          │
          ┌───────────────┴──────────────────────┐
          │ Kernel netdev dropped = +2542         │
          │ (sk_buff-Allokierung / Backlog voll)  │
          └───────────────┬──────────────────────┘
                          ▼
UDP-Stack empfängt:    ~1176 Pakete
                          │
          RcvbufErrors = 0  ← Socket-Buffer nie voll
                          ▼
iperf registriert:     ~1167 Pakete empfangen
                       6849/8016 = 85 % Verlust
```

**Kritische Beobachtung:** `RcvbufErrors = 0` — der UDP-Socket-Buffer war zu keinem Zeitpunkt voll. Die Verluste entstehen ausschließlich im NIC/Treiber-Layer, **nicht** in der Applikationsschicht. Tuning von `rmem_max` oder `SO_RCVBUF` ist **wirkungslos**.

---

## 3. Ursachendiagramm

```
                 ┌─────────────────────────────────────────────┐
                 │         MCU → MPU Problembaum               │
                 └──────────────────┬──────────────────────────┘
                                    │
             ┌──────────────────────▼──────────────────────┐
             │  MCU iperf sendet ~9.3 Mbit/s statt Zielrate │
             │  (1-ms Timer-Floor, Firmware-Bug)            │
             └──────────────────────┬──────────────────────┘
                                    │ 9.3 Mbit/s in 10BASE-T1S
                                    ▼
             ┌──────────────────────────────────────────────┐
             │  MPU eth0 NIC kann > ~5.8 Mbit/s nicht halten│
             │  ┌──────────┐  ┌────────────────────────┐   │
             │  │ HW-FIFO  │  │ Kernel netdev dropped   │   │
             │  │ overflow │  │ +2542 Pakete            │   │
             │  │ ~4307 Pkt│  │ (kein rmem tuning hilft)│   │
             │  └──────────┘  └────────────────────────┘   │
             └──────────────────────────────────────────────┘
                                    │
                                    ▼
                            85 % Paketverlust
```

---

## 4. Grenzwerte (gemessen)

### 4.1 UDP

| Parameter | Wert | Bemerkung |
|---|---:|---|
| Max. verlustfreie Rate MPU→MCU | **~6,1 Mbit/s** | Physikalisches Limit 10BASE-T1S |
| Max. verlustfreie Rate MCU→MPU | **~4 Mbit/s** | Begrenzt durch MPU NIC-Empfangspfad |
| MCU iperf Timer-Floor | **1 ms** | Entspricht ~9,3 Mbit/s Maximum |
| MCU iperf Overshoot bei -b 4M | **5,80 Mbit/s** | ×1,45 Faktor |
| Empfangskollaps MCU→MPU ab | **~6 Mbit/s Senderate** | 85 % Verlust, konstant |
| MPU `dropped` pro Phase (~10s) | **+2542** | Kernel netdev-Layer |
| MPU `RcvbufErrors` | **0** | Socket-Buffer nie der Engpass |

### 4.2 TCP

| Parameter | Wert | Bemerkung |
|---|---:|---|
| TCP-Durchsatz MPU→MCU (Baseline) | **4,78 Mbit/s** | Transfer: 5,75 MByte, RetransSegs=0 |
| TCP-Durchsatz MCU→MPU (Baseline) | **3,90 Mbit/s** | Transfer: 4,64 MByte, RetransSegs=0 |
| TCP-Durchsatz MPU→MCU (optimiert) | **5,79 Mbit/s** | Linux `-w 16K` (effektiv 32 KByte), +21 % vs. Baseline |
| TCP-Durchsatz MCU→MPU (optimiert) | **3,90 Mbit/s** | MCU TX=4096 B (Default), keine Änderung |
| MPU `dropped` pro Phase (~10 s) | **+0** | vs. +2542 bei UDP — TCP verhindert NIC-Overflow |
| TCP RetransSegs total | **0** | Kein einziges Paket neu gesendet |
| TCP-Fenster Standard | **16 KB** | Kernel verdoppelt auf 32 KB effektiv |
| MCU TX-Puffer Grenzwert | **4096 B** | > 4096 B verursacht TCP-Regression (~65 Kbps) |

---

## 5. Empfehlungen

### 5.1 Firmware-Fix (primär — behebt Grundursache)

Der MCU-iperf benötigt eine Sub-Millisekunden-Ratenkontrolle. Die Inter-Packet-Gap muss als Mikrosekunden-Wert berechnet und mit einem Hardware-Timer (z.B. TC-Peripheral auf SAME54) realisiert werden:

```
Zielrate 4M:  IPG = 1470 * 8 / 4000000 = 2,94 ms → 2940 µs  (korrekt)
Zielrate 6M:  IPG = 1470 * 8 / 6000000 = 1,96 ms → 1960 µs  (korrekt)
Zielrate 10M: IPG = 1470 * 8 / 10000000 = 1,18 ms → 1180 µs (korrekt)
```

Bis dieser Fix implementiert ist, ist die MCU-iperf `-b`-Option über **~2 Mbit/s nicht zuverlässig**.

### 5.2 MPU Kernel-Tuning (sekundär — mildert Symptome)

Da der Verlust im NIC/Treiber-Layer entsteht, könnten folgende Maßnahmen helfen.  
> **Hinweis:** `ethtool` steht auf dem BusyBox-MPU nicht zur Verfügung — die nachfolgenden Befehle sind daher auf diesem System **nicht anwendbar**.

```bash
# NICHT verfügbar auf BusyBox-MPU:
# ethtool -G eth0 rx 512          # RX Ring Buffer vergrößern
# ethtool -C eth0 rx-usecs 0 rx-frames 1  # Interrupt-Koaleszierung abschalten
# ethtool -S eth0                  # Treiber-spezifische Zähler
```

Als Alternative können Kernel-Parameter über `/proc` und `sysctl` beobachtet werden:

```bash
# RX-Drop-Zähler beobachten (verfügbar auf BusyBox)
cat /proc/net/dev               # RX drop/fifo-Spalten
cat /proc/net/snmp              # UDP/TCP-Stack-Zähler

# Netzwerkpuffer-Einstellungen lesen (nur lesend, kein ethtool nötig)
cat /proc/sys/net/core/netdev_max_backlog
cat /proc/sys/net/core/rmem_default

# Backlog vergrößern (kann helfen bei kurzzeitigen Bursts)
echo 10000 > /proc/sys/net/core/netdev_max_backlog
```

**Kein Nutzen:** `sysctl net.core.rmem_max`, `SO_RCVBUF` — da `RcvbufErrors=0` (Verlust entsteht vor dem Socket-Buffer).

### 5.3 Betrieb innerhalb sicherer Grenzen (sofort anwendbar)

Bis der Firmware-Fix verfügbar ist:

**UDP:**
```
MPU → MCU:  bis 6 Mbit/s  → 0 % Verlust  ✓
MCU → MPU:  bis 2 Mbit/s  → 0 % Verlust  ✓  (sicherer Bereich mit Reserven)
            bis 4 Mbit/s  → 0,04 % Verlust (akzeptabel für unkritische Anwendungen)
            ab  6 Mbit/s  → ~85 % Verlust  ✗  (nicht verwendbar)
```

**TCP (empfohlen für verlässliche Übertragung):**
```
MPU → MCU:  ~4,78 Mbit/s  → 0 drops, 0 RetransSegs  ✓
MCU → MPU:  ~3,90 Mbit/s  → 0 drops, 0 RetransSegs  ✓
```
> TCP regelt die Senderate automatisch herunter — der MCU Timer-Floor-Bug hat keine Auswirkung auf Paketverluste. Das TCP-Protokoll kompensiert ihn durch Flusskontrolle.

### 5.4 TCP-Fenster-Tuning (gemessen)

Mit `tcp_optimized_iperf_test.py --tcp-window 16K --mcu-rx-buffer 16384` wurden folgende Werte reproduzierbar gemessen (2 Testläufe, 2026-03-17):

```
Ph. 1 MPU→MCU:  5,72 / 5,79 Mbit/s  (+21 % vs. Baseline 4,78 Mbit/s)  ✓
Ph. 2 MCU→MPU:  3,90 / 3,89 Mbit/s  (±0 % vs. Baseline 3,90 Mbit/s)   ✓
```

Der Gewinn in Phase 1 kommt ausschließlich vom Linux-Sendefenster (`-w 16K` → Kernel meldet **32 KByte**). MCU `iperfs -rx 16384` hat keinen Effekt, da `TCP_OPTION_RX_BUFF` in der aktuellen Firmware-Konfiguration nicht aktiv ist (`iperf: Set of RX buffer size failed`).

```powershell
python tcp_optimized_iperf_test.py
# optionale Parameter:
# --tcp-window 16K   (Standard)
# --mcu-rx-buffer 16384
```

> **Warnung:** `--mcu-tx-buffer > 4096` (Phase 2, MCU als Client) nie verwenden — verursacht TCP-Regression auf ~65 Kbps. Das Skript setzt den MCU-TX-Puffer für Phase 2 automatisch auf 4096 B (Firmware-Default) zurück.

---

## 6. Teststatus — Reproduzierbarkeit

### 6.1 UDP — MCU→MPU Verlust (vollständig reproduzierbar)

| Testlauf | Datum | Rate (Ziel) | Rate (tatsächlich) | Verlust |
|---|---|---:|---:|---:|
| dual_target_iperf (Run 1) | 2026-03-17 | 10M | 9,29 Mbit/s | 85 % |
| dual_target_iperf (Run 2) | 2026-03-17 | 10M | 9,29 Mbit/s | 85 % |
| bandwidth_sweep @6M | 2026-03-17 | 6M | 9,29 Mbit/s | 85 % |
| bandwidth_sweep @8M | 2026-03-17 | 8M | 9,30 Mbit/s | 85 % |
| bandwidth_sweep @10M | 2026-03-17 | 10M | 9,27 Mbit/s | 85 % |

### 6.2 TCP — Ergebnisse (2026-03-17)

#### 6.2.1 Baseline (`tcp_dual_target_iperf_test.py`)

| Phase | Richtung | BW | Transfer | Drops | RetransSegs |
|---|---|---:|---:|---:|---:|
| Phase 1 | MPU → MCU | **4,78 Mbit/s** | 5,75 MByte | 0 | 0 |
| Phase 2 | MCU → MPU | **3,90 Mbit/s** | 4,64 MByte | 0 | 0 |

**Diagnose-Snapshots (Baseline):**

| Counter | Phase1 vorher | Phase1 nachher | Phase2 nachher | Delta |
|---|---:|---:|---:|---:|
| MCU eth0 TX ok | 40326 | 44671 | 48017 | +7691 |
| MCU eth0 RX ok | 27167 | 31512 | 34850 | +7683 |
| MPU eth0 dropped | 10262 | 10262 | 10262 | **+0** |
| TCP InSegs | 0 | 4345 | 7685 | +7685 |
| TCP RetransSegs | 0 | 0 | 0 | **0** |

#### 6.2.2 Optimiert (`tcp_optimized_iperf_test.py`, `-w 16K`, MCU RX=16384 B)

Optimierungsparameter:
- Linux `-w 16K` → Kernel verdoppelt auf **32 KByte** effektives Sendefenster
- MCU `iperfs -rx 16384` für Phase 1 (MCU als Server) → `iperf: Set of RX buffer size failed` (TCP_OPTION_RX_BUFF nicht aktiv) → kein Effekt auf MCU-RWND; Gewinn kommt ausschließlich vom Linux-Fenster
- MCU TX/RX für Phase 2 auf Firmware-Default 4096 B zurückgesetzt (TX-Puffer > 4096 B verursacht TCP-Regression)

| Phase | Richtung | BW Run 1 | BW Run 2 | Δ vs. Baseline | Transfer | Drops | RetransSegs |
|---|---|---:|---:|---:|---:|---:|---:|
| Phase 1 | MPU → MCU | **5,72 Mbit/s** | **5,79 Mbit/s** | **+21 %** | 7,00 MByte | 0 | 0 |
| Phase 2 | MCU → MPU | **3,90 Mbit/s** | **3,89 Mbit/s** | **±0 %** | 4,64 MByte | 0 | 0 |

**Diagnose-Snapshots (Optimiert, Run 2):**

| Counter | Phase1 vorher | Phase1 nachher | Phase2 nachher | Delta Phase1 | Delta Phase2 |
|---|---:|---:|---:|---:|---:|
| MCU eth0 TX ok | 10170 | 17163 | 20505 | +6993 | +3342 |
| MCU eth0 RX ok | 8373 | 13724 | 17064 | +5351 | +3340 |
| MPU eth0 dropped | 10323 | 10323 | 10323 | **+0** | **+0** |
| TCP RetransSegs | 11 | 11 | 11 | **+0** | **+0** |
| MCU eth0 qFull | 0 | 0 | 0 | **0** | **0** |

---

## 7. Testskripte

| Skript | Protokoll | Beschreibung |
|---|---|---|
| `dual_target_iperf_serial_test.py` | UDP | Einzeltest beider Richtungen mit `ip -s link`, `stats`-Diagnose-Snapshots vor/nach jeder Phase |
| `bandwidth_sweep_iperf_test.py` | UDP | Sweep über konfigurierbare Raten (Standard: 1M–10M), Zusammenfassungstabelle am Ende |
| `tcp_dual_target_iperf_test.py` | TCP | TCP-Einzeltest beider Richtungen; kein `-b`-Parameter, optionales `--tcp-window` und `--parallel` |
| `tcp_optimized_iperf_test.py` | TCP | TCP-Test mit Puffer-/Fensteroptimierung: MCU `iperfs -rx N` für Phase 1, Linux `-w` konfigurierbar; MCU TX/RX für Phase 2 auf Default zurückgesetzt |

---

## 8. UDP vs. TCP — Direktvergleich

| Messgröße | UDP MPU→MCU | UDP MCU→MPU | TCP MPU→MCU | TCP MCU→MPU |
|---|---:|---:|---:|---:|
| Durchsatz | **6,10 Mbit/s** | 9,29 Mbit/s (nominell) | **4,78 Mbit/s** | **3,90 Mbit/s** |
| Durchsatz optimiert | — | — | **5,79 Mbit/s** (+21 %) | **3,89 Mbit/s** (±0 %) |
| Effektiv nutzbar | 6,10 Mbit/s | **≤4 Mbit/s** (sicher) | 5,79 Mbit/s | 3,90 Mbit/s |
| Paketverlust | **0 %** | **85 %** (bei 9,3 Mbit/s) | **n.a.** | **n.a.** |
| MPU eth0 dropped | 0 | **+2542 / 10 s** | **0** | **0** |
| RetransSegs / Verlust | — | — | **0** | **0** |
| Timer-Floor-Einfluss | nein | ja (Bug) | nein | nein (TCP drosselt) |

> **Fazit:** TCP ist die empfohlene Wahl für robuste Übertragungen. Mit Linux `-w 16K` (effektiv 32 KByte) erreicht MPU→MCU stabile **5,79 Mbit/s** (+21 % vs. Baseline), MCU→MPU bleibt konstant bei **3,90 Mbit/s** (physikalische Grenze). Der MCU Timer-Floor-Bug ist bei TCP ohne Einfluss. UDP bietet den höchsten Durchsatz MPU→MCU (~6,1 Mbit/s, verlustfrei), ist MCU→MPU jedoch nur bis ~4 Mbit/s (Zielrate ≤2M) sicher verwendbar.
>
> **Hinweis:** MCU `iperfs -rx N > 4096` auf Client-Sockets (Phase 2) verursacht eine katastrophale TCP-Regression (~65 Kbps). MCU-TX-Puffer immer auf 4096 B (Firmware-Default) belassen.

```powershell
# Einzeltest
python dual_target_iperf_serial_test.py

# Sweep
python bandwidth_sweep_iperf_test.py --rates 1M,2M,4M,6M,8M,10M
```

---

## 9. Mögliche Ursachen für die begrenzten Bandbreiten

### 9.1 Richtung MCU → MPU (UDP: ~85 % Verlust ab 6 Mbit/s; TCP: ~3,9 Mbit/s Decke)

| # | Hypothese | Indiz | Wahrscheinlichkeit |
|---|---|---|---:|
| A | **MCU iperf Timer-Floor** (1 ms): MCU sendet unkontrolliert ~9,3 Mbit/s statt Zielrate | `[0-1 sec] 9,29 Mbit/s` trotz `-b 4M`; Period-Berechnung in `iperf.c` ganzzahlig in ms | **Bestätigt** |
| B | **MPU NIC RX-FIFO-Overflow**: DMA-Ring des 100BaseT-Controllers fasst nur wenige Descriptor-Einträge; bei > ~5,8 Mbit/s Burst läuft FIFO über bevor DMA nachfüllen kann | `ip -s link` zeigt `dropped` im Kernel, aber `RcvbufErrors = 0` → Verlust vor Socket-Buffer | **Bestätigt** |
| C | **MPU Kernel Backlog** (`netdev_max_backlog`): Softirq-Handler kann eingehende Pakete nicht schnell genug aus dem NIC-Ringbuffer in den Stack übertragen | `dropped` steigt bei höheren Raten linear; cat `/proc/sys/net/core/netdev_max_backlog` typisch 1000 | **Möglich** |
| D | **LAN8651 PLCA-Overhead**: PLCA-Beacon + Commit-Phase reduziert nutzbaren Bruttodurchsatz; bei 10 Nodes und kurzen Burst-Gaps könnte Jitter die MPU-NIC überlasten | Konfigurationsabhängig; bei Node-Count=8 typisch ~15 % Overhead | **Möglich** |
| E | **MCU GMAC TX-DMA Starvation**: MCU sendet Pakete schneller aus dem DMA-Ring als der PLCA-MAC sie abbauen kann → intern staut sich der Ring | `qFull = 0` widerspricht dem, aber serielle Ausgabe verzögert sich bei hoher Last | **Unwahrscheinlich** |
| F | **TCP ACK-Pacing**: Harmony TCP sendet ACKs für Phase 1 (MCU als Server) langsam zurück → Linux-Sender wird künstlich gebremst | Phase 1 TCP: 5,79 Mbit/s statt 6,1 Mbit/s (UDP-Plafond) → ~0,3 Mbit/s Differenz | **Möglich** |

### 9.2 Richtung MPU → MCU (UDP: ~6,1 Mbit/s Plafond; TCP: ~4,78 Mbit/s Baseline / 5,79 Mbit/s optimiert)

| # | Hypothese | Indiz | Wahrscheinlichkeit |
|---|---|---|---:|
| G | **10BASE-T1S Brutto-Limit**: 10 Mbit/s Brutto − Ethernet-Preamble/IFG − PLCA-Overhead − IP/UDP-Header = ~6,1 Mbit/s Nutzlast-Maximum | UDP-Sweep erreicht 6,11 Mbit/s und geht nicht weiter | **Bestätigt** |
| H | **MCU RX-DMA Buffer zu klein**: `TCPIP_IPERF_RX_BUFFER_SIZE = 4096 B` → bei großem TCP-Fenster schlägt `TCP_OPTION_RX_BUFF` fehl | `iperf: Set of RX buffer size failed` im Log; `TCPIP_TCP_DYNAMIC_OPTIONS = 0` | **Bestätigt** (begrenzt TCP Phase 1) |
| I | **Harmony TCP RWND-Werbung zu klein**: MCU bewirbt ein kleines Receive Window → Linux-Sender wird gehalten | TCP Phase 1 ohne `-w`: 4,78 Mbit/s; mit Linux `-w 16K`: 5,79 Mbit/s (+21 %) → Engpass war auf Linux-Seite | **Möglich als Sekundäreffekt** |
| J | **FreeRTOS Task-Scheduling**: IP-Task und TCP-Task laufen mit fester Priorität; bei CPU-Last durch serielle Ausgabe könnten Delays entstehen | Serieller Overhead sichtbar bei schneller Ausgabe; keine direkten Counter | **Möglich** |

---

## 10. Vorschläge zur weiteren Untersuchung

### 10.1 MCU-seitig

**A — Timer-Floor-Bug verifizieren und beheben**
```c
// In iperf.c: aktuelle fehlerhafte Berechnung (Ganzzahl-Division → Floor)
uint32_t period_ms = (pktLen * 8 * 1000) / targetRate;  // verliert Sub-ms-Anteil

// Fix: Sub-Millisekunden-Periode mit TC-Peripheral berechnen
uint32_t period_us = (pktLen * 8 * 1000000UL) / targetRate;
// Dann TC-Peripheral mit µs-Auflösung konfigurieren
```

```bash
# Gegenmessung: MCU mit verschiedenen -b Werten, Server-BW auf MPU ablesen
# Erwartung nach Fix: 1M→1,0 Mbit/s, 2M→2,0 Mbit/s, 4M→4,0 Mbit/s
iperf -c 192.168.0.5 -u -b 1M -t 10
iperf -c 192.168.0.5 -u -b 2M -t 10
iperf -c 192.168.0.5 -u -b 4M -t 10
```

**B — `TCPIP_TCP_DYNAMIC_OPTIONS` aktivieren**
```c
// src/config/default/configuration.h
#define TCPIP_TCP_DYNAMIC_OPTIONS   1   // war 0 → TCP_OPTION_RX_BUFF funktioniert danach
```
Erwartetes Ergebnis: `iperfs -rx 16384` wirksam → MCU bewirbt größeres RWND → TCP Phase 1 sollte Richtung UDP-Plafond (6,1 Mbit/s) steigen.

**C — FreeRTOS-Task-Prioritäten prüfen**
```c
// In initialization.c / app.c: IP-Task-Priorität und Stack-Größe prüfen
// Typisch: tskIDLE_PRIORITY + 1 für IP-Task → erhöhen auf tskIDLE_PRIORITY + 2
```

**D — MCU-interne Statistik bei hoher Last erfassen**
```
stats           // TCPIP_STACK_NetMACStatisticsGet → qFull-Zähler beobachten
iperf -c ... -b 6M -t 30   // Längerer Lauf; stats vorher/nachher
```

### 10.2 MPU-seitig (Linux)

**E — NIC-Ringbuffer-Größe auslesen**
```bash
# FALLS ethtool verfügbar (auf Standard-Linux, nicht BusyBox):
ethtool -g eth0                  # Current/Max RX ring entries

# Auf BusyBox: via /proc/net/dev fortlaufend beobachten
watch -n 1 "cat /proc/net/dev | grep eth0"
```

**F — Interrupt-Verluste messen**
```bash
watch -n 1 "cat /proc/interrupts | grep -i eth"
# Steigende Zahl ohne korrespondierendes RX-Paket-Wachstum → NAPI-Budget-Problem
```

**G — Softirq-Budget prüfen**
```bash
cat /proc/net/softnet_stat
# Spalte 2 (hex): dropped im NET_RX_SOFTIRQ
# Spalte 3 (hex): time_squeeze (NAPI-Budget erschöpft → Paket zurückgestellt)
# Wenn time_squeeze hoch → NAPI-Budget vergrößern (net.core.netdev_budget)
echo 600 > /proc/sys/net/core/netdev_budget        # Standard: 300
echo 20000 > /proc/sys/net/core/netdev_budget_usecs # Standard: 2000
```

**H — Backlog vergrößern und erneut testen**
```bash
echo 10000 > /proc/sys/net/core/netdev_max_backlog
# Dann Testlauf wiederholen; Vergleich dropped vorher/nachher
```

**I — CPU-Auslastung während Test**
```bash
# In zweitem Terminal auf MPU (falls verfügbar):
top -d 0.5
# oder:
cat /proc/stat | awk '{print $1,$2,$3,$4,$5}' ; sleep 1 ; cat /proc/stat | awk '{print $1,$2,$3,$4,$5}'
```

### 10.3 Netzwerk-Layer-Analyse

**J — Wireshark / tcpdump auf MPU**
```bash
# Auf MPU (falls tcpdump installiert):
tcpdump -i eth0 -w /tmp/capture.pcap -s 100 &
# Testlauf...
# kill %1
# Capture auf PC kopieren und in Wireshark analysieren:
# - TCP-Fenstergröße in ACKs (zeigt MCU-RWND)
# - TCP-Retransmissions und Out-of-Order
# - Inter-Arrival-Time der UDP-Pakete (zeigt Burst-Verhalten)
```

**K — ping RTT und Jitter messen**
```bash
# Baseline-RTT ohne Last:
ping -c 100 192.168.0.200
# RTT unter Last (iperf läuft gleichzeitig):
ping -c 100 -i 0.1 192.168.0.200
# Jitter > 2 ms bei voller Last → PLCA-Koordination als Engpass
```

---

## 11. Suche nach Ursachen im Linux-Kernel-Treiber

Der MPU-Treiber für den 100BaseT-NIC auf eth0 (`9a:38:4d:ae:e6:99`) ist ein Standard-GMAC/EMAC-Treiber. Auf einem SAMA5Dx/MPU mit Linux 5.x ist das typischerweise `macb` oder `stmmac`.

### 11.1 Treiber identifizieren

```bash
# Auf MPU:
ls -la /sys/class/net/eth0/device/driver
# Zeigt Symlink auf Treiber-Namen, z.B. "macb" oder "dwmac"

dmesg | grep -i "eth0\|macb\|stmmac\|emac"
# Zeigt Treiber-Probe-Meldung und Ringbuffer-Konfiguration beim Boot

cat /sys/class/net/eth0/device/uevent
# DRIVER=macb  (oder ähnlich)
```

### 11.2 Relevante Kernel-Quelltexte finden

Sobald der Treiber identifiziert ist (Annahme: `macb`):

```bash
# Im Linux-Kernel-Quellbaum (z.B. linux/drivers/net/ethernet/cadence/):
find linux/ -name "macb*" -type f
# Typische Dateien:
#   drivers/net/ethernet/cadence/macb_main.c   ← Haupt-Treiberlogik
#   drivers/net/ethernet/cadence/macb.h        ← Ringbuffer-Defines
```

### 11.3 Schlüsselstellen im `macb`-Treiber für RX-Drop-Analyse

**Ringbuffer-Größe:**
```bash
grep -n "RX_RING_SIZE\|rx_ring_size\|RX_DESC\|num_rx_desc" \
    drivers/net/ethernet/cadence/macb_main.c macb.h
# Typischer Wert: 128 oder 256 Descriptors
# Bei 9,3 Mbit/s × 10s / 1470B = ~7900 Pakete → 128 Entries bei Burst-Gap leer
```

**NAPI-Poll-Gewicht (`weight`):**
```bash
grep -n "napi_enable\|netif_napi_add\|napi_weight\|NAPI_POLL_WEIGHT\|budget" \
    drivers/net/ethernet/cadence/macb_main.c
# Typisch: weight=64 → pro Interrupt-Koaleszierungsperiode max. 64 Pakete
# Bei 9,3 Mbit/s / 1470B = 790 Pakete/s → bei weight=64 und 10ms Koaleszierung:
# 7,9 Pakete/10ms < 64 → Budget kein Problem; FIFO-Overflow wahrscheinlicher
```

**RX-Drop-Counter im Treiber:**
```bash
grep -n "dropped\|rx_dropped\|stats.rx_dropped\|ndev->stats\|ring_full\|rx_overflow" \
    drivers/net/ethernet/cadence/macb_main.c
# Zeigt: wo der Treiber stats.rx_dropped inkrementiert
# Typisch in macb_rx_frame() oder macb_poll()
```

**Interrupt-Koaleszierung:**
```bash
grep -n "irq_coalesce\|rx_coalesce\|GEM_RXWATERMARK\|MACB_NCFGR\|coal" \
    drivers/net/ethernet/cadence/macb_main.c
# GEM (Gigabit Ethernet MAC) hat ggf. Watermark-Register für RX-Interrupt-Verzögerung
```

**DMA-Descriptor-Flags (Overflow-Bit):**
```bash
grep -n "MACB_RX_FRMLEN_MASK\|MACB_RX_RXOVR\|MACB_RSR_OVR\|RXOVR\|OVR" \
    drivers/net/ethernet/cadence/macb.h macb_main.c
# MACB_RSR_OVR = RX overrun bit; wenn gesetzt → FIFO-Overflow bestätigt
```

### 11.4 Konkrete Git-Suche im Kernel-Repository

```bash
# Kernel-Quellen klonen (falls nicht vorhanden):
git clone --depth=1 --branch v5.15 \
    https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git linux-5.15

cd linux-5.15

# 1. RX-Drop-Pfad nachverfolgen
git log --oneline --no-walk --all -- drivers/net/ethernet/cadence/macb_main.c | head -20
grep -n "rx_dropped\|rx_missed\|rx_fifo_errors" \
    drivers/net/ethernet/cadence/macb_main.c

# 2. Ringbuffer-Konfiguration (anpassbar zur Laufzeit via ethtool -G?)
grep -n "set_ringparam\|get_ringparam\|ethtool_ops" \
    drivers/net/ethernet/cadence/macb_main.c
# Wenn set_ringparam implementiert: ethtool -G eth0 rx 512 würde funktionieren

# 3. PLCA-Support im Treiber
grep -rn "plca\|PLCA\|T1S" \
    drivers/net/ethernet/cadence/
# Neuere Kernel (≥ 6.1) haben PLCA-Support im phylink-Layer

# 4. Vergleich Treiber-Version auf MPU vs. aktueller Kernel
cat /proc/version           # Kernel-Version auf MPU
# Dann gezielt den entsprechenden Tag untersuchen
```

### 11.5 Interpretation: Was der Drop-Counter aussagt

```
ip -s link → eth0 RX dropped = 10323  (kumulativ seit Boot)

Mögliche Quellen im macb-Treiber:
  ┌─────────────────────────────────────────────────────────┐
  │ macb_rx_frame()                                         │
  │   if (skb == NULL) → stats.rx_dropped++  ← sk_buff     │
  │                                             Allokations-│
  │                                             fehler       │
  ├─────────────────────────────────────────────────────────┤
  │ macb_poll() / NAPI                                      │
  │   if (budget exhausted) → napi_schedule() wieder        │
  │   → time_squeeze in /proc/net/softnet_stat              │
  ├─────────────────────────────────────────────────────────┤
  │ GEM Hardware RSR-Register                               │
  │   MACB_RSR_OVR (Bit 2) → FIFO Overrun                  │
  │   → macb_tx_error_task() / macb_rx()  checks this bit  │
  │   → wenn gesetzt: direkte Hardware-FIFO-Overflow        │
  └─────────────────────────────────────────────────────────┘

Diagnose auf MPU (ohne ethtool):
  cat /proc/net/softnet_stat
  # Spalte 3 (hex) = time_squeeze → wenn > 0: NAPI-Budget-Engpass
  # Spalte 2 (hex) = total dropped in NET_RX softirq
```

---

## 12. TC6-Architektur: Control- vs. Data-Transaktionen als strukturelles RX-Limit

**Datum:** 2026-03-26 (Analyse aus `oa_tc6.c` Treiberquellcode)

### 12.1 Hintergrund: TC6 kann Control und Data nicht gleichzeitig übertragen

Jede SPI-Transaktion zum LAN8651 enthält in Bit 31 des 4-Byte-Headers das Flag `DATA_NOT_CTRL`:

```
DATA_NOT_CTRL = 1  →  DATA-Transaktion   (Ethernet-Chunks, max. 48 × 68 Byte)
DATA_NOT_CTRL = 0  →  CTRL-Transaktion   (Register-Lesen/Schreiben, 12 Byte)
```

Ein SPI-CS-Zyklus kann immer nur **eines davon** transportieren — niemals beides gleichzeitig.

Der `oa_tc6`-Treiber implenentiert dies mit zwei völlig getrennten Pfaden:

```
CTRL-Pfad:  spi_ctrl_tx/rx_buf  → oa_tc6_read/write_register()
                                   geschützt durch spi_ctrl_lock (Mutex)
DATA-Pfad:  spi_data_tx/rx_buf  → oa_tc6_try_spi_transfer()
                                   läuft im dedizierten kthread (SCHED_FIFO)
```

### 12.2 Was unter hoher RX-Last passiert

Der kthread-Loop `oa_tc6_try_spi_transfer()` arbeitet in dieser Schleife:

```
while (true):
  1. Baue leere TX-Chunks auf  (um RX-Daten aus dem FIFO zu holen)
  2. → DATA-SPI-Transfer        (bis zu 48 Chunks = 3072 Byte Nutzdaten)
  3. Lies rx_chunks_available aus dem Footer des letzten empfangenen Chunks
  4. Falls EXTENDED_STS-Bit im Footer gesetzt:
       → CTRL-SPI-Transfer: STATUS0 lesen     (12 Byte, ~10 µs auf 5 MHz SPI)
       → CTRL-SPI-Transfer: STATUS0 löschen   (12 Byte, ~10 µs)
       Falls Status = RX_BUFFER_OVERFLOW → rx_buf_overflow = true
  5. Falls noch rx_chunks_available > 0 → goto 1
  6. Sonst: break
```

**Das Problem:** Sobald der LAN8651 RX-FIFO überläuft, setzt der Chip `EXTENDED_STS = 1` in **jedem** Footer. Das zwingt den Treiber nach **jedem DATA-Transfer** zu zwei zusätzlichen CTRL-SPI-Transfers (je 12 Byte). Diese blockieren den SPI-Bus — während das FIFO weiterläuft.

### 12.3 Zahlenmäßige Einschätzung des strukturellen Limits

| Parameter | Wert | Quelle |
|---|---|---|
| Max. Chunks pro DATA-Transfer | **48** | `OA_TC6_MAX_TX_CHUNKS` |
| Chunk-Größe | 68 Byte (64 Payload + 4 Header) | `OA_TC6_CHUNK_SIZE` |
| Max. Payload pro SPI-Transfer | 48 × 64 = **3072 Byte** | — |
| Frames pro Transfer bei 1470 Byte | **~2 Frames** (≈23 Chunks/Frame) | — |
| Max. meldbare RX-Chunks im Footer | **31** (5-Bit-Feld) | `OA_TC6_DATA_FOOTER_RX_CHUNKS` |
| CTRL-Overhead pro Overflow-Zyklus | **2 × 12 Byte = 24 Byte** statt 0 | `oa_tc6_process_extended_status()` |
| RX-Drainzeit für 31 Chunks @ 5 MHz SPI | **~3,4 ms** | gerechnet |
| Zeit bis FIFO wieder voll bei 9,3 Mbit/s | **~1,7 ms** | gerechnet (31 × 64 = 1984 Byte) |

**Fazit:** Das FIFO füllt sich doppelt so schnell wie der Treiber es leeren kann. Sobald der Overflow-Zustand einmal eintritt, ist er selbstverstärkend.

### 12.4 Zweites strukturelles Problem: MDIO-Polling auf demselben SPI-Bus

Der phylib-Layer pollt regelmäßig den Link-Status über:

```c
oa_tc6_mdiobus_read()
  → oa_tc6_read_register()
    → mutex_lock(&tc6->spi_ctrl_lock)
    → spi_sync()                    ← serialisiert mit dem DATA-kthread
    → mutex_unlock()
```

Alle `spi_sync()`-Aufrufe zum selben SPI-Device werden im Linux-SPI-Subsystem serialisiert. Jeder MDIO-Register-Read (Link-Status, AN-Zustand) unterbricht den DATA-kthread mitten im RX-Drain-Loop und erzeugt eine messbare Lücke im SPI-Datenstrom.

### 12.5 Verortung im Gesamtbild (Verlustpfad Wiederholung)

```
MCU sendet 9,3 Mbit/s
    │
    ▼ LAN8651 RX-FIFO (physisch begrenzt, ~31 Chunks meldbar)
    │   FIFO füllt sich in ~1,7 ms
    │   oa_tc6-Treiber draint in ~3,4 ms
    │   → FIFO-Overflow tritt strukturell auf
    │
    ▼ EXTENDED_STS = 1 in jedem Footer
    │   → 2 CTRL-Transfers nach jedem DATA-Transfer
    │   → Drain-Lücke vergrößert sich
    │
    ▼ Kernel-Drop-Zähler (netdev dropped: +2542 / 10s)
    │
    ▼ iperf-Server: ~1167 von 8020 Paketen → 85% Loss
```

### 12.6 Offene Fragen (für Treiberentwickler / Parthiban)

1. Ist `OA_TC6_MAX_TX_CHUNKS = 48` ein TC6-Spec-Constraint oder ein Tuning-Parameter, der erhöht werden kann?
2. Ließe sich der `EXTENDED_STS`-CTRL-Round-Trip unter Overflow vermeiden — z.B. `RXBO` als transienten Zustand behandeln ohne STATUS0-Read-Modify-Write?
3. Erzeugt das phylib-MDIO-Polling messbare SPI-Bus-Contention bei hoher Eingangsrate?
4. Wäre ein größerer SPI-DMA-Burst (mehr als 48 Chunks pro Transfer) mit dem TC6-Protokoll konform?

**Status:** Offen — noch nicht an Parthiban kommuniziert (Stand 2026-03-26)
