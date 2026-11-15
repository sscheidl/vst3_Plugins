# Nachprüfung: Mendelejew 0.1.1

Datum: 2026-11-15. Prüfer: Codex.
Grundlage: [Umsetzungsbericht](CODEX_REVIEW_0.1.0_UMSETZUNG.md),
[ursprünglicher Prüfauftrag](CODEX_PRUEFAUFTRAG_0.1.0.md) und
[Review 0.1.0](CODEX_REVIEW_0.1.0.md).

**Ergebnis: noch keine Empfehlung zum Merge.** EL-R02 bis EL-R05 sind im
geprüften Umfang behoben. EL-R01 besteht im ursprünglichen Testfall nicht
mehr; seine neue Absicherung enthält jedoch einen Fehler bei gleichzeitiger
UI-/Audio-Verarbeitung, hier als **EL-R06 (P2)** dokumentiert. Die Aussage
„alle fünf behoben und keine neuen Fehler“ ist damit nicht bestätigt.

## Geprüfter Stand

Branch `elements-0.1.0`, Head `2f9b0ff`:
`Mendelejew 0.1.1: correct the note on Torus Classic in the review follow-up`.
Die funktionalen Änderungen stammen aus `4354a8c`; Vergleich gegen den
zuletzt geprüften Stand `c3c3623`. Bei Beginn keine Produktänderungen in der
Arbeitskopie. Die vorgefundenen unversionierten Torus-Berichte wurden nicht
angefasst.

Das ist weiterhin eine **monophone** Engine. Eine Polyphonie-Implementierung
ist in diesem Stand nicht enthalten; laut Klarstellung des Nutzers gehört
die erwähnte Polyphonie zu Torus/Rings. Diese Nachprüfung betrifft Mendelejew.

## Status der fünf Befunde

| Befund | Nachprüfung | Nachweis und Grenze |
| --- | --- | --- |
| EL-R01: voller Klaviaturpuffer verliert Note-Off | Teilweise behoben | Der Test des alten Überlastfalls besteht jetzt: 64 Queue-Ereignisse plus ausgelagerte Freigabe enden mit geschlossenem Gate und vollständigem MIDI-Off. Neue konkurrierende Folge verletzt aber die zugesagte Reihenfolge, siehe EL-R06. |
| EL-R02: abgewiesenes MIDI-Off gilt als gesendet | Behoben im geprüften Umfang | `host_keys_` und `owed_releases_` halten erfolgreiche Anschläge und ausstehende Freigaben getrennt. Tests mit einer Ausgangskapazität von 1, erneutem Anschlag derselben Taste und zeitweise fehlender Ausgangsliste bestehen. Keine verlorene endgültige Freigabe nachgewiesen. Ein neuer MIDI-Anschlag kann bei Überlast weiterhin absichtlich entfallen; die eigene Engine spielt ihn trotzdem. |
| EL-R03: Ausgang überschreitet hinter SRC ±1 | Behoben | Die letzte Stufe in `Processor::Render()` begrenzt nach der Wandlung. Die alte Reproduktion erreicht jetzt exakt 1,0 statt 1,23976874. Die Tests bei 44,1/48/88,2/96/192 kHz bestehen. Ein zusätzlicher Vergleich mit Einzel-Samples und gemischten Blockgrößen liefert bitgleiche Ausgaben auch bei tatsächlich aktiver Begrenzung. |
| EL-R04: Hoch-Q-Näherung statt genauer Polabbildung | Behoben im geprüften Umfang | Die Normalmoden verwenden jetzt die Polabbildung `p_neu = p_alt^(1/ratio)`. Herleitung und Rückrechnung von g/Q geprüft; zusätzlich 39040 Fälle über eine unabhängige komplexe bilineare Polberechnung kontrolliert, inklusive Q bis 400000. Alle Koeffizienten/Verstärkungen endlich und positiv; maximale relative Abweichung des Polwinkels etwa 3,37e-7, der Abklingrate etwa 5,76e-6. Die Amplitudenprüfung im vorhandenen Test verwendet eine gerechnete Impulsantwort und besteht. |
| EL-R05: unskalierte Anfangsfilter | Behoben im geprüften Umfang | `Resonator::Init()` initialisiert bei anderen Raten alle normalen Filter samt Gain mit dem umgerechneten Original-Startfilter. Der isolierte Mode-27-Vergleich ohne Vorlauf besteht bei 32/48/96 kHz. Der 32-kHz-Pfad bleibt unverändert. |

Die gebogenen Moden behalten bewusst die vorige Umrechnung ihrer
Bandpassmitte und Bandbreite im Delay-Kreis. Das ist im Code und in
`UPSTREAM.md` ausdrücklich getrennt beschrieben. Keine versehentliche
Übernahme der neuen Normalmoden-Koeffizienten in diese Schleife gefunden.

## EL-R06 — P2: Ein verspätet übernommenes Loslassen beendet den neueren Anschlag

**Fundstellen:** `source/processor.cpp:253–275` und `445–447`;
`tests/test_plugin.cpp:764` (`TestKeyboardLimits`).

Die ausgelagerten Freigaben werden nur einmal vor dem Leeren der Queue in
den lokalen Audiothread-Zustand übernommen. Danach kann die Queue bereits
neuere Anschläge liefern, während eine ältere Freigabe noch in der atomaren
Übergabe liegt. Die Sequenznummer verhindert ein zu frühes Anwenden einer
bereits übernommenen Freigabe. Sie verhindert nicht, dass eine noch nicht
übernommene Freigabe von einem späteren Queue-Ereignis überholt wird.

### Reproduzierte Reihenfolge

Ein UI-Produzent und ein Audio-Verbraucher, reguläre `notify()`-/
`process()`-Aufrufe. Die Taste 60 wird im letzten Schritt neu angeschlagen
und danach **nicht** losgelassen:

| Schritt | UI-Thread | Audio-Thread |
| --- | --- | --- |
| 1 | 64 Anschläge von Note 60 füllen die Queue, Sequenzen 1–64. | Noch kein Block. |
| 2 | Note-Off 60 trifft die volle Queue. `overflowed_releases_[60]` erhält 65. Unterbrechung unmittelbar vor `releases_overflowed_.store(true)`. | |
| 3 | Weiter unterbrochen. | Erster Block sieht das Flag noch als false und verarbeitet Sequenzen 1–64. |
| 4 | Weiter unterbrochen. | Zweiter Block prüft das Flag erneut: noch false. Unterbrechung nach diesem Snapshot, vor dem Queue-Leeren. |
| 5 | Fortsetzung: Flag true veröffentlichen, danach Note-On 60 als Sequenz 65 erfolgreich einreihen. | |
| 6 | Taste 60 bleibt gedrückt; kein weiterer Note-Off. | Zweiter Block verarbeitet den neuen Anschlag 65. Die ältere Freigabe wurde noch nicht übernommen. |
| 7 | | Dritter Block übernimmt endlich den alten Off-Marker 65. Da `65 - 1 <= keyboard_taken_` gilt, wird Note 60 freigegeben und damit der neuere Anschlag beendet. |

**Gemessen:**

```text
RACE_BEFORE_LATE_RELEASE gate=1 held=1
RACE_AFTER_LATE_RELEASE gate=0 held=0
MIDI-Ende: ... F60 N60 F60
```

Erwartet wären eine Freigabe des alten Anschlags **vor** dem neuen Note-On
und danach `gate=1`, `held=1`. Die neue Note wird tatsächlich sowohl intern
als auch am MIDI-Ausgang vorzeitig beendet.

### Art des Nachweises

Deterministischer Zwei-Thread-Schedulertest mit zwei Wartepunkten in einer
temporären Kopie von `processor.cpp`: unmittelbar vor Veröffentlichung des
Overflow-Flags und unmittelbar nach dem einmaligen Freigabe-Snapshot. Die
eigentliche Zustands-, Queue- und Audiologik wurde nicht verändert. Der
Test steuert lediglich eine Reihenfolge, die auch durch gewöhnliche
Thread-Unterbrechungen entstehen kann. Die regulären Produktdateien blieben
unverändert. Das ist ein Nachweis dieser zulässigen Konkurrenzfolge,
keine Aussage über ihre Häufigkeit in einer bestimmten DAW.

Die neu hinzugefügten normalen Tests liefern ihre Nachrichten jeweils vor
dem Aufruf des Audioblocks ein. Sie prüfen den ursprünglichen Defekt,
aber nicht diese gleichzeitige Veröffentlichung.

### Vorschlag

Ausgelagerte Freigaben und Queue-Ereignisse anhand ihrer Sequenzen gemeinsam
geordnet verarbeiten. Vor dem Spielen eines neu abgeholten Anschlags muss
auch eine ihm vorausgehende, inzwischen veröffentlichte Overflow-Freigabe
berücksichtigt werden. Beispielsweise die Übergabe nach dem erfolgreichen
Queue-Pop erneut abgleichen und fällige ältere Freigaben vor diesem Ereignis
anwenden. Die Acquire-/Release-Beziehung zum Queue-Ereignis muss dabei
Teil der Lösung sein; lediglich ein stärkerer Speicherordnungswert am
einmaligen Snapshot beseitigt die gezeigte Zeitfolge nicht.

Die obige Unterbrechung über zwei Audioblöcke als gezielten Regressionstest
ergänzen. Keine wartende oder sperrende Fehlerbehandlung im Produkt-
Audiothread einführen.

## Validierung und Grenzen

Eigener Build mit Visual Studio 2026/MSVC 19.51 unter
`.work/elements-review/followup-0.1.1/build`, keine installierte Kopie benutzt.
Die Sandbox blockierte den SDK-Git-Metadatenzugriff und der erste
MSBuild-Lauf kam über den Start seiner Unterprozesse nicht hinaus. Die
Builds/Tests liefen anschließend mit automatisch genehmigter Ausführung
außerhalb dieser Sandbox; alle Buildausgaben blieben im Prüfunterordner.

| Eigene Prüfung | Ergebnis |
| --- | --- |
| Release-Build Mendelejew und drei Testprogramme | PASS |
| Validator Release | 47 bestanden, 0 fehlgeschlagen |
| Release-CTest, vier Elements-Tests | 4/4, 207,39 s |
| Debug-Build Mendelejew und drei Testprogramme | PASS |
| Validator Debug | 47 bestanden, 0 fehlgeschlagen |
| Debug-CTest, vier Elements-Tests | 4/4, 161,40 s |
| 32-kHz-Vergleich im Release-Lauf | 48 Szenarien, 6 144 000 Samples bitgleich |
| Release-Messtabellen gegen `DSP_MEASUREMENTS_0.1.1.md` | Alle 101 Tabellenzeilen identisch |
| Zusätzlicher Vergleich bei aktiver Ausgangsbegrenzung | Identische Samples für Blockgröße 1 und gemischte Größen 1–2048 bei 44,1/48/96/192 kHz |
| Unabhängige komplexe Polrechnung | 39040 Fälle bestanden |
| Deterministischer Konkurrenztest | EL-R06 reproduziert |

Die grünen normalen Tests widersprechen EL-R06 nicht: Sie enthalten diese
konkurrierende Folge nicht. Die neuen Review-Probes ändern keine regulären
CTest-Targets. Compilerwarnungen im regulären Build stammen aus VSTGUI;
die zusätzliche Moden-Probe sieht die im Produktbuild unterdrückten
Upstream-Konvertierungswarnungen.

Die unabhängige Polprüfung deckt logarithmisch 0.00001 bis 0.49 relative
Originalfrequenz und Q von 1 bis 400000 bei Ratenverhältnissen 1,5 und 3 ab.
Sie nutzt `p = (1 + g*u)/(1 - g*u)` mit
`u = -1/(2Q) + i*sqrt(1 - 1/(4Q²))`; damit wird nicht bloß dieselbe
`atan2`-/`expm1`-Implementierung aus `ConvertMode()` nochmals abgeschrieben.
Eine solche endliche Rasterprüfung ist kein Beweis für jeden reellen
Eingangswert und keine Hörprüfung.

Engine, Adapter und die bestehenden Plugins sind gegenüber `c3c3623`
unverändert. Die Änderungen außerhalb des eigentlichen Fixcodes, die
Versionierung und Dokumentation wurden mit durchgesehen. Insbesondere ist
die begrenzte Messbandbreite der Low-Pass-Zahl jetzt korrekt benannt. Kein
weiterer konkreter neuer Defekt im geprüften Diff nachgewiesen; das ist
keine Garantie allgemeiner Fehlerfreiheit.

Nicht Gegenstand: Fensterbedienung, reale DAWs und Hören. Kein zusätzlicher
ASan-/TSan-Lauf. Der bekannte HOLD für internen 96-kHz-Pluginbetrieb bleibt.

## Ergebnisdateien und Arbeitsordner

Nur dieser Nachprüfbericht wurde als neue Produktdokumentation erstellt.
Keine Fehler selbst behoben, keine Commits oder GitHub-Kommentare erzeugt.
Der Bericht 0.1.0 bleibt ein historisches Dokument seines geprüften Stands.

Auf die Nachfrage zum Platzverbrauch wurden die alten Builddateien
(1343,5 MiB) und Referenzdownloads (1,7 MiB) entfernt, insgesamt rund 1,3 GB.
Die Berichte und kleinen Prüfscripte sind erhalten. Der neue Build der
Nachprüfung wurde nach den Läufen ebenfalls entfernt. Kleine Reproduktionen
und Protokolle bleiben unter `.work/elements-review/followup-0.1.1/`,
insbesondere `prepare_probes.py`, `probe_main.txt`, `mode_probe.cpp`,
`inject.cmake`, `repro-plugin.log`, `repro-modes.log` und die Build-/Testlogs.
