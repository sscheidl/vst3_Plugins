# Prüfauftrag für Codex: Messier 0.2.4 (GUI-Standard, MS-R53, MS-R54)

Dies ist ein Code-Review des eigenen Audio-Plugins des Nutzers (VST3, Granular-Looper).
Du liest Quelltext, baust und lässt Tests laufen; sonst nichts.

Dein Bericht zu 0.2.3 ([CODEX_REVIEW_0.2.3.md](CODEX_REVIEW_0.2.3.md)) gab die Freigabe
für den DAW-Test, mit MS-R53 und MS-R54 (P3). 0.2.4 behebt beide und setzt die Übergabe
vom 2026-11-19 um: den GUI-Standard mit einem Layout für alle vier Presets, DETAILS, die
START-Ausrichtung und die gelieferten Bitmaps und Farbtabellen. Einzelheiten stehen im
[Build-Bericht 0.2.4](BUILD_REPORT_0.2.4.md) (Englisch). Klang, Parameter und der Zustand
des Processors sind unverändert.

## Was du prüfst

* Repository `vst3_Plugins`, Branch `messier-gui`, Worktree `.work/messier-gui/wt`.
* 0.2.4 auf `086e178` (0.2.3). Ist 0.2.4 committet, prüfe diesen Commit und nenne ihn;
  sonst den Arbeitsstand (`git -C .work/messier-gui/wt diff 086e178`, dazu die neuen
  Bitmaps in `instruments/messier/resource/gui`).
* Die Übergabe: `out/Messier-Claude-Uebergabe-2026-11-19.zip` im Haupt-Checkout
  (`CLAUDE_AUFTRAG_MESSIER.md`, `design/GUI_STANDARD_TEMPLATE.md`, `presets/`).

## Prüfpunkte

1. **MS-R53 und MS-R54:** behoben, teilweise oder offen?
   * MS-R53: `MapView::ShowAt`. Die Audiozeit aus der Differenz von `rendered`
     (64 Samples bei 48 kHz je Quantum, Umlauf des Zählers), erst die laufenden, dann die
     beendeten Grains, jedes neue Grain höchstens einmal. Kann die Toleranz
     `0.01 + rate * kSameGrainSlack` falsch zuordnen?
   * MS-R54: `MapView::EndedFor` und seine Verwendung beim Zeichnen.
2. **Layout:** `gui_layout.h`, `Editor::OpenUnchecked`, `Editor::LayOut`,
   `SetDetailsVisible`, `PanelView`. Überlappt etwas? Stimmen Fenstergrößen, Zoom und
   Ziehen mit DETAILS (`EditorBaseHeight`, `ConstrainEditorSize`, `DragConstraint`)?
   Bleiben die verborgenen kleinen Regler ohne Maus?
3. **Zustand:** Controller-Zustand Version 3 (DETAILS als Byte am Ende). Werden
   Version 1 und 2 weiter gelesen, unbekannte Versionen abgewiesen?
4. **START:** `MapView::StartX` und `FrameX`. Steht der Griff bei festem START genau auf
   der linken Loop-Klammer?
5. **Bitmaps und Farben:** 72 PNG gegen die SHA-256-Liste des Pakets, `kPresets` gegen
   `GUI_PRESETS.inc`, die CMake-Liste.
6. **Neue Fehler** durch die Änderungen, auch in den Tests.

## Regeln

* Nur Code, Builds und Tests ohne Fenster. Das Aussehen beurteilt der Nutzer; die
  Vorschaubilder in `out/messier-0.2.4/preview/` darfst du ansehen.
* Ändere nichts außer deiner Ergebnisdatei `instruments/messier/docs/CODEX_REVIEW_0.2.4.md`
  im Worktree. Kein Commit, kein Push, kein Branchwechsel, kein Stash. Eigene Build-Ordner
  entfernst du danach.

## Ergebnis

* Urteil: FREIGABE oder KEINE FREIGABE für den DAW-Test.
* Nachprüfung von MS-R53 und MS-R54 als Tabelle.
* Neue Befunde ab **MS-R55**, jeweils mit Schwere P0 bis P3, Beleg (Datei:Zeile),
  Auslösung, bewiesen oder vermutet, minimaler Korrektur.
* Schreib die Ergebnisdatei früh an und ergänze sie, damit bei einem Abbruch etwas bleibt.
