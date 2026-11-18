# Umsetzung von Codex' Review zu Messier 0.1.0

> **Nachtrag 2026-11-18:** Codex' Nachprüfung fand Lücken in den Behebungen von MS-R01 bis
> MS-R04 (MS-R06 bis MS-R10). Insbesondere arbeiten Projektübergabe, Reset-Taster und
> Projektänderung seit 0.1.2 anders als hier beschrieben; siehe
> [CODEX_REVIEW_0.1.1_UMSETZUNG.md](CODEX_REVIEW_0.1.1_UMSETZUNG.md).

Review: [CODEX_REVIEW_0.1.0.md](CODEX_REVIEW_0.1.0.md), geprüfter Stand `26b8509`,
Urteil KEINE FREIGABE. Umgesetzt von Claude am 2026-11-17 als **Messier 0.1.1**. Jeder
Befund wurde zuerst am Code nachvollzogen; alle vier Fehler stimmen. Der Klang ist
unverändert: Der bitgenaue Vergleich mit dem Firmware-Kern besteht wie vorher.

| Befund | Schwere | Stand | Wo | Test |
| --- | --- | --- | --- | --- |
| MS-R01 Speicherfehler beendet den Prozess | P1 | behoben | `source/wav_reader.cpp`, `source/file_loader.*`, `source/processor.cpp`, `source/controller.cpp` | `test_loader.cpp` `TestOutOfMemory` |
| MS-R02 Projektwerte mit alter Datei | P2 | behoben | `source/processor.*` (`AdoptPendingProject`), `source/file_loader.cpp`, `source/parameter_state.h` | `test_plugin.cpp` `TestProjectBackpressure` |
| MS-R03 Reset-Druck geht beim Host verloren | P2 | behoben | `source/editor.*` (`PressReset`, `ServiceReset`) | `test_plugin.cpp` `TestResetButtonThroughHost` |
| MS-R04 Dateiwechsel meldet keine Projektänderung | P2 | behoben | `source/file_loader.*` (`user_loads`), `source/controller.cpp` | `test_plugin.cpp` `TestFileChangeMarksProject` |
| MS-R05 Atlas/Orion-Fenster fehlt | P2, Umfang | offen, Entscheidung des Nutzers | – | – |

## MS-R01: Speicherfehler beim Laden

Nachvollzogen: Nur das PCM-Array war `new(std::nothrow)`; `FileData`, der Umrechnungspuffer
von 1 MiB und alle Strings konnten werfen, und `FileLoader::Run()` fing nichts ab.

Behoben auf drei Ebenen:

* `DecodeWav` und `ReadWavFile` sind `noexcept`. Eine fehlgeschlagene Allokation ergibt
  `kTooLarge` mit „out of memory“. Der Grund steht in einem festen Puffer im Ergebnis
  (`WavResult::detail`, 96 Zeichen) statt in einem `std::string`, sodass ein Ergebnis ganz
  ohne Allokation entsteht.
* `FileLoader`: `Request`, `LoadNow` und `Finish` sind `noexcept`. Das Melden des Ergebnisses
  baut zuerst alle Texte und weist dann zu; scheitert dabei etwas, setzt ein Ersatzweg nur
  kurze Texte. Eine Anforderung aus dem Fenster scheitert dann, die alte Datei bleibt. Ein
  Projekt wird still; dafür hält der Lader ein leeres Dateiobjekt in Reserve, das er nach
  jedem Ergebnis wieder anlegt. Kann nicht einmal der Pfad eines Projekts gespeichert
  werden, bleibt der alte Pfad im Status (dokumentierte Grenze).
* Host-Aufrufe, die Strings bauen (`getState`, `setState`, die Nachrichten in
  `Processor::notify` und `Controller::notify`, `setComponentState`, `LoadFile`), geben
  `kOutOfMemory` zurück statt zu werfen.

Test `TestOutOfMemory` mit eigenem `operator new`, das ab einer Größe in jedem Thread
`std::bad_alloc` wirft:

* Umrechnungspuffer (genau 1 MiB) einer kleinen Datei im Ladethread;
* PCM-Array einer 1,2-MB-Datei, deren Puffer noch passt;
* fast alles (ab 40 Bytes), auch der Pfad der Anforderung;
* Projektladen im aufrufenden Thread ab 40 und ab 16 Bytes, wobei beim zweiten auch das
  leere Dateiobjekt nicht mehr gebaut werden kann (die Reserve kommt zum Einsatz und wird
  danach wieder angelegt).

Jedes Mal lebt der Prozess weiter, die Anforderung steht auf „failed“ mit Grund, die alte
Datei spielt, ein Projekt wird still. Danach lädt alles wieder normal.

Grenze, beim Prüfen im Debug-Bau gefunden: Dort legt die Standardbibliothek von MSVC
auch in `noexcept`-Konstruktoren (etwa dem Default-Konstruktor von
`std::filesystem::path`) einen Container-Proxy von 16 Bytes an und beendet den Prozess
absichtlich, wenn das scheitert. Der erste Testlauf mit der Schwelle 16 hing deshalb am
Abbruchdialog des Debug-CRT. Der Fall mit der 16-Byte-Schwelle läuft daher nur im
Release-Bau, der ausgeliefert wird und keine Proxies hat; die Fälle ab 40 Bytes laufen in
beiden Bauarten. Eine gescheiterte Allokation von 16 Bytes heißt ohnehin, dass der Speicher
völlig erschöpft ist.

## MS-R02: Projektwerte und Datei getrennt

Nachvollzogen: `process()` nahm die Werte auch, wenn `TakeIncoming()` wegen vollem Rückstau
keine Datei gab, und startete mit der alten Datei neu. Die Werte kamen erst, nachdem die
Datei angeboten war; das stimmte, half aber bei Rückstau nicht.

Behoben:

* `process()` nimmt zuerst den geladenen Zustand und hält ihn als ausstehendes Projekt
  (`pending_project_`, nur Audio-Seite, keine Allokation). Erst wenn eine Datei genommen
  werden kann, kommen Werte und Datei zusammen in die Engine (`AdoptPendingProject`). Weil
  `setState()` die Datei vor den Werten anbietet, liegt dann im Austausch diese Datei oder
  eine neuere, oder der Audio-Thread hat sie schon. Bis dahin spielt der alte Klang
  vollständig weiter, und es wird keine andere Datei eingetauscht.
* Der Rückstau wird jetzt auch am Anfang jedes Blocks abgebaut, nicht nur am Ende, damit ein
  wartendes Projekt so früh wie möglich kommt. `setActive(true)` gibt wartende Dateien
  selbst frei (dort darf es) und übernimmt Projekt und Datei immer zusammen.
* Projektladungen sind nummeriert (`FileData::project`, `Loaded::project`). Eine Ladung aus
  dem Fenster verdrängt eine laufende Projektladung nicht mehr, sonst hätte ein Projekt
  auf eine Datei gewartet, die nie kommt. Grenze: Wählt jemand im Fenster eine Datei,
  während der Host gerade ein Projekt lädt, und ist die Fenster-Datei zuerst fertig, gewinnt
  die Datei des Projekts.
* Grenze: Kann bei völligem Speichermangel nicht einmal die Reserve angeboten werden,
  übernimmt das Projekt die bisherige Datei.

Test `TestProjectBackpressure`: 400 Projektladungen im Wechsel, kleine Datei mit Freeze
aus, große mit Freeze an, nach jeder ein Block von 64 Samples. In 340 bis 346 Blöcken (je nach Lauf) musste ein
Projekt warten (der Rückstau trat also ein); in keinem lief eine Datei mit den Werten des
anderen Projekts. Danach spielt das letzte Projekt. Keine Allokation in `process()`. Gegen
den Stand `26b8509` lässt sich dieser Test nicht unverändert bauen (er nutzt die neue
Schnittstelle von `LoadNow`); dass der alte Stand mischte, hat Codex' Nachstellung gezeigt
(83 von 200).

## MS-R03: Reset-Taster

Nachvollzogen: `PressReset()` meldete 1 und 0 im selben Aufruf; die Warteschlange des SDK
behält je Parameter und Position einen Wert.

Behoben: Der Druck geht sofort hinaus (`beginEdit`, `performEdit(1)`), das Loslassen 50 ms
später aus dem Timer des Fensters (`ServiceReset`, `performEdit(0)`, `endEdit`). Ein zweiter
Druck in dieser Zeit wird nach dem Loslassen nachgeholt. Schließt das Fenster mit
gehaltenem Taster, geht das Loslassen beim Schließen hinaus. Die Bedeutung von Parameter
121 für Automation bleibt: Die Flanke von 0 auf 1 wirkt.

Test `TestResetButtonThroughHost`: das echte `Editor`-Objekt (nicht geöffnet, den Timer
treibt der Test mit vorgegebenen Zeiten) an einem Controller mit einem Handler, der jede
Änderung an Position 0 des nächsten Blocks legt wie die SDK-Warteschlange. Ein Druck setzt
die Wiedergabe zurück, das Loslassen nicht; zwei schnelle Drücke ergeben zwei Resets; ein
Druck vor dem Schließen wirkt und endet sauber.

## MS-R04: Projektänderung

Nachvollzogen: Weder `LoadFile` noch die Statusantwort riefen `setDirty`.

Behoben: Der Lader zählt die Dateien aus dem Fenster, die zum Klang wurden
(`FileStatus::user_loads`, als Attribut der Statusnachricht). Wächst der Zähler, ruft der
Controller einmal `setDirty(true)`. Projektladen zählt nicht, Statusabfragen ändern nichts,
eine gescheiterte Anforderung zählt nicht.

Test `TestFileChangeMarksProject` mit einem `IComponentHandler2`: eine Datei aus dem Fenster
ergibt eine Meldung; fünf Statusabfragen keine weitere; eine kaputte Datei keine; ein
geladenes Projekt keine; die nächste Datei aus dem Fenster wieder eine.

## MS-R05: Atlas/Orion-Fenster

Nachvollzogen: Abschnitt 8 des Buildauftrags wurde um 21:58 erweitert, nachdem ich ihn
gelesen hatte; das gebaute Fenster ist das frühere neun-spaltige.

**Entscheidung des Nutzers vom 2026-11-17:** erst die vier Fehler als 0.1.1, das Fenster
bleibt vorläufig; das Atlas/Orion-Fenster folgt, wenn Aufteilung und Farben feststehen.
README, `docs/PLANUNG.md`, CHANGELOG und DAW-Prüfliste sagen das jetzt.

## Außerdem

* Beim Schreiben des Build-Berichts von 0.1.0 gefunden und dort schon beschrieben: Der
  Fenster-Timer fragte den Dateistatus nicht ab. In `26b8509` war das bereits behoben.
* Version 0.1.1 in `CMakeLists.txt`, `source/version.h`, `components.json`;
  `kEngineVersion` 0.1.1 (`FileData` hat das Feld `project`).

## Bauen und Prüfen

Auf dem Endstand von 0.1.1, alle vier Plugins, Windows 11, i7-8700K:

| Bau | Ergebnis |
| --- | --- |
| VS 2026 (MSVC 19.51), Release | 0 Fehler, keine Warnung im eigenen Code; Validator 47/0 für jedes Bundle; `ctest` 16 von 16 |
| VS 2026, Debug | dasselbe; `ctest` 16 von 16 (mit `--timeout 1500`) |
| VS 2022 (MSVC 19.44), Release, frischer Build-Ordner | dasselbe; `ctest` 16 von 16 |
| AddressSanitizer (VS 2026 Release, `/fsanitize=address`), die fünf Messier-Testprogramme | alle bestanden, keine Meldung |

Der bitgenaue Vergleich mit dem Firmware-Kern ist unverändert: Engine-Schicht 5 Szenarien je
0 von 1.152.000 Samples verschieden, Plugin bei 48 kHz 0 von 960.000. Das Bundle liegt in
`out/messier-0.1.1/`, die Logs in `out/messier-0.1.1/logs/`. Nicht wiederholt: die
Lastmessung (der DSP-Weg ist unverändert, die Zahlen im Build-Bericht gelten weiter).
