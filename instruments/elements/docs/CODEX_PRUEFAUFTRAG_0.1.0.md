# Prüfauftrag an Codex: Mendelejew 0.1.0 (Projekt Elements, Phase 1, nur Code)

* **Stand:** 2026-11-14
* **Name:** Das Plugin heißt „Mendelejew" (Bundle `Mendelejew.vst3`).
  „Elements" ist nur noch der interne Projektname: Verzeichnis, Branch,
  Build-Targets, Namensräume und Testnamen.
* **Gegenstand:** `instruments/elements/` im Repo
  [sscheidl/vst3_Plugins](https://github.com/sscheidl/vst3_Plugins), Branch
  `elements-0.1.0`, dazu die wenigen Zeilen außerhalb davon (`CMakeLists.txt`,
  READMEs, `THIRD_PARTY_NOTICES.md`, `docs/PLANUNG.md`). Der Branch setzt auf
  `torus-classic-0.1.0` auf; `git diff torus-classic-0.1.0...elements-0.1.0`
  zeigt genau diese Arbeit. Zu prüfen ist der letzte Commit des Branches, der
  mit der Umbenennung; Pull Request
  [#2](https://github.com/sscheidl/vst3_Plugins/pull/2).
* **Verfasser des Codes:** Claude (Fable 5.2)
* **Grundlage:** der Arbeitsauftrag „Phase 1 – Mutable Instruments Elements
  als natives VST3" (seine Anforderungen stehen zusammengefasst im Pull
  Request und in Abschnitt 6 des Build-Berichts), das
  [README](../README.md), [UPSTREAM.md](../UPSTREAM.md), der
  [Build-Bericht](BUILD_REPORT_0.1.0.md) und der Abschnitt „Mendelejew" in
  [docs/PLANUNG.md](../../../docs/PLANUNG.md)
* **Ergebnis nach:** `instruments/elements/docs/CODEX_REVIEW_0.1.0.md`

## Auftrag und Grenzen

Unabhängige Prüfung des Codes vor einem Merge (Vier-Augen-Prinzip). Es gelten
die Regeln der Prüfaufträge zu Torus Classic:

* Nichts ändern außer der Ergebnisdatei. Eigenes Build-Verzeichnis unter
  `.work/elements-review/`; `build/`, `dist/`, `out/` und installierte Kopien
  nicht anfassen.
* Behauptungen aus README, UPSTREAM.md und Build-Bericht am Code und durch
  eigene Läufe prüfen, nicht übernehmen. Bewiesenes von Vermutetem trennen.
* Befunde nummerieren (EL-R01, …) mit Schwere, Fundstelle, Nachweis und
  Vorschlag. Kein Befund ist auch ein Ergebnis; dann bitte sagen, was geprüft
  wurde.
* Nicht Gegenstand: Aussehen und Bedienung des Fensters und das Verhalten in
  einer DAW (prüft der Nutzer), Toy Piano Classic und Torus Classic (außer:
  wurden sie verändert?).

Die Originalquellen für eigene Vergleiche:
`pichenettes/eurorack` bei `f4cfa7697095a28d292bff758beb51deb49b5a86`,
`pichenettes/stmlib` bei `e3bd7c9cc00e4364166f9905c0509b6ffd0535ec`.

## Was behauptet wird

1. `core/` ist bei 32 kHz das Original, bitgenau (`TestBitExactAt32k`).
2. Bei 48 kHz rechnet die DSP nativ: kein 32-kHz-Kern mit Wandler dahinter.
3. Bei 48 und 96 kHz hat jede Resonatormode Frequenz, Ausklang und Pegel wie
   bei 32 kHz; Verzögerungen, Hall, Röhre und Erreger sind in der Zeit
   umgerechnet. Was trotzdem abweicht, steht im README unter „Known
   differences".
4. Die bestehenden Plugins sind unverändert.
5. Im Audio-Thread wird nicht alloziert, nicht gesperrt, keine Datei
   angefasst.
6. Kein Ton bleibt hängen, kein nicht endliches Sample verlässt das Plugin.
7. Fremder Code ist MIT, die Hinweise sind erhalten, Herkunft und Änderungen
   sind vollständig aufgeführt. Aus den GPL-Portierungen (VCV, MAGDA) ist
   nichts übernommen.

## Worauf es besonders ankommt

### A. Die Umrechnung der Abtastrate (`core/`)

Das ist der Kern der Arbeit und der Teil, in dem ein Denkfehler am wenigsten
auffällt, weil die Tests ihn mit derselben Annahme prüfen könnten.

* **`resonator.cc`, `ComputeFilters`.** Die Herleitung im Kommentar: Ein
  Filter dieser Bauart mit Güte Q klingt je Sample um `sin(Winkel) / (2 Q)` ab
  und antwortet auf einen Ein-Sample-Impuls mit `sin(Winkel)`; daraus `k`.
  Stimmt die Herleitung, auch für die obersten Moden (Winkel nahe π bei
  32 kHz) und für kleine Güten (Damping 0)? Ist `tanf(atanf(g32) *
  inv_ratio)` die richtige Frequenz, wenn die Güte niedrig ist? Was passiert
  mit `mode_gain_` für Moden, die nur jeden zweiten Block aktualisiert werden,
  und nach `Init()`?
* **Gebogene Moden** (`f_bow_`, `d_bow_`): Verzögerung auf ganze Samples
  gerundet, Güte mit demselben `k`. Reicht das, oder verschiebt die Rundung
  bei 48 kHz die Kammfrequenz hörbar gegen das Bandpassfilter? Gemessen ist
  hier nur ein Mittelwert über acht Startwerte.
* **`matched_low_pass.h` und `SampleRate::ComputeMatchedLowPass`.** Der
  Tiefpass der Erreger wird numerisch an den Frequenzgang des Originals
  angepasst (ein Parameter je Tabelleneintrag, gewichtete Abweichung in dB).
  Ist die Suche robust (ein Minimum, kein Sprung zwischen benachbarten
  Tabelleneinträgen, stabil für alle 257 Einträge, auch dort, wo der Pol des
  Originals negativ wird)? Ein Sprung zwischen Nachbarn wäre beim Drehen von
  Timbre hörbar. Die Koeffizienten wechseln je Block ohne Übergang wie im
  Original; ist das bei dieser Filterform (direkte Form) noch unkritisch?
* **`tube.cc`:** die Reflexion mit 1,5 Samples Verzögerung (linear
  interpoliert) bei 48 kHz.
* **`fx/reverb.h`, `fx/diffuser.h`, `fx/fx_engine.h`:** gerundete Längen,
  Platz im Puffer (bei 48 kHz 32437 von 32768 Elementen), Modulationstiefe
  gegen Leitungslänge, Verhalten bei zu kleinem Puffer.
* **`exciter.cc`:** Wahrscheinlichkeiten, Rauschpegel, Abspielgeschwindigkeit,
  Plektrum. Gibt es eine „pro Sample"-Konstante, die übersehen wurde? Bitte
  das Original (`tests/reference/eurorack/elements/dsp/`) selbst nach Zahlen
  durchsehen, die eine Zeit oder Frequenz bedeuten, und mit der Tabelle in
  UPSTREAM.md abgleichen.
* **Bitgenauigkeit bei 32 kHz:** Deckt `TestBitExactAt32k` alle Zweige ab
  (alle Erregermodelle, Röhre, Freeze, Panik)? Gibt es einen Ausdruck, der
  bei 32 kHz nur zufällig gleich ist?

### B. Die Messungen (`tests/test_dsp.cpp`)

Sind die Messverfahren geeignet, das Behauptete zu zeigen? Insbesondere:
Könnte der Vergleich bestehen, obwohl etwas falsch ist (zu grobe Bänder, zu
weite Grenzen, Fälle, die den Fehler nicht anregen)? Welche Klänge fehlen
(tiefe und sehr hohe Noten, Geometry an den Rändern, lange Dämpfung,
Übergänge zwischen den Erregermodellen)? Die Grenzen sind bewusst als
Grobprüfung gesetzt, die Zahlen im Bericht sind das Ergebnis; sind die
Aussagen im README durch die Zahlen gedeckt?

### C. Engine (`engine/`)

* Stimme: Reihenfolge von Note-On und Note-Off im selben Quantum, mehr als 16
  gehaltene Tasten, Note-Off für eine vergessene Taste, Retrigger.
* Glättung und Grenzen wie in `cv_scaler.cc` des Originals? Freeze über den
  SPACE-Wert 2.0.
* Der Schutz gegen nicht endliche Samples: reicht ein Neustart der DSP, und
  ist er allokationsfrei? Was kostet er im Audio-Thread (er füllt den
  Hallpuffer)? Dasselbe gilt für `Reset()` nach dem Laden eines Zustands,
  das im Audio-Thread läuft: `SampleRate::Init` rechnet beim ersten Mal
  Tabellen (einige Millisekunden) und darf danach nichts mehr kosten.
* Ausgangsstufe: begrenzt sie wirklich auf ±1?

### D. Hülle (`source/`, `adapter/`)

Adapter, Parameterübergabe zwischen den Threads, Texteingabe und Fenster sind
Kopien aus Torus Classic 0.2.0 und dort schon geprüft. Neu und zu prüfen:

* `adapter/stream_adapter.*`: Note-Off und „alle Noten aus" in der
  Warteschlange; die Regel bei Überlauf (ein Note-Off geht nie verloren).
* **Offener Befund der automatischen Prüfung am Pull Request** (P2,
  `processor.cpp` um Zeile 366, Commit `7901899`): Kommen von der
  Bildschirm-Klaviatur zwischen zwei Audio-Blöcken mehr als 64 Ereignisse,
  gehen auch Loslass-Ereignisse verloren, und die letzte Taste bleibt hängen.
  Noch nicht nachgeprüft und nicht behoben. Bitte bestätigen oder widerlegen
  und als EL-R-Befund führen; dabei auch die anderen Warteschlangen zwischen
  Fenster, Controller und Prozessor auf dieselbe Lücke ansehen.
* `processor.cpp`: Note-Off-Ereignisse, Note-On mit Velocity 0, die
  Bildschirm-Klaviatur mit Loslassen, die Statusnachricht an den Controller
  aus `setActive` (Thread, Host ohne Nachrichten, Controller ohne Fenster).
* `controller.cpp`: `notify`, die neuen Parameterklassen, MIDI-Zuordnung.
* Zustand: Version 1 mit 19 Werten; ist das Format für spätere Versionen
  erweiterbar, und wird ein beschädigter Zustand ganz abgelehnt?
* Unterschiede zu Torus, die beim Kopieren entstanden sein könnten: bitte
  `source/` gegen `instruments/torus-classic/source/` vergleichen.

### E. Herkunft und Lizenz

* Stimmen `SHA256SUMS.upstream` und UPSTREAM.md mit den Originalquellen
  überein? Sind alle geänderten Dateien als geändert gekennzeichnet, mit
  erhaltenem Lizenzhinweis?
* Ist irgendwo Code aus `VCVRack/AudibleInstruments` oder
  `Conceptual-Machines/magda-core` (beide GPL) übernommen?
* Der Name: Das Plugin heißt „Mendelejew"; „Elements" ist nur noch der
  interne Projektname. Trägt noch etwas, das ein Nutzer des Plugins sieht
  (Anzeigename, Bundle, Fenster, `moduleinfo.json`), den Modulnamen oder den
  Namen Mutable Instruments? Reicht der Hinweis im README?

## Bekannt und nicht zu melden

* 96 kHz im Plugin: nicht umgesetzt, Status HOLD.
* Die im README unter „Known differences" genannten Abweichungen, es sei denn,
  eine ist größer als angegeben oder anders begründet.
* Keine Fenstertests.
* `common/` ist leer; die Kopien aus Torus Classic sind Absicht.
* Die Datei `instruments/torus-classic/docs/CODEX_REVIEW_0.2.0.md` gehört
  nicht zu diesem Branch.
