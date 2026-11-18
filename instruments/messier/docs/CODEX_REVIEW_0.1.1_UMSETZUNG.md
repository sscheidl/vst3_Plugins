# Umsetzung von Codex' Nachprüfung zu Messier 0.1.1

Nachprüfung: [CODEX_REVIEW_0.1.1.md](CODEX_REVIEW_0.1.1.md), geprüfter Stand: die
uncommittete Arbeitskopie von 0.1.1 auf `26b8509`, Urteil KEINE FREIGABE. Codex meldet,
dass seine Läufe mehrmals abbrachen und die Nachprüfung unvollständig sein kann; die fünf
Befunde sind trotzdem vollständig beschrieben und alle am Code nachvollziehbar. Umgesetzt
von Claude am 2026-11-18 als **Messier 0.1.2**. Der Klang ist unverändert, der bitgenaue
Vergleich mit dem Firmware-Kern besteht wie vorher.

| Befund | Schwere | Stand | Wo | Test |
| --- | --- | --- | --- | --- |
| MS-R06 Speicher-Ausnahme verlässt den Controller | P1 | behoben | `source/controller.*` | `test_plugin.cpp` `TestNoExceptionLeavesHostCalls` |
| MS-R07 Projektdaten in zwei Schritten | P2 | behoben | `source/processor.*` (`TakeHandovers`, `TakeWindowFile`), `source/file_loader.*` (`LoadProject`), `engine/looper_engine.h` (`FileData::sequence`) | `TestProjectHandoverGap`, `TestProjectBackpressure` |
| MS-R08 Reset-Drucke gehen verloren | P2 | behoben | `source/editor.*`, `source/controller.*`, `source/processor.cpp` | `TestResetButtonThroughHost` |
| MS-R09 Dirty-Meldung hängt am Fenster | P2 | behoben | `source/parameters.h` (Parameter 199), `source/processor.cpp` (`ReportSampleChanges`), `source/controller.cpp` | `TestFileChangeMarksProject` |
| MS-R10 Reserve nicht synchronisiert | P2 | behoben | `source/file_loader.*` | `test_loader.cpp` `TestOutOfMemory` (Reserve beobachtbar) |

Stand der Altbefunde danach: MS-R01 behoben (die offene Stelle war MS-R06), MS-R02 behoben
(die offene Stelle war MS-R07), MS-R03 behoben (MS-R08), MS-R04 behoben (MS-R09).

## MS-R06: Controller

Nachvollzogen: In `setComponentState` war nur das Lesen geschützt; danach konnten die
Anzeige der Werte (`Editor::ParameterChanged`), die Anzeige des Dateistatus und die
Statusanfrage werfen.

Behoben: Jeder Host-Aufruf des Controllers lässt keine Ausnahme hinaus. `initialize`,
`createView`, `editorAttached`, `setState` (Fensterzustand), `setParamNormalized`,
`setComponentState`, `notify` und die Nachrichten (`SendKey`, `RequestFileStatus`,
`PressReset`, `LoadFile`) sind abgesichert. Beim Laden eines Projekts wird unterschieden:
Kann der Zustand nicht gelesen werden, antwortet der Controller `kOutOfMemory` oder
`kResultFalse`; sind die Werte gesetzt und scheitert nur die Anzeige, gilt der Zustand
(`kResultOk`), und das Fenster zeigt ihn beim nächsten Mal.

Test `TestNoExceptionLeavesHostCalls`: Für sechs Host-Aufrufe (Controller
`setComponentState` mit einem angemeldeten, ungeöffneten Editor-Objekt, `setParamNormalized`
und `notify`; Processor `setState`, `getState` und `notify` mit einer Ladeanforderung)
scheitert der Reihe nach jede Allokation des Aufrufs: die erste, die zweite und so fort, bis
der Aufruf ohne Fehlschlag durchläuft. In keinem Fall verlässt eine Ausnahme den Aufruf.
Gemessen: `setComponentState` 18 Allokationen, `notify` (Status) 5, Processor `setState`
11, `getState` 1, `notify` (Laden) 23. `setParamNormalized` allozierte in diesem Aufbau
nichts (das Editor-Objekt hat ohne geöffnetes Fenster keine Regler, deren Text es baut);
dieser Fall belegt also wenig, die Absicherung dort steht im Code
(`Controller::ShowParameter`). Nur im Release-Bau, siehe unten.

## MS-R07: Projektübergabe

Nachvollzogen: `LoadNow` legte die Projektdatei in denselben Austausch wie Fenster-Dateien,
bevor `state_.Load` die Werte veröffentlichte. Ein Audioblock dazwischen tauschte sie mit
den alten Werten ein. Mein Rückstau-Test traf die Lücke nie, weil er erst nach `setState`
rendert; Codex' Nachstellung mit angehaltenem Ladethread zeigte sie.

Behoben:

* `FileLoader::LoadProject` legt die Datei nirgends ab, sondern gibt sie zurück, mit der
  Nummer der Projektladung. `setState` legt sie auf einen eigenen Übergabeplatz für
  Projektdateien und veröffentlicht danach die Werte mit derselben Nummer.
* Der Audio-Thread (`TakeHandovers`) übernimmt eine Projektdatei nur zusammen mit den Werten
  derselben Nummer. Eine Datei ohne ihre Werte wartet, Werte ohne ihre Datei warten (auch
  bei vollem Rückstau); eine Datei eines älteren Projekts, dessen Werte nie kamen, wird
  zurückgegeben. Solange etwas wartet, spielt das alte Projekt vollständig weiter, und es
  wird keine Fenster-Datei eingetauscht.
* `setActive(true)` geht denselben Weg. Wartet dort ein Projekt auf seine andere Hälfte,
  startet die Engine mit der Datei und den Werten des zuletzt übernommenen Projekts neu, nicht
  mit den neuen Werten.
* Fenster-Dateien tragen eine Reihenfolgenummer des Laders (`FileData::sequence`); eine
  Fenster-Datei, die älter ist als die spielende Datei, wird nicht mehr eingetauscht. Die in
  0.1.1 genannte Grenze (Fenster-Datei gegen gleichzeitige Projektladung) ist damit
  aufgelöst: Es gewinnt, was zuletzt fertig wurde.
* Ein dauerhaftes Warten gibt es nicht: Jede veröffentlichte Projektnummer hat vorher ihre
  Datei bekommen. Kann nicht einmal ein leeres Dateiobjekt angelegt werden, veröffentlicht
  `setState` gar nichts und antwortet `kOutOfMemory`; das alte Projekt bleibt.

Test `TestProjectHandoverGap`: Ein Haken im Processor (nur für Tests) rendert zwanzig
Audioblöcke genau zwischen Übergabe der Datei und der Werte, einmal ohne und einmal mit
`setActive(false)` und `setActive(true)` an dieser Stelle. In jedem Block spielen Datei und
Freeze des alten Projekts (101 Frames, Freeze aus); im ersten Block danach das neue (202
Frames, Freeze an). `TestProjectBackpressure` (400 schnelle Projektladungen) besteht weiter.

## MS-R08: Reset-Taster

Nachvollzogen: Das einzelne Bit für einen weiteren Druck, das Verwerfen beim Schließen und
das sofortige Loslassen beim Schließen ließen Drucke verloren gehen.

Behoben: Der Taster im Fenster schickt jeden Druck als Nachricht über den Controller an den
Processor; er macht keinen Edit von Parameter 121 mehr. Der Processor zählt die Drucke und
setzt im nächsten Audioblock zurück. Das ist unabhängig vom Fenster und vom Timer, und kein
Host kann Druck und Loslassen zusammenlegen. Parameter 121 bleibt für Automation mit der
Flanke von 0 auf 1; da das Fenster ihn nicht mehr bewegt, gibt es keine doppelte Auslösung
über die Rückmeldung des Hosts.

Festgelegt und dokumentiert: Drucke, die denselben Audioblock erreichen, wirken einmal (es ist
derselbe Neustart bei START). Drucke, während kein Audio läuft, verfallen nach 0,5 s wie die
Tasten der Bildschirm-Klaviatur. Drucke im Fenster nimmt der Host nicht als Automation auf.

Test `TestResetButtonThroughHost`: ein Druck und sofort danach das Schließen des Fensters,
vor dem nächsten Block: ein Reset; drei Drucke mit Blöcken dazwischen: drei; zwei vor einem
Block: einer; ein Druck ohne Audio, länger als 0,5 s: keiner; kein Edit von Parameter 121 aus
dem Fenster; Automation von Parameter 121: ein Reset.

## MS-R09: Projektänderung ohne Fenster

Nachvollzogen: Der Zähler erreichte den Controller nur über Statusantworten, und die fragte
nur der Timer eines offenen Fensters ab; mehrere Erfolge wurden zu einer Meldung.

Behoben: Der Processor schreibt den Zähler der Fenster-Dateien in den versteckten,
schreibgeschützten Ausgabeparameter 199 („Sample Changes“), im Audioblock nach dem Erfolg.
Der Host reicht ihn auf seinem Thread an den Controller; der fragt dann den Status ab und ruft
für jede seit dem letzten Status neue Datei einmal `setDirty(true)`. Projektladen und
wiederholte Statusantworten zählen nicht. Parameter 199 wird nicht gespeichert und nicht
automatisiert; Eingaben des Hosts darauf ignoriert der Processor.

Test `TestFileChangeMarksProject` ohne jemals geöffnetes Fenster, mit einem Host-Double, das
die Ausgabeparameter an den Controller reicht: eine Datei, eine Meldung; fünf Statusabfragen,
keine; eine kaputte Datei, keine; zwei Dateien, bevor der Host etwas weiterreicht, zwei
Meldungen; ein geladenes Projekt, keine.

Grenze: Ein Host, der Ausgabeparameter nicht an den Controller weiterreicht, meldet die
Änderung erst, wenn ein Fenster offen ist. Das prüft die DAW-Liste.

## MS-R10: Reserve

Nachvollzogen: `LoadNow` rief `TakeEmpty` bei leerem Pfad ohne die Sperre des Laders.

Behoben: Jede Entnahme und jedes Auffüllen der Reserve geschieht unter der Sperre
(`TakeEmptyLocked`, `RefillSpareLocked`). Nach einer fehlgeschlagenen Allokation nimmt der
Ersatzweg die Reserve zuerst, ohne einen weiteren Allokationsversuch; das Auffüllen danach
versucht eine Allokation und fängt ihr Scheitern ab. Die Formulierung „frei von
Allokationen“ in der Umsetzung zu 0.1.0 war zu stark und gilt nur für das Setzen der kurzen
Texte.

## Teilauftrag: Debug-Abdeckung von MS-R01

Codex' Frage: welche Zusicherung für die Reserve im Debug-Bau dokumentiert werden kann.

Beurteilt aus Code und Läufen:

* **Was die Debug-STL tut.** Mit Iterator-Debugging legt MSVCs Standardbibliothek für jeden
  `std::string` und jeden `std::filesystem::path` einen Proxy von 16 Bytes an, auch in
  `noexcept`-Konstruktoren. Scheitert diese Allokation, endet der Prozess über
  `std::terminate`, bevor irgendein `catch` des Plugins erreicht wird. Codex hat das an der
  installierten MSVC 14.51 bestätigt.
* **Was daraus folgt.** Im Debug-Bau lässt sich nicht zusichern, dass der Prozess eine
  gescheiterte Allokation von 16 Bytes übersteht; das liegt außerhalb des Plugin-Codes. Die
  Zusicherung „keine Ausnahme verlässt einen Host-Aufruf, der Prozess lebt weiter“ gilt für
  den Release-Bau, der ausgeliefert wird.
* **Was im Debug-Bau geprüft ist.** Alle Fälle des Ladertests mit einer Schwelle ab 40 Bytes
  laufen auch im Debug-Bau: Umrechnungspuffer, PCM-Array, fast alle Allokationen im
  Ladethread, Projektladen. Seit 0.1.2 hat `FileData` 40 Bytes; bei der Schwelle 40 kann
  deshalb nicht einmal das leere Dateiobjekt angelegt werden, und der Test prüft in beiden
  Bauarten, dass die Reserve verwendet wird, danach fehlt und nach Erholung des Speichers
  wieder da ist (`SpareReadyForTest`). **Die Reserve ist damit im Debug- wie im Release-Bau
  geprüft.**
* **Was nur im Release-Bau geprüft ist.** Die Schwelle 16 (auch die Reserve, wenn selbst die
  Proxies fehlen) und `TestNoExceptionLeavesHostCalls`, das jede Allokation der Reihe nach
  scheitern lässt und dabei auch die Proxies träfe.
* **Abgleich der Dokumentation.** README, Build-Bericht und die Umsetzungen nennen jetzt die
  geprüften Bauarten so: Release (VS 2026 und VS 2022) mit allen Fällen, Debug mit den Fällen
  ab 40 Bytes, ASan (Release) mit allen Fällen.

## Bauen und Prüfen

Auf dem Endstand von 0.1.2, alle vier Plugins, Windows 11, i7-8700K:

| Bau | Ergebnis |
| --- | --- |
| VS 2026 (MSVC 19.51), Release | 0 Fehler, keine Warnung im eigenen Code; Validator 47/0 für jedes Bundle; `ctest` 16 von 16 |
| VS 2026, Debug | dasselbe; `ctest` 16 von 16 (mit `--timeout 1500`); die Speicherfehler-Fälle ab 40 Bytes |
| VS 2022 (MSVC 19.44), Release, frischer Build-Ordner | dasselbe; `ctest` 16 von 16 |
| AddressSanitizer (VS 2026 Release), die fünf Messier-Testprogramme | alle bestanden, keine Meldung |

Der bitgenaue Vergleich mit dem Firmware-Kern ist unverändert (Engine-Schicht fünf Szenarien
mit je 0 von 1.152.000 verschiedenen Samples, Plugin 0 von 960.000). Der Rückstau-Test
ließ in 336 von 400 Blöcken ein Projekt warten, ohne Mischung. Bundle und Logs liegen in
`out/messier-0.1.2/`. Nicht wiederholt: die Lastmessung (der DSP-Weg ist unverändert).
