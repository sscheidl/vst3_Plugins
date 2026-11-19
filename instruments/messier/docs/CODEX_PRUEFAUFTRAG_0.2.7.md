# Prüfauftrag für Codex: Messier 0.2.5 bis 0.2.7

Dies ist ein Code-Review des eigenen Audio-Plugins des Nutzers (VST3, Granular-Looper).
Du liest Quelltext, baust und lässt Tests laufen; sonst nichts.

Dieser Auftrag ersetzt [CODEX_PRUEFAUFTRAG_0.2.5.md](CODEX_PRUEFAUFTRAG_0.2.5.md): 0.2.5 und
0.2.6 sind noch nicht geprüft, 0.2.7 kommt hinzu. Einzelheiten stehen in den
Build-Berichten [0.2.5](BUILD_REPORT_0.2.5.md), [0.2.6](BUILD_REPORT_0.2.6.md) und
[0.2.7](BUILD_REPORT_0.2.7.md) (Englisch). Der Klang und der Zustand des Processors sind
unverändert.

## Was du prüfst

* Repository `vst3_Plugins`, Branch `messier-gui`, Worktree `.work/messier-gui/wt`.
* 0.2.5 ist `6a2ce0c` auf `b7d7414` (0.2.4). 0.2.6 und 0.2.7 sind der Arbeitsstand
  darauf, oder ihr Commit, wenn es einen gibt (`git -C .work/messier-gui/wt diff b7d7414`).
* Dazu gehören deine eigenen Änderungen für den Skin-Test vom 19.11.2026 (Abschnitt
  „Codex handoff" im Build-Bericht 0.2.6). Claude hat sie übernommen, ohne sie zu ändern.
  Prüf sie wie fremden Code.

## Prüfpunkte

1. **MS-R55 bis MS-R57 (0.2.5):** behoben, teilweise oder offen?
   * MS-R55: die Grain-Nummern in `LooperEngine::Impl::TrackGrains`, `GrainDot::id`, die
     Anzeige mit 536 Byte, die Zuordnung in `MapView::ShowAt`. Bleibt der Klang bitgleich?
   * MS-R56: `MapView::StartX`. MS-R57: `rest_at_` und `MapView::EndedFor`.
2. **0.2.6:** `Skin::SetAccent` (nur der Farbton in HSL), `WaveSlider`, `Editor::SetWave`,
   `MapView::SetWave`, der feste `MapView::kTrail`, `ApplyColourTable` (seit 0.2.7 im
   normalen Build, siehe Punkt 6).
3. **Deine Skin-Test-Änderungen:** das Gehäuse in voller Höhe mit Zuschnitt
   (`PanelView::DrawUnchecked`), Plums WAVE-Vorgabe mit `kPresetWave` und Zustand
   Version 7 (`EditorSettings::WavePercent`, `Controller::setState`), die Menüreihenfolge
   `kPresetMenuOrder`.
4. **Der Dateiname in Studio One (0.2.7):** Studio One behält kein Binärattribut der
   Länge 0. Der Status trug bisher vier Texte, auch leere. Der Controller lehnte den
   ganzen Status ab, wenn einer fehlte, und das Fenster behielt den Namen aus dem Projekt.
   Jetzt in `source/message_text.h`: Ein leerer Text wird nicht gesendet, ein fehlender
   gilt als leer (`GetOptionalText`). Der Pfad eines Ladeauftrags bleibt Pflicht
   (`GetText`). Prüfe:
   * Gibt es in irgendeiner Nachricht noch ein Attribut, das leer sein kann und
     beim Fehlen die Nachricht verwirft?
   * Kann ein fehlender Text etwas Falsches anzeigen? Ein Beispiel wäre ein Status ohne
     Pfad, der den Namen löscht, obwohl eine Datei spielt.
   * Der Test `TestHostWithoutEmptyTexts` mit einem Host, der leere Binärattribute
     verwirft: Er muss ohne die Korrektur scheitern.
5. **Der Dateiname aufrecht (0.2.7):** `skin.Sans(18)` statt `skin.Italic(19)`,
   `kTruncateTail`. Passt ein langer Name vor die Katalognummer?
6. **Eigene Skins in jedem Build (0.2.7, Wunsch des Nutzers):** Der Schalter
   `MESSIER_SKIN_FOLDER` ist weg. `SkinFolders()` nennt erst `Documents\Messier-Skin`
   (`UserSkinFolder`, `SHGetKnownFolderPath`), dann `Messier-Skin` neben dem Bundle
   (`BundleSkinFolder`). `Skin::Skin` nimmt je Datei den ersten passenden Ordner, sonst die
   eingebaute Bitmap. `BitmapFromFile` und `ReadSkinTable` werfen nie und begrenzen die
   Dateigröße. Das Palettenmenü hat „Reload skin folders" und „Open skin folder"
   (`OpenUserSkinFolder`). Prüfe:
   * Kann eine Datei im Ordner das Fenster leer lassen, abstürzen lassen oder das Layout
     verzerren? Eine Datei mit falscher Größe, eine 1x-Datei unter dem 2x-Namen, eine
     kaputte oder leere Datei, ein Ordner an Stelle der Datei, eine riesige Datei?
   * Tests und Vorschau setzen `UseSkinFolders({})`. Hängt sonst noch ein Test davon ab,
     was auf dem Rechner liegt?
   * `TestCustomSkin` initialisiert COM für WIC. Im Host tut das der Host. Gibt es einen
     Host-Pfad ohne COM?
7. **Neue Fehler** durch die Änderungen, auch in den Tests.

## Regeln

* Nur Code, Builds und Tests ohne Fenster. Das Aussehen beurteilt der Nutzer. Die
  Vorschaubilder in `out/messier-0.2.7/preview/` darfst du ansehen.
* Ändere nichts außer deiner Ergebnisdatei `instruments/messier/docs/CODEX_REVIEW_0.2.7.md`
  im Worktree. Kein Commit, kein Push, kein Branchwechsel, kein Stash. Eigene Build-Ordner
  entfernst du danach.

## Ergebnis

* Urteil: FREIGABE oder KEINE FREIGABE für den DAW-Test.
* Nachprüfung von MS-R55 bis MS-R57 als Tabelle.
* Neue Befunde ab **MS-R58**, jeweils mit Schwere P0 bis P3, Beleg (Datei:Zeile),
  Auslösung, bewiesen oder vermutet, minimaler Korrektur.
* Schreib die Ergebnisdatei früh an und ergänze sie, damit bei einem Abbruch etwas bleibt.
