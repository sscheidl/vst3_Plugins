# Prüfauftrag für Codex: Messier 0.2.5 und 0.2.6 (Plum, Gold, HUE, WAVE, MS-R55 bis MS-R57)

**Ersetzt durch [CODEX_PRUEFAUFTRAG_0.2.7.md](CODEX_PRUEFAUFTRAG_0.2.7.md)** (0.2.5 bis 0.2.7 zusammen).

Dies ist ein Code-Review des eigenen Audio-Plugins des Nutzers (VST3, Granular-Looper).
Du liest Quelltext, baust und lässt Tests laufen; sonst nichts.

Dein Bericht zu 0.2.4 ([CODEX_REVIEW_0.2.4.md](CODEX_REVIEW_0.2.4.md)) gab die Freigabe
mit MS-R55 bis MS-R57 (P3). 0.2.5 behebt sie und setzt die Übergabe „Plum, Gold, HUE und
TRAIL" um (`out/messier-claude-uebergabe-plum-gold/` im Haupt-Checkout). Einzelheiten
stehen im [Build-Bericht 0.2.5](BUILD_REPORT_0.2.5.md) (Englisch). Klang, Parameter und
der Zustand des Processors sind unverändert.

## Was du prüfst

* Repository `vst3_Plugins`, Branch `messier-gui`, Worktree `.work/messier-gui/wt`.
* 0.2.5 ist `6a2ce0c` auf `b7d7414` (0.2.4). Danach folgt 0.2.6 (der Blick des Nutzers
  auf 0.2.5, [Build-Bericht 0.2.6](BUILD_REPORT_0.2.6.md)): HUE dreht nur den Farbton,
  TRAIL ist fest, WAVE steht an seiner Stelle, Zustand Version 6. Prüfe 0.2.5 und 0.2.6
  zusammen, den Commit von 0.2.6, wenn es einen gibt, sonst den Arbeitsstand
  (`git -C .work/messier-gui/wt diff b7d7414`).

## Prüfpunkte

1. **MS-R55 bis MS-R57:** behoben, teilweise oder offen?
   * MS-R55: die Grain-Nummern in `LooperEngine::Impl::TrackGrains` (liest den Kern nur),
     `GrainDot::id`, die Anzeige mit 536 Byte, die Zuordnung in `MapView::ShowAt`. Kann
     eine Nummer einen Grain doppelt tragen oder ein neuer Grain eine alte Nummer
     erben? Bleibt der Klang bitgleich?
   * MS-R56: `MapView::StartX`.
   * MS-R57: `rest_at_` und `MapView::EndedFor`.
2. **HUE und WAVE (0.2.6):** `Skin::SetAccent` (nur der Farbton in HSL), `WaveSlider`,
   `Editor::SetWave`, `MapView::SetWave`, der feste `MapView::kTrail`. Zustand Version 4
   bis 6 (`Controller::getState`, `setState`): Version 5 wird gelesen, ihr Trail geprüft
   und verworfen; Version 6 trägt WAVE. Das Einklappen mit DETAILS.
3. **Plum und Gold:** die gespeicherten IDs 2 und 3, die 72 PNG gegen die SHA-256-Liste,
   `kPresets` gegen `GUI_PRESETS.inc`, die CMake-Liste, die entfernten Copper- und
   Photo-plate-Dateien, Golds Querlinien in `MapView::DrawUnchecked`.
4. **Neue Fehler** durch die Änderungen, auch in den Tests.

## Regeln

* Nur Code, Builds und Tests ohne Fenster. Das Aussehen beurteilt der Nutzer; die
  Vorschaubilder in `out/messier-0.2.5/preview/` darfst du ansehen.
* Ändere nichts außer deiner Ergebnisdatei `instruments/messier/docs/CODEX_REVIEW_0.2.5.md`
  im Worktree. Kein Commit, kein Push, kein Branchwechsel, kein Stash. Eigene Build-Ordner
  entfernst du danach.

## Ergebnis

* Urteil: FREIGABE oder KEINE FREIGABE für den DAW-Test.
* Nachprüfung von MS-R55 bis MS-R57 als Tabelle.
* Neue Befunde ab **MS-R58**, jeweils mit Schwere P0 bis P3, Beleg (Datei:Zeile),
  Auslösung, bewiesen oder vermutet, minimaler Korrektur.
* Schreib die Ergebnisdatei früh an und ergänze sie, damit bei einem Abbruch etwas bleibt.
