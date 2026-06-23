# Direct Register Access Issue

## Übersicht

Dieses Dokument fasst die Erkenntnisse zum direkten Registerzugriff auf den LAN865x zusammen.

Kernaussage:

- Direkter Registerzugriff auf der MPU ist bei aktivem Kernel-Treiber nicht sicher und kann die TC6-Kommunikation zerstören.
- Direkter Registerzugriff auf der MCU benutzt denselben TC6/SPI-Pfad wie der normale Datenverkehr und kann daher die Kommunikation stören, insbesondere unter Last oder bei Register-Scans.

Stand: 23. März 2026

## Messergebnis (23. März 2026) — BUG AKTIV

Der Bug wurde mit `mcu_direct_access_proof.py` automatisiert nachgewiesen.

**Testaufbau:** MPU = iperf-UDP-Client, MCU = iperf-UDP-Server, 2 Mbit/s, 10 s, Richtung MPU→MCU (bewusst gewählt — vermeidet den separaten MCU-Timer-Bug in MCU→MPU-Richtung).

| Case | Beschreibung | Paketverlust | Urteil |
|------|-------------|-------------|--------|
| A BASELINE | iperf 2M/10s, keine Eingriffe | **0%** (0/1786) | OK |
| B LAN_READ | iperf + `lan_read 0x00000000` alle 200 ms | **5%** (95/1786) | **BUG AKTIV** |
| C CONTROL | iperf + `stats` alle 200 ms (kein SPI) | **0%** (2/1786) | OK |

**Interpretation:**
- `lan_read` verursacht reproduzierbar ~5% UDP-Paketverlust gegenüber 0% Baseline.
- `stats` (selbe CLI-Last, aber kein SPI-Zugriff) verursacht keinen Verlust → der Effekt ist eindeutig auf den TC6/SPI-Zugriff zurückzuführen.
- 46 Injektionen je Case (200 ms Intervall über 10 s iperf).

**Verifikation nach Fix:** Test mit unveränderter Konfiguration erneut ausführen. Wenn Case B ebenfalls ≤ 3% zeigt, gilt der Bug als behoben.

## Kurzfazit

### MPU

Auf der MPU ist das Problem bereits empirisch bestätigt:

- `lan_read` raced mit dem laufenden LAN865x-Kernel-Treiber bzw. dessen ISR auf dem TC6/SPI-Bus.
- Dadurch wird die TC6-State-Machine korrumpiert.
- Die T1S-Kommunikation kann unmittelbar ausfallen.

### MCU

Auf der MCU ist derselbe Effekt architektonisch plausibel:

- `lan_read` und `lan_write` laufen über denselben LAN865x-Treiber und dieselbe TC6-Service-Logik wie der Datenpfad.
- Registerzugriffe sind nicht vom normalen Ethernet-Traffic isoliert.
- Häufige Reads, Scans oder kritische Writes können den Live-Verkehr stören oder Link-Zustände direkt verändern.

## Evidenz auf der MPU

Die vorhandene Diagnose dokumentiert das Problem explizit:

- In `mpu_rx_diagnostic_test.py` ist festgehalten, dass `lan_read` die TC6-SPI-State-Machine dauerhaft korrumpieren kann.
- Hardware-Register-Snapshots werden deshalb absichtlich erst ganz am Ende eines Testlaufs ausgeführt.
- Während aktiver Messungen werden stattdessen nur Kernel- und Netzwerkzähler verwendet.

Relevante Stellen:

- `mpu_rx_diagnostic_test.py`: Kommentar zu bestätigter Korruption der TC6-State-Machine
- `mpu_rx_diagnostic_test.py`: Hinweis, dass `lan_read` mit dem Kernel-LAN865x-ISR raced
- `mpu_rx_diagnostic_test.py`: Register-Snapshot läuft absichtlich zuletzt, weil er den Link zerstören kann
- `mpu_rx_diagnostic_test.py`: `0x00000008` wird ausgelassen, da W1C-Zugriff noch destruktiver ist

Zusammengefasst bedeutet das:

- Direkter User-Space-Registerzugriff auf der MPU darf nicht parallel zum aktiven Kernel-Treiber erfolgen.
- Das ist nicht nur ein Timing-Problem auf der seriellen Konsole, sondern eine echte Kollision auf dem TC6/SPI-Zugriffspfad.

## Evidenz auf der MCU

Die MCU-Firmware stellt direkte CLI-Kommandos für Registerzugriffe bereit:

- `lan_read <addr>`
- `lan_write <addr> <value>`

Diese Kommandos rufen direkt den LAN865x-Treiber auf.

### CLI-Pfad

In `src/app.c`:

- `lan_read()` ruft `DRV_LAN865X_ReadRegister(0, addr, false, ...)` auf.
- `lan_write()` ruft `DRV_LAN865X_WriteRegister(0, addr, value, true, ...)` auf.

Wichtig:

- Diese Zugriffe werden asynchron gestartet.
- Es gibt keine Trennung zwischen „Debug-Zugriff“ und „laufendem Kommunikationsbetrieb“.

### Treiber-Architektur

Der LAN865x-Treiber und die TC6-Logik zeigen, dass Registerzugriffe und Ethernet-Datenpfad denselben Kanal teilen:

- `DRV_LAN865X_ReadRegister()` und `DRV_LAN865X_WriteRegister()` reichen die Operationen an `TC6_ReadRegister()` bzw. `TC6_WriteRegister()` weiter.
- `TC6_Service()` bearbeitet zuerst `serviceControl()` und danach erst `serviceData()`.
- Registerzugriffe liegen also auf dem Control-Pfad, aber weiterhin auf derselben TC6/SPI-Infrastruktur.

Das hat zwei direkte Konsequenzen:

- Registerzugriffe konkurrieren mit dem Datenverkehr.
- Control-Operationen können den Datenpfad verdrängen oder verzögern.

### Kritische Queue-Größen

Die TC6-Konfiguration ist sehr klein dimensioniert:

- `REG_OP_ARRAY_SIZE = 2`
- `SPI_FULL_BUFFERS = 1`

Das bedeutet:

- Schon wenige parallele Registeroperationen können zu Engpässen führen.
- Ein Register-Scan mit vielen aufeinanderfolgenden `lan_read`-Aufrufen ist während aktivem Traffic riskant.

## Warum die GUI-Verbesserungen das Grundproblem nicht lösen

Die Register-Tools und GUIs wurden in Richtung robuster Prompt-Synchronisation verbessert.

Das hilft für:

- saubere Antworterkennung auf UART/Telnet
- weniger Race-Conditions auf Protokoll- bzw. Tool-Ebene

Das hilft nicht gegen:

- Konkurrenz auf dem echten TC6/SPI-Bus
- Kollisionen mit aktivem Netzwerkverkehr
- destruktive Registerzugriffe auf Reset-, Status- oder PLCA-Register

Mit anderen Worten:

- Prompt-basierte Synchronisation behebt Tool-Synchronisation.
- Sie behebt nicht die Bus-Konkurrenz im Treiberpfad.

## Besonders kritische Registerzugriffe

Nicht jeder Registerzugriff ist gleich harmlos.

Besonders riskant sind:

- Reset-Register
- PHY-Reset-Zugriffe
- W1C-Statusregister
- PLCA-Steuerregister
- Vendor-Control-Register

In den vorhandenen CLI-Beispielen existieren direkte Write-Beispiele für:

- Software Reset
- PHY Software Reset
- Konfigurationsregister

Diese Operationen können die Kommunikation nicht nur stören, sondern unmittelbar umkonfigurieren oder zurücksetzen.

## Praktische Bewertung

### MPU

Bewertung: bestätigt kritisch

- Direkter Registerzugriff während aktivem Kernel-Treiber ist unsicher.
- Die vorhandenen Diagnosen behandeln ihn bereits als destruktiv.

### MCU

Bewertung: **messtechnisch bestätigt kritisch** (23. März 2026)

- Direkter Registerzugriff ist nicht sauber isoliert.
- Unter Last, beim Bridging, bei `iperf` oder bei zyklischen Scans verursacht `lan_read` ~5% UDP-Paketverlust (gemessen, reproduzierbar).
- Kritische Writes können zusätzlich direkt den Link-Zustand verändern.
- Testskript: `mcu_direct_access_proof.py` — liefert Baseline / LAN_READ / CONTROL in ~60 s.

## Empfohlene Arbeitsweise

### Auf der MPU

- Keine `lan_read`- oder `lan_write`-Zugriffe während der Kernel-Treiber aktiv kommuniziert.
- Stattdessen Kernel-Zähler und Systemdaten verwenden:
  - `/proc/interrupts`
  - `/proc/net/dev`
  - `/sys/class/net/eth0/statistics/*`
- Falls direkter Registerzugriff nötig ist, dann nur in einem sicheren Zustand ohne parallelen Kernel-Traffic.

### Auf der MCU

- Keine Register-Scans während aktivem Bridging oder `iperf`.
- Keine zyklischen `lan_read`-Polls während Live-Traffic.
- Kritische `lan_write`-Zugriffe nur in definierten Wartungsfenstern.
- Für Laufzeitdiagnose bevorzugt Software-Zähler und Stack-Statistiken verwenden.

## Technische Root Cause

Die gemeinsame Ursache ist die fehlende Isolation des direkten Registerzugriffs gegenüber dem laufenden TC6-Datenpfad.

Auf der MPU äußert sich das als Race zwischen:

- User-Space-Registerzugriff
- Kernel-ISR / Kernel-Treiber

Auf der MCU äußert sich das als Konkurrenz zwischen:

- Control-Operationen für Registerzugriffe
- normalem Ethernet-Datenverkehr auf derselben TC6/SPI-Service-Logik

## Nächste sinnvolle Maßnahmen

1. Direkten Registerzugriff während Traffic als „unsicher“ dokumentiert behandeln.
2. Diagnosetools standardmäßig auf Software-Zähler und Kernel-/Stack-Statistiken umstellen.
3. MCU-CLI mit Safe-Mode absichern, damit `lan_read` und `lan_write` nur in ruhigen Zuständen erlaubt sind (Fix in `src/app.c`).
4. Auf der MPU den Registerzugriff nur über einen korrekt serialisierten Kernel-Pfad zulassen.
5. Fix mit `mcu_direct_access_proof.py` verifizieren — Case B muss auf ≤ 3% fallen.

## Testskript

`mcu_direct_access_proof.py` — automatisierter Nachweis und Fix-Verifikator.

```
python mcu_direct_access_proof.py                     # Standard (COM8/COM9, 2M, 10s)
python mcu_direct_access_proof.py --verbose           # mit Live-Ausgabe
python mcu_direct_access_proof.py --skip-control      # ~30s, nur A+B
python mcu_direct_access_proof.py --rate 4M           # aggressiver
```

**Erwartetes Ergebnis Bug aktiv:**
```
A_BASELINE   0%  (0/1786)    OK
B_LAN_READ   5%  (95/1786)   BUG AKTIV
C_CONTROL    0%  (2/1786)    OK
```

**Erwartetes Ergebnis nach Fix:**
```
A_BASELINE   0%   OK
B_LAN_READ   0%   GEFIXT / kein Effekt
C_CONTROL    0%   OK
```

## Zusammenfassung in einem Satz

Direkter Registerzugriff auf MPU und MCU teilt sich effektiv den Kommunikationspfad mit dem laufenden LAN865x-Verkehr und ist daher nicht als harmloser Debug-Zugriff zu betrachten, sondern als potenziell kommunikationsstörender Eingriff.