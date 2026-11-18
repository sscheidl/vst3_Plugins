# Prüfauftrag für Codex: Messier 0.1.6 (MS-R20 bis MS-R25)

Deine Nachprüfung von 0.1.5 ([CODEX_REVIEW_0.1.5.md](CODEX_REVIEW_0.1.5.md), Commit
`1d1632b`, mit dem Datenweg aus 0.1.4) fand sechs Punkte. 0.1.6 behebt sie; was wo
geändert ist und wie es getestet wurde, steht im
[Build-Bericht 0.1.6](BUILD_REPORT_0.1.6.md) (Englisch, mit Tabelle je Befund).

## Was du prüfst

* Repository `vst3_Plugins`, Branch `messier-0.1.0`, Worktree `.work/messier/wt` im
  Haupt-Checkout `D:\Eigene Dateien\Eigene Dokumente\Playground\vst3_Plugins`.
* Der Commit direkt nach `1d1632b` auf `messier-0.1.0`, gepusht nach `origin`
  (`git -C .work/messier/wt log -2`). `git diff 1d1632b HEAD` zeigt alles Neue.
* Geändert: `source/controller.*` (Verbindungsgeneration, Zählung vor und nach dem ersten
  Status, `ReadDisplay`), `source/processor.*` (Stempel, nummerierte Anzeigeantworten,
  `PrepareAdapter`, geschütztes `notify`), `source/parameters.h`, `source/editor.*`
  (`attached`, `open`, `Add`), `source/wav_reader.cpp`; Tests in `test_plugin.cpp`
  (`TestNewProcessorSameController` neu geschrieben, `TestLateDisplayReply`, neuer
  Abschnitt in `TestResetButtonThroughHost`, zwei neue Aufrufe im Countdown-Test) und
  `test_loader.cpp` (`TestStreamErrors`).
* Das gestagte Bundle `out/messier-0.1.6/` ist aus genau diesem Stand gebaut
  (`SOURCE_SHA256SUMS.txt`, `BUILD_INFO.txt`).
* Das Atlas/Orion-Fenster (MS-R05) bleibt zurückgestellt; den Kartenstreifen bitte nicht
  optisch bewerten.

## Prüfpunkte

1. **MS-R20 bis MS-R25 nachprüfen:** behoben, teilweise, offen? Vor allem:
   * Zählt der Controller jetzt in allen Reihenfolgen richtig, und zählt dieselbe
     Verbindung nichts doppelt? Gemeint sind `connect`, Parameter 199, ein verspäteter
     oder ausbleibender Status, ein Reconnect zum selben Processor und eine umgeordnete
     Statusantwort (MS-R20).
   * Kann irgendeine Antwort einer früheren Verbindung noch Anzeige, Wellenform oder
     Zählung erreichen (MS-R21)?
   * Ist die Annahme haltbar, dass ein alter Processor die neue Generation nie hört?
2. **Doppelte Markierung:** Vor dem ersten Status markiert jede Änderung von Parameter
   199 das Projekt. Ist das in jedem Fall höchstens eine Überzählung, nie ein Verlust?
3. **MS-R24 im Code:** Die SDK-Methode `VSTGUIEditor::attached` hängt den Editor auch bei
   `open() == false` an; `Editor::attached` macht das mit `VSTGUIEditor::removed()`
   rückgängig. Bleibt dabei etwas angemeldet, offen oder doppelt freigegeben? Das lässt
   sich ohne Fenster nur im Code beurteilen.
4. **MS-R25:** Ist die Ausnahmemaske des Aufrufer-Streams in jedem Pfad wiederhergestellt?
   Gibt es Dateien, die jetzt fälschlich als Lesefehler statt als „fehlt“ gelten (etwa
   ungültige Pfadzeichen unter Windows)?
5. **Klang und Audio-Thread** unberührt: `PrepareAdapter` und der Stempel laufen nicht im
   Audio-Thread. Ändert das Wiedereinplanen des Resets etwas, wenn keiner wartete?
6. **Neue Fehler** durch die Änderungen, auch in den neuen Tests.

## Mein Stand

Matrix am 2026-11-18 auf genau diesem Stand, Logs in `out/messier-0.1.6/logs/`:

| Bau | ctest | Validator (alle vier Plugins) |
| --- | --- | --- |
| VS 2026 (MSVC 19.51) Release | 16/16 | 47/0 |
| VS 2026 Debug | 16/16 | 47/0 |
| VS 2022 (MSVC 19.44) Release | 16/16 | 47/0 |
| ASan (die fünf Messier-Testprogramme) | alle Exit 0, kein Bericht | – |

Firmware-Vergleich im Plugin: 0 von 960.000 Samples verschieden. Die Countdown-Tests
laufen nur im Release-Bau (README).

Zusätzlich habe ich die Korrekturen von MS-R20, MS-R21 und MS-R22 je einzeln wieder
herausgenommen: Der jeweils neue Test scheiterte dann (an B's erster Ladung, an der
verspäteten Antwort, am Reset nach dem Ratenwechsel). Für MS-R24 gibt es keinen
automatischen Test (Fenster nötig), für MS-R23 und MS-R25 laufen die neuen Tests nur gegen
den neuen Code.

## Bauen und testen

```text
cmake -S .work/messier/wt -B .work/messier-review/build -G "Visual Studio 18 2026" -A x64 -DVST3_SDK_ROOT="<Haupt-Checkout>/external/vst3sdk"
cmake --build .work/messier-review/build --config Release --parallel 4
ctest --test-dir .work/messier-review/build -C Release -R messier --output-on-failure
```

## Regeln

* Nur Code, Builds und Tests ohne Fenster. Keine Fenster-, DAW- oder Hörtests.
* Ändere nichts außer deiner Ergebnisdatei `instruments/messier/docs/CODEX_REVIEW_0.1.6.md`
  im Worktree. Kein Commit, kein Push, kein Branchwechsel, kein Stash. Eigene Build-Ordner
  danach entfernen.

## Ergebnis

Urteil (FREIGABE oder KEINE FREIGABE für den DAW-Test); Nachprüfung MS-R20 bis MS-R25 als
Tabelle (behoben, gegenstandslos, offen). Neue Befunde ab **MS-R26** mit Schwere P0 bis P3,
Beleg (Datei:Zeile), Auslösung, bewiesen oder vermutet und minimaler Korrektur; dann ohne
Befund Geprüftes, fehlende Tests, nicht Geprüftes.
