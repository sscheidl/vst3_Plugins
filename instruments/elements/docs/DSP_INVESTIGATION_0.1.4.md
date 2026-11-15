# Hohe Töne, Sättigung und möglicher Aliasanteil

Datum: 2026-11-15. Untersuchung durch Codex; unabhängige Prüfung steht aus.

Ausgangspunkt ist der DAW-Hörtest des Nutzers: hohe Töne verzerren abhängig
von Geometry, besonders bei fast ungedämpftem Resonator. Der Klang darf
seinen Charakter behalten. Deshalb wurden **keine DSP-Korrekturen** vorgenommen.

## Bereits vorhandene Begrenzung

1. `core/elements/dsp/part.cc`: `SoftLimit` vor dem Hall auf beiden Kanälen.
2. `engine/engine.cpp`: nach dem Hall halber Pegel, `SoftLimit`, Begrenzung
   auf ±1, **danach** Volume. Dies bildet die Ausgangskennlinie der Hardware
   ohne deren Rundung auf 16 Bit nach.
3. `source/processor.cpp`: nach der Ratenwandlung nochmals Begrenzung auf ±1.
   Bei 48-kHz-Hostrate wird diese Wandlung übersprungen.

`SoftLimit(x) = x * (27 + x²) / (27 + 9x²)` ist eine statische Kennlinie.
Das ist kein Dynamik-Limiter mit Attack/Release oder Lookahead. Volume kann
bereits erzeugte Sättigung nicht rückgängig machen. Neben diesen Stufen
enthalten auch die Anreger nichtlineare Verarbeitung.

## Befund und Grenzen

Die neue Release-Messung in `tests/test_dsp.cpp::MeasureHighNotes` prüft
24 Kombinationen aus MIDI 84/96, drei Geometry-Werten, zwei Damping-Werten
und Strike bzw. gemischten Anregern, jeweils ohne Hall. Die konkrete
Nutzereinstellung lag nicht als Patch vor.

* Alle berechneten Samples bleiben endlich. Im rechten Kanal der 24 Fälle
  erreicht kein Sample die harte Begrenzung der **äußeren** Ausgangsstufe.
  Daraus folgt nichts über die linke Seite oder andere/hallreiche Patches.
* Diese äußere Stufe ändert die Gesamtenergie gegenüber halbem DSP-Pegel
  um bis zu etwa −0,30 dB. Sättigung **innerhalb** des Kerns bleibt darin
  bereits enthalten und wurde nicht umgangen oder isoliert gemessen.
* Bei kaum gedämpften gemischten Anregern weichen die Nachklangpegel zwischen
  48 und 96 kHz bei MIDI 96 um etwa 6,7 bis 7,3 dB ab. Das ist ein Befund
  zur Ratenabhängigkeit; **kein Beweis für Aliasing als Ursache**. Der Kern
  bei 96 kHz bleibt ohnehin experimentell; das Plugin rechnet weiterhin
  intern bei 48 kHz, auch wenn der Host 96 kHz verwendet.
* Der isolierte Test nur der äußeren SoftLimit-Kennlinie erzeugt aus einem
  10-kHz-Sinus (Spitzenwert 0,8 vor dem Halbpegel) bei 48 kHz eine
  18-kHz-Komponente mit −47,05 dBFS. Die dritte Harmonische bei 30 kHz wird
  auf 18 kHz gefaltet. Bei 192 kHz liegt die 18-kHz-Komponente im selben
  Test bei −182,84 dBFS. Der Test benötigt keine Änderung des Produkts und
  weist den möglichen Aliasmechanismus dieser vorhandenen Stufe nach.

Damit ist Aliasing technisch plausibel und für die isolierte Kennlinie
nachgewiesen. Welche Anteile des gehörten Instruments von der Modenstruktur,
den Anregern, interner Sättigung oder Aliasing kommen, bleibt offen.
Inharmonische Teiltöne allein sind kein Aliasing-Nachweis.

Für eine gezielte Fortsetzung den auffälligen Patch mit MIDI-Note, Geometry,
Brightness, Damping und Hostrate speichern; dieselbe Anregung vergleichen,
Anregerpegel vor der Sättigung variieren und die Spektren beurteilen. Eine
eventuelle Qualitätsoption erst nach Hörvergleich planen. Keine automatische
Entfernung charaktergebender Artefakte.

## Reproduktion

Release-Target `test_elements_dsp` bauen, dann
`ctest --preset windows-vs2026-x64-release -R "^elements\.dsp$"` ausführen.
Die Tabelle unten stammt aus dem Messbericht
`build/windows-vs2026-x64/instruments/elements/dsp-measurements-Release.md`.
Die Nachklangspalten sind RMS von `core * 0.5` zwischen 1 und 2 Sekunden,
**vor** der äußeren Ausgangskennlinie; die anderen Ausgangskennzahlen und
das 16–24-kHz-Band liegen **danach**. Debug überspringt diese Messreihen.


### High-note investigation (0.1.4, no DSP changes)

48 kHz core, dry, brightness 0.9, strike timbre 0.9, strength 0.5.
Mixed adds bow/blow at 0.7 and holds the gate for 1 s; strike uses 10 ms.
Damping here is the LEGACY DSP value: 0.9995 means almost no damping.
Core peak is AFTER the core's pre-reverb SoftLimit. Output applies the
existing engine stage: clamp(SoftLimit(core * 0.5), -1, 1), at Volume 1.
Stage change is output energy relative to core * 0.5, in dB.
This sweep cannot distinguish inharmonic modes from aliasing by itself.

| Exciters | MIDI | Geometry | Legacy damping | Core peak R | Output peak R | Stage change dB | Hard-clipped samples R % | Tail RMS R dBFS | Compare 96k tail dBFS | 48k 16-24k dBFS |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| strike | 84 | 0.000 | 0.2500 | 0.994 | 0.463 | -0.30 | 0.0000 | -189.83 | -208.50 | -57.49 |
| strike | 84 | 0.000 | 0.9995 | 0.995 | 0.464 | -0.08 | 0.0000 | -24.71 | -24.79 | -52.58 |
| strike | 84 | 0.500 | 0.2500 | 0.260 | 0.130 | -0.01 | 0.0000 | -90.44 | -152.43 | -96.06 |
| strike | 84 | 0.500 | 0.9995 | 0.310 | 0.154 | -0.01 | 0.0000 | -28.34 | -28.42 | -79.88 |
| strike | 84 | 0.999 | 0.2500 | 0.070 | 0.035 | -0.00 | 0.0000 | -151.66 | -167.70 | -116.99 |
| strike | 84 | 0.999 | 0.9995 | 0.075 | 0.037 | -0.00 | 0.0000 | -38.16 | -38.22 | -115.40 |
| strike | 96 | 0.000 | 0.2500 | 0.405 | 0.200 | -0.02 | 0.0000 | -89.60 | -179.92 | -83.13 |
| strike | 96 | 0.000 | 0.9995 | 0.404 | 0.200 | -0.01 | 0.0000 | -28.30 | -28.35 | -75.96 |
| strike | 96 | 0.500 | 0.2500 | 0.175 | 0.087 | -0.00 | 0.0000 | -135.23 | -200.93 | -103.27 |
| strike | 96 | 0.500 | 0.9995 | 0.182 | 0.091 | -0.01 | 0.0000 | -31.53 | -31.58 | -89.47 |
| strike | 96 | 0.999 | 0.2500 | 0.073 | 0.037 | -0.00 | 0.0000 | -202.65 | -212.22 | -115.40 |
| strike | 96 | 0.999 | 0.9995 | 0.075 | 0.037 | -0.00 | 0.0000 | -36.11 | -36.13 | -112.35 |
| mixed | 84 | 0.000 | 0.2500 | 0.993 | 0.463 | -0.04 | 0.0000 | -31.89 | -32.59 | -58.03 |
| mixed | 84 | 0.000 | 0.9995 | 0.995 | 0.464 | -0.20 | 0.0000 | -13.95 | -15.56 | -49.45 |
| mixed | 84 | 0.500 | 0.2500 | 0.301 | 0.149 | -0.01 | 0.0000 | -31.66 | -30.33 | -86.61 |
| mixed | 84 | 0.500 | 0.9995 | 0.862 | 0.409 | -0.14 | 0.0000 | -15.94 | -14.29 | -57.60 |
| mixed | 84 | 0.999 | 0.2500 | 0.234 | 0.116 | -0.01 | 0.0000 | -31.29 | -30.37 | -105.65 |
| mixed | 84 | 0.999 | 0.9995 | 0.640 | 0.311 | -0.09 | 0.0000 | -17.61 | -20.24 | -99.92 |
| mixed | 96 | 0.000 | 0.2500 | 0.405 | 0.200 | -0.02 | 0.0000 | -28.09 | -31.99 | -71.35 |
| mixed | 96 | 0.000 | 0.9995 | 0.961 | 0.450 | -0.30 | 0.0000 | -10.20 | -16.88 | -47.12 |
| mixed | 96 | 0.500 | 0.2500 | 0.363 | 0.180 | -0.02 | 0.0000 | -28.12 | -32.11 | -93.41 |
| mixed | 96 | 0.500 | 0.9995 | 0.913 | 0.430 | -0.30 | 0.0000 | -10.22 | -17.31 | -70.29 |
| mixed | 96 | 0.999 | 0.2500 | 0.359 | 0.178 | -0.02 | 0.0000 | -28.14 | -32.12 | -101.41 |
| mixed | 96 | 0.999 | 0.9995 | 0.901 | 0.425 | -0.29 | 0.0000 | -10.22 | -17.47 | -68.09 |

Output SoftLimit only: 10 kHz sine, peak 0.8 before the half-gain stage.
At 48000 Hz: 18 kHz component -47.05 dBFS.
At 192000 Hz: 18 kHz component -182.84 dBFS.
This proves aliasing is possible in the existing output stage, not that
it caused the user's particular sound. The core has further nonlinearities.
