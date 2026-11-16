# Prüfauftrag an Codex: Toy Piano Generative 0.2.0

Datum: 2026-11-16. Verfasst von Claude; die Prüfung macht Codex. Der gesamte
Code ist von Claude.

## Gegenstand

Branch `toy-piano-generative-0.2.0`, auf `toy-piano-generative-0.1.0` bei
`dd4695a`. 0.1.0 ist noch nicht geprüft. Bitte beides in einem Durchgang:

1. 0.1.0 nach [CODEX_PRUEFAUFTRAG_0.1.0.md](CODEX_PRUEFAUFTRAG_0.1.0.md),
   verglichen mit `1195c78`.
2. Die Änderungen von 0.2.0: `git diff dd4695a..toy-piano-generative-0.2.0`.

Ergebnis bitte nach `instruments/toy-piano-generative/docs/CODEX_REVIEW_0.2.0.md`,
Befunde ab **TG-R01** wie im Auftrag zu 0.1.0, je mit der Version, in der sie
entstanden sind.

## Was 0.2.0 ändert

Vom Nutzer nach seinem ersten Test von 0.1.0 verlangt (2026-11-16):

* **Mutationssperre.** `State::mutation_lock` im Kern; `Tick()` ruft
  `Mutate()` nicht, solange sie gesetzt ist. Parameter 121 „Mutation Lock",
  ein Schloss-Symbol unten rechts am Mutation-Regler (`LockButton` in
  `keyboard_view.*`, gezeichnet wie Kekulés Stimmsperre).
* **Namen der Algorithmen** Drift, Runs, Ladder, Breathe, Focus
  (`kAlgorithms` in `parameters.h`); Parameter 111 ist jetzt eine Liste, die
  auch Zahlen annimmt (`AlgorithmParameter`). Tooltip über dem Namen und dem
  Regler mit der Erklärung.
* **Zehn weitere Skalen**, 17 insgesamt, in neuer Reihenfolge (`kScales` im
  Kern, `kScaleNames` im Plugin).
* **Mutation nach Durchläufen.** `State::mutate_every` (0 bis 16) und
  `passes` im Kern: bei 0 wie bisher ein Versuch je Tick; sonst zählt
  `Advance()` volle Durchläufe, und der fällige beginnt mit `MutatePass()`,
  allen Versuchen eines Durchlaufs auf einmal. Parameter 122 „Mutate Every",
  ein kleiner Regler links unten an Mutation.
* **Abdunkeln bei Ladder** (`Editor::UpdateDimming`).
* **Zustand Version 2**: je ein Byte für Sperre und „Mutate Every". Version 1 wird gelesen; ihre
  sieben Skalen werden auf die neuen Nummern umgesetzt
  (`kScaleModesOfVersion1` in `state_io.h`), die Sperre ist offen.

## Bitte prüfen

1. Sperre: Kann ein Weg das Muster bei geschlossener Sperre doch ändern
   (Algorithmen 1, 2, 4, 5; Laden eines Zustands; die Übergabe zwischen den
   Threads in `handover.h`, die jetzt sechs Worte hat)? Bleibt der
   Zufallsgenerator stehen, und ist das so gewollt dokumentiert?
2. Mutation nach Durchläufen: Kommt ein fälliger Durchlauf genau einmal, auch
   wenn Länge oder Teiler mitten im Lauf wechseln? Ist `MutatePass()` für Focus
   (Position je Schritt) und Runs gleichwertig zu einem Durchlauf mit einem
   Versuch je Tick? Die Zahl der Durchläufe wird nicht gespeichert: richtig?
3. Dass Tonic und Scale bei geschlossener Sperre weiter wirken, steht im
   README als Festlegung. Widerspricht etwas im Code oder Fenster dem?
4. Zustand: Version 1 und 2 vollständig geprüft, in Processor und Controller
   gleich; eine Skala ≥ 7 in Version 1 wird abgelehnt; jede Abschneidung
   abgelehnt. Stimmt `kStateSize` und `kStateSizeVersion1` mit dem Format?
5. Die Skalenlisten in `kScales` gegen die Tabelle im README und gegen
   `kLists` in `tests/test_sequencer.cpp`. Passen `kOddMode` und
   `kTonicOnlyMode` überall, auch in den Tests, die früher feste Nummern
   hatten?
6. `AlgorithmParameter::fromString`: Namen ohne Rücksicht auf Groß- und
   Kleinschreibung, Zahlen 1 bis 5 geklemmt, alles andere abgelehnt, ohne den
   Ausgabewert zu ändern.
7. Fenster: `mutation_lock_`, der kleine Regler (Eintrag 14 in `knobs_`, ohne Namenszeile) und das Abdunkeln über `setAlphaValue`; in `open`, `close` und im Fehlerweg; der Tooltip
   setzt voraus, dass das Wertfeld des Algorithmus die Maus annimmt. Kann es
   dadurch Klicks schlucken oder anders stören? `AlgorithmTooltip` liest das
   Attribut über `getAttribute`: korrekt?
8. README, CHANGELOG und DAW-Prüfliste: stimmen sie mit dem Code überein? Die
   Folge für alte Automation von Scale ist benannt; ist sie richtig
   beschrieben?
9. Öffentlicher Text nennt weder das Originalgerät noch dessen Hersteller.

## Regeln

Wie im Auftrag zu 0.1.0: eigener Build in `.work/toy-piano-generative-review/`,
keine Produktdateien ändern, keine Commits, Pushes oder GitHub-Kommentare.
