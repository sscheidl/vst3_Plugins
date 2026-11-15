# Codex Review: Torus Classic 0.2.0

VERDICT: FREIGABE MIT KORREKTUREN

Geprüft: `78e5b9a8d80e24e0deec3d80a1d3d4818e28102c`, 2026-11-14,
Windows x64, Visual Studio 2026 Build Tools, MSVC 19.51.36256.0, Release.
Der Produktcode von 0.2.0 stammt aus `f275beb`; der zusätzliche Commit
`78e5b9a` enthält nur den ursprünglichen Buildauftrag.

SHA-256 von `instruments/torus-classic/components.json`, vor und nach den
Prüfungen identisch:

```text
0BEC8F765F443D8BF08BC1AC1F4E38E2E339291707E293C8B122D7BC2D989E6F
```

Zu Beginn war `git status --short --ignore-submodules` leer. Im Verlauf
erschien der fremde unversionierte Pfad `instruments/elements/` und zum
Abschluss eine fremde Änderung der Wurzel-`CMakeLists.txt`; beide wurden
nicht verändert. Elements wurde nicht untersucht. Die normalen Submodul-Metadaten wurden
nicht erneut vollständig geprüft. Kein Branchwechsel, Commit oder Push.

Alle vier Befunde aus 0.1.0 sind im geprüften Umfang behoben. Neu bestätigt
sind ein wiederkehrender Filterzustand nach Bypass sowie mehrere begrenzte
Robustheitsfälle. **Kein neuer P0-Befund.** Der Bericht ist keine allgemeine
Release-Freigabe. GUI-, DAW- und Hörtests führt der Benutzer durch; sie wurden
hier ausdrücklich nicht ausgeführt. Ein kurzer Signalrest ist technisch
gemessen, seine Wahrnehmbarkeit wurde nicht durch Abhören bewertet.

## Nachprüfung TC-R01 bis TC-R04

Pfade ohne Präfix sind relativ zu `instruments/torus-classic/`.

| ID | Ergebnis | Beleg | Rest |
| --- | --- | --- | --- |
| TC-R01 | **behoben** | `source/processor.cpp:98`, `:141`, `:324`; `source/parameter_state.h:53`, `:68`, `:105`; `TestParameterState`, `TestState` fensterlos erneut bestanden | `Restart` bekommt genau den lokalen Wertesatz und liest die State-Atomics nicht erneut. Während 1 236 819 State-Ladevorgängen sah die Sonde in 99 547 beobachteten Ein-Sample-Blöcken keine Mischung; beide Zustände wurden tatsächlich beobachtet. Dies ist ein Belastungstest plus Codeprüfung, kein erschöpfender Nebenläufigkeitsbeweis. |
| TC-R02 | **behoben** | `source/editor.h:160`, `source/editor.cpp:377`; SDK `ccontrol.cpp:186`, `:204`; eigene In-Memory-Control-Sonde | Der Listener leitet Begin/End nicht mehr zusätzlich weiter. Ein realer `CKnob` mit Frame-Anbindung meldete auch bei zweimal verschachteltem `beginEdit/endEdit` genau `begin, perform, end`. Kein natives Fenster oder echter Mausgriff wurde verwendet. |
| TC-R03 | **behoben** | `adapter/stream_adapter.cpp:102`, `:126`, `:138`, `:142`, `:164`; `tests/test_adapter.cpp:455` erneut bestanden | Getrennte Queues vermeiden das frühere unmittelbare Anwenden. Volle Control-Queue ersetzt den letzten Punkt mit dessen neuem Zeitpunkt; Noten werden bei Überlast verworfen. Die Reparatur garantiert keine verlustfreie Verarbeitung beliebig vieler Punkte. Die endlichen Timing-/Blocktests bestehen. |
| TC-R04 | **behoben** | `source/editor.cpp:350`; SDK `ccontrol.cpp:204`, `:369`; eigene fensterlose Close-Sonden | Vor dem View-Abbau werden verschachtelte Edits zu Ende geführt. Gemessen: `begin,end` bei Schließen eines zweifach begonnenen Edits; `begin,perform,end` bei Schließen während des Wheel-Nachlaufs. Keine zusätzliche Host-End-Meldung durch den Control-Abbau gefunden. Native `removed`-/Fensterabläufe bleiben ungetestet. |

### Einordnung der State-Synchronisation

`Load` und `Current` serialisieren sich außerhalb des Audiopfads über den
Mutex. Die State-Basis und Generationsnummer können dadurch nicht während
`Current` gegeneinander ausgetauscht werden. `process` benutzt weder diesen
Mutex noch `Current`, sondern den vollständig übernommenen `Loaded`-Wert.
Automation wird mit der zuletzt vom Audio-Thread übernommenen Generation
geschrieben; ältere Generationen überlagern eine neuere Basis nicht.

Der Sequenz-Lock benutzt atomare Nutzdaten, also keine konkurrierenden
nichtatomaren Lese-/Schreibzugriffe. Liest ein Acquire-Nutzdatenzugriff einen
neueren Release-Wert, liegt auch die davor geschriebene ungerade Sequenz vor
dem abschließenden Sequenzzugriff; dieser kann dann nicht erfolgreich die
alte gerade Sequenz bestätigen. Liest er die neue abschließende gerade
Sequenz, werden deren vorherige Nutzdatenschreibvorgänge übernommen. Unter
dem Vertrag **ein Slot-Schreiber** wurde kein Fehler der Speicherordnungen
gefunden. Die Retry-Schleife liegt auf der Nicht-Audio-Seite.

`Current` ist keine atomare Transaktion über alle Automationsparameter eines
laufenden Blocks: jeder einzelne aktuelle Automationswert kann zu einem
anderen Lesezeitpunkt stammen. Er gehört aber zur festgehaltenen geladenen
Generation. Das ist eine andere Aussage als der frühere Mischzustand aus
zwei geladenen Presets.

Zwei parallele Aufrufer von `Write` sind nicht unterstützt: Sequenznummern
könnten kollidieren, und auch die Audio-seitige Generationsverwaltung wäre
nicht für konkurrierende `Adopt`-Aufrufe geschützt. Der Processor bietet
diesen Ablauf bei regelkonformem, serialisiertem `process` und Lifecycle
nicht an. Der Mutex auf der Leserseite würde mehrere Slot-Schreiber nicht
nachträglich sicher machen.

Aktivierung nimmt wartende Generationen an und startet aus `Current`.
Ein währenddessen noch neuer geladener Zustand bleibt zur nächsten
Übernahme vorgemerkt. Die vorhandenen Tests decken den inaktiven Ladepfad
und veraltete Automation ab; sie erzwingen nicht jede Unterbrechung innerhalb
eines einzelnen atomaren Slot-Zugriffs. Kein Rest des früheren TC-R01 gefunden.

## Neue Befunde

| ID | Schwere | Bereich | Befund | Beleg (Datei:Zeile) | Auslösung und Auswirkung | bewiesen/vermutet | minimale Korrektur |
| --- | --- | --- | --- | --- | --- | --- | --- |
| TC-R05 | **P2** | Filter-Glättung | Bei hohen Raten bleibt der Filterwert vor dem Ziel stehen. Nach Rückkehr auf „off“ wird der Bypass nicht exakt erreicht. | `adapter/output_stage.h:62`, `:64`; `adapter/output_stage.cpp:40`; `tests/test_adapter.cpp:746` | Nach `.2 → 1` bleibt der Wert bei 96 kHz auf `0.999985695`, bei 192 kHz auf `0.999971390`; eine weitere Sekunde ändert ihn nicht. Dadurch verbleiben etwa 0,0715 % bzw. 0,1431 % Filteranteil und laufende Koeffizientenberechnung. Der vorhandene Endpunkttest benutzt nur 48 kHz. | **Bewiesen.** Ein hörbarer Fehler bei den üblichen hohen Raten ist nicht nachgewiesen; die Abweichung ist sehr klein. | Wie bei der reparierten Toy-Piano-Glättung auch `next == current` als Anlass zum exakten Ziel-Snap behandeln, oder genauer akkumulieren. An allen unterstützten Raten in beide Richtungen prüfen. |
| TC-R06 | **P1** | Filterzustand nach Bypass | Bei `mix_ == 0` stehen die Integratoren still. Beim späteren Schließen des Filters wird der lange alte Zustand wieder ausgegeben. | `adapter/output_stage.h:68`, `:75`; `adapter/output_stage.cpp:37` | 48 kHz, gefiltertes Signal, Filter regulär öffnen, danach zehn Sekunden Stille im vollständigen Bypass, Filter wieder schließen. Trotz weiterhin ausschließlich null am Eingang entsteht ein Signalrest. Mit 1-kHz-Sinus vorher: Peak `0.0102930339` = −39,75 dBFS. | **Bewiesen** mit unveränderter Release-Bibliothek und normalen `SetFilter`-Übergängen. Hörprüfung nicht durchgeführt. | Integratoren auch bei ausgeblendetem Filter weiterführen oder den Zustand beim vollständigen Bypass/erneuten Einschalten definiert zurücksetzen und den Übergang passend behandeln. Kein altes Signal nach langer Stille wieder freigeben. |
| TC-R07 | **P2** | Tune an Rundungsgrenzen | Anzeige und Klang runden unterschiedliche Präzisionen: Controller in Double, Bridge nach vorheriger Float-Konvertierung. Nahe einer Halbton-Schwelle kann das unterschiedliche ganze Halbtöne ergeben. | `source/controller.cpp:95`; `source/processor.cpp:109`, `:165`; `source/engine_bridge.h:30`, `:48`, `:55`; `source/parameters.h:114` | Gültiger Normalwert `0.51041666249999995` entspricht `+0.4999998 st`. Anzeige bei FREE aus: `0`; Engine: ungefähr `+1 st`. Bei `0.48958332916666669`: Anzeige `−1`, Engine ungefähr `0`. Die betroffenen Wertebereiche sind sehr schmal. | **Bewiesen** über Controller-Text und die echte Bridge/Engine. Kein praktischer DAW-Vorfall beobachtet. | Einen gemeinsamen kanonischen Rundungsweg mit gleicher Eingangspräzision für Anzeige und Engine verwenden; die Präzision nicht erst nach voneinander abweichenden Rundungsentscheidungen vereinheitlichen. Beide Seiten jeder Halbton-Schwelle testen. |
| TC-R08 | **P2** | MIDI-Ausgang / Rückstau | Note-offs sind an beiden Übergaben verlierbar. Nach erfolgreich ausgegebenem Note-on kann die empfangende MIDI-Stimme dadurch offen bleiben. | `source/processor.cpp:202`, `:223`, `:242`, `:383`; `source/controller.cpp:352`, `:373` | (a) Volle UI-Queue weist den Release zurück; der GUI-Callback ignoriert das Ergebnis und merkt keinen Retry vor. (b) `SendKey` löscht den Held-Status schon vor `output->addEvent`; dessen Fehlschlag wird ignoriert. Nach Wiederherstellung freien Platzes kommt kein Note-off nach. | **Bewiesen** mit künstlich voller UI-Queue und einer begrenzten Host-Eventliste. Keine CPU-Überlast oder hängende Note in der DAW beobachtet. | Neue Anschläge dürfen bei Überlast entfallen; Releases für bereits akzeptierte Note-ons müssen separat vorgemerkt/erneut zugestellt werden. Den Zustand tatsächlich an den Host zugestellter Noten erst nach erfolgreichem `addEvent` ändern; fehlende Liste und Rückstau explizit behandeln. |
| TC-R09 | **P2, defensiver Komponentenvertrag** | Limiter bei nicht endlichem Eingang | Die neue Ausgangsstufe macht NaN/Infinity nicht endlich. Infinity wird durch `gain = 0` und anschließendes `inf * 0` sogar zu NaN. | `adapter/output_stage.h:71`, `:80`, `:83`, `:88` | Direkte Eingabe NaN oder ±Infinity an `OutputStage::Process` liefert nicht endlichen Ausgang. Bei aktivem Filter kann ein nicht endlicher Eingang außerdem die Integratoren kontaminieren. | **Bewiesen an der Komponente. Kein regulärer VST3-Auslöser nachgewiesen:** der trockene Engine-Ausgang besitzt bereits einen Finite-Schutz und ist begrenzt; die bestehenden End-to-End-Tests bleiben endlich. | Finite-Voraussetzung ausdrücklich festlegen oder, für eine eigenständig robuste letzte Schutzstufe, nicht endliche Eingänge/Zustände gezielt abfangen und kontaminierte Filterzustände zurücksetzen. Nicht als derzeit nachgewiesenen Absturz-/Lautstärkefehler beim Spielen darstellen. |

### Reproduzierbare Einzelheiten und praktische Gewichtung

**TC-R05:** `Prepare(rate)`, `JumpFilter(.2f)`, `SetFilter(1.f)`, zwei Sekunden
Samples mit unverändertem endlichem Eingang, Wert ablesen, eine weitere
Sekunde verarbeiten. Ergebnisse:

| Rate | Endwert | Exakt „off“? |
| --- | --- | --- |
| 44.1 kHz | 1 | ja |
| 48 kHz | 1 | ja |
| 88.2 kHz | 0.999986887 | nein |
| 96 kHz | 0.999985695 | nein |
| 192 kHz | 0.999971390 | nein |
| 4 MHz, oberes dokumentiertes Extrem | 0.999403954 | nein |

Die Größenordnung der Pegelabweichung bei 20 kHz ist bei 96/192 kHz aus
Mix-Anteil und Butterworth-Übertragung etwa 0,006/0,012 dB. Das ist eine
Abschätzung, keine Hörmessung oder vollständige Frequenzgangmessung dieses
Restfehlers. Entsprechend dem Hinweis des Benutzers als P2 gewertet, nicht
als nachgewiesener hörbarer Hauptfehler. Kein blockierender Loop entsteht;
die unnötige Glättungs-/Koeffizientenarbeit läuft lediglich weiter.

**TC-R06:** `Prepare(48000)`, Filter zunächst `.5`, eine Sekunde 1-kHz-Sinus
mit Amplitude `.3`, danach `SetFilter(1)` und eine weitere Sekunde desselben
Signals. Der Wert ist dann exakt 1. Zehn Sekunden Nullen im Bypass liefern
exakt null. Nach `SetFilter(.5)` und ausschließlich weiteren Nullen entsteht
der oben gemessene Peak. Dasselbe mit Gleichanteil `.3` statt Sinus ergibt
`0.0115680462` = −38,73 dBFS. Das ist der gespeicherte Filterzustand, keine
neue Synthesenote und kein Resampler-Ausklang. Dieser Befund ist vom winzigen
Off-Restfehler TC-R05 unabhängig.

**TC-R08a:** Note-on 60 zunächst erfolgreich ausgeben. Dann 64 gültige
Keyboard-Nachrichten ohne Audio dazwischen annehmen; der anschließende
Release 60 wird von `notify` zurückgewiesen. Nach Leeren der Queue und einem
weiteren Audioblock: kein Note-off 60. Der Rückgabefehler wird in
`Controller::SendKey` zwar ermittelt, vom `createView`-Release-Callback aber
nicht benutzt. Die `held_keys_`-Markierung kann deshalb bestehen bleiben.

**TC-R08b:** Eine bereits an den Host ausgegebene Note 60 halten. In einem
Block eine weitere Note 61 vor dem Release 60 schicken und eine Host-Liste
mit Kapazität 1 anbieten: Note 61 wird akzeptiert, der Release 60 abgewiesen.
Nach Vergrößern der Liste und dem nächsten Block werden null Events
nachgeliefert, weil die Markierung für 60 schon gelöscht wurde. Die
SDK-`EventList::addEvent` gibt bei fehlendem Platz regulär `kResultFalse`
zurück; dies benötigt keine Protokollverletzung des Hosts.

Diese Fälle testen **Nachrichten-/Eventpuffer**, nicht die CPU-Sättigung.
Die geringe im Screenshot des Benutzers sichtbare CPU-Last widerspricht
ihnen nicht, ist aber ebenso wenig ein Nachweis, dass sie praktisch auftreten.
Bei gewöhnlicher Mausbedienung wurde die UI-Queue hier nicht gefüllt. Beide
Varianten bleiben P2-Robustheitsfälle unter erzwungener Knappheit.

## Ohne Befund geprüft

| Bereich | Ergebnis und Belegumfang |
| --- | --- |
| Control-Queue-Indizes und Überlast | `Last` wird nur bei `count > 0` oder voller vorbereiteter Queue gelesen. Bei voller Queue bleibt `count` unverändert, Ring-Indizes bleiben innerhalb der reservierten Vektoren. Zeitmonotonisierung kann ein unsortiertes Event verspäten, nicht vorziehen. Der vorhandene Überlasttest prüft die tatsächlichen Anwendungsquanten und den Vergleich mit blockweiser Zufuhr ohne Überlast; erneut bestanden. |
| Control-Reihenfolge vor Noten | `DryEngine::NoteOn` reiht nur den Anschlag ein. Erst `Synth::Render` übernimmt bis zu vier Anschläge und liest die dann aktuellen Controls. Tune und FREE setzen ebenfalls nur den finalen Zielwert vor dem nächsten Render. In dieser Engine wurde kein zusätzlicher Klangunterschied aus der HandleEvent-Reihenfolge innerhalb desselben Quantums gefunden. |
| Normales Timing und Blockunabhängigkeit | Der fensterlose `TestBlockSizes` rendert dieselbe absolute Folge einschließlich Filterautomation in 20 Varianten pro Rate (neun feste Blockgrößen und wechselnde Größen, 32/64 Bit), bei 44.1/48/88.2/96/192 kHz. Alle Varianten samplegleich. Außerhalb der ausdrücklich zugelassenen Überlastverluste kein neuer Blockfehler gefunden. |
| Filter-Ereigniszeit | `RenderQuantum` setzt den Filter-Zielwert, bevor `OutputStage` die nächsten Host-Samples verarbeitet. Wegen der bereits geprüften Render-/Latenzgrenze wird kein Event-Quantum für einen späteren Hostblock vorweggenommen. Das Filterziel wird ungefähr am Host-Ereigniszeitpunkt wirksam; es wartet nicht auf die Ausgabe desselben bereits verzögerten Synthesequantums. Dies entspricht der jetzigen Dokumentation. `TestOutputStage` prüft Ziel vor dem Ereignis unverändert und danach innerhalb Raster-plus-Latenz; bestanden. Keine erneute vollständige Latenzanalyse des unveränderten Resamplers. |
| Statischer Tiefpass | TPT-SVF mit positivem `g`, Q = 1/√2 und Grenzfrequenz höchstens 0.45 × Rate. Die vorhandenen 100-Hz-/1-/2-/4-kHz-Messungen bei 44.1/48/96 kHz bestehen. Kein konkreter instabiler Fall mit regulären endlichen Eingängen gefunden; keine erschöpfende Prüfung aller Raten und sämtlicher Koeffizientenverläufe. TC-R05/06 bleiben. |
| Limiter bei regulären endlichen Signalen | Offener Filter und Gain 1 lassen die Samples exakt durch. Gemeinsamer Gain schützt beide Kanäle; kein Look-ahead, keine zusätzliche Latenz. Die vorhandenen Bursts/Rechtecke bis Amplitude 4 und Release-Tests an allen fünf üblichen Raten bestehen: Ausgang ≤ 0.5, Verstärkung danach exakt 1. Wiederholter/fortdauernder Gleichanteil über der Decke wird jeweils begrenzt. Der Gain-Snap behandelt auch einen nicht mehr darstellbaren Fortschritt. Die erste leisere Probe nach einem lauten Signal bleibt während des Releases absichtlich abgesenkt; „unter der Decke unverändert“ gilt im unbelasteten/erholten Zustand. TC-R09 betrifft nicht endliche Eingänge. |
| Firmware-Vergleich | `TestAgainstFirmware` wurde unverändert ausgeführt: 5 760 000 Samples im 48-kHz-Vergleich gleich, wenn in beiden Pfaden Denormals geflusht werden; maximale Differenz zur ungeflushten Referenz `1.26e-44`. Volume wird begrenzt, der Pegel liegt tatsächlich unter 0.5, der Filter ist offen. Keine Gleichheit bei eingreifendem Limiter oder geschlossenem Filter daraus abgeleitet. Damping/Tune verwenden im Referenz-Wrapper dieselben Mapping-Helfer; dieser Test allein wäre deshalb kein unabhängiger Beweis ihrer Richtigkeit. Damping-Endpunkte und normale Tune-Stufen sind separat geprüft; Tune-Grenzen haben TC-R07. |
| Damping | `DirectControl` spiegelt genau einmal, sowohl für Restart als auch für Ereignisse. Die Engine selbst bleibt in der bisherigen Richtung. Version-1-State wird vor dieser Abbildung einmal in die neue Benutzer-Richtung überführt. Die alte Automations-/CC-Richtung wird bewusst nicht migriert; im Changelog dokumentiert. |
| State-Versionen und Reihenfolge | IDs 100–107, 109, 110 werden über `kSavedIndex` geschrieben/gelesen, Pitch Bend 108 bleibt transient. `Load`, `getState` und `setComponentState` verwenden dieselbe Reihenfolge. V1 ergänzt Filter off und bestimmt FREE anhand des alten Tune-Werts; Model und FREE werden auf ihre Stufe kanonisiert. Das Read-Result wird erst vollständig veröffentlicht. 157 beschädigte V2-/Versionsfälle und die bestehenden V1-Konversions-/Fehlerfälle erneut bestanden. Controller liest denselben Sound-State-Reader und zentriert Bend. |
| FREE und Tune | Zwei getrennte Events werden an der Quantum-Grenze kombiniert; `JumpTune` initialisiert beide Werte auf einmal. Modell-/Schalter-Flags und Step-Regeln sind stimmig. Der Host-Parameter Tune bleibt absichtlich kontinuierlich. Kein allgemeiner Fehler des FREE-Schalters; TC-R07 ist der konkrete Präzisions-Grenzfall. |
| Controller und MIDI-Mapping | Elf Parameter, die neuen IDs 109/110 angehängt, Filter kontinuierlich, FREE mit einem Schritt und `kIsList`. Strenge Parser für „off“, „on“, Zahlen/Hz und Fehlerfälle erneut bestanden. Zehn MIDI-Zuordnungen einschließlich CC74, alle gültigen Kanäle auf Bus 0; kein FREE-CC und keine ehemaligen Reverb-/Polyphony-CCs. |
| Normale MIDI-Ausgabe | Event-Bus 0, Kanal 0 aus Nullinitialisierung, Offset 0, `kIsLive`, Note-ID −1; direkte Events in `outputEvents` im Prozessaufruf sind der vorgesehene Weg. Normale On-/Off-Paarung, Retrigger mit vorgeschaltetem Off, parameter-only Flush und Aufräumen beim nächsten Audioblock nach Reaktivierung bestehen. `held_keys_` und der Lifecycle-Merker werden bei SDK-konformer Aufrufordnung nicht gleichzeitig benutzt. Der Code kann ohne Ausgabeliste lokal spielen; er garantiert damit keine spätere vollständige MIDI-Ereignisgeschichte. TC-R08 betrifft verlorene Zustellung. |
| Keyboard-Verfall und State-Laden | `ReadKeyboard` läuft nur mit positiven Audiolängen. Ein vor dem neu geladenen Zustand datierter Anschlag oder ein über 500 ms alter Anschlag wird verworfen. Releases werden gesondert verarbeitet. State- und Pause-/Flush-Tests erneut bestanden. |
| Echtzeit | Kein neuer eigener Lock, keine Heap-Reservierung, kein Datei-I/O und keine Message-Allokation im Audio-Pfad gefunden. `bitset` arbeitet im Objekt; Event-Queues sind vorreserviert. Die New-Zähler der fensterlosen Tests bleiben null. Mathematische Filterkoeffizienten mit `pow`/`tan` werden während der Glättung pro Hostsample berechnet: begrenzte, möglicherweise teure Arbeit, aber kein gefundener blockierender Aufruf. TC-R05 hält diese Berechnung unnötig am Leben. Keine Worst-Case-CPU-Garantie oder Erfassung aller fremden Host-Allokationen. |
| Ressourceninitialisierung | Im tatsächlich verglichenen Git-Diff `d2e62ab..HEAD` sind `core/` und `engine/` unverändert. Der im Prüfauftrag beschriebene Konstruktorpfad ist im gelesenen Code vorhanden: `DryEngine` ruft `InitResources`, Reset/`Synth::Init` schreiben die gemeinsamen Tabellen nicht neu. Diese Stelle wurde deshalb nicht als neue Änderung der Reparaturrunde behandelt. |

### Doku und Host-Fragen ohne zusätzlichen funktionalen Befund

- README „Sound“ bei `README.md:50` sagt noch, bei **vollem Damping** würden
  Saiten/FM unendlich halten. Die Parametertabelle und der Code definieren
  seit 0.2.0 gerade die Gegenrichtung: Benutzerwert 0 hält, 1 dämpft am
  stärksten. Den alten Prosasatz und entsprechenden Kommentar in
  `source/processor.h:51` berichtigen. Das ist eine bestätigte
  Dokumentationsinkonsistenz; die neue Damping-Abbildung selbst ist korrekt.
- Die erweiterte Dokumentation begrenzt die Firmware-Gleichheit auf offenen
  Filter und nicht eingreifenden Limiter und nennt die veränderte
  Automationsrichtung. Diese Einschränkungen sind nötig und stimmen im
  geprüften Umfang mit dem Code überein. Der Buildbericht bleibt wie
  beauftragt ein historischer Bericht über 0.1.0.
- Beim FREE-Umschalten ändert sich Tunes Textdarstellung. Der eigene Editor
  wird aktualisiert, ein Host-Refresh wird nicht ausdrücklich angefordert.
  `kParamTitlesChanged` betrifft laut SDK Metadaten, die hier unverändert
  bleiben; dessen Fehlen allein ist kein bewiesener Vertragsverstoß. Ein
  gecachter Host-Text kann dennoch ein Kompatibilitätsproblem sein.
  Host-Anzeige im GUI-/DAW-Test prüfen, keine falsche Anzeige in einer
  konkreten DAW behaupten.

## Fehlende Tests

1. TC-R05: Annäherung des Filters an alle Endpunkte und Rückkehr auf exakten
   Bypass bei 88.2/96/192 kHz und dokumentierten Extremraten; nicht nur 48 kHz.
2. TC-R06: Signal → vollständiger Bypass → lange Nullstrecke → Filter wieder
   aktivieren, jeweils ohne neue Eingangsanregung. Auch beliebige Phasen beim
   Öffnen sowie Reset und länger inaktive Instanzen abdecken.
3. TC-R07: Controller-/Engine-Vergleich mit identischen gültigen Normalwerten
   beiderseits jeder positiven und negativen Halbton-Grenze, über Ereignis
   und geladenen Zustand, FREE an/aus. Fixpunkte ganzer Halbtöne reichen nicht.
4. TC-R08: Erfolgreiches Note-on, volle Eingangsqueue, Release; zusätzlich
   begrenzte/zeitweise fehlende Host-Eventliste und Wiederherstellung. Die
   tatsächlich akzeptierte On-/Off-Geschichte prüfen, nicht nur intern
   konsumierte Nachrichten. Alle verworfenen Releases gezielt erfassen.
5. TC-R09: NaN/Infinity am Eingang der letzten Stufe, mit/ohne Filter, sowie
   Wiederherstellung endlicher Samples danach. Die derzeitigen Tests prüfen
   nicht endliche **Controlwerte**, nicht solche **Audiosamples**.
6. State: Für V1 alle Präfix-Abschneidungen und jedes ungültige Feld ergänzen;
   aktuell ist dies für V2 umfangreicher getestet. Der Slot-Stresstest prüft
   hauptsächlich Grenzen, nicht alle möglichen Generation/Wert-Paare. Ein
   gezielt synchronisierter Test erhöht hier die Aussagekraft.
7. FREE-Toggle und erneuter Tune-Text im generischen Host-Editor; tatsächliche
   MIDI-Aufzeichnung/Routing und Suspend-/Wake-Verhalten bleiben bei der
   DAW-Abnahme des Benutzers.

## Nicht geprüft

- Keine nativen Fenster, kein `Editor::open`, kein Editorhost, keine echte
  Maus-/GUI-Bedienung, keine DAW, kein Hörtest. Die Code-Sonden verwenden
  ausschließlich im Speicher angelegte Controls/Frames und simulierte
  Callback-Aufrufe. Auch der Wheel-Fall öffnete kein Fenster.
- `torus-classic.plugin` und `toy-piano-classic.plugin` nicht als vollständige
  CTest-Einträge ausgeführt, da sie Fenster öffnen. Keine vollständigen 6/6
  eigenen CTests behauptet. Die zwölf fensterlosen Torus-Plugin-Funktionen
  wurden separat unverändert ausgeführt.
- Debug, VS2022 und AddressSanitizer wurden hier nicht erneut ausgeführt.
  Vorliegende historische Logs ersetzen keinen eigenen Lauf. Keine komplette
  erneute Prüfung unveränderter Herkunft, Hallentfernung, Instanzisolierungs-
  Implementierung, Resampler-Koeffizienten oder Latenzformel. Die zugehörigen
  vorhandenen fensterlosen Plugin-Tests liefen als Teil des kurzen Laufs mit.
- Keine erschöpfende dynamische Analyse sämtlicher Thread-Verschachtelungen,
  aller Sampleraten, beliebig großer Hostlisten oder aller Laufzeiten.
- `build/`, `dist/`, `out/`, installierte Plugins, Daisy-Seed und fremde
  Änderungen wurden nicht verändert. Nur diese Ergebnisdatei bleibt als
  Review-Ergebnis; der eigene Wegwerfbereich wurde nach Abschluss entfernt.

### Tatsächlich ausgeführte Prüfung

```text
cmake -S . -B .work/torus-classic-review/build -G "Visual Studio 18 2026" -A x64
cmake --build .work/torus-classic-review/build --config Release --parallel 4
ctest --test-dir .work/torus-classic-review/build -C Release \
  -R '^(torus-classic\.(adapter|provenance)|toy-piano-classic\.core)$' \
  --output-on-failure --timeout 60
```

Der erste sandboxierte Build blieb bei „Checking Build System“ stehen und
wurde beendet. Derselbe isolierte Build lief anschließend mit erlaubter
Ausführung außerhalb der Sandbox durch. Beide echten Plugin-Bundles wurden
vom Build-Validator geprüft: jeweils **47 bestanden, 0 fehlgeschlagen**.

| Lauf | Ergebnis |
| --- | --- |
| Toy Piano Core | bestanden, 0,30 s |
| Torus Adapter einschließlich OutputStage/Überlast | bestanden, 25,46 s |
| Torus Provenance | bestanden, 0,22 s |
| Ausgewählte CTests insgesamt | 3/3, 26,41 s |
| Fensterloser Plugin-Teil | zwölf vorhandene Funktionen bestanden |
| Neue Sonden | TC-R05 bis TC-R09 reproduziert; Edit-/Close-Korrekturen bestätigt |

Der zusätzliche Test-Runner unter `.work/torus-classic-review/` band
`tests/test_plugin.cpp` mit umbenanntem `main` ein und verwendete denselben
Release-Code und `/fp:precise`. Ausgeführt: `TestAgainstFirmware`,
`TestBlockSizes`, `TestNoteHandling`, `TestSampleRates`, `TestInstances`,
`TestParameterState`, `TestKeyboardQueue`, `TestKeyboardMidiOut`,
`TestPauseAndFlush`, `TestActivationAndRateChange`, `TestState`,
`TestController`. `TestViewLayout`, `TestEditorResize` und
`TestEditorInWindow` wurden ausgelassen. Für die zusätzliche Close-Sonde
wurde nur im Wegwerf-Runner Zugriff auf das Editor-Knob-Array freigegeben;
Produktdateien wurden nicht verändert.

## Vereinbarte Folgeschritte während dieser Nachprüfung

Diese Anforderungen sind **noch nicht implementiert** und keine rückwirkenden
Fehler des bisher beauftragten Vier-Stimmen-Produkts:

1. **Polyphonie wählbar 4/8/16/32**, vom Benutzer ausdrücklich gewählt.
   Der Vier-Stimmen-Modus und die Klangkomplexität je Stimme sollen erhalten
   bleiben. Nicht bloß `kMaxPolyphony` hochsetzen: `part.cpp:99` berechnet
   `64 / polyphony_ - 4`; das ergibt bei 16 null und bei 32 einen negativen
   Wert. `part.cpp:158`, `:211`, `:282` koppeln Akkordtabellen ebenfalls an
   die historische maximale Polyphonie. Eine Entkopplung oder mehrere
   unveränderte Vier-Stimmen-Bänke ist gezielt zu entwerfen. Notenverteilung,
   Pegel/Mix, Limiter, RNG, Speicher, State-Migration und Kosten bei voller
   Belegung/Modellwechsel separat prüfen. Die aktuelle geringe CPU-Anzeige
   ersetzt diese Messung nicht.
2. **Plugin-Nap-Unterstützung in Studio One 7.** PreSonus beschreibt Nap als
   hostseitiges Aussetzen ruhender Plugins und bietet eine Wahl je Plugin:
   [PreSonus: Studio One Pro 7, CPU usage / Plug-in Nap](https://support.presonus.com/hc/en-us/articles/29252442184333-Studio-One-Pro-7-Managing-CPU-usage-and-overcoming-high-CPU-problems-on-your-system).
   Der jetzige Code meldet immer `kInfiniteTail` (`processor.h:52`) und setzt
   Silence-Flags bei tatsächlich null ausgegebenen Samples. Das allein
   beweist weder vollständige Nap-Unterstützung noch deren Ausschluss.
   Die PreSonus-Beschreibung veröffentlicht keinen vollständigen technischen
   Entscheidungsalgorithmus. Nicht blind `kNoTail` zurückgeben: echte
   Resonatoren und das FM-Modell können unendlich weiterklingen. Ein künftiger
   Idle-/Tail-Vertrag muss tatsächliche Ruhe erkennen, Filter-/Resampler-
   Zustände berücksichtigen und auf MIDI, Automation und Bildschirmnoten
   zuverlässig wieder anlaufen. Insbesondere Bildschirm-Noten per Message
   sind noch kein nachgewiesener Host-Wakeup-Mechanismus. Native
   Studio-One-Abnahme durch den Benutzer erforderlich; ein sichtbarer Haken
   oder eine CPU-Schätzung allein belegt die Funktion nicht.
3. **Tuning-Lock als Symbol:** Den jetzigen sichtbaren FREE-Schalter neben
   TUNE durch ein kleines Schloss im Kreis ersetzen, ohne sichtbaren Text.
   Geschlossen = Halbtonraster, geöffnet = freies Stimmen. Tooltip und
   zugängliche Benennung erhalten. Der vorhandene Parameter `Tune Free`
   darf dabei seine ID und gespeicherte Bedeutung behalten; das Schloss
   zeigt dessen invertierte Sperrbedeutung. Dies ist eine neue GUI-Vorgabe,
   keine Bewertung des bisherigen Layouts.

Die Nachprüfung änderte keinen Produktcode und setzte diese Erweiterungen
nicht vorweg um. Ein Folgeauftrag an Claude kann die gezielten Korrekturen
und die drei neuen Anforderungen getrennt von der abgeschlossenen Prüfung
behandeln.
