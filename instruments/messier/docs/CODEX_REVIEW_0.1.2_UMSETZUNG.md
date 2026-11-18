# Umsetzung von Codex' Nachprüfung zu Messier 0.1.2

Nachprüfung: [CODEX_REVIEW_0.1.2.md](CODEX_REVIEW_0.1.2.md), geprüfter Stand `381f06a`,
Urteil KEINE FREIGABE; MS-R07 und MS-R10 bestätigt behoben. Umgesetzt von Claude am
2026-11-18 als **Messier 0.1.3**. Alle fünf neuen Befunde wurden am Code nachvollzogen und
stimmen. Der Klang ist unverändert, der bitgenaue Vergleich mit dem Firmware-Kern besteht.

| Befund | Schwere | Stand | Wo | Test |
| --- | --- | --- | --- | --- |
| MS-R11 Parametertexte können eine Ausnahme weitergeben | P1 | behoben | `source/controller.*` (`DisplayParameter`, `getParamStringByValue`, `getParamValueByString`) | `TestNoExceptionLeavesHostCalls`, zwei neue Aufrufe |
| MS-R12 Ausgebliebene Statusantwort verschluckt die Meldung | P2 | behoben | `source/parameters.h` (`SampleChangesValue/Count`), `source/controller.cpp` (`CountUserLoads`), `source/processor.cpp` | `TestFileChangeMarksProject`, neuer Abschnitt |
| MS-R13 Reset über Blockgrenzen zusammengefasst | P3 | behoben bzw. dokumentiert | `source/processor.cpp` (`setActive`), README, CHANGELOG, Planung | `TestResetButtonThroughHost`, zwei neue Abschnitte |
| MS-R14 Abgelehnter Projektwechsel ändert den gespeicherten Pfad | P2 | behoben | `source/file_loader.cpp` (`LoadProject`) | `test_loader.cpp` `TestOutOfMemory` |
| MS-R15 Editor-Erstellung kann ein Objekt verlieren | P3 | behoben | `source/editor.*` (`ShowFileStatus`), `source/controller.cpp` | `TestNoExceptionLeavesHostCalls`, `createView` |

Stand der Altbefunde danach: MS-R06 behoben (die offene Stelle war MS-R11), MS-R08 behoben
(MS-R13), MS-R09 behoben (MS-R12).

## MS-R11: Parametertexte

`DisplayParameter::toString` gibt bei einer Ausnahme einen leeren Text zurück,
`fromString` dann `false`. Zusätzlich fängt der Controller in seinen eigenen
`getParamStringByValue` und `getParamValueByString` jede Ausnahme ab und antwortet
`kOutOfMemory`; beide wirken ohne Editor-Objekt. Test: Beide Aufrufe für alle 19 Parameter
im Countdown-Test, der jede Allokation der Reihe nach scheitern lässt (21 und 19
Allokationen); der Eingabetext ist dafür länger als der eigene Puffer von `std::string`.

## MS-R12: Projektänderung ohne Statusantwort

Parameter 199 trägt jetzt den Zähler selbst, modulo 65.536 und so normiert, dass er auch
einen Host übersteht, der Werte als Float hält (24 Bit Mantisse). Der Controller rechnet
bei jeder Änderung von 199 den Unterschied zum zuletzt gezählten Stand aus und ruft für jede
neue Datei einmal `setDirty(true)`, ohne auf die Statusantwort zu warten. Eine Statusantwort
geht durch dieselbe Zählung (`CountUserLoads`) und zählt bereits gezählte Dateien nicht noch
einmal; ohne Ausgabeparameter (ein Host, der sie nicht weiterreicht) bleibt sie der
Rückweg, solange ein Fenster offen ist. Der Deckel bei 1024 ist weg. Ein Rückschritt des
Zählers (ein neu gestarteter Processor) zählt nichts.

Test: Der Rückweg Processor → Controller wird getrennt; eine Datei aus dem Fenster markiert
das Projekt allein über Parameter 199; nach dem Wiederverbinden zählt die späte
Statusantwort nichts doppelt.

## MS-R13: Reset

Drucke überstehen jetzt eine Aktivierung (`setActive(true)` verwirft sie nicht mehr); sie
verfallen nur nach 0,5 s. Festgelegt und dokumentiert (README, CHANGELOG, Planung): Drucke,
die in denselben noch nicht gerechneten 64-Sample-Abschnitt der Engine fallen, wirken einmal,
denn es ist derselbe Neustart bei START. Ob zwei Drucke in zwei 1-Sample-Blöcken einmal oder
zweimal wirken, hängt deshalb davon ab, wo sie zum Raster liegen; nie wirken sie gar nicht.
Codex sah eine Auslösung, mein Test an einer Quantumsgrenze zwei. Test: ein Druck direkt vor
einer Aktivierung wirkt danach; zwei Drucke in zwei 1-Sample-Blöcken wirken ein- oder
zweimal.

## MS-R14: Abgelehnter Projektwechsel

`LoadProject` baut Texte und Dateiobjekt zuerst und übernimmt Pfad und Status nur, wenn ein
Dateiobjekt vorliegt. Fehlt es, lehnt `setState` das Projekt mit `kOutOfMemory` ab, und
Status und Pfad (den `getState` speichert) bleiben die des noch spielenden Projekts; der
Versuch wird getrennt als gescheiterte Anforderung gemeldet. Geändert gegenüber 0.1.1/0.1.2:
Kann ein Projekt mangels Speicher nicht vollständig übernommen werden (auch wenn nur der
Pfad nicht kopiert werden kann), wird es ganz abgelehnt, statt mit dem alten Pfad still zu
werden; sonst entstünde dieselbe Unstimmigkeit auf anderem Weg. Die Reserve dient dem leeren
Dateiobjekt: für ein Projekt ohne Datei oder mit unlesbarer Datei, wenn nicht einmal das
leere Objekt angelegt werden kann.

Test (`TestOutOfMemory`): ein Projekt bei Mangel ab 40 Bytes wird abgelehnt, Status und Pfad
bleiben (läuft auch im Debug-Bau); nur die Größe des Dateiobjekts scheitert: die Reserve wird
für ein Projekt ohne Datei genommen, ein zweites (fehlende Datei) danach abgelehnt, nach
Erholung ist die Reserve wieder da; im Release-Bau ab 16 Bytes ein Projekt mit Datei
abgelehnt, sein Pfad bleibt der gespeicherte.

## MS-R15: Editor-Erstellung

`Editor::ShowFileStatus` wirft nicht mehr (bei fehlendem Speicher bleibt die bisherige
Anzeige); damit kann `MakeEditor` das angelegte Objekt nicht mehr verlieren. Test:
`createView` im Countdown-Test (6 Allokationen).

## Bauen und Prüfen

Vollständige Matrix am 2026-11-18 auf dem uncommitteten 0.1.3-Stand (Logs in
`out/messier-0.1.3/logs/`):

| Bau | Build | ctest | Validator (alle vier Plug-ins) |
| --- | --- | --- | --- |
| VS 2026 (MSVC 19.51) Release | ok | 16/16 | 47/0 |
| VS 2026 Debug | ok | 16/16 | 47/0 |
| VS 2022 (MSVC 19.44) Release | ok | 16/16 | 47/0 |
| ASan (fünf Messier-Testprogramme) | ok | alle mit Exit 0 | keine Meldung |

Bitgenau mit dem getrennt gebauten Firmware-Kern: Engine-Schicht in allen fünf Szenarien,
Plug-in 0 von 960.000 Samples abweichend (10 s bei 48 kHz, wechselnde Blöcke,
0 Allokationen in `process`). Die Countdown-Tests (neun Host-Aufrufe) laufen nur im
Release-Bau, aus dem in der Notiz zum Debug-Bau genannten Grund.
