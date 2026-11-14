# Prüfauftrag an Codex: Torus Classic 0.2.0 (Nachprüfung und Ergänzungen, nur Code)

* **Stand:** 2026-11-14
* **Gegenstand:** `instruments/torus-classic/` im Repo
  [sscheidl/vst3_Plugins](https://github.com/sscheidl/vst3_Plugins), Branch
  `torus-classic-0.1.0` (Pull Request 1). Zwei Commits nach dem zuletzt
  geprüften `d2e62ab`:
  * `9f09ef9`: Version 0.1.1, die Korrekturen zu deinem Review.
  * der Commit danach (Spitze des Branches): Version 0.2.0, fünf Ergänzungen.
* **Verfasser des Codes:** Claude (Fable 5.2)
* **Grundlage:** dein Bericht [CODEX_REVIEW_0.1.0.md](CODEX_REVIEW_0.1.0.md),
  die [Umsetzung](CODEX_REVIEW_0.1.0_UMSETZUNG.md), das
  [Änderungsprotokoll](../CHANGELOG.md), das [README](../README.md); für alles
  Übrige weiter der [Buildauftrag](../../../docs/CLAUDE_BUILDAUFTRAG_TORUS_CLASSIC_0.1.0.md)
* **Ergebnis nach:** `instruments/torus-classic/docs/CODEX_REVIEW_0.2.0.md`

## Auftrag und Grenzen

Zwei Teile, bitte getrennt berichten:

1. **Nachprüfung** der vier Befunde TC-R01 bis TC-R04: behoben, ohne neue
   Fehler? `git diff d2e62ab 9f09ef9` zeigt genau diese Runde.
2. **Prüfung der Ergänzungen** aus 0.2.0: `git diff 9f09ef9 HEAD`.

Es gelten alle Regeln des ersten
[Prüfauftrags](CODEX_PRUEFAUFTRAG_0.1.0.md), Abschnitt „Auftrag und
Grenzen": nichts ändern außer der Ergebnisdatei, eigenes Build-Verzeichnis
unter `.work/torus-classic-review/`, `build/`, `dist/`, `out/` und die
installierten Kopien nicht anfassen, Behauptungen am Code prüfen, Bewiesenes
von Vermutetem trennen. Dazu:

* Dein Arbeitsordner `.work/torus-classic-review/` vom ersten Lauf liegt
  noch da; du kannst ihn weiterverwenden. Bitte am Ende löschen.
* **Keine Fenster-, GUI- oder DAW-Tests.** Der Nutzer prüft Bedienung, Optik
  und Funktion selbst in Studio One. Die vorhandenen Tests, die ein Fenster
  öffnen (`TestEditorInWindow`), brauchst du nicht zu starten; Sonden ohne
  Fenster wie beim ersten Mal sind willkommen.
* Was im ersten Review ohne Befund geprüft wurde und sich nicht geändert hat
  (Hallentfernung, Herkunft, Referenzvergleich als Verfahren,
  Instanzisolierung, Resampler, Latenzformel), musst du nicht wiederholen.
  `core/` und `engine/` sind seit `d2e62ab` unverändert bis auf
  `engine/synth.*` und `engine/dry_engine.cpp` (Aufruf von
  `torus::InitResources` verlegt).

## Bauen und Testen

Wie im ersten Prüfauftrag. CTest führt weiter sechs Einträge. Stand des
Verfassers für 0.2.0: VS 2026 Release und Debug, VS 2022 Release, Validator
je 47 bestanden und 0 fehlgeschlagen, CTest je 6 von 6, AddressSanitizer ohne
Meldung; die Ausgaben liegen unversioniert in `out/torus-classic-0.2.0/logs/`.

## Teil 1: Nachprüfung der Korrekturen (0.1.1)

| ID | Was geändert wurde | Bitte prüfen |
| --- | --- | --- |
| TC-R01 | Neu `source/parameter_state.h`: Generationsnummer je geladenem Zustand, `LatestValue` für den vollständigen Schnappschuss, je Parameter ein Sequenz-Lock für Automationswerte mit Generation. `Processor::Restart` nimmt nur noch einen übergebenen Wertesatz. `getState` und der Neustart in `setActive` lesen `ParameterState::Current()` | Ist der Sequenz-Lock mit seinen Speicherordnungen korrekt (ein Schreiber, Leser unter Mutex)? Kann `Current()` einen Wert liefern, der zu keinem geladenen Zustand und keiner Automation gehört? Verhalten, wenn doch zwei Threads `Write` rufen. Hält `TestState` (zwei Zustände im Wechsel, Ein-Sample-Blöcke) und `TestParameterState`, was die Umsetzung daraus ableitet? Dein Sondenlauf vom ersten Mal gegen den neuen Stand |
| TC-R02 | `Editor` überschreibt `controlBeginEdit`/`controlEndEdit` nicht mehr | Genau ein begin/perform/end je Griff, auch verschachtelt und beim Mausrad? Bleibt irgendein Weg, auf dem ein Edit gar nicht mehr gemeldet wird? |
| TC-R03 | `adapter/stream_adapter.*`: getrennte Ring-Queues für Noten (1024) und je Regler, Modell, Bend, Filter (je 256). Volle Regler-Queue: der jüngste wartende Punkt wird durch den neuen ersetzt, der seinen Zeitpunkt behält. Innerhalb einer Queue bleibt die Ankunftsreihenfolge (ein Ereignis mit früherer Zeit als sein Vorgänger wirkt mit ihm) | Wirkt unter irgendeiner Überlast oder Reihenfolge noch etwas vor seinem Zeitpunkt? Ist das Zusammenlegen und Ersetzen fehlerfrei (Ring-Indizes, `Last()`, leere Queue)? Die neue Reihenfolge im Quantum (erst alle Regler, dann Noten): gibt es einen Fall, in dem sie hörbar anders ist als die Ankunftsreihenfolge? Ist die Blockunabhängigkeit bei sortierten Offsets weiter gegeben? |
| TC-R04 | `Editor::close()` beendet laufende Regler-Edits (`while(isEditing()) endEdit()`) vor dem Abbau | Genau ein Ende je begonnenem Edit, auch bei Freigabe ohne `removed()` und beim Mausrad-Nachlauf (Timer)? Kann `endEdit` hier auf einen schon abgebauten Frame treffen? |

Zusätzlich aus derselben Runde, bitte mitprüfen:

* `ReadKeyboard` läuft nur noch in Blöcken mit Audio; Klaviatur-Noten von vor
  einem geladenen Zustand verfallen (Zeitstempelvergleich in
  `Processor::ReadKeyboard`).
* `torus::InitResources` wird nur noch im Konstruktor von `DryEngine`
  gerufen; `Synth::Init` verlässt sich darauf.
* Die Punkte unter „Fehlende Tests" deines Berichts: 1 bis 4 sind als Tests
  umgesetzt, 5 (Aktivierung mit nicht übernommenem Zustand) über
  `TestParameterState` und einen Abschnitt in `TestState`. Reicht das?

## Teil 2: Ergänzungen (0.2.0)

Vom Nutzer nach dem ersten DAW-Test verlangt. Aufbau:

| Ergänzung | Dateien |
| --- | --- |
| Tiefpass und Limiter am Ende der Kette, mit Hostrate | neu `adapter/output_stage.{h,cpp}`; eingebunden in `StreamAdapter::Process` (je Sample nach der Wandlung), Ereignistyp `EngineEvent::kOutputFilter`, eigene Queue |
| Damping umgekehrt | `EngineDamping` in `source/parameters.h`, angewandt in `Processor` (`DirectControl`) |
| Tune in Halbtönen, Schalter „Tune Free" (Parameter 110) | `TuneSounding` in `parameters.h`; kombiniert in `source/engine_bridge.h` beim Quantum; `TuneParameter`, `SwitchParameter` im Controller; FREE-Knopf und Einrasten im Editor |
| Filter-Parameter 109, CC 74 | `FilterParameter` im Controller, `FilterEvent`, Regler im Editor |
| MIDI aus der Bildschirm-Klaviatur | Event-Ausgang in `Processor::initialize`; `SendKey`, `held_keys_`, `release_held_keys_` in `Processor`; Loslassen über `Controller::ReleaseNote` und `KeyboardView` |
| Zustand Version 2 | `source/state_io.h` (`FromVersion1`, `kSavedIndex`), `ParameterState::Load`, `Processor::getState`, `Controller::setComponentState` |

Prüfpunkte:

1. **Limiter** (`OutputStage::Process`). Verlässt wirklich kein Sample die
   Stufe über `kCeiling`, auch bei NaN, Unendlich, Gleichanteil, sehr hohen
   Raten? Erreicht die Verstärkung nach der Erholung exakt 1 (die Stelle, an
   der der Schritt kleiner wird als die Float-Auflösung)? Unterhalb der Decke
   bitgenau unverändert?
2. **Tiefpass.** Stabilität und Koeffizienten des TPT-SVF bei allen Raten
   und bei bewegter Grenzfrequenz; die Überblendung zum ungefilterten Signal
   oberhalb `kTop`; der Filterzustand, wenn die Stufe lange übersprungen
   wurde (`mix_ == 0`) und dann wieder greift; Glättung, die ihr Ziel
   erreicht.
3. **Zeitpunkt des Filter-Ereignisses.** Es wirkt, wenn sein Quantum
   gerendert wird, also etwa zur Hostzeit des Ereignisses, während der Klang
   desselben Quantums erst nach der Latenz am Ausgang ist. Ist die Aussage im
   README („at or just after the sample it was sent for") richtig, und bleibt
   das Ergebnis unabhängig von der Blockaufteilung?
4. **Referenzaussage.** Die Gleichheit mit der Firmware gilt jetzt nur bei
   offenem Filter und Pegel unter der Limiter-Decke; `TestAgainstFirmware`
   hält dafür Volume bei höchstens 50 % und prüft die Vorbedingung. Trägt
   der Test die Aussage im README noch? Die Abbildungen Damping und Tune
   liegen im Test auf der Firmware-Seite (`RenderFirmware`) mit denselben
   Funktionen wie im Produkt; sind sie anderswo unabhängig genug geprüft?
5. **Tune und Schalter.** Beide kommen als getrennte Ereignisse und werden
   in `EngineBridge` zusammengeführt. Reihenfolge innerhalb eines Quantums,
   Neustart (`JumpTune`), Rundung an Halbtongrenzen in Float gegen die
   Anzeige in Double. Der Parameter bleibt für den Host stufenlos, die
   Anzeige rundet: Stimmen Anzeige, Texteingabe und Klang überein? Der
   Controller aktualisiert die Anzeige von Tune beim Umschalten nur im
   eigenen Fenster, nicht beim Host (kein `restartComponent`). Befund oder
   vertretbar?
6. **Damping.** Überall gespiegelt, wo der Wert zur Engine geht (Ereignis,
   Neustart), und nirgends doppelt? Automation alter Projekte wirkt jetzt
   umgekehrt; das ist dokumentiert und gewollt.
7. **Zustand Version 2 und 1.** Reihenfolge der zehn Werte in `getState`,
   `setState`, `setComponentState`, `ParameterState::Load` (in der ersten
   Fassung schrieb `getState` sie falsch; ein Test fand es). Umrechnung von
   Version 1 einschließlich der Regel für den Schalter. Abgeschnittene und
   beschädigte Zustände beider Versionen.
8. **MIDI-Ausgang.** Paarung von Note-on und Note-off unter allen Abläufen:
   Gleiten über Tasten, dieselbe Taste zweimal, verfallene Anschläge, volle
   Queue, Deaktivierung mit gehaltener Taste, Fenster schließt mit gehaltener
   Taste, Host ohne `outputEvents`. Ist das Schreiben in die Ausgabeliste des
   Hosts im Audio-Thread so in Ordnung (Felder des `Event`, `busIndex`,
   Flags)? `held_keys_` wird nur vom Audio-Thread benutzt,
   `release_held_keys_` in `setActive` gesetzt: Wettlauf?
9. **Neue Parameter im Controller.** IDs 109 und 110 hinten angefügt, Flags,
   `stepCount`, strenge Texteingabe („off", „on", Hz), MIDI-Zuordnung mit
   jetzt zehn Einträgen.
10. **Echtzeit.** Kommt durch die Ergänzungen etwas Blockierendes oder eine
    Allokation in `process()` (Ausgabeliste des Hosts, `std::bitset`,
    `tan` je Sample während der Filterglättung)?
11. **Doku gegen Code:** README (Abschnitte „Sound", „Level", „MIDI",
    „State", „Differences"), `CHANGELOG.md`, `docs/PLANUNG.md`.

## Bekannt und gewollt (nur melden, wenn die Beschreibung nicht stimmt)

* Alles aus dem ersten Prüfauftrag, soweit nicht unten ersetzt.
* Der Limiter hat kein Look-ahead und greift sprunghaft zu; die Decke von
  −6 dBFS ist eine feste Konstante. Laute Passagen weichen damit von der
  Firmware ab.
* Filter und Limiter arbeiten mit der Hostrate, nach der Wandlung.
* Der Build-Bericht beschreibt 0.1.0 und bleibt unverändert; für 0.1.1 und
  0.2.0 gibt es die Umsetzung und das Änderungsprotokoll, keinen neuen
  Bericht.
* Für die Ergänzungen gibt es keine neuen Fenstertests; Bild und
  Fenstermaße im README stammen von 0.1.0 (jetzt acht Regler bei gleicher
  Fensterbreite).
* 0.1.1 wurde nur mit VS 2026 Release geprüft; die volle Matrix lief über
  0.2.0.

## Format des Ergebnisses

```
# Codex Review: Torus Classic 0.2.0
VERDICT: FREIGABE FÜR DAW-TEST | FREIGABE MIT KORREKTUREN | KEINE FREIGABE
Geprüft: <Commit>, Datum, Compiler

## Nachprüfung TC-R01 bis TC-R04
| ID | Ergebnis (behoben / teilweise / nicht behoben) | Beleg | Rest |

## Neue Befunde
| ID | Schwere | Bereich | Befund | Beleg (Datei:Zeile) | Auslösung und Auswirkung | bewiesen/vermutet | minimale Korrektur |

## Ohne Befund geprüft

## Fehlende Tests

## Nicht geprüft
```

Schweregrade wie im ersten Prüfauftrag. Neue Befunde bitte ab `TC-R05`
zählen. Danach arbeitet Claude die Befunde ein, und der Mensch entscheidet,
was davon umgesetzt wird.
