#!/usr/bin/env python3
"""Render the HW-timebase signal-level timing diagram (doc HW_TIMEBASE_B_C_IMPLEMENTATION.md, 8.4).

Generates hw-timebase-timing.svg from the WaveJSON below using the `wavedrom` module.
    pip install wavedrom
    python render_timing.py
"""
import os
import wavedrom

WAVEJSON = r"""
{ "signal": [
  { "name": "GCLK_TC2 (96 MHz)",       "wave": "P..........." },
  { "name": "TC2.COUNT",               "wave": "=.=.=.=.=.=.", "data": ["n", "n+1", "n+2", "CC0=T", "T+1", "T+2"] },
  { "name": "CC0 = ticks(T)",          "wave": "=...........", "data": ["T (konstant geladen)"] },
  {},
  { "name": "COUNT==CC0 (Match)",      "wave": "0.....10...." },
  { "name": "MC0-Event (EVSYS async)", "wave": "0.....10...." },
  { "name": "ADC START / S&H",         "wave": "0.....10...." },
  { "name": "GPIO (PORT toggle)",      "wave": "0.....1....." },
  { "name": "PWM (TCC, syntonisiert)", "wave": "hhhlllhhhlll" }
],
  "config": { "hscale": 2 },
  "head": { "text": "NTP-Hardware-Uhr treibt Peripherie zum Instant T  (1 Spalte ~ 1 TC-Takt ~ 10,4 ns)" },
  "foot": { "text": "Trigger-Jitter = EVSYS-async (~ns) + 1 TC-Takt-Quantisierung (~10 ns)  <<  10-100 us Ziel" }
}
"""

if __name__ == "__main__":
    here = os.path.dirname(os.path.abspath(__file__))
    out = os.path.join(here, "hw-timebase-timing.svg")
    svg = wavedrom.render(WAVEJSON)
    svg.saveas(out)
    print("written:", out)
