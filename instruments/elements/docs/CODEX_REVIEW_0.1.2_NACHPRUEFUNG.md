# Nachprüfung: Mendelejew 0.1.2

Datum: 2026-11-15. Prüfer: Codex.
Grundlage: [Prüfauftrag](CODEX_PRUEFAUFTRAG_0.1.0.md),
[Nachprüfung 0.1.1](CODEX_REVIEW_0.1.1_NACHPRUEFUNG.md) und
[Umsetzungsbericht 0.1.2](CODEX_REVIEW_0.1.1_UMSETZUNG.md).

**Ergebnis: EL-R01 und EL-R06 sind im geprüften Stand behoben. Kein neuer
konkreter Befund. Aus Sicht dieser Codeprüfung kein Einwand gegen einen
Merge.** Die bekannten Tabellenkonflikte mit `main` und die Validierung des
anschließend integrierten Stands sind davon getrennte, noch ausstehende
Schritte. Die Freigabe umfasst keine DAW- oder Hörabnahme.

## Geprüfter Stand und Umfang

Branch `elements-0.1.0`, Commit
`0f5c6b35eead07eb72e2e6f5d98d5604ed46d242`:
`Mendelejew 0.1.2: keep room in the keyboard queue for every release`.
Vergleich gegen den zuletzt geprüften Stand `2f9b0ff`.

Schwerpunkt sind die neue Platzreservierung für die Bildschirm-Klaviatur,
die Speicherordnung der Queue, Aktivierung, Zustandsladen, abgelaufene
Anschläge und das Verhalten bei abgewiesenen Anschlägen. Auch die übrigen
Änderungen dieses Commits einschließlich Tests, Versionierung und
Dokumentation wurden durchgesehen.

DSP-Kern, Engine, Adapter, Toy Piano Classic und Torus Classic sind im
Vergleich unverändert. Die Ergebnisse zu EL-R02 bis EL-R05 aus der letzten
Nachprüfung gelten im dort beschriebenen Umfang weiter; ihre regulären
Regressionstests wurden erneut ausgeführt. Die Originalquellen und die
DSP-Herleitung wurden für diesen Queue-Fix nicht nochmals vollständig
unabhängig geprüft.

`main` wurde nicht hereingeholt. Diese Prüfung umfasst daher noch keinen
gemeinsamen Stand mit Kekulé 0.2.2. Die zwei vorgefundenen unversionierten
Torus-Berichte wurden nicht angefasst.

## EL-R01 und EL-R06: neue Lösung am Code

Fundstellen: `source/processor.cpp:389` (`notify`),
`source/processor.cpp:223` (`ReadKeyboard`), `source/processor.h:119`
und `source/spsc_queue.h:32`.

Die ausgelagerten Freigaben und ihre Sequenznummern existieren nicht mehr.
Angenommene Anschläge und Freigaben werden durch dieselbe FIFO übertragen.
Eine noch nicht veröffentlichte Freigabe kann deshalb nicht von einem
späteren Anschlag desselben Produzenten überholt werden.

Die Platzregel lässt sich als Invariante prüfen. Mit `q` wartenden
Ereignissen, `d` vom Produzenten als gedrückt geführten Tasten und Kapazität
`N = 256` gilt nach jeder abgeschlossenen Produzentenoperation:

```text
q + d <= N
```

* Ein Anschlag wird nur angenommen, wenn die Ungleichung auch mit seinem
  Queue-Eintrag und gegebenenfalls einer zusätzlichen gedrückten Taste gilt.
* Bei der Freigabe einer gedrückten Taste wächst `q` um eins und `d` sinkt
  um eins. Da vorher `d >= 1` ist, gibt es diesen freien Queue-Platz.
* Eine Freigabe einer nicht gedrückten Taste ändert nichts.
* Entnahmen des Verbrauchers verkleinern `q`. Sie gefährden die Reserve nicht.

Bitset und Zähler werden ausschließlich vom Produzenten benutzt. Zwischen
`Push()` und der anschließenden Buchführung darf der Audiothread bereits
entnehmen: Er benutzt diese Buchführung nicht, und eine weitere
Produzentenoperation beginnt erst nach ihrem Abschluss. Die Annahme eines
einzigen UI-Produzenten entspricht auch dem mitgelieferten SDK-Vertrag für
`IConnectionPoint::notify` (`pluginterfaces/vst/ivstmessage.h:86`).

`SizeForProducer()` liest den eigenen Schreibzähler relaxed und den
Lesezähler acquire. Der einzige Produzent kann seinen eigenen letzten
Schreibstand nicht unterschreiten; ein älterer Lesestand überschätzt nur
die Belegung. Zwischen Größenprüfung und `Push()` kann der Verbraucher
weiter entnehmen. Die Acquire-/Release-Übertragung der Einträge bleibt
unverändert. Auf dem geprüften Windows-x64-Ziel wird die Subtraktion der
32-Bit-Zähler vor der Erweiterung zu `size_t` vorzeichenlos ausgeführt;
auch der Zählerüberlauf ist damit abgedeckt. Eigene Grenztests siehe unten.

### Aktivierung, Zustand und Lebensdauer

`setActive()` leert die Queue ohne paralleles `process()`. Es verändert die
UI-Buchführung nicht. Die übrig gebliebenen Reservierungen sind konservativ:
Eine später losgelassene Taste kann ihren Off-Eintrag weiterhin einreihen.
Die Engine startet bei Aktivierung neu, und bereits an den Host gesendete
Anschläge werden im nächsten Audioblock freigegeben.

Zustandsladen und die 500-ms-Frist verwerfen alte Anschläge beim Verbrauch.
Sie nehmen dem Produzenten ebenfalls keine Reserve weg. Freigaben werden
ohne diese Frist verarbeitet. Nach alter Freigabe und neuem Anschlag bleibt
der neue Anschlag in der FIFO-Reihenfolge erhalten.

Die UI-Kette wurde am Code verfolgt: `Controller::SendKey`, die
Editor-Callbacks und `KeyboardView::Press/Release`. Ein abgewiesener
Anschlag wird nicht als angenommen gebucht. Das spätere Loslassen ist dann
ein unschädlicher No-op. Bei einem abgewiesenen Wiederanschlag bleibt die
Reserve der bereits gedrückten Taste bestehen. Das Fenster kann die
gedrückte Taste bis zum Loslassen weiter hervorheben, obwohl der Anschlag
abgewiesen wurde; es gibt keinen automatischen Wiederholungsversuch. Das
entspricht der dokumentierten Überlastregel. Fensterbedienung und Darstellung
wurden nicht praktisch geprüft.

## Eigene zusätzliche Proben

Alle Proben wurden in Release und Debug gebaut und bestanden. Produktcode
wurde dafür nicht geändert. `prepare_probes.py` erzeugt eine temporäre
Kopie von `processor.cpp` mit zwei Wartepunkten; die normale Queue-,
Noten-, Zustands- und Audiologik bleibt erhalten. Die regulären Tests
benutzen weiterhin den unveränderten Produktprozessor.

| Eigene Probe | Ergebnis |
| --- | --- |
| Gesteuerte Konkurrenzfolge zu EL-R06 | 255 Anschläge derselben Taste, UI vor der Veröffentlichung ihres Offs angehalten; ein Audioblock leert die Queue. Der nächste Block hält vor dem Queue-Lesen an, dann veröffentlicht die UI Off und neuen On. MIDI endet auf `N60 F60 N60`, Gate und eine gehaltene Taste bleiben eine Sekunde bestehen. Erst das abschließende neue Off beendet sie. PASS in Release und Debug. |
| Maximale Freigabereserve | Alle 128 Notennummern angeschlagen; weitere Anschläge werden an der Reservegrenze abgewiesen; alle 128 Freigaben in umgekehrter Reihenfolge werden angenommen. Danach keine gehaltene Note. PASS. |
| Reserve über Lebenszyklusgrenzen | Obige Folge zusätzlich mit Deaktivierung/Aktivierung, Zustandsladen einschließlich Null-Sample-Flush und Ablauf der 500-ms-Frist. Alte Anschläge bleiben stumm; ein nachfolgender neuer Anschlag hält und lässt sich freigeben. PASS. |
| Vollständige Reihenfolge unter gleichzeitiger Überlast | 40000 zufällige Nachrichten auf vier Tasten, UI und Audio parallel. Jeder angenommene Anschlag und jede wirksame Freigabe gegen eine unabhängig geführte erwartete MIDI-Ereignisfolge verglichen, einschließlich Wiederanschlägen; abschließend keine gehaltene Taste. Release: 25213 angenommen, 14787 Anschläge abgewiesen. Debug: 28271 angenommen, 11729 Anschläge abgewiesen. Keine Freigabe abgewiesen; beide vollständigen Folgen identisch zur Erwartung. |
| Queue-Zählerüberlauf | Originalimplementierung mit vier gesetzten Zählerstartständen `0`, `0xffffff00`, `0xfffffffe`, `0xffffffff`: je 1024 vollständige Füll-/Leerzyklen, Belegung und Reihenfolge korrekt. Zusätzlich zwei Millionen FIFO-Übertragungen mit zwei Threads, beginnend nahe dem Überlauf. PASS in beiden Konfigurationen. |
| Allokationen im Audiopfad der eigenen Plugin-Proben | Kein erfasster `operator new`-Aufruf. Grenzen des vorhandenen AllocationProbe gelten weiter: kein Nachweis für `malloc`, ausgerichtete Allokationen oder fremde DLLs. |

Die Zähler wurden nur in der separaten Queue-Probe für den Teststart
zugänglich gemacht; der Algorithmus selbst ist unverändert. Die
Zwei-Thread-Läufe ergänzen die Prüfung der Invariante und Speicherordnung;
sie sind kein Beweis für alle möglichen Thread-Zeitfolgen.

## Builds und reguläre Tests

Eigener Build mit VS 2026/MSVC 19.51, Windows x64, unter
`.work/elements-review/followup-0.1.2/build`. Keine installierte Kopie benutzt.
Der erste sandboxierte MSBuild-Aufruf blieb beim Unterprozessstart hängen
und wurde abgebrochen. Builds und Tests liefen danach mit automatisch
genehmigter Ausführung außerhalb der Sandbox. Ausgaben blieben im
Prüfunterordner.

| Prüfung | Release | Debug |
| --- | --- | --- |
| Build Mendelejew und drei reguläre Testprogramme | PASS | PASS |
| Steinberg-Validator Mendelejew | 47 bestanden, 0 fehlgeschlagen | 47 bestanden, 0 fehlgeschlagen |
| Vier reguläre Elements-CTest-Tests | 4/4, 194,71 s | 4/4, 139,49 s |
| Zusätzliche Plugin- und Queue-Proben | PASS | PASS |

Alle 101 Tabellenzeilen der neuen Release-DSP-Messung sind identisch zu
`DSP_MEASUREMENTS_0.1.1.md`. Die Compilerwarnungen des regulären Builds
stammen aus VSTGUI. Eine zunächst falsch übergebene CMake-Option führte
beim Aufruf eines zusätzlichen Review-Ziels zu „Projektdatei nicht vorhanden“;
nach Korrektur wurden beide zusätzlichen Ziele erfolgreich gebaut. Der
reguläre Release-Build wurde anschließend nochmals mit Exitcode 0 bestätigt.

## Grenzen und Dateien

Keine DAW-, Hör- oder praktischen Fenstertests; kein ASan-/TSan-Lauf.
Der bekannte HOLD für internen 96-kHz-Pluginbetrieb bleibt bestehen.
Keine Aussage über einen bereits integrierten Stand mit aktuellem `main`.

Als Produktdokumentation wurde nur dieser Bericht erstellt. Keine
Produktfehler selbst behoben, keine Commits, Pushes oder GitHub-Kommentare
erzeugt. Der Branch blieb auf dem geprüften Commit.

Reproduzierbare Zusatzproben und Logs liegen unter
`.work/elements-review/followup-0.1.2/`: `prepare_probes.py`, `inject.cmake`,
`probe.cpp`, `queue.cpp`, `processor_scheduled.cpp`, `probe-*.log`,
`queue-*.log`, `ctest-*.log`, `build-*.log` und die kopierte
`dsp-measurements-Release.md`.

Die selbst erzeugten Builddateien (1286,7 MiB) wurden nach Abschluss entfernt,
um den Arbeitsbereich nicht mit den Debug-/Release-Zwischendateien zu belasten.
Logs, Zusatzproben und die kopierten DSP-Messungen bleiben erhalten.
