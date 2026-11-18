# Umsetzung von Codex' Nachprüfung zu Messier 0.1.3

Nachprüfung: [CODEX_REVIEW_0.1.3.md](CODEX_REVIEW_0.1.3.md), geprüfter Stand `160a478`,
Urteil KEINE FREIGABE; MS-R11 und MS-R15 bestätigt behoben. Umgesetzt von Claude am
2026-11-18 als **Messier 0.1.5**, auf dem Datenweg des Fensters (0.1.4, bis dahin
uncommittet). Alle vier neuen Befunde wurden am Code nachvollzogen und stimmen. Der Klang
ist unverändert, der bitgenaue Vergleich mit dem Firmware-Kern besteht.

| Befund | Schwere | Stand | Wo | Test |
| --- | --- | --- | --- | --- |
| MS-R16 Speicherfehler beim WAV-Dekodieren gibt ein stilles Projekt | P2 | behoben | `source/wav_reader.*` (`WavError::kOutOfMemory`), `source/file_loader.cpp` (`LoadProject`) | `test_loader` `TestOutOfMemory`, neuer Abschnitt; `test_plugin` `TestProjectWithoutMemory` |
| MS-R17 Ladungen eines neuen Processors zählen zunächst nicht | P2 | behoben | `source/processor.*` (Kennung im Status), `source/controller.*` (`connect`, `CountFromParameter`, `CountFromStatus`), `source/parameters.h` (`kAttrProcessor`) | `test_plugin` `TestNewProcessorSameController` |
| MS-R18 Reset geht über `process(0)` und Aktivierung verloren | P3 | behoben | `source/processor.cpp` (`process`, `Restart`), `adapter/stream_adapter.h` (`ControlWaiting`) | `test_plugin` `TestResetButtonThroughHost`, zwei neue Abschnitte |
| MS-R19 Offene Ausnahmegrenzen in Factory und Lebenszyklus | P1 | behoben | `source/processor.*` (`createInstance`, `initialize`, `setupProcessing`, `setActive`), `source/controller.h` (`createInstance`), `adapter/stream_adapter.cpp` (`Prepare`) | `test_plugin` `TestNoExceptionLeavesHostCalls`, drei neue Aufrufe; `test_adapter` `TestPrepareWithoutMemory` |

Stand der Altbefunde danach: MS-R12 behoben (die offene Stelle war MS-R17), MS-R13 behoben
(MS-R18), MS-R14 behoben (MS-R16).

## MS-R16: Speichermangel beim Dekodieren

Der WAV-Leser hat einen eigenen Fehlercode `kOutOfMemory` („out of memory“). Er steht für
jede fehlgeschlagene Allokation beim Lesen: den Konvertierungspuffer, das PCM-Feld (vorher
„too large: not enough memory“) und den Pfad (vorher fing ein `catch(...)` dort auch
`std::bad_alloc` und meldete „file not found“). `kTooLarge` bleibt allein für Dateien über
der Grenze. `LoadProject` lehnt bei `kOutOfMemory` das Projekt als Ganzes ab, wie jeden
anderen Speichermangel seit MS-R14: kein Dateiobjekt, Status und gespeicherter Pfad
bleiben die des noch spielenden Projekts, `setState` antwortet `kOutOfMemory`. Still mit
gespeichertem Pfad wird ein Projekt nur noch bei fehlender oder unbrauchbarer Datei. Eine
Datei aus dem Fenster scheitert wie bisher mit Meldung, nur der Text ist jetzt „out of
memory (while decoding)“ bzw. „(for the audio data)“. Das README beschreibt die Regel jetzt
ausdrücklich.

Nicht unterscheidbar bleibt ein `std::ifstream`, das sich mangels Speicher nicht öffnen
lässt: Die Standardbibliothek meldet das nur als gescheitertes Öffnen („cannot open the
file“, `kReadError`), das Projekt wird dann still mit seinem Pfad.

Tests: Im Ladertest scheitert nacheinander nur der Konvertierungspuffer (1 MiB; `std::vector`
legt bei großen Blöcken einige Bytes Ausrichtung dazu, daher ein schmales Größenband) und nur
das PCM-Feld (19.200 Bytes) einer gültigen Projektdatei: `ReadWavFile` meldet
`kOutOfMemory`, `LoadProject` gibt kein Dateiobjekt zurück, Pfad, Klangstatus und Text
bleiben. Im Plugin-Test über den Processor: `setState` antwortet `kOutOfMemory`, das alte
Projekt spielt weiter, `getState` speichert seinen Pfad; mit Speicher lädt dasselbe Projekt.

## MS-R17: Neuer Processor am selben Controller

Jeder Processor trägt eine Kennung (64 Bit aus Zeit, Adresse und einem Zähler, nie 0) und
schickt sie mit jedem Status (`kAttrProcessor`), dazu den vollen 32-Bit-Zähler statt nur
16 Bit davon. Der Controller zählt getrennt:

* **Status:** Nennt er einen anderen Processor als den gezählten, beginnt die Zählung bei
  dessen Null. Eine Datei doppelt zu melden, markiert nur ein Projekt, das sich ohnehin
  geändert hat; eine zu verlieren, könnte es kosten. Ein Rückschritt desselben Processors
  ist eine alte Antwort und zählt nichts. Keine 32.767-Grenze mehr auf diesem Weg.
* **Parameter 199:** wie bisher modulo 65.536, ein Schritt vorwärts unter der Hälfte zählt.
  Ein Rückschritt zählt nichts, außer nach einem `connect`, bevor der neue Processor seinen
  ersten Status geschickt hat: Dann ist er der Zähler eines neu gestarteten Processors und
  zählt ab dessen Null; der folgende Status findet die Dateien schon gezählt.
* **`connect`:** setzt den eigenen Wert von Parameter 199 auf 0 (damit der erste Wert des
  neuen Processors auch dann eine Änderung ist, wenn er dem letzten des alten gleicht),
  vergisst Anzeige und Wellenform des alten Processors (seine Dateinummern beginnen beim
  neuen wieder bei 1, sonst könnte das Fenster eine fremde Wellenform zeigen) und fragt
  nach dem Status.

Die 16-Bit-Grenze von Parameter 199 bleibt: mehr als 32.766 Dateien zwischen zwei Werten,
die der Host weiterreicht, kommen beim Tempo des Ladens nicht vor; der Status trägt den
vollen Zähler.

Test (`TestNewProcessorSameController`): ein Controller, nacheinander drei Processoren.
Beim ersten eine Datei (und eine Wellenform im Controller), beim zweiten zwei Dateien mit
Statusantworten; sein erster Wert von Parameter 199 gleicht dem letzten des ersten. Beim
dritten fehlt der Rückweg ganz, Parameter 199 allein. Jede Datei gibt genau ein
`setDirty`, Statusabfragen keines; nach jedem `connect` ist die alte Wellenform weg. Der
zweite Processor ist der Fall aus Codex' Zusatztest; gegen den Code von 0.1.3 habe ich den
neuen Test nicht laufen lassen.

## MS-R18: Reset über `process(0)` und Aktivierung

Zwei Stellen. `process` nimmt die Drucke des Fensters nur noch aus dem Zähler, wenn der
Aufruf Samples hat; ein Aufruf ohne Samples lässt sie liegen, eine Aktivierung danach
behält sie (wie seit MS-R13). Und `Restart` (Aktivierung, Projektwechsel) fragt den Adapter,
ob ein Reset-Ereignis auf einen noch nicht gerechneten Abschnitt wartet
(`StreamAdapter::ControlWaiting`), und legt es nach dem Neustart auf dessen ersten
Abschnitt. Das betrifft auch Drucke aus der Automation von Parameter 121, die ein Block
zwischen zwei Abschnittsgrenzen schon eingeplant hat.

Test: ein Druck, `process(0)`, Aktivierung, ein Block: ein Reset. Ein automatisierter Druck
in einem 1-Sample-Block mitten in einem Abschnitt (er wartet im Adapter), Aktivierung, ein
Block: ein Reset. Der erste Fall ist der aus Codex' Zusatztest; gegen den Code von 0.1.3
habe ich die neuen Abschnitte nicht laufen lassen.

## MS-R19: Factory und Lebenszyklus

* `Processor::createInstance` und `Controller::createInstance` sind `noexcept` und geben
  bei einer Ausnahme im Konstruktor (Lader-Reserve, Start des Worker-Threads) null zurück;
  `CPluginFactory::createInstance` des SDK antwortet dann `kNoInterface`
  (`public.sdk/source/main/pluginfactory.cpp`, Zeilen 229–247).
* `initialize` fängt das Anlegen der Busse ab, gibt sie bei einem Fehler wieder frei und
  antwortet `kOutOfMemory`, ebenso wenn der Adapter nicht vorbereitet werden kann.
* `StreamAdapter::Prepare` ist `noexcept` und baut Kern, Puffer und Warteschlangen einer
  neuen Rate erst vollständig nebenher auf; nur wenn alles da ist, ersetzt es das Alte
  (Adapter-Version 0.1.1). Bei Mangel bleibt der Adapter, wie er war: Rate, Latenz, Puffer
  und wartende Ereignisse.
* `setupProcessing` antwortet bei Mangel `kOutOfMemory` und stellt auch `processSetup`
  wieder her; der Ratenwechsel-Zweig in `setActive(true)` antwortet `kOutOfMemory`, statt
  mit der alten Rate weiterzulaufen.

Tests: Im Countdown-Test drei neue Aufrufe, jede Allokation der Reihe nach scheiternd:
Erzeugen über die Factory-Funktion, `initialize`, `setActive` mit Ratenwechsel (44,1 kHz
gegen die 48 kHz aus `initialize`), `setupProcessing` auf 96 kHz und `setActive` (103
Allokationen); Controller erzeugen und `initialize` (44); `connect` des Controllers (24).
Nach jedem Fehlschlag hat der Adapter die vorige Rate. Im Adaptertest scheitert
`Prepare(44100)` an jeder seiner 30 Allokationen: `false`, keine Ausnahme, Rate, Latenz,
belegter Speicher und das wartende Ereignis unverändert; danach gelingt es.

Weiterhin ohne eigenen Fehlerpfad: Steinbergs Hilfsfunktion `GetPluginFactory` legt die
Factory beim ersten Aufruf mit `new` an (SDK-Code, vor jedem Messier-Code); scheitert das,
bleibt es eine Sache des SDK.

## Bauen und Prüfen

Siehe [CODEX_PRUEFAUFTRAG_0.1.5.md](CODEX_PRUEFAUFTRAG_0.1.5.md), Abschnitt „Mein Stand“.
