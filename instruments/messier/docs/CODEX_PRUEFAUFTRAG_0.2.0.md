# Prüfauftrag für Codex: Messier 0.2.0 (das neue Fenster)

0.2.0 ersetzt das vorläufige Fenster von 0.1.4 bis 0.1.11 durch das Fenster der
Fensterstudie, Fassung 3, mit dem GUI-Preset Slate aus deinen Bitmaps
(`out/messier-slate-2026-11-18/assets/`). Was wo geändert ist, welche Entscheidungen der
Nutzer und ich getroffen haben und wie getestet wurde, steht im
[Build-Bericht 0.2.0](BUILD_REPORT_0.2.0.md) (Englisch, Tabelle am Anfang). Der Klang ist
unverändert.

## Was du prüfst

* Repository `vst3_Plugins`, Branch `messier-gui`, Worktree `.work/messier-gui/wt` im
  Haupt-Checkout `D:\Eigene Dateien\Eigene Dokumente\Playground\vst3_Plugins`.
* **0.2.0 ist uncommittet** (Stand 2026-11-18) auf `8543254` (0.1.11, gepusht auf
  `messier-0.1.0`). `git -C .work/messier-gui/wt diff 8543254` und
  `git -C .work/messier-gui/wt status` zeigen alles Neue (neue Dateien sind untracked).
  Falls der Nutzer vor deiner Prüfung committet, prüfe diesen Commit und nenne ihn.
* Neu: `source/gui_skin.*`, `source/gui_layout.h`, `source/panel_view.*`,
  `resource/gui/` (26 PNG, unverändert aus deiner Lieferung). Geändert:
  `source/editor.*`, `source/map_view.*` (weitgehend neu), `source/widgets.*`
  (`MetalKnob` neu, Zeichnen mit Skin), `source/controller.cpp`, `source/parameters.h`
  (Fensterzustand Version 2), `source/wave_overview.*`, `source/display_data.cpp`
  (Abtastrate in der Übersicht), `CMakeLists.txt`, `components.json`; Tests
  `TestGuiPresets` (neu), Anpassungen in `TestViewLayout`, `TestDisplayPath` und im
  Ladertest.
* Das gestagte Bundle `out/messier-0.2.0/` ist aus genau diesem Stand gebaut
  (`SOURCE_SHA256SUMS.txt`, `BUILD_INFO.txt`).

## Prüfpunkte

1. **Fensterzustand Version 2** (`Controller::getState`/`setState`): Version 1 wird
   gelesen und öffnet mit Slate, Version 2 trägt das Preset. Entsteht bei irgendeinem
   beschädigten oder abgeschnittenen Zustand ein Teilzustand? Sind alte Songs (0.1.x)
   unbeeinträchtigt?
2. **Übersicht mit Abtastrate** (`WaveOverview::rate`, `ReadOverviewBytes`): reist kein
   ungesetztes Byte mit (Padding)? Ist die Prüfung der Rate vollständig, und bleibt die
   Zuordnung von Übersicht und Anzeige (Dateinummer, Länge) wie bisher?
3. **Ausnahmegrenzen** (MS-R37 gilt weiter): Ist jeder neue Weg von VSTGUI, Host oder
   System in den Code des Fensters abgesichert, auch die Rückrufe der beiden Menüs
   (`COptionMenu::popup`), `ApplySkin`, `SetGuiPreset` und das Anlegen eines `Skin`
   (Bitmaps, Pixelzugriff, Schriften)? Bleibt nach einer Ausnahme mitten im Wechsel des
   Presets etwas halb zurück?
4. **Lebensdauer:** Ansichten halten den Skin als `shared_ptr`; Schriften bleiben für die
   Lebensdauer des Skins gültig (`Skin::FontOf`). Kann eine Ansicht nach `close()` oder
   nach einem Presetwechsel auf etwas Freigegebenes zugreifen? Kann der Rückruf eines
   Menüs ein zerstörtes Fenster erreichen?
5. **Grains als Sternschnuppen** (`MapView::FollowGrains`): Die Anzeige trägt keine
   Grain-Nummern; das Fenster ordnet Grains über Panorama, Fensterphase und Position zu.
   Kann das falsche Schweife oder Abstürze erzeugen (Dateiwechsel, Umlauf am Dateiende,
   gleiche Panoramen, mehr Grains als zuvor, Zeitsprünge)? Bleiben alle Indizes in
   Grenzen? Ist der Ansatz vertretbar, oder sollte der Datenweg eine Grain-Nummer tragen?
6. **Bitmaps:** Werden nur Bitmaps in den Maßen des Grafikauftrags genommen und fehlende
   als Ersatzfläche gezeichnet? Wählt das Fenster 1x und 2x richtig, auch unter dem Zoom
   des Rahmens? Stimmen die Neun-Felder-Teile (Ränder 12 und 8) und die Tönung der Nebel
   mit geradem Alpha? Liegen genau die 26 Dateien im Bundle und sonst nichts aus der
   Lieferung?
7. **Fensterlogik:** Größenmenü, Griff, Fensterrand und Projektladen ändern den Zoom
   weiter nur über `Resize()`/`onSize()`; LATCH und die Oktavtasten verschwinden mit der
   Klaviatur; der START-Schieber folgt dem Parameter (auch aus Automation).
8. **Neue Fehler** durch die Änderungen, auch in den Tests.

## Mein Stand

Matrix am 2026-11-18 auf genau diesem Stand, Logs in `out/messier-0.2.0/logs/`:

| Bau | ctest | Validator (alle vier Plugins) |
| --- | --- | --- |
| VS 2026 (MSVC 19.51) Release | 16/16 | 47/0 |
| VS 2026 Debug | 16/16 | 47/0 |
| VS 2022 (MSVC 19.44) Release | 16/16 | 47/0 |
| ASan (die fünf Messier-Testprogramme) | alle Exit 0, kein Bericht | – |

Firmware-Vergleich im Plugin: 0 von 960.000 Samples verschieden.

Angesehen: einmal im `editorhost` des SDK (Anzeige 120 %, also die 2x-Bitmaps), ohne
geladene Datei. Alle Bitmaps laden, keine Nähte oder Säume zu sehen. Mit Sample, die
Menüs und die 1x-Bitmaps habe ich nicht gesehen; das ist Teil des DAW-Tests des Nutzers.

## Bauen und testen

```text
cmake -S .work/messier-gui/wt -B .work/messier-review/build -G "Visual Studio 18 2026" -A x64 -DVST3_SDK_ROOT="<Haupt-Checkout>/external/vst3sdk"
cmake --build .work/messier-review/build --config Release --parallel 4
ctest --test-dir .work/messier-review/build -C Release -R messier --output-on-failure
```

## Regeln

* Nur Code, Builds und Tests ohne Fenster. Keine Fenster-, DAW- oder Hörtests; das Aussehen
  beurteilt der Nutzer.
* Ändere nichts außer deiner Ergebnisdatei `instruments/messier/docs/CODEX_REVIEW_0.2.0.md`
  im Worktree. Kein Commit, kein Push, kein Branchwechsel, kein Stash. Eigene Build-Ordner
  danach entfernen.

## Ergebnis

Urteil (FREIGABE oder KEINE FREIGABE für den DAW-Test); neue Befunde ab **MS-R40** mit
Schwere P0 bis P3, Beleg (Datei:Zeile), Auslösung, bewiesen oder vermutet und minimaler
Korrektur; dann ohne Befund Geprüftes, fehlende Tests, nicht Geprüftes.
