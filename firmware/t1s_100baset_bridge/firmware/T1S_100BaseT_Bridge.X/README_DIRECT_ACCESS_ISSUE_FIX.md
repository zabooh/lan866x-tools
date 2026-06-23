# Known Limitation: Direct Register Access Issue (MCU Firmware)

## Entscheidung (23. März 2026)

> **Die MCU-Firmware wird nicht verändert.**
>
> Das Problem ist architektonisch in der TC6-Library (MCC-generierter Code) verankert
> und lässt sich in `app.c` nur durch zeitliches Hinauszögern kaschieren — nicht wirklich beheben.
> Solche Workarounds lösen das eigentliche Problem nicht, verschleiern es nur.
>
> **Operative Regel:** `lan_read` und `lan_write` dürfen nicht während aktivem
> Ethernet-Traffic verwendet werden. Das ist eine akzeptierte Einschränkung des
> Debug-Werkzeugs, keine Firmware-Fehlfunktion im Normalbetrieb.

## Dokument-Kontext

Dieses Dokument beschreibt die Analyse und Entscheidung zum in
`README_DIRECT_ACCESS_ISSUE.md` nachgewiesenen Verhalten.

**Gemessene Auswirkung (reproduzierbar, 23. März 2026):**

| Modus | Case B LAN_READ Verlust |
|-------|------------------------|
| Normal (200ms, burst=1, 2M UDP) | **5%** |
| `--aggressive` (50ms, burst=3, 4M UDP) | **18%** |

**Root Cause:**  
`lan_read`/`lan_write` enqueuen in `regop_storage[REG_OP_ARRAY_SIZE=2]`.  
`TC6_Service()` wertet `serviceControl()` **vor** `serviceData()` aus →  
jede pending Register-Operation blockiert einen Data-Service-Zyklus vollständig.  
Die Priorität ist fest im MCC-generierten `tc6.c` codiert und wird nicht verändert.

---

## Architektur-Analyse: Warum es kracht

### `TC6_Service()` — die Prioritätsreihenfolge (MCC-generiert, nicht verändern)

Datei: `src/config/default/driver/lan865x/src/dynamic/tc6/tc6.c`, Zeile 227

```c
bool TC6_Service(TC6_t *g, bool interruptLevel)
{
    if (!g->intContext) {
        if (serviceControl(g)) {         // <-- erst Control (Register-Ops)
            if (!interruptLevel) intPending = true;
        } else if (g->enableData) {
            processDataRx(g);
            if (!serviceData(g, !interruptLevel)) {  // <-- dann erst Data
                ...
            }
        }
    }
}
```

**Konsequenz:** Jede Register-Operation, die in `regop_q` liegt, verdrängt
`serviceData()` vollständig für diesen Aufruf-Zyklus.

### Queue-Dimensionierung

```c
// tc6-conf.h
#define REG_OP_ARRAY_SIZE   (2u)   // lediglich 2 ausstehende Register-Ops möglich
#define SPI_FULL_BUFFERS    (1u)   // nur 1 SPI-DMA-Buffer

// user.h
#define SPI_FULL_BUFFERS    (2u)   // Override auf 2 (ändert aber nichts am Control-Priority-Problem)
```

Bei burst=3 werden **3 Kommandos in < 20ms** eingereiht → Queue sofort voll,
`TC6_ReadRegister()` gibt `false` zurück (→ CLI: „failed to start"),
aber die **2 bereits eingereihten Ops** blockieren trotzdem den Data-Pfad.

### Aufruf-Kette

```
CLI-Kommando "lan_read 0x00000000"
  → app.c: lan_read()
  → DRV_LAN865X_ReadRegister(0, addr, false, lan_read_callback, NULL)
  → drv_lan865x_api.c: TC6_ReadRegister(pDrv->drvTc6, addr, ...)
  → tc6.c: regop_stage1_enqueue_done(&g->regop_q)   ← ab hier blockiert Data
  → nächster TC6_Service()-Aufruf: serviceControl() gewinnt gegen serviceData()
```

---

## Warum ein Firmware-Fix keinen Sinn ergibt

Die naheliegenden Workarounds in `app.c` (Rate-Limit, Deferred Execution,
Maintenance-Window) verschieben das Problem zeitlich, lösen es aber nicht:

- **Rate-Limit:** Reduziert die Häufigkeit des Problems. Ein einzelner `lan_read`
  während Volllast verursacht trotzdem 1–5ms Datenpfad-Blockade.

- **Deferred Execution:** Führt den Zugriff im App-Task-Loop aus. Ändert nichts
  daran, dass `serviceControl()` weiterhin Vorrang vor `serviceData()` hat.

- **Maintenance-Window (`TC6_EnableData(false)`):** Die einzige Lösung mit echter
  Isolation — erfordert aber Eingriffe in MCC-generierten Code (`drv_lan865x_api.c`)
  und verursacht im Debug-Betrieb spürbare Traffic-Pausen.

Die echte Lösung wäre eine Änderung in `tc6.c` (Control/Data-Priorisierung),
was MCC-generiertem Code entspricht und bei jedem MCC-Regenerationslauf verloren
ginge. Das ist kein akzeptables Risiko.

---

## Operative Regel (verbindlich)

```
lan_read / lan_write dürfen NICHT verwendet werden während:
  - fwd (Forwarding) aktiv ist
  - iperf oder andere Traffic-Last läuft
  - zyklische Diagnose-Scans laufen (GUI, Skripte)

Erlaubt:
  - im Leerlauf (kein aktiver Traffic)
  - nach explizitem "fwd 0" und Warten auf Traffic-Ende
  - für einmalige Diagnose-Reads im Wartungszustand
```

Diese Einschränkung gilt bereits analog auf der MPU (dort korrumpiert `lan_read`
sogar die TC6-State-Machine vollständig — MCU-Verhalten ist vergleichsweise mild).

---

## Fix-Strategie — drei Stufen (dokumentiert, nicht umgesetzt)

Die folgenden Strategien wurden analysiert und bewusst **nicht implementiert**.
Sie sind hier zur Vollständigkeit dokumentiert, falls die Entscheidung
zu einem späteren Zeitpunkt revidiert wird.

### Stufe 1: Rate-Limit in `app.c` (nicht umgesetzt)

**Wirkung:** Verhindert Burst-Flooding der `regop_q` durch Mindestabstand zwischen Zugriffen.  
**Warum nicht:** Löst das Problem nicht, schützt nur gegen schnelle Bursts.
Ein einzelner Zugriff während Volllast verursacht trotzdem Verlust.

```c
/* Beispiel — nicht in Firmware übernommen */
#define LAN_REG_ACCESS_MIN_INTERVAL_MS  200u
static uint32_t lan_reg_last_access_ms = 0;

static bool lan_reg_access_allowed(void) {
    uint32_t now = (uint32_t)(SYS_TIME_CountToMS(SYS_TIME_Counter64Get()));
    if ((now - lan_reg_last_access_ms) >= LAN_REG_ACCESS_MIN_INTERVAL_MS) {
        lan_reg_last_access_ms = now;
        return true;
    }
    return false;
}
```

### Stufe 2: Deferred Execution via `APP_Tasks()` (nicht umgesetzt)

**Wirkung:** CLI-Handler setzt nur Flag, Ausführung im Task-Loop.
`TCPIP_MAC_RES_PENDING` → automatisches Retry statt Verwerfen.  
**Warum nicht:** Ändert nichts am Control/Data-Prioritätsverhältnis in `tc6.c`.
Verschiebt nur den Ausführungszeitpunkt.

### Stufe 3: Maintenance-Fenster via `TC6_EnableData(false)` (nicht umgesetzt)

**Wirkung:** Echter Datenpfad-Stopp während Register-Zugriff.  
**Warum nicht:** Erfordert Änderungen in MCC-generiertem `drv_lan865x_api.c`
(neuer `DRV_LAN865X_SuspendData()` Wrapper). Wird bei MCC-Regeneration überschrieben.

---

## Zusammenfassung

| Aspekt | Status |
|--------|--------|
| Bug reproduzierbar nachgewiesen | ✅ ja (`mcu_direct_access_proof.py`) |
| Root Cause lokalisiert | ✅ `tc6.c`: `serviceControl()` vor `serviceData()` |
| Firmware-Fix implementiert | ❌ bewusst nicht — Workarounds lösen das Problem nicht |
| Operative Regel dokumentiert | ✅ s.o. |
| Verifikations-Tool verfügbar | ✅ `mcu_direct_access_proof.py --aggressive` |




---

## Zusammenfassung

Das Problem ist architektonisch klar lokalisiert: `lan_read`/`lan_write` konkurrieren
mit dem Ethernet-Datenpfad auf demselben TC6-Service-Loop, weil `serviceControl()`
immer Vorrang vor `serviceData()` hat und `REG_OP_ARRAY_SIZE=2` die Queue schnell sättigt.

Der Fix ist **vollständig innerhalb von `src/app.c`** umsetzbar (Stufe 1+2),
ohne Änderungen an MCC-generiertem Code. Die Verifikation mit
`mcu_direct_access_proof.py` ist in < 60s automatisiert durchführbar.
