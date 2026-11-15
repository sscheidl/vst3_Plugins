# Codex Review: Torus Classic 0.2.1

VERDICT: FREIGABE FÜR DAW-TEST

Geprüft: `697b33d8b137d90eddc97095a4a0dfde1650fcdd`, 2026-11-14,
Windows x64, Visual Studio 2026 Build Tools, MSVC 19.51.36256.0,
Release, `/fp:precise`.

Die fünf Befunde TC-R05 bis TC-R09 sind behoben. Kein neuer Befund der
Prioritäten P0 bis P2. TC-R10 ist eine nachgewiesene Lücke im vorhandenen
Adapter-Stresstest und eine leicht vermeidbare Falle im internen Event-Helfer;
der aktuelle Parameterweg des Plugins ist davon nicht betroffen. Dieser
Restpunkt verhindert die GUI-, DAW- und Hörabnahme nicht.

Grundlage waren der Prüfauftrag 0.2.1, das Review 0.2.0, dessen
Umsetzungsbericht und der Diff `78e5b9a..697b33d`. Der geprüfte Commit
entsprach beim Abgleich dem Head von PR #1 und dem lokalen Torus-Branch.
Der aktive Arbeitszweig war dagegen `elements-0.1.0` auf `7901899`.
Deshalb wurde der Torus-Commit als eigenes Archiv unter
`.work/torus-classic-review/source/` geprüft und gebaut; kein Branchwechsel
und keine Verwendung des älteren Torus-Codes aus dem aktiven Arbeitsbaum.
Das vorhandene SDK wurde nur als Build-Abhängigkeit verwendet.

## Nachprüfung TC-R05 bis TC-R09

Die Dateiangaben in diesem Bericht beziehen sich auf `instruments/torus-classic/`
im oben genannten Prüfcommit.

| ID | Ergebnis | Beleg | Rest |
| --- | --- | --- | --- |
| TC-R05 | **behoben** | `adapter/output_stage.h:78`, `:88`, `adapter/output_stage.cpp:14`: Steuerwert und Glättungsfaktor sind Double; der Rest unter 1e-6 bzw. ein nicht mehr fortschreitender Schritt setzt den tatsächlichen Zielwert. Vorhandene sechs Übergänge bei sieben Raten von 1 kHz bis 4 MHz bestanden. Eigene Wiederholung 0.2 → 1: nach 100 ms überall Wert 1 und Mix exakt 0. | Der Float-Testgetter allein könnte einen noch nicht exakt angekommenen Double verdecken. Der Code-Snap und die zusätzliche Prüfung auf Mix 0 sichern den relevanten Off-Fall ab. Keine erschöpfende Enumeration aller Float-Ziele. |
| TC-R06 | **behoben** | `adapter/output_stage.h:97`: beide Integratoren werden unabhängig vom Mix weitergeführt; bei Mix 0 wird der begrenzte Eingang unmittelbar ausgegeben. Die acht vorhandenen Sinus-/Gleichanteil-Szenarien nach zehn Sekunden Bypass-Stille bestanden. Zusätzliche Grenzratenprobe bei 1 kHz und 4 MHz bestanden; Reset und Ratenwechsel in den bestehenden Tests unauffällig. | Exakter Durchgang gilt innerhalb der Eingangsgrenze und bei Limiter-Gain 1. Eine bereits laufende Limiter-Abregelung wirkt weiterhin, wie vorgesehen. |
| TC-R07 | **behoben** | `source/parameters.h:117`, `source/engine_bridge.h:50`, `:74`, `source/processor.cpp:109`, `:164`: Rundung vor der Float-Konvertierung; Automation und geladener Zustand übernehmen denselben Halbton wie der Controller. `TestTuneRounding`: 962 Grenzwerte bestanden. Eigene Engine-Probe bestätigt den Halbton auch nach Zusammenlegen im selben Quantum und Ersetzen bei voller Regler-Queue. | TC-R10 betrifft den allgemeinen Helfer und dessen bestehende Testaufrufe. Der Produktweg benutzt korrekt `TuneEvent(double)`; `DirectControl` schließt Tune aus. Der bekannte Host-Refresh beim FREE-Umschalten bleibt offen für den DAW-Test. |
| TC-R08 | **behoben** | `source/processor.cpp:201`, `:229`, `:235`, `:387`, `source/processor.h:122`: Releases umgehen die Queue; der Audio-Thread merkt nur tatsächlich angenommene Host-Note-ons und wiederholt abgelehnte Note-offs. Vorhandene Regressionstests, erzwungene Thread-Verschachtelungen sowie zwei zusätzliche Läufe mit jeweils 100.000 Aktionen bestanden. Details unten. | Zustellung setzt spätere Audioblöcke mit einer wieder aufnahmefähigen Hostliste voraus. Ein dauerhaft nicht mehr aufgerufener Prozessor kann kein MIDI-Ereignis zustellen. Genau ein Notify-Produzent bleibt Vertragsannahme. |
| TC-R09 | **behoben** | `adapter/output_stage.h:62`, `:98`: NaN/±Inf werden vor dem Filter zu 0, endliche Extremwerte auf ±1e6 begrenzt. Vorhandene Fehler-/Erholungsfälle bestanden. Eigene Probe mit wechselndem Filterziel und extremen Eingängen: je 100.000 Samples bei 1 kHz, 48 kHz, 192 kHz und 4 MHz; alle Ausgänge endlich und betragsmäßig ≤ 0.5. | Kein mathematischer Beweis für beliebige unendliche Eingangsfolgen; kein Fehler im geprüften Raten-/Parameterbereich gefunden. Der Schutz ergänzt den bereits vorhandenen Engine-Schutz. |

Der Snap aus TC-R05 verursacht gegenüber dem ungesnappten nächsten Wert
höchstens 1e-6 Steuerwertänderung. Im oberen Überblendbereich ist der
zusätzliche Mix-Schritt damit höchstens 5e-5. Das ist für diesen geglätteten
Regler vertretbar; eine Aussage zur Hörbarkeit ersetzt dies nicht. Bei
konstantem Ziel endet damit auch die laufende Koeffizientenberechnung.

Bei der zusätzlichen TC-R06-Probe wurde nach Gleichanteil zehn Sekunden
Bypass-Stille verarbeitet und anschließend wieder geschlossen. Die Spitze
beim Schließen betrug bei 1 kHz 0 und bei 4 MHz ungefähr 9.015e-37,
jeweils unter der vorhandenen Testgrenze 1e-20. FTZ/DAZ war dabei wie im
Streaming-Adapter aktiviert. Im Bypass waren die Nullen exakt null; nach
Reset war der offene Durchgang für das leise Prüfsignal bitgleich.

## Neue Befunde

| ID | Schwere | Bereich | Befund | Beleg (Datei:Zeile) | Auslösung und Auswirkung | bewiesen/vermutet | minimale Korrektur |
| --- | --- | --- | --- | --- | --- | --- | --- |
| TC-R10 | **P3** | Adapter-Stresstest / interner Event-Helfer | Der Kommentar „Any control but Tune“ verhindert keine weiterhin vorhandenen falschen Tune-Ereignisse im Test mit echter Engine. `ControlEvent` lässt `data` auf 0; die Bridge interpretiert dies jetzt als −24 Halbtöne. | `source/engine_bridge.h:34`, `:95`; `adapter/stream_adapter.h:48`; `tests/test_adapter.cpp:932`, `:949` | `ControlEvent(Control::Tune, 0.5f)` führt bei FREE aus nach dem Einschwingen zu normiert 0 / −24 st statt 0.5 / 0 st. Der zufällige Score des Adaptertests benutzt diesen Weg; Tune variiert dort nicht mehr wie beabsichtigt. Der Vergleich verschiedener Blockgrößen bleibt trotzdem grün, weil alle Renderings denselben falschen Event-Inhalt erhalten. Der aktuelle Plugin-Prozessor ist nicht betroffen. | **bewiesen**, Code und eigene Probe gegen die echte Bridge/Engine | Im Score für Tune `TuneEvent` verwenden und den tatsächlich ankommenden Halbton prüfen. Zusätzlich `ControlEvent` bei Tune an `TuneEvent(static_cast<double>(value))` weiterleiten, damit bestehende Float-Aufrufer gültige Events erhalten. Die Double-Ereignisse des Prozessors beibehalten. |

Die übrigen generischen Tune-Aufrufe in Queue-Tests mit `CounterEngine`
verwenden den Wert nur als undurchsichtige Testmarkierung. Sie beweisen weder
einen falschen Plugin-Ton noch eine korrekte Übernahme des neuen Halbtonfelds.
Die eigene Probe mit gültigen `TuneEvent`s schließt letztere Lücke für diese
Nachprüfung: Sowohl `Last().event = event` als auch `Last() = {quantum, event}`
erhalten das ganze Ereignis einschließlich `data`
(`adapter/stream_adapter.cpp:139`, `:146`).

## Ohne Befund geprüft

### TC-R08: Freigabe der Bildschirmtasten

- **Besitz und Speicherordnung:** `key_struck_` gehört allein dem
  Notify-Produzenten. Die Queue veröffentlicht Nummer und Anschlag mit
  Release/Acquire. Der Audio-Thread liest diesen Produzentenzähler nicht;
  Releases veröffentlicht der Produzent separat über `key_released_`
  mit Release, gelesen mit Acquire. Held-Status, gesendete Anschlagnummer
  und ausstehende Note-offs gehören dem Audio-Thread. Das Aktualisieren von
  `key_struck_` nach erfolgreichem Queue-Push erzeugt damit keinen gemeinsamen
  ungeschützten Zugriff.
- **Thread-Vertrag:** Ein Notify-Produzent ist für diesen Kommunikationsweg
  haltbar. Das vorhandene SDK kennzeichnet `IConnectionPoint::notify` als
  UI-Thread-Aufruf (`external/vst3sdk/pluginterfaces/vst/ivstmessage.h:86`).
  Mehrere gleichzeitig aufrufende Notify-Threads unterstützt der Code nicht;
  dies war bereits Voraussetzung der SPSC-Queue. Lebenszyklusaufrufe werden
  entsprechend dem bisherigen Vertrag außerhalb paralleler Audiobearbeitung
  vorausgesetzt.
- **Kurze Tipper und erneute Anschläge:** Ein schon freigegebener, noch
  wartender Anschlag bekommt nach erfolgreichem Host-Note-on sein Note-off.
  Ein alter Host-Anschlag wird vor einem neuen derselben Taste beendet. Passt
  dessen Note-off nicht in die Liste, bleibt es vorgemerkt und es folgt kein
  zweites Host-Note-on. Ein verworfenes neues Note-on erzeugt keinen eigenen
  späteren Note-off.
- **Erzwungene Verschachtelungen:** Ein eigener Host-Hook hielt die Rückkehr
  aus dem ersten erfolgreichen `addEvent(NoteOn)` gezielt an. Der separate,
  einzige Notify-Produzent führte währenddessen Release, weiteren Anschlag
  plus Release oder Release plus weiteren Anschlag aus. Danach wurde jeweils
  vollständig freigegeben. Keine doppelten Note-ons, keine ungepaarten
  Note-offs, am Ende keine gehaltene Host-Note. Die Synchronisationswartezeit
  war ausschließlich Teil des Testhosts.
- **Rückstau und Lebenszyklus:** 100.000 gemischte Aktionen mit vier Tasten,
  State-Laden, De-/Reaktivierung, Null-Sample-Flushes und fehlenden bzw. auf
  0/1/2/3/64 Ereignisse begrenzten Hostlisten: 13.804 angenommene und 10.663
  abgelehnte Ereignisse, keine falsche Paarung, nach Freigeben und Leeren
  keine gehaltene Host-Note. Ein separater Lauf mit 100.000 Notify-Aktionen
  auf acht Tasten parallel zu Audioblöcken bestand ebenfalls, einschließlich
  wiederholt abgelehnter Ereignisse. Zählwerte dieses zweiten Laufs hängen
  vom Thread-Scheduling ab.
- **Verfall und State-Laden:** Veraltete oder vor einem geladenen Zustand
  liegende wartende Anschläge werden weiter verworfen. Bereits beim Host
  gehaltene Noten bleiben über ihren tatsächlich gesendeten Anschlag
  identifizierbar und können durch Release bzw. Reaktivierung beendet werden.
  Die vorhandenen Queue-, Pause-/Flush- und Aktivierungsfälle bestanden.
- **Begrenzte Arbeit:** Je Audioblock höchstens 128 Prüfungen bereits
  gehaltener Tasten und 64 Queue-Pops, auch bei weiter schreibendem Produzenten.
  Höchstens 128 + 3 × 64 = 320 Versuche von `addEvent`; kein Spin auf eine
  volle Hostliste. Ein fehlgeschlagenes Note-off bleibt für spätere Blöcke
  bestehen. Die Aufwandsschranke setzt voraus, dass der Host selbst aus
  `addEvent` zurückkehrt.
- **Zählerüberlauf:** Der vorzeichenbehaftete Modulovergleich ist auf dem
  geprüften C++20/MSVC-Ziel definiert und über den Überlauf hinweg passend,
  solange die verglichenen Sequenzstände weniger als 2^31 auseinanderliegen.
  Beliebig viele Milliarden Anschläge derselben Taste ohne entsprechenden
  Release sind dadurch nicht abgedeckt; kein praktisch reproduzierter Fehler.

Dies sind künstlich erzeugte Kapazitäts- und Parallelitätsfälle. Daraus folgt
keine beobachtete CPU-Überlast oder hängende Note in Studio One.

### Doku und übrige Korrekturen

README („Sound“, „Level“, „Screen keyboard“), CHANGELOG 0.2.1 und
`CODEX_REVIEW_0.2.0_UMSETZUNG.md` stimmen im geprüften Umfang mit dem Code
überein. Damping 0 statt 1 für endloses Halten ist korrigiert. „Nicht endlich
zählt als Stille“ beschreibt die Behandlung des Eingangssamples: Ein bereits
angeregter Filter darf auf diesen Nulleingang noch seinen eigenen endlichen
Ausklang liefern. Es ist keine Zusage, jeden betroffenen Ausgang sofort auf
null zu setzen. Die erneute MIDI-Zustellung benötigt weitere Audioblöcke und
letztlich freien Listenplatz.

### Regressionstests gegen 0.2.0

Die Erkennungswirkung wurde zusätzlich dynamisch geprüft. Dafür wurden die
alten Produktionsdateien aus `78e5b9a` separat gebaut und die neuen
Testabschnitte darauf angewandt. Unveränderte Engine-/Referenz- und
SDK-Bibliotheken wurden wiederverwendet. Nur der alte Testzugang erhielt den
neuen lesenden `FilterMix()`-Getter; `TuneStep()` wurde als Testorakel ergänzt.
Kein alter Verarbeitungsalgorithmus wurde dabei korrigiert.

| Regression aus 0.2.1 | Gegen alten Code | Gegen 0.2.1 |
| --- | --- | --- |
| TC-R05: sechs Filterübergänge bei sieben Raten | erwarteter Fehler bei `FilterValue() == glide[1]` | bestanden |
| TC-R06: zehn Sekunden Bypass-Stille, danach schließen | erwarteter Fehler bei Zielwert-/Restspitzenprüfung | bestanden |
| TC-R07: `TestTuneRounding`, 962 Werte | erwarteter Fehler bei `mismatches == 0` | bestanden |
| TC-R08a: Release bei voller Anschlagqueue | erwarteter Fehler bei `SendNote(..., 64, 0) == kResultOk` | bestanden |
| TC-R08b: Hostliste mit Platz für ein Ereignis | separat nach Auslassen des vorherigen Queue-Szenarios: erwarteter Fehler beim nachzuholenden `F62` | bestanden |
| TC-R09: nicht endliche / extreme Eingangspaare | erwarteter Fehler bei `isfinite(l) && isfinite(r)` | bestanden |

Damit erkennen die neuen Tests die alten Defekte tatsächlich; die Aussage
beruht nicht allein auf dem Lesen ihrer Assertions.

### Tatsächlich ausgeführte positive Läufe

| Lauf | Ergebnis |
| --- | --- |
| Isolierter Release-Build | bestanden; Torus-Build-Validator 47 bestanden, 0 fehlgeschlagen |
| `torus-classic.engine` | bestanden, 4.30 s |
| `torus-classic.adapter` | bestanden, 25.50 s |
| `torus-classic.provenance` | bestanden, 0.60 s |
| Fensterloser Plugin-Teil | 13 vorhandene Testfunktionen bestanden |
| Zusätzliche Filter-, Tune- und MIDI-Proben | bestanden, abgesehen von der gezielt bestätigten Abweichung TC-R10 |

Der eigene Runner band `tests/test_plugin.cpp` mit umbenanntem `main` ein
und führte ausschließlich `TestAgainstFirmware`, `TestBlockSizes`,
`TestNoteHandling`, `TestSampleRates`, `TestInstances`, `TestParameterState`,
`TestKeyboardQueue`, `TestKeyboardMidiOut`, `TestPauseAndFlush`,
`TestActivationAndRateChange`, `TestState`, `TestController` und
`TestTuneRounding` aus. Keine Änderung dieser vorhandenen Testfunktionen im
positiven Lauf. `TestViewLayout`, `TestEditorResize` und
`TestEditorInWindow` wurden nicht aufgerufen.

## Fehlende Tests

1. TC-R10 korrigieren und den korrekten Tune-Halbton im Adapter-Stresstest
   dauerhaft prüfen. Die zusätzliche Prüfung gültiger Tune-Payloads bei
   Queue-Zusammenlegung und -Ersetzung sollte in die Regressionen übernommen
   werden.
2. Die erzwungenen Notify-/Audio-Verschachtelungen und variablen
   Hostlisten-Kapazitäten sind bislang eigene Wegwerfproben. Dauerhafte Tests
   hierfür würden das neue Release-Verfahren besser absichern als nur die
   vorhandenen sequentiellen Beispiele.
3. Der Getter `FilterValue()` liefert Float; ein Double-Getter oder eine
   explizite Ziel-erreicht-Prüfung könnte den exakten Snap auch für innere
   Ziele unabhängig von der Float-Rückkonvertierung testen.
4. Keine dynamische Grenzprüfung unmittelbar am 32-Bit-Zählerüberlauf und
   keine erschöpfende Interleaving- oder Race-Detector-Prüfung. Die
   Speicherordnungen wurden zusätzlich am Code nachvollzogen.
5. Die bereits dokumentierten Lücken zu sämtlichen V1-State-Präfixen und
   einem gezielt synchronisierten Slot-Test bleiben bestehen. Der bekannte
   FREE-Text-Refresh sowie MIDI-Routing und Suspend/Wake gehören zur
   ausstehenden Host-Abnahme.

## Nicht geprüft

- Keine GUI-, Fenster-, DAW- oder Hörtests. Insbesondere kein Aufruf des
  vollständigen `torus-classic.plugin`-CTest, da dieser Fenster öffnet.
  Die 13 einzeln ausgeführten Funktionen sind keine Behauptung eines
  vollständigen Plugin-CTest-Laufs.
- Debug, VS 2022, AddressSanitizer und Performance-Benchmark nicht erneut
  ausgeführt. Die Angaben im Umsetzungsbericht bleiben Fremdergebnisse.
- Keine vollständige Neuauditierung des unveränderten DSP, des Resamplers,
  der Oberfläche oder der Zustandsarchitektur. Vorhandene zugehörige
  fensterlose Regressionen liefen wie oben angegeben mit.
- Polyphonie 4/8/16/32, Plugin-Nap und das Schloss-Symbol sind gemäß Auftrag
  weiterhin Folgeanforderungen und nicht Bestandteil dieser Nachprüfung.
- Kein Produktcode, vorhandener Test oder vorhandenes Dokument geändert;
  kein Commit, Push oder PR-Kommentar. `build/`, `dist/`, `out/`, installierte
  Plugins und fremde Änderungen blieben unberührt. Der isolierte Build und
  alle eigenen Wegwerfproben unter `.work/torus-classic-review/` wurden nach
  Abschluss entfernt. Nur diese Ergebnisdatei bleibt hinzugefügt.
