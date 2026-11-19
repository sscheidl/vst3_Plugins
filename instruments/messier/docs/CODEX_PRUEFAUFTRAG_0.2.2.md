# Prüfauftrag für Codex: Messier 0.2.2 (MS-R44 bis MS-R47 und die Designrunde Slate)

Deine Prüfung von 0.2.1 ([CODEX_REVIEW_0.2.1.md](CODEX_REVIEW_0.2.1.md)) gab keine Freigabe:
MS-R44 (P2), MS-R45 bis MS-R47 (P3) und Reste zu MS-R40, MS-R41 und MS-R43. Danach hat der
Nutzer eine Designrunde für das Preset Slate beauftragt: zwölf Punkte und fünf
Entscheidungen vom 2026-11-19. 0.2.2 setzt beides um. Was wo geändert ist und wie es
getestet wurde, steht im [Build-Bericht 0.2.2](BUILD_REPORT_0.2.2.md) (Englisch). Der
Klang ist unverändert.

## Was du prüfst

* Repository `vst3_Plugins`, Branch `messier-gui`, Worktree `.work/messier-gui/wt` im
  Haupt-Checkout `D:\Eigene Dateien\Eigene Dokumente\Playground\vst3_Plugins`.
* 0.2.2 ist Commit `34a5a70` (lokal, nicht gepusht) auf `69aa73b` (0.2.1, gepusht). Der
  Unterschied: `git -C .work/messier-gui/wt diff 69aa73b 34a5a70`, darin auch die neuen
  Dateien `instruments/messier/tests/preview_window.cpp` und
  `instruments/messier/tools/make_metal_bitmaps.py`. Ein späterer Commit, der nur diesen
  Prüfauftrag ändert, gehört nicht zur Prüfung.
* Der Designauftrag liegt im Haupt-Checkout unter
  `out/messier-gui-entwurf-2026-11-19/CLAUDE_AUFTRAG_MESSIER_GUI.md`, mit dem Entwurfsbild
  daneben. Der Text gilt vor dem Bild.
* Das gestagte Bundle `out/messier-0.2.2/` ist aus genau diesem Stand gebaut
  (`SOURCE_SHA256SUMS.txt`, `BUILD_INFO.txt`). Die Fenstervorschauen liegen in
  `out/messier-0.2.2/preview/`.

## Prüfpunkte

1. **MS-R44 bis MS-R47 und die Reste nachprüfen:** behoben, teilweise oder offen?
   * **MS-R44:** `Editor::AnimateLater`, `Editor::Animate`, `~Editor`,
     `Controller::ForEachEditor` und der Poll-Abstand im Controller. Kann ein Aufruf aus
     dem Controller noch einen abgemeldeten, halb gebauten oder zerstörten Editor
     erreichen?
     * Kann die Aufgabe in `Tasks::mainQueue` nach `close()`, nach einem neuen `open()`
       oder nach der Zerstörung des Editors Schaden anrichten? Sie prüft `alive_` (den
       Editor) und `window_serial_` (das Fenster, für das sie gestellt wurde).
   * **MS-R45/MS-R43:** Die Anzeige trägt jetzt die eigenen Geschwindigkeiten des Kerns
     (`EngineDisplay::head_speed`, `GrainDot::speed`, `phase_rate`, `rendered`; 456
     Bytes, Engine 0.1.5). Stimmen die Einheiten mit `LooperEngine::Display` und dem
     Kern überein: Frames der Datei pro Sekunde, Fensterphase pro Sekunde, ohne das
     Grain-FM des Eingangs? Bleibt der Klang bitgleich?
     * Prüft `display_data.cpp` die neuen Felder vollständig?
   * **MS-R46:** `MapView::ShownLoop` mit `kPreview` (300 ms).
   * **MS-R47:** `DrawWave`: Neuversuch nach 2 s oder bei neuem Schlüssel; drei Teile
     einzeln, seit der Designrunde mit 16 %, 40 % und 16 %.
2. **Nachleuchten der Grains** (`MapView::ShowAt`, `Keep`, `DrawTrail`, `Afterglows`):
   * Kann ein Grain doppelt leuchten, als beendetes und als laufendes?
     * Ein beendetes Grain wird an Pan, Geschwindigkeit und Fensterrate erkannt; diese
       drei Werte gelten ein Grain-Leben lang.
     * Kann das bei gleichen Werten zweier Grains falsch zuordnen, und mit welcher Folge?
   * Stimmt die Lebensdauer (Ende plus `kTrail`) über Ruhe, STOP, Freeze und einen
     Dateiwechsel hinweg? Ein Dateiwechsel muss alles verwerfen.
   * Ist das Feld mit 48 Einträgen und dem Ersetzen des ältesten Eintrags in Ordnung?
3. **Nebeldrift** (`DriftTarget`, `AdvanceDrift`, `SetStopped`):
   * Folgt der Drift nur dem wirksamen, vorzeichenbehafteten Tempo, nie der Leseposition?
     Wendet ein Umlauf von Loop oder Datei den Drift nie?
   * Kommt er bei 0×, STOP und Freeze zur Ruhe?
   * Bleibt `drift_offset_` über lange Zeit numerisch harmlos (`fmod` auf 1e6)?
   * Die Wiederholung der Nebelbitmaps über die Platte: Kann dort eine Lücke oder ein
     Sprung entstehen?
4. **Layout und Bedienung:**
   * SPRAY als großer Regler mit demselben Parameter, LATCH/KEYS unter SPRAY, LATCH ohne
     Klaviatur gedimmt und ohne Maus (`Editor::LayOut`), die Oktavtasten und ihre
     Grenzen.
   * Die Spur des Dreiecks: `gui_layout.h`, Plattenunterkante und Fenstergröße
     unverändert. Gespeicherte Größen aus 0.2.x müssen weiter passen.
   * Ohne Schrauben: Die Schraubenbitmaps sind aus Bundle, CMake und `SkinImageId`
     entfernt; der Test zählt 24 Bitmaps.
5. **Farben:** Hat jede frühere Verwendung von `accent` jetzt die richtige der drei Farben
   `control`, `value` oder `marker`, wie im Auftrag (Punkt 7)? Die noch nicht gelieferten
   Presets benutzen für alle drei ihre alte Akzentfarbe.
6. **Werkzeuge:**
   * `tools/make_metal_bitmaps.py`: Liefert es die Dateien in `resource/gui`
     reproduzierbar (gleiche Hashes)?
   * `tests/preview_window.cpp` (nicht in ctest): Ist es als Handwerkzeug sauber, auch
     COM, Ressourcenpfad und `removed()`?
7. **Neue Fehler** durch die Änderungen, auch in den Tests (`TestWindow022`).

## Mein Stand

Matrix am 2026-11-19 auf genau diesem Stand, Logs in `out/messier-0.2.2/logs/`:

| Bau | ctest | Validator (alle vier Plugins) |
| --- | --- | --- |
| VS 2026 (MSVC 19.51) Release | 16/16 | 47/0 |
| VS 2026 Debug | 16/16 | 47/0 |
| VS 2022 (MSVC 19.44) Release | 16/16 | 47/0 |
| ASan (die fünf Messier-Testprogramme) | alle Exit 0, kein Bericht | – |

Firmwarevergleich bitgleich (0 von 960.000 Samples verschieden). Die Fenstervorschauen
habe ich angesehen (Bericht, Abschnitt „Window previews“). Bewegung zeigen sie nicht.

## Bauen und testen

```text
cmake -S .work/messier-gui/wt -B .work/messier-review/build -G "Visual Studio 18 2026" -A x64 -DVST3_SDK_ROOT="<Haupt-Checkout>/external/vst3sdk"
cmake --build .work/messier-review/build --config Release --parallel 4
ctest --test-dir .work/messier-review/build -C Release -R messier --output-on-failure
```

## Regeln

* Nur Code, Builds und Tests ohne Fenster. Keine Fenster-, DAW- oder Hörtests; das
  Aussehen beurteilt der Nutzer. Die Vorschaubilder darfst du ansehen.
* Ändere nichts außer deiner Ergebnisdatei `instruments/messier/docs/CODEX_REVIEW_0.2.2.md`
  im Worktree. Kein Commit, kein Push, kein Branchwechsel, kein Stash. Eigene Build-Ordner
  entfernst du danach.

## Ergebnis

* Urteil: FREIGABE oder KEINE FREIGABE für den DAW-Test.
* Nachprüfung von MS-R44 bis MS-R47 und der Reste als Tabelle.
* Neue Befunde ab **MS-R48**, jeweils mit:
  * Schwere P0 bis P3;
  * Beleg (Datei:Zeile);
  * Auslösung;
  * bewiesen oder vermutet;
  * minimaler Korrektur.
* Danach: ohne Befund Geprüftes, fehlende Tests, nicht Geprüftes.
