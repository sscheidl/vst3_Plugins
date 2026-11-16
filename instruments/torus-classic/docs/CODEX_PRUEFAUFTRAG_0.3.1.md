# Prüfauftrag für Codex: Kekulé 0.3.1 (Nachprüfung)

Nachprüfung deines Reviews von 0.3.0 ([CODEX_REVIEW_0.3.0.md](CODEX_REVIEW_0.3.0.md))
und Prüfung der zwei Ergänzungen von 0.3.1. Umsetzung beschrieben in
[CODEX_REVIEW_0.3.0_UMSETZUNG.md](CODEX_REVIEW_0.3.0_UMSETZUNG.md).

## Was du prüfst

* Repository `vst3_Plugins`, Branch `kekule-0.3.0` (auf GitHub gepusht).
* **Geprüfter Stand: Commit `27ea200`** („Kekulé 0.3.1: no Plug-in Nap,
  octave keys, Mendelejew colours“). Der Commit danach fügt nur diesen
  Auftrag hinzu.
* Dein geprüfter Stand von 0.3.0 war eine uncommittete Arbeitskopie auf
  `490eca8` (Merge von 0.2.2). Alles seit 0.2.2 zeigt
  `git diff 490eca8 27ea200`. Der Commit `3e2b6a3` dazwischen ist ein
  ungebauter Zwischenstand, den der Nutzer vor einem Sitzungslimit sichern
  ließ; gegen ihn ändern sich nur Tests und Doku.
* Der Worktree liegt unter `.work/kekule-0.3.0/wt` im Haupt-Checkout
  `D:\Eigene Dateien\Eigene Dokumente\Playground\vst3_Plugins`. Prüfe dort
  oder in einem eigenen Checkout des Commits.

## Prüfpunkte

1. **TC-R11 bis TC-R13.** Ist die Ruhe-Meldung vollständig entfernt? Fester
   `kInfiniteTail`; kein `AtRest()` mehr in Engine, Adapter und Prozessor;
   keine exakten Nullen in Ruhe. Ist der Adapter (`adapter/`) byte-gleich mit
   `490eca8`? Bleibt mit dem Wegfall ein Rest der drei Befunde, etwa über die
   Silence-Flags (unverändert seit 0.2.2)?
2. **Bitgleichheit.** Der Firmware-Vergleich in Engine- und Plugin-Test hat
   keine Ruhe-Ausnahme mehr. Ist er wieder so streng wie bis 0.2.2?
3. **Engine 0.2.1.** Bank 0 läuft wie die Firmware, Zusatzbänke werden nach
   `kQuietQuanta` unter `kQuietLevel` weggelegt (`engine/synth.cpp`). Ist
   beim Entfernen der Ruhe etwas am Bankverhalten von 0.3.0 kaputtgegangen?
4. **TC-R14.** Ist die Beschreibung in README (Abschnitt Voices),
   `engine/synth.h` und `docs/PLANUNG.md` zutreffend und vollständig?
5. **Oktavtasten** (aus Mendelejew 0.1.4 übernommen):
   `source/view_layout.h` (`ClampKeyboardTranspose`, Start −12),
   `source/widgets.{h,cpp}` (`KeyboardView::SetTranspose`,
   `ToggleButton::Style::Action`), `source/editor.{h,cpp}`
   (`SetKeyboardTranspose`, `ApplySettings`, `LayOut`, `ForgetViews`),
   `source/controller.cpp` (Fensterzustand Version 2, Version 1 lesbar,
   ungültige Transposition abgelehnt). Wird eine gehaltene Taste vor dem
   Verschieben losgelassen, auch beim Laden eines Projekts mit anderer Lage
   und beim Schließen des Fensters? Kann ein Note-off mit falscher Tonhöhe
   oder ein hängendes Note-on entstehen? Prozessor und Controller nehmen
   Noten 0 bis 127 an; die Klaviatur reicht von 36 bis 108.
6. **Farben.** Drei Konstanten in `source/editor.cpp`, Werte wie in
   Mendelejew (`instruments/elements/source/editor.cpp` auf `main`). Nur am
   Code.
7. **Versionen.** Produkt und Shell 0.3.1, Engine 0.2.1, Adapter 0.2.1;
   `components.json`, `SHA256SUMS.upstream`, `version.h`, `CMakeLists.txt`.
   Provenance-Test.
8. **Doku.** README, CHANGELOG, UPSTREAM, DAW-Liste (Abschnitt 9), PLANUNG:
   keine stehengebliebene Aussage über Ruhe, Nap oder „bitgenau außer in
   Ruhe“.

## Bekannt und gewollt

* Kein Plugin-Nap. Entscheidung des Nutzers nach deinem Review.
* Die Klaviatur startet bei C3–C5 statt C4–C6, wie Mendelejew. Alte
  Projekte (Fensterzustand Version 1) öffnen dort.
* Die Altersreihenfolge über Bänke nach einer Erhöhung der Stimmenzahl
  bleibt ungenau (TC-R14); der vendorte Kern bleibt unverändert.
* Keine Fenstertests der Oktavtasten; Optik und Bedienung prüft der Nutzer.

## Bauen und testen

Der Worktree hat kein SDK; nimm das des Haupt-Checkouts und einen eigenen
Build-Ordner:

```text
cmake -S .work/kekule-0.3.0/wt -B .work/torus-classic-review/build -G "Visual Studio 18 2026" -A x64 -DVST3_SDK_ROOT="<Haupt-Checkout>/external/vst3sdk"
cmake --build .work/torus-classic-review/build --config Release --parallel 4 --target Kekule test_torus_classic_engine test_torus_classic_adapter test_torus_classic_plugin
ctest --test-dir .work/torus-classic-review/build -C Release -R torus-classic --output-on-failure
```

Meine Läufe zu `27ea200` (VS 2026, MSVC 19.51): Release und Debug, Validator
47/0, `ctest -R torus-classic` 4/4. Logs im Test-Build
`out/torus-classic-0.3.1/logs/`.

## Regeln

* Nur Code, Builds und Tests ohne Fenster. Keine Fenster-, DAW- oder
  Hörtests.
* Ändere nichts außer deiner Ergebnisdatei
  `instruments/torus-classic/docs/CODEX_REVIEW_0.3.1.md` im Worktree
  `.work/kekule-0.3.0/wt`. Kein Commit, kein Push, kein Branchwechsel, kein
  Stash, weder im Worktree noch im Haupt-Checkout.
* Den Haupt-Checkout und `.work/elements-review/` nicht verändern. Eigene
  Build-Ordner danach entfernen.

## Ergebnis

Wie beim letzten Mal: Urteil (FREIGABE oder KEINE FREIGABE), Nachprüfung
TC-R11 bis TC-R14 als Tabelle (behoben, gegenstandslos, offen), neue Befunde
ab **TC-R15** mit Schwere P0 bis P3, Beleg (Datei:Zeile), Auslösung,
bewiesen oder vermutet und minimaler Korrektur; dann ohne Befund Geprüftes,
fehlende Tests, nicht Geprüftes.
