# Umsetzung des Codex-Reviews von Kekulé 0.3.0

Review: [CODEX_REVIEW_0.3.0.md](CODEX_REVIEW_0.3.0.md), Urteil „keine
Freigabe“. Umgesetzt in 0.3.1, 2026-11-16, von Claude. 0.3.0 wurde nie
veröffentlicht oder installiert.

## Entscheidung vorab

Drei der vier Befunde (TC-R11 bis TC-R13) betreffen die Ruhe-Meldung für
Studio Ones Plugin-Nap. Statt sie einzeln zu reparieren, ist die
Ruhe-Meldung ganz entfallen. **Entscheidung des Nutzers vom 2026-11-15**, auf
meinen Vorschlag: Eine ruhende Instanz kostet rund 1 % eines Kerns (Codex
maß 2,49 µs je Quantum), mit vier spielenden Stimmen sind es 0,8 bis 1,8 %.
Nap spart also wenig, die Meldung aber war fehleranfällig und hob die
Bitgleichheit mit der Firmware in Ruhe auf. Ob Studio One 7 ein Instrument
überhaupt schlafen legt, ist nicht belegt.

## Befunde

| ID | Schwere | Ergebnis | Was geändert wurde |
| --- | --- | --- | --- |
| TC-R11 | P2 | **gegenstandslos** | `Processor::getTailSamples()` gibt wieder fest `kInfiniteTail` zurück, wie bis 0.2.2 (`source/processor.h`). Codex' Empfehlung wörtlich |
| TC-R12 | P2 | **gegenstandslos** | `Processor::AtRest()`, `at_rest_`, `keys_queued_`, `keys_taken_` und `PopKeyboardNote()` sind entfernt. Es gibt keine Ruhe-Meldung zwischen den Threads mehr, also auch keine Zeitfenster darin |
| TC-R13 | P2 | **gegenstandslos** | Ohne Ruhe-Meldung meldet das Plugin auch nicht „nichts wartet“, solange ein Note-off aussteht. Die Silence-Flags sind wieder die von 0.2.2: gesetzt nur für Blöcke aus exakten Nullen. Das war vor 0.3.0 schon so und beschreibt nur das Audio des Blocks; ein ausstehendes Note-off kommt mit dem nächsten Block, den der Host aufruft (TC-R08) |
| TC-R14 | P3 | **dokumentiert** | Codex' zweite Variante. README, Abschnitt Voices, beschreibt die Abweichung nach einer Erhöhung der Stimmenzahl mit Codex' Notenfolge; ebenso `engine/synth.h` und PLANUNG. Die genaue Altersreihenfolge über Bänke hinweg hieße, in die Stimmenverwaltung des vendorten `Part` einzugreifen, und der Kern bleibt unverändert. Die Test-Ausgabe sagt jetzt „the oldest voice taken while the number stays“ |

## Fehlende Tests aus dem Review

| Punkt | Ergebnis |
| --- | --- |
| 1 bis 3 (TC-R11 bis TC-R13) | Entfallen mit der Ruhe-Meldung |
| 4 (TC-R14) | Nicht ergänzt: Das Verhalten ist dokumentiert, nicht zugesichert. Der Nutzer will nur die nötigen Tests |
| 5 (Ausnahme im Plugin-Firmwarevergleich) | Erledigt: Die Ausnahme ist weg. `TestAgainstFirmware` verlangt wieder jedes Sample gleich, wie bis 0.2.2; ebenso der Engine-Vergleich |
| 6, 7 | Nicht ergänzt (wie im Review als Grenze beschrieben) |

## Was mit der Ruhe-Meldung sonst entfallen ist

* `engine/synth.{h,cpp}`: `AtRest()`, `kWakeLevel`, das Mitzählen der Stille
  von Bank 0 und die exakten Nullen in Ruhe. Bank 0 läuft wie die Firmware.
  `kRestLevel`/`kRestQuanta` heißen jetzt `kQuietLevel`/`kQuietQuanta` und
  dienen nur noch dem Weglegen stiller Zusatzbänke. Engine 0.2.1.
* `adapter/`: alle Ruhe-Änderungen von 0.3.0 zurückgenommen (`AtRest()`,
  `silent_`, das Leeren der Filterintegratoren). Die Dateien sind wieder
  byte-gleich mit 0.2.2; Adapter-Version 0.2.1.
* Tests: `TestRest` in Engine, Adapter und Plugin, der Firmware-Vergleich
  „through a rest“. `bench_torus_classic` misst statt der Zeit bis zur Ruhe
  die Kosten einer stillen Instanz und die Zeit, bis drei Bänke weggelegt
  sind.
* Doku: README-Abschnitt „Rest“ ersetzt durch „No Plug-in Nap“; die Nap-Schritte
  der DAW-Liste (9.6 bis 9.11, 9.15) ersetzt durch einen Schritt, der nur
  prüft, dass nichts abgeschnitten wird.

## Außerdem in 0.3.1 (nicht aus dem Review)

Wünsche des Nutzers vom 2026-11-15:

* **Oktavtasten** an der Bildschirm-Klaviatur, wie in Mendelejew 0.1.4, von
  dort übernommen: `KeyboardView::SetTranspose`, `ToggleButton::Style::Action`,
  `ClampKeyboardTranspose`, Fensterzustand Version 2 mit der Transposition.
  Start bei C3–C5 statt C4–C6. Prozessor und Controller nahmen schon alle 128
  Noten an; dort ist nichts geändert.
* **Reglerfarben**: die drei Töne genau von Mendelejew; Zuordnung unverändert.

Neuer Test: `TestController` prüft den Fensterzustand Version 2 (Hin und
zurück, Version 1 ohne Transposition, ungültige Werte abgelehnt) und
`ClampKeyboardTranspose`. Fenstertests der Oktavtasten gibt es nicht; Optik
und Bedienung prüft der Nutzer (DAW-Liste 9.7, 9.8).
