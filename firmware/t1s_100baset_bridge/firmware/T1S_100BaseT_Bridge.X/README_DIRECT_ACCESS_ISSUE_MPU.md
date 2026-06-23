# Direct Register Access Issue — MPU-Seite

## Zusammenfassung

`lan_read` auf der MPU-Seite (Linux Userspace) **zerstört den Link zum LAN8651 vollständig und dauerhaft.**

Der Schaden entsteht sofort bei der ersten Race Condition, ist aber **latent** — der laufende TC6-Betrieb kaschiert ihn. Solange kein Reinitialisierungsversuch erfolgt, zeigt der Link kein Symptom. Erst beim nächsten `ip link set eth0 down/up` wird sichtbar, dass der interne Zustand des LAN8651 korrupt ist und der Kernel-Treiber keinen sauberen Link mehr aufbauen kann. **Einzige Lösung: Power Cycle.**

Stand: 23. März 2026

---

## Messergebnis (23. März 2026) — SILENT CORRUPTION BESTÄTIGT

Werkzeug: `mpu_direct_access_proof.py`  
Report: `mpu_direct_access_proof_20260323_163604.json`

**Testaufbau:**
- MPU = iperf-UDP-Client, Hintergrundprozess (`&`), T1S-Interface eth0, IP 192.168.0.5
- MCU = iperf-UDP-Server, IP 192.168.0.200
- Rate: 2 Mbit/s, Dauer: 10 s, Richtung: MPU → MCU
- Injektionskommando: `lan_read 0x00000001` (Register STS0, lesend, kein W1C)
- Injektionsintervall: 200 ms → 51 Injektionen in Case B
- Messung: MCU-seitig (unabhängig vom MPU-Zustand)

| Case | Beschreibung | Paketverlust | Urteil |
|------|-------------|-------------|--------|
| A BASELINE | iperf 2M/10s, keine Eingriffe | **0%** (0/1786) | OK |
| B LAN_READ_MPU | iperf + `lan_read 0x00000001` alle 200 ms | **0%** (0/1786) | **SILENT CORRUPTION** |
| RECOVERY | eth0-Bounce + 5× Ping | vor Bounce: 3/3 ✓ | **POWER CYCLE ERFORDERLICH** |
| | | nach Bounce: 0/5 ✗ | |
| C CONTROL | iperf + `cat /proc/net/dev` alle 200 ms | n/a (Link tot) | KEIN MESSWERT |

---

## Interpretation

### Warum 0% Verlust in Case B kein "OK" ist

Case B zeigt 0% Paketverlust — das sieht auf den ersten Blick unverdächtig aus, ist aber das täuschende Merkmal dieser Korruption.

**Die Korruption verläuft in drei Phasen:**

**Phase 1 — Schaden (sofort, unsichtbar):**  
`lan_read` (Userspace ioctl) greift auf den TC6/SPI-Bus zu, während der Kernel-ISR denselben Bus für den laufenden Ethernet-Betrieb nutzt. Es gibt keine Synchronisation. Die Race Condition korrumpiert den internen Zustand des TC6-Controllers im LAN8651. Der laufende Datenstrom läuft noch weiter — der Chip arbeitet auf dem alten, nun inkonsistenten Zustand weiter.

**Phase 2 — Latente Phase (kein Symptom):**  
Solange eth0 hochgefahren bleibt und kein Neustart erfolgt, ist der Link scheinbar intakt. Ping läuft, iperf läuft, kein Fehler. Der Schaden ist da, aber unsichtbar.

**Phase 3 — Kollaps (beim nächsten Reinitialisierungsversuch):**  
Sobald eth0 neu gestartet wird (`ip link set eth0 down/up`), versucht der Kernel-Treiber, den TC6-Controller neu zu initialisieren. Das schlägt fehl — der Chip ist noch im korrupten Zustand. Kein Ping, kein IP, kein Link.

Aus den Messdaten:
- **Vor dem Bounce:** Ping 3/3 (1.27–1.53 ms) — Link scheinbar intakt
- **Nach dem Bounce:** Ping 0/5 — eth0 nicht mehr erreichbar
- **Case C:** MPU-iperf kommt von `192.168.178.20` statt `192.168.0.5` — T1S-Interface tot, Traffic läuft über ein anderes Interface

### Power Cycle als einzige Lösung

`ip link set eth0 down && ip link set eth0 up` reicht nicht aus. Das Zurücksetzen des Software-Zustands im Kernel-Treiber genügt nicht — der LAN8651-Chip selbst ist in einem Hardwarezustand, den der Treiber ohne Hardware-Reset nicht reparieren kann. Nur ein Power Cycle des LAN8651 stellt den Ausgangszustand wieder her.

Dieses Verhalten deckt sich mit der empirischen Beobachtung aus dem 18. März 2026 (dokumentiert in `mpu_rx_diagnostic_test.py`):

> "lan_read corrupts TC6 SPI state machine permanently (confirmed 2026-03-18)"

---

## Root Cause

### Architektur auf der MPU

```
Userspace:  lan_read (ioctl) ─────────────────────────────────┐
                                                               ▼
Kernel:     LAN865x-Treiber → TC6-SPI-Bus ←──── IRQ 37 (spi0.0)
```

`lan_read` greift per ioctl direkt auf den TC6/SPI-Bus zu — denselben Bus, den der Kernel-ISR (Interrupt 37, `spi0.0`) für den laufenden Ethernet-Betrieb exklusiv benötigt.

Es gibt **keine Synchronisation** zwischen dem Userspace-ioctl und dem Kernel-ISR. Das ist eine echte Race Condition auf dem SPI-Bus:

- Der Kernel-ISR kann einen SPI-Transfer in beliebiger Phase unterbrechen
- Das ioctl startet eine eigene SPI-Transaktion ohne Kenntnis des Treiberzustands
- Beide greifen konkurrierend auf dieselben TC6-internen Zustandsvariablen zu
- Ergebnis: TC6-State-Machine läuft in einen inkonsistenten Zustand

### Unterschied zur MCU-Seite

| Aspekt | MCU | MPU |
|--------|-----|-----|
| Mechanismus | TC6_Service: serviceControl() vor serviceData() | Kernel-ISR vs. Userspace-ioctl Race |
| Soforteffekt | ~5% UDP-Paketverlust (messbar) | 0% Verlust (unsichtbar) |
| Langzeitwirkung | Link überlebt | Link-Tod nach Reinitialisierung |
| Recovery | Automatisch, kein Eingriff nötig | Power Cycle erforderlich |
| Nachweis | `mcu_direct_access_proof.py` | `mpu_direct_access_proof.py` |

Die MPU-Variante ist **gefährlicher**, weil:
1. Sie während des Betriebs keinen Hinweis gibt (kein Paketverlust, kein Fehler)
2. Der Schaden erst bei der nächsten Reinitialisierung sichtbar wird
3. Nur ein Power Cycle hilft — kein Software-Recovery möglich

---

## Operative Regel

> **`lan_read` und `lan_write` dürfen auf der MPU NIEMALS ausgeführt werden, während der LAN865x-Kernel-Treiber aktiv ist.**

Das gilt insbesondere für:
- Jede Situation, in der `eth0` (T1S) hochgefahren ist
- Diagnose-Scans während laufendem Netzwerkbetrieb
- Register-Snapshots in GUI-Tools während aktiver Sessions

**Erlaubt:** Registerzugriffe nur im stromlosen Zustand, im Zustand vor `ip link set eth0 up`, oder nach explizitem `ip link set eth0 down` + Entladen des Treibers.

---

## Betroffene Dateien

| Datei | Relevanz |
|-------|---------|
| `mpu_direct_access_proof.py` | Automatisierter Nachweis (dieser Test) |
| `mpu_rx_diagnostic_test.py` | Vorherige empirische Bestätigung (2026-03-18) |
| `lan8651_linux_bitfield_gui.py` | GUI-Tool, das `lan_read` verwendet — betroffen |
| `README_DIRECT_ACCESS_ISSUE.md` | Übergreifende Zusammenfassung (MCU + MPU) |
| `README_DIRECT_ACCESS_ISSUE_FIX.md` | Entscheidung: MCU-Firmware bleibt unverändert |

---

## Detailergebnis Recovery-Phase

```
Vor eth0-Bounce:  ping -c 3 -W 1 192.168.0.200
  3 packets transmitted, 3 received, 0% packet loss
  rtt min/avg/max/mdev = 1.273/1.395/1.533/0.106 ms  ← Link war scheinbar OK

eth0 bounce:      ip link set eth0 down && sleep 2 && ip link set eth0 up
  Warte 6s für PLCA-Resync ...

Nach eth0-Bounce: ping -c 5 -W 2 192.168.0.200
  5 packets transmitted, 0 received  ← kompletter Ausfall

→ POWER CYCLE ERFORDERLICH
```

Das ist das entscheidende Merkmal der MPU-seitigen Korruption: Der Link ist **bereits tot**, aber solange kein Reinitialisierungsversuch erfolgt, zeigt er kein Symptom.

---

# MPU Direct Register Access Issue – Testprotokoll (23.03.2026)

## Testaufbau
- **MCU**: COM8 (192.168.0.200)
- **MPU**: COM9
- **Testskript**: `mpu_direct_access_proof.py --verbose`
- **iperf**: 2 Mbit/s, 10s, UDP, Injektionsintervall 0.2s
- **Injektion**: `lan_read 0x00000001` auf MPU (parallel zu Kernel-ISR)
- **Messung**: Paketverlust auf MCU (iperf-Server)
- **Recovery**: eth0 bounce nach Test

## Testfälle & Ergebnisse

### CASE A: Baseline (ohne Registerzugriff)
- iperf 2M/10s, keine Eingriffe
- **Verlust**: 0% (0/1786)
- **Urteil**: OK

### CASE B: lan_read auf MPU während iperf
- iperf 2M/10s + alle 0.2s `lan_read 0x00000001` auf MPU
- **Warnung**: lan_read auf MPU kann TC6-State-Machine permanent zerstören!
- **Verlust**: 0% (0/1786)
- **Urteil**: OK (kein Effekt in diesem Lauf)
- **Beobachtung**: lan_read wurde mehrfach blockiert (EBUSY), Fallback auf debugfs sichtbar
- **Hinweis**: Injektionsrate evtl. zu niedrig, um Fehler sicher auszulösen

### RECOVERY: eth0 bounce + Ping-Test
- Nach eth0 down/up: Ping auf MCU schlägt fehl (0/5)
- **Urteil**: POWER CYCLE ERFORDERLICH (eth0-Bounce ohne Wirkung, Link tot)

### CASE C: Kontrolle (nur /proc/net/dev, kein SPI)
- iperf 2M/10s + alle 0.2s cat /proc/net/dev
- **Verlust**: n/a
- **Urteil**: KEIN MESSWERT

## Zusammenfassung
- **Direkter Registerzugriff (lan_read) auf der MPU** führt weiterhin dazu, dass der LAN8651/TC6-Treiber in einen korrupten Zustand geraten kann.
- In diesem Testlauf wurde **kein Paketverlust** gemessen, aber nach dem Test war der Link tot und nur ein Power Cycle konnte ihn wiederherstellen (eth0-Bounce wirkungslos).
- Die zuletzt implementierte Schutzmaßnahme (EBUSY bei laufendem Interface) hat **keinen Fortschritt** gebracht – der Fehlerzustand kann weiterhin auftreten.
- **Vermutung**: Die Injektionsrate war evtl. zu niedrig, um den Fehler im Messfenster sicher zu provozieren. Ein erneuter Versuch mit aggressiveren Parametern ist geplant.

## Fazit
- **Der direkte Registerzugriff auf der MPU ist weiterhin kritisch und kann den LAN8651/TC6 dauerhaft stören.**
- Ein einfacher eth0-Bounce reicht zur Wiederherstellung nicht aus – nur ein Power Cycle hilft.
- Weitere Tests mit erhöhter Injektionsrate sind notwendig, um das Fehlerbild sicher zu reproduzieren und abzusichern.

---
**Testreport:** mpu_direct_access_proof_20260323_173026.json

---

**[2026-03-23] Projektabbruch & Erkenntnis:**

Die Arbeiten am Thema "Direct Register Access" auf der MPU wurden eingestellt. Grund: Es existiert ein grundlegendes Problem im Zusammenspiel von LAN8651-Treiber, OA TC6 SPI-Treiber und dem SPI-Subsystem auf dem LAN9661 Embedded Linux. Ohne tiefgreifende Systemeingriffe ist keine stabile Lösung möglich. Der Zeitaufwand steht in keinem Verhältnis zum Nutzen. Die Entwicklung wird daher nicht weitergeführt.
