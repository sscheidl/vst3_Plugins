# Prüfauftrag an Codex: Mendelejew 0.1.4, Damping-Umkehr

Datum: 2026-11-15. Verfasst von Claude; die Prüfung macht Codex.

## Gegenstand

Branch `elements-0.1.4`:

1. `6f2b090`: Codex' 0.1.4, unverändert so committet, wie Codex sie im
   Arbeitsbaum hinterlassen hat. Von Claude geprüft:
   [CLAUDE_REVIEW_0.1.4.md](CLAUDE_REVIEW_0.1.4.md).
2. Der Commit danach: Claudes Umsetzung der Nutzerentscheidung zu CL-R01
   („Ja, wie Kekulé") und die Behebung von CL-R02. **Nur dieser Commit ist
   zu prüfen**; er ist Code von Claude.

Was sich ändert, steht im Nachtrag des Reviews. Kurz: Damping hat im
Parameter selbst den neuen Sinn (höher = stärker gedämpft, Default 0,75),
die Engine bekommt `1 − x` über `EngineValue`, der Klang-Zustand ist
Version 2, und Version 1 wird beim Lesen gespiegelt. Vorbild ist Kekulé
0.2.0 (`instruments/torus-classic/source/parameters.h:128`,
`state_io.h:34`). Die Sonderbehandlung von Damping im Fenster und im
LinearParameter entfällt. POLY hat keinen Callback mehr.

## Bitte prüfen

1. Jeder Weg vom Parameter zur Engine geht über `EngineValue`:
   Automation (`QueueParameter`), Neustart (`Restart`), Aktivierung,
   Zustandsladen. Es darf keinen Weg geben, auf dem die Engine den
   normalisierten Damping-Wert ungespiegelt bekommt, und keinen, auf dem
   er doppelt gespiegelt wird.
2. Ein Zustand der Version 1 klingt nach dem Laden wie in 0.1.3: im
   Prozessor und im Controller gleich gespiegelt, danach als Version 2
   gespeichert. Version 2 wird nicht gespiegelt. Unbekannte Versionen,
   kurze und ungültige Zustände werden weiter abgelehnt, ohne etwas zu
   ändern.
3. Defaults: Parameter-Default 0,75, Engine-Default weiter 0,25;
   `DefaultOf` und `kDefaults` passen für alle 20 Parameter zusammen.
4. Fenster: Knopf, Default-Reset, Mausrad, Wertanzeige und Texteingabe von
   Damping ohne Spiegelung; MIDI CC 23 dreht wie der Knopf.
5. CL-R02: POLY löst nichts aus; ENG/MOD/GEN schalten weiter die Panels
   0/1/2.
6. Die angepassten Tests prüfen das Richtige; besonders, dass die
   Testpatches mit 0,4 und 0,2 dasselbe klingen wie zuvor mit 0,6 und 0,8.
7. README, CHANGELOG und DAW-Prüfliste beschreiben die Umkehr und ihre
   Folge für alte Automation zutreffend.
8. Build und Tests in Release und Debug; Validator; die anderen Plugins
   bleiben grün; der DSP-Messbericht unverändert gegenüber `6f2b090`.

Nicht Gegenstand: Codex' eigene Änderungen in `6f2b090` (die hat Claude
geprüft) und die Optik (prüft der Nutzer).

## Regeln

Wie im [Prüfauftrag zu 0.1.0](CODEX_PRUEFAUFTRAG_0.1.0.md): eigener Build in
`.work/elements-review/`, keine installierte Kopie benutzen, keine
Produktdateien ändern, keine Commits, Pushes oder GitHub-Kommentare.
Ergebnis bitte nach `instruments/elements/docs/CODEX_REVIEW_0.1.4.md`.
