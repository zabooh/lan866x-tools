# Theoretische Betrachtung: konvergierende Zwei-Knoten-Software-NTP über einen T1S-Strang

Diese Notiz untersucht **theoretisch**, ob sich zwei T1S-Knoten mit demselben
Software-NTP-Mechanismus dieses Projekts (`ntp_sync.c` / `lan866x-ntpsync`,
t1/t2/t3/t4-Austausch alle 250 ms) **konvergierend** synchronisieren lassen: beide
Boards tauschen ständig Zeitstempel aus, mitteln die Ergebnisse und nähern sich
zeitlich immer weiter an. Es geht um die Frage: **Wie genau und wie zuverlässig kann
das werden — und wo ist die physikalische Grenze?**

Grundlage sind die in diesem Repo **real gemessenen** Werte (siehe
[NTP_TIMING.md](NTP_TIMING.md)):

| Größe | gemessen | Quelle |
|---|---|---|
| Zähler-Auflösung | **16 ns** (SYS_TIME @ 60 MHz) | `ntp`-Status |
| Oszillator-Drift, frei laufend | **~1600 ppm** (≈ 1,6 ms/s) | `ntp` est. drift / bridge_delay |
| Sync-Residuum (1 Austausch) | **~150–360 µs** | ntpsync residual |
| Round-Trip-Delay PC↔Bridge | **~0,5–1,0 ms** | ntpsync delay |
| Pfad-Asymmetrie (eth0 egress vs. ingress) | **~0,3–0,5 ms** | bridge_delay |

> Kurzfazit vorweg: Ja, Konvergenz ist möglich und sinnvoll — aber **nicht durch
> reines Mitteln**. Der Gewinn durch Mittelung trifft auf zwei harte Böden: die
> **Oszillator-Drift** (erfordert Frequenz-Disziplinierung, nicht nur Positions-
> Mittelung) und die **systematische Pfad-Asymmetrie** (lässt sich *nicht*
> wegmitteln). Realistisch erreichbar auf dieser Hardware: **~10er-µs Präzision**,
> und mit einmaliger Asymmetrie-Kalibrierung auch **~10er-µs Richtigkeit**. Im reinen
> **Embedded-Master/Slave-Fall** (Abschnitt 9) hebt sich der Asymmetrie-Bias
> *slave↔slave* weg → dort sind sogar **einstellige µs** drin. Der größte
> Hardware-Hebel ist der **Taktgeber** (Abschnitt 3.2): weg vom internen RC, hin zu
> Quarz/TCXO/MEMS. **Sub-µs bleibt ohne Hardware-Zeitstempelung (PTP) unerreichbar.**

---

## 1. Das Messmodell — was ein NTP-Austausch liefert

Pro Austausch (Knoten A fragt, Knoten B antwortet):

```
offset  θ = ((t2 − t1) + (t3 − t4)) / 2      # Uhr_B − Uhr_A
delay   δ = (t4 − t1) − (t3 − t2)            # Round-Trip-Transit
```

![NTP-Austausch: t1/t2/t3/t4-Quartett](img/ntp_exchange.png)

Der **fundamentale NTP-Trick**: θ ist exakt richtig, **wenn Hin- und Rückweg gleich
lang sind**. Ist der Hinweg `d→` und der Rückweg `d←`, dann gilt:

```
θ_gemessen = θ_wahr + (d→ − d←) / 2
```

Der Term `(d→ − d←)/2` ist die **halbe Pfad-Asymmetrie**. Das ist der Kern der
ganzen Betrachtung:

- **Zufälliger Anteil** von δ (Jitter, Contention, Scheduling) ist mittelwertfrei →
  **mittelt sich weg**.
- **Systematischer Anteil** (konstante Asymmetrie der Verarbeitung) ist **kein**
  Rauschen → **mittelt sich NICHT weg**. Er ist ein Bias, der als Boden stehen bleibt.

NTP gibt deshalb auch eine ehrliche Fehlerschranke an: weil die wahre Asymmetrie
zwischen 0 und δ liegen kann, ist der Offset-Fehler **garantiert ≤ δ/2** beschränkt.
Bei δ_min ≈ 0,5 ms ist das eine Worst-Case-Schranke von **±250 µs** — selbst wenn der
Zufallsanteil schon perfekt wegmittelt ist.

---

## 2. Zwei Fehlerklassen — und nur eine ist mittelbar

| Fehlerklasse | Ursache | Verhalten bei Mittelung über N Samples |
|---|---|---|
| **Zufällig** (white phase noise) | Zeitstempel-Jitter, PLCA-Contention, Scheduling | sinkt mit **1/√N** |
| **Drift** (frequency/random walk) | Oszillator läuft schnell/langsam, T-abhängig | mittelt **nicht** weg — muss *modelliert* (Frequenz) werden |
| **Systematisch** (Asymmetrie-Bias) | konstant ungleiche Hin-/Rückverarbeitung | mittelt **nicht** weg — bleibt als Boden |

Reines Mitteln des Offsets adressiert **nur die erste Zeile**. Die anderen beiden
sind der Grund, warum „immer genauer im Laufe der Zeit" eine Grenze hat.

---

## 3. Der Drift-Killer: warum Mitteln allein nicht reicht

Die MCU-Oszillatoren hier driften **~1600 ppm**. Zwischen zwei Syncs (250 ms) läuft
die Uhr also um

```
1600 ppm × 250 ms = 400 µs
```

weg. Wer nur **die Position** korrigiert (Offset-Sprung alle 250 ms) und dazwischen
„mittelt", erzeugt einen **Sägezahn** mit ~400 µs Spitze / ~200 µs Mittel. Dieser
Sägezahn ist *deterministisch* — Mittelung über viele Samples senkt ihn **nicht**,
weil zwischen den Samples die Uhr ja weiterdriftet.

![Drift-Sägezahn vs. Frequenz-Disziplinierung](img/ntp_sawtooth.png)

*(obere Linie: nur Positions-Korrektur — der Drift-Sägezahn, den Mitteln nicht
beseitigt; untere Linie: mit Frequenzregelung bleibt der Fehler praktisch bei 0.)*

**Lösung: Frequenz-Disziplinierung.** Man schätzt nicht nur den Offset, sondern auch
die **Frequenzabweichung** und korrigiert die Tick-Rate (bzw. addiert eine laufende
Rate auf `s_offset_ns`). Das ist ein **Zwei-Zustands-Schätzer** (Offset *und* Drift)
— ein PI-Regler / eine PLL / ein kleiner Kalman-Filter. Sobald die Frequenz auf z. B.
**±1 ppm** eingeregelt ist, beträgt der Holdover-Fehler über 250 ms nur noch

```
1 ppm × 250 ms = 0,25 µs   → vernachlässigbar.
```

Erst **mit** Frequenzregelung wird der verbleibende Positionsfehler so klein, dass
das Mitteln des Zufallsanteils überhaupt sichtbar wird. Genau das macht „echtes" NTP
(und PTP) — die `est. drift`-Anzeige des `ntp`-Kommandos liefert dafür bereits den
Rohwert (`drift ≈ −adjust / interval`).

### 3.1 „Drift vorhersagen" ist der größte Hebel — aber nur der *veränderliche* Teil zählt

Das ist genau der Punkt: Wer die Frequenzabweichung kennt/vorhersagen kann, korrigiert
sie **vorausschauend** und nicht erst nachträglich. Wichtig ist aber die Trennung:

- **Konstanter Frequenz-Offset** — egal wie groß — ist **trivial lernbar**: der
  PI-Regler misst ihn einmal und kompensiert ihn dauerhaft. Die in diesem Projekt
  gemessenen **~1600 ppm** sind so ein konstanter Offset (und ein Warnzeichen, siehe
  3.2) — sie *allein* begrenzen die Genauigkeit **nicht**.
- **Veränderlicher Anteil** — die *Änderung* der Frequenz zwischen zwei Syncs durch
  **Temperatur** (`df/dT · dT/dt`), Alterung und Rauschen — ist das, was Holdover und
  Regelschleife wirklich begrenzt. Nur dieser Teil zählt.

„Drift vorhersagbar machen" heißt also dreierlei: (a) einen **stabilen, charakterisierten
Oszillator** verwenden, (b) **Temperatur messen** und den bekannten `df/dT`-Gang
**vorsteuern** (Feed-forward), (c) den langsam veränderlichen Rest vom **PI-Regler
lernen** lassen.

### 3.2 Taktgeber: RC vs. Quarz vs. TCXO vs. MEMS

Wie stark der veränderliche Anteil ausfällt, entscheidet die **Taktquelle** des
Zeitzählers:

| Taktquelle | Toleranz / Offset | Temperaturgang | Anmerkung |
|---|---|---|---|
| **Interner RC / DFLL** (MCU) | ±1–5 % (10⁴–5·10⁴ ppm), geregelt ~0,1 % | stark, ~10er ppm/°C | **= unsere ~1600 ppm** — kein Quarz! |
| **MHz-Quarz** (AT-cut) | ±10–30 ppm initial | ±10–50 ppm über −40…+85 °C (kubisch) | Standard neben fast jedem MCU |
| **32,768-kHz-Uhrenquarz** | ±20 ppm | parabolisch, ~−0,034 ppm/°C² → ~−20 ppm bei ±25 °C vom Scheitel | RTC-typisch |
| **TCXO** (temp.-kompensiert) | ±0,5–2 ppm über Temperatur | sehr flach | Quarz + interne Kompensation |
| **MEMS** (Standard) | ±1–5 ppm | flach | robust, schneller Start |
| **MEMS-TCXO** (z. B. SiTime) | **±0,1 ppm** | sehr flach | beste praktikable Wahl, schock-/vibrationsfest |
| **OCXO** (beheizt) | ±0,01–0,1 ppm | minimal | Strom/Platz — meist überzogen |

**Wichtigster Hardware-Hebel auf dieser Plattform:** Den SYS\_TIME-Zähler **nicht vom
internen RC/DFLL** takten (das sind die ~1600 ppm), sondern vom **externen Quarz**
bzw. einem **TCXO/MEMS**. Das drückt die Drift von ~1600 ppm auf **zehner-ppm (Quarz)**
bzw. **~1 ppm (TCXO/MEMS)** — und macht damit den temperaturinduzierten Drift-*Ramp*
um Größenordnungen kleiner *und* besser vorhersagbar.

**Bringen MEMS-Oszillatoren bessere Konvergenz?** Differenziert:

- **Konvergenz-*Geschwindigkeit*** — nein, die hängt an Loop-Bandbreite und Sync-Rate,
  nicht am Oszillator.
- **Absoluter µs-*Boden* bei häufigem Sync (250 ms)** — nein, den setzt der
  **SPI-Stempel-Jitter** (Abschnitt 7), nicht der Oszillator. Ein besserer Takt senkt
  den µs-Boden also *nicht*.
- **Robustheit & Holdover** — **ja, deutlich.** Ein TCXO/MEMS (besonders MEMS-TCXO mit
  ±0,1 ppm) ist gegen **Temperatur-Transienten** (Lüfter, Last, Sonne) viel
  unempfindlicher → der `|Δf|·t`-Holdover-Term in Λ (Abschnitt 6) schrumpft, der Regler
  muss weniger nachführen, und man kann den **Sync seltener** machen (alle paar Sekunden
  statt 250 ms) und trotzdem µs halten. MEMS zusätzlich: schock-/vibrationsfest,
  schneller Start, kein Quarz-Layout.

**Fazit Taktgeber:** MEMS/TCXO verbessern **Vorhersagbarkeit, Robustheit und Holdover**
— nicht den absoluten µs-Boden. Für ständigen 250-ms-Sync auf einem ruhigen Strang
genügt ein ordentlicher **Quarz**; **TCXO/MEMS** lohnen, wenn die Temperatur stark
schwankt, Vibration herrscht oder man den Sync-Verkehr reduzieren will. Der mit Abstand
größte Sprung ist aber zuerst: **weg vom internen RC, hin zu irgendeinem Quarz.**

---

## 4. Konvergenz-Mathematik

### 4.1 Zufallsanteil: 1/√N — bis zum Plateau

Mit Frequenzregelung bleibt ein mittelwertfreier Jitter σ_θ pro Sample. Der gemittelte
Schätzer hat den Standardfehler

```
SE(N) = σ_θ / √N
```

Bei 250 ms Kadenz sind das **240 Samples/Minute**. Über 1 min → √240 ≈ **15×**, über
10 min → ≈ **49×** Reduktion des Zufallsanteils. **Aber:** das läuft gegen den
**Asymmetrie-Boden** (Abschnitt 1). Sobald `σ_θ/√N` unter `(d→−d←)/2` fällt, bringt
weiteres Mitteln nichts mehr — die Kurve **plateaut**. Praktisch ist dieses Plateau
nach **Sekunden bis wenigen Minuten** erreicht; „beliebig genau mit der Zeit" gibt es
nicht.

![Konvergenz σ/√N mit Plateau](img/ntp_convergence.png)

*(fallende Linie: σ/√N (hier σ ≈ 200 µs); flache Linie: der nicht mittelbare Boden
aus Asymmetrie/Transit. Der effektive Fehler folgt dem Maximum beider → die
Konvergenz **plateaut**, sobald σ/√N den Boden erreicht.)*

### 4.2 Optimale Mittelungsdauer (Allan-Deviation)

Es gibt ein **Optimum** für die Mittelungs-/Sync-Dauer τ:

- **τ zu kurz** → Zeitstempel-Jitter (white phase noise) dominiert → Mitteln hilft.
- **τ zu lang** → Oszillator-Random-Walk/Drift dominiert → Mitteln **schadet**.

Das Minimum der **Allan-Deviation** σ_y(τ) der MCU-Uhr markiert das τ, bei dem beide
sich die Waage halten. Für einen einfachen MCU-Quarz liegt es typisch im Bereich
**~1–10 s**. 250 ms liegt sicher im white-noise-Bereich → es ist richtig, mehrere
Samples zu kombinieren, um in die Allan-Senke zu kommen.

### 4.3 Symmetrische Variante: Konsens statt Master/Slave

Mit dem PC war es Master/Slave (FW folgt PC). Zwischen zwei **gleichberechtigten**
Knoten ist die **Konsens-Mittelung** eleganter und „konvergierend" im Wortsinn:
jeder Knoten korrigiert nur die **Hälfte** des gemessenen Offsets in Richtung des
anderen.

```
A: offset_A += +θ/2          B: offset_B += −θ/2
```

Beide laufen auf eine gemeinsame **virtuelle Mittenzeit** zu — bei zwei Knoten
konvergiert das geometrisch (Faktor ½ pro Runde, d. h. nach wenigen Runden praktisch
zusammen). Das mittelt die **unabhängigen** Uhrfehler beider Seiten heraus (Vorteil
gegenüber „einer folgt einem"). Der gemeinsame Bias durch Asymmetrie bleibt natürlich.

![Konsens: beide Knoten zur Mittenzeit](img/ntp_consensus.png)

*(Knoten A und B korrigieren je die Hälfte des gemessenen Offsets → der Abstand
halbiert sich pro Runde und beide treffen sich in der Mitte. Auf eine externe
Referenz folgt das nicht — es ist gegenseitige, relative Synchronisation.)*

**Bonus — Asymmetrie wird sichtbar:** misst A den Offset und B unabhängig auch (jeder
als Anfrager), müssten beide betragsgleich/vorzeichenverkehrt sein. Die **Differenz**
der beiden unabhängigen Schätzungen ist ein direktes Maß der Verarbeitungs-Asymmetrie
— womit man sie sogar **kalibrieren** und den Bias herausrechnen kann (analog zur
Capture-Clock-Kalibrierung in [NTP_TIMING.md](NTP_TIMING.md) §5).

---

## 5. Ausreißer-Filterung auf dem T1S-Strang

Auf einem **eigenen, ruhigen** T1S-Strang ist δ weitgehend konstant — genau wie der
Nutzer vermutet. Variationen kommen v. a. von:

- **PLCA-Sendeslot:** ein Knoten muss auf seine Transmit-Opportunity im Beacon-Zyklus
  warten → **quantisierter** Zusatz-Delay bis zur Zykluszeit. Das ist die größte
  Jitter- *und* Asymmetrie-Quelle.
- **SPI-Anbindung der MAC-PHY (LAN8651):** RX-Indication über SPI aus dem Superloop →
  Latenz und Asymmetrie zwischen TX- und RX-Stempelpunkt.

Bewährte Gegenmittel (klassisches NTP macht genau das):

1. **Minimum-Delay-Filter:** im Fenster nur das Sample mit dem **kleinsten δ**
   verwenden (das hat am wenigsten Contention erlitten und ist am symmetrischsten).
   Der Host-Tool tut das bereits.
2. **Robuste Schätzer** statt arithmetischem Mittel: Median / getrimmtes Mittel /
   Huber über die Offsets der δ-kleinsten Samples.
3. **Schwellwert-Verwurf:** Samples mit δ > δ_min + Schwelle fallen lassen.

Auf einem dedizierten Strang ist δ_min damit sehr stabil → konstante Asymmetrie →
**kalibrierbar** (Abschnitt 4.3).

---

## 6. Qualitätsmaß, das jeder Knoten lokal berechnen kann

Jeder Knoten kann seine **Synchronisations-Güte** aus der eigenen Statistik schätzen
— ohne externe Referenz. Sinnvolle Größen (alle aus den vorhandenen t1..t4 + dem
laufenden Schätzer ableitbar):

| Metrik | Berechnung | Aussage |
|---|---|---|
| `δ_min` | Minimum über Fenster | beste erreichte Schranke (Fehler ≤ δ_min/2) |
| δ-Spread | `δ_p90 − δ_p10` | Contention/Last; klein = vertrauenswürdig |
| `σ_θ` | Streuung der Offsets im Fenster | Zufallsanteil pro Sample |
| `SE = σ_θ/√N` | Standardfehler des Mittels | wie weit das Mitteln noch trägt |
| Frequenz-Stabilität | Streuung aufeinanderfolgender Drift-Schätzungen (≈ Allan-Dev) | Holdover-Güte zwischen Syncs |

Daraus lässt sich eine **NTP-artige „synchronization distance"** Λ bilden — die
ehrliche obere Fehlerschranke, die der Knoten selbst meldet:

```
Λ  ≈  δ_min/2                # irreduzible Transit-Unsicherheit (Asymmetrie unbekannt)
    +  σ_θ/√N                # verbleibender Zufallsanteil
    +  |Δf| · t_seit_sync    # Holdover-Drift seit dem letzten Sync
```

Der dritte Term ist genau das, was das erweiterte `ntp`-Kommando heute schon anzeigt
(`est. dev.`). Ein Knoten kann damit zur Laufzeit sagen: *„Ich bin auf ±Λ genau" —*
und Λ schrumpft sichtbar, während die Konvergenz läuft, bis sie auf δ_min/2 (dem
Asymmetrie-/Transit-Boden) aufsetzt.

---

## 7. Was ist erreichbar — Genauigkeit & Zuverlässigkeit

| Ausbaustufe | erwartetes Niveau (diese HW) | begrenzt durch |
|---|---|---|
| nur Positions-Sprung + Mitteln | **±200–400 µs** | Drift-Sägezahn (Abschnitt 3) |
| + Frequenz-Disziplinierung (PI/PLL) | **~10–50 µs** (Präzision) | Software-Zeitstempel-Jitter, SPI-RX-Latenz |
| + Minimum-Delay-Filter, ruhiger Strang | **~10 µs** (Wiederholbarkeit) | PLCA-Quantisierung, Superloop-Kadenz |
| + einmalige Asymmetrie-Kalibrierung | **~10er µs** auch in der *Richtigkeit* | Reststreuung der Kalibrierung |
| Hardware-Zeitstempelung (PTP/802.1AS) | **sub-µs … ns** | — (anderes Verfahren) |

**Präzision (Wiederholbarkeit):** beide Knoten *zueinander* stabil im **10er-µs**-
Bereich — gut machbar, sobald die Frequenz geregelt ist.

**Richtigkeit (Trueness):** durch die Asymmetrie zunächst auf **δ_min/2 ≈ 100er µs**
beschränkt; mit Kalibrierung (Abschnitt 4.3) ebenfalls in den **10er-µs**-Bereich.

**Harte Decke:** Die Stempel entstehen in Software (Superloop/Hook), die MAC-PHY
hängt am SPI. Der **16-ns-Tick ist NICHT die Grenze** — die Stempel-Latenz und ihr
Jitter sind es. Für **sub-µs** braucht es Zeitstempelung in der MAC/PHY-Hardware
(PTP / IEEE 802.1AS), was genau das Schwesterprojekt `net_10base_t1s` macht.

**Zuverlässigkeit:** Auf einem dedizierten, gering belasteten Strang ist das Verfahren
robust — δ ist stabil, Ausreißer sind selten und filterbar, und jeder Knoten kennt
über Λ seine eigene Güte (kann bei wachsendem δ-Spread die Schranke aufweiten oder in
**Holdover** auf der disziplinierten Frequenz gehen). Es ist allerdings **relative**
Synchronisation (die beiden stimmen *miteinander* überein), **nicht** UTC-rückführbar
— ohne externe Referenz gibt es keine absolute Zeit.

---

## 8. Was damit möglich wäre — und was nicht

**Möglich (im ~10er-µs-Regime):**
- **Koordinierte Aktionen** zwischen zwei T1S-Knoten: gemeinsam getriggerte GPIO-/
  Sensor-Sampling-Zeitpunkte, abgestimmte Mess-Fenster.
- **Ereignis-Korrelation** über Knoten hinweg auf einer gemeinsamen Zeitachse
  (Logs/Captures beider Boards verschmelzen, kausale Reihenfolge sicher bestimmen).
- **Selbst-attestierte Güte:** jeder Knoten meldet sein Λ → das System weiß, *wie sehr*
  es der gemeinsamen Zeit gerade trauen darf.

**Nicht möglich (ohne Hardware-Zeitstempelung):**
- Harte, deterministische Trigger im **sub-µs**-Bereich.
- UTC-/absolut-rückführbare Zeit ohne externe Referenzuhr.
- „Beliebig genau, je länger es läuft" — die Konvergenz **plateaut** am Asymmetrie-/
  Transit-Boden; jenseits davon hilft nur Kalibrierung oder ein anderes Verfahren.

---

## 9. Bester Fall: nur Embedded-Knoten, Master/Slave auf einem Strang

Nimmt man an, dass **ausschließlich gleichartige T1S-MCU-Knoten** auf **einem** Strang
synchronisiert werden — einer ist **Master**, die übrigen **Slaves** — fällt fast
alles zugunsten der Genauigkeit aus. Das ist der günstigste Fall für reinen
Software-Sync. Drei Gründe:

**(a) Identische Hardware → symmetrische Pfade.** Master und Slaves sind dieselbe MCU
+ dieselbe SPI-MAC-PHY (LAN8651) + derselbe Stack. Der Asymmetrie-Bias `(d→−d←)/2`,
der beim PC↔Bridge-Fall noch ~0,3–0,5 ms betrug (NIC ↔ SPI-MAC-PHY), ist hier viel
kleiner — und **bei allen Slaves praktisch gleich**.

**(b) Beide Enden stempeln leitungsnah.** Die ~150–360 µs Residuum des PC-Aufbaus
kamen großteils vom **PC-Stempel** (Userland/Winsock, Npcap). Embedded↔embedded
stempeln **beide** im Paket-Hook / MAC-ISR → die größte Jitter-Quelle entfällt.

**(c) Der gemeinsame Bias kürzt sich slave↔slave weg.** Man muss trennen, *was* man
genau will:

![Fehlerbudget: Slave↔Master vs. Slave↔Slave](img/ntp_bias_cancel.png)

- **Slave ↔ Master** (gegen die Referenz): Bias `b` + Zufalls-Rest → ~10–50 µs.
- **Slave ↔ Slave** (zwei Slaves untereinander): jeder trägt **denselben** Bias `b`,
  die Differenz ist `(b+rnd_A) − (b+rnd_B) = rnd_A − rnd_B` — **`b` fällt heraus**, übrig
  bleibt nur der mittelbare Zufalls-Rest. Für „zwei Knoten lösen *gleichzeitig* etwas
  aus" ist genau das die relevante Größe — und der Sweet Spot.

Die **Master-Drift ist common-mode**: alle Slaves folgen ihr, also ist sie für die
Synchronität *zwischen* den Knoten egal — nur der Slave-Drift-Rest zwischen zwei Syncs
zählt (→ Frequenzregelung, Abschnitt 3).

### 9.1 Noch besser: Broadcast-One-Way statt Einzel-Round-Trips

Weil es *einen* Master gibt, lässt sich das NTP-Round-Trip durch ein **PTP-artiges
Einweg-Schema in Software** ersetzen:

![Master/Slave Broadcast-One-Way-Sync](img/ntp_master_slave.png)

1. Master sendet eine **Broadcast-Sync** und stempelt seinen **TX-Zeitpunkt** (im Hook).
2. Jeder Slave stempelt den **RX-Zeitpunkt** (im Hook).
3. Master sendet ein **Follow-up** mit seinem TX-Stempel.
4. `Offset_Slave = (Master_TX + t_Kabel) − Slave_RX`.

Die Leitungslaufzeit `t_Kabel` auf ein paar Metern T1S ist **~ns** und für alle Slaves
nahezu gleich. Damit hängt das **slave↔slave**-Ergebnis *nur noch am RX-Stempel-Jitter*
— die Asymmetrie ist **komplett raus**, weil sich alle Slaves auf **dasselbe eine
Master-TX-Ereignis** beziehen. Das ist die softwareseitig beste Variante; sie nutzt,
dass die Bridge ohnehin **PLCA-Koordinator (Node 0)** und damit der natürliche Master
ist.

### 9.2 Erreichbar in diesem Fall

| Beziehung | realistisch (Software) | begrenzt durch |
|---|---|---|
| Slave ↔ Master | ~10–50 µs | SPI-Stempel-Jitter + Rest-Asymmetrie |
| Slave ↔ Slave (Round-Trip-NTP) | niedrige zweistellige µs | Zufalls-Rest (Bias gekürzt) |
| **Slave ↔ Slave (Broadcast-One-Way)** | **potenziell < 10 µs** | nur RX-Stempel-Jitter |

Einstellige µs sind die softwareseitige Schallmauer (SPI-Indication-Jitter); alles
darunter braucht Hardware-Zeitstempelung (PTP/802.1AS, `net_10base_t1s`).

---

## 10. Fazit & ein konkreter Algorithmus-Vorschlag

Konvergierende Zwei-Knoten-Synchronisation per Software-NTP auf einem T1S-Strang ist
**machbar und sinnvoll** — wenn man drei Dinge kombiniert:

1. **Frequenz-Disziplinierung** (PI/PLL über Offset *und* Drift) — beseitigt den
   dominierenden Drift-Sägezahn. **Wichtigster Hebel.**
2. **Minimum-Delay-Filter + robuster Offset-Schätzer** — macht δ und damit die
   Asymmetrie konstant und filtert PLCA-/SPI-Ausreißer.
3. **Konsens-Korrektur (je ½ zur Mitte)** + **Λ-Gütemaß pro Knoten** — echte
   gegenseitige Konvergenz mit Selbstbewertung; die Differenz der wechselseitigen
   Offset-Schätzungen kalibriert die Restasymmetrie.

![Regelkreis: Austausch → Filter → PI → Uhr → Güte](img/ntp_loop.png)

Skizze einer Iteration (alle 250 ms, beide Knoten symmetrisch):

```
1. NTP-Austausch -> (θ_i, δ_i)
2. Fenster der letzten K Samples: nimm die mit δ nahe δ_min, verwirf Ausreißer
3. θ* = robustes Mittel dieser Offsets
4. PI-Regler: Frequenzkorrektur f += Ki·θ*,  Positionskorrektur += Kp·θ*·(1/2)
   (Faktor ½ = Konsens zur Mitte; der Partner macht die andere Hälfte)
5. aktualisiere Λ = δ_min/2 + σ_θ/√N + |Δf|·t_seit_sync   -> melde Güte
```

Damit landet man realistisch bei **~10er µs** zueinander, stabil und mit
mitlaufender, ehrlicher Fehlerschranke. Für alles darunter (sub-µs, ns) führt der Weg
über **Hardware-Zeitstempelung / PTP** — siehe `net_10base_t1s`.

> **Abgrenzung:** Dieses Dokument ist eine *theoretische* Betrachtung. Implementiert
> ist heute der Master/Slave-Sync gegen den PC ([NTP_TIMING.md](NTP_TIMING.md)); der
> hier skizzierte Zwei-Knoten-Konsens-Regler ist ein möglicher Ausbau, kein
> vorhandenes Feature.

---

*Die Diagramme (`img/ntp_*.png`) werden mit matplotlib aus
[`ntp_convergence_diagrams.py`](ntp_convergence_diagrams.py) erzeugt
(`pip install matplotlib && python ntp_convergence_diagrams.py`); die Zahlen sind
illustrativ und entsprechen den im Text genannten Werten.*
