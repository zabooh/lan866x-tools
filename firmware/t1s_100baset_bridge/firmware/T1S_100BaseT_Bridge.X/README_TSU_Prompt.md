```markdown
## Prompt für diese Datei

Du bist ein erfahrener Embedded‑/PTP‑Ingenieur und hilfst mir, die **grundsätzliche Funktionalität des Timestamping/TSU‑PTP‑Supports des LAN8651** zu verstehen und systematisch zu validieren.  
Der Fokus liegt ausschließlich auf der **korrekten Verwendung der LAN8651‑Register** (CONFIG0, PADCTRL, STATUS0/1, IMASK0, TXMLOC, TXMMSKH/L, TXMPATH/TXMPATL, TSC‑Bits im SPI‑Header usw.), nicht auf dem SAM‑GEM.

### Ausgangsbeobachtung

- TTSCAA‑Bit (TX Timestamp Capture A) in STATUS0 wird **niemals** gesetzt.  
- TXPMDET (TX Pattern Match Detect) tritt sporadisch (~0.6) auf, d. h. der TX‑Match‑Pfad ist grundsätzlich aktiv.  
- Es gibt diverse Fixes und Tests, aber das Grundproblem „TXPMDET ok, TTSCAA nie gesetzt“ ist noch nicht erklärt und nicht gelöst.

### Theorien / mögliche Ursachen, die du aktiv prüfen und strukturieren sollst

Formuliere aus den folgenden Punkten ein **konsistentes Bild** der möglichen Ursachen und leite daraus einen **konkreten Validierungs‑/Experimentierplan** ab. Ziel: Verstehen, ob der LAN8651‑TSU prinzipiell korrekt genutzt wird und unter welchen Bedingungen TTSCAA überhaupt gesetzt werden kann.

1. **Globales Timestamp‑Enable**
   - CONFIG0.FTSE/FTSS falsch konfiguriert → Timestamping global deaktiviert oder im falschen Modus.
   - CONFIG0.TXPE/TXCTE falsch → TX‑Timestamping / TX‑Preamble‑Extension nicht aktiv.

2. **TX‑Match‑Konfiguration**
   - TXMLOC Nibble vs. Byte‑Offset falsch interpretiert.
   - TXMMSKH/TXMMSKL maskieren relevante Bits weg.
   - TXMPATH/TXMPATL passen nicht zur realen MAC/PLCA‑Topologie.

3. **Unterschied TXPMDET vs. TTSCAA**
   - TXPMDET zeigt nur Pattern‑Treffer, nicht erfolgreiche Timestamp‑Erzeugung.
   - TTSCMA/TTSCMB‑Bits (Capture missed/Overflow) könnten gesetzt sein, TTSCAA bleibt 0.
   - Timestamp landet in Slot B/C, Firmware schaut nur auf TTSCAA (Slot A).

4. **Interrupt‑ und W1C‑Handling**
   - IMASK0 maskiert TTSCAA (Bit 8) → Event kommt nie in der Firmware an.
   - STATUS0 wird durch W1C‑Zugriffe zu früh gelöscht (anderer Codepfad oder ISR).
   - OnStatus0 liest/löscht STATUS0 bevor der PTP‑Code DRVLAN865XGetAndClearTsCapture ausführt.

5. **TSC‑Bit im SPI‑Header**
   - tsc‑Feld im SPI‑Data‑Header falsch gesetzt (falscher Slot, falsches Byte, falsche Bits).
   - TSC‑Bit geht auf dem Weg TC6SendRawEthernetPacket → SPI‑Header verloren oder wird überschrieben.

6. **TX‑Fehler / PHY‑Zustand**
   - STATUS0‑Fehler (z. B. TXBUE) verhindern, dass überhaupt ein gültiger Timestamp geschrieben wird.
   - PLCA nicht sauber synchron, bestimmte Zustände blockieren TX‑Timestamping.

7. **Reset/MemMap‑Effekte**
   - Loss‑of‑Framing/Reset Complete löst MemMap‑Reinit aus, überschreibt TXMLOC/TXMMSK/TXMCTL u. a.
   - PTPGMInit wird nach einem Reset nicht erneut aufgerufen → PTP‑Konfig und MemMap sind inkonsistent.
   - CLI‑/lanread‑Zugriffe verursachen LOFR‑Resets durch ungeschützte Control‑Frames im Datenstrom.

8. **Timing / Race Conditions**
   - Reihenfolge und Timing von „TXMCTL arm“ vs. Frame‑TX vs. STATUS0/EXST‑Verarbeitung führen dazu, dass TXPMDET sichtbar ist, aber TTSCAA nie im richtigen Fenster gelesen wird.
   - TC6Service/OnStatus0 wird nicht oft genug oder zum falschen Zeitpunkt aufgerufen.

9. **Datenblatt‑ vs. Implementierungs‑Details**
   - Bestimmte Kombinationen (z. B. MACTXTSE + spezielle FTSS/FTSE‑Einstellungen) erzeugen zwar TXPMDET, aber laut Datenblatt/Errata keinen TTSCAA‑Event.
   - Missmatch zwischen CONFIG0/FTSS‑Mode und dem, was der Treiber bzgl. RTSA/Strip‑Länge erwartet.

10. **Diagnose‑Artefakte**
    - lanread‑Zugriffe zerstören durch LOFR/Reset den Zustand, während TTSCAA gemessen werden soll.
    - Logging/Debug beeinflusst nur die Beobachtung, nicht die Hardware, kann aber Korrelationen verwirren.

### Was ich von dir konkret brauche

1. **Strukturierung**
   - Sortiere diese Theorien nach Wahrscheinlichkeit und Prüfbarkeit.
   - Fasse logisch zusammen, was zusammenhört (z. B. „CONFIG0/FTSE/FTSS‑Block“, „IMASK0/W1C‑Block“, „TSC/TXMLOC‑Block“).

2. **Checkliste / Schritt‑für‑Schritt‑Plan**
   - Erstelle eine priorisierte Checkliste:  
     „Schritt 1: Lies/prüfe Register X,Y,Z mit erwarteten Werten …“  
     „Schritt 2: Führe Minimal‑Test A durch, erwarte B, logge C …“  
   - Ziel: In wenigen, klaren Experimenten entscheiden, ob der LAN8651 überhaupt jemals TTSCAA setzt.

3. **Minimal‑Hardware‑Testdesign**
   - Skizziere einen minimalen Test (gern pseudocode‑artig), der ohne großen PTP‑Stack:
     - den LAN8651 initialisiert (CONFIG0, PADCTRL, IMASK0, PLCA),
     - genau einen TX‑Frame mit gesetztem TSC‑Bit und eindeutigem Pattern sendet,
     - danach gezielt STATUS0/TTSCx liest (ohne lanread‑Nebenwirkungen),
     - und eindeutig beantwortet: „TTSCAA wird in dieser Konfiguration gesetzt: ja/nein“.

4. **Hinweise zur sauberen Nutzung der Register**
   - Erkläre zu jedem verwendeten Register kurz:
     - welche Bits für TX‑Timestamping kritisch sind,
     - welche Fallstricke es gibt (z. B. W1C, Masken, Reset‑Verhalten),
     - wie man sie im Test sinnvoll protokolliert.

Antworte in dieser README so, dass ich die resultierende Checkliste direkt als Arbeitsplan verwenden kann und einzelne Schritte in Code/Tests umsetzen kann.
```