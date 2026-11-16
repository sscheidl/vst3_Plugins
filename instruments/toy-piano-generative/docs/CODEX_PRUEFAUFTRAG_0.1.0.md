# Prüfauftrag an Codex: Toy Piano Generative 0.1.0

Datum: 2026-11-16. Verfasst von Claude; die Prüfung macht Codex
(Vier-Augen-Prinzip). Der gesamte Code dieses Plugins ist von Claude.

## Gegenstand

Branch `toy-piano-generative-0.1.0`, abgezweigt von `main` bei `1195c78`
(Merge Mendelejew 0.1.4). Verglichen wird gegen diesen Commit, nicht gegen den
aktuellen Stand von `main`.

Neu ist das Verzeichnis `instruments/toy-piano-generative/`. Außerhalb davon
ändern sich nur: eine Zeile im Wurzel-`CMakeLists.txt`, je eine Zeile in
`README.md`, `instruments/README.md` und `THIRD_PARTY_NOTICES.md`, ein
Abschnitt in `docs/PLANUNG.md`. Toy Piano Classic ist unverändert; das neue
Plugin linkt dessen Target `toy_piano_core`.

Was das Plugin tun soll, steht im [README](../README.md): Abschnitte „The
sequencer's rules", „Transport", „Keys that transpose" und „Choices and
deviations". Die Verhaltensspezifikation, nach der der Sequencer gebaut ist,
liegt nicht im Repository; maßgeblich für die Prüfung ist das README. Ob das
README die Spezifikation trifft, ist nicht Gegenstand.

## Herkunft des Codes

| Teil | Herkunft |
| --- | --- |
| `sequencer/` | neu |
| `source/step_clock.h`, `handover.h`, `state_io.h` | neu |
| `source/processor.*` | Kopie aus Toy Piano Classic 0.3.2, weitgehend neu geschrieben |
| `source/engine.*` | Kopie, ergänzt um die Stimmung je Stimme (`TuningRatio`, `tuning_`) |
| `source/controller.*`, `parameters.h`, `editor.*`, `keyboard_*` | Kopie, erweitert |
| `source/value_text.h`, `factory.cpp` | Kopie, nur Namensraum |
| `tests/test_sequencer.cpp` | neu |
| `tests/test_plugin.cpp` | Kopie, angepasst; ab `StateStream` neu |

Die Kopien sind in Toy Piano Classic bereits geprüft
(`instruments/toy-piano-classic/docs/CODEX_REVIEW_0.3.0.md` und Nachprüfung).
Bitte dort nur die Änderungen prüfen: `git diff --no-index
instruments/toy-piano-classic/source instruments/toy-piano-generative/source`.

## Bitte prüfen

### Sequencer-Kern

1. Stimmt `generative_sequencer.cpp` mit den Regeln im README überein:
   Ziehen, Skalenlisten, Neuziehen mit Ersatznote, Transponieren, Reihenfolge
   vorrücken → mutieren → spielen, die fünf Algorithmen, Teiler, Länge,
   Durchlauf-Flag?
2. Zufall: `Below` ohne Schiefe, `Chance` in ganzen Prozent, der Lauf von
   Algorithmus 2 mit der Wahrscheinlichkeit Mutation × Notes.
3. Grenzen: Kann ein Schrittwert `int8` verlassen (wiederholtes
   Transponieren, Läufe)? Bleibt `position` immer unter 16, auch wenn Länge
   oder Teiler mitten im Lauf kleiner werden?
4. `IsValid` und `Load`: Wird jeder Zustand abgelehnt, mit dem der Kern
   außerhalb seiner Felder lesen oder durch null teilen könnte?
5. Die statistischen Tests: Sind die erwarteten Werte unabhängig vom
   geprüften Code hergeleitet (`NoteChance`, `DrawChance` gegen die im Test
   erneut getippten Listen)? Ist die Schranke von fünf Standardabweichungen
   bei `CheckCount` gerechtfertigt (Summe ungleicher Bernoulli-Versuche)?

### Takt und Echtzeit

6. `StepClock`: jede Sechzehntel genau einmal und in Reihenfolge, auch bei
   Tempowechsel zwischen zwei Blöcken, bei Sprüngen, bei Start mitten in einer
   Sechzehntel, ohne Tempo und ohne Songposition. Gibt es eine Folge von
   Blöcken, bei der ein Schritt doppelt kommt, ausfällt oder sich Schritte
   stauen und dann gebündelt kommen? Ist die Sprungschwelle von einer
   Viertel-Sechzehntel sinnvoll?
7. `Processor::Render`: Reihenfolge von Sechzehnteln und Noten-Events am
   selben Sample; unsortierte oder zu große `sampleOffset`; Blockgrößen 1 bis
   2048. Kein Schritt darf vor seinem Sample wirken.
8. Im Audio-Thread keine Allokation, keine Sperre, kein blockierender Aufruf.
   `Report` ruft `addParameterData` des Hosts auf: vertretbar?
9. Neustart: bei jedem Transportstart, beim Einschalten von RUN, nach
   `setActive`. Gibt es einen Weg, auf dem das Muster ohne Neustart weiterläuft
   oder doppelt neu startet?

### Zustand und Threads

10. `SharedWords` (`handover.h`): Ist die Sequenzsperre mit atomaren Worten
    korrekt für einen Schreiber und mehrere Leser? Kann `TakeRequestedState`
    einen halb geschriebenen oder veralteten Zustand übernehmen?
11. `getState`/`setState` gegen `process`: Die Anfragenummer
    (`request_number_`, `taken_number_`, `GenerationOf`) entscheidet, ob der
    veröffentlichte Zustand schon der geladene ist. Gibt es eine Verschränkung,
    in der `getState` einen Zustand liefert, der älter ist als ein bereits
    zurückgekehrtes `setState`? Zwei `setState` kurz hintereinander?
12. Laden zieht kein neues Muster: Der Skalenparameter des Hosts darf nach
    dem Laden kein `ChooseScaleMode` auslösen. Stimmt das auch, wenn der Host
    nach dem Laden alle Parameter noch einmal sendet?
13. `state_io.h`: Wird jeder abgeschnittene, falsch versionierte oder
    außerhalb der Bereiche liegende Zustand ganz abgelehnt, in Processor und
    Controller gleich? NaN in den Double-Feldern?
14. Der Test für gleichzeitiges Lesen und Laden (`TestSequencerState`, letzter
    Block): Prüft er genug, oder nur, dass nichts abstürzt?

### Transponieren und Stimmung

15. `TranspositionForKey`, `KeyForTransposition`, `SoundingNote`: Grenzen C1
    und H6, Tasten außerhalb, Falten über Note 127.
16. MIDI-Note mit TRANSPOSE an: wirkt am Sample der Note, klingt nicht, wird
    über `outputParameterChanges` gemeldet. Bildschirm-Taste: geht als
    `beginEdit`/`performEdit`/`endEdit` über den Controller. Können sich die
    Werte von Controller und Processor dauerhaft unterscheiden?
17. Oktavknöpfe: Klaviatur und Oktave klemmen getrennt. Ist das im Fenster
    widerspruchsfrei (`Editor::ShiftKeyboard`, die als Taster benutzten
    `ToggleButton`)?
18. `TuningRatio`: „nächstes ganzes Hertz darunter" für alle 128 Noten; der
    Faktor ist bei 0 % exakt 1, sodass der bitgenaue Vergleich mit dem
    Firmware-Kern weiter gilt. Die Zuordnung Stimme ↔ Note in `Engine::NoteOn`
    über `MidiVoiceNote`: immer eindeutig?

### Hülle

19. Parameter: Stufenregel (`StepIndex`) in Processor, Controller, Anzeige und
    Texteingabe gleich; Listen- und Bereichsparameter; die Kennlinie des
    Mutation-Reglers hin und zurück.
20. Fenster: neue Zeiger (`run_button_`, `transpose_button_`, `octave_down_`,
    `octave_up_`) in `open`, `close` und im Fehlerweg; `controlBeginEdit` und
    `controlEndEdit` sind neu gegenüber Classic.
21. `/fp:precise` für Kern, Hülle und Tests: nötig, ausreichend, und bleibt
    der bitgenaue Vergleich mit `toy_piano_core` (mit `/fp:fast` gebaut)
    belastbar?

### Dokumente

22. README, CHANGELOG und DAW-Prüfliste: Stimmt jede Aussage mit dem Code
    überein? Steht irgendwo mehr, als geprüft wurde?
23. Öffentlicher Text nennt weder das Originalgerät noch dessen Hersteller und
    enthält keine Firmware-Adressen oder Befundnummern. Bitte bestätigen.

## Nicht Gegenstand

Die Optik des Fensters und das Verhalten in Studio One (prüft der Nutzer);
der unveränderte Klangkern; ob die Regeln im README dem Original entsprechen.

## Regeln

Eigener Build in `.work/toy-piano-generative-review/`, mit
`-DVST3_SDK_ROOT=<Haupt-Checkout>/external/vst3sdk`. Keine installierte Kopie
benutzen, keine Produktdateien ändern, keine Commits, Pushes oder
GitHub-Kommentare. Befunde ab **TG-R01**, mit Priorität P1 bis P3, Datei und
Zeile, und einem Weg, sie nachzustellen. Ergebnis bitte nach
`instruments/toy-piano-generative/docs/CODEX_REVIEW_0.1.0.md`.
