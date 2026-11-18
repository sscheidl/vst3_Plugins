# Prüfauftrag für Codex: Messier 0.1.2 (Nachprüfung)

Nachprüfung deiner Nachprüfung von 0.1.1 ([CODEX_REVIEW_0.1.1.md](CODEX_REVIEW_0.1.1.md)).
Umsetzung beschrieben in [CODEX_REVIEW_0.1.1_UMSETZUNG.md](CODEX_REVIEW_0.1.1_UMSETZUNG.md).

## Was du prüfst

* Repository `vst3_Plugins`, Branch `messier-0.1.0`, Worktree `.work/messier/wt` im
  Haupt-Checkout `D:\Eigene Dateien\Eigene Dokumente\Playground\vst3_Plugins`.
* **0.1.2 ist uncommittet** (Stand 2026-11-18). `git -C .work/messier/wt diff 26b8509` zeigt
  alles seit dem Commit, also 0.1.1 und 0.1.2 zusammen; neu gegenüber deinem letzten Stand
  sind vor allem `source/processor.*`, `source/file_loader.*`, `source/controller.*`,
  `source/editor.*`, `source/parameters.h`, `engine/looper_engine.h` (`FileData::sequence`)
  und die Tests in `tests/test_plugin.cpp` und `tests/test_loader.cpp`. Falls der Nutzer vor
  deiner Prüfung committet, prüfe diesen Commit und nenne ihn.
* Das gestagte Bundle `out/messier-0.1.2/` ist aus genau diesem Stand gebaut
  (`SOURCE_SHA256SUMS.txt`).
* MS-R05 (Atlas/Orion-Fenster) bleibt zurückgestellt; das Fenster ist vorläufig.

## Prüfpunkte

1. **MS-R06:** Verlässt noch eine Ausnahme einen Host-Aufruf von Controller oder Processor?
   `TestNoExceptionLeavesHostCalls` lässt jede Allokation von sechs Aufrufen der Reihe nach
   scheitern; fehlt ein Aufruf, der es braucht?
2. **MS-R07:** Kann der Audio-Thread jetzt Datei und Werte verschiedener Projekte mischen,
   auch bei mehreren `setState` hintereinander, bei vollem Rückstau, in `setActive`, mit einer
   Fenster-Datei dazwischen? Kann ein Projekt für immer warten? Hält die Reihenfolgenummer
   eine ältere Fenster-Datei zuverlässig fern?
3. **MS-R08:** Erreicht jeder Druck den Processor? Ist die Festlegung (Drucke im selben
   Block wirken einmal, ohne Audio verfallen sie nach 0,5 s, keine Aufnahme als Automation)
   vertretbar und dokumentiert?
4. **MS-R09:** Der Ausgabeparameter 199 als Weg vom Processor zum Controller: richtig
   angelegt (schreibgeschützt, versteckt, nicht gespeichert), richtig ausgewertet, eine
   Meldung je Datei?
5. **MS-R10:** Jeder Zugriff auf die Reserve unter der Sperre?
6. **Debug-Abdeckung:** Stimmt die Bewertung im Abschnitt „Teilauftrag“ der Umsetzung mit
   Code und Testlogs überein?
7. **Neue Fehler** durch die Änderungen, auch in den neuen Tests.

## Wenn deine Läufe abbrechen

Beim letzten Mal brachen deine Läufe mehrmals ab. Dann bitte zuerst die Punkte 2 und 3 aus
dem Code beurteilen (sie tragen den Hauptteil der Änderung) und im Bericht klar sagen, was
nicht geprüft werden konnte. Ein vollständiger eigener Bau ist nützlich, aber nicht nötig:
Meine Läufe (VS 2026 Release und Debug, VS 2022 Release, ASan) liegen unter
`out/messier-0.1.2/logs/`.

## Bauen und testen

```text
cmake -S .work/messier/wt -B .work/messier-review/build -G "Visual Studio 18 2026" -A x64 -DVST3_SDK_ROOT="<Haupt-Checkout>/external/vst3sdk"
cmake --build .work/messier-review/build --config Release --parallel 4
ctest --test-dir .work/messier-review/build -C Release -R messier --output-on-failure
```

## Regeln

* Nur Code, Builds und Tests ohne Fenster. Keine Fenster-, DAW- oder Hörtests.
* Ändere nichts außer deiner Ergebnisdatei `instruments/messier/docs/CODEX_REVIEW_0.1.2.md`
  im Worktree. Kein Commit, kein Push, kein Branchwechsel, kein Stash. Eigene Build-Ordner
  danach entfernen.

## Ergebnis

Urteil (FREIGABE oder KEINE FREIGABE für den DAW-Test), Nachprüfung MS-R06 bis MS-R10 als
Tabelle (behoben, gegenstandslos, offen), neue Befunde ab **MS-R11** mit Schwere P0 bis P3,
Beleg (Datei:Zeile), Auslösung, bewiesen oder vermutet und minimaler Korrektur; dann ohne
Befund Geprüftes, fehlende Tests, nicht Geprüftes.
