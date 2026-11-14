# Prüfauftrag an Codex: Toy Piano Classic 0.3.0 (nur Code)

* **Stand:** 2026-11-13
* **Gegenstand:** VST3-Instrument `instruments/toy-piano-classic/` im Repo
  [sscheidl/vst3_Plugins](https://github.com/sscheidl/vst3_Plugins), Branch
  `main` @ `13519b1`
* **Verfasser des Codes:** Claude (Fable 5.2 und Opus 5.5)
* **Ergebnis nach:** `instruments/toy-piano-classic/docs/CODEX_REVIEW_0.3.0.md`

## Auftrag und Grenzen

Prüfe das Plugin unabhängig und kritisch **auf Codeebene**, bevor es in den
DAWs getestet wird. Schwerpunkt sind die Teile, die kein automatischer Test
abdeckt: Threads, Lebensdauer von Objekten, das Zusammenspiel mit dem Host.

* **Ändere keinen Code, keine Doku und keine Tests.** Schreibe nur die
  Ergebnisdatei. Korrigiert wird erst, wenn der Mensch das danach beauftragt.
* Die Regeln in `AGENTS.md` im Playground-Wurzelordner gelten. Wegwerf-Code
  (zusätzliche Tests, Messungen) nur unter `vst3_Plugins/.work/`; der Ordner
  ist von Git ausgenommen. Am Ende `.work/` und `build/` löschen.
* Nichts committen, nichts pushen, keine GitHub-Workflows.
* `dist/` und die installierte Kopie unter
  `%LOCALAPPDATA%\Programs\Common\VST3\` nicht anfassen.
* Die vier Dateien in `core/` (außer `compat/`) sind absichtlich bytegleich mit
  der Firmware. Befunde dort bitte melden, aber als Firmware-Befund
  kennzeichnen; sie werden in Daisy-Seed behoben, nicht hier.
* Keine GUI-Bewertung (Farben, Anordnung). Die Bedienung prüft der Mensch.
* **Behauptungen am Code prüfen, nicht an der Doku.** Wo du etwas nicht prüfen
  konntest, schreib das dazu. Trenne bewiesene Befunde (Test, Messung,
  eindeutige Codestelle) von Vermutungen.

## Bauen und Testen

Windows x64, CMake ≥ 3.25, Visual Studio 2026 oder 2022 (Build Tools reichen).
Aus `vst3_Plugins/`:

```bash
git submodule update --init external/vst3sdk
git -C external/vst3sdk submodule update --init --depth 1 base cmake pluginterfaces public.sdk vstgui4
cmake --preset windows-vs2026-x64
cmake --build --preset windows-vs2026-x64-release
ctest --preset windows-vs2026-x64-release
```

Der erste Build dauert etwa fünf Minuten. Jeder Plugin-Build endet mit
Steinbergs `validator` (erwartet: 47 bestanden, 0 fehlgeschlagen). Für
VS 2022 `vs2026` durch `vs2022` ersetzen. Die Debug-Konfiguration prüft
Array-Grenzen der Standardbibliothek; sie ist für Speicherfragen nützlich.

Werkzeuge: `build/windows-vs2026-x64/bin/Release/editorhost.exe` öffnet das
Fenster ohne DAW (kein Audio). `scripts/capture-editor.ps1` bildet es ab und
kann vorher die Größe ändern und klicken; `scripts/resize-benchmark.ps1` misst
die Zeit pro Größenschritt.

## Aufbau

| Bereich | Dateien |
| --- | --- |
| Klangkern der Firmware, unverändert | `core/toy_piano.*`, `core/config.h`, `core/spsc_queue.h`; Ersatz für DaisySP in `core/compat/Utility/dsp.h` |
| Ergänzungen zum Kern | `source/engine.*` (`Engine`: Tonhöhenversatz mit Glättung, Volume vor dem Limiter, Schutz oberhalb Nyquist) |
| Parameter, Konstanten | `source/parameters.h` |
| Audioteil | `source/processor.*` (`Processor`: `process`, `Render`, `ReadParameterChanges`, `UpdateEngine`, `setActive`, `getState`/`setState`, `notify`) |
| Steuerteil | `source/controller.*` (`Controller`: Parameter, `getMidiControllerAssignment`, `setComponentState`, `getState`/`setState` für die Fenstereinstellungen, `createView`, `PlayNote`, `editorAttached`/`editorRemoved`) |
| Fenster | `source/editor.*` (`Editor`; `ConstrainEditorSize`, `DragConstraint`, `EditorSizeFor`, `EditorZoomForSize` in `editor.h`) |
| Klaviatur | `source/keyboard_layout.h` (Geometrie), `source/keyboard_view.*` (`KeyboardView`, `ToggleButton`) |
| Registrierung | `source/factory.cpp`, `source/plugin_ids.h`, `source/version.h` |
| Build | `CMakeLists.txt` (Wurzel), `instruments/toy-piano-classic/CMakeLists.txt`, `CMakePresets.json` |
| Tests | `tests/test_toy_piano_core.cpp` (Kern-Test der Firmware), `tests/test_plugin.cpp` (Plugin) |

Die Threads, wie der Code sie annimmt: `process()` auf dem Audio-Thread des
Hosts; `notify()`, `setActive()`, `setState()` und alles im Controller und im
Fenster auf dem UI-Thread. Zwischen beiden liegen nur Atomics (Parameterwerte)
und die SPSC-Queue `keyboard_notes_` (Noten der Bildschirm-Klaviatur).

## Prüfpunkte

1. **Echtzeit im Audio-Thread** (`Processor::process`, `Render`,
   `Engine::Process`). Keine Allokation, keine Sperren, nichts Blockierendes?
   Auch nicht versteckt, etwa in `std::function`, Logging oder dem SDK.
2. **Threads und Datenaustausch.**
   * Sind die Atomics in `Processor` ausreichend? Kann `setState` während
     `process` einen halb geschriebenen Zustand erzeugen, und wäre das schlimm?
   * `keyboard_notes_` ist eine SPSC-Queue. Hält die Annahme „genau ein
     Erzeuger" (`notify`) und „genau ein Verbraucher" (`Render`, aber auch das
     Leeren in `setActive`)? Was, wenn ein Host `notify` von einem anderen
     Thread als dem UI-Thread aufruft, oder `setActive` während `process`?
   * Volle Queue: Noten werden verworfen. Gibt es einen Fall, in dem sich die
     Queue dauerhaft füllt (Plugin aktiv, aber `process` wird nicht gerufen)?
3. **Notenweg der Klaviatur** (`Controller::PlayNote` → `Processor::notify` →
   `Render`). Prüfe Validierung, Verhalten ohne Nachrichtenunterstützung des
   Hosts, und die Aussage „klingt bitgenau wie eine MIDI-Note am Blockanfang".
4. **MIDI-Ereignisse** (`Render`). Annahme: Ereignisse sind nach `sampleOffset`
   sortiert. Was passiert bei unsortierten Ereignissen, negativen oder zu
   großen Offsets, Velocity 0, Noten außerhalb 0–127? Note-Off wird absichtlich
   ignoriert.
5. **Parameter und Zustand.**
   * Prozessor-Zustand (Version 1: Master Pitch, Attack, Decay, Volume) und
     Controller-Zustand (Version 2: Zoom und Klaviatur; Version 1 aus 0.2.0 muss
     lesbar bleiben). Robust gegen abgeschnittene oder fremde Daten?
   * Stimmt die Rundung `MasterPitchSemitones` mit dem überein, was Hosts und
     `RangeParameter` (48 Schritte) anzeigen und senden?
   * Pitch Bend ist ein Parameter mit `kCanAutomate`, wird aber nicht
     gespeichert. Ist das so richtig, oder sollte er anders markiert sein?
   * `fromString`/`toString` in `LogTimeParameter` und `VolumeParameter`
     (Controller), auch für Eingaben wie `-inf`, leere Strings, Unsinn.
   * `getMidiControllerAssignment`: Pitch Wheel, CC 16, 73, 75 auf allen Kanälen.
6. **Klangteil** (`Engine`).
   * Schutz oberhalb Nyquist (`ApplyPitch`): korrekt für alle Abtastraten von
     8 bis 192 kHz und alle Tonhöhen bis +26 Halbtöne (Master Pitch plus Pitch
     Bend)? Kann der Kern je über seine Sinustabelle hinauslesen?
   * Glättung von Tonhöhe und Volume: Klicks, Endlosschleifen beim Annähern,
     denormale Zahlen?
   * Die Aussage „bitgenau wie der Kern bei Master Pitch 0, Volume 0 dB, Pitch
     Wheel in der Mitte": Gilt sie unter allen Bedingungen, die der Test nicht
     abdeckt (etwa nach einer Volume-Änderung zurück auf 0 dB)?
7. **Fenster und Lebensdauer** (`Editor`, `Controller`).
   * `editors_` hält rohe Zeiger, gepflegt über `editorAttached`/`editorRemoved`.
     Kann ein Zeiger hängen bleiben, etwa wenn ein Host `removed()` nicht ruft
     oder zwei Fenster gleichzeitig offen hat?
   * Die Lambdas in `createView` fangen den Controller ein, die in `open()` den
     Editor. Kann eines davon nach dem Ende seines Objekts aufgerufen werden
     (Timer, Mausereignis während `close()`)?
   * `close()` und erneutes `open()`: `applied_zoom_`, `settle_pending_`,
     `keyboard_view_`, `keyboard_button_` korrekt zurückgesetzt?
8. **Größenänderung** (`checkSizeConstraint`, `onSize`, `Resize`,
   `setContentScaleFactor`, `getSize`, `DragConstraint`).
   * Rekursion: `Resize` → Host `resizeView` → `onSize`. Endlos möglich?
   * Hosts, die `checkSizeConstraint` ohne `onSize` aufrufen, `onSize` mit
     Größen schicken, die nie erlaubt wurden, oder `setContentScaleFactor` vor,
     nach oder mehrfach während `open()` senden.
   * Der Fall, dass ein Host `resizeView` ablehnt (Rückfall in `Resize`).
   * `DragConstraint` erkennt Kante oder Ecke je Ziehvorgang und beginnt nach
     250 ms Pause neu. Tragfähig, oder gibt es Hosts/Abläufe, bei denen das
     falsch greift?
   * Die Grenzen 50–200 % gelten für den Zoom des Nutzers, nicht für Zoom mal
     Anzeigeskalierung. Ist das überall so umgesetzt?
9. **Klaviatur** (`keyboard_layout.h`, `KeyboardView`, `ToggleButton`).
   Trefferprüfung an Tastengrenzen, Ziehen über die Tasten, Abbruch der Maus,
   Koordinaten bei Zoom. `ToggleButton` ist absichtlich kein `CControl`, damit
   kein `beginEdit` mit einer Nicht-Parameter-ID an den Host geht. Hält das?
10. **Build und Tests.**
    * `CMakeLists.txt`: Bindet `include_directories(SYSTEM ...)` und
      `SYSTEM TRUE` auf den VSTGUI-Zielen nur SDK-Warnungen aus, nicht eigene?
      Statische Runtime, validator, moduleinfo korrekt eingerichtet?
    * `tests/test_plugin.cpp` definiert `void* moduleHandle = nullptr;`, damit
      die Plugin-Klassen in einer EXE linken. Kann das etwas verdecken?
    * Belegen die Tests, was sie behaupten? Wo fehlt ein Test, der einen
      Befund aus Punkt 1–9 gefunden hätte?
11. **Doku gegen Code.** Stimmt jede Zeile der Tabelle „Verified" in
    `instruments/toy-piano-classic/README.md` mit dem überein, was Tests und
    Code tatsächlich zeigen?

## Bekannt und gewollt (nur melden, wenn die Beschreibung nicht stimmt)

* In keiner DAW getestet außer: 0.1.0 lädt in Studio One, Regler reagieren.
* Note-Off wird ignoriert; jeder Anschlag ist ein One-Shot wie in der Firmware.
* Parameteränderungen wirken an Blockgrenzen (letzter Wert des Blocks).
* Klaviatur-Noten: feste Velocity 100, Start mit dem nächsten Audioblock, nicht
  über das MIDI der DAW, eingehende MIDI-Noten leuchten nicht mit.
* Der Fensterinhalt skaliert, er ordnet sich nicht neu an.
* Pegel einer Einzelnote bei 0 dB etwa −27 dBFS (Firmware-Verstärkung).
* Hersteller-URL und E-Mail leer, keine Presets, kein CI, CPU-Last ungemessen.

## Format des Ergebnisses

```
# Codex Review: Toy Piano Classic 0.3.0
VERDICT: FREIGABE FÜR DAW-TEST | FREIGABE MIT KORREKTUREN | KEINE FREIGABE
Geprüft: main @ <Commit>, Datum, Compiler

## Befunde
| ID | Schwere | Bereich | Befund | Beleg (Datei:Zeile) | Auslösung und Auswirkung | bewiesen/vermutet | minimale Korrektur |

## Ohne Befund geprüft
(Prüfpunkt, kurz wie geprüft)

## Fehlende Tests

## Nicht geprüft
```

Schweregrade:

* **P0** Absturz, Hänger, Speicherfehler, unsicherer Audio-Thread, Verlust
  gespeicherter Projekte.
* **P1** Falsches Verhalten, das ein Nutzer bemerkt, oder eine Aussage in der
  Doku, die nicht stimmt.
* **P2** Vor einer Weitergabe zu beheben: Robustheit, Host-Verträglichkeit.
* **P3** Hinweis, Stil, Vereinfachung.

Danach arbeitet Claude die Befunde ein, und der Mensch entscheidet, was davon
umgesetzt wird.
