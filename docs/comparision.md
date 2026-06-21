# `rcp.c` vs. der generierte `lan866x_client.c` — Größen- & Architekturvergleich

> Festhalten der Erkenntnisse aus einer Analyse-Session zur Frage: *Lohnt es sich,
> den Microchip-Code-Generator in `lan866x-tools` einzupflegen, und was kostet der
> generierte C-Client an Flash/RAM gegenüber dem handgeschriebenen `src/rcp.c`?*
>
> **Kontext:** Der generierte Client (`lan866x_client.c`) ist NDA-Material und lebt
> unter `EVB/SOMEIP/lan866x-someip-client-v1.10.0a/` (gitignored). Dieses Dokument
> enthält nur generische Engineering-Fakten (Architektur, Code-/RAM-Größen) — keine
> NDA-Inhalte.

## 1. Worum es geht — „Generator" ist zweideutig

Es gibt zwei verschiedene Dinge mit dem Namen „Generator":

1. **Der Microchip Code-Generator** (`generator/` im SDK): erzeugt aus der einen
   Quelle `lan866x.proto` per protoc + Jinja2 die Clients in **6 Sprachen**
   (C, C++, Python, C#, Java, Rust) + Blockly + ARXML + Wireshark + Doxygen.
2. **Der Laufzeit-Serializer** `SOMEIP_Generator_Fill_*` in `libsomeip` — *kein*
   Code-Generator, sondern füllt zur Laufzeit den SOME/IP-Byte-Strom. Wird von
   `rcp.c` direkt benutzt.

Dieses Dokument betrifft (1) — den Code-Generator und seine C-Ausgabe.

## 2. Warum den Microchip-Generator NICHT 1:1 vendoren

- **NDA-Bruch.** Quelle `lan866x.proto` ist aus dem confidential
  `KEPLER_Architecture_SOME_IP_API_Catalogue.pdf` abgeleitet; das SDK steht unter
  Microchip SLA. Beides muss unter `EVB/` (gitignored) bleiben.
- **Falsche Ausgabe.** Der Generator produziert C **+ C++/Python/C#/Java/Rust/
  Blockly/ARXML/Doxygen**. `lan866x-tools` ist bewusst *pure C, 0 C++-Symbole,
  Superloop, MCU-portierbar*. 95 % der Ausgabe wäre Ballast.
- **Schwere Toolchain.** protoc 27.2 + Ruby+Devkit + bake-toolkit + Jinja2 +
  clang-format 18 + doxygen + dotnet + JDK + cargo. Soll nicht Voraussetzung von
  `build.bat` werden.

## 3. Der EVB-Generator erzeugt zwar C — aber den „falschen"

`generate-lan866x.bat` schreibt nach `EVB/.../lan866x_c/`:

| Datei | Zeilen |
|---|--:|
| `lan866x_client.c` | **3925** (voller blockierender Multi-Node-Client, alle 49 Methoden) |
| `lan866x_client.h` | 383 |
| `lan866x_common.h` | 823 (alle `*Var_t`/`*Reply_t`-Structs) |

Zum Vergleich: `src/rcp.c` = **782 Zeilen**, `src/rcp.h` = 182. `rcp.c` ist eine
**bewusste handgeschriebene Reduktion**, nicht der generierte Client. Der generierte
C-Client kann nicht einfach nach `src/` wandern, weil:

1. **Falsches Ausführungsmodell** — generiert = blockierend + Multi-Node-Factory
   (`MAX_REMOTE_NODES = 12`); `rcp.c` = async/non-blocking, Single-Node, Superloop.
2. **Die Host-Gotchas fehlen** — Reply-Drop-Pacing, `ReadI2C`-statt-`WriteAndReadI2C`
   für Scans, Compound-SPI `0x1509`, Displays-als-ein-RTP-Frame, `FinishUpdate`-rc
   ignorieren. Das weiß das proto nicht; es steht nur im Hand-`rcp.c`.
3. **Lizenz/NDA** — 3925 Zeilen `Copyright Microchip / DO NOT EDIT` ins Repo zu
   kopieren ist genau das Vendor-Bundling, das die Repo-Regel auf `libepmicrochip/`
   beschränkt.

## 4. Warum der generierte Client ~5× mehr Zeilen hat

Beide rufen *dieselben* `libsomeip`-Primitive auf (`SOMEIP_Generator_Fill_*`,
`SOMEIP_Parser_Read_*`, `SOMEIP_Transmit_*`). Die eigentliche SOME/IP-Logik liegt
bei **beiden** in der Lib. Der Unterschied liegt nur in der Schicht darüber:

| Faktor | generiert | `rcp.c` |
|---|---|---|
| **1. Faktorisierung** | Header-Aufbau + Send + Wait + Retry **inline pro Methode** (~70 Zeilen × 49) | gemeinsamer Kern **einmal** in `rcp_attempt()`/`rcp_xfer()` → Methode ~13 Zeilen |
| **2. Methodenumfang** | alle 49 Methoden | nur die genutzte Teilmenge |
| **3. Modell** | Multi-Node-Factory (12) + blockierend | Single-Node + Superloop, **plus** async-API zusätzlich |

Beispiel `ReadI2C`: generiert ~70 Zeilen, `rcp_read_i2c` ~13 Zeilen — **identisches
Byte-Ergebnis auf dem Kabel.**

## 5. Messung: Objektcode & RAM

Methodik: beide `.c` mit demselben Compiler/Flags zu `.o` übersetzt und mit `size`
vermessen.

- Compiler: MinGW-w64 **GCC 16.1.0**, Flags `-c -Os -DWIN32 -D_WIN32_WINNT=0x0601`
- `rcp.c` gegen `libepmicrochip/libsomeip` (Repo)
- `lan866x_client.c` gegen die SDK-eigene `someip`-Lib (v1.10.0, braucht die
  neueren `_ExpectTag`-Parser, die der Repo-`libsomeip` noch fehlen)

> ⚠️ **Einschränkung:** Das sind **x86-64**-Zahlen, nicht das MCU-Target. ARM
> Thumb-2 wäre absolut ~30–40 % kleiner. Der **relative** Vergleich und die
> RAM-Aussagen bleiben gültig.

### 5.1 Objektcode (`.text`, `-Os`)

| Schicht | `rcp.c` | generierter `lan866x_client.c` |
|---|--:|--:|
| Wrapper-/Client-Schicht | **12 040 B (~12 KB)** | **38 820 B (~38 KB)** |
| Verhältnis | 1× | **≈ 3,2× (+27 KB)** |
| gemeinsame `libsomeip` (gleich beide Seiten) | ~11,7 KB | ~11,7 KB |

→ Die Code-Schicht über der Lib wird **~3× so groß, +27 KB Flash**. Das ist Faktor 1
(70 vs. 13 Zeilen/Methode, 49× ausgestanzt) in Bytes.

> Praxis: die 38,8 KB sind **alle 49 Methoden**. Mit
> `-ffunction-sections -Wl,--gc-sections` verwirft der Linker ungenutzte. Aber selbst
> für *dieselbe* Methoden-Teilmenge bleibt der generierte Code pro Methode ~3× größer.

### 5.2 RAM (statisch, `.bss`)

Überraschend: der generierte Client braucht *weniger* **eigenes** statisches RAM.

| | `rcp.c` | generierter Client |
|---|--:|--:|
| eigenes statisches RAM | **3 840 B** | **352 B** |
| Aufschlüsselung | `s_rx[1440]` + `s_scratch[1440]` + `s_eps[]` | nur die 12-Node-Tabelle |

Grund: `rcp.c` hält **zwei eigene 1440-B-Puffer** (`s_rx` für die Antwort,
`s_scratch` für die Parameter). Der generierte Client serialisiert direkt in
`env.tb->payload` und parst direkt aus `env.pWait->rxBuf` — kein Kopieren, keine
eigenen Puffer.

### 5.3 Der dominante RAM-Term liegt woanders — und ist gleich

Das eigentliche RAM steckt bei **beiden** im konfigurierbaren Transmit-Buffer-Pool
der `libsomeip`:

```
Pool = SOMEIP_TRANSMIT_MAX_QUEUE_ENTRIES × ~1472 B
  Repo someip-cfg:  8 × 1472 ≈ 11,8 KB   (gemessen: someip-transmit.o bss = 11 808 B)
  SDK-Beispiel-cfg: 3 × 1472 ≈  4,4 KB
```

Das hängt nur an `someip-cfg.h`, **nicht** an generiert vs. Hand-`rcp.c`. (CLAUDE.md:
„core static RAM ≈ 15 kB" = dieser Pool.)

### 5.4 Gesamtbild

```
                       FLASH (.text)      eigenes RAM (.bss)   + libsomeip-Pool (cfg)
rcp.c:                 ~12 KB             3,8 KB               ~12 KB (Q=8) →  Σ ~28 KB
generierter Client:    ~39 KB            0,35 KB              ~12 KB (Q=8) →  Σ ~51 KB
                       └─ +27 KB Flash ┘ └ −3,5 KB RAM ┘      └─ gleich ──┘
```

Stack-Nuance: der generierte Client legt pro Aufruf die ganze
`struct FunctionEnvironment` + `SOMEIP_Header` aufs Stack (inline pro Methode);
`rcp.c` bündelt das in `rcp_attempt()`. Peak-Stack/Call ist beim Generat etwas höher,
aber nicht multipliziert (immer nur ein Call gleichzeitig).

## 6. Fazit

- Die Kosten der Code-Generierung sind ein **Flash-/ROM-Thema, kein RAM-Thema**:
  **~3× Code, +27 KB Flash**. RAM-seitig ist der generierte Client sogar minimal
  sparsamer (kein Kopier-Puffer); der große RAM-Posten (Transmit-Pool) ist auf beiden
  Seiten dieselbe konfigurierbare `libsomeip`.
- Auf einem flash-knappen MCU ist die Generierung relevant; RAM-seitig unkritisch.
- `rcp.c` ist im Ergebnis das, was herauskäme, wenn der Generator (a) den
  gemeinsamen Transaktionskern in eine Funktion zöge und (b) nur die genutzten
  Methoden im Single-Node-Superloop-Stil emittierte.

## 7. Optionen, falls man doch einen Generator will

- **A) Status quo (nichts einpflegen).** Bei proto-Änderung `generate-lan866x.bat`
  in EVB laufen lassen, Deltas in `lan866x_common.h` ansehen, von Hand in `rcp.c`
  nachziehen. Kostet nichts, driftet aber leise.
- **B) Schlanker eigener Generator.** Ein kleines `tools/gen/`-Skript, das aus
  `lan866x.proto` (bzw. einer einmalig extrahierten `methods.json`) **`rcp.c`-Stil**
  emittiert (Method-ID-Tabelle, Structs, Wire-Glue), pure C, async-tauglich,
  ~13 Zeilen/Methode. Input bleibt NDA-konform in `EVB/`; committet werden nur Skript
  + generierte C-Ausgabe. Idempotenz-Disziplin (DO-NOT-EDIT-Header, stabile
  Sortierung) vom SDK übernehmen.
- **C) EVB-Generator als Tabellen-Vorstufe.** Den Microchip-Generator in EVB laufen
  lassen, daraus nur eine maschinenlesbare Method-Tabelle ziehen, die der eigene
  Repo-Generator in `rcp.c`-Stil übersetzt.

Empfehlung wenn das Ziel „proto→`rcp.c`-Drift loswerden" ist: **B** (max. Eigen-
ständigkeit, keine schwere Toolchain im Build). Wenn das Ziel „mehr Sprachen/Methoden
aus einer Quelle" ist: beim Microchip-Generator in EVB bleiben und `lan866x-tools`
der schlanke C-Konsument lassen, der er ist.

---

### Reproduktion der Messung

```sh
# Repo-rcp.c
gcc -c -Os -DWIN32 -D_WIN32_WINNT=0x0601 \
    -Isrc -Iinclude -Ilibepmicrochip/libsomeip/inc -Ilibepmicrochip/libsomeip/stub \
    src/rcp.c -o rcp.o && size rcp.o

# generierter Client (Pfade relativ zum SDK-Wurzelordner unter EVB/)
gcc -c -Os -DWIN32 -D_WIN32_WINNT=0x0601 \
    -Ilan866x_c -Isomeip/libsomeip/inc -Isomeip/libsomeip/cfg/apg_zonaldemo \
    lan866x_c/lan866x_client.c -o genclient.o && size genclient.o
```
