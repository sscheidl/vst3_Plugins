# Prüfauftrag für Codex: Messier 0.2.1 (MS-R40 bis MS-R43 und der erste Blick des Nutzers)

Deine Prüfung von 0.2.0 ([CODEX_REVIEW_0.2.0.md](CODEX_REVIEW_0.2.0.md)) fand vier Punkte.
Der Nutzer hat 0.2.0 in Studio One angesehen und acht Wünsche genannt. 0.2.1 setzt alles
um. Was wo geändert ist und wie getestet wurde, steht im
[Build-Bericht 0.2.1](BUILD_REPORT_0.2.1.md) (Englisch, Tabelle am Anfang). Der Klang ist
unverändert.

## Was du prüfst

* Repository `vst3_Plugins`, Branch `messier-gui`, Worktree `.work/messier-gui/wt` im
  Haupt-Checkout `D:\Eigene Dateien\Eigene Dokumente\Playground\vst3_Plugins`.
* 0.2.1 auf `37d4466` (0.2.0, Code `17a6970`, gepusht). Ist 0.2.1 bei deiner Prüfung
  committet, prüfe diesen Commit und nenne ihn; sonst den Arbeitsstand
  (`git -C .work/messier-gui/wt diff 37d4466`).
* Geändert: `source/map_view.*` (Weiterlaufen zwischen Anzeigen, Loop aus den Parametern,
  Wellenform auf der Platte, Nebel, Schneeflocke, `FollowGrains`), `source/editor.*`
  (`Animate`, `PressReset`, `PopUpMenu`, `MakePresetMenu`, `MakeSizeMenu`, `ApplySkin`,
  `SetGuiPreset`, `ApplySettings`), `source/gui_skin.*` (`Sans`, `plate_wave`),
  `source/gui_layout.h`, `source/panel_view.cpp`, `source/widgets.cpp`; Test
  `TestWindow021` (neu).
* Das gestagte Bundle `out/messier-0.2.1/` ist aus genau diesem Stand gebaut
  (`SOURCE_SHA256SUMS.txt`, `BUILD_INFO.txt`).

## Prüfpunkte

1. **MS-R40 bis MS-R43 nachprüfen:** behoben, teilweise, offen? Zu MS-R40: Dein Test mit
   einem Rahmen ohne Plattformfenster stürzt in diesem Testprogramm in VSTGUI ab
   (`COptionMenu::popup` erreicht den fehlenden Plattformrahmen); bitte `PopUpMenu` im Code
   beurteilen. Zu MS-R42: Ich habe statt des einfachen Auswahlstils den Mehrfachstil mit
   Häkchen je Eintrag genommen; trägt der Win32-Pfad das?
2. **Weiterlaufen zwischen Anzeigen** (`MapView::ShowAt`, `FollowPlayhead`, `DrawUnchecked`):
   Kann das Weiterlaufen Positionen außerhalb von Datei oder Loop zeichnen, über eine
   Freeze-, Stop- oder Dateigrenze hinweg weiterlaufen, oder nach einem Projektladen alte
   Geschwindigkeiten benutzen? Stimmt die Ruheerkennung (gleiche Anzeige länger als 0,1 s)?
3. **Loop aus den Parametern:** Stimmt `LoopStart`/`LoopLength` mit dem Gesetz in
   `LooperEngine::Display` überein (START und SIZE ohne `PotOf`-Kurve)? Gibt es Fälle, in
   denen Platte und Kern dauerhaft auseinanderliegen?
4. **Herzschlag** (`Editor::Animate`): Fragt das Fenster jetzt zu oft nach der Anzeige,
   etwa bei Automation vieler Parameter oder beim RESET? Kann `Animate` aus
   `ParameterChanged` eine Ausnahme oder einen Rückruf in einen halb gebauten oder
   geschlossenen Zustand tragen?
5. **RESET** (`PressReset`): Werden genau START, die 14 Regler und Freeze gesetzt, als
   vollständige Edits, und bleibt die Reset-Nachricht wie bei MS-R08? Ist es richtig, dass
   Parameter 121 (Automation) nur die Abspielposition zurücksetzt?
6. **Wellenform-Cache** (`DrawWave`): Lebensdauer und Schlüssel (Übersicht, Skin,
   Gerätemaßstab, Größe); Verhalten bei fehlgeschlagenem Offscreen-Kontext.
7. **Neue Fehler** durch die Änderungen, auch in den Tests (VSTGUI wird in `TestWindow021`
   für die Menüs initialisiert und wieder beendet).

## Mein Stand

Matrix am 2026-11-18 auf genau diesem Stand, Logs in `out/messier-0.2.1/logs/`:

| Bau | ctest | Validator (alle vier Plugins) |
| --- | --- | --- |
| VS 2026 (MSVC 19.51) Release | 16/16 | 47/0 |
| VS 2026 Debug | 16/16 | 47/0 |
| VS 2022 (MSVC 19.44) Release | 16/16 | 47/0 |
| ASan (die fünf Messier-Testprogramme) | alle Exit 0, kein Bericht | – |

Gegenprobe: Die Übereinstimmungsregel für Grain-Schweife (MS-R43) habe ich einmal
herausgenommen; der neue Test scheiterte dann.

## Bauen und testen

```text
cmake -S .work/messier-gui/wt -B .work/messier-review/build -G "Visual Studio 18 2026" -A x64 -DVST3_SDK_ROOT="<Haupt-Checkout>/external/vst3sdk"
cmake --build .work/messier-review/build --config Release --parallel 4
ctest --test-dir .work/messier-review/build -C Release -R messier --output-on-failure
```

## Regeln

* Nur Code, Builds und Tests ohne Fenster. Keine Fenster-, DAW- oder Hörtests; das Aussehen
  beurteilt der Nutzer.
* Ändere nichts außer deiner Ergebnisdatei `instruments/messier/docs/CODEX_REVIEW_0.2.1.md`
  im Worktree. Kein Commit, kein Push, kein Branchwechsel, kein Stash. Eigene Build-Ordner
  danach entfernen.

## Ergebnis

Urteil (FREIGABE oder KEINE FREIGABE für den DAW-Test); Nachprüfung MS-R40 bis MS-R43 als
Tabelle; neue Befunde ab **MS-R44** mit Schwere P0 bis P3, Beleg (Datei:Zeile), Auslösung,
bewiesen oder vermutet und minimaler Korrektur; dann ohne Befund Geprüftes, fehlende Tests,
nicht Geprüftes.
