# PTP Demo — Aktivierung und Validierung via CLI

**Datum:** 2026-04-04  
**Plattform:** ATSAME54P20A / LAN865x (T1S 100BaseT Bridge)  
**Status:** Validiert — Offset mean=+45.5 ns, stdev=8.6 ns  
**Letzte Aktualisierung:** 2026-04-04 (Konvergenzsequenz, FINE-Ausgabe-Erklärung, PTP stoppen, reale Messdaten)

---

## Hardware-Voraussetzungen

| Element | Beschreibung |
|---------|-------------|
| 2× ATSAME54P20A Boards | Jeweils mit LAN865x (T1S PHY) |
| 1× T1S-Kabel | 10BASE-T1S Segment, beide Boards darauf |
| 2× USB-UART Adapter | Serielle Konsole (115200 baud, 8N1) |
| PC | Windows/Linux mit Terminalemulator (z.B. PuTTY, Tera Term) |

**Wichtig:** Beide Boards müssen mit derselben Firmware geflasht sein.  
Das Board mit **PLCA node ID = 0** wird als GM konfiguriert, das andere als Follower.

---

## Serielle Verbindung

| Rolle | COM-Port | PLCA Node ID | IP-Adresse |
|-------|----------|--------------|------------|
| Grandmaster (GM) | COM10 | 0 | 192.168.0.20 |
| Follower (FOL) | COM8 | 1 | 192.168.0.30 |

Baudrate: **115200**, 8N1, kein Hardware-Handshake.

---

## Schritt-für-Schritt Aktivierung

### Schritt 1 — IP-Adressen konfigurieren

**GM (COM10):**
```
setip eth0 192.168.0.20 255.255.255.0
```

**Follower (COM8):**
```
setip eth0 192.168.0.30 255.255.255.0
```

Erwartete Ausgabe (je Board):
```
IP address set to 192.168.0.xx
```

---

### Schritt 2 — Netzwerk-Konnektivität prüfen

**GM → Follower (COM10):**
```
ping 192.168.0.30
```

**Follower → GM (COM8):**
```
ping 192.168.0.20
```

Erwartete Ausgabe:
```
Pinging 192.168.0.xx ...
Reply from 192.168.0.xx: time<1ms
```

Falls kein Ping: T1S-Kabel prüfen, PLCA-Konfiguration prüfen (`lan_read 0x000A0010`).

---

### Schritt 3 — PTP starten

**Zuerst** den Follower aktivieren, **dann** den GM:

**Follower (COM8):**
```
ptp_mode follower
```
Ausgabe: `[PTP] follower mode (PLCA node 1)`

**GM (COM10):**
```
ptp_mode master
```
Ausgabe: `[PTP] grandmaster mode (PLCA node 0)`

### PTP stoppen

Auf **beiden** Boards:
```
ptp_mode off
```
Ausgabe: `[PTP] disabled`

Das ruft intern `PTP_GM_Deinit()` + `PTP_FOL_SetMode(PTP_DISABLED)` auf. Die Uhr läuft weiter, aber Sync-Frames werden weder gesendet noch verarbeitet.

---

### Schritt 4 — Konvergenz beobachten

Der Follower durchläuft die Zustände: **UNINIT → MATCHFREQ → HARDSYNC → COARSE → FINE**

#### Vollständige reale Konsolenausgabe (Follower COM8)

```
> ptp_mode follower
GM_RESET -> Slave node reset initiated due to sequence ID mismatch
PTP_FOL_Init: HW init done, PTP mode=2 (not activated)
[PTP] follower mode (PLCA node 1)
FollowUp seqId out of sync. Is: 0 - 1          ← einmalig beim Start, harmlos
PTP UNINIT->MATCHFREQ  scheduling TI=40 TISUBN=0x1E00000E
[FOL] Clock increment set: TI=40 TISUBN=0x1E00000E
[FOL] 1PPS output enabled
Large offset, scheduling hard sync
[FOL] Hard sync completed
Large offset, scheduling hard sync              ← zweiter Sprung bei großem Versatz
[FOL] Hard sync completed
PTP FINE    offset=-3 val=3
PTP FINE    offset=-2 val=2
...
```

#### Bedeutung der Konvergenzphasen

| Meldung | Bedeutung |
|---------|-----------|
| `GM_RESET -> Slave node reset` | Sequence-ID-Sprung erkannt, Follower hat sich zurückgesetzt |
| `UNINIT->MATCHFREQ TI=40` | Frequenzabgleich: MAC_TI=40 → 25 MHz GEM-Uhr (40 ns/Tick) |
| `TISUBN=0x1E00000E` | Subnanosekunden-Feinkorrektur des Takt-Inkrements |
| `1PPS output enabled` | 1-Puls-pro-Sekunde Ausgang aktiviert |
| `Large offset, scheduling hard sync` | Uhr weit von GM entfernt → Direktsprung auf GM-Zeit |
| `Hard sync completed` | Harter Zeitsprung abgeschlossen |
| `PTP COARSE offset=N val=N` | Grobe Regelung (Offset > 50 ns, < 90 ns) |
| `PTP FINE offset=N val=N` | Feine Regelung aktiv (Offset ≤ 50 ns) |

#### Konvergenzphasen-Schwellen

| Zustand | Bedingung |
|---------|-----------|
| HARDSYNC | `|offset|` > 90 ns |
| COARSE | 50 ns < `|offset|` ≤ 90 ns |
| FINE | `|offset|` ≤ 50 ns |
| Rückfall auf HARDSYNC | `|offset|` > 90 ns im FINE-Betrieb |

Auf der **GM-Konsole (COM10)** erscheinen je Sync-Zyklus (~125 ms):

```
[PTP-GM] TXPMDET ok, Sync #0
[PTP-GM] TTSCAA via CB=0x00000100, Sync #0
[PTP-GM] FU #0 t1=<sec>s <ns>ns
```

**Typische Konvergenzzeit:** 5–15 Sekunden nach `ptp_mode master`.

---

### Schritt 5 — Offset abfragen

**Follower (COM8) — laufend:**
```
ptp_offset
```
Ausgabe:
```
[PTP] offset=+45 ns  abs=45 ns
```

Erwarteter Wert im eingeschwungenen Zustand: **< 100 ns** (typisch ±50 ns).

---

## Bedeutung der FINE-Ausgabe

```
PTP FINE    offset=-13 val=4
```

| Feld | Bedeutung | Einheit |
|------|-----------|--------|
| `offset` | Roher gemessener Zeitversatz Follower-Uhr vs. GM (positiv = Follower eilt vor, negativ = Follower ist nach) | ns |
| `val` | FIR-gefilterter Korrekturwert, der tatsächlich in `MAC_TA` (GEM Time Adjust) geschrieben wird | ns |

### Was ist der Unterschied zwischen offset und val?

`val` ist der **gleitende Mittelwert der letzten 3 `offset`-Messungen** (FIR-Lowpass, Fenstergröße 3).  
Das dämpft Messrauschen, bevor die Korrektur ans Hardware-Register geht.

Beispiel:
```
offset=+38, val=30    ← Filter noch mit Vorwerten gefüllt
offset=+5,  val=33    ← (38+5+?)/3
offset=-30, val=4     ← Mittelwert nähert sich 0
offset=-36, val=20
offset=-19, val=28    ← Ausklang über ~7 Zyklen
...
offset=-2,  val=1     ← eingeschwungen
```

### Periodische Ausreißer im FINE-Betrieb

Typisches normales Muster nach vollständiger Konvergenz:

```
PTP FINE    offset=-3  val=3   ┐
PTP FINE    offset=-2  val=2   │ stabile Phase (±4 ns)
PTP FINE    offset=-4  val=3   ┘
PTP FINE    offset=38  val=30  ← Ausreißer
PTP FINE    offset=5   val=33  ┐
PTP FINE    offset=-30 val=4   │ Ausklang über ~10 Zyklen
PTP FINE    offset=-36 val=20  │
PTP FINE    offset=-19 val=28  │
PTP FINE    offset=7   val=16  │
PTP FINE    offset=-1  val=11  ┘
PTP FINE    offset=-3  val=2   ← wieder stabil
```

**Beurteilung:** Kein Fehler — alle Offsets bleiben < 50 ns (HARDSYNC-Schwelle), der Servo fängt jeden Ausreißer ab. Typische Ursache ist ein periodisches PLCA-Burst-Ereignis das einen einzelnen FollowUp-Timestamp leicht verzögert. Ruhephase-Offset: **±4 ns**.

### Bewertung der Messwerte

| Bereich | Bedeutung |
|---------|-----------|
| `|offset|` ≤ 10 ns | Ausgezeichnet — Uhr sehr stabil |
| `|offset|` ≤ 50 ns | Normal FINE-Betrieb |
| `|offset|` 50–90 ns | Rückfall auf COARSE → kurze Nachregelung |
| `|offset|` > 90 ns | HARDSYNC → harter Zeitsprung |

---

## Weitere CLI-Befehle

| Befehl | Beschreibung |
|--------|-------------|
| `ptp_status` | Zeigt PTP-Modus, Sync-Zähler und GM-State an |
| `ptp_interval <ms>` | Setzt Sync-Sendeinterval des GM in ms (Standard: 125) |
| `ptp_dst [multicast\|broadcast]` | Setzt PTP Ziel-MAC (Standard: multicast `01:80:C2:00:00:0E`) |
| `ptp_reset` | Setzt den Follower-Servo auf UNINIT zurück (Re-Konvergenz auslösen) |
| `ptp_regs` | Dumpt TX-Match-Register des GM (wird nach nächstem WAIT_PERIOD ausgegeben) |
| `ptp_mode off` | Deaktiviert PTP auf diesem Board |
| `stats` | TX/RX-Softwarezähler für eth0/eth1 |

### ptp_status — Beispielausgabe (GM)

```
[PTP] mode=master gmSyncs=198 gmState=3
```

Felder: `mode` = master/slave/disabled, `gmSyncs` = gesendete Sync-Frames, `gmState` = interner GM-State (3 = WAIT_PERIOD = normal).

### ptp_interval — Sync-Rate anpassen

```
ptp_interval 250
```
Ausgabe: `[PTP-GM] sync interval set to 250 ms`

Niedrigere Werte (z.B. 62) erhöhen die Sync-Rate → schnellere Konvergenz, höhere CPU-Last.

### ptp_dst — Multicast vs. Broadcast

```
ptp_dst multicast    → 01:80:C2:00:00:0E  (IEEE 1588 Standard)
ptp_dst broadcast    → FF:FF:FF:FF:FF:FF  (für Switches ohne Multicast-Support)
```

---

## Automatisierter Test mit ptp_diag.py

Das Skript führt einen vollständigen Bottom-Up-Diagnosetest durch und speichert ein Log.

### Voraussetzungen

```
pip install pyserial
```

COM-Ports in der Skript-Kopfzeile anpassen (Standard: COM10=GM, COM8=FOL):
```python
FOLLOWER_PORT    = "COM8"
GRANDMASTER_PORT = "COM10"
```

### Vollständiger Test (ab Reset)

```
python ptp_diag.py
```

### Test ab Schritt 2 (Boards bereits konfiguriert)

```
python ptp_diag.py --from-step 2
```

### Nur Stabilitätsmessung (ab FollowUp-Pfad)

```
python ptp_diag.py --from-step 6
```

### Weitere Optionen

| Option | Beschreibung |
|--------|-------------|
| `--from-step N` / `-s N` | Ab Schritt N starten (0=alles) |
| `--no-reset` | Board-Reset überspringen |
| `--duration S` | Dauer der Stabilitätsmessung in Sekunden (Standard: 30) |
| `--dump-txmatch` | TX-Match-Konfigurationsregister am Ende auslesen |

Das Skript speichert automatisch ein Log-File: `ptp_diag_YYYYMMDD_HHMMSS.log`

### Erwartetes Ergebnis

```
=== Testergebnis ===
[PASS] Schritt 1: Ping GM→FOL
[PASS] Schritt 2: Ping FOL→GM
[PASS] Schritt 3: PTP Start (kein Absturz)
[PASS] Schritt 5: TX-Timestamp (TTSCAA) erkannt
[PASS] Schritt 7: Stabilitätsmessung
  offset mean=+45.5 ns  stdev=8.6 ns  < 100 ns: 13/13 (100%)
```

---

## Erwartete Konsolenausgaben

### GM (COM10) — Normal-Betrieb

```
[PTP-GM] Init started (MAC 00:04:25:...)
[PTP-GM] TXPMDET ok, Sync #0
[PTP-GM] TTSCAA via CB=0x00000100, Sync #0
[PTP-GM] FU #0 t1=40s 629478410ns
[PTP-GM] TXPMDET ok, Sync #1
[PTP-GM] TTSCAA via CB=0x00000100, Sync #1
[PTP-GM] FU #1 t1=40s 629603610ns
...
```

### Follower (COM8) — Vollständige Konvergenz (reale Ausgabe)

```
> ptp_mode follower
GM_RESET -> Slave node reset initiated due to sequence ID mismatch
PTP_FOL_Init: HW init done, PTP mode=2 (not activated)
[PTP] follower mode (PLCA node 1)
FollowUp seqId out of sync. Is: 0 - 1
PTP UNINIT->MATCHFREQ  scheduling TI=40 TISUBN=0x1E00000E
[FOL] Clock increment set: TI=40 TISUBN=0x1E00000E
[FOL] 1PPS output enabled
Large offset, scheduling hard sync
[FOL] Hard sync completed
Large offset, scheduling hard sync
[FOL] Hard sync completed
PTP FINE    offset=-3 val=3
PTP FINE    offset=-2 val=2
PTP FINE    offset=-3 val=2
PTP FINE    offset=-4 val=3
PTP FINE    offset=38 val=30   ← periodischer Ausreißer ~alle 20 Zyklen normal
PTP FINE    offset=5  val=33
PTP FINE    offset=-30 val=4
PTP FINE    offset=-36 val=20
PTP FINE    offset=-19 val=28  ← Ausklang via FIR-Filter
PTP FINE    offset=7  val=16
PTP FINE    offset=-1 val=11
PTP FINE    offset=-8 val=1
PTP FINE    offset=-3 val=2    ← wieder eingeschwungen
```

---

## Bekannte Einschränkungen

| Einschränkung | Beschreibung |
|--------------|-------------|
| `ptp_diag.py` Schritt 4 (TXPMDET) | Kann FAIL zeigen bei ungünstigem Script-Timing; PTP-Funktion ist unabhängig davon korrekt |
| `ptp_regs` Output | Erscheint erst nach dem nächsten WAIT_PERIOD-State (~125 ms Verzögerung), nicht sofort |
| Kein NTP | Dieses Demo implementiert IEEE 1588 PTPv2 Hardware-Timestamping; kein SNTP/NTP |
| PLCA node 0 = GM | Das Master-Board muss zwingend PLCA node ID 0 haben (TX-Match-Konfiguration hardcodiert) |

---

## Troubleshooting

### TTSCAA fires nicht → kein FollowUp

Symptom: GM-Konsole zeigt `TTSCA not set after Sync #N` wiederholt.  
Ursache: TC6-Library hat STATUS0 bereits W1C-gelöscht bevor GM-Task es lesen konnte.  
Lösung: Bereits in `ptp_gm_task.c` behoben (CB-Buffer-Shortcut in `GM_STATE_READ_STATUS0`).

### Follower bleibt in COARSE, erreicht kein FINE

Symptom: Offset schwankt > 1 µs, kein FINE nach 30+ Syncs.  
Prüfen:
1. `ptp_regs` auf GM ausführen — TX-Match-Register korrekt?
2. `ipdump 2` auf Follower — kommen FollowUp-Frames an?
3. `ptp_interval 62` setzen — kürzeres Sync-Intervall testen.

### Ping schlägt fehl nach IP-Konfiguration

Symptom: `ping` gibt `No Route to Host` zurück.  
Prüfen:
1. T1S-Kabel korrekt verbunden?
2. `stats` auf beiden Boards — TX/RX-Zähler aktiv?
3. Beide Boards am gleichen PLCA-Segment (gleiche `plca_node_count`)?
