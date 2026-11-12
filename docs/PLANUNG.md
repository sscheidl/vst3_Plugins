# Planung

Stand: 2026-11-13. Das erste Testobjekt, Toy Piano Classic 0.1.0, ist gebaut
und automatisch geprüft. In Studio One lädt es, die Regler reagieren. Angehört
wurde es noch nicht; Waveform und Live stehen aus.

## Entscheidungen

Getroffen am 2026-11-12.

### Technische Basis und Repository

| Thema | Entscheidung |
| --- | --- |
| Framework | Steinberg VST3 SDK direkt, GUI mit VSTGUI |
| Plattform | Nur Windows x64 |
| Formate | Nur VST3 |
| Sichtbarkeit und Lizenz | Öffentlich, MIT |
| Hersteller | TAUREON |
| Code aus Daisy-Seed | Kopie mit Commit-Vermerk |
| SDK-Einbindung | Git-Submodul `external/vst3sdk`, Tag `v3.8.0_build_66`, nur die fünf benötigten Teile |
| C++ und Laufzeit | C++20, statische MSVC-Runtime: kein VC-Redistributable nötig |
| Build-Struktur | Ein Wurzelprojekt, je Plugin ein Unterverzeichnis |
| Installation | Von Hand aus dem Build-Ordner, keine automatische Verknüpfung |
| Dokumentation | README-Dateien englisch, Planung deutsch |

Die letzten fünf Zeilen sind bisher Vorschläge, die so umgesetzt wurden. Sie
lassen sich ändern, solange kein zweites Plugin darauf aufbaut.

### Toy Piano Classic

| Thema | Entscheidung |
| --- | --- |
| „Classic" | Klangkern ohne Modulationsmatrix, Ambient und Tape |
| Note-Off | Wird ignoriert, jeder Anschlag ist ein One-Shot |
| Master Pitch | Halbtonschritte, −24 bis +24 |
| Polyphonie | 16 Stimmen |
| Pegel | Eigener Volume-Regler |
| MIDI | Noten mit Velocity, Pitch Bend, CCs für Attack, Decay und Pitch |
| GUI | Regler mit Wertanzeige, feste Größe |
| Test-Hosts | Studio One, Tracktion Waveform 14, Ableton Live 12 Lite |

Bei der Umsetzung selbst festgelegt, weil die Vorgaben es offen ließen:

| Thema | Festlegung | Begründung |
| --- | --- | --- |
| Volume: Bereich | Stille, dann −60 bis +24 dB, Vorgabe 0 dB | 0 dB ist der Pegel der Firmware; damit bleibt die Vorgabe bitgenau gleich |
| Volume: Position | Vor dem Soft-Limiter des Kerns | Der Ausgang bleibt bei jeder Stellung unter 0,8; lauter heißt dichter, nicht übersteuert |
| Pitch-Bend-Bereich | ±2 Halbtöne, stufenlos | Üblicher Standardwert |
| CC-Nummern | Attack 73, Decay 75, Master Pitch 16 | 73 und 75 sind im MIDI-Standard für diese Zeiten vorgesehen; für Transposition gibt es keine Standardnummer |
| Vorgabewerte | Attack 2 ms, Decay 583 ms | Kurzer Anschlag, mittlerer Ausklang |
| Master Pitch: Wirkung | Sofort auf klingende Noten, über etwa 5 ms geglättet | Nur so ist der Regler ohne Knackser automatisierbar |
| Messlatte | Bitgenau gleich dem Kern bei Master Pitch 0 und Volume 0 dB | Schärfster möglicher Nachweis, dass die Hülle den Klang nicht verändert |

## Stand des Fahrplans

| Schritt | Inhalt | Stand |
| --- | --- | --- |
| 1 | Build-Gerüst, SDK, Presets, validator | Erledigt |
| 2 | Klangkern unverändert übernommen, Firmware-Test läuft mit MSVC | Erledigt |
| 3 | Noten, Parameter, Abtastrate des Hosts, Stereo, Zustand | Erledigt |
| 4 | Master Pitch mit Glättung, dazu Pitch Bend und Volume | Erledigt |
| 5 | GUI mit vier Reglern | Erledigt, in Studio One bedient |
| 6 | Abnahme in den DAWs, CPU-Last | **Begonnen:** lädt in Studio One. Hörtest, Waveform, Live und CPU-Last offen |
| 7 | CI auf GitHub | **Offen** |

Die Ergebnisse stehen im
[README des Plugins](../instruments/toy-piano-classic/README.md), Abschnitte
„What the study showed" und „Verified".

## Offen

### Abnahme in den DAWs (Schritt 6)

Die Prüfliste steht im README des Plugins unter „Still to be checked". Die
wichtigste Unbekannte ist die MIDI-Steuerung: VST3 reicht Controller nicht roh
durch. Das Plugin meldet dem Host, welcher Controller zu welchem Parameter
gehört; ob Pitch Bend und die drei CCs ankommen, entscheidet der Host. Das
lässt sich nur in Studio One, Waveform und Live selbst feststellen.

### CI (Schritt 7)

Ein Workflow, der bei jedem Push unter Windows baut, den validator laufen lässt
und die Tests ausführt. Zwei Dinge sind vorher zu klären:

* Das Token, mit dem gepusht wird, braucht den Scope `workflow`. Ohne ihn
  lehnt GitHub jeden Push ab, der eine Datei unter `.github/workflows/` anlegt.
  Mit der GitHub-CLI: `gh auth refresh -s workflow`.
* Welche Visual-Studio-Version die Windows-Runner von GitHub mitbringen, ist
  vor dem Einrichten nachzusehen. Presets gibt es für 2026 und 2022; beide
  sind lokal gebaut und getestet.

### Fragen für den nächsten Port

| Nr. | Frage | Empfehlung |
| --- | --- | --- |
| 1 | Welcher Effekt wird das erste Testobjekt unter `effects/`? | `drive` oder `stereo-delay`: wenig Zustand, klar messbar. Reverb und Tape hängen stärker an DaisySP. |
| 2 | Wird DaisySP auf dem Desktop mitgenutzt? | Ja, als Submodul, sobald ein Port mehr als eine Funktion davon braucht. DaisySP ist reines C++ unter MIT. |
| 3 | Kopie oder gemeinsamer DSP-Kern mit Daisy-Seed? | Beim zweiten Port neu bewerten. Toy Piano kam mit drei unveränderten Dateien aus; das spricht bisher für die Kopie. |
| 4 | Was wandert nach `common/`? | Kandidaten nach diesem Port: die Größenabstimmung des Fensters mit dem Host und die Parameterklassen für Zeiten und dB. Erst verschieben, wenn ein zweites Plugin sie braucht. |
| 5 | Hersteller-URL und E-Mail im Plugin | Beide Felder sind leer. Eintragen, sobald feststeht, was dort stehen soll. |
| 6 | Sollen Ergebnisse zurück in die Firmware fließen? | Der Nyquist-Schutz wäre ein Kandidat, falls die Firmware je über ±2 Halbtöne hinaus moduliert. |

## Was der erste Port über das Portieren gezeigt hat

* **Der Klangkern war das kleinste Problem.** Sauber von der Hardware getrennter
  Firmware-Code läuft ohne Änderung. Die Arbeit steckt in der Hülle: Parameter,
  Zustand, Fenster, MIDI-Zuordnung.
* **Feste Blockgröße und Kontrollrate der Firmware entfallen.** Der Host liefert
  beliebige Blockgrößen, und Noten tragen einen Sample-Offset. Wer den Kern
  sampleweise aufruft, bekommt bei jeder Blockgröße dasselbe Ergebnis.
* **Grenzen, die die Hardware garantiert, gelten im Plugin nicht.** Die Firmware
  läuft immer mit 48 kHz und moduliert höchstens ±2 Halbtöne. Im Plugin führen
  niedrige Abtastraten und +24 Halbtöne in einen Bereich, in dem der Kern über
  seine Sinustabelle hinausliest. Bei jedem Port ist zu prüfen, welche
  Annahmen der Firmware-Code über Abtastrate und Wertebereiche macht.
* **Referenz-Hashes der Firmware sind an Compiler und C-Bibliothek gebunden.**
  Als Messlatte taugt der Vergleich mit demselben Kern, auf demselben Rechner
  gerechnet.
* **MIDI-Controller sind in VST3 Parameter.** Pitch Bend und CCs brauchen je
  einen Parameter und die Zuordnung im Controller; was ankommt, hängt vom Host ab.
* **Die Fenstergröße muss mit dem Host ausgehandelt werden.** Bei skalierten
  Anzeigen fragt mancher Host die Größe ab, bevor er den Skalierungsfaktor
  mitteilt. Das Fenster war im ersten Versuch bei 120 % abgeschnitten.
