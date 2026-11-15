# Nachprüfung: Mendelejew 0.1.4 — Damping-Umkehr

Datum: 2026-11-15. Prüfer: Codex. Code der geprüften Korrektur: Claude.

**Ergebnis: PASS für die Korrektur in `7ad93a7`. Keine neuen Befunde.**
CL-R01 ist entsprechend der Nutzerentscheidung „wie Kekulé“ umgesetzt,
CL-R02 ist behoben. Die bekannten Einschränkungen bei alter Automation und
beim deaktivierten POLY-Tooltip bleiben wie dokumentiert bestehen.

## Geprüfter Stand und Umfang

Auftrag: [CODEX_PRUEFAUFTRAG_0.1.4.md](CODEX_PRUEFAUFTRAG_0.1.4.md).
Geprüft ist ausschließlich Claudes Commit
`7ad93a790e22c1b925557875d33d4898171a6b6e` gegenüber
`6f2b090`. Codex' vorherige Oberfläche und DSP-Untersuchung in `6f2b090`
sind hier kein erneuter Review-Gegenstand.

Zu Beginn war die Korrektur noch uncommittet. Für den eigenen Build wurde
`6f2b090` ohne Git-Metadaten nach
`.work/elements-review/followup-0.1.4/source/` exportiert und der eingefrorene
Korrektur-Diff darübergelegt. Die währenddessen von Claude ergänzte
Testpatch-Korrektur in `TestState` wurde anschließend übernommen und in
beiden Konfigurationen nachgeprüft. Zum Abschluss waren alle zwölf
geänderten, eingefrorenen Dateien anhand ihrer SHA256-Werte identisch mit
dem finalen Arbeitsbaum; dieser entsprach dem inzwischen vorhandenen
Commit `7ad93a7`. Die zwei zusätzlich committeten Review-/Auftragsdokumente
wurden gelesen, waren aber für den Build nicht erforderlich.

Die Produktquellen wurden von Codex nicht verändert. Haupt-Build, `out/`,
`dist/` und installierte Plugins wurden für diese Prüfung nicht verwendet
oder geändert. Eigene Builds und Prüfer lagen ausschließlich unter dem
genannten Review-Verzeichnis; das SDK wurde dort aus dem Haupt-Checkout
eingebunden. Keine Commits, Pushes oder GitHub-Kommentare durch Codex.

## Ohne Befund geprüft

| Gegenstand | Ergebnis und Fundstellen |
| --- | --- |
| Parameter → Engine | `parameters.h:82` spiegelt ausschließlich Damping. `processor.cpp:149` verwendet die Abbildung für Automation, `processor.cpp:88` für `Restart`. Initialisierung (`:44`), Aktivierung (`:123`) und geladenes State (`:138`) führen über diesen Restart. Der Engine-Bridge erhält bereits umgerechnete Werte und reicht sie unverändert weiter. Keine fehlende oder doppelte Spiegelung gefunden. |
| Speicherung und Migration | `state_io.h:36` akzeptiert ausschließlich Version 1/2; alle 19 Werte werden vollständig auf Endlichkeit und Bereich geprüft. Erst danach wird bei Version 1 Damping gespiegelt (`:43`); Version 2 bleibt unverändert. Übernahme erst bei `:51`. Prozessor und Controller verwenden denselben Leser. Schreiben erfolgt als Version 2. |
| Defaults | `parameters.h:87` enthält Damping 0,75, alle anderen Defaults einschließlich Pitch Bend bleiben erhalten. `DefaultOf` liest diese Tabelle. Der Engine-Default bleibt 0,25; die Umrechnung ergibt genau den bisherigen Startklang. Alle 20 Parameter werden im Controller-Test geprüft. |
| Oberfläche und CC | Der gewöhnliche `LinearParameter` in `controller.cpp:217` hat wieder Nullpunkt 0 und Skala 1. Im Editor sind Default (`editor.cpp:277`), Anfangswert (`:299`), Eingabe (`:565`) und Rückmeldung (`:616`) unmittelbar der Parameterwert; die bisherige `KnobValue`-Spiegelung ist vollständig entfernt. Das Mausrad verwendet unverändert +0,01, der Default-Reset 0,75. CC 23 ist weiterhin unmittelbar Parameter 113 zugeordnet (`controller.cpp:357`). |
| POLY | `editor.cpp:197` erkennt POLY; `clicked` bleibt dafür leer. Nur ENG, MOD, GEN erhalten Callbacks und die Panel-Indizes 0/1/2. POLY bleibt deaktiviert. Kein verborgener GEN-Aufruf mehr vorhanden. |
| Testpatches | 0,4 liefert der Engine 0,6; 0,2 liefert 0,8. Die nachgereichte `TestState`-Korrektur spiegelt auch dessen ursprünglichen Wert 0,655, sodass die Tonhöhenmessung wieder denselben ausreichend langen Ton untersucht. Die Messgrenzen wurden nicht gelockert. Der Übersteuerungsfall in `TestOutputBound` bleibt ein Version-1-State und wird deshalb korrekt migriert. |
| Dokumentation | README, CHANGELOG und DAW-Prüfliste beschreiben Parameter-Umkehr, Default, CC-Richtung und State-Migration zutreffend. README und CHANGELOG weisen ausdrücklich darauf hin, dass alte Hostautomation nicht konvertiert wird. Der tiefere Tastaturstart beim Laden alter Projekte ist nun dokumentiert. |
| Umfang | Kein Diff in Core, Engine, Adapter, `tests/test_dsp.cpp`, Toy Piano Classic oder Kekulé gegenüber `6f2b090`. Parameter- und Klassen-IDs bleiben erhalten. |

Die Prüfung von Reglerwegen und POLY erfolgte am Code und durch die
automatisierten Kontrolltests, nicht durch neue Mausaktionen in einem
laufenden Pluginfenster. Optik und DAW-Verhalten sind wie beauftragt
ausgenommen.

## Unabhängiger Audiovergleich

Zusätzlich zu den Produkt-Tests wurde ein eigener Prüfer gegen den
unveränderten Ausgangsstand und gegen die Korrektur gebaut. Der zweite Lauf
prüft ausdrücklich, dass er mit Klang-State-Version 2 kompiliert wurde.
Er rendert acht jeweils 24.000 Samples lange Stereoaufnahmen:

* Damping 0,6 bisher gegenüber 0,4 künftig;
* Damping 0,8 bisher gegenüber 0,2 künftig;
* unveränderter Defaultklang;
* mehrere Damping- und Notenwechsel innerhalb von Hostblöcken;
* Version-1-State laden im aktiven und im inaktiven Plugin;
* aktuelles State laden und unverändert wieder speichern;
* Parameter und Note in einem Aufruf ohne Audiosamples, danach Audio.

**Alle 1.536.000 Audiobytes der beiden Läufe sind identisch.** SHA256:
`fd4354753c0c0309dee9ce23af0ebb09944a757e881822e0039dc2c92093ffc9`.
Die erwarteten Engine-Zielwerte werden zusätzlich geprüft. Alle 156
abgeschnittenen Varianten des aktuellen Klang-States werden ohne Änderung
des gespeicherten Zustands abgelehnt. Keine durch den vorhandenen
`AllocationProbe` gezählte Audioallokation in diesen Fällen.

Dieser Vergleich belegt die getesteten Wege und Klänge, keine vollständige
Gleichheit für beliebige Patches oder alte Automationsspuren. Letztere
laufen nach der bewusst gewählten Parameter-Umkehr andersherum.

## Eigene Builds und Tests

Visual Studio 2026 Build Tools, Windows x64. Eigenes Build-Verzeichnis:
`.work/elements-review/followup-0.1.4/build/`.

| Prüfung | Release | Debug |
| --- | --- | --- |
| Vollständiger Build des Korrekturstands | Exit 0 | Exit 0 |
| VST3-Validator, alle drei Plugins | je 47 bestanden, 0 Fehler | je 47 bestanden, 0 Fehler |
| Neun übrige CTest-Ziele einschließlich DSP, Engine und beider Schwester-Plugins | bestanden | bestanden |
| `elements.plugin`, finaler Testpatch | bestanden, 10,18 s | bestanden, 22,12 s |

Damit sind alle zehn regulären Testziele in beiden Konfigurationen
erfolgreich geprüft. Die vollständigen Läufe vor Übernahme des letzten
Testpatches enthielten jeweils 9/10 erfolgreiche Ziele; nur
`elements.plugin` scheiterte an der zu kurzen Testnote. Nach dem reinen
Testdaten-Fix wurden gezielt dieser Test und sein Build erneut ausgeführt,
ohne die unveränderten DSP-/Engine-Tests zu wiederholen.

Im frischen Debug-Build gab es drei Warnungen ausschließlich aus dem
unveränderten VSTGUI-Code (`cshadowviewcontainer.cpp`, `ctexteditor.cpp`).
Im geänderten Produktcode wurden keine Compilerwarnungen ausgegeben.

### DSP-Messbericht

Der Release-DSP-Prüfer blieb zwischen dem Basisbuild und dem Korrekturbuild
sogar als ausführbare Datei SHA256-identisch. Der eigene Messlauf
reproduziert alle **101 bisherigen Tabellenzeilen** aus 0.1.1 und den
**vollständigen Hochtonabschnitt** aus `6f2b090`, einschließlich aller
24 Datenzeilen und der Sinustestwerte −47,05/−182,84 dBFS. Kein abweichender
DSP-Befund durch die Parameter-Umkehr.

## Einordnung der Vorläufe

* Ein erster inkrementeller Release-Versuch verwendete wegen erhaltener
  Kopierzeitstempel noch Basisbinärdateien. Er wurde verworfen; nach
  Aktualisierung der Zeitstempel wurden die geänderten Dateien neu gebaut.
  Erst der Lauf mit bestätigter State-Version 2 zählt als Korrekturvergleich.
* In diesem verworfenen, parallelen Basis-Testlauf scheiterte einmal
  `torus-classic.engine` an `other_quanta.load() > 0`. Im finalen seriellen
  Ablauf bestand derselbe unveränderte Test in beiden Konfigurationen.
  Eine Scheduling-Ursache ist naheliegend, hier aber nicht nachgewiesen;
  kein Befund gegen Claudes Damping-Diff.
* Das Scheitern der ursprünglichen kurzen `TestState`-Note ist in beiden
  Suite-Logs dokumentiert und durch den nachgereichten Testdaten-Fix
  behoben. Der anschließend beim `CHECK`-Abbruch auftretende Prozessabsturz
  entspricht dem bereits in Claudes Nachtrag genannten VSTGUI-Testproblem
  aus `6f2b090`; es wurde hier nicht als neue Produktregression bewertet.

## Belege und Grenzen

Logs, eingefrorener Diff, SHA256-Manifest und wiederverwendbare Prüfquellen:
`.work/elements-review/followup-0.1.4/`. Insbesondere:
`logs/differential-audio.log`, `logs/measurements-comparison.log`,
`logs/ctest-release.log`, `logs/ctest-debug.log` und
`logs/ctest-plugin-final-*.log`. Der erzeugte Messbericht liegt ebenfalls
unter `logs/`. Die temporären Quellenkopien, Build-Dateien und rohen
Audioaufnahmen werden nach Abschluss entfernt.

Kein neuer Hörtest, keine DAW-/Optikabnahme, kein ASan/TSan. Der auffällige
Nutzerpatch lag weiterhin nicht vor. Der bereits bekannte POLY-Tooltip
(CL-R04) bleibt außerhalb der beauftragten Korrektur offen.
