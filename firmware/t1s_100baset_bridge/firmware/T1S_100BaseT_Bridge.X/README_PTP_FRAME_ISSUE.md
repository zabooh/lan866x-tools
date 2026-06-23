    # PTP Frame Issue Investigation

Datum: 2026-04-01  
Zuletzt aktualisiert: 2026-04-01 (Bug gefunden und gefixt — TCPIP_PKT_PacketAcknowledge fehlt im 0x88F7-Handler ✓ RESOLVED)

## Ziel dieser Analyse

Dieses Dokument beschreibt den aktuell untersuchten Fehler rund um den PTP-Frame-Versand im Projekt T1S_100BaseT_Bridge, die bisher durchgeführten Tests, die belastbaren Beobachtungen und die daraus abgeleiteten nächsten Schritte.

## Kurzbeschreibung des Problems

Der Grandmaster (GM) läuft im PTP-Master-Modus und erhöht intern den Sync-Zähler (gmSyncs), aber auf der Follower-Seite werden keine PTP-Frames mit EtherType 0x88F7 gesehen.

Zusätzlich tritt nach PTP-Phasen ein asymmetrisches Verhalten beim NoIP-Smoke-Test auf:
- Richtung GM -> Follower kann ausfallen.
- Richtung Follower -> GM funktioniert weiterhin.

## Ausgangslage und Kontext

- Plattform: ATSAME54P20A mit LAN865x.
- Projekt: Harmony-basierter Bridge-Firmware-Stack.
- Testablauf: build_dual.bat, danach flash_dual.py, danach ptp_frame_test.py.
- Serielle Diagnostik und ipdump-Zähler werden für beide Boards ausgewertet.

## Bereits implementierte Diagnostikmaßnahmen

1. Bedingte Kompilierung der LAN865x-Zugriffe in der GM-Task
- DRV_LAN865X_WriteRegister
- DRV_LAN865X_ReadRegister
- DRV_LAN865X_SendRawEthFrame
- DRV_LAN865X_GetAndClearTsCapture

2. Zustandswechsel-Logging in der GM-State-Machine
- Bei jedem Wechsel wird der neue Zustand inklusive Quellzeile ausgegeben.
- Format: [PTP-GM][STATE] <STATE> @L<line>.

3. Erweiterte Testausgabe in ptp_frame_test.py
- Parallel-Capture von GM- und Follower-Serialausgaben.
- Auswertung von PTP-Hits und Anzahl GM-State-Debugmeldungen.

## Chronologie der wichtigsten Befunde

1. Vor der PTP-Frame-Hypothese
- GM-State-Machine läuft zyklisch.
- gmSyncs steigt.
- Follower sieht trotzdem keine 0x88F7-Pakete im ipdump.

2. Kontrollversuch mit noip_send-ähnlichem Frame aus der GM-PTP-Task
- Ersetzt im SEND_SYNC-Pfad den PTP-Frame durch einen bekannten funktionierenden NoIP-Testframe.
- Beobachtung: Empfang auf der Gegenseite ist grundsätzlich möglich.
- Schluss: Der reine Sendepfad ist wahrscheinlich nicht das alleinige Problem.

3. Fokus auf PTP-Frame-Länge (Ethernet Minimum)
- Originale Sync-Frame-Länge war 58 Byte.
- Anpassung auf 60 Byte (Ethernet-Minimum ohne FCS) durchgeführt.
- Zusätzlich Compile-Time-Prüfung auf erwartete syncMsg_t-Größe eingeführt.

4. Verifikation nach der 60-Byte-Anpassung (Build -> Flash -> Test)
- Build erfolgreich.
- Flash erfolgreich auf beiden Boards.
- Testresultat:
- PTP-Hits bleiben 0 in beiden A/B-Varianten (Broadcast-DST und Multicast-DST).
- GM-State-Debugmeldungen laufen stabil.
- gmSyncs steigt weiter.

5. End-Postcheck NoIP (nach PTP-Phase)
- Richtung GM -> Follower: FAIL (0 Treffer).
- Richtung Follower -> GM: PASS (Treffer vorhanden).
- Damit bleibt die bekannte Asymmetrie bestehen.

## Technische Interpretation

Die 60-Byte-Korrektur war ein sinnvoller und notwendiger Hypothesentest, hat den Fehler aber nicht beseitigt.

Belastbare Zwischenbewertung:
- Der GM-Sendepfad wird ausgeführt.
- Das Problem liegt nicht ausschließlich an der zu kurzen Frame-Länge.
- Die Persistenz der GM->Follower-Asymmetrie nach PTP deutet auf einen GM-seitigen Zustand hin, der durch PTP-Konfiguration oder PTP-Lauf beeinflusst wird und nicht vollständig zurückgesetzt wird.

## Betroffene Dateien in der aktuellen Untersuchung

- [src/ptp_gm_task.h](../src/ptp_gm_task.h)
- [src/ptp_gm_task.c](../src/ptp_gm_task.c)
- [ptp_frame_test.py](ptp_frame_test.py)
- [src/app.c](../src/app.c)

## Derzeitiger Status

- Diagnoseinstrumentierung ist vorhanden und reproduzierbar.
- Build- und Flash-Pipeline ist stabil.
- Das PTP-Issue ist reproduzierbar.
- Die 60-Byte-Hypothese ist getestet und als alleinige Ursache verworfen.

## Update: Ergebnis Schritt 1 (EtherType-isolierter A/B-Test)

- Umsetzung: PTP-Payload blieb aktiv, EtherType wurde testweise von 0x88F7 auf 0x88B5 umgestellt.
- Ergebnis im A/B-Test (Broadcast vs Multicast):
    - In beiden Varianten wurden viele 0x88B5-Treffer gesehen (hits=96).
    - Kein Unterschied zwischen Broadcast und Multicast in diesem Testlauf.
- Wichtiger Befund:
    - Damit ist der Sendepfad mit derselben Payload bei 0x88B5 sichtbar.
    - Das stützt die Hypothese, dass 0x88F7-spezifisches Verhalten (Filterung/Behandlung) ein zentraler Faktor sein kann.
- End-Postcheck nach diesem Lauf:
    - NoIP bidirektional PASS.
    - Die zuvor beobachtete Asymmetrie trat in diesem Schritt-1-Lauf nicht auf.

## Update: Ergebnis Schritt 2 (inkrementeller 0x88F7-PTP-Aufbau)

- Umsetzung: 0x88F7 wurde beibehalten, Sync-Header in drei Profilstufen getestet:
    - Inkrement 1: Minimalprofil (Basisfelder)
    - Inkrement 2: Minimal + Timing/Control-Felder
    - Inkrement 3: Legacy-Profil (vorheriger Header-Stil)
- Testablauf pro Inkrement: Build -> Flash -> ptp_frame_test (expect-ethertype 88F7)
- Ergebnis über alle drei Inkremente:
    - A/B Broadcast vs Multicast jeweils hits=0
    - gmSyncs steigt weiter, State-Logs laufen
    - End-Postcheck weiterhin asymmetrisch (GM->Follower FAIL, Follower->GM PASS)
- Schlussfolgerung:
    - Das Problem wird durch Header-Minimalisierung nicht aufgelöst.
    - Ein 0x88F7-spezifischer Pfad (Filterung/Forwarding/Stack-Behandlung) bleibt Hauptverdacht.

## Update: Ergebnis Schritt 3 (GM-Deinit Cleanup — MAC_TI, PPSCTL, TXMLOC)

Datum: 2026-04-01

### Umsetzung

`PTP_GM_Deinit()` in `ptp_gm_task.c` wurde erweitert:
- `GM_TXMLOC` auf 0 zurückgesetzt (war auf 30 gesetzt beim Init).
- `MAC_TI` (0x00010077) auf 0 gesetzt — TSU-Timer-Inkrement gestoppt.
- `PPSCTL` (0x000A0239) auf 0 gesetzt — PPS-Pulsausgang deaktiviert.

Neue Deinit-Meldung: `[PTP-GM] deinit: TX-Match/TSU/PPS disarmed, state reset`

### Testablauf

Build -> Flash (beide Boards bestätigt: `SUCCESS: Device programmed and running`) -> ptp_frame_test.py (mit Board-Reset, vollständiger Durchlauf).

### Ergebnis

| Schritt | Ergebnis |
|---|---|
| Ping GM->FOL | PASS (4/4) |
| Ping FOL->GM | PASS (4/4) |
| NoIP-Vortest GM->FOL (vor PTP) | PASS (sichtbar) |
| PTP-Hits 0x88F7 Broadcast | 0x |
| PTP-Hits 0x88F7 Multicast | 0x |
| gmSyncs | 171 (stabil) |
| NoIP Postcheck GM->FOL (nach PTP) | FAIL (0 Treffer) |
| NoIP Postcheck FOL->GM (nach PTP) | PASS (5 Treffer) |

### Schlussfolgerung

Der Cleanup von MAC_TI, PPSCTL und TXMLOC hat die GM->Follower-Asymmetrie **nicht behoben**.

Entscheidende Beobachtung: Der NoIP-Vortest (vor der PTP-Phase) ist PASS. Die Asymmetrie entsteht **während** der PTP-Phase und bleibt danach persistent — ein Board-Reset würde sie zurücksetzen, `ptp_mode off` allein nicht.

Die Ursache liegt damit in einem **persistenten LAN865x-Zustand**, der durch den PTP-Betrieb gesetzt wird und nicht durch die bisher bekannten Deinit-Register-Writes zurückgesetzt werden kann. Kandidaten:

1. **PLCA-Zustand / TC6-Layer** — ein Puffer- oder Zustandsbit im LAN865x, das nach `DRV_LAN865X_SendRawEthFrame(tsc=1)` aktiv bleibt oder den TX-Pfad für nachfolgende Frames verändert.
2. **OA_CONFIG0 / Cut-Through-Modus** — beim Init bewusst nicht gesetzt (TC6Error_SyncLost-Risiko), könnte durch PTP-Initialisierung oder den TC6-Treiber implizit geändert worden sein.
3. **TC6-SPI-Buffer** oder **FIFO-Zustand** — ein hängender Timestamp-Request (tsc=1) blockiert nachfolgende TX.

## Update: Baseline-Verifikation (2026-04-01 — nach Schritt-3-Stand)

Datum: 2026-04-01

### Ziel

Vollständiger Re-Run (Build → Flash → Test) ohne Codeänderung, um den aktuellen Firmware-Stand zu verifizieren und eine belastbare Ausgangsbasis für die nächsten Schritte herzustellen.

### Testablauf

`build_dual.bat` → `flash_dual.py` → `ptp_frame_test.py` (alle drei Schritte im Terminal ausgeführt).

### Ergebnis

| Messung | Ergebnis |
|---|---|
| Build GM + Follower | SUCCESS |
| Flash GM (SN: ...1290) | SUCCESS: Device programmed and running |
| Flash Follower (SN: ...1049) | SUCCESS: Device programmed and running |
| PTP-Hits 0x88F7 Broadcast (A) | 0 |
| PTP-Hits 0x88F7 Multicast (B) | 0 |
| gmSyncs | 171 (stabil) |
| GM-State-Debugmeldungen | 215 (A) / 222 (B) |
| NoIP Postcheck GM→FOL (nach PTP) | **FAIL** (0 Treffer) |
| NoIP Postcheck FOL→GM (nach PTP) | PASS (5 Treffer) |

### Schlussfolgerung

Verhalten ist vollständig reproduzierbar und identisch mit Schritt 3. Die GM-State-Machine läuft stabil (L374/L402-Zyklen), aber kein einziger 0x88F7-Frame kommt beim Follower an. Die GM→FOL-Asymmetrie bleibt nach der PTP-Phase bestehen. Dies ist die verifizierte Ausgangsbasis für den nächsten Isolationstest.

---

## Update: Schritt 4 — Isolationstest GM-SW-Reset (2026-04-01)

### Ziel

Prüfen ob ein SW-Reset des GM-Boards nach `ptp_mode off` die GM→FOL-Asymmetrie behebt.
- PASS → persistenter LAN865x/TC6-Zustand auf GM-Seite
- FAIL → Ursache liegt außerhalb des GM (Follower oder Bus)

### Testablauf

`ptp_frame_test.py --reset-gm-after-ptp`: Nach A/B-PTP-Phase wird `ptp_mode off` gesetzt, dann `reset` auf GM gesendet, 8 s Hochlauf gewartet, IP neu gesetzt, dann NoIP-Postcheck bidirektional.

### Ergebnis

| Messung | Ergebnis |
|---|---|
| PTP-Hits 0x88F7 (A Broadcast / B Multicast) | 0 / 0 |
| gmSyncs | 171 |
| GM SW-Reset ausgeführt | ✓ (reset + 8 s + setip) |
| NoIP Postcheck GM→FOL **nach GM-Reset** | **FAIL (0 Treffer)** |
| NoIP Postcheck FOL→GM | PASS (5 Treffer) |

### Schlussfolgerung — Weichenstellung

**Die Asymmetrie bleibt auch nach vollständigem GM-SW-Reset (inkl. LAN865x-Neuinitialisierung) bestehen.**

Das schließt den GM als alleinige Ursache aus. Der GM sendet nachweislich erfolgreich in frischem Zustand (NoIP-Vortest vor PTP ist immer PASS), aber nach der PTP-Phase und GM-Reset ist GM→FOL weiterhin FAIL.

**Neue Hypothese: Follower-seitiger oder Bus-seitiger Zustand.**

Kandidaten:
1. **Follower-LAN865x RX-Zustand** — nach PTP-Phase wird der Follower-LAN865x in einen Zustand versetzt, in dem er Frames vom GM verwirft oder nicht weiterleitert (RCA/RX-FIFO/Filter).
2. **PLCA-Koordination** — die PTP-Phase verändert den PLCA-Zustand auf dem Bus so, dass GM-TX-Slots nach `ptp_mode off` nicht mehr korrekt zugeteilt werden.
3. **Follower-Firmware** — `ptp_mode off` auf Follower setzt einen RX-Filter oder Bridge-Eintrag nicht zurück.

**Nächster Schritt: Follower-SW-Reset** (statt GM) nach PTP → ist die Asymmetrie dann behoben, liegt die Ursache eindeutig auf Follower-Seite.

---

## Update: Schritt 5 — Isolationstest Follower-SW-Reset (2026-04-01)

### Ziel

Prüfen ob ein SW-Reset des Follower-Boards nach `ptp_mode off` die GM→FOL-Asymmetrie behebt.
- PASS → persistenter Zustand auf Follower-Seite (LAN865x/TC6/Firmware)
- FAIL → Ursache liegt im Bus-Zustand (PLCA) oder ist bilateral

### Testablauf

`ptp_frame_test.py --reset-fol-after-ptp`: Nach A/B-PTP-Phase wird `ptp_mode off` gesetzt, dann `reset` auf FOL gesendet, 8 s Hochlauf gewartet, IP neu gesetzt (`setip eth0 192.168.0.30 255.255.255.0`), 2 s Stabilisierung, dann NoIP-Postcheck bidirektional.

### Ergebnis

| Messung | Ergebnis |
|---|---|
| PTP-Hits 0x88F7 (A Broadcast / B Multicast) | 0 / 0 |
| gmSyncs | 171 |
| Follower SW-Reset ausgeführt | ✓ (reset + 8 s + setip + 2 s) |
| NoIP Postcheck GM→FOL **nach Follower-Reset** | **✓ PASS (5 Treffer)** |
| NoIP Postcheck FOL→GM | ✓ PASS (5 Treffer) |
| Postcheck gesamt | **✓ PASS** |

### Terminal-Ausschnitt (Kernstelle)

```
[Isolationstest] Follower-SW-Reset wird ausgefuehrt ...
Warte 8 s auf Follower-Neustart ...
Follower-Reset abgeschlossen.
[FOL] >> setip eth0 192.168.0.30 255.255.255.0
[FOL] << Set ip address OK
Warte 2 s auf IP-Stack-Stabilisierung nach Reset ...

Richtung 1: GM -> FOL
  Treffer: 5×  -> ✓ PASS
Richtung 2: FOL -> GM
  Treffer: 5×  -> ✓ PASS
Postcheck gesamt: ✓ PASS
```

### Schlussfolgerung — Durchbruch

**Die Asymmetrie ist nach einem SW-Reset des Followers vollständig behoben.**

Damit steht fest: Die Ursache liegt ausschließlich auf Follower-Seite — ein persistenter Zustand im Follower-Board (LAN865x-TC6, RX-FIFO, Firmware-Filter oder Bridge-Tabelle) wird durch die PTP-Phase gesetzt und durch `ptp_mode off` allein **nicht** zurückgesetzt.

Kombiniert mit Schritt 4:
- GM-Reset nach PTP → FAIL (GM nicht die Ursache)
- **Follower-Reset nach PTP → PASS (Follower ist die Ursache)**

Das Problem ist vollständig isoliert: Es handelt sich um einen persistenten Follower-seitigen Zustand, der durch `ptp_mode follower` gesetzt wird, aber in `ptp_mode off` nicht vollständig rückgängig gemacht wird.

---

## Nächste sinnvolle Schritte

1. ~~GM-seitigen Cleanup beim Verlassen des PTP-Modus ergänzen~~ (MAC_TI, PPSCTL, TXMLOC — durchgeführt, keine Wirkung auf Asymmetrie)

2. ~~**Isolationstest: SW-Reset des GM-Boards nach ptp_mode off**~~ (durchgeführt — FAIL → Ursache liegt NICHT auf dem GM)

3. ~~**Isolationstest: SW-Reset des Follower-Boards nach ptp_mode off**~~ (durchgeführt — **✓ PASS → Ursache liegt auf Follower-Seite**)

4. ~~**Follower-Deinit untersuchen**~~ (durchgeführt — Root Cause gefunden, siehe Schritt 6)

5. ~~**OA_CONFIG0-Register auf Follower lesen**~~ (nicht erforderlich — Bug gefunden und gefixt)

6. ~~**Follower-Firmware `PTP_FOL_task.c` analysieren**~~ (durchgeführt — Root Cause in `app.c` gefunden)

---

## Update: Schritt 6 — Root Cause gefunden und gefixt (2026-04-01)

### Root Cause: Fehlende `TCPIP_PKT_PacketAcknowledge()` im 0x88F7-Handler

**Fundstelle:** `src/app.c`, Funktion `pktEth0Handler()`.

Beim Vergleich der beiden EtherType-Handler wurde der Bug sofort sichtbar:

```c
// EtherType 0x88B5 (NoIP) — KORREKT:
TCPIP_PKT_PacketAcknowledge(rxPkt, TCPIP_MAC_PKT_ACK_RX_OK);  // ← Buffer freigegeben
return true;

// EtherType 0x88F7 (PTP) — BUG:
PTP_FOL_OnFrame(rxPkt->pMacLayer, (uint16_t)rxPkt->pDSeg->segLen, rxTs);
return true;  // ← KEIN PacketAcknowledge → RX-Puffer wird NIE freigegeben!
```

### Warum das alle Symptome erklärt

| Symptom | Ursache |
|---|---|
| Nach PTP-Phase: GM→FOL FAIL | RX-Paketpool des Followers erschöpft: ~342 Puffer (171 Sync + 171 FollowUp) geleakt |
| FOL→GM immer PASS | GM läuft nicht im PTP_SLAVE-Modus → kein 0x88F7-Empfang → kein Leak |
| SW-Reset Follower hebt Asymmetrie auf | TCPIP-Paketpool nach Reset wieder voll |
| NoIP-Vortest (vor PTP) immer PASS | Pool noch nicht geleakt |
| PTP-Hits = 0 in ipdump | ipdump-Ausgabe (`E0:PTP[0x88F7]`) nur mit `ipdump_mode != 0` — im PTP-Test ist `ipdump 0` auf FOL → kein Log, aber Frames **wurden empfangen** und buffers **wurden geleakt** |

### Fix

```c
// src/app.c — pktEth0Handler(), 0x88F7-Block:
PTP_FOL_OnFrame(rxPkt->pMacLayer, (uint16_t)rxPkt->pDSeg->segLen, rxTs);
TCPIP_PKT_PacketAcknowledge(rxPkt, TCPIP_MAC_PKT_ACK_RX_OK);  // ← HINZUGEFÜGT
return true;
```

### Testergebnis nach Fix

Build → Flash → `ptp_frame_test.py` (ohne Reset-Flags):

| Messung | Ergebnis |
|---|---|
| PTP-Hits 0x88F7 (A Broadcast) | **96** (vorher: 0) |
| PTP-Hits 0x88F7 (B Multicast) | **96** (vorher: 0) |
| gmSyncs | 171 |
| NoIP Postcheck GM→FOL (nach PTP) | **✓ PASS** (vorher: FAIL) |
| NoIP Postcheck FOL→GM (nach PTP) | ✓ PASS |
| Postcheck gesamt | **✓ PASS** |

### Schlussfolgerung — Issue geschlossen

Das PTP-Frame-Problem ist vollständig gelöst. Die 0x88F7-Frames wurden immer korrekt empfangen und an `PTP_FOL_OnFrame()` weitergereicht — aber der fehlende `PacketAcknowledge`-Aufruf führte über die gesamte PTP-Phase zu einem Speicherleck im TCPIP-RX-Paketpool, das nach ~342 Frames (171×Sync + 171×FollowUp) den Pool erschöpfte und damit alle nachfolgenden Empfangsvorgänge blockierte.
