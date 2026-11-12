# Planung

Stand: 2026-11-12. Das Repository ist angelegt, es gibt noch keinen Plugin-Code.
Dieses Dokument sammelt, was vor der Umsetzung entschieden werden muss, und den
Fahrplan für das erste Testobjekt.

## Ausgangslage

* **Rechner:** Windows 11 x64, CMake 4.4.2, Visual Studio Build Tools 2022 und
  2026. Ninja und die ARM-Toolchain liegen nicht im PATH; beides wird hier nicht
  gebraucht.
* **Vorerfahrung im Playground:** `vst3-plugin-scanner` nutzt das Steinberg
  VST3 SDK 3.8.0 direkt (als Submodul), `Preen FM VST3 Editor` nutzt JUCE 9.
  Der frühere A440-Test (`BAK/vst3_a440_test.rar`, nicht versioniert) war ein
  reines SDK-Plugin mit Processor, Controller und Factory, Hersteller TAUREON.
* **Quelle für den ersten Port:** `instruments/toy-piano` in Daisy-Seed. Der
  Klangkern (`toy_piano.h/.cpp`, `config.h`) ist frei von Hardware-Code und
  hängt nur an `daisysp::mtof`. Details stehen in der
  [Projektbeschreibung](../instruments/toy-piano-classic/README.md).

## Offene Fragen

Jede Frage hat eine Empfehlung. Die mit **blockiert** markierten müssen vor der
ersten Codezeile entschieden sein.

### A. Technische Basis

| Nr. | Frage | Optionen | Empfehlung |
| --- | --- | --- | --- |
| A1 | **Framework** (blockiert) | Steinberg VST3 SDK direkt mit VSTGUI · JUCE 9 · iPlug2 · DPF | VST3 SDK direkt. Das entspricht „native VST3", der A440-Test und der Scanner liefen so, und das SDK steht seit 3.8 unter MIT. JUCE bringt die GUI schneller, bindet das Repo aber an AGPL oder eine kommerzielle Lizenz. |
| A2 | **Plattformen** (blockiert) | nur Windows x64 · zusätzlich macOS · zusätzlich Linux | Nur Windows x64. Der Code bleibt portabel, gebaut und getestet wird vorerst nur dort. |
| A3 | **Formate** | nur VST3 · zusätzlich Standalone · zusätzlich CLAP | Nur VST3. Zum Testen ohne DAW genügt der `editorhost` aus dem SDK. |
| A4 | **GUI-Technik** (hängt an A1) | VSTGUI mit `.uidesc`-Editor · VSTGUI in Code · generische Host-Oberfläche | VSTGUI mit `.uidesc`. Als Zwischenschritt reicht die generische Host-Oberfläche, bis der Klang stimmt. |
| A5 | **C++-Standard und Laufzeit** | C++17 · C++20; statische · dynamische MSVC-Runtime | C++20 und statische Runtime, wie im Scanner. Das Plugin braucht dann kein VC-Redistributable. |
| A6 | **SDK-Einbindung** | Git-Submodul · CMake FetchContent mit festem Tag | Submodul unter `external/vst3sdk`, fest auf einen Tag. Der A440-Test hat per FetchContent 350 MB in den Projektordner gezogen. |

### B. Repository und Organisation

| Nr. | Frage | Optionen | Empfehlung |
| --- | --- | --- | --- |
| B1 | **Sichtbarkeit** | privat · öffentlich | Vorerst privat (so angelegt). Öffentlich schalten, sobald das erste Plugin läuft. |
| B2 | **Lizenz** (hängt an A1) | MIT · GPL/AGPL · keine | MIT wie Daisy-Seed, falls A1 auf das SDK fällt. Bis zur Entscheidung liegt keine LICENSE im Repo. |
| B3 | **Herstellername und IDs** (blockiert) | — | Hersteller „TAUREON" wie im A440-Test. Jedes Plugin bekommt feste, einmal erzeugte Processor- und Controller-UIDs; sie dürfen sich später nie ändern, sonst verlieren DAW-Projekte das Plugin. |
| B4 | **Build-Struktur** | ein CMake-Projekt pro Plugin · ein Wurzelprojekt, das alle Plugins baut | Ein Wurzelprojekt mit `add_subdirectory` je Plugin. Das SDK wird dann nur einmal konfiguriert und gebaut. |
| B5 | **Code aus Daisy-Seed übernehmen** (blockiert) | Kopie mit Herkunftsvermerk · Daisy-Seed als Submodul · gemeinsamer DSP-Kern als eigenes Repo | Für das erste Testobjekt eine Kopie mit Commit-Angabe. Ob sich ein gemeinsamer Kern lohnt, ist selbst ein Ergebnis der Studie. |
| B6 | **Dokumentationssprache** | Englisch · Deutsch · gemischt | README-Dateien englisch wie in Daisy-Seed, Planung deutsch. So ist es jetzt angelegt. |
| B7 | **Installation nach dem Build** | automatisch nach `C:\Program Files\Common Files\VST3` · manuell aus `dist/` | Manuell aus `dist/`. Der Systemordner braucht Administratorrechte. |

### C. Toy Piano Classic

| Nr. | Frage | Optionen | Empfehlung |
| --- | --- | --- | --- |
| C1 | **Was heißt „Classic"?** | Klang von Firmware v0.2 ohne Modulationsmatrix, Ambient und Tape · etwas anderes | Der Klangkern ohne Matrix und Effekte: Sinus plus 12 % zweite Harmonische. |
| C2 | **Polyphonie** | 16 Stimmen wie der MIDI-Pool · mehr · einstellbar | 16, mit derselben Stimmenvergabe wie in der Firmware. |
| C3 | **Note-Off** | ignorieren wie die Firmware (One-Shot) · Note-Off verkürzt den Ausklang | Ignorieren. Das ist das Verhalten des Originals und passt zur AD-Hüllkurve. |
| C4 | **Master Pitch: Raster** | Halbtonschritte · stufenlos · Halbtöne plus Fine-Tune | Halbtonschritte von −24 bis +24. Fine-Tune wäre ein vierter Parameter. |
| C5 | **Master Pitch: Wirkung** | sofort auf klingende Noten (geglättet) · erst ab dem nächsten Anschlag | Sofort und geglättet. Nur so ist der Regler automatisierbar, ohne dass es knackt. |
| C6 | **Hüllkurvenzeiten** | Bereiche der Firmware (Attack 2–300 ms, Decay 50–3000 ms, logarithmisch) · erweitert | Bereiche der Firmware. Die Werte gelten wie dort ab dem nächsten Anschlag. |
| C7 | **Velocity** | linear auf die Spitzenlautstärke wie die Firmware · Kurve · abschaltbar | Linear wie die Firmware. |
| C8 | **Ausgangspegel** | Pegel der Firmware (eine Note etwa −27 dBFS) · fester Aufholgewinn · Volume-Regler | Fester Aufholgewinn von etwa +12 dB vor dem Limiter. Ein Volume-Regler wäre ein vierter Parameter; in der DAW gibt es den Kanalfader. |
| C9 | **Ausgang** | Stereo mit gleichem Signal auf beiden Kanälen · Mono-Bus | Stereo-Bus mit doppeltem Mono. Viele Hosts erwarten bei Instrumenten Stereo. |
| C10 | **„MIDI steuerbar": Umfang** | nur Noten · zusätzlich Pitch Bend · zusätzlich feste CCs für Attack, Decay und Pitch | Noten mit Velocity; die drei Parameter über die Host-Automation. VST3 reicht CCs nicht roh durch, sie müssen einzeln auf Parameter abgebildet werden (`IMidiMapping`). Pitch Bend und CCs als zweiter Schritt. |
| C11 | **GUI-Umfang** | drei Regler mit Wertanzeige · zusätzlich Bildschirmtastatur · skalierbar | Drei Regler mit Wertanzeige, feste Größe. |
| C12 | **Zustand und Presets** | Zustand im DAW-Projekt · zusätzlich Werkspresets | Nur Zustand im Projekt. Das ist Pflicht, sonst gehen Einstellungen beim Laden verloren. |
| C13 | **Messlatte für „gelungen"** | bitgenau wie die Firmware · bitgenau wie der Kern auf demselben Rechner · hörbar gleich | Bei 48 kHz und Master Pitch 0 liefert das Plugin bitgenau dasselbe wie der Kern, wenn man ihn ohne Plugin mit denselben Noten rechnet. Der Golden-Hash der Firmware (`test_golden.cpp`) taugt nicht direkt: Er umfasst den ganzen Callback samt Tasten und Potis und wurde mit GCC und glibc aufgezeichnet; `sin` und `pow` von MSVC können im letzten Bit abweichen. Gegen die Firmware deshalb mit Toleranz vergleichen. |

### D. Qualitätssicherung

| Nr. | Frage | Optionen | Empfehlung |
| --- | --- | --- | --- |
| D1 | **Automatische Prüfung** | `validator` aus dem SDK · pluginval · beides | `validator` als Build-Schritt; pluginval später. |
| D2 | **Test-Hosts** | Studio One (laut Toy-Piano-Doku vorhanden) · weitere | Studio One und der `editorhost` des SDK. Welche DAWs sind noch installiert? |
| D3 | **CI auf GitHub** | Windows-Build bei jedem Push · erst später | Einrichten, sobald das erste Plugin lokal baut. In einem privaten Repo verbrauchen Builds das Freikontingent an Actions-Minuten, Windows-Runner stärker als Linux-Runner. |
| D4 | **Tests des Klangkerns** | Host-Tests aus Daisy-Seed übernehmen · neu schreiben | `test_toy_piano.cpp` übernehmen: Er prüft den Kern allein. Die übrigen Tests hängen an `main.cpp` und den Hardware-Attrappen und bleiben in Daisy-Seed. |

### E. Ausblick

| Nr. | Frage | Empfehlung |
| --- | --- | --- |
| E1 | Welcher Effekt wird das erste Testobjekt unter `effects/`? | `drive` oder `stereo-delay`: wenig Zustand, klar messbar. Reverb und Tape hängen stärker an DaisySP. |
| E2 | Wird DaisySP auf dem Desktop mitgenutzt? | Ja, als Submodul, sobald ein Port es braucht. DaisySP ist reines C++ unter MIT. Für Toy Piano Classic genügt die eine Funktion `mtof`. |
| E3 | Sollen Ergebnisse zurück in die Firmware fließen? | Offen. Davon hängt ab, ob sich der gemeinsame DSP-Kern aus B5 lohnt. |

## Fahrplan Toy Piano Classic

Die Schritte bauen aufeinander auf. Jeder endet mit etwas Prüfbarem.

| Schritt | Inhalt | Fertig, wenn |
| --- | --- | --- |
| 0 | Blockierende Fragen entscheiden: A1, A2, B3, B5 | Entscheidungen stehen in diesem Dokument |
| 1 | Build-Gerüst: Wurzel-CMake, SDK, Presets für VS 2026 und 2022, leeres Instrument | Das Plugin baut, besteht den `validator` und erscheint in der DAW als Instrument |
| 2 | Klangkern übernehmen: `toy_piano.*` und `config.h` als eigene Bibliothek ohne Plugin-Abhängigkeit, dazu die Kern-Tests | Die Tests laufen mit MSVC grün; eine Referenzaufnahme des Kerns liegt als Vergleichsbasis vor |
| 3 | Kern anbinden: Note-On mit Sample-Offset, Parameter Attack und Decay, Abtastrate des Hosts, Stereo-Ausgang, Zustand speichern | Spielbar in der DAW; Einstellungen überleben Speichern und Laden |
| 4 | Master Pitch ±24 Halbtöne mit Glättung | Automation des Reglers ohne Knackser; Tonhöhe nachgemessen |
| 5 | GUI mit drei Reglern | Regler, Automation und Host-Anzeige laufen synchron |
| 6 | Abnahme: `validator`, Test in der DAW, CPU-Last, Abtastraten 44,1 bis 192 kHz, Blockgrößen 16 bis 2048 | Ergebnisbericht unter `instruments/toy-piano-classic/docs/` |
| 7 | CI-Workflow und Auswertung: Was ging 1:1, was musste angepasst werden, was folgt daraus für weitere Ports? | Abschnitt „Erkenntnisse" im Bericht |

## Bekannte Stolpersteine beim Port

* **Feste Blockgröße.** Die Firmware rechnet in Blöcken von 48 Samples und
  liest Regler mit 1 kHz. Ein Host liefert beliebige Blockgrößen, und Noten
  tragen einen Sample-Offset innerhalb des Blocks.
* **Abtastrate.** Die Firmware läuft fest mit 48 kHz. Der Kern rechnet seine
  Tabellen in `Init()` aus der Abtastrate, er muss also bei jedem Wechsel neu
  initialisiert werden.
* **Master Pitch.** Im Kern nicht vorhanden. Die Stimmen haben ein
  Tonhöhenverhältnis (`Modulate`), das die Matrix der Firmware nutzt; darauf
  kann der Master Pitch aufsetzen. Bei +24 Halbtönen muss die Regel greifen,
  die die zweite Harmonische oberhalb eines Viertels der Abtastrate abschaltet.
* **Echtzeit-Regeln.** Im Audio-Callback keine Speicheranforderung, keine
  Locks, keine Dateizugriffe. Der Firmware-Kern hält das bereits ein.
* **Zwei Threads.** VST3 trennt Processor (Audio) und Controller (GUI). Werte
  wandern nur über Parameteränderungen und Nachrichten zwischen beiden, nie
  über gemeinsame Zeiger.
* **Typen.** `toy_piano.cpp` verlässt sich auf Includes aus `config.h` und auf
  `daisysp::mtof`. Beides ist auf dem Desktop mit wenigen Zeilen ersetzt.
