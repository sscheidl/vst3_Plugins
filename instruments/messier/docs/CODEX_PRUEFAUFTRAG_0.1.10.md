# Prüfauftrag für Codex: Messier 0.1.10 (MS-R34 bis MS-R37)

Deine Prüfungen von 0.1.8 ([CODEX_REVIEW_0.1.8.md](CODEX_REVIEW_0.1.8.md)) und 0.1.9
([CODEX_REVIEW_0.1.9.md](CODEX_REVIEW_0.1.9.md)) fanden vier Punkte. 0.1.10 behebt sie,
jeweils als Klasse. Was wo geändert ist und wie es getestet wurde, steht im
[Build-Bericht 0.1.10](BUILD_REPORT_0.1.10.md) (Englisch, Tabelle am Anfang).

## Was du prüfst

* Repository `vst3_Plugins`, Branch `messier-0.1.0`, Worktree `.work/messier/wt` im
  Haupt-Checkout `D:\Eigene Dateien\Eigene Dokumente\Playground\vst3_Plugins`.
* **0.1.10 ist uncommittet** (Stand 2026-11-18) auf `302b9fc` (0.1.7 bis 0.1.9, gepusht).
  `git -C .work/messier/wt diff 302b9fc` zeigt alles Neue. Falls der Nutzer vor deiner
  Prüfung committet, prüfe diesen Commit und nenne ihn.
* Geändert: `source/output_gate.h` (neu), `source/guard.h` (neu), `source/processor.*`
  (`ReadSwitches`, `Render`, Ereignisse auf der Zeit der Engine, `screen_held_` und
  `host_held_`, `EngineNoteOff`, `PlayKeyboardNote`), `source/file_drop.cpp`,
  `source/editor.*`, `source/map_view.*`, `source/widgets.*`; Tests
  `TestSwitchesOnTheSample`, `TestScreenAndHostKeys` und `TestLongPause` (neu), drei neue
  Aufrufe im Countdown-Test.
* Das gestagte Bundle `out/messier-0.1.10/` ist aus genau diesem Stand gebaut
  (`SOURCE_SHA256SUMS.txt`, `BUILD_INFO.txt`).

## Prüfpunkte

1. **MS-R34 bis MS-R37 nachprüfen:** behoben, teilweise, offen?
2. **Der Plan des Gates** (`OutputGate::Plan`, `Render`):
   * Stimmen Plan und Rendering auf das Sample überein, auch an Segmentgrenzen und bei
     mehreren Punkten an derselben Stelle?
   * Ist die Ausgabe für jede Blockteilung gleich, auch mit Ratenwandlung (44,1 oder
     96 kHz)?
   * Ruht die Engine ab dem richtigen Sample?
   * Was passiert bei mehr als 64 Punkten je Schalter und Block, bei einem Block mit
     0 Samples und bei Aktivierung oder Projektladen mitten in einer Blende?
3. **Ereignisse auf der Zeit der Engine** (`EngineOffset`):
   * Kommt ein Ereignis nach einer Pause im selben Block an der richtigen Stelle?
   * Bleibt die Reihenfolge erhalten?
   * Kann eine Warteschlange des Adapters während einer langen Pause überlaufen?
     Seit 0.1.10 gibt der Processor kein Loslassen einer Note weiter, die niemand hält
     (`host_held_` je Kanal, `screen_held_`); `TestLongPause` prüft das.
4. **Gemeinsame Noten** (MS-R34):
   * Gibt es einen Ablauf, in dem eine Note hängen bleibt oder zu früh endet?
     Zum Beispiel Neustart, Projektladen, LATCH, Transponieren, Schließen des Fensters,
     Disconnect, eine Host-Note ohne Note-off oder doppelte Note-ons.
   * Ist es vertretbar, nur Kanal 1 zu teilen? Die anderen Kanäle trennt der Kern
     selbst.
5. **Ausnahmegrenzen** (MS-R37): Fehlt noch ein Weg von VSTGUI, Host oder System in den
   Code des Fensters, der eine Ausnahme durchlassen kann? Lässt ein Abbruch mitten im
   Zeichnen oder in `close()` etwas halb zurück? Die Handler der Ansichten sind ohne
   Fenster nicht testbar; bitte im Code beurteilen.
6. **Neue Fehler** durch die Änderungen, auch in den neuen Tests.

## Mein Stand

Matrix am 2026-11-18 auf genau diesem Stand, Logs in `out/messier-0.1.10/logs/`:

| Bau | ctest | Validator (alle vier Plugins) |
| --- | --- | --- |
| VS 2026 (MSVC 19.51) Release | 16/16 | 47/0 |
| VS 2026 Debug | 16/16 | 47/0 |
| VS 2022 (MSVC 19.44) Release | 16/16 | 47/0 |
| ASan (die fünf Messier-Testprogramme) | alle Exit 0, kein Bericht | – |

Firmware-Vergleich im Plugin: 0 von 960.000 Samples verschieden.

Gegenproben: Jede der vier Korrekturen habe ich einmal herausgenommen. Der jeweils neue
Test scheiterte dann:

* MS-R35 (Punkte wieder am Blockanfang) beim Vergleich der Blockteilungen;
* MS-R34 (Halterprüfung entfernt) an der gemeinsamen Note;
* MS-R36 (Ruheprüfung entfernt) an der MIDI-Ausgabe;
* MS-R37 (Kopie zurück) mit einer Ausnahme, die den Drop verlässt;
* die Regel für das Loslassen während einer Pause an verworfenen Noten im Adapter.

## Bauen und testen

```text
cmake -S .work/messier/wt -B .work/messier-review/build -G "Visual Studio 18 2026" -A x64 -DVST3_SDK_ROOT="<Haupt-Checkout>/external/vst3sdk"
cmake --build .work/messier-review/build --config Release --parallel 4
ctest --test-dir .work/messier-review/build -C Release -R messier --output-on-failure
```

## Regeln

* Nur Code, Builds und Tests ohne Fenster. Keine Fenster-, DAW- oder Hörtests.
* Ändere nichts außer deiner Ergebnisdatei `instruments/messier/docs/CODEX_REVIEW_0.1.10.md`
  im Worktree. Kein Commit, kein Push, kein Branchwechsel, kein Stash. Eigene Build-Ordner
  danach entfernen.

## Ergebnis

Urteil (FREIGABE oder KEINE FREIGABE für den DAW-Test); Nachprüfung MS-R34 bis MS-R37 als
Tabelle (behoben, gegenstandslos, offen). Neue Befunde ab **MS-R38** mit Schwere P0 bis P3,
Beleg (Datei:Zeile), Auslösung, bewiesen oder vermutet und minimaler Korrektur; dann ohne
Befund Geprüftes, fehlende Tests, nicht Geprüftes.
