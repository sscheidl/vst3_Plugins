# Prüfauftrag für Codex: Messier 0.1.7 (MS-R26 bis MS-R29)

Deine Nachprüfung von 0.1.6 ([CODEX_REVIEW_0.1.6.md](CODEX_REVIEW_0.1.6.md), Commit
`46c3998`) fand vier Punkte. Drei davon (MS-R26 bis MS-R28) gehören zur selben Klasse wie
die Befunde davor: verspätete Nachrichten über einen Verbindungswechsel hinweg. 0.1.7
behebt sie als Klasse statt Fall für Fall. Was wo geändert ist, steht im
[Build-Bericht 0.1.7](BUILD_REPORT_0.1.7.md) (Englisch).

Die Grundentscheidung zur Projektänderung, bitte bewusst mitprüfen: Abgeglichen wird
nichts mehr. Jede Änderung von Parameter 199 und jeder Status mit mehr Dateien als der
letzte desselben Processors ruft `setDirty(true)` einmal. Eine Datei kann so zweimal
gemeldet werden, was für den Host nichts ändert. Damit gilt MS-R27 als erlaubtes
Verhalten, nicht als Fehler. Gegen ein Verdecken nach MS-R26 trägt Parameter 199 einen
Versatz je Processor.

## Was du prüfst

* Repository `vst3_Plugins`, Branch `messier-0.1.0`, Worktree `.work/messier/wt` im
  Haupt-Checkout `D:\Eigene Dateien\Eigene Dokumente\Playground\vst3_Plugins`.
* **0.1.7 ist uncommittet** (Stand 2026-11-18) auf `46c3998` (gepusht).
  `git -C .work/messier/wt diff 46c3998` zeigt alles Neue. Falls der Nutzer vor deiner
  Prüfung committet, prüfe diesen Commit und nenne ihn.
* Geändert: `source/controller.*` (gemeinsamer Generationszähler, `MarkChanged`,
  `CountFromStatus`), `source/processor.*` (Abweisen älterer Aufträge, Stempel je
  Auftrag, Versatz auf Parameter 199), `source/parameters.h` (`SampleChangesStep`),
  `source/wav_reader.cpp` (`RethrowStreamErrors`); Tests `TestNewProcessorSameController`
  (neu geschrieben), `TestLateRequest` (neu), `TestFileChangeMarksProject` (auf den neuen
  Vertrag umgestellt), `TestStreamErrors` (zwei Fälle).
* Das gestagte Bundle `out/messier-0.1.7/` ist aus genau diesem Stand gebaut
  (`SOURCE_SHA256SUMS.txt`). Es ist auf Wunsch des Nutzers auch im VST3-Ordner
  installiert. Das berührt deine Prüfung nicht.

## Prüfpunkte

1. **MS-R26 bis MS-R29 nachprüfen:** behoben, gegenstandslos, offen?
2. **Der neue Vertrag zur Projektänderung:**
   * Gibt es einen Ablauf, in dem eine Datei aus dem Fenster gar kein `setDirty` mehr
     bekommt, sofern der Host Parameter 199 oder den Status überhaupt zustellt?
   * Gibt es einen, in dem ohne neue Datei eines kommt, etwa durch Abfragen, ein Projekt
     oder wiederholte Werte?
   * Verspätete Werte nach einem Processor-Wechsel sind als einmaliges Mehr ausdrücklich
     erlaubt.
3. **Abweisen älterer Aufträge:**
   * Kann ein Auftrag einer älteren Verbindung noch Laden, Reset, Noten, Status oder
     Anzeige auslösen?
   * Kann umgekehrt ein gültiger Auftrag fälschlich abgewiesen werden, etwa nach
     `initialize`, bei mehreren Controllern im Prozess oder bei einem Host, der nach
     einem Processor-Wechsel denselben Controller weiterverwendet?
   * Ist die Ausnahme für das Loslassen einer Taste vertretbar?
4. **Grenzen:** Controller in einem anderen Prozess als der Processor (der gemeinsame
   Zähler gilt nur pro Prozess). Ist das richtig beschrieben?
5. **Neue Fehler** durch die Änderungen, auch in den neuen Tests.

## Mein Stand

Matrix am 2026-11-18 auf genau diesem Stand, Logs in `out/messier-0.1.7/logs/`:

| Bau | ctest | Validator (alle vier Plugins) |
| --- | --- | --- |
| VS 2026 (MSVC 19.51) Release | 16/16 | 47/0 |
| VS 2026 Debug | 16/16 | 47/0 |
| VS 2022 (MSVC 19.44) Release | 16/16 | 47/0 |
| ASan (die fünf Messier-Testprogramme) | alle Exit 0, kein Bericht | – |

Firmware-Vergleich im Plugin: 0 von 960.000 Samples verschieden.

Gegenproben: Mit herausgenommenem Versatz auf Parameter 199 scheitert der neue Test an
der ersten Ladung von B (Codex' Fall aus MS-R26). Mit herausgenommenem Abweisen scheitert
`TestLateRequest` am verspäteten Ladeauftrag. Für MS-R29 laufen die neuen Fälle nur gegen
den neuen Code.

## Bauen und testen

```text
cmake -S .work/messier/wt -B .work/messier-review/build -G "Visual Studio 18 2026" -A x64 -DVST3_SDK_ROOT="<Haupt-Checkout>/external/vst3sdk"
cmake --build .work/messier-review/build --config Release --parallel 4
ctest --test-dir .work/messier-review/build -C Release -R messier --output-on-failure
```

## Regeln

* Nur Code, Builds und Tests ohne Fenster. Keine Fenster-, DAW- oder Hörtests.
* Ändere nichts außer deiner Ergebnisdatei `instruments/messier/docs/CODEX_REVIEW_0.1.7.md`
  im Worktree. Kein Commit, kein Push, kein Branchwechsel, kein Stash. Eigene Build-Ordner
  danach entfernen.

## Ergebnis

Urteil (FREIGABE oder KEINE FREIGABE für den DAW-Test); Nachprüfung MS-R26 bis MS-R29 als
Tabelle (behoben, gegenstandslos, offen). Neue Befunde ab **MS-R30** mit Schwere P0 bis P3,
Beleg (Datei:Zeile), Auslösung, bewiesen oder vermutet und minimaler Korrektur; dann ohne
Befund Geprüftes, fehlende Tests, nicht Geprüftes.
