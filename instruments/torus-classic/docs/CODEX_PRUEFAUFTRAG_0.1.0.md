# Prüfauftrag an Codex: Torus Classic 0.1.0 (nur Code)

* **Stand:** 2026-11-14
* **Gegenstand:** VST3-Instrument `instruments/torus-classic/` im Repo
  [sscheidl/vst3_Plugins](https://github.com/sscheidl/vst3_Plugins). Beim
  Schreiben dieses Auftrags lag der Code **uncommittet** im Arbeitsverzeichnis
  auf `main` @ `e553451`. Ist er inzwischen committet, gilt dieser Commit.
* **Verfasser des Codes:** Claude (Fable 5.2)
* **Grundlage:** [Buildauftrag](../../../docs/CLAUDE_BUILDAUFTRAG_TORUS_CLASSIC_0.1.0.md),
  [Build-Bericht](BUILD_REPORT_0.1.0.md), [UPSTREAM.md](../UPSTREAM.md),
  [README](../README.md)
* **Ergebnis nach:** `instruments/torus-classic/docs/CODEX_REVIEW_0.1.0.md`

## Auftrag und Grenzen

Prüfe das Plugin unabhängig und kritisch **auf Codeebene**, bevor es in den
DAWs getestet wird. Maßstab ist der Buildauftrag. Schwerpunkt sind die Teile,
die kein automatischer Test sicher abdeckt: Threads, Echtzeit, Lebensdauer von
Objekten, das Zusammenspiel mit dem Host, und die Frage, ob die Tests wirklich
belegen, was der Build-Bericht daraus ableitet.

* **Ändere keinen Code, keine Doku und keine Tests.** Schreibe nur die
  Ergebnisdatei. Korrigiert wird erst, wenn der Mensch das danach beauftragt.
* Die Regeln in `AGENTS.md` im Playground-Wurzelordner gelten. Wegwerf-Code
  (zusätzliche Tests, Messungen, Sonden) nur unter
  `vst3_Plugins/.work/torus-classic-review/`; der Ordner ist von Git
  ausgenommen. Am Ende diesen Ordner löschen.
* **Baue in ein eigenes Verzeichnis**, etwa
  `cmake -S . -B .work/torus-classic-review/build -G "Visual Studio 18 2026" -A x64`.
  Die Verzeichnisse unter `build/` werden parallel von anderen Sitzungen
  benutzt: nicht löschen, möglichst nicht hineinbauen.
* Nichts committen, nichts pushen, keine GitHub-Workflows. Fremde Änderungen
  im Arbeitsverzeichnis nicht anfassen, auch nicht an Toy Piano Classic.
* `dist/`, `out/` und die installierten Kopien unter
  `%LOCALAPPDATA%\Programs\Common\VST3\` nicht anfassen. Dort liegt seit dem
  2026-11-14 auch `Torus Classic.vst3` für den DAW-Test des Menschen.
* Das Nachbarrepo `../Daisy-Seed` nur lesen.
* `core/` ist übernommener Fremdcode mit wenigen, markierten Eingriffen
  (Kommentare, die mit „Torus Classic" beginnen; Liste in `UPSTREAM.md`).
  Prüfe die Eingriffe. Befunde im unveränderten Upstream-Code bitte melden,
  aber als Firmware-Befund kennzeichnen; sie gehören nach Daisy-Seed.
  `tests/reference/` ist absichtlich bytegleich mit der Firmware.
* Keine GUI-Bewertung (Farben, Anordnung) und keine Hörprüfung. Bedienung und
  Klang prüft der Mensch.
* **Behauptungen am Code prüfen, nicht an der Doku.** Der Build-Bericht ist
  vom Verfasser des Codes geschrieben; übernimm nichts daraus ungeprüft. Wo du
  etwas nicht prüfen konntest, schreib das dazu. Trenne bewiesene Befunde
  (Test, Messung, eindeutige Codestelle) von Vermutungen.
* Halte fest, was genau du geprüft hast: `git rev-parse HEAD`,
  `git status --short` und die SHA-256 von
  `instruments/torus-classic/components.json`.

## Bauen und Testen

Windows x64, CMake ≥ 3.25, Visual Studio 2026 oder 2022 (Build Tools reichen).
Aus `vst3_Plugins/`, mit eigenem Build-Verzeichnis (siehe oben) oder, falls
gerade niemand sonst baut, mit den Presets:

```bash
git submodule update --init external/vst3sdk
git -C external/vst3sdk submodule update --init --depth 1 base cmake pluginterfaces public.sdk vstgui4
cmake --preset windows-vs2026-x64
cmake --build --preset windows-vs2026-x64-release --parallel 4
ctest --preset windows-vs2026-x64-release
```

Jeder Plugin-Build endet mit Steinbergs `validator` (bisher: 47 bestanden,
0 fehlgeschlagen, je Bundle). CTest führt sechs Einträge: zwei von Toy Piano
Classic und `torus-classic.engine`, `.adapter`, `.plugin`, `.provenance`.
Der Adapter-Test läuft in Release etwa 25 s, in Debug etwa 60 s.

Nützlich:

* `bench_torus_classic.exe` (wird mitgebaut, kein Test) druckt die
  Messwerte des Build-Berichts.
* AddressSanitizer: zweites Build-Verzeichnis mit
  `-DCMAKE_CXX_FLAGS="/DWIN32 /D_WINDOWS /EHsc /fsanitize=address /Zi"`,
  Release bauen; zum Ausführen der Tests das `bin\Hostx64\x64` des Compilers
  in den `PATH` nehmen, sonst starten sie nicht (0xC0000135).
* `build/…/bin/Release/editorhost.exe` öffnet das Fenster ohne DAW (kein
  Audio); `scripts/capture-editor.ps1` bildet es ab und kann klicken.
* Die vollständigen Ausgaben der letzten Läufe des Verfassers liegen in
  `out/torus-classic-0.1.0/logs/` (nicht versioniert; nur zum Vergleich).

## Aufbau

Vier Schichten, je ein CMake-Ziel; jede kennt nur die darunter.

| Bereich | Dateien |
| --- | --- |
| Schicht 1: Torus-DSP und stmlib, übernommen und gepatcht | `core/torus/`, `core/stmlib/`; neu: `core/torus/dsp/rng.h` |
| Schicht 2: trockene Engine | `engine/synth.*` (aus `physical_modeling.*` der Firmware, ohne Hall), `engine/parameter_manager.h`, `engine/audio_output.h`, `engine/config.h`; Fassade `engine/dry_engine.*` (`DryEngine`, PIMPL, zeigt keine DSP-Typen) |
| Schicht 3: Streaming-Adapter | `adapter/stream_adapter.*` (`StreamAdapter`: `Prepare`, `Reset`, `AddEvent`, `Process`, `RenderQuantum`, `Compact`), `adapter/sinc_kernel.*` (`MakeRateRatio`, `SincKernel`) |
| Schicht 4: Audioteil | `source/processor.*` (`Processor`: `process`, `Render`, `ReadParameterChanges`, `ReadKeyboard`, `ReadEvents`, `Restart`, `ApplyLoadedState`, `setupProcessing`, `setActive`, `getState`/`setState`, `notify`), `source/engine_bridge.h`, `source/latest_value.h`, `source/spsc_queue.h`, `source/state_io.h` |
| Schicht 4: Steuerteil | `source/controller.*` (Parameterklassen `LinearParameter`, `ModelParameter`, `PitchBendParameter`; MIDI-Zuordnung; Zustände; `PlayNote`; `editorAttached`/`editorRemoved`/`editorDestroyed`) |
| Schicht 4: Fenster | `source/editor.*`, `source/widgets.*` (`KeyboardView`, `ToggleButton`, `ModelSelector`), `source/view_layout.h` |
| Parameter, Texteingabe | `source/parameters.h`, `source/value_text.h` |
| Registrierung | `source/factory.cpp`, `source/plugin_ids.h`, `source/version.h` |
| Herkunft | `UPSTREAM.md`, `components.json`, `SHA256SUMS.upstream`, `tools/update_provenance.ps1`, `tests/verify_provenance.cmake` |
| Referenz nur für Tests | `tests/reference/` (Firmware-Engine samt Hall, unverändert), `tests/reference/reference_api.*` |
| Tests | `tests/test_engine.cpp`, `tests/test_adapter.cpp`, `tests/test_plugin.cpp`, `tests/test_support.h`, `tests/bench.cpp` |
| Build | `CMakeLists.txt` (Wurzel, eine Zeile neu), `instruments/torus-classic/CMakeLists.txt` |

Die Threads, wie der Code sie annimmt: `process()` auf dem Audio-Thread des
Hosts; `setupProcessing()` und `setActive()` nur, während `process()` nicht
läuft; `notify()`, `setState()`, `getState()` und alles im Controller und im
Fenster auf dem UI-Thread, `setState()` möglicherweise während `process()`.
Zwischen den Threads liegen: Atomics (`values_`, `latency_`), `LatestValue`
(geladener Zustand, ein Schreiber, ein Leser; Schreiber über `load_mutex_`
serialisiert) und die SPSC-Queue `keyboard_notes_`. Engine und Adapter gehören
dem Audio-Thread.

Die Zeit, wie der Adapter sie rechnet: Host-Sample n steht für die interne
Zeit n · 48000 / Hostrate, als exakter Bruch. Ein Ereignis wirkt am ersten
12-Sample-Quantum, das nicht vor seiner internen Zeit beginnt. Die Ausgabe
ist um `LatencySamples()` verzögert (0 bei 48 kHz). Die Engine rendert nur so
weit, wie das gerade verlangte Ausgabe-Sample braucht.

## Prüfpunkte

1. **Hallentfernung und Herkunft.**
   * Ist `engine/synth.cpp` wirklich der trockene Pfad von
     `tests/reference/src/physical_modeling.cpp`, Anweisung für Anweisung?
     Reihenfolge von Fade, Polaritätswechsel des sechsten Modells und
     `Output()`; Velocity mal Excitation; höchstens vier Trigger je Quantum.
   * Enthält das Plugin noch irgendetwas vom Hall (Instanz, Puffer, Parameter,
     Includes, Symbole im gebauten Binary)?
   * Stimmen die Eingriffe in `core/` mit der Liste in `UPSTREAM.md` überein,
     vollständig und ohne weitere? Vergleiche mit `tests/reference/` und,
     wenn vorhanden, mit `../Daisy-Seed` @ `10f906f`.
   * Sind `components.json` und `SHA256SUMS.upstream` richtig, und prüft
     `tests/verify_provenance.cmake`, was es soll?
2. **Aussagekraft des Referenzvergleichs** (`TestDryReference`,
   `TestAgainstFirmware`).
   * Vergleicht der Test wirklich zwei getrennt kompilierte Engines, oder
     landen durch die Namensraum-Umbenennung per Compiler-Definition
     (`torus=torus_reference` usw.) doch gemeinsame Symbole im Testprogramm?
   * Die Referenz startet mit Dry/Wet 0,25; der Test rendert 400 stille Quanta
     und prüft dann auf exakt 0. Reicht das, und läuft der Vergleich wirklich
     erst danach?
   * Könnte die Gleichheit trivial erfüllt sein (Stille, zu kurze Folgen,
     Modelle oder Parameter, die gar nicht erreicht werden)?
   * CMake kompiliert `tests/reference/torus/resources.cpp` aus einer Kopie
     ohne zwei leere Arrays. Ändert das sonst etwas?
3. **Instanzisolierung.**
   * `torus::Rng` gegen `stmlib::Random`: gleiche Folge, gleiche
     Ziehreihenfolge über Plucker und Saiten?
   * `torus::InitResources()` unter `std::call_once`, aufgerufen nur im
     Konstruktor von `DryEngine`. Gibt es einen Weg, auf dem eine Instanz die
     Sinustabelle liest, bevor sie gefüllt ist, oder schreibt, während eine
     andere liest?
   * Weitere veränderliche globale oder statische Zustände in `core/`,
     `engine/`, `adapter/`, `source/`?
   * `DryEngine::Impl::Construct`: Speicher nullen, Placement-new, `Init`.
     Sauber, auch beim wiederholten Reset?
4. **Adapter: Zeit, Kausalität, Grenzen** (`adapter/stream_adapter.cpp`).
   * Latenzformel in `Prepare()`: Reicht `(Latenz + 1) · num / den ≥ Right() + 1`
     wirklich aus, damit nie ein Ereignis eines späteren Blocks in ein schon
     gerendertes Quantum gehört? Auch für Raten mit Schritt > 12 interne
     Samples je Host-Sample (unter 4 kHz) und für die größten Raten?
   * Jeder Pufferindex in `Process()`, `RenderQuantum()`, `Compact()` im
     Bereich, direkt nach `Reset()` und bei jeder Rate von 1 kHz bis 4 MHz?
   * Überläufe in `AddEvent()` (Offset · num), in den Positionszählern bei
     langer Laufzeit, in `MakeRateRatio()` bei krummen Raten.
   * Volle Ereignis-Queue: Noten werden verworfen und gezählt; Parameter
     ersetzen den jüngsten wartenden ihrer Art oder wirken sofort. Kann dabei
     ein Wert verloren gehen oder ein älterer einen neueren überschreiben?
   * Zusammenfassen gleicher Parameter im selben Quantum: Reihenfolge
     gegenüber Noten und anderen Parametern erhalten?
   * Hängt das Ergebnis wirklich nur von den absoluten Positionen ab, nicht
     von der Blockaufteilung? Suche einen Ablauf, bei dem es das nicht tut.
   * `DenormalsAreZero` setzt FTZ/DAZ im MXCSR und stellt es wieder her.
     Vollständig, auch auf jedem Rückweg? Wirkung auf den Host-Thread?
5. **Sampleratenwandlung** (`adapter/sinc_kernel.*`).
   * Tabellenindizes in `Interpolate()` für exakte und interpolierte Tabellen;
     Zeilenzahl, Randkoeffizienten, Normierung je Zeile.
   * Sind die Angaben zu Durchlassbereich, Sperrdämpfung und Latenz im README
     und im Build-Bericht durch die Messmethode in `TestResamplerResponse`
     gedeckt? Misst der Test, was er behauptet (Fit, Fenster, Frequenzraster)?
   * Der Wandler ist eigener Code statt einer Bibliothek; der Buildauftrag
     nannte SDK-Funktion oder eine kleine Abhängigkeit. Bewerte die
     Entscheidung (Begründung in `docs/PLANUNG.md`).
6. **Echtzeit im Audio-Thread** (`Processor::process` und alles darunter).
   * Keine Allokation, keine Sperren, nichts Blockierendes? Auch nicht
     versteckt: `std::chrono::steady_clock::now()` in `ReadKeyboard`,
     `std::call_once`, SDK-Aufrufe, `std::function`.
   * Ein geladener Zustand löst im Audio-Thread einen vollen Engine-Reset aus
     (rund 110 kB nullen und initialisieren). Vertretbar?
   * Feste Obergrenzen aller Schleifen: Klaviatur-Queue, Ereignisliste,
     Parameterpunkte, `while(rendered_ < …)` im Adapter.
7. **Threads und Zustandsübergabe.**
   * `LatestValue`: korrekt bei gleichzeitigem `Publish` und `Take`? Was,
     wenn `setState` doch von zwei Threads kommt (der Mutex liegt nur auf der
     Schreiberseite) oder `setActive` während `process` gerufen wird?
   * `setState` schreibt die Atomics und veröffentlicht den Zustand;
     `process` setzt beim Übernehmen die Atomics erneut. Gibt es eine
     Verschachtelung mit Automation, nach der Processor, `getState()` und
     Controller verschiedene Werte halten?
   * `setActive(true)` wendet einen noch nicht übernommenen Zustand über
     `ApplyLoadedState` an. Dieser Weg hat **keinen Test** (die
     Verschachtelung lässt sich nicht erzwingen). Bitte besonders ansehen.
   * `keyboard_notes_`: genau ein Erzeuger (`notify`), ein Verbraucher
     (`ReadKeyboard`, aber auch das Leeren in `setActive`)? Verfall nach
     500 ms und Verwerfen von Noten vor einem geladenen Zustand über
     Zeitstempel: Grenzfälle (gleicher Zeitstempel, Uhr, Offline-Rendering)?
8. **Noten und MIDI** (`ReadEvents`, `ReadParameterChanges`, Controller).
   * Validierung vor Rundung und Cast: Tonhöhe, Velocity (0, negativ, > 1,
     NaN, unendlich), Offsets (negativ, hinter dem Block, unsortiert).
   * Note-Off und Velocity 0 lösen nichts aus und beenden nichts.
   * Parameterpunkte wirken als Stufen am Quantum, nicht als Rampen. Jeder
     Punkt, nicht nur der letzte des Blocks?
   * Pitch Bend: `PitchBendRaw`/`PitchBendSemitones` gegen `Bend()` der
     Firmware; Mitte exakt 0, auch für 8192/16383 vom Host.
   * Modell: `ModelIndex` (VST3-Stufenregel, `stepCount` 5) gegen
     `DesiredModel()`/`SelectModel()` der Engine. Stimmen Anzeige, Texteingabe,
     normalisierte Werte und DSP-Auswahl an jeder Stufengrenze überein?
   * `getMidiControllerAssignment`: Pitch Wheel, CC 20–25, 28, 33 auf allen
     16 Kanälen von Bus 0; CC 26, 27, 29–32 nicht.
9. **Parameter und Zustand.**
   * IDs 100–108, Bereiche, Vorgaben, Flags laut Buildauftrag Abschnitt 5.
     Tune stufenlos?
   * `value_text.h` und die `fromString`-Wege aller neun Parameter: streng,
     locale-unabhängig, Ausgabewert bei Ablehnung unverändert, auch für
     `+-5`, NaN, unendlich, Überlauf.
   * Zustand Version 1 (acht Werte) in `state_io.h`: Version, Abschneidung an
     jeder Stelle, nicht endliche Werte, Werte außerhalb 0–1. Wird je ein
     Teilzustand übernommen? Lesen Processor und Controller dasselbe?
   * Pitch Bend wird nicht gespeichert und beim Laden auf beiden Seiten
     zentriert. Controller-Zustand (Zoom, Klaviatur) getrennt und geprüft.
   * `getLatencySamples`, `getTailSamples` (`kInfiniteTail`), `silenceFlags`.
     Beim Ratenwechsel ruft das Plugin kein `restartComponent(kLatencyChanged)`;
     trägt die Begründung im Build-Bericht (Abschnitt 9)?
10. **Fenster und Lebensdauer** (`Editor`, `Controller`, `widgets`).
    Übernommen aus Toy Piano Classic 0.3.2 samt den Korrekturen aus dessen
    Reviews; neu sind sieben Regler, `ModelSelector`, `ChooseModel`,
    `controlBeginEdit`/`controlEndEdit`.
    * Freigabe ohne `removed()`, zwei Fenster, erneutes Anhängen derselben
      View, fehlgeschlagenes `open()`: hängende Zeiger in `editors_`, Timer,
      Lambdas, die ihr Objekt überleben?
    * Größenänderung: erst Host fragen, Fenster erst in `onSize` ändern;
      Ablehnung am Mindestzoom; `setContentScaleFactor` vor, nach und während
      `open()`; Rekursion `Resize` → `resizeView` → `onSize`.
    * KEYS und Fenstergröße dürfen nie als Parameter-Edit beim Host ankommen.
      Modellwahl dagegen als begin/perform/end. Hält beides, auch wenn die
      Maus während eines Edits das Fenster verlässt oder das Fenster schließt?
    * `PlayNote` ohne Nachrichtenunterstützung des Hosts.
11. **Gleitkomma-Optionen.**
    * Sind wirklich alle Torus-Ziele mit `/fp:precise` gebaut (in den
      erzeugten Projektdateien oder Compiler-Aufrufen nachsehen), obwohl das
      SDK `/fp:fast` global setzt? Welche Übersetzungseinheiten bleiben
      `/fp:fast`, und rechnen sie etwas?
    * Hält der NaN-Schutz in `Output()` und in der Zustandsprüfung unter den
      tatsächlich verwendeten Optionen?
12. **Build und Tests.**
    * `CMakeLists.txt`: Sichtbarkeit von `TEST` und `_USE_MATH_DEFINES`
      (sollen in `core/` und `engine/` bleiben), abgeschaltete Warnungen nur
      für Fremdcode, statische Runtime, validator, moduleinfo, eigene FUIDs.
    * Toy Piano Classic unverändert und weiter baubar?
    * Belegen die Tests, was Build-Bericht und README daraus ableiten? Achte
      auf: vorab festgelegte Toleranzen (im Quelltext begründet), den
      Allokationszähler (`operator new` je Thread; was sieht er nicht?),
      Tests, die nur die eigene Hülle zweimal vergleichen, und auf die Läufe
      mit zweitem Thread (was beweisen sie, was nicht?).
    * Wo fehlt ein Test, der einen Befund aus Punkt 1–11 gefunden hätte?
      Bekannt ohne Test: Mausbedienung von Reglern und Klaviatur, der
      `setActive`-Weg aus Punkt 7, die Abwesenheit von Sperren.
13. **Doku gegen Code.** Stimmen die Aussagen in `README.md` (besonders
    „Timing", „State", „Differences from the firmware"), `UPSTREAM.md` und
    `docs/BUILD_REPORT_0.1.0.md` mit dem überein, was Code und Tests zeigen?
    Jede allgemeine Aussage im Bericht soll durch einen benannten Test oder
    eine begrenzte Codeaussage gedeckt sein.
14. **Buildauftrag.** Gehe die verbindlichen Punkte des Buildauftrags durch
    (Abschnitte 1, 3 bis 6) und nenne jeden, der nicht oder nur teilweise
    erfüllt ist.

## Bekannt und gewollt (nur melden, wenn die Beschreibung nicht stimmt)

* In keiner DAW geladen, nicht angehört. Der Mensch testet parallel.
* Kein Hall, kein Ersatz. Das sechste Modell heißt „Stereo Strings"; bei
  Position 0,5 sind seine Kanäle exakt gegenphasig.
* Intern 48 kHz und 12 Samples. Noten und Parameter bis 0,25 ms verspätet,
  nie verfrüht; höchstens vier Anschläge je Quantum, weitere in den folgenden.
* Bei anderen Raten als 48 kHz rund 1 ms gemeldete Latenz (49/90/97/195
  Samples bei 44,1/88,2/96/192 kHz); keine zusätzliche Bandbreite; Spitzen
  zwischen den Samples können über 0,8 liegen.
* Denormale Zahlen gelten beim Rendern als null (FTZ/DAZ).
* Alle Instanzen starten ihren Zufallsgenerator vom selben Wert, bei jeder
  Aktivierung und jedem geladenen Zustand.
* Reset bei Aktivierung und beim Laden eines Zustands, nicht bei
  `setProcessing`. Laden verwirft klingende Noten.
* Klaviatur-Noten: Velocity 100, Start mit dem nächsten Audioblock, nicht
  über das MIDI der DAW; verfallen nach 500 ms ohne Audio und beim Laden.
* Zustandswerte außerhalb 0–1 werden abgelehnt, nicht geklemmt.
* Eigener Resampler statt Bibliothek (Punkt 5 bittet um eine Bewertung).
* Fenster- und Textcode sind Kopien aus Toy Piano Classic; nichts liegt in
  `common/`.
* Vor den letzten Builds hat eine zweite Claude-Instanz den Code gelesen und
  vier Randfall-Fehler im Processor gefunden; sie sind behoben (Build-Bericht,
  Abschnitt 8, „Second read"). Das ersetzt diese Prüfung nicht; prüfe die
  vier Korrekturen mit.
* Hersteller-URL und E-Mail leer, keine Presets, kein CI, kein MPE, kein
  Aftertouch, feste vier Stimmen.

## Format des Ergebnisses

```
# Codex Review: Torus Classic 0.1.0
VERDICT: FREIGABE FÜR DAW-TEST | FREIGABE MIT KORREKTUREN | KEINE FREIGABE
Geprüft: <Commit oder „main @ e553451 + Arbeitsverzeichnis">, SHA-256 von components.json, Datum, Compiler

## Befunde
| ID | Schwere | Bereich | Befund | Beleg (Datei:Zeile) | Auslösung und Auswirkung | bewiesen/vermutet | minimale Korrektur |

## Ohne Befund geprüft
(Prüfpunkt, kurz wie geprüft)

## Fehlende Tests

## Abweichungen vom Buildauftrag

## Nicht geprüft
```

Schweregrade:

* **P0** Absturz, Hänger, Speicherfehler, unsicherer Audio-Thread, Verlust
  gespeicherter Projekte.
* **P1** Falsches Verhalten, das ein Nutzer bemerkt, eine unzutreffende
  Aussage in Doku oder Build-Bericht, oder ein Test, der nicht belegt, was
  aus ihm abgeleitet wird.
* **P2** Vor einer Weitergabe zu beheben: Robustheit, Host-Verträglichkeit,
  nicht erfüllter Punkt des Buildauftrags ohne spürbare Folge.
* **P3** Hinweis, Stil, Vereinfachung.

Danach arbeitet Claude die Befunde ein, und der Mensch entscheidet, was davon
umgesetzt wird.
