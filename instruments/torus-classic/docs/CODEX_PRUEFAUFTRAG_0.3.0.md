# Prüfauftrag an Codex: Kekulé 0.3.0 (Torus Classic), nur Code

* **Stand:** 2026-11-15
* **Gegenstand:** `instruments/torus-classic/` im Repo
  [sscheidl/vst3_Plugins](https://github.com/sscheidl/vst3_Plugins), Branch
  `kekule-0.3.0`, abgezweigt von `main` @ `490eca8` (Kekulé 0.2.2).
  `git diff 490eca8` zeigt genau diese Runde; `main` ist inzwischen weiter
  (`b57f68c`, Mendelejew 0.1.2), `instruments/torus-classic/` ist dort
  unverändert.
* **Wo der Stand liegt:** noch nicht committet, als Arbeitskopie im Worktree
  `.work/kekule-0.3.0/wt` (Pfad ab dem Haupt-Checkout; `HEAD` dort ist
  `490eca8`). Gegenüber `HEAD` sind 34 Dateien geändert und diese Datei neu.
  Zum Festhalten des geprüften Stands: SHA-256 von
  `instruments/torus-classic/components.json` im Worktree ist
  `dc2f84a934467dad9b217ce119d2b7785e2a5558fa38c28620cdcb8cf81f6d8f`. Bitte
  vor und nach der Prüfung vergleichen und im Bericht nennen. Hat der Nutzer
  inzwischen committen lassen, gilt der Commit auf `kekule-0.3.0`.
* **Verfasser des Codes:** Claude
* **Grundlage:** der Abschnitt „Vereinbarte Folgeschritte" deines Berichts
  [CODEX_REVIEW_0.2.0.md](CODEX_REVIEW_0.2.0.md), Befund TC-R10 aus
  [CODEX_REVIEW_0.2.1.md](CODEX_REVIEW_0.2.1.md), das
  [Änderungsprotokoll](../CHANGELOG.md), das [README](../README.md)
  (Abschnitte Voices, Rest, Window, State) und die Festlegungen in
  `docs/PLANUNG.md`, Abschnitt „Kekulé", letzte Tabelle
* **Ergebnis nach:**
  `.work/kekule-0.3.0/wt/instruments/torus-classic/docs/CODEX_REVIEW_0.3.0.md`,
  also in den Worktree, neben diese Datei. Nicht in den Haupt-Checkout: Der
  steht auf einem fremden Branch.

## Auftrag und Grenzen

Erstprüfung von 0.3.0. Neu sind: wählbare Stimmenzahl 4/8/16, die
Ruhe-Erkennung für Plugin-Nap, das Schloss statt des Schalters FREE,
dreifarbige Reglerringe, die Korrektur zu TC-R10 und das Zustandsformat 3.
Der Nutzer hat 32 Stimmen gestrichen und will die Stimmenzahl nur im
Fenster, ohne MIDI-CC und ohne Automation (2026-11-15).

Es gelten die Regeln der bisherigen Prüfaufträge
([0.1.0](CODEX_PRUEFAUFTRAG_0.1.0.md), [0.2.0](CODEX_PRUEFAUFTRAG_0.2.0.md),
[0.2.1](CODEX_PRUEFAUFTRAG_0.2.1.md)): nichts ändern außer der Ergebnisdatei;
eigenes Build-Verzeichnis unter `.work/torus-classic-review/` und am Ende
löschen; `build/`, `dist/`, `out/` und die installierten Kopien nicht
anfassen; keine Fenster-, GUI-, DAW- oder Hörtests; Bewiesenes von
Vermutetem trennen. Der Haupt-Checkout gehört der Mendelejew-Sitzung (zuletzt
Branch `elements-0.1.3`, mit deren uncommitteten Änderungen); dort nichts
wechseln, committen oder stashen und nichts anlegen außer dem eigenen
Wegwerfbereich unter `.work/`. Im Worktree `.work/kekule-0.3.0/wt` ist die
Ergebnisdatei die einzige erlaubte Änderung; auch dort kein Branchwechsel,
Commit oder Stash. `.work/elements-review/` nicht anfassen.

Optik des Schlosses und der Farben, das Verhalten in Studio One und der
Höreindruck sind Sache des Nutzers. Zum Schloss und zur Stimmenwahl bitte
nur den Code prüfen (Zustände, Rückmeldung an den Host, Lebensdauer).

Unverändert seit `490eca8`: `core/` (der vendorte Kern, auch `part.{h,cpp}`),
`adapter/sinc_kernel.*`, die Zeitrechnung des Adapters, `engine/config.h`,
`parameter_manager.h`, `audio_output.h`, Factory und Klassen-IDs, das
Fenster-Größenverhalten.

Geändert:

| Bereich | Dateien |
| --- | --- |
| Engine 0.2.0 | `engine/synth.{h,cpp}`, `engine/dry_engine.{h,cpp}` |
| Adapter 0.3.0 | `adapter/stream_adapter.{h,cpp}`, `adapter/output_stage.h` |
| Shell 0.3.0 | `source/parameters.h`, `state_io.h`, `engine_bridge.h`, `processor.{h,cpp}`, `controller.cpp`, `editor.{h,cpp}`, `widgets.{h,cpp}`, `version.h` |
| Tests | `tests/test_engine.cpp`, `test_adapter.cpp`, `test_plugin.cpp`, `bench.cpp` |
| Sonstiges | `CMakeLists.txt`, `components.json`, `SHA256SUMS.upstream`, `tools/*.ps1`, Doku |

## Bauen und Testen

Der Worktree hat kein SDK-Submodul und keinen Build-Ordner mehr (der des
Verfassers ist entfernt). Bitte außerhalb des Worktrees bauen, vom
Haupt-Checkout aus:

```text
cmake -S .work/kekule-0.3.0/wt -B .work/torus-classic-review/build -G "Visual Studio 18 2026" -A x64 -DVST3_SDK_ROOT="<Haupt-Checkout>/external/vst3sdk"
cmake --build .work/torus-classic-review/build --config Release --parallel 4
ctest --test-dir .work/torus-classic-review/build -C Release -R "torus-classic\.(engine|adapter|provenance)" --output-on-failure
```

Der Stand `490eca8` baut Toy Piano Classic und Kekulé, nicht Mendelejew.
`torus-classic.plugin` öffnet am Ende Fenster (`TestViewLayout`,
`TestEditorResize`, `TestEditorInWindow`); wie bisher bitte nur die
fensterlosen Funktionen über einen eigenen Runner ausführen, neu darunter
`TestPolyphony` und `TestRest`.

Stand des Verfassers für 0.3.0: VS 2026 Release und Debug, Validator je 47
bestanden und 0 fehlgeschlagen, CTest
`torus-classic.{engine,adapter,plugin,provenance}` je bestanden. Die Logs
liegen beim Test-Build unter `out/torus-classic-0.3.0/logs/`. VS 2022 und
AddressSanitizer wurden für 0.3.0 nicht ausgeführt.

Die Tests zur Ruhe rechnen lange Ausklänge (bis 42 s Audio je Fall). Auf
dem Rechner des Verfassers brauchen die vier Tests zusammen in Release rund
75 s, in Debug rund 3 Minuten.

## Prüfpunkte

| Nr. | Was neu ist | Bitte prüfen |
| --- | --- | --- |
| 1 | **Bänke.** `Synth` hält `torus::Part part_[4]` statt einem. Bank 0 ist der Ablauf der Firmware; Bänke 1 bis 3 bekommen dasselbe `Patch` und denselben `PerformanceState`, werden vor Blende und `Output()` addiert. Der Kern ist unverändert | Ist der Pfad mit einer Bank wirklich Anweisung für Anweisung der alte (Reihenfolge der Aufrufe, `fade_`, Modellwechsel, Triggerschleife)? Der Engine-Test vergleicht bitgenau mit der Firmware; reicht das als Beleg? Gibt es einen Zustand im `Part`, den mehrere Bänke teilen (statische Daten, Tabellen)? `chords[polyphony_ - 1]` und `64 / polyphony_ - 4` bleiben bei `polyphony_ == 4`: stimmt das für jede Bank zu jeder Zeit? |
| 2 | **Verteilung.** `strike_` zählt die Anschläge modulo 4 × Bänke; Bank = `strike_ / 4`, die Stimme in der Bank wählt der `Part` reihum. Je Quantum höchstens 4 × Bänke Anschläge. Beim Erhöhen der Bankzahl springt `strike_` auf die erste neue Bank (nur wenn schon ein Anschlag kam) | Kann eine Bank in einem Quantum mehr als vier Anschläge bekommen und damit einen noch nicht gerenderten überschreiben (auch direkt nach einem Wechsel der Bankzahl, nach dem Zurücksetzen einer Bank, bei `strike_`-Überlauf)? Stimmt „die am längsten nicht angeschlagene Stimme" nach Wechseln und nach weggelegten Bänken noch, und wo nicht: ist die Abweichung harmlos und richtig dokumentiert? |
| 3 | **Bank-Zustände** `Off`/`On`/`Leaving`. Weniger Stimmen: Bänke jenseits der Zahl blenden über 192 Samples aus und werden dann zurückgesetzt. Ein Anschlag für eine Bank im Zustand `Leaving` wartet (Schleife bricht ab). `ResetBank`: Destruktor, `memset`, Placement-new, `Init`, `Seed`, `set_model`, auf dem Audio-Thread | Ist das Placement-new in ein Array-Element eines Members zulässig und vollständig (Lebensdauer, `DISALLOW_COPY_AND_ASSIGN`, keine const-Member)? Kann die wartende Triggerschleife hängen bleiben (Bank bleibt `Leaving`)? Kosten von `ResetBank` (111 KB je Bank, bis zu drei in einem Quantum): im Benchmark gemessen, vertretbar? Modellwechsel bei weggelegten Bänken: `set_model` auf alle, `ConfigureResonators` erst beim nächsten `Process`; bleibt etwas Altes hörbar? |
| 4 | **Ruhe in der Engine.** Je Bank ein Zähler `quiet` der Quanten, in denen jedes Sample des `Part`-Ausgangs unter `kRestLevel` (1e-9) lag. Bank ≥ 1: bei 4000 Quanten zurücksetzen. Bank 0: `AtRest()` wahr, wenn `quiet ≥ 4000`, kein Anschlag wartet und die anderen Bänke `Off` sind; dann schreibt `Render()` exakte Nullen. Hysterese: In Ruhe zählt erst `kWakeLevel` (4e-9) als Ton. Bank 0 läuft darunter weiter | Ist die Schwelle am richtigen Signal gemessen (vor Blende, Volume, Kniekennlinie)? Kann `AtRest()` wahr sein, während ohne neuen Anschlag noch hörbarer Ton folgt (Volume 0, Modellwechsel-Blende, Regleränderungen, FM mit Damping am Halteende, NaN)? Der Verfasser hat einen Fall gefunden und dokumentiert (FM-Dauerton); gibt es weitere, und endet die Ruhe dort jeweils von selbst? Ist die Aussage „erste Note nach der Ruhe bitgleich zur Firmware" haltbar (`TestDryReference`, Teil „through a rest")? Reicht eine Sekunde gegen die längste Verzögerungsleitung? |
| 5 | **Abweichung von der Firmware.** In Ruhe Nullen statt des Rests unter −180 dBFS. `CompareWithFirmware` und `TestAgainstFirmware` lassen genau das zu und zählen es | Lassen die Tests mehr durch als das (zu weite Ausnahme)? Ist die Abweichung im README („Differences from the firmware", „Rest") und im CHANGELOG richtig und vollständig beschrieben? |
| 6 | **Ruhe im Adapter.** `StreamAdapter::AtRest()`: keine wartenden Ereignisse, `silent_ ≥ count_` (alle gehaltenen Engine-Samples sind Nullen), `OutputStage::AtRest()`. `OutputStage::Process` leert die Integratoren, sobald exakte Nullen hereinkommen und alle vier unter 1e-9 liegen | Stimmt `silent_ ≥ count_` als Beweis, dass der Puffer nur Nullen hält, auch nach `Compact`, nach `Reset` und bei Raten mit großem Fenster? Ist das Leeren der Integratoren hörbar oder messbar relevant (Sprung unter 1e-9; bei offenem Filter wirkungslos)? Kann der Filter ohne das Leeren bei manchen Raten nie leer werden (deine Messung zu TC-R06 bei 4 MHz deutet es an)? Kosten je Sample |
| 7 | **Ruhe im Prozessor.** `at_rest_` (atomar) wird am Ende von `process()` gesetzt, wenn Audio gerendert wurde oder Ereignisse warten; `Restart` setzt es. Bildschirmtasten: `keys_queued_` (UI-Thread, nach erfolgreichem `Push`) und `keys_taken_` (Audio-Seite, bei jedem `Pop`); `AtRest()` verlangt Gleichstand. `getTailSamples()` meldet `kNoTail` in Ruhe, sonst `kInfiniteTail` | Speicherordnungen und Reihenfolgen: Kann `AtRest()` wahr sein, während ein Anschlag in der Queue liegt? Fenster zwischen `Push` und `fetch_add`? Verhalten bei `setActive`, bei geladenem Zustand, bei Parameter-Flush ohne Audio (`numSamples == 0`), bei Blöcken ohne Ausgabepuffer. Ist ein Tail, der sich zur Laufzeit ändert, mit dem SDK vereinbar (`getTailSamples` laut Header „UI-thread & Setup Done"; kein Restart-Flag dafür)? Was folgt, wenn ein Host den Wert nur einmal liest: beim Start ist das Plugin in Ruhe und meldet `kNoTail`. Ist das Risiko abgeschnittener Ausklänge damit größer als beim festen `kInfiniteTail` von 0.2.x? Bitte ausdrücklich bewerten |
| 8 | **Parameter 111 „Polyphony"** (Liste 4/8/16, `kIsList` ohne `kCanAutomate`), als Ereignis `PolyphonyEvent` über die Regler-Queue Nr. 8 des Adapters; `Restart` nimmt den Wert aus dem Zustand (`JumpPolyphony`) | Kommt der Wert auf allen Wegen an: Fenster, generische Parameterliste, geladener Zustand, Flush ohne Audio, Aktivierung? Stufenregel (`PolyphonyIndex`) überall dieselbe? Verträgt `ParameterState` den zwölften Parameter (Feldgrößen, `kSavedIndex`)? |
| 9 | **Zustand Version 3** (elf Werte). Version 2 lädt mit vier Stimmen, Version 1 wie bisher plus vier Stimmen | Alle Abschneidungen und ungültigen Felder für Version 2 und 3; ein V2-Zustand mit elf Werten (überzählige Bytes); Kanonisierung der drei gestuften Werte; Controller und Prozessor lesen denselben Leser |
| 10 | **TC-R10.** `ControlEvent(Control::Tune, x)` leitet an `TuneEvent(double(x))` weiter; der Score des Adaptertests nutzt `TuneEvent` und prüft den ankommenden Halbton; neu `TestTuneEvents` (Zusammenlegen im Quantum, volle Queue) | Behoben? Die Queue-Tests mit `CounterEngine` nutzen jetzt `Control::Structure` als Marke, weil Tune-Werte über 1 nun geklemmt würden: verliert das Aussagekraft? |
| 11 | **Fenster (nur Code).** `LockButton` und `ChoiceSelector` in `widgets.{h,cpp}`; das Schloss zeigt `!TuneFree`; `Editor::ChoosePolyphony` und `SetTuneFree` melden begin/perform/end; Tooltips über `CFrame::enableTooltips` | Zustände und Rückkopplung (Parameter → Anzeige → kein erneuter Edit), Lebensdauer der Views (`ForgetViews`, `close`), Tooltip-Text bei Zustandswechsel. Kein Fenstertest |
| 12 | **Tests.** Neu: `TestPolyphony`, `TestRest` (Engine und Plugin), `TestRest` und `TestTuneEvents` (Adapter); erweitert: Firmware-Vergleiche, Blockgrößen mit wechselnder Stimmenzahl, Zustände, Controller, Allokation | Hätten die Tests die naheliegenden Fehler gefunden: Anschlag in falscher Bank, Bank bleibt hängen, Ruhe zu früh, Nullen ohne Ruhe, Ruhe trotz wartender Taste? Was fehlt? Der Verfasser hat die Tests nur gegen 0.3.0 laufen lassen |
| 13 | **Doku gegen Code.** README (Voices, Rest, Window, Timing, State, Differences, Install), CHANGELOG 0.3.0, DAW-Prüfliste Abschnitt 9, PLANUNG | Jede Zahl und jede Aussage, die sich am Code oder an einer Messung prüfen lässt. Besonders: die Zeiten bis zur Ruhe, die Pegel mit 16 Stimmen, die CPU-Zahlen, die Aussagen über Studio One (belegt sind nur die drei verlinkten Quellen) |

## Bekannt und gewollt (nur melden, wenn die Beschreibung nicht stimmt)

* Alles aus den bisherigen Prüfaufträgen, soweit nicht oben ersetzt.
* Die erste Bank läuft auch in Ruhe weiter; das Plugin spart in Ruhe selbst
  keine Rechenzeit.
* Ob Studio One 7 ein Instrument überhaupt einschlafen lässt, worauf es
  dabei achtet und ob es für Bildschirmtasten und Parameteränderungen
  aufwacht, ist nicht bekannt und nicht automatisch prüfbar. Das steht so im
  README und in der DAW-Prüfliste.
* Eine weggelegte Bank beginnt mit ihrer ersten Stimme und ihrem
  Rausch-Startwert neu.
* Bänke, die einzeln unter der Schwelle liegen, können zusammen bis zum
  Vierfachen ausgeben, bevor sie weggelegt sind.
* Kein Anschlag ohne Erregung wird unterdrückt: Er hält das Instrument eine
  Sekunde wach.
* Die Farbzuordnung der Regler ist ein Vorschlag an den Nutzer.

## Format des Ergebnisses

```
# Codex Review: Kekulé 0.3.0
VERDICT: FREIGABE FÜR DAW-TEST | FREIGABE MIT KORREKTUREN | KEINE FREIGABE
Geprüft: <Commit oder Arbeitsstand>, Datum, Compiler

## Nachprüfung TC-R10
| ID | Ergebnis (behoben / teilweise / nicht behoben) | Beleg | Rest |

## Neue Befunde
| ID | Schwere | Bereich | Befund | Beleg (Datei:Zeile) | Auslösung und Auswirkung | bewiesen/vermutet | minimale Korrektur |

## Ohne Befund geprüft

## Fehlende Tests

## Nicht geprüft
```

Schweregrade wie bisher. Neue Befunde bitte ab `TC-R11` zählen.
