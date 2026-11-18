# Prüfauftrag für Codex: Messier 0.1.8 (MS-R30 bis MS-R33, Drag-and-drop, DAW-Wünsche)

Deine Nachprüfung von 0.1.7 ([CODEX_REVIEW_0.1.7.md](CODEX_REVIEW_0.1.7.md)) fand vier
Punkte. 0.1.8 behebt sie und bringt auf Wunsch des Nutzers Drag-and-drop für WAV-Dateien.
Einzelheiten und Tests stehen im [Build-Bericht 0.1.8](BUILD_REPORT_0.1.8.md) (Englisch).

**Die Grundentscheidung zu MS-R30/MS-R31:** Der Processor setzt sich zwei eigene Schranken
aus Ereignissen, die er selbst sieht: sein Trennen (`disconnect`) und sein Projektladen
(`setState`). Jeder Auftrag des Controllers trägt seinen Entstehungszeitpunkt
(`kAttrIssued`, `std::chrono::steady_clock`, eine Uhr für den ganzen Prozess). Abgewiesen
wird, was vor dem letzten Trennen entstand, und jeder Ladeauftrag von vor dem letzten
Projektladen. Beim Trennen lässt der Processor die noch gehaltenen Tasten selbst los; eine
späte Freigabe wird deshalb wie jeder späte Auftrag abgewiesen.

## Was du prüfst

* Repository `vst3_Plugins`, Branch `messier-0.1.0`, Worktree `.work/messier/wt` im
  Haupt-Checkout `D:\Eigene Dateien\Eigene Dokumente\Playground\vst3_Plugins`.
* **0.1.7 und 0.1.8 sind uncommittet** (Stand 2026-11-18) auf `46c3998` (gepusht).
  `git -C .work/messier/wt diff 46c3998` zeigt beides. Neu gegenüber deinem Stand von 0.1.7
  sind:
  * `source/processor.*` (`disconnect`, Schranken in `NotifyUnchecked`, `LoadState`,
    `MakeSampleOffset`);
  * `source/controller.*` (`NewMessage` mit `kAttrIssued`, `known_`);
  * `source/parameters.h`;
  * `source/file_drop.*` (neu), `source/map_view.*` (Hervorhebung beim Ziehen),
    `source/editor.*` (Ablageziele);
  * die Tests `TestLateRequest` (neu geschrieben), `TestReturnToProcessor` und
    `TestFileDrop` (beide neu).

  Falls der Nutzer vor deiner Prüfung committet, prüfe diesen Commit und nenne ihn.
* Das gestagte Bundle `out/messier-0.1.8/` ist aus genau diesem Stand gebaut
  (`SOURCE_SHA256SUMS.txt`).
* Das Atlas/Orion-Fenster ist entworfen, aber noch nicht gebaut (Planung, Zeile „Fenster“;
  `docs/GRAFIKAUFTRAG_GUI.md`). Das vorläufige Fenster bitte nicht optisch bewerten.

## Prüfpunkte

1. **MS-R30 bis MS-R33 nachprüfen.** Insbesondere:
   * Gibt es noch eine Reihenfolge von `disconnect`, `connect`, `setState` und
     verspäteten Nachrichten, in der ein alter Auftrag Laden, Reset, Note, Status oder
     Anzeige auslöst?
   * Kann umgekehrt ein gültiger Auftrag abgewiesen werden, etwa ein Ladeauftrag, der
     kurz nach einem Projektladen entsteht, oder Aufträge, wenn der Host den Processor
     nie trennt?
2. **Losgelassene Tasten beim Trennen:** Reicht der Platz in der Warteschlange immer? Kann
   eine Note hängen bleiben, oder eine fremde Note enden?
3. **Drag-and-drop:**
   * Auswahl der Datei (`PickDroppedWav`), Ausnahmegrenzen in `FileDropTarget`.
   * Lebensdauer: das Ziel hängt an Views, die mit dem Fenster gehen; die Rückrufe prüfen
     `alive_`.
   * Kann ein Drop etwas anderes tun als LOAD...?
4. **MS-R33:** Ist die Schranke (Zählerdifferenz 4099·m modulo 65535, für den nächsten
   Processor und dessen erste Ladung also 4100 Ladungen davor) richtig gerechnet und im
   README richtig beschrieben?
5. **DAW-Wünsche des Nutzers** (Build-Bericht, Abschnitt „From the user's DAW test“):
   * Speed hat 0x bei 12 Uhr, Pitch Random reicht bis ±12 Halbtöne; beides über `PotOf`
     vor dem unveränderten Kern. Ist `PotOf` an jeder Stelle angewandt, an der ein
     Parameter zum Potiwert wird (Laden, Automation, MIDI-CC, Anzeige, Eingabe, Restart)?
     Bleibt der Firmwarevergleich aussagekräftig?
   * Das WINDOW-Symbol und LATCH: Ausnahmegrenzen, gehaltene Noten beim Schließen des
     Fensters, beim Transponieren und beim Ausschalten.
   * Die Karte nimmt die Maus (`MapView::PressAt`, `DragTo`, `Release`, `Wheel`; im
     Editor als Bearbeitung von START und SPRAY): vollständige begin/perform/end-Paare,
     auch beim Abbruch der Maus und beim Schließen des Fensters während des Ziehens?
6. **Neue Fehler** durch die Änderungen, auch in den neuen Tests.

## Mein Stand

Matrix am 2026-11-18 auf genau diesem Stand, Logs in `out/messier-0.1.8/logs/`:

| Bau | ctest | Validator (alle vier Plugins) |
| --- | --- | --- |
| VS 2026 (MSVC 19.51) Release | 16/16 | 47/0 |
| VS 2026 Debug | 16/16 | 47/0 |
| VS 2022 (MSVC 19.44) Release | 16/16 | 47/0 |
| ASan (die fünf Messier-Testprogramme) | alle Exit 0, kein Bericht | – |

Firmware-Vergleich im Plugin mit denselben Potistellungen: 0 von 960.000 Samples
verschieden.

**Gegenproben:** Mit herausgenommenen Schranken scheitert `TestLateRequest` an deinem Fall
aus MS-R30. Mit herausgenommenem Gedächtnis für Processoren scheitern die Tests zur
Projektänderung (MS-R32).

## Bauen und testen

```text
cmake -S .work/messier/wt -B .work/messier-review/build -G "Visual Studio 18 2026" -A x64 -DVST3_SDK_ROOT="<Haupt-Checkout>/external/vst3sdk"
cmake --build .work/messier-review/build --config Release --parallel 4
ctest --test-dir .work/messier-review/build -C Release -R messier --output-on-failure
```

## Regeln

* Nur Code, Builds und Tests ohne Fenster. Keine Fenster-, DAW- oder Hörtests.
* Ändere nichts außer deiner Ergebnisdatei `instruments/messier/docs/CODEX_REVIEW_0.1.8.md`
  im Worktree. Kein Commit, kein Push, kein Branchwechsel, kein Stash. Eigene Build-Ordner
  danach entfernen.

## Ergebnis

Urteil (FREIGABE oder KEINE FREIGABE für den DAW-Test); Nachprüfung MS-R30 bis MS-R33 als
Tabelle (behoben, gegenstandslos, offen). Neue Befunde ab **MS-R34** mit Schwere P0 bis P3,
Beleg (Datei:Zeile), Auslösung, bewiesen oder vermutet und minimaler Korrektur; dann ohne
Befund Geprüftes, fehlende Tests, nicht Geprüftes.
