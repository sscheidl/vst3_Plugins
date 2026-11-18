# Prüfauftrag für Codex: Messier 0.1.3 (Nachprüfung)

Nachprüfung deiner Nachprüfung von 0.1.2 ([CODEX_REVIEW_0.1.2.md](CODEX_REVIEW_0.1.2.md)).
Umsetzung beschrieben in [CODEX_REVIEW_0.1.2_UMSETZUNG.md](CODEX_REVIEW_0.1.2_UMSETZUNG.md).

## Was du prüfst

* Repository `vst3_Plugins`, Branch `messier-0.1.0`, Worktree `.work/messier/wt` im
  Haupt-Checkout `D:\Eigene Dateien\Eigene Dokumente\Playground\vst3_Plugins`.
* **0.1.3 ist uncommittet** (Stand 2026-11-18) auf `381f06a`, deinem letzten Stand.
  `git -C .work/messier/wt diff 381f06a` zeigt alles Neue: vor allem `source/controller.*`,
  `source/file_loader.cpp`, `source/parameters.h`, `source/processor.cpp`, `source/editor.*`
  und die Tests in `tests/test_plugin.cpp` und `tests/test_loader.cpp`. Falls der Nutzer vor
  deiner Prüfung committet, prüfe diesen Commit und nenne ihn.
* Das gestagte Bundle `out/messier-0.1.3/` ist aus genau diesem Stand gebaut
  (`SOURCE_SHA256SUMS.txt`).
* MS-R05 (Atlas/Orion-Fenster) bleibt zurückgestellt; das Fenster ist vorläufig.

## Prüfpunkte

1. **MS-R11:** Lassen Parametertexte (`getParamStringByValue`, `getParamValueByString`)
   noch eine Ausnahme hinaus? Gibt es weitere Host-Aufrufe des Controllers oder Processors,
   die Speicher brauchen und nicht abgesichert sind?
2. **MS-R12:** Kann eine erfolgreiche Fenster-Datei jetzt noch ohne `setDirty` bleiben,
   oder doppelt gezählt werden? Trägt die Normierung von Parameter 199 (65.536 Stufen)
   durch einen Host mit Float-Werten?
3. **MS-R13:** Ist die dokumentierte Zusammenfassung von Reset-Drucken vertretbar und
   richtig beschrieben?
4. **MS-R14:** Kann ein abgelehntes oder gescheitertes Projektladen noch Pfad oder Status
   ändern, die `getState` speichert? Ist die neue Regel (bei Speichermangel ganz ablehnen)
   stimmig mit README und Planung?
5. **MS-R15:** Kann `createView` noch ein Objekt verlieren?
6. **Neue Fehler** durch die Änderungen, auch in den neuen Tests.

## Wenn deine Läufe abbrechen

Falls deine Läufe abbrechen, bitte zuerst die Punkte 2 und 4 aus dem Code beurteilen (sie
tragen den Hauptteil der Änderung) und im Bericht klar sagen, was
nicht geprüft werden konnte. Ein vollständiger eigener Bau ist nützlich, aber nicht nötig:
Meine Läufe (VS 2026 Release und Debug, VS 2022 Release, ASan) liegen unter
`out/messier-0.1.3/logs/`.

## Bauen und testen

```text
cmake -S .work/messier/wt -B .work/messier-review/build -G "Visual Studio 18 2026" -A x64 -DVST3_SDK_ROOT="<Haupt-Checkout>/external/vst3sdk"
cmake --build .work/messier-review/build --config Release --parallel 4
ctest --test-dir .work/messier-review/build -C Release -R messier --output-on-failure
```

## Regeln

* Nur Code, Builds und Tests ohne Fenster. Keine Fenster-, DAW- oder Hörtests.
* Ändere nichts außer deiner Ergebnisdatei `instruments/messier/docs/CODEX_REVIEW_0.1.3.md`
  im Worktree. Kein Commit, kein Push, kein Branchwechsel, kein Stash. Eigene Build-Ordner
  danach entfernen.

## Ergebnis

Urteil (FREIGABE oder KEINE FREIGABE für den DAW-Test), Nachprüfung MS-R11 bis MS-R15 als
Tabelle (behoben, gegenstandslos, offen), neue Befunde ab **MS-R16** mit Schwere P0 bis P3,
Beleg (Datei:Zeile), Auslösung, bewiesen oder vermutet und minimaler Korrektur; dann ohne
Befund Geprüftes, fehlende Tests, nicht Geprüftes.
