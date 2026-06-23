# TC6 Register-Zugriff: Race Condition & SPI-Bus Konflikt

**Stand: 26.03.2026**  
**Plattform:** LAN865X Rev.B0 auf vcoreiii MPU (ARM Cortex-A8, Linux 6.12.48)  
**Treiber:** `lan8650` v6.12.48, Interface `eth0`, 10BASE-T1S P2MP/Half, SPI0.0

---

## 1. Zusammenfassung

Jeder direkte Register-Zugriff auf den LAN865X TC6-Chip — egal ob via **ioctl**
(`/dev/lan865x_eth0`) oder via **debugfs** (`/sys/kernel/debug/lan865x_eth0/register`) —
**destabilisiert den T1S-Link** und macht das Gerät für nachfolgende Netzwerkkommunikation
unbrauchbar. Ein Power-Cycle beider Boards ist die einzige Wiederherstellungsmaßnahme.

---

## 2. Empirische Beweise

### Testergebnisse (26.03.2026)

Alle Tests wurden mit `register_impact_test.py` und `debugfs_impact_test.py` durchgeführt.
Ablauf: iperf-Baseline → Register-Reads → iperf-Nachher.

| Test | Methode | eth0-Status | Phase 3 iperf | Ergebnis |
|---|---|---|---|---|
| `--skip-regs` (nur down/up) | keiner | DOWN→UP | ✅ 4.80 Mbits/sec | Harmlos |
| `register_impact_test.py` | ioctl | DOWN | ❌ No route to host | **Link tot** |
| `debugfs_impact_test.py` | debugfs | UP | ❌ Server sofort beendet | **Link tot** |

### Schlüsselbeobachtungen

**ioctl-Test (eth0 DOWN):**
```
[MPU] Ping FEHLGESCHLAGEN — MCU nicht erreichbar!
      PING 192.168.0.200 ... 4 packets transmitted, 0 received, 100% packet loss
[MPU] tcp connect to 192.168.0.200 port 5001 failed (No route to host)
```

**debugfs-Test (eth0 UP):**
```
[1]+  Done    iperf -s -u -i 1 -B 192.168.0.5          ← Server sofort beendet
[MPU] tcp connect to 192.168.0.200 port 5001 failed    ← keine Verbindung mehr
```

**Kontrolle (`--skip-regs`, nur eth0 down/up):**
```
[MPU] Ping OK — 4/4 Antworten ✓
[  1] 0.00-12.24 sec  7.00 MBytes  4.80 Mbits/sec       ← alles normal
```

---

## 3. Ursachenanalyse

### 3.1 OA TC6 SPI-Protokoll Grundlagen

Der LAN865X implementiert den **OPEN Alliance 10BASE-T1x MAC-PHY Serial Interface Standard
(TC6)**. Das SPI-Protokoll ist kein einfaches Register-Read/Write — es ist ein
**zustandsbasiertes Chunk-Framing-Protokoll**:

```
Host (MPU)                        LAN865X (TC6)
─────────────────────────────────────────────────
TX Chunk (64 Byte)    ──────→     RX verarbeiten
                      ←──────     TX Chunk (64 Byte)
[Daten + Header + Footer stets gleichzeitig, Full-Duplex]
```

Jede SPI-Transaktion enthält:
- **Header**: Daten-/Konfigurations-Frame-Typ, Chunk-Nummer, Sequence Counter
- **Footer**: RX-Buffer-Status, Fehlerflags, Inter-Frame-Gap-Status
- **Payload**: Die eigentlichen Ethernet-Frames (fragmentiert auf 64-Byte-Chunks)

Der Chip erwartet einen **streng sequenziellen Ablauf** der SPI-Transaktionen.
Jede Transaktion baut auf dem Zustand der vorherigen auf.

### 3.2 Der Kernel-Treiber und sein kthread

Der `lan8650`-Treiber startet bei `eth0 up` einen dedizierten **kthread**
(Kernel-Thread), der ständig SPI-Transaktionen mit dem TC6 durchführt:

```c
/* lan865x.c (vereinfacht) */
static int lan865x_kthread(void *data) {
    struct lan865x_priv *priv = data;
    while (!kthread_should_stop()) {
        /* Sendet TX-Frames, liest RX-Buffer, verarbeitet Interrupts */
        oa_tc6_process(priv->oa_tc6);  // ← SPI-Transaktion
        wait_for_completion_interruptible(&priv->tx_wake);
    }
}
```

Parallel läuft **IRQ 37** (SPI-Interrupt), der bei eingehenden Daten den kthread weckt.

### 3.3 Die Race Condition: Zwei SPI-Nutzer, kein Lock

Wenn `lan_read` (ioctl) oder debugfs eine Register-Leseoperation startet:

```c
/* lan865x.c — ioctl-Handler */
static long lan865x_ioctl(struct file *filp, unsigned int cmd, unsigned long arg) {
    if (netif_running(priv->netdev))
        return -EBUSY;   // Schutz nur bei link UP via netif_running()!
    
    /* Bei eth0 DOWN: netif_running() = false → kein Schutz */
    ret = oa_tc6_read_register(priv->oa_tc6, reg.address, &reg.value);
}
```

```c
/* lan865x.c — debugfs-Handler */
static ssize_t lan865x_debugfs_write(struct file *file, ...) {
    /* KEIN netif_running()-Check! */
    oa_tc6_read_register(priv->oa_tc6, address, &value);  // direkt!
}
```

Beide Pfade rufen `oa_tc6_read_register()` auf, das intern eine **SPI-Transaktion**
des Typs `OA_TC6_REGISTER_READ` sendet:

```
SPI-Bus Chrono (Race Condition):
─────────────────────────────────────────────────────────────
Zeit → 

kthread:   [DATA_CHUNK #N] ──→  [DATA_CHUNK #N+1] ──→ ...
                                     ↑
ioctl/debugfs:    ────────── [REG_READ] ─────────────────────

TC6 empfängt: DATA_CHUNK #N, dann REG_READ (erwartet aber DATA_CHUNK #N+1)
→ Sequence-Counter stimmt nicht mehr
→ TC6 geht in Fehler-Latch-Zustand
→ Chip reagiert nicht mehr auf normale Frames
```

### 3.4 Warum `eth0 DOWN` nicht hilft

Der `netif_running()`-Check in der ioctl schützt nur den **Netzwerk-Stack-Zustand**.
Der OA TC6 kthread und der IRQ-Handler haben einen **eigenen Lebenszyklus**:

```c
/* oa_tc6.c (Linux kernel) */
static int oa_tc6_sys_open(struct oa_tc6 *tc6) {
    tc6->kthread = kthread_run(oa_tc6_task, tc6, "oa-tc6-%s", ...);
    // kthread läuft bis zum expliziten Stop (oa_tc6_sys_close)
}
```

`ip link set eth0 down` ruft `ndo_stop()` auf, was **nicht `oa_tc6_sys_close()`**
aufruft — der kthread bleibt aktiv! Der TC6 verarbeitet weiterhin intern Frames
(PLCA-Beacon, Keep-Alive-SPI-Transaktionen etc.).

### 3.5 Warum debugfs trotz Link-UP scheitert

debugfs hat zwar **keinen** `netif_running()`-Check, aber beim aktiven Link UP
läuft der kthread **hochfrequent** (IRQ 37-getrieben). Das Zeitfenster für eine
saubere Register-Transaktion ohne Kollision ist extrem klein.

Unsere Tests zeigen: **21 debugfs-Reads in 1.73s** — scheinbar erfolgreich — aber
Phase 3 iperf schlägt dennoch fehl. Die TC6-State-Machine ist beschädigt, aber
`OA_STATUS0` zeigt noch `0x00000000` (Fehler werden erst beim nächsten Frame sichtbar).

---

## 4. Technischer Zustandsverlauf nach einem Race-Condition-Event

```
Normalzustand:
  kthread → SPI → TC6: [CHUNK #N] → TC6 antwortet → PLCA aktiv
  
Nach Register-Read-Kollision:
  kthread → SPI → TC6: [CHUNK #N+1] → TC6: "Sequenz-Fehler! STS[TXPE]=1"
  TC6 setzt TX_PROTOCOL_ERROR-Flag
  kthread versucht Recovery, sendet RESET_FRAME → weiterer Konflikt
  TC6 geht in RXBO (RX Buffer Overflow) oder TXPE (TX Protocol Error) Zustand
  
Endergebnis:
  - eth0 RX/TX Counter frieren ein
  - IRQ 37 feuert nicht mehr (keine neuen Frames)
  - PLCA-Bus sendet weiterhin Beacons, aber keine Daten-Chunks mehr
  - Nur Power-Cycle setzt TC6-State-Machine zurück
```

---

## 5. Registerwerte als Indikator

Während des problematischen Zustands zeigen die Register:

| Register | Normaler Wert | Problemzustand |
|---|---|---|
| `OA_STATUS0` (0x08) | `0x00000000` | `0x00000002` (TXPE) oder `0x00000008` (RXBOE) |
| `OA_BUFSTS` (0x0B) | `0x00003000` | `0x00000000` (Buffer leer/hängend) |
| `MAC_NET_CTL` (0x10000) | `0x0000000C` | `0x00000000` (TX/RX deaktiviert) |
| `RX_GOOD_FRAMES` (0x20000) | steigt an | bleibt eingefroren |
| `PLCA_STATUS0` (0x800304) | `0x8000` (PCIS=1) | `0x00000000` |

**Wichtig:** Unmittelbar nach der Kollision können die Register noch korrekte Werte
zeigen — der Fehler manifestiert sich erst beim nächsten Ethernet-Frame-Versuch.

---

## 6. Maßnahmen zur Behebung

### 6.1 Kurzfristig: Kein direkter Register-Zugriff (Status Quo)

**Für Read-Only-Diagnose** ist nur noch folgendes sicher:

```bash
# Sicher: ethtool (via Kernel-API, kein direkter TC6-Zugriff)
ethtool eth0
ethtool --get-plca-status eth0
ethtool --get-plca-cfg eth0

# Unsicher (destroys link):
lan_read 0x00000000          # ioctl → TC6 beschädigt
echo '0x...' > /sys/kernel/debug/lan865x_eth0/register  # debugfs → TC6 beschädigt
```

### 6.2 Mittelfristig: Mutex im Treiber

Die korrekte Lösung ist ein **gemeinsamer Mutex** für alle SPI-Zugriffe auf `oa_tc6`:

```c
/* In struct oa_tc6 (oa_tc6.c) hinzufügen: */
struct mutex spi_lock;   /* Serialisiert alle SPI-Transaktionen */

/* In oa_tc6_process() — kthread-Pfad: */
mutex_lock(&tc6->spi_lock);
oa_tc6_spi_transfer(tc6, ...);   /* DATA_CHUNK */
mutex_unlock(&tc6->spi_lock);

/* In oa_tc6_read_register() — ioctl/debugfs-Pfad: */
mutex_lock(&tc6->spi_lock);
oa_tc6_spi_transfer(tc6, ...);   /* REG_READ */
mutex_unlock(&tc6->spi_lock);
```

**Problem dabei:** Der kthread läuft in einem Tight-Loop mit hoher Frequenz.
Ein blockierender mutex würde den kthread stören und die Latenz stark erhöhen.
Besser wäre ein **trylock mit Retry**:

```c
/* Alternativer Ansatz: Register-Read-Request über kthread */
static int oa_tc6_read_register_safe(struct oa_tc6 *tc6, u32 addr, u32 *val) {
    /* Request in Queue einreihen */
    tc6->pending_reg_read.addr = addr;
    tc6->pending_reg_read.pending = true;
    complete(&tc6->reg_work_wake);       /* kthread wecken */
    
    /* Auf Ergebnis warten (kthread führt Read in seinem Zeitfenster aus) */
    wait_for_completion_timeout(&tc6->reg_read_done, HZ);
    *val = tc6->pending_reg_read.result;
    return tc6->pending_reg_read.error;
}
```

Dieser Ansatz stellt sicher, dass der Register-Read **nur vom kthread selbst**
durchgeführt wird — in einem Zeitfenster, in dem kein DATA_CHUNK aktiv ist.

### 6.3 Langfristig: Upstream-Patch

Das Problem ist im Linux-Upstream-Treiber `drivers/net/ethernet/microchip/lan865x/`
sowie in `drivers/net/phy/oa_tc6.c` zu beheben. Ein korrekter Patch müsste:

1. **`oa_tc6.c`**: Alle `oa_tc6_*_register()`-Aufrufe durch einen Work-Queue-Mechanismus
   serialisieren (analog zu Punkt 6.2).

2. **`lan865x.c` debugfs**: Den `netif_running()`-Check auch in den debugfs-Write-Handler
   einbauen, solange kein sicherer paralleler Zugriff möglich ist:
   ```c
   if (netif_running(priv->netdev)) {
       pr_warn("lan865x: Register access blocked — link is UP\n");
       return -EBUSY;
   }
   ```

3. **`lan865x.c` ioctl**: Den `netif_running()`-Check durch einen Check auf den
   tatsächlichen kthread-Zustand ersetzen:
   ```c
   /* Nicht ausreichend: */
   if (netif_running(priv->netdev)) return -EBUSY;
   
   /* Besser: kthread pausieren */
   if (priv->oa_tc6->kthread)
       kthread_park(priv->oa_tc6->kthread);
   ret = oa_tc6_read_register(...);
   if (priv->oa_tc6->kthread)
       kthread_unpark(priv->oa_tc6->kthread);
   ```

### 6.4 Sofortmaßnahme für Debugging: ethtool-basierter Zugriff

Für die Register, die über das PLCA/PHY-Interface erreichbar sind, ist `ethtool`
die einzig sichere Methode:

```bash
# PLCA-Konfiguration (sicher)
ethtool --get-plca-cfg eth0
ethtool --set-plca-cfg eth0 node-cnt 8

# Link-Status (sicher)
ethtool eth0

# Statistiken — falls vom Treiber implementiert (sicher)
ethtool -S eth0
```

Für direkte OA TC6 / MAC-Register gibt es **keine sichere Methode** ohne Kernel-Fix.

---

## 7. Betroffene Dateien

| Datei | Relevanz |
|---|---|
| `drivers/net/ethernet/microchip/lan865x/lan865x.c` | ioctl + debugfs Handler |
| `drivers/net/phy/oa_tc6.c` | TC6 SPI-Protokoll, kthread, Register-Zugriff |
| `include/linux/oa_tc6.h` | `struct oa_tc6` Definition |

**Lokale Pfade (Buildroot):**
```
mchp-brsdk-source-2025.12/output/mybuild/build/linux-custom/
  drivers/net/ethernet/microchip/lan865x/lan865x.c
  drivers/net/phy/oa_tc6.c
```

---

## 8. Testprotokolle (Reproduzierbare Beweise)

| Datum | Tool | Ergebnis |
|---|---|---|
| 26.03.2026 | `register_impact_test.py --skip-regs` | Phase 3 OK — `eth0 down/up` allein harmlos |
| 26.03.2026 | `register_impact_test.py` (ioctl) | Phase 3 FAIL — ioctl-Reads zerstören Link |
| 26.03.2026 | `debugfs_impact_test.py` | Phase 3 FAIL — debugfs-Reads zerstören Link |

Alle Tests: MCU=COM8, MPU=COM9, iperf 12s, LAN865X Rev.B0, Linux 6.12.48.

---

## 9. Workaround-Fazit

| Verwendungszweck | Empfohlene Methode | Sicher? |
|---|---|---|
| Link-Status ablesen | `ethtool eth0` | ✅ |
| PLCA-Status ablesen | `ethtool --get-plca-status eth0` | ✅ |
| OA TC6 Register lesen | **keine sichere Methode verfügbar** | ❌ |
| MAC-Register lesen | **keine sichere Methode verfügbar** | ❌ |
| Register schreiben | **keine sichere Methode verfügbar** | ❌ |
| Diagnose ohne Risiko | MCU-seitige RTOS-Ausgaben (UART) | ✅ |

**Einzige Wiederherstellung nach Schaden:** Power-Cycle beider Boards.
`ip link set eth0 down && ip link set eth0 up` reicht nicht aus.
