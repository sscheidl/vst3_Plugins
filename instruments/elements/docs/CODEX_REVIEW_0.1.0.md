# Code-Review: Mendelejew 0.1.0 (Projekt Elements)

Datum: 2026-11-14. Prüfer: Codex. Grundlage:
[CODEX_PRUEFAUFTRAG_0.1.0.md](CODEX_PRUEFAUFTRAG_0.1.0.md).

**Ergebnis: Änderungen vor dem Merge empfohlen.** Drei reproduzierte
Funktionsfehler der Priorität P2 und zwei begrenzte DSP-Abweichungen der
Priorität P3. Der offene PR-Befund zum verlorenen Loslassen ist bestätigt
(EL-R01). Die vorhandenen Tests und der Validator bestehen trotzdem.

## Prüfstand und Vorgehen

* Geprüfter Stand: `c3c362325064c8282edce0a3a291332674b5d81f`,
  `Elements 0.1.0: name the plug-in Mendelejew`, auf `elements-0.1.0`.
  Der Head von [PR #2](https://github.com/sscheidl/vst3_Plugins/pull/2)
  wurde vor Beginn über GitHub mit diesem lokalen Commit abgeglichen.
* Gesamte Änderung seit dem gemeinsamen Vorfahren mit
  `torus-classic-0.1.0`: `78e5b9a8d80e24e0deec3d80a1d3d4818e28102c`.
  Nicht lediglich den Umbenennungs-Diff geprüft.
* Eigene Builds, heruntergeladene Originalquellen, Vergleichsskripte und
  zusätzliche Testprogramme unter `.work/elements-review/`. Keine Änderung
  am Produktcode, an vorhandenen Tests, `build/`, `dist/`, `out/` oder
  installierten Plugins. Die beiden vorgefundenen unversionierten
  Torus-Reviewdateien blieben unverändert.
* Statische Durchsicht von `core/`, `engine/`, `adapter/`, Hülle und
  Integration; zusätzliche native Reproduktionen mit den unveränderten
  Produktbibliotheken. Als Testhost dient eine Arbeitskopie des Host-Doubles
  aus `tests/test_plugin.cpp`. Dessen Produktcode wurde nicht angepasst.
* MSVC 19.51.36256.0, Visual Studio 2026 Build Tools, CMake,
  Windows x64. SDK-Commit `9fad9770f2ae8542ab1a548a68c1ad1ac690abe0`.

P2 bedeutet hier einen zu behebenden Funktionsfehler. P3 bezeichnet eine
kleinere DSP- oder Aussageabweichung; ihre Hörbarkeit ist nicht nachgewiesen.
Fundstellen und Zeilennummern beziehen sich auf den geprüften Commit.

## Befunde

### EL-R01 — P2: Volle Klaviatur-Queue verliert auch Note-Offs

**Fundstellen:** `source/processor.cpp:363–367`,
`source/spsc_queue.h:20–26`, `source/controller.cpp:313–314`.

`Processor::notify()` behandelt Anschlagen und Loslassen gleich: Ist die
64 Einträge große Queue voll, verwirft `Push()` das Ereignis. Der Controller
gibt den Fehler zwar zurück, aber die Editor-Callbacks verwerfen dieses
Ergebnis. Es gibt weder einen Wiederholungsversuch noch eine gesonderte
Release-/Panik-Übergabe. Die 500-ms-Frist schützt nur vor alten Anschlägen;
sie beendet keine bereits gehaltene Note.

**Nachweis:** Über die regulären `IMessage`-Nachrichten zunächst Note 60
anschlagen und einen Audio-Block verarbeiten. Danach vor dem nächsten Block
32 Paare aus Loslassen und Anschlagen senden, also 64 Ereignisse. Die
anschließende Freigabe der letzten Taste wird abgewiesen. Nach einer Sekunde
Audio bleibt die letzte Note gehalten; auch im MIDI-Ausgang fehlt ihr Off.

```text
KEYBOARD_OVERFLOW release_result=1 gate=1 held=1 note=66
MIDI-Ausgang endet mit: ... F65 N66
```

Eine erneut gesendete Freigabe nach dem Leeren der Queue schließt das Gate.
Damit ist der gemeldete Befund von Commit `7901899` auch auf `c3c3623`
bestätigt. Es ist kein ausschließlich optisch hängenbleibender Tastenzustand.

**Vorschlag:** Releases unabhängig von der begrenzten Anschlag-Queue
zuverlässig übergeben, beispielsweise durch einen atomaren Release-/Panik-
Mechanismus mit klarer Reihenfolge zur Queue. Er muss interne Stimme und
bereits ausgegebene MIDI-Noten freigeben. Keine wartende oder sperrende
Fehlerbehandlung im Audio-Thread. Den beschriebenen Überlastfall durch die
Controller-/Prozessor-Strecke testen.

### EL-R02 — P2: Abgewiesene MIDI-Note-Offs werden als gesendet verbucht

**Fundstelle:** `source/processor.cpp:172–193`, insbesondere die Änderung
von `held_keys_` vor dem ungeprüften `output->addEvent(event)`.

Die Ereignisliste des Hosts kann voll sein. `SendKey()` ignoriert deren
Fehlerwert und löscht beim Loslassen den Eintrag in `held_keys_` bereits
vor dem Ausgabeversuch. Nach einer abgewiesenen Freigabe glaubt das Plugin
deshalb, der Host halte die Note nicht mehr. Spätere Blöcke und die
Reaktivierungsbehandlung können die Freigabe nicht nachholen.

**Nachweis:** Den SDK-Testhost mit `EventList(1)` als Ausgangsliste betreiben.
Vor einem Block `N60`, danach `F60` in die Prozessor-Queue geben. Beides
wird intern angenommen. In diesem Block passt nur das Note-On in die
Host-Liste. Nach einer Sekunde mit weiteren Blöcken lautet das Ergebnis:

```text
HOST_OUTPUT_OVERFLOW gate=0 held=0 midi=N60
```

Die eigene Stimme ist korrekt aus, der Empfänger am MIDI-Ausgang erhält
kein Note-Off und kann dauerhaft weiterklingen. `EventList::addEvent()`
liefert bei voller Liste ausdrücklich `kResultFalse`; der Nachweis benötigt
keinen fehlerhaften Host. Der Mechanismus wurde aus Torus übernommen und
ist auch für Mendelejews zugesagten MIDI-Ausgang relevant.

**Vorschlag:** Erfolgreich ausgegebene Noten getrennt vom internen
Tastenzustand führen. Ausstehende Freigaben erst nach erfolgreichem
`addEvent()` löschen und in folgenden Blöcken vor neuen Anschlägen
wiederholen. Auch eine vorübergehend fehlende Ausgangsliste und Releases
bei Reaktivierung berücksichtigen. Kleine/volle Host-Listen testen.

### EL-R03 — P2: Hinter dem Engine-Limiter überschreitet das Plugin ±1

**Fundstellen:** `engine/engine.cpp:367–370`,
`adapter/stream_adapter.cpp:203–210`, `source/processor.cpp:265–300`.

Die Engine begrenzt ihre Samples vor dem Sinc-Wandler. Dessen negative
Koeffizienten können neue Spitzen erzeugen. Der Adapter und die abschließende
Kopie in die Host-Puffer begrenzen diese nicht mehr. Die Aussage einer
Begrenzung auf ±1 gilt damit nur am Engine-Ausgang.

**Nachweis über die vollständige Plugin-Schnittstelle:** Host 44100 Hz,
gültigen Zustand der Version 1 mit folgenden 19 Werten für IDs 100–118
laden; Note-On 78, Velocity 127, bei Sample 0; 19200 Samples in Blöcken von
48 Samples verarbeiten:

```text
1, 1, 0, 1, 0, 0, 0, 0.923, 1, 0.5, 0.5, 0, 1, 1, 1, 0, 0, 1, 1

PLUGIN_OUTPUT_BOUND host=44100 peak=1.23976874 allocations=0
```

Die Werte kommen direkt aus `Processor::process()`, nicht aus einer
Frequenzganganalyse. Die normale Grenzwert-Assertion im kopierten Testhost
wurde für diese Messung durch eine Spitzenmessung ersetzt; die Prüfung auf
endliche Samples blieb aktiv. Ein zusätzlicher Engine-/Adapter-Lauf mit
300 Eckwert-Patches pro Hostrate fand bei 96 kHz Hostrate ebenfalls eine
Spitze von `1.22007990`; die Engine lief auch dabei intern mit 48 kHz.
Kein nicht endliches Sample und kein DSP-Neustart in diesen Läufen.

**Vorschlag:** Die zugesagte Grenze an der letzten Ausgangsstufe hinter
der Abtastratenwandlung sicherstellen. Die klangliche Entscheidung zwischen
Reserve und Begrenzung gehört zur Implementierung. Tests mit den obigen
Werten an umgerechneten Hostraten ergänzen; typische Zufallspegel reichen
hier nicht aus.

### EL-R04 — P3: Die Moden-Umrechnung erhält den Ausklang nur näherungsweise

**Fundstellen:** `core/elements/dsp/resonator.cc:117–130`,
`core/stmlib/dsp/filter.h:214–244`, `tests/test_dsp.cpp:533–591`;
die uneingeschränkte Beschreibung gleicher Moden in `UPSTREAM.md`.

Die Herleitung mit `sin(Winkel)/(2 Q)` ist eine Näherung für hohe Güten.
Für den tatsächlich verwendeten SVF und komplexe Pole gilt mit
`theta = 2 atan(g)` und `x = sin(theta)/(2 Q)`:

```text
rho² = (1 - x) / (1 + x)
Abklingkonstante pro Sekunde = -fs * log(rho) = fs * atanh(x)
```

Das implementierte `k` stellt `fs * x` gleich, nicht `fs * atanh(x)`.
Bei kleinen Güten ist der Unterschied messbar. Auch der Winkel des
gedämpften Pols hängt von Q ab. Die Abbildung mit `atan(g)` erhält die
Bandpass-Mittenfrequenz, aber nicht allgemein diesen Polwinkel.

**Nachweis:** Erreichbarer Randfall: MIDI 87, Geometry 0, Brightness 0,
Damping 0, Mode 32 (Index 31). Hier sind die Eingangsfrequenz der Mode
ungefähr `0.2504924988` relativ zu 32 kHz und Q praktisch 1. Einen einzelnen echten
`stmlib::Svf` mit den von `ComputeFilters()` berechneten Koeffizienten
anregen; die Pole aus den Impulsantwort-Samples 1–4 bestimmen:

| | Original-Koeffizienten bei 32 kHz | Umgerechnet bei 48 kHz |
| --- | ---: | ---: |
| g | 1.00083756 | 0.57772243 |
| Q | 1 | 1.29945695 |
| Polfrequenz | 8004.923 Hz | 7734.252 Hz |
| T60 aus dem Polradius | 0.392982 ms | 0.415241 ms |

Der Ausklang ist hier um 5.66 % länger. Das ist eine sehr kurze und breit
gedämpfte obere Mode: **kein Nachweis einer hörbaren Verstimmung des
Instruments und keine Widerlegung der gemessenen Grundtonhöhen.** Die
Bandpass-Mittenfrequenz ist von der hier angegebenen Polfrequenz zu
unterscheiden. Der Befund betrifft die allgemeine Behauptung einer exakten
Umrechnung und die fehlende Prüfung des niedrigen-Q-Randfalls.

**Vorschlag:** Die Näherung und ihren Gültigkeitsbereich dokumentieren oder
die Abbildung anhand der gewünschten exakten Pole/Abklingkonstanten
herleiten. Niedrige Güten und obere Moden separat prüfen. Die bestehenden
neun Ausklangmessungen bei Brightness 0.8 und Damping 0.25/0.5/0.75 können
diese Randfälle nicht absichern.

### EL-R05 — P3: Im ersten Quantum bleiben einige Startfilter unskaliert

**Fundstellen:** `core/elements/dsp/resonator.cc:49–52, 82–85, 99–101`,
`core/stmlib/dsp/filter.h:181–185`.

`Resonator::Init()` ruft für alle normalen Moden `Svf::Init()` auf und setzt
`mode_gain_` auf 1. Der SVF initialisiert unabhängig von der Abtastrate einen
Filter bei normierter Frequenz 0.01 und Q 100. Beim ersten `ComputeFilters()`
wird der Zähler auf 1 erhöht. Moden mit geradem Index über 24 bleiben bis
zum zweiten Quantum auf diesen Startwerten. Ihre `mode_gain_` bleibt dabei
ebenfalls 1.

Das übernimmt das Startverhalten des Originals in Samples, nicht in Zeit:
Die temporären Filter liegen bei ungefähr 320 Hz im Original, bei 480 Hz
im 48-kHz-Port. Bei einer Note im ersten Quantum nach Init/Reset werden sie
angeregt; beim zweiten Quantum ändern sich ihre Koeffizienten mit bereits
abweichender Filterhistorie. Das ist eine zusätzliche Anfangsabweichung
neben der dokumentierten anfänglichen Strength-Glättung.

**Nachweis:** Zwei frische Resonatoren je Rate mit Auflösung 26 bzw. 27,
`frequency=0.002`, Geometry 0, Position 0, Bow-Stärke 0; ein Impuls bei Sample
0. Die Differenz ihrer Center-Ausgänge isoliert Mode 27 (Index 26). Sie
entspricht im gesamten ersten Quantum einem unveränderten `Svf::Init()`:

```text
32000 Hz: maximale Sampleabweichung 1.08e-8
48000 Hz: maximale Sampleabweichung 1.68e-8
erste zwei Differenzsamples bei beiden Raten: 0.00392334, 0.00782871
```

Gleiche Samplefolge bei unterschiedlicher Rate bestätigt hier die
unskalierte Initialisierung. Der 32-kHz-Bitvergleich kann das nicht finden;
der Vorlauf von 20 Blöcken in `Play()` entfernt es aus den 48-kHz-Messungen.
Die Hörbarkeit dieser kurzen Anfangsabweichung wurde nicht geprüft.

**Vorschlag:** Startfilter und zugehörige Verstärkungen bei anderen Raten
konsistent umrechnen oder vor dem ersten hörbaren Quantum vollständig
vorbereiten. Den 32-kHz-Referenzpfad beibehalten. Eine Reproduktion ohne
Vorlauf und nach dem Laden eines Zustands ergänzen.

## Eigene Build- und Testergebnisse

Konfiguration ohne Änderung der Projekt-CMake-Dateien:

```powershell
cmake -S . -B .work/elements-review/build -G "Visual Studio 18 2026" -A x64
cmake --build .work/elements-review/build --config Release --parallel 8
ctest --test-dir .work/elements-review/build -C Release --output-on-failure
cmake --build .work/elements-review/build --config Debug --target Mendelejew test_elements_dsp test_elements_engine test_elements_plugin --parallel 8
ctest --test-dir .work/elements-review/build -C Debug -R '^elements\.' --output-on-failure
```

| Eigener Lauf | Ergebnis |
| --- | --- |
| Release-Build aller regulären Targets | PASS |
| Validator Mendelejew Release | 47 bestanden, 0 fehlgeschlagen |
| Release-CTest einschließlich bestehender Plugins | 10/10, 229.52 s |
| Debug-Build Mendelejew und seiner Testprogramme | PASS |
| Validator Mendelejew Debug | 47 bestanden, 0 fehlgeschlagen |
| Debug-CTest Elements | 4/4, 129.40 s |
| Zahlen der neu erzeugten Release-DSP-Messdatei gegen `DSP_MEASUREMENTS_0.1.0.md` | Alle Tabellen identisch; nur Einleitung/Überschrift verschieden |
| Unabhängiger Download und Hashvergleich der Originaldateien | PASS: 63 Manifest-Einträge, 47 unverändert und 16 geändert/abgeleitet |
| Zusätzliche Reproduktionen | EL-R01 bis EL-R05 wie oben |

Keine Compilerwarnung im regulären Elements-Produktcode. Der frische Build
meldet Warnungen in VSTGUI. Die zusätzlichen Review-Probes zeigten außerdem
die im Produktbuild unterdrückten Konvertierungswarnungen der Upstream-
Header; diese sind kein neuer Produktbefund.

## Prüfung der übrigen Auftragspunkte

### DSP, Messverfahren und Grenzen

* **Echte interne 48 kHz bestätigt.** `Engine::Prepare(48000)` und Quanten
  von 24 Samples; `Part`/Filter/Delays rechnen mit diesem Takt. Nur hinter
  der Engine wird gegebenenfalls zum Host gewandelt. Bei 48 kHz Hostrate
  ist der Adapter ein Direktpfad; der bestehende Vergleich gegen die
  direkt getriebene Engine besteht samplegenau. Der feste 48-kHz-Betrieb
  bei 96-kHz-Hosts ist der beauftragte bekannte HOLD-Umfang.
* **32-kHz-Bitvergleich bestätigt**, im Release-Lauf für 48 Szenarien mit
  zusammen 6 144 000 Kanal-Samples. Das beweist diese Sequenzen, nicht
  sämtliche möglichen Patches. Die Szenarien wechseln ihre Zufallspatches
  einmal und enthalten Gate-, Pitch-, Strength- und Freeze-Verläufe. Es
  gibt keine explizite vollständige Zweigabdeckung, keinen erzwungenen
  Panik-/Bypass-Fall und keine systematische Grenzprüfung aller
  Modellübergänge. Der separat vorhandene NOISE-Erreger ist über die
  normale `Part`-Konfiguration nicht ausgewählt. Die von `Part` benutzten
  FLOW-/Granular-/Strike-Modelle und die Röhre sind im Codevergleich und
  den zugehörigen Tests berücksichtigt.
* **Moden:** `mode_gain_` wird bei regulären Updates zusammen mit den
  Koeffizienten gesetzt und bleibt in ausgelassenen Quanten bei seinem
  passenden alten Wert. Keine neue Verstärkung mit alten Koeffizienten
  gefunden. Für den Start gilt EL-R05; für niedrige Q gilt EL-R04.
* **Bowed-Moden:** Die Rundung ist keine exakte Zeitabbildung. Bei 48 kHz
  beträgt ihr zusätzlicher Fehler höchstens 0.5 Sample, also 10.42 µs;
  bei kurzen Perioden kann das im Verhältnis groß sein. Die acht gemittelten
  Rauschläufe sichern weder einzelne Tonhöhen noch alle Geometry-Stellungen
  ab. Kein darüber hinaus eigenständig nachgewiesener Fehler; die bekannte
  Streuung wird nicht als neuer Defekt gemeldet.
* **Matched-Low-Pass:** Alle 257 gespeicherten Koeffizientensätze selbst
  ausgewertet. Größter Polradius ungefähr 0.995827 bei 48 kHz und 0.998008
  bei 96 kHz, somit statisch stabil. Auch der Wechsel vom positiven zum
  negativen Originalpol ist berücksichtigt. Die vorhandene unabhängige
  Frequenzgangauswertung reproduziert die Testzahlen von 1.23/0.28 dB und
  0.60/0.23 dB zusätzlichem Nachbarschritt im Testbereich 100 Hz–14 kHz.
  Der Fit ist eine begrenzte Koordinatensuche, kein Beweis eines eindeutigen
  globalen Minimums.
* Ein dichterer eigener Frequenzraster zeigt, weshalb die Messbereichsgrenze
  bei Aussagen beibehalten werden muss: Bei 48 kHz, Tabellenindex 227 und
  14950 Hz beträgt die Abweichung beider Filtersektionen zusammen etwa
  1.68 dB, bei einem Originalpegel von -39.74 dB. Die 1.3-dB-Zahl darf
  daher nur mit dem tatsächlich geprüften Frequenzbereich verwendet
  werden. Dies ist eine Präzisierung der Messabdeckung; daraus wird kein
  zusätzlicher hörbarer Klangfehler abgeleitet.
* Alle sieben einzelnen Exciter-Modelle zusätzlich jeweils 4000 Blöcke
  mit abwechselnden Timbre-Werten 0 und 0.995 bei 48/96 kHz betrieben:
  keine nicht endlichen Werte. Das ersetzt weder einen Beweis der
  Stabilität aller zeitvarianten Koeffizientenfolgen noch einen Hörtest
  auf Klicks. Die Engine glättet normale Potiänderungen zusätzlich.
* **Röhre:** Der History-Zugriff bildet bei 48 kHz den Wert von vor
  1.5 Samples durch lineare Interpolation ab, bei 96 kHz von vor 3 Samples;
  der zusätzliche ganzzahlige History-Schritt ist berücksichtigt.
  Die Interpolation besitzt erwartungsgemäß einen eigenen Frequenzgang.
* **FX-Puffer:** Längen, Rundung, zusätzliche Trennpositionen, modulierte
  Offsets und Interpolationszugriffe durchgesehen. Bei 48 kHz braucht das
  Reverb 32437 Positionen und verwendet 32768; bei 96 kHz 65536. Ein um
  eins verkleinerter 32768er Puffer wird abgewiesen; bei 96 kHz auch der
  entsprechend zu kleine Puffer. `Part::Init()`/`Reverb::Init()` melden
  Fehler; ein danach unbenutzbarer Part ist dokumentiert. Für die
  unterstützten Raten passen auch die Diffuser- und Tube-Puffer.
* **Exciter-Konstanten:** Original und Port für Phase-Inkremente,
  Restart-/Flip-Wahrscheinlichkeiten, Weißrauschdichte, Plectrum-Delay und
  dessen Decays, Partikelintervalle sowie Meter durchgesehen. Die übrigen
  Werte 0.95, Envelope-Schritte und Gate-Damping sind pro Block bzw.
  pro Partikelereignis und benötigen keine weitere Samplerate-Skalierung.
  Keine weitere vergessene Per-Sample-Konstante nachgewiesen.
* Die vorhandenen Spektralbänder sind geeignet, grobe Pegeländerungen zu
  erkennen. Sie beweisen keine Gleichheit jeder Partialschwingung. Die
  automatischen Grenzwerte sind weiter als die Berichtszahlen; schwache
  Bänder und viele hohe Bänder werden nur protokolliert. Die Ausklangtests
  prüfen drei Noten und drei Damping-Werte bei fester heller Einstellung.
  Tiefe/extrem hohe Noten, niedrige Q, Geometry-Ränder, sehr lange Decays,
  erste Quanten ohne Vorlauf und systematische Modellübergänge bleiben als
  weitergehende Klanggleichheitsprüfung offen.

### Engine, Threads, Warteschlangen und Zustand

* Note-On/Off im selben Quantum, Retrigger mit Gate-Lücke, Last-Note-
  Priority, mehr als 16 gehaltene Noten und Freigabe vergessener Tasten
  durchgesehen; die entsprechenden vorhandenen Tests bestehen. Ein kurzer
  Anschlag bekommt absichtlich mindestens ein Quantum Gate, auch wenn
  sein Note-Off bereits im selben Quantum ankommt.
* Die Poti-Glättung und BIND-Grenzen gegen die unabhängig geladene
  Originaldatei `elements/cv_scaler.cc` verglichen: 0.01 erster Tiefpass,
  0.05 für Meta/Geometry, 0.01 für Position/Space, obere Grenzen
  0.9995/0.995. Freeze schiebt Space auf 2.0; der Kern friert ab 1.75 ein.
  Coarse/Fine und Velocity sind die dokumentierte Keyboard-Abbildung.
* Im geprüften `process()`-Pfad kein eigener Mutex, Dateizugriff oder
  Allokationsaufruf. Die vorhandenen Allokationsprüfungen für Render,
  Reset, Guard und Plugin bestehen; auch EL-R03 benötigt keine
  Audio-Thread-Allokation. Fremder Hostcode wird damit nicht garantiert.
* Der Nicht-Endlich-Guard prüft beide DSP-Kanäle, verwirft das Quantum,
  initialisiert den DSP und schließt die Tasten. Der vorhandene injizierte
  Fehlerfall und erneutes Spielen danach bestehen. Das ist keine
  Garantie gegen jeden denkbaren numerischen Fehler.
* `SampleRate::Init()` überspringt die Tabellensuche bei gleicher Rate
  anhand des Instanzfeldes `hz_`. Der Part wird beim Reset nicht neu
  konstruiert. Somit erneuert ein laufender Reset keine Fit-Tabellen.
  Er löscht weiterhin Filter-, Delay- und Reverb-Historien. Eigene 200
  Release-Resets bei 48 kHz: Median ungefähr 3.7 µs, Maximum unter 10 µs
  auf dieser Maschine. Kein allgemeines Echtzeit-Zeitbudget daraus ableiten.
* **Andere Queues:** Die 1024er Adapter-Notenqueue ersetzt bei Überlauf
  eine Freigabe durch `AllNotesOff`; eigene Reproduktion bestätigt
  `gate=0`, `held=0`, `DroppedNotes=1`. Die 256er Control-/Bend-Queues
  behalten bei Überlauf den neuesten Wert zu dessen Zeit. Sie können keine
  Note-Offs verlieren, da sie solche nicht transportieren. Die Zustands-
  Übergabe ist ein Drei-Slot-`LatestValue` mit vollständigen Snapshots,
  keine Ereignisqueue. Ihre Ersetzungsregel passt zu Preset-Ladevorgängen.
  In der Fenster-/Controller-Strecke gibt es keine zusätzliche zuverlässige
  Release-Sicherung; siehe EL-R01. Beim Host-Ausgang gilt EL-R02.
* **Status:** `setActive()` und `IConnectionPoint::notify()` sind laut den
  mitgelieferten SDK-Interfaces UI-Thread-Aufrufe. Die Statusnachricht
  wird daher nicht aus `process()` verschickt. Fehlende Nachrichtenobjekte
  und ein Controller ohne Fenster sind abgefangen; letzterer speichert
  den Status für ein späteres Fenster. Keine DAW-spezifische Threadprüfung.
* Controllerparameter, neue Textkonvertierungen und MIDI-Zuordnung
  durchgesehen; bestehende Tests bestehen. Die Factory, Queue-, State-
  und Editor-Änderungen zusätzlich gegen Torus 0.2.0 (`f275beb`) verglichen.
  Keine weitere konkret nachgewiesene Kopierabweichung.
* **Zustand:** Version 1 ist 4 Byte Version plus 19 Little-Endian-Doubles,
  insgesamt 156 Byte. Unbekannte Version, verkürzte Nutzdaten,
  NaN/Inf und Werte außerhalb 0–1 werden vor Veröffentlichung abgewiesen.
  Coarse/Freeze werden absichtlich quantisiert; Pitch Bend wird zentriert.
  Der Test bestätigt die vollständige Ablehnung der geprüften beschädigten
  Zustände. Zusätzliche Endbytes werden nicht geprüft. Ein beliebiger
  Bitfehler, der wieder einen gültigen Wert ergibt, ist ohne Prüfsumme nicht
  erkennbar. Für zukünftige Versionen ist eine explizite Migration nötig;
  der heutige Reader lehnt neue Versionen korrekt ab.

### Name, Herkunft, Lizenz und bestehende Plugins

* **Umbenennung bestanden:** Factory-Name `Mendelejew`, Controllername
  `Mendelejew Controller`, Vendor `TAUREON`, Bundle und Binärdatei
  `Mendelejew.vst3`. Das selbst erzeugte Release-`moduleinfo.json`
  enthält diese Namen und Version 0.1.0. Der sichtbare Titel im Editorcode
  verwendet `stringPluginName`. Keine Anzeige von `Elements` oder
  `Mutable Instruments` in den geprüften Plugin-Namen/Labels gefunden.
  Die verbleibenden Namespaces, IMessage-IDs und Target-/Projektbezeichner
  sind intern. Kein Fenster geöffnet und keine Host-eigene Titelleiste
  begutachtet.
* Das README erklärt Herkunft und fehlende Unterstützung durch Mutable
  Instruments. Das entspricht einer sachlichen Herkunftsangabe statt eines
  Produktnamens. Die öffentlich dokumentierten Namensrichtlinien wurden
  am [gepinnten eurorack-README](https://github.com/pichenettes/eurorack/blob/f4cfa7697095a28d292bff758beb51deb49b5a86/README.md)
  gegengeprüft. Der Hinweis ersetzt nicht die beizulegenden Lizenztexte;
  diese sind vorhanden und im Staging-Skript aufgeführt. Eine Prüfung der
  rechtlichen Verfügbarkeit von „Mendelejew“ war nicht Gegenstand.
* Die Manifest-Hashes wurden unabhängig gegen die 35 unterschiedlichen
  dort benannten Originaldateien geprüft; mit README und `cv_scaler.cc`
  wurden 37 Remote-Dateien geladen. Revisionen:
  eurorack `f4cfa7697095a28d292bff758beb51deb49b5a86`,
  stmlib `e3bd7c9cc00e4364166f9905c0509b6ffd0535ec`.
  Alle 47 als unverändert bezeichneten lokalen Dateien stimmen
  LF-normalisiert mit den Originalbytes überein, darunter die 34
  Referenzdateien. Die 15 geänderten DSP-Dateien tragen Änderungsvermerke
  und erhaltene MIT-Hinweise; `rng.h` ist separat als abgeleitet markiert.
  Die drei neuen Samplerate-/Filterdateien tragen eigene MIT-Hinweise.
* Keine belastbaren Anzeichen einer Übernahme des GPL-spezifischen
  Wrappercodes gefunden. Die geprüften Unterschiede zum MIT-Original
  betreffen die dokumentierten Änderungen. Zum Gegenlesen dienten auch
  der [VCV-Wrapper](https://github.com/VCVRack/AudibleInstruments/blob/v2/src/Elements.cpp)
  und [MAGDAs Wrapper](https://github.com/Conceptual-Machines/magda-core/blob/a593628073e15d0fcb670937f9467246796fdc91/magda/daw/audio/plugins/mutable/MutableElementsPlugin.cpp).
  Deren Architektur und spezifischer Framework-/Wandlercode finden sich
  hier nicht. Allgemeine Nullinitialisierung und eine übliche monophone
  Tastenliste beweisen für sich keine Codeübernahme. Die Entstehungsgeschichte
  lässt sich durch einen Quellvergleich allein nicht lückenlos beweisen.
  Die MIT-Zuordnung der benutzten stmlib-Dateien stimmt mit dessen
  [Original-Lizenz](https://github.com/pichenettes/stmlib/blob/e3bd7c9cc00e4364166f9905c0509b6ffd0535ec/LICENSE)
  und den jeweiligen Dateihinweisen überein.
* `git diff torus-classic-0.1.0...HEAD -- instruments/toy-piano-classic instruments/torus-classic`
  ist leer. Außerhalb Elements beschränkt sich die Buildänderung auf
  dessen `add_subdirectory`; weitere Änderungen sind Dokumentation und
  Third-Party-Hinweise. Die sechs vorhandenen Regressionstests bestehen.

## Abschluss und offene Freigaben

EL-R01 bis EL-R03 sollten vor dem Merge behoben und gezielt nachgetestet
werden. Für EL-R04/EL-R05 sind mindestens die Aussagen zur vollständigen
Klanggleichheit zu präzisieren und die gewünschte Behandlung festzulegen.
Die bisherigen Messzahlen sind reproduziert; sie reichen für einen
uneingeschränkten Nachweis aller Patches und Anfangszustände nicht aus.

Ungeprüft bleiben Aussehen/Bedienung, reale DAWs und Hören wie beauftragt;
außerdem keine zusätzliche ASan-/TSan- oder zweite Toolchain-Prüfung.
96 kHz interner Pluginbetrieb bleibt der bereits bekannte HOLD-Umfang.
Es wurden keine Fehler behoben, keine Kommentare an GitHub gesendet und
kein Merge vorgenommen. Die einzige neue Ergebnisdatei dieses Reviews ist
dieses Dokument.

Die abschließende Entfernung von `.work/elements-review/` wurde von der
automatischen Freigabeprüfung mit „blocked by policy“ abgewiesen, ohne
weitere Begründung. Die temporären Builds, Quellen und Prüfarbeitsdateien
liegen deshalb weiterhin ausschließlich in diesem ignorierten Verzeichnis.
