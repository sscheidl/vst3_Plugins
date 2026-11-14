# Buildauftrag an Claude Code: Torus Classic 0.1.0

Stand: 2026-11-14. Zielrepository: `sscheidl/vst3_Plugins`.

## 1. Ziel und verbindlicher Umfang

Baue ein eigenständiges Windows-x64-VST3-Instrument aus unserer bereits
untersuchten Rings-/Torus-Tastaturfassung in `Daisy-Seed`. Verwende das
vorhandene Steinberg-SDK-/VSTGUI-Gerüst. Ergebnis ist eine lokal gebaute,
automatisch geprüfte und für einen anschließenden DAW-Test vorbereitete
Version, mit nachvollziehbarem Bericht und Quellcode.

Arbeitsname: **Torus Classic**, Hersteller **TAUREON**, Version **0.1.0**,
Verzeichnis `instruments/torus-classic/`, Bundle `Torus Classic.vst3`.
Eigene, neu erzeugte Processor-/Controller-FUIDs; keine Toy-Piano-IDs kopieren.

Verbindlich:

- Sechs vorhandene Synthesemodelle, fest vier Stimmen, interner Exciter,
  trockener Stereoausgang, kein Audioeingang.
- **Kein Reverb:** keine laufende Hallberechnung, keine Hallpuffer/-Instanz,
  keine Hallparameter, kein Dry/Wet-Regler und kein Ersatz-Effekt.
- MIDI-Noten mit Velocity, Pitch Bend, die unten definierte CC-Zuordnung,
  Host-Automation, Projektzustand und Bildschirm-Klaviatur.
- Note-off und Velocity-null lösen keinen neuen Anschlag aus und lassen die
  Resonatoren wie in der Daisy-Fassung ausklingen. Damping bleibt ein
  Syntheseparameter. „Ohne Reverb“ bedeutet nicht „ohne Ausklang“.
- VST3 SDK direkt, VSTGUI, C++20, statische MSVC-Runtime. Keine Umstellung
  auf JUCE, kein zusätzliches Pluginformat.

Nicht Bestandteil: Mod on Daisy, Modulationsmatrix, MPE/Aftertouch,
variable Stimmenzahl, externe Audioanregung, SysEx-Gerätesteuerung,
Hardware-/Control-App-Änderungen, Preset-Browser oder weitere Effekte.

Die sechs Modelle entsprechen der Reihenfolge in `torus/dsp/part.h`:

| Index | Anzeige | Herkunft |
| --- | --- | --- |
| 0 | Modal Resonator | `RESONATOR_MODEL_MODAL` |
| 1 | Sympathetic Strings | `RESONATOR_MODEL_SYMPATHETIC_STRING` |
| 2 | Inharmonic Strings | `RESONATOR_MODEL_STRING` |
| 3 | FM Voice | `RESONATOR_MODEL_FM_VOICE` |
| 4 | Western Chords | `RESONATOR_MODEL_SYMPATHETIC_STRING_QUANTIZED` |
| 5 | Stereo Strings | trockener Synthesezweig von `RESONATOR_MODEL_STRING_AND_REVERB` |

Beim letzten Modell die vorhandene Saitendämpfung, Stereo-Kreuzmischung und
abschließende Polarität des trockenen rechten Kanals erhalten. Nur den Hall
entfernen; das Modell nicht durch ein anderes ersetzen. Den Anzeigenamen
als Benennung dieser trockenen Variante dokumentieren.

## 2. Arbeitsstand, Quellen und Grenzen

Lies vor Beginn die anwendbaren `AGENTS.md`, insbesondere
`../AGENTS.md`, sowie `README.md`, `docs/PLANUNG.md` und die aktuellen
Toy-Piano-Quellen. Die bereits vorhandenen Änderungen gehören dem Benutzer
beziehungsweise der laufenden Claude-Arbeit und dürfen nicht überschrieben,
zurückgesetzt, gestasht oder als Nebenwirkung mitcommittet werden.

Ausgangsstand des VST3-Repos bei Abschluss dieses Auftrags ist
`3aa03339201ba3f748067975e71fbdb38cf5edbd` (Toy Piano Classic 0.3.1,
„fixes from the Codex review of 0.3.0“). Der Commit entstand während der
Vorbereitung dieses Auftrags. Verwende diese 0.3.1-Hülle einschließlich
Fehlerkorrekturen als Vorlage und prüfe beim Arbeitsbeginn auf neuere oder
noch uncommittete Änderungen. Lies dazu:

- `instruments/toy-piano-classic/docs/CODEX_REVIEW_0.3.0.md`
- `instruments/toy-piano-classic/docs/CODEX_REVIEW_0.3.0_UMSETZUNG.md`

Die Umsetzung ist berichtet, noch nicht unabhängig nachgeprüft. Übernimm
Behauptungen daraus nicht ungeprüft. Ist die Toy-Piano-Arbeit noch parallel
aktiv, beginne mit den eigenen Torus-Dateien und stimme gemeinsame
Integrationsdateien erst mit dem dann vorliegenden Stand ab.

Primäre DSP-Quelle ist das **lokale** Nachbarrepository:

```text
../Daisy-Seed/instruments/physical-modeling/
  src/physical_modeling.{h,cpp}
  src/parameter_manager.h
  src/audio_output.h
  src/config.h
  src/performance_render.h
  torus/
  third_party/stmlib/
  tests/
```

Dort besonders `README.md`, `torus/UPSTREAM.md`, die Architektur-/Review-
und Performance-Dokumente lesen. Die Komponenten-Trennung aus
`../Daisy-Seed/docs/MODULAR_DEVELOPMENT_POLICY.md` beibehalten. Dieser Auftrag
betrifft einen lokalen Audio-Host, keinen Daisy-Geräteeditor; die dortigen
App-/USB-Integrationsschritte sind hier kein zusätzlicher Produktumfang.

Die Provenienz nennt Torus aus DaisyExamples
`259ed82c7d4c0d7699f695945e6e111b7dc7cb27` und stmlib
`5dc8b89b9d166de5f47ba91ee9d25c8805c2bd86`. **Unsere lokale Fassung enthält
weitere wichtige Korrekturen.** Nicht durch einen frischen Upstream-Download
ersetzen und keine neue Rings-Portierung von vorn beginnen.

Vor dem Kopieren den tatsächlichen Daisy-Seed-Commit und Dirty-Status
ermitteln. Bei abweichenden Arbeitsdateien zusätzlich SHA-256 der wirklich
übernommenen Dateien festhalten. Falls Git-Metadaten nicht lesbar sind, diese
Grenze ausdrücklich nennen und die Dateihashes als überprüfbare Grundlage
verwenden; keinen Commit erfinden. Quelle und anschließende lokale Patches
in `UPSTREAM.md` und einem maschinenlesbaren Komponentenmanifest festhalten.

Das VST3-Projekt muss nach Übernahme **ohne das Nachbarrepository** baubar sein.
Nur benötigte Quellen und ihre Lizenztexte übernehmen. Daisy-Seed bleibt
unverändert; kein Firmware-Build, Flashen oder Ändern von Submodul-Pins.

Nichts installieren, nichts in `dist/` oder installierten VST3-Verzeichnissen
ändern. Keine Commits, Pushes, PRs oder GitHub-Workflows im Rahmen dieses
Auftrags. Projekttexte, Code, GUI und neue READMEs auf Englisch verfassen.

## 3. Aufbau und Übernahme des Kerns

Trenne mindestens:

1. hardwarefreien Torus-DSP und stmlib-Abhängigkeiten;
2. trockene Instrument-Engine mit Noten-/Stimmenlogik;
3. Streaming-Adapter für Samplerate, Render-Quanten und Event-Zeit;
4. VST3-Processor/Controller, Parameterzustand und Editor.

Jede Schicht bekommt eine kleine nachvollziehbare Schnittstelle und passende
Tests. Produkt-/DSP-/Adapter-Versionen und Herkunft im Komponentenmanifest
auflisten. Keine Firmware-USB-Stacks, ADC-/Poti-Abfrage, LEDs, Bootloader,
ARM-Linkerskripte oder SDRAM-Platzierung in den Desktop-Pfad ziehen.
Vorhandene Host-Kompatibilitätsstellen gezielt nutzen.

Kein allgemeines Pluginframework auf Vorrat entwickeln. Toy Piano funktional
unverändert lassen. Ein kleiner gemeinsamer Helfer unter `common/` ist nur
sinnvoll, wenn beide Consumer ihn tatsächlich brauchen und geprüft werden;
eine breite Herauslösung des bestehenden Editors ist kein Ziel.

### Hallentfernung und Klangreferenz

In `Synth::Render` läuft der globale Reverb derzeit auch bei Dry/Wet null.
`Wet=0` allein erfüllt diesen Auftrag deshalb nicht. Entferne den Wet-Pfad
aus dem Produkt einschließlich Instanz, Puffer, Initialisierung und Parametern.
Prüfe auch unbenutzte Includes und transitive Abhängigkeiten in `Part`.

Erhalte den trockenen Signalweg: Modellpegel, Excitation/Velocity, bestehende
Modellwechsel-Fades, Stereo-Routing, rechten Polaritätswechsel von Modell 5
und die Ausgangsfunktion aus `audio_output.h`. Den Klang nicht nebenbei durch
zusätzliche Normalisierung, neue Filter oder ein neues Voice-Stealing ändern.

Die Referenz ist unsere Daisy-Fassung mit exakt trockenem Mix, nicht eine
Original-Rings-Hardware und nicht ein GCC-/ARM-Hash unter MSVC. Achtung:
`ParameterManager` startet mit `Wet=0.25`; `Set(Wet,0)` erreicht null erst
nach Glättung. Für Referenz und Port eine identische, dokumentierte
Initialisierung beziehungsweise stille Vorlaufphase herstellen. Referenz-
Render dürfen den alten Reverb nur im Test enthalten, nicht im Plugin.

Ein Referenztest darf nicht bloß zweimal denselben neuen Wrapper aufrufen.
Mit identischen Parametern, Events und RNG-Seeds gegen eine separat
kompilierte, gepinnte trockene Referenz vergleichen. Bei notwendigen
Portänderungen jede Abweichung erklären und quantifizieren.

### Instanzisolierung

`stmlib::Random` besitzt einen globalen veränderlichen Zustand. Jede
Plugin-Instanz benötigt ihren **eigenen** Zufallszustand mit reproduzierbarem
Startseed. Kein globales Seed/Restore um `process`, keine Audio-Sperre;
`thread_local` allein stellt keine Instanzisolierung sicher, weil mehrere
Instanzen auf demselben Thread oder wechselnden Threads laufen können.
Den Eingriff in den DSP gezielt dokumentieren und die Zufallsfolge erhalten.

`torus::InitResources` beschreibt die globale Sinustabelle erneut. Nach der
Initialisierung müssen gemeinsam genutzte Tabellen unveränderlich sein;
Initialisierung einmal sicher außerhalb des Audio-Threads oder pro Instanz.
Eine neue Instanz darf keine von einer laufenden Instanz gelesenen Tabellen
überschreiben. Weitere veränderliche `static`-Zustände ebenfalls prüfen.

## 4. Audiotakt, Ereignisse und Echtzeit

### Fester interner Takt

Behalte **48 kHz intern**. In `torus/dsp/dsp.h` ist diese Rate fest in den
Algorithmen verankert. Nicht nur eine Konstante auf die Host-Rate umstellen.

Die vorhandene Engine arbeitet mit **12 Samples je Render-Quantum**, höchstens
48 Samples pro `Synth::Process`. Übergib ihr keine beliebigen Host-Blöcke.
Baue einen zustandsbehafteten FIFO-/Quantum-Adapter. Er muss insbesondere
Host-Blöcke von 1, 7, 12, 16, 48, 64, 333, 1024 und 2048 Samples sowie
wechselnde Blocklängen verarbeiten, ohne Samples zu verlieren oder zu doppeln.

Bei 48 kHz einen Pfad ohne unnötiges Resampling verwenden. Für 44.1, 88.2,
96 und 192 kHz einen geeigneten Streaming-Resampler einsetzen. Vorhandene
SDK-Funktionalität zuerst auf Eignung prüfen; andernfalls eine kleine
geeignete Abhängigkeit mit Version/Lizenz dokumentieren. Kein einfacher
linearer Interpolator ohne Alias-Unterdrückung für die allgemeine Umsetzung.

Resampler-/FIFO-Zustand bleibt über Host-Blöcke erhalten. Speicher vorab in
Setup/Aktivierung reservieren. Gruppenlaufzeit, Pufferung, Startup und Flush
definieren; tatsächliche algorithmische Latenz über `getLatencySamples`
melden. Eine Latenzänderung nur im geeigneten Host-Lebenszyklus aushandeln.

### Notenzeit und Quantisierung

Events anhand einer durchgehenden Host-/internen Sample-Zeitleiste zuordnen.
Für die erste Version ist bewusst erlaubt: Anschlag auf dem **ersten internen
12-Sample-Rasterpunkt, der nicht vor seinem Sollzeitpunkt liegt**. Das sind
weniger als 12 interne Samples zusätzlicher Rasterverzögerung, zuzüglich
ausgewiesener Adapterlatenz und gegebenenfalls der folgenden Trigger-Regel.
Das Raster darf nicht bei jedem Host-Block neu beginnen.

Die Daisy-Fassung verarbeitet höchstens vier wartende Anschläge je Quantum;
weitere folgen im nächsten Quantum. Dieses Verhalten einschließlich
Reihenfolge/Voice-Stealing erhalten und die zusätzliche Verzögerung bei
dichten Akkorden dokumentieren. Queues begrenzen und Überlauf explizit
behandeln; keine unbegrenzte Drain-Schleife im Audio-Thread.

Nicht „samplegenau“ oder „latenzfrei“ behaupten. Bereits vorgerenderte Samples
nicht nachträglich ändern; zukünftige Events nicht vorzeitig anwenden.
Resampler-Lookahead durch echte kausale Pufferung/Latenz berücksichtigen.
Für dieselbe absolute Eventfolge muss das Resultat unabhängig von der
Aufteilung in Host-Blöcke sein.

Note-on-Daten vor Rundung/Casts validieren. Ungültige/unsortierte Offsets
sicher begrenzen, Velocity null ignorieren, normale Velocity auf 1–127
abbilden. Pitch Bend und CCs kommen über VST3-Parameter/`IMidiMapping`.

### Echtzeit und Lebenszyklus

In `process` und allen aufgerufenen DSP-Pfaden keine Allokation, Sperren,
Dateioperationen, Logging oder blockierende Host-Kommunikation. Engine und
Resampler ausschließlich vom Audio-/zulässigen Lifecycle-Pfad aus verändern.
UI und State-Reader veröffentlichen Parameter/Noten sicher und begrenzt.

Modellwechsel müssen ohne neue Speicherreservierung funktionieren. Vorhandene
Fades zunächst erhalten und die maximalen Rechenkosten des eigentlichen
Wechsels separat messen; eine Fade-Kurve allein beseitigt keine Lastspitze.
Keine unbelegte Zusage, dass die auf Daisy beobachteten Spitzen am PC weg sind.

Aktivierung, Deaktivierung, Sample-Rate-Wechsel, Reset und Laden definieren:
keine verspäteten alten UI-Noten, vollständiger FIFO-/Resampler-Reset und
reproduzierbarer Neustart. Keine pauschale Silence-Meldung nur wegen fehlender
neuer Events; Resonatoren und FM können lange weiterklingen. Tail-Länge
konservativ korrekt melden, bei nicht begrenztem Ausklang gegebenenfalls
`kInfiniteTail`.

## 5. Parameter und Zustand

Folgende stabile VST3-IDs für dieses neue Plugin verwenden:

| ID | Parameter | Bereich / Vorgabe | MIDI |
| --- | --- | --- | --- |
| 100 | Tune | kontinuierlich −24…+24 st, 0 st | CC20 |
| 101 | Structure | 0…1, 0.5 | CC21 |
| 102 | Brightness | 0…1, 0.5 | CC22 |
| 103 | Damping | 0…1, 0.5 | CC23 |
| 104 | Position | 0…1, 0.5 | CC24 |
| 105 | Excitation | 0…1, 0.8 | CC25 |
| 106 | Model | sechs Einträge, Modal Resonator | CC28 |
| 107 | Volume | linear 0…1, Anzeige 0…100 %, Vorgabe 100 % | CC33 |
| 108 | Pitch Bend | kontinuierlich ±2 st, Mitte 0 st | Pitch Wheel |

Tune bleibt wie in Daisy kontinuierlich; nicht versehentlich den
Halbton-Snap des Toy Piano übernehmen. Volume entspricht dem Firmware-Master
vor der bestehenden Ausgangsbegrenzung. Keine zusätzliche Verstärkung über
den Firmware-Maximalpegel in dieser Version. Die ehemaligen Reverb-CCs
26/27/30/31/32 und Polyphony-CC29 nicht zuordnen. Alle gültigen MIDI-Kanäle
0–15 auf Event-Bus 0 unterstützen.

Model hat `stepCount=5`; Anzeige, Textparser, normalisierte Werte und
DSP-Auswahl müssen dieselbe VST3-Stufenregel benutzen. Für die normalen
Parameter `kCanAutomate`; Pitch Bend ist ebenfalls automatisierbar, bekommt
aber keinen eigenen Drehregler. Strenge, locale-unabhängige Texteingabe
einschließlich unverändertem Ausgabewert bei Fehler; NaN/Infinity abfangen.
Die SDK-Scanner nicht ungeprüft als Validator verwenden.

Versionierter Processor-State speichert IDs 100–107. Pitch Bend ist transient
und wird beim Laden auf Mitte zurückgesetzt, auf beiden VST3-Seiten.
Controller-State speichert Nutzerzoom und Keyboard-Sichtbarkeit separat.
DSP-Ausklang, laufende Stimmen, Queues und Resampler werden nicht als Patch
gespeichert. Ein dokumentiertes Laden setzt diese Laufzustände sicher zurück;
die Übergabe darf nicht mit `process` auf denselben DSP-Speicher konkurrieren.

Vollständig erst in lokale Variablen lesen und validieren, dann veröffentlichen.
Fehlerhafte Versionen und jede Abschneidung ablehnen, ohne einen Teilzustand
zu übernehmen. Auch Wertebereiche, nicht endliche Binärwerte und konsistente
Controller-/Processor-Wiederherstellung prüfen.

## 6. Editor und Bedienung

Eine einfache Vektoroberfläche mit sieben Drehreglern (Tune, Structure,
Brightness, Damping, Position, Excitation, Volume), Modellwahl und
Wertanzeigen. Bildschirm-Klaviatur C4–C6, Velocity 100, ein-/ausblendbar.
Kein Reverb-Bedienelement. Fenster als Ganzes skalierbar; Nutzerzoom 50–200 %
zusätzlich zum Host-DPI-Faktor. GUI-Texte auf Englisch.

Die korrigierten Toy-Piano-Wege für Resize, Destruktion, fehlendes `removed`,
Timerabbau, KEYS-Rücknahme bei Host-Ablehnung und Textparser berücksichtigen.
Host erst fragen, native View erst in `onSize` ändern. Mehrere Views und
erneutes Öffnen sicher behandeln. Keyboard-Toggle nicht als musikalischen
Parameter an den Host melden.

Bildschirmnoten verwenden VST3-Messages und eine begrenzte Queue zum
Processor. Keine Audioverarbeitung und keine Messages im Audio-Thread
allokieren. Fehlende Nachrichtenunterstützung darf nicht abstürzen;
das Verhalten dokumentieren.

## 7. Arbeitsschritte und erforderliche Nachweise

### A. Herkunft und trockener Kern

Quellstand/Hashes festhalten, benötigte Dateien übernehmen, MSVC-Build des
hardwarefreien Kerns herstellen. Gezielte Desktop-Patches dokumentieren.
Erst trockenen Referenzvergleich und Instanzisolierung herstellen, dann
Sampleratenadapter und vollständige VST3-Anbindung ergänzen.

### B. Adapter und Plugin

Adapter separat testen; anschließend Instrument in das vorhandene
CMake-Wurzelprojekt aufnehmen. Toy Piano bleibt parallel baubar. Eindeutige
Target-/Testnamen, eigene Version und korrektes `moduleinfo.json`.
32- und 64-Bit-Hostpuffer unterstützen; die interne DSP-Präzision darf Float
bleiben und wird entsprechend dokumentiert.

### C. Gezielte Tests

Mindestens folgende Fälle tatsächlich ausführen und Ergebnisse aufzeichnen:

1. Alle sechs Modelle, Einzelnoten, vier-/mehrstimmige Akkorde, Retrigger,
   Note-off/Velocity-null, Voice-Stealing, MIDI-Noten 0 und 127, Pitch-Bend-
   und Tune-Grenzen, Parameter-Endpunkte, Modellwechsel und lange Ausklänge.
2. Trockene Referenzvergleiche bei 48 kHz mit festem Seed, gleichem
   Eventraster und mehreren deterministischen musikalischen Folgen,
   einschließlich aller sechs Modelle und Parameterbewegungen. Exaktheit
   nur für tatsächlich gleiche Randbedingungen behaupten. Numerische
   Toleranzen vor der Auswertung begründen, nicht nach einem Fehlschlag lockern.
3. Dieselbe absolute Event-/Parameterfolge über alle oben genannten
   Blockgrößen, zusätzlich wechselnde Größen und 32/64 Bit. Auch Anschläge
   direkt vor, auf und nach Quantum-/Host-Grenzen sowie dichte Akkorde.
4. Resampling bei allen fünf Host-Raten: Sampleanzahl über längere Laufzeit,
   kein Taktdrift, Kontinuität, gemeldete/gemessene Latenz und dokumentierte
   quantitative Passband-/Alias-Unterdrückungsprüfung des Adapters mit
   geeigneten Testsignalen. Nicht jeden Saitenmodus auf ideale 440 Hz zwingen;
   die bekannte Torus-Stimmung und Spektren mit der Referenz vergleichen.
5. Zwei Instanzen isoliert, verschachtelt auf einem Thread und parallel:
   A muss mit gleichem Seed/gleicher Folge dasselbe liefern, unabhängig von B.
   B erzeugen/initialisieren/zurücksetzen/zerstören, während A läuft. Auch
   Offline-/Wiedergabereihenfolge und Threadwechsel berücksichtigen.
6. Volle Queues, Pause ohne `process`, Parameter-Flush ohne Audio, Wiederaufnahme,
   Aktivierungs-/Sampleratenwechsel, Reset, beschädigte Zustände und Laden
   bei laufendem Audio. Keine alten Noten nach einem dokumentierten Reset.
7. Editor mit Host-Doppel: An-/Abhängen, Freigabe ohne `removed`, erneutes
   Öffnen, zwei Views, Host-Resize-Ablehnung am Mindestzoom, DPI vor/nach
   Öffnen sowie Parameterautomation bei geschlossenem Fenster.
8. Diagnose für Speicherzugriffe/undefiniertes Verhalten mit geeignetem
   Sanitizer beziehungsweise verfügbarem Debug-Werkzeug. Ebenso
   Allokationsfreiheit im laufenden Render-Pfad gezielt prüfen. Grenzen des
   gewählten Werkzeugs offenlegen; `assert` in Release nicht wegkompilieren.
9. Pro Modell normale Renderkosten und Modellwechselkosten messen, mit
   Rechner/Compiler/Rate/Blockgröße und Wiederholungsumfang. Das liefert
   Messwerte auf diesem Rechner, keine generelle DAW-Echtzeitgarantie.

Finite-/NaN-Prüfungen unter den tatsächlich verwendeten MSVC-Float-Optionen
prüfen: das SDK aktiviert `/fp:fast`. Falls nötig für Validierung oder
Referenz gezielt präzisere Optionen setzen; keine durch Compilerannahmen
wirkungslos gewordenen Tests als Erfolg zählen.

### D. Build-Matrix

Die vorhandenen Presets nutzen:

```powershell
cmake --preset windows-vs2026-x64
cmake --build --preset windows-vs2026-x64-release --parallel 4
ctest --preset windows-vs2026-x64-release
cmake --build --preset windows-vs2026-x64-debug --parallel 4
ctest --preset windows-vs2026-x64-debug

cmake --preset windows-vs2022-x64
cmake --build --preset windows-vs2022-x64-release --parallel 4
ctest --preset windows-vs2022-x64-release
```

Validator am echten Torus-Bundle in allen drei Konfigurationen ausführen;
Testanzahl und Ergebnis tatsächlich berichten, nicht pauschal die 47 Tests
des Toy Piano als neue Erwartung übernehmen. Bestehende Toy-Piano-Tests
ebenfalls ausführen, um Integrationsregressionen zu erkennen. Moduleinfo,
eindeutige IDs, Runtime und Bundle-Struktur prüfen.

Bei fehlender Toolchain oder einem Umgebungsproblem sichere Teilprüfungen
fortsetzen und die konkrete Lücke nennen. Keine fehlgeschlagenen oder nicht
ausgeführten Schritte als bestanden darstellen.

## 8. Dokumentation, Artefakte und Abschluss

Liefere im Plugin-Verzeichnis mindestens:

- `README.md`: Umfang, Parameter/MIDI, Modelle, Installation durch den
  Menschen, Eventquantisierung/Latenz, bekannte Abweichungen und offene Tests.
- `UPSTREAM.md`, Lizenztexte, Komponentenmanifest und Hashliste: tatsächliche
  Quelle, Versionen und jede lokale Portänderung nachvollziehbar.
- `docs/BUILD_REPORT_0.1.0.md`: Befehle, Compiler, Ergebnisse je Konfiguration,
  Referenz-/Block-/Mehrinstanztests, Adapterlatenz, Performance-Messwerte,
  Speicherbedarf und nicht geprüfte Punkte. Jede allgemeine Aussage muss
  durch einen konkret benannten Test oder eine begrenzte Codeaussage gedeckt sein.
- `docs/DAW_TEST_0.1.0.md`: kurze noch offene Prüfliste für Studio One 7,
  Tracktion Waveform 14 und Ableton Live 12 Lite: Laden/Spielen aller Modelle,
  Automation/CC/Bend, Speichern/Laden, mehrere Instanzen, Transport und
  Offline-Export, GUI/DPI. DAW-/Hörabnahme macht der Benutzer.

Projektübersichten und `THIRD_PARTY_NOTICES.md` gezielt ergänzen; keine
historischen Testergebnisse überschreiben. Der Benutzer hat Starten und
Spielen per Tastatur des bisherigen Toy Piano in Studio One 7 bestätigt;
das ist kein bereits erfolgter Torus-Test.

Das gebaute Release-Bundle mit benötigten Lizenztexten, kurzer Testanleitung
und SHA-256-Liste als lokales Ergebnis unter
`out/torus-classic-0.1.0/` bereitstellen. Keine automatische Installation.
Temporäre Hilfen unter `.work/torus-classic/` führen. Zum Abschluss nur eigene
Wegwerfdateien/Build-Artefakte beseitigen; das explizite Ergebnisbundle
behalten. Keine fremden oder noch parallel verwendeten Build-Verzeichnisse
löschen. Generierte Binaries nicht versionieren.

**Fertig ist dieser Auftrag**, wenn Quellcode und lokales Ergebnisbundle
vorliegen, die Prüfungen tatsächlich bestanden sind oder konkrete Restfehler
ehrlich ausgewiesen werden und der Bericht einen unabhängigen Code-Review
ermöglicht. Bei bekannten funktionalen Restfehlern lautet der Status
„nicht bereit“; ein erfolgreicher Validator allein ist keine Freigabe.
Nicht auf eine erneute Bestätigung warten, um die hier beauftragte lokale
Implementierung und Prüfung auszuführen. Veröffentlichung und DAW-Abnahme
sind separate Schritte.
