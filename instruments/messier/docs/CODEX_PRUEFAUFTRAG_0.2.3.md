# Prüfauftrag für Codex: Messier 0.2.3 (Fenster)

Dies ist ein Code-Review des eigenen Audio-Plugins des Nutzers (VST3, Granular-Looper).
Du liest Quelltext, baust und lässt Tests laufen; sonst nichts.

Dein Bericht zu 0.2.2 ([CODEX_REVIEW_0.2.2.md](CODEX_REVIEW_0.2.2.md)) gab keine Freigabe:
MS-R48 (P2) und MS-R49 bis MS-R52 (P3). 0.2.3 behebt alle fünf und setzt Punkt 13 des Designauftrags um
(Beugungsstrahlen der Grains, Färbung nach PITCH). Der Klang ist unverändert. Einzelheiten
stehen im [Build-Bericht 0.2.3](BUILD_REPORT_0.2.3.md) (Englisch).

## Was du prüfst

* Repository `vst3_Plugins`, Branch `messier-gui`, Worktree `.work/messier-gui/wt`.
* 0.2.3 ist `cc35386` und `a77041c` (MS-R48) auf `085d8ce` (0.2.2), gepusht:
  `git -C .work/messier-gui/wt diff 085d8ce a77041c`. Ein späterer Commit, der nur diesen
  Prüfauftrag ändert, gehört nicht zur Prüfung.
* Der Designauftrag, Punkt 13: `out/messier-gui-entwurf-2026-11-19/CLAUDE_AUFTRAG_MESSIER_GUI.md`
  im Haupt-Checkout.

## Prüfpunkte

1. **MS-R48 bis MS-R52:** behoben, teilweise oder offen? MS-R48: `Editor::OpenUnchecked`
   ruft jetzt `AnimateLater` statt direkt `poll`. Lass dein Prüfprogramm
   gegen 0.2.3 laufen; wo eine Prüfung jetzt anders ausgehen muss, sag, ob das neue
   Verhalten stimmt.
   * Nachleuchten: `MapView::ShowAt` mit `kSameGrainSlack`.
   * Späte Wellenform: der Zweig für die gleiche Anzeige in `ShowAt`.
   * Drift ohne Anzeigen: `AdvanceDrift` mit `kDriftStaleAfter`.
   * Nebelkacheln: `drift_a_`, `drift_b_` und die Kachelperiode ohne Schwellen.
   * Leser: `kMinPhaseRate` in `display_data.cpp`, der Vergleich in Sekunden in `ShowAt`.
2. **Strahlen und Färbung:** `MapView::PitchTint`, `SetPitch`, die Strahlen im Zeichnen der
   Grains, `Editor::ParameterChanged` und `open()`. Entspricht die neutrale Farbe dem
   Nullwert, den der PITCH-Regler anzeigt? Bleibt die Sättigung begrenzt?
3. **Neue Fehler** durch die Änderungen, auch in den Tests.

## Regeln

* Nur Code, Builds und Tests ohne Fenster. Das Aussehen beurteilt der Nutzer; die
  Vorschaubilder in `out/messier-0.2.3/preview/` darfst du ansehen.
* Ändere nichts außer deiner Ergebnisdatei `instruments/messier/docs/CODEX_REVIEW_0.2.3.md`
  im Worktree. Kein Commit, kein Push, kein Branchwechsel, kein Stash. Eigene Build-Ordner
  entfernst du danach.

## Ergebnis

* Urteil: FREIGABE oder KEINE FREIGABE für den DAW-Test.
* Nachprüfung von MS-R48 bis MS-R52 als Tabelle.
* Neue Befunde ab **MS-R53**, jeweils mit Schwere P0 bis P3, Beleg (Datei:Zeile),
  Auslösung, bewiesen oder vermutet, minimaler Korrektur.
* Schreib die Ergebnisdatei früh an und ergänze sie, damit bei einem Abbruch etwas bleibt.
