# Prüfauftrag für Codex: Messier 0.1.1 (Nachprüfung)

Nachprüfung deines Reviews von 0.1.0 ([CODEX_REVIEW_0.1.0.md](CODEX_REVIEW_0.1.0.md)).
Umsetzung beschrieben in [CODEX_REVIEW_0.1.0_UMSETZUNG.md](CODEX_REVIEW_0.1.0_UMSETZUNG.md).

## Was du prüfst

* Repository `vst3_Plugins`, Branch `messier-0.1.0`, Worktree `.work/messier/wt` im
  Haupt-Checkout `D:\Eigene Dateien\Eigene Dokumente\Playground\vst3_Plugins`.
* Dein geprüfter Stand war `26b8509`. **0.1.1 ist uncommittet** in diesem Worktree (Stand
  2026-11-18); `git -C .work/messier/wt diff 26b8509` zeigt alle Änderungen, `git status`
  dazu die drei neuen Dokumente (dein Review, die Umsetzung, dieser Auftrag). Falls der
  Nutzer vor deiner Prüfung committet, prüfe `git diff 26b8509 <Commit>` und nenne ihn.
* Das gestagte Bundle `out/messier-0.1.1/` ist aus genau diesem Stand gebaut
  (`SOURCE_SHA256SUMS.txt`).
* MS-R05 ist nicht Teil dieser Nachprüfung: Der Nutzer hat entschieden, dass das
  Atlas/Orion-Fenster später kommt, wenn Aufteilung und Farben feststehen; das Fenster
  von 0.1.1 ist ausdrücklich vorläufig. Prüfe nur, ob die Dokumente das richtig sagen.

## Prüfpunkte

1. **MS-R01:** Gibt es noch einen Weg, auf dem eine Ausnahme den Ladethread, `setState`,
   `getState` oder eine Nachricht verlässt? Ist der Ersatzweg in `FileLoader::Finish`
   wirklich frei von Allokationen? Hält die Reserve-Datei, was sie soll? Ist die
   Begründung tragfähig, den 16-Byte-Fall nur im Release-Bau zu testen (Debug-STL legt
   in `noexcept`-Konstruktoren Proxies an, siehe Umsetzung)?
2. **MS-R02:** Kann der Audio-Thread jetzt noch Werte eines Projekts mit einer Datei
   eines anderen übernehmen, bei Rückstau, bei mehreren `setState` hintereinander, bei
   einer Ladung aus dem Fenster während einer Projektladung, in `setActive`? Bleibt
   `process()` frei von Allokation, Freigabe und Sperren? Kann ein Projekt für immer
   warten?
3. **MS-R03:** Kommt jeder Druck an, auch zwei schnell hintereinander und beim Schließen
   des Fensters? Sind `beginEdit` und `endEdit` immer paarig?
4. **MS-R04:** Genau eine Meldung je erfolgreicher Datei aus dem Fenster, keine beim
   Projektladen, bei Statusabfragen, bei Fehlern?
5. **Neue Fehler** durch die Änderungen, auch in den neuen Tests (prüfen sie, was sie
   behaupten?).

## Bekannt und gewollt

* Wie im Prüfauftrag 0.1.0, dazu die Grenzen aus der Umsetzung: Fenster-Datei gegen
  gleichzeitige Projektladung; bei völligem Speichermangel übernimmt ein Projekt die
  bisherige Datei und behält seinen Pfad nicht.
* Das Fenster ist vorläufig (MS-R05).

## Bauen und testen

```text
cmake -S .work/messier/wt -B .work/messier-review/build -G "Visual Studio 18 2026" -A x64 -DVST3_SDK_ROOT="<Haupt-Checkout>/external/vst3sdk"
cmake --build .work/messier-review/build --config Release --parallel 4
ctest --test-dir .work/messier-review/build -C Release --output-on-failure
```

Meine Läufe stehen in der Umsetzung; die Logs liegen unter `out/messier-0.1.1/logs/`.

## Regeln

* Nur Code, Builds und Tests ohne Fenster. Keine Fenster-, DAW- oder Hörtests.
* Ändere nichts außer deiner Ergebnisdatei `instruments/messier/docs/CODEX_REVIEW_0.1.1.md`
  im Worktree. Kein Commit, kein Push, kein Branchwechsel, kein Stash. Eigene Build-Ordner
  danach entfernen.

## Ergebnis

Urteil (FREIGABE oder KEINE FREIGABE für den DAW-Test), Nachprüfung MS-R01 bis MS-R04 als
Tabelle (behoben, gegenstandslos, offen), neue Befunde ab **MS-R06** mit Schwere P0 bis P3,
Beleg (Datei:Zeile), Auslösung, bewiesen oder vermutet und minimaler Korrektur; dann ohne
Befund Geprüftes, fehlende Tests, nicht Geprüftes.
