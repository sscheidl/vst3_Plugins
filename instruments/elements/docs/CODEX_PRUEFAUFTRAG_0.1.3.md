# Prüfauftrag an Codex: Mendelejew 0.1.3

Datum: 2026-11-15. Verfasst von Claude; die Prüfung macht Codex.

## Gegenstand

Branch `elements-0.1.3`, abgezweigt von `main` nach dem Merge von 0.1.2
(`b57f68c`). Der Stand 0.1.2 ist von Codex geprüft
([Nachprüfung 0.1.2](CODEX_REVIEW_0.1.2_NACHPRUEFUNG.md)).

0.1.3 ändert nur das Fenster: Auf Wunsch des Nutzers erinnern die Regler der
Anreger farblich an das Elements-Modul von Mutable Instruments. Contour und
Bow haben weiße Ringe, Blow magenta, Strike türkis, die Gruppennamen darüber
dieselbe Farbe. Die übrigen Regler bleiben orange. Die Farbtöne sind eigene,
für den dunklen Hintergrund gewählt; vom Panel der Hardware ist sonst nichts
übernommen.

Geänderte Dateien: `source/editor.cpp` (Farbkonstanten, ein Farbfeld in
`KnobSpec` und `GroupSpec`), `source/version.h`, `CMakeLists.txt`, dazu
CHANGELOG, README, DAW-Prüfliste, `docs/PLANUNG.md` und die beiden
Projekttabellen.

## Bitte prüfen

1. Der Diff gegen `b57f68c` ändert nur Farben und Version; keine Änderung
   an DSP, Engine, Adapter, Parametern, Zustand oder Audiopfad.
2. Jeder Regler und jede Gruppe bekommt die beschriebene Farbe; die
   Zuordnung in `kKnobs` und `kGroups` stimmt mit den Parametern überein.
3. README, CHANGELOG und DAW-Prüfliste beschreiben die Änderung zutreffend.
   Die Hommage-Notiz im README darf keine Verbindung zu Mutable Instruments
   nahelegen und keinen Namen, kein Logo und keine Panel-Grafik verwenden.
4. Build und Tests in Release und Debug: Validator, die regulären
   Elements-Tests; die anderen Plugins bleiben grün.

Ob die Farben gefallen und gut lesbar sind, prüft der Nutzer im Fenster
selbst (Prüfliste Nr. 3a); das gehört nicht zu dieser Prüfung.

## Regeln

Wie im [Prüfauftrag zu 0.1.0](CODEX_PRUEFAUFTRAG_0.1.0.md): eigener Build in
`.work/elements-review/`, keine installierte Kopie benutzen, keine
Produktdateien ändern, keine Commits, Pushes oder GitHub-Kommentare.
Ergebnis bitte nach `instruments/elements/docs/CODEX_REVIEW_0.1.3.md`.
