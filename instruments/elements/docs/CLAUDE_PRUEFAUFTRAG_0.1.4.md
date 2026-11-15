# Prüfauftrag an Claude: Mendelejew 0.1.4

Datum: 2026-11-15. Umsetzung und Eigenprüfung: Codex. Claude-Review: **offen**.

## Gegenstand und Auftrag

Bitte Änderungen **und Vorschläge** unabhängig prüfen. Ausgangspunkt ist
Commit `3634b5f8a4f41991792f9198e635cf410e276422` auf `elements-0.1.3`.
Die Änderungen liegen uncommitted im gemeinsamen Working Tree. Andere
Projekte und fremde uncommitted Dateien nicht verändern oder zurücksetzen.
Für die Prüfung einen eigenen Build unter `.work/mendelejew-0.1.4-review/`
verwenden, um parallele Kekulé-Builds nicht zu stören. Keine Installation,
Commits oder Veröffentlichung im Rahmen dieses Prüfauftrags.

Ergebnis bitte in `instruments/elements/docs/CLAUDE_REVIEW_0.1.4.md` festhalten,
mit konkreten Befunden, Schweregrad, Dateistelle und Prüfevidenz. Nicht von
Codex' Testergebnissen auf Korrektheit schließen.

## Maßgebliche Nutzerentscheidungen

* Tastatur tiefer starten und `<<` / `>>` für Oktavschritte bis ±24 Halbtöne.
  Umsetzung: C3–C5 als Start; relativ zur alten C4–C6-Basis −24 bis +24.
* Damping höher = stärkere Dämpfung. Bestehende Klänge erhalten.
* Freeze als Kreis mit Schneeflocke. Nach Nutzerkorrektur sitzt der Kreis
  **neben der SPACE-Beschriftung**, ohne Kontakt zum Reglerring.
* Farbschema an MI Elements anlehnen. Die vorhandenen Farben aus 0.1.3
  wurden beibehalten: Envelope/Bow weiß, Blow magenta, Strike türkis.
* Kopfzeile: **ENG · MOD · POLY · GEN · KEYS**, alle gleich groß.
* Nur ENG/MOD/GEN ersetzen den Inhaltsbereich. KEYS schaltet unabhängig
  davon die Tastatur. MOD/GEN sind ausdrücklich noch Funktionsplatzhalter.
* POLY später zyklisch **1 → 2 → 4 → 8 → 1**, ohne Panelwechsel. Bis eine
  echte Stimmenverwaltung existiert bleibt POLY deaktiviert.
* Für Polyphonie gelten die Ergebnisse von **Kekulé** als Grundlage.
  Keine eigenen Polyphonie- oder Headroom-Entscheidungen vorwegnehmen.
* Hohe Töne können abhängig von Geometry und geringer Dämpfung verzerren.
  Technisch untersuchen, **nicht zwingend beseitigen**. Niedrige beobachtete
  DAW-CPU ist eine Nutzerrückmeldung, kein gemessener 8-Stimmen-Nachweis.

## Umgesetzter Umfang: gezielt prüfen

1. `source/editor.*`, `widgets.*`, `view_layout.h`: korrekte Trefferflächen,
   Oktavgrenzen, Notenbeschriftung und MIDI-Noten; genau ein Note-off für
   den alten Ton beim Bereichswechsel, beim Ausblenden/Schließen keine
   hängenden Töne. Oktavbereich betrifft nur Bildschirmtastatur/MIDI out.
2. Damping: Anzeige und Knopf invertiert, ID 113 und rohe Normalisierung
   bleiben kompatibel. `controller.cpp` verwendet für Plain/Normalized die
   negative lineare Abbildung. Prüfen: Drehen, Scrollen, Default-Reset,
   Textwerte, Hostautomation, MIDI CC 23 und geladenes Altprojekt. Rohe
   Automation und CC behalten bewusst ihre alte Richtung. Dies ist eine
   explizit dokumentierte Kompatibilitätsentscheidung, bitte bewerten.
3. Navigation: genau eine der drei Ansichten aktiv; erneut auf aktiven
   Schalter klicken schaltet sie nicht ab. KEYS ändert weder Panel noch
   Stimme. POLY ist deaktiviert und ändert ebenfalls nichts. Versteckte
   Regler dürfen keine Eingaben erhalten; offene Automationsgesten enden
   sinnvoll beim Wechsel. Speziell `ApplySettings` während eines Edits prüfen.
4. Controller-State v2: Zoom, Sichtbarkeit, Oktave und Panel speichern;
   v1 bleibt lesbar; unbekannte/beschädigte Zustände atomar ablehnen.
   Klang-State bleibt bei v1, ebenso alle Parameter- und Klassen-IDs.
5. Freeze: sechszackige Vektorschneeflocke im Kreis, On/Off und Hostautomation
   funktionieren weiter. Letzte Positionsänderung nach der Sichtprüfung
   erfolgte auf Nutzerwunsch; **visuelle Prüfung bei 50/100/200 % noch offen**.
6. Keine Änderung in `core/`, Engine, Adapter oder Processor. Bestehende
   Sättigung wird untersucht, weder entfernt noch ersetzt. Provenienzprüfung
   und bisherige Klang-/Stabilitätsprüfungen müssen unverändert bestehen.

## Technische Untersuchung und Vorschläge bewerten

Siehe [DSP_INVESTIGATION_0.1.4.md](DSP_INVESTIGATION_0.1.4.md), einschließlich
Rohdaten und Grenzen der Aussagen. `MeasureHighNotes` ergänzt eine trockene
24-Fälle-Messung und einen isolierten Sinustest der vorhandenen SoftLimit-
Kennlinie. Bitte Reproduzierbarkeit und Schlussfolgerungen prüfen: Der
isolierte Test zeigt Aliasing, identifiziert aber nicht die Ursache des
konkreten Nutzerpatches; Ratenunterschiede sind kein Aliasbeweis. Der
96-kHz-Kern wird durch diese Prüfung nicht für das Plugin freigegeben.

Vorschläge sind noch **nicht implementiert**:

* MOD: Matrix mit LFOs; geeignete Parameterziele, Modulationstiefe und
  Glättung klanglich und technisch festlegen.
* POLY: Kekulé-Ergebnisse zu Stimmenvergabe, Ausklängen, Voice-Stealing,
  Pegelsummierung, CPU und Echtzeitfähigkeit übernehmen und anschließend
  für Elements validieren; Auswahl 1/2/4/8.
* GEN: melodische MIDI-Sequenzen, Stochastik und Probability. Verhalten bei
  Transport Stop, Note-off-Verantwortung, Hosttempo und MIDI-Ausgabe vor
  Implementierung festlegen. Reproduzierbare Zufallsfolgen wären sinnvoll.
* Optionaler Qualitätsmodus gegen Aliasing erst nach gezieltem Patch-
  Vergleich; charaktervolle bisherige Sounds sollen erhalten bleiben.

## Eigenprüfung und offene Punkte

* Release und Debug gebaut, jeweils VST3-Validator: 47 bestanden, 0 Fehler.
* `elements.dsp`, `elements.engine`, `elements.provenance` bestanden in
  Release und Debug. Die neue UI-Prüfung hatte zunächst fehlende VSTGUI-
  Initialisierung im Testprogramm; `InitModule`/`DeinitModule` korrigiert.
  Der Plugin-Test bestand danach separat in Release und Debug, einschließlich
  des letzten Stands der Gestenbehandlung beim geladenen Panel (finale Logs
  beachten). Die früheren Suite-Logs enthalten den inzwischen korrigierten
  Testinitialisierungsfehler; maßgeblich sind die separaten finalen Plugin-Logs.
* Testlogs liegen während der Übergabe in `out/elements-0.1.4/logs/`.
* Sichtprüfung im SDK-Testhost: Kopfzeile, ENG/MOD und Tastatur inklusive
  Oktavwechsel bestätigt. Computer Use wurde vom Nutzer per Escape beendet.
  GEN und die nachträglich korrigierte Freeze-Position wurden nicht erneut
  visuell geprüft. Keine neue DAW-Abnahme dieser Version behaupten.
* Keine installierte Kopie ersetzt. Review-Kandidat liegt in
  `out/elements-0.1.4/`; Quellstand im Working Tree ist maßgeblich.
