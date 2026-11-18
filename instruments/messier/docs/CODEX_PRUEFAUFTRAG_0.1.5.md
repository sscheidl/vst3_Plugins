# Prüfauftrag für Codex: Messier 0.1.5 (Datenweg des Fensters und MS-R16 bis MS-R19)

Deine Nachprüfung von 0.1.3 ([CODEX_REVIEW_0.1.3.md](CODEX_REVIEW_0.1.3.md), Stand
`160a478`) fand vier Punkte. 0.1.5 behebt sie
([Umsetzung](CODEX_REVIEW_0.1.3_UMSETZUNG.md)). Dazwischen liegt 0.1.4, das du noch nicht
geprüft hast: der Datenweg des Fensters (Wellenform, Loop, Abspielposition, Spray und die
echten Grains von der Engine bis ins Fenster) mit einem vorläufigen Kartenstreifen
([Build-Bericht 0.1.4](BUILD_REPORT_0.1.4.md); Vorgaben in Abschnitt 8 des Buildauftrags,
`docs/CLAUDE_BUILDAUFTRAG_MESSIER_0.1.0.md` im Haupt-Checkout, untracked). Beides liegt in
einem Commit.

## Was du prüfst

* Repository `vst3_Plugins`, Branch `messier-0.1.0`, Worktree `.work/messier/wt` im
  Haupt-Checkout `D:\Eigene Dateien\Eigene Dokumente\Playground\vst3_Plugins`.
* Der Commit direkt nach `160a478` (`git -C .work/messier/wt log -2`), nicht gepusht.
  `git diff 160a478 HEAD` zeigt 0.1.4 und 0.1.5 zusammen.
* **0.1.4, Datenweg:** `core/engine.h` (Patch P4), `engine/looper_engine.*`
  (`EngineDisplay`, `Display()`), `source/wave_overview.*`, `source/display_data.*`,
  `source/map_view.*`, die Anzeige-Teile von `source/file_loader.*`, `source/processor.*`,
  `source/controller.*`, `source/editor.*`; Tests `TestDisplay` (test_engine),
  `TestOverview` (test_loader), `TestDisplayPath` (test_plugin).
* **0.1.5, Korrekturen:** `source/wav_reader.*`, `LoadProject` in `source/file_loader.cpp`,
  die Kennung im Status (`source/processor.*`, `source/parameters.h`), `connect` und die
  Zählung im Controller (`CountFromParameter`, `CountFromStatus`), `process` und
  `Restart` im Processor, `StreamAdapter::Prepare` und `ControlWaiting`, die
  `createInstance`-Funktionen; Tests `TestNewProcessorSameController`,
  `TestProjectWithoutMemory`, die neuen Abschnitte von `TestResetButtonThroughHost`,
  `TestOutOfMemory` (test_loader) und `TestNoExceptionLeavesHostCalls`,
  `TestPrepareWithoutMemory` (test_adapter).
* Das gestagte Bundle `out/messier-0.1.5/` ist aus diesem Stand gebaut
  (`SOURCE_SHA256SUMS.txt`, `BUILD_INFO.txt`).
* Das Atlas/Orion-Fenster (MS-R05) bleibt zurückgestellt: Aufteilung und Farben sind
  offen. Der Kartenstreifen ist vorläufig; bitte seine Optik nicht bewerten.

## Prüfpunkte

1. **MS-R16 bis MS-R19 nachprüfen:** behoben, teilweise, offen? Insbesondere: Gibt es
   weitere Wege, auf denen Speichermangel beim Lesen als „Datei fehlt“ oder „ungültig“
   durchgeht (MS-R16)? Ist die Zählung nach einem Processor-Wechsel jetzt in allen
   Reihenfolgen von `connect`, Parameter 199 und Status richtig, ohne dass dieselbe
   Verbindung doppelt zählt (MS-R17)? Gibt es weitere Host-Einstiegspunkte mit offener
   Ausnahmegrenze (MS-R19)?
2. **Klang unberührt:** Kann das Lesen der Anzeige (P4, `LooperEngine::Display`) DSP-Takt,
   Zufallszustand oder Ausgabe ändern? Bleibt der Referenzvergleich mit dem unveränderten
   Kern gültig? Ändert das neue Wiedereinplanen eines Resets in `Restart` etwas, wo kein
   Reset wartete?
3. **Audio-Thread:** Ist der Weg in `process` frei von Sperren, Warten und Allokation, auch
   ohne offenes Fenster und beim Aktivieren? Ist `LatestValue` richtig benutzt (ein
   Schreiber, ein Leser)?
4. **Dateigeneration:** Kann das Fenster je die Wellenform einer anderen Datei zeigen als
   der, die der Kern liest: beim Tausch, nach einem gescheiterten Laden, nach einem
   Projektwechsel, wenn mehr als vier Dateien schnell hintereinander kommen, oder nach
   einem Processor-Wechsel (dessen Nummern wieder bei 1 beginnen)? Lebensdauer der
   Übersicht (`shared_ptr<const WaveOverview>`) zwischen Lader, Processor, Controller und
   Fenster.
5. **Stimmen die Werte?** Loop und Abspielposition gegen `core/engine.cpp` (dieselben
   Formeln, dieselben geglätteten Werte), Spray, Panorama, wartende Grains.
6. **Nachrichten:** Sind die Byte-Kopien von `EngineDisplay` und `WaveOverview` sicher
   (Größe, Polsterung, Prüfung beim Empfang in `ReadDisplayBytes`/`ReadOverviewBytes`)?
7. **Neue Fehler** durch die Änderungen, auch in den neuen Tests und im Kartenstreifen
   (Zeichnen bei leeren oder extremen Werten).

## Mein Stand

Matrix am 2026-11-18 auf genau diesem Stand, Logs in `out/messier-0.1.5/logs/`:

| Bau | ctest | Validator (alle vier Plugins) |
| --- | --- | --- |
| VS 2026 (MSVC 19.51) Release | 16/16 | 47/0 |
| VS 2026 Debug | 16/16 | 47/0 |
| VS 2022 (MSVC 19.44) Release | 16/16 | 47/0 |
| ASan (die fünf Messier-Testprogramme) | alle Exit 0, kein Bericht | – |

Firmware-Vergleich im Plugin 0 von 960.000 Samples verschieden; Anzeige lesen 25 ns je
Block, Wellenform von zehn Minuten Stereo 52 ms. Die Countdown-Tests laufen nur im
Release-Bau (README). Gegen den Code von 0.1.3 habe ich die neuen Tests nicht laufen
lassen; dass sie die Fälle aus deinem Zusatztest nachstellen, ist am Code begründet.

## Wenn deine Läufe abbrechen

Bitte zuerst die Punkte 1 und 4 aus dem Code beurteilen und im Bericht klar sagen, was
nicht geprüft werden konnte.

## Bauen und testen

```text
cmake -S .work/messier/wt -B .work/messier-review/build -G "Visual Studio 18 2026" -A x64 -DVST3_SDK_ROOT="<Haupt-Checkout>/external/vst3sdk"
cmake --build .work/messier-review/build --config Release --parallel 4
ctest --test-dir .work/messier-review/build -C Release -R messier --output-on-failure
```

## Regeln

* Nur Code, Builds und Tests ohne Fenster. Keine Fenster-, DAW- oder Hörtests.
* Ändere nichts außer deiner Ergebnisdatei `instruments/messier/docs/CODEX_REVIEW_0.1.5.md`
  im Worktree. Kein Commit, kein Push, kein Branchwechsel, kein Stash. Eigene Build-Ordner
  danach entfernen.

## Ergebnis

Urteil (FREIGABE oder KEINE FREIGABE für den DAW-Test); Nachprüfung MS-R16 bis MS-R19 als
Tabelle (behoben, gegenstandslos, offen). Neue Befunde ab **MS-R20** mit Schwere P0 bis P3,
Beleg (Datei:Zeile), Auslösung, bewiesen oder vermutet und minimaler Korrektur; dann ohne
Befund Geprüftes, fehlende Tests, nicht Geprüftes.
