# Codex-Nachprüfung: Toy Piano Classic 0.3.1

VERDICT: FREIGABE MIT KORREKTUREN

Geprüft: `3aa03339201ba3f748067975e71fbdb38cf5edbd`, 2026-11-14,
Windows x64, MSVC 19.51.36256.0 (Visual Studio Build Tools 2026).

Grundlage: [CODEX_REVIEW_0.3.0_UMSETZUNG.md](CODEX_REVIEW_0.3.0_UMSETZUNG.md).
Nur die Änderungen zu R01–R06 und R08 wurden nachgeprüft. R07 bleibt
außerhalb dieses Auftrags und gehört in eine eigene Daisy-Seed-Aufgabe.

Die ursprünglichen Freigabehindernisse sind behoben. Ein kleiner Restfehler
in der Texteingabe bleibt; DAW-Tests können beginnen. Eine uneingeschränkte
Bestätigung „sieben von acht vollständig behoben“ wäre wegen R03 verfrüht.

## Verbleibender Befund

Dateiverweise relativ zu `instruments/toy-piano-classic/`.

| ID | Schwere | Befund | Beleg | Auslösung und Auswirkung | Nachweis | Minimale Korrektur |
| --- | --- | --- | --- | --- | --- | --- |
| R03-N1 | P2 | Doppeltes Vorzeichen wird als gültige Zahl angenommen | `source/value_text.h:59–60` | `+-5` wird bei allen fünf Parametern erfolgreich als `-5` verarbeitet. Master Pitch wird −5 st, Volume −5 dB; Attack, Decay und Bend werden an ihre Untergrenze geklemmt. Auch mit passender Einheit, etwa `+-5 dB`. | Bewiesen über `Controller::getParamValueByString` gegen die Release-Bibliotheken sowie in der ASan-Sonde gegen den festgehaltenen Commit. | Beim Entfernen des führenden `+` ein weiteres Vorzeichen ablehnen. Regressionstest für `+-5`, mit/ohne Einheit, über alle fünf Parameter; bei Ablehnung Ausgabewert unverändert lassen. |

Die Ursache: Nach dem Entfernen von `+` erhält `std::from_chars` den gültigen
Text `-5`. Die bisherigen Tests enthalten `--5` und `+`, aber kein `+-5`.
Dies ist kein Speicherfehler und kein Hindernis für den begonnenen DAW-Test.

## Ergebnis je ursprünglichem Befund

| ID | Ergebnis der Nachprüfung |
| --- | --- |
| R01 | Behoben. `editorDestroyed` entfernt anhand des gespeicherten Basiszeigers den Eintrag; der abgeleitete Destruktor schließt einen verbliebenen Frame. Tests mit echtem verborgenem HWND und ASan bestehen, einschließlich mehrerer Views, Wiederöffnung derselben View, Freigabe ohne `removed()`, nachfolgenden Parameter-/State-Updates und anschließender Windows-Nachrichtenverarbeitung. Nach Freigabe bleiben keine Kindfenster am Test-Elternfenster. |
| R02 | Behoben. `Approach` setzt bei einem auf null gerundeten Schritt auf das Ziel. Die neuen Tests für Volume und Pitch in beide Richtungen bei 8/44,1/48/96/192 kHz sowie die bitgenaue Volume-Rückkehr bestehen. |
| R03 | Ursprüngliche Fälle behoben: `foo`, `12junk`, NaN und positive Unendlichkeit werden abgelehnt; `-inf` bleibt bei Volume erlaubt. Ausgabe bleibt bei Ablehnung unverändert. Restfehler R03-N1 siehe oben. Zusätzlich wurden NaN mit Payload, `infinity`, Zahlenüber-/unterlauf und weitere fehlerhafte Zahlen geprüft. |
| R04 | Behoben. Bei abgelehntem KEYS-Umschalten werden die Einstellungen zurückgenommen. Geladene Einstellungen und erzwungene kleine Fenster passen den Inhalt auch unter 50 % ein; gespeicherter Nutzerzoom bleibt begrenzt. Mindestzoom und zusätzlich erzwungene 100 × 40 px geprüft. |
| R05 | Behoben in den geprüften Pfaden. Bei der Host-Anfrage stimmen bisheriges `getSize` und bisherige Frame-Größe überein; die angeforderte Größe wird über `onSize` angewendet. Zustimmung, Ablehnung, geladene Einstellungen und DPI vor/nach Öffnen geprüft. |
| R06 | Behoben durch die feste Schleifengrenze `i < kKeyboardQueueSize` mit Kapazität 64. Gleichzeitiges Nachfüllen kann die Zahl der Pops im Render-Aufruf nicht erhöhen. Volle Queue meldet jetzt Misserfolg; vorhandener Queue-Test besteht. Einschränkung des Regressionstests siehe unten. |
| R08 | Behoben. Beide Präzisionen laufen tatsächlich durch alle sieben Blockgrößen `1,16,48,64,333,1024,2048`; alle 14 Kombinationen bestehen. |

## Ausgeführte Prüfungen

| Konfiguration | Build | Validator am VST3-Bundle | CTest |
| --- | --- | --- | --- |
| VS 2026 Release | bestanden | 47 bestanden, 0 fehlgeschlagen | 2/2 bestanden |
| VS 2026 Debug | bestanden | 47 bestanden, 0 fehlgeschlagen | 2/2 bestanden |

Zusätzlich: erweiterte Prüfsonde gegen die Release-Bibliotheken und eine
ASan-EXE mit den Plugin-Implementierungen und dem Kern aus `3aa0333`;
beide enden mit Exitcode 0. SDK-Bibliotheken waren nicht instrumentiert;
STL-Container-Annotationen wurden dafür deaktiviert. Keine ASan-Meldung in
den ausgeführten Fällen. Der erste Sandbox-Build blieb bei MSBuild hängen;
die erfolgreichen Builds liefen außerhalb der Sandbox. Warnungen betrafen
SDK/VSTGUI, keine eigenen Plugin-Quellen.

## Fehlende Tests und Grenzen

* R03-N1 fehlt als negativer Parserfall.
* `TestKeyboardQueueLimit` prüft Füllstand, Ablehnung der 65. Note und erneute
  Annahme nach einem Block. Er prüft nicht das Arbeitsbudget bei parallelem
  Nachfüllen: Entfernen allein der neuen Render-Schleifengrenze würde diesen
  Test nicht fehlschlagen lassen. R06 ist hier durch den Code belegt; ein
  gezielter Regressionstest für die Obergrenze fehlt weiterhin.
* Keine DAW, Hörprüfung, GUI-Gestaltungsprüfung oder neue Performance-Messung;
  kein VS-2022-Build, kein vollständiger erneuter 0.3.0-Gesamtreview.
* Kein erneuter GitHub-Abgleich. Lokaler Commit und Auftraggeberangabe sind
  die Grundlage. Submodul-Git-Metadaten waren weiterhin gesperrt; gebaut
  wurde mit den vorhandenen SDK-Dateien.
* Während der Prüfung kamen fremde lokale Änderungen an Engine und Tests
  für R07 hinzu. Sie wurden nicht verändert oder mitgeprüft. Die zusätzlichen
  ASan-Prüfungen verwenden deshalb ausdrücklich aus `3aa0333` extrahierte
  Plugin-/Core-Dateien. Die oben genannten Build-Ergebnisse betreffen den
  ursprünglichen Stand; die CTest-Läufe verwendeten die zuvor gebauten
  Testprogramme. Spätere fremde Neubauten wurden nicht als Nachweis verwendet.
* Nur dieser neue Bericht wurde dauerhaft hinzugefügt. Kein Produktcode,
  kein bestehender Bericht und keine Tests geändert; nichts committet oder
  gepusht. `dist/` und installierte Plugins blieben unberührt. Eigene
  Wegwerf-Sonden entfernt; `build/` wegen der inzwischen parallelen lokalen
  Build-Arbeiten belassen.
