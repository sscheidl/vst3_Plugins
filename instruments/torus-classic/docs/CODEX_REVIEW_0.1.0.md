# Codex Review: Torus Classic 0.1.0

VERDICT: KEINE FREIGABE

Geprüft: `d2e62abf789b77592a0f471778ad964b5e9a0449`, 2026-11-14,
Windows x64, CMake 4.4.2, Visual Studio 2026 Build Tools,
MSVC 19.51.36256.0, Release. Der Code ist inzwischen committet; gemäß
Prüfauftrag gilt dieser Commit. Beim Abschluss hieß der ausgecheckte Branch
`torus-classic-0.1.0`; der Review hat keinen Branch gewechselt.

SHA-256 der tatsächlich gelesenen `instruments/torus-classic/components.json`:

```text
424d92837c0ee351406a2c9821b1c365829678a79b1e57ee0c49e095d731edb3
```

`git status --short` vor Erstellung dieser Ergebnisdatei:

```text
?? docs/CLAUDE_BUILDAUFTRAG_TORUS_CLASSIC_0.1.0.md
```

Der normale Status scheiterte zunächst in der Sandbox an gesperrten
Submodul-Metadaten. Die anschließend erlaubte, rein lesende Abfrage außerhalb
der Sandbox lieferte den obigen vollständigen Status. Auch Daisy-Seed wurde
so nur gelesen: HEAD `10f906fcd5a036fd88d20851186337e46fa38198`, keine
Änderungen unter `instruments/physical-modeling`.

Maßstab war der Buildauftrag, nicht dessen Ergebnisbericht. Vier funktionale
Befunde sind bestätigt. Die bestandenen Tests und Validator-Läufe heben sie
nicht auf. Eine Korrektur und deren gezielte Nachprüfung stehen noch aus.

## Befunde

Alle Pfade ohne Präfix sind relativ zu `instruments/torus-classic/`.

| ID | Schwere | Bereich | Befund | Beleg (Datei:Zeile) | Auslösung und Auswirkung | bewiesen/vermutet | minimale Korrektur |
| --- | --- | --- | --- | --- | --- | --- | --- |
| TC-R01 | P1 | Geladener Zustand / Audio | Ein vollständig übergebener Zustand wird beim Engine-Neustart wieder mit gleichzeitig geschriebenen Parametern vermischt. | `source/processor.cpp:129`, `:136`, `:92`, `:98`, `:295` | Während `ApplyLoadedState(A)` läuft, schreibt ein weiterer `setState(B)` dieselben Atomics. `Restart()` liest daraus jeden Parameter einzeln. Die Engine kann deshalb einen nie geladenen Mischzustand rendern, bis ein weiterer Block den nächsten Zustand übernimmt. | **Bewiesen:** Konkurrenz-Sonde mit einem State-Schreiber und einem Audio-Leser, auch mit Ein-Sample-Audioblöcken. | Die Engine beim Übernehmen unmittelbar aus dem lokalen `LoadedState` initialisieren, einschließlich zentriertem Bend. Die Atomics dürfen nicht als Zwischenquelle dieses Snapshots dienen. Auch die Veröffentlichung für `getState()` so ordnen, dass ein älterer Audio-Snapshot einen neueren geladenen Zustand nicht wieder als aktuell ausgibt. Keine Audio-Sperre einführen. |
| TC-R02 | P1 | Regler / Host-Automation | Jeder Regler meldet Beginn und Ende eines Edits doppelt an den Host. | `source/editor.cpp:361`, `:366`; SDK `vstgui4/vstgui/lib/controls/ccontrol.cpp:186`, `:204`; `public.sdk/source/vst/vstguieditor.cpp:202`, `:209` | `CControl` ruft sowohl seinen Listener als auch den Frame auf. Der Listener ist `Editor`, der Frame reicht an dessen geerbte `VSTGUIEditor::beginEdit/endEdit` weiter. Ein Griff erzeugt `begin, begin, perform, end, end` statt genau eines Edit-Paars. Die konkrete Reaktion auf verschachtelte/doppelte Edits hängt vom Host ab. | **Bewiesen:** Aufrufkette und Host-Doppel, ohne natives Fenster. | Nur einen der beiden VSTGUI-Wege an den Controller weiterleiten; für diese Konstruktion reichen die geerbten Frame-Callbacks. Ein Regler muss genau `begin/perform/end` erzeugen. |
| TC-R03 | P1 | Queue / Kausalität | Bei voller Adapter-Queue wirkt zukünftige Automation vor ihrem Sample-Zeitpunkt. | `adapter/stream_adapter.cpp:117`, `:125`, `:131`; `tests/test_adapter.cpp:437` | Sind 1024 Noten vorgemerkt, wird ein neuer Control ohne wartenden Vorgänger sofort mit `engine_.HandleEvent` angewendet. Existiert ein Vorgänger, übernimmt der neue Wert dessen ältere Zeit. Das verletzt die verbindliche Regel „zukünftige Events nicht vorzeitig anwenden“ und die uneingeschränkte Timing-Aussage im README. Blockgröße und Füllstand können das Resultat ändern. | **Bewiesen:** 1024 Noten bei Offset 1000, danach Volume bei Offset 1000: Volume wird bei interner Position 0 angewendet, noch vor dem ersten `Process()`. Der vorhandene Queue-Test erwartet dieses Fehlverhalten ausdrücklich. | Bei Überlast Platz für Controls schaffen, z. B. durch Verwerfen einer Note oder getrennte begrenzte Control-Slots. Zeitpunkte erhalten; keine sofortige Ersatzanwendung und kein Verschieben auf einen früheren Quantum-Zeitpunkt. Überlastregel dokumentieren und testen. |
| TC-R04 | P1 | Regler / Schließen | Ein beim Schließen laufender Regler-Edit wird nicht beendet. | `source/editor.cpp:334`, `:340`, `:342`; SDK `vstgui4/vstgui/lib/cviewcontainer.cpp:235`, `:557`; `controls/ccontrol.cpp:92` | `close()` vergisst die Regler und entfernt die Views. `removeAll()` löscht die Mouse-Down-Zuordnung ohne Cancel, und der Control-Destruktor sendet kein `endEdit`. Der Host erhält für den begonnenen Griff keinen Abschluss; ein Automation-/Touch-Edit kann offen bleiben. Dieser Fehler besteht auch nach Beseitigung der doppelten Meldungen aus TC-R02. | **Bewiesen:** Codepfad und In-Memory-Sonde: nach `beginEdit()` und echtem `Editor::close()` nur `begin, begin`, kein `end`. Kein natives Fenster geöffnet. | Vor `ForgetViews()`/View-Abbau alle noch aktiven Regler-Edits kontrolliert beenden bzw. abbrechen und ausstehende Wheel-Edits berücksichtigen. Abschluss genau einmal sicherstellen. |

### Reproduktionsdetails und Aussagegrenzen

**TC-R01:** Eine Wegwerf-Prüfhilfe verwendete die unveränderten Klassen und
die State-Helfer aus `tests/test_plugin.cpp`. Ein Thread lud abwechselnd
Version-1-Zustände mit achtmal `0.1` und achtmal `0.9`. Model wird dabei wie
im Produkt auf seine Stufe normalisiert; geprüft wurden die sieben
kontinuierlichen Engine-Controls. Der andere Thread rief höchstens 10 000-mal
`process()` mit einem Sample und ohne Events/Parameteränderungen auf und las
danach auf demselben Thread die Engine-Controls. Bereits nach Audioblock 11
ergab sich in einem Lauf:

```text
Tune Structure Brightness Damping Position Excitation Volume
0.9  0.1       0.9        0.1     0.1      0.9        0.9
```

Ein weiterer Lauf fand nach Block 15 sechs Werte `0.1` und Volume `0.9`.
Auch ein reiner Parameter-Flush reproduzierte die Mischung. Es gab genau
einen Schreiber und einen Audio-Leser, keine konkurrierenden DSP-Leser und
keine Änderungen am Produktcode. Die Ursache liegt hinter der korrekten
Triple-Buffer-Übergabe: `Restart()` benutzt nicht deren lokalen Snapshot.
Die Sonde beweist keinen dauerhaft verlorenen letzten Zustand und keine
Dateibeschädigung. Sie widerlegt aber den Kommentar in
`tests/test_plugin.cpp:989`, wonach jeder Block aus einem vollständigen
Wertesatz gerendert werde. Der vorhandene Stresstest prüft dies während des
Laufs nicht; seine Endprüfung nach einem weiteren einzelnen Load reicht
dafür nicht.

**TC-R02/TC-R04:** Ein echter `CKnob` mit `Editor` als Listener und ein
`CFrame` mit demselben Editor als Frame-Editor wurden nur als C++-Objekte
erzeugt. Ein `IComponentHandler`-Doppel protokollierte die Meldungen.
`beginEdit`, Wertänderung und `endEdit` ergaben `b b p e e`. Beim zweiten
Fall wurde der Control dem Frame hinzugefügt und nach `beginEdit` durch
`Editor::close()` abgebaut: `b b`. Kein `open`, kein HWND, kein Zeichnen,
keine visuellen Tests. Die Sonde belegt die Meldungsfolge, nicht das
Verhalten einer bestimmten DAW.

**TC-R03:** Die unmittelbare Anwendung ist außerdem bereits in
`TestQueueLimits` enthalten. Das Bestehen dieses Tests ist hier kein
Korrektheitsnachweis gegenüber dem Buildauftrag: seine Erwartung ist falsch.
Eine volle Queue darf Noten gemäß dokumentierter Überlastregel verwerfen;
dies erlaubt keine zeitlich vorgezogenen Parameter. Eine allgemeine
Blockinvarianz bei beliebiger Überlast wurde nicht bewiesen.

## Ohne Befund geprüft

„Ohne Befund“ bezeichnet die jeweils genannten Codepfade und Prüfungen,
keine allgemeine Echtzeit- oder Host-Freigabe.

| Prüfpunkt | Prüfung und Ergebnis |
| --- | --- |
| 1: Trockener Pfad, Herkunft | `engine/synth.cpp` mit `tests/reference/src/physical_modeling.cpp` verglichen: Advance, Modellwechsel, maximal vier Trigger, Velocity × Excitation, Part, Fade, rechter Polaritätswechsel und Output bleiben in der entsprechenden Reihenfolge. Wet-Instanz, Speicher, Mix und Hallparameter sind aus dem Produktpfad entfernt. Kein Reverb-Symbol in den vier Produktbibliotheken mit `dumpbin /linkermember:1`; die Referenz wird nicht ins Plugin gelinkt. |
| 1: Manifest und Patches | Alle 73 `origin_sha256` unabhängig mit den lokalen Daisy-Seed-Dateien verglichen: 0 Abweichungen. Provenance-CTest bestätigt zusätzlich 73 lokale Dateien, 57 unverändert und 16 gepatcht/adaptiert/abgeleitet. Alle zehn veränderten/abgeleiteten Core-Dateien gegen ihre Quelle gelesen: RNG, const-Tabellen, einmalige Sinusinitialisierung, Hall-Include, leere Arrays und Profiling entsprechen der Liste. |
| 2: Referenzvergleich | Separate Bibliothek mit `torus_reference`, `stmlib_reference`, `physical_modeling_reference`. Auch Symboltabellen bestätigen getrennte `Synth`, `InitResources` und RNG-Symbole. Die generierte Ressourcenquelle ist exakt die Referenz minus der beiden leeren Array-Definitionen, nach LF-Normalisierung. 400 stille Quanta und `Wet == 0` werden vor dem eigentlichen Vergleich geprüft. Energie-, Modell-, Trigger- und RNG-Prüfungen verhindern einen bloßen Stillevergleich. Engine: 1 712 712 Samples gleich, nur 1765 unterschiedliche Null-Vorzeichen. Plugin: 5 760 000 Samples gleich bei gleichem FTZ/DAZ; im anderen Float-Modus maximal 1.12e-39. |
| 3: Instanzisolierung und Speicherleben | RNG-Arithmetik und Ziehstellen stimmen mit der Referenz überein. `DryEngine` initialisiert die Sinustabelle vor dem ersten Synth; Reset erreicht `call_once` nicht. Gemeinsam gelesene Klangtabellen werden danach nicht erneut geschrieben. Storage hat `alignas(Synth)`, der alte Synth wird vor Nullung/Placement-new zerstört. Engine- und Plugin-Tests für Verschachtelung, parallele Instanzen und Threadwechsel bestanden. Kein weiterer veränderlicher globaler Klangzustand in den untersuchten Produktdateien gefunden. |
| 4: Adapter, regulärer Betrieb | Rationale Zeit, Quantum-Aufrundung, FIFO-Indizes, Compaction und Reset gelesen; vorhandene Timing-/Blocktests bestanden. Für den letzten Sample eines Blocks folgt aus `(latency+1)·num/den >= Right()+1`, dass kein benötigtes Quantum bei oder hinter dem frühesten Rasterpunkt eines Events des nächsten Blocks beginnt. Das Argument gilt auch bei mehr als zwölf internen Samples je Host-Sample. Die Einschränkung ist TC-R03 bei Überlast. |
| 4: Grenzen und Float-Modus | Im dokumentierten Ratenbereich ist `offset * num` für nichtnegative int32-Offsets unter 1.04e18 und damit innerhalb uint64. Ratio-Nenner höchstens 4e10, Zähler höchstens 480 000 000; die laufenden Restadditionen passen. Die int64-Positionen sind für Jahre ausreichend, nicht mathematisch unbegrenzt. `DenormalsAreZero` stellt MXCSR über RAII wieder her. `Prepare` reserviert den aus Filterfenster, maximalem Schritt und Quantum abgeleiteten Puffer; kein konkreter Indexfehler gefunden. Keine erschöpfende dynamische Prüfung jeder Rate. |
| 5: Resampling | Exakte Zeilen über `rem < den`; interpolierte Tabellen mit zusätzlicher Randzeile gelesen. Normierung erfolgt in Double vor Float-Speicherung. Vorhandener Response-Test bestanden: ±0.01 dB im geprüften Durchlassraster und weniger als −90 dB RMS-Rest/Leakage im geprüften Frequenzraster, einschließlich der zusätzlichen krummen Raten. Gemeldete Latenz 49/0/90/97/195 Samples bei 44.1/48/88.2/96/192 kHz bestätigt. |
| 6: Audio-Pfad | Im eigenen Renderpfad keine Heap-Reservierung, kein Mutex, keine Dateioperation und keine Nachrichtenallokation gefunden; vorhandene New-Zähler blieben 0. `std::function` betrifft die UI. `Clock::now` wird beim Lesen von Tastaturnoten höchstens einmal pro Block aufgerufen; die installierte MSVC-Implementierung führt zum Performance Counter, kein eigener Lock erkennbar. Ein Worst-Case-Zeitnachweis für diesen Systemaufruf wurde nicht erbracht. Der Engine-Reset nullt/initialisiert rund 112 kB ohne Allokation; technisch begrenzt, seine DAW-Spitzen wurden nicht gemessen. |
| 7: Übergabe und Lebenszyklus | `LatestValue` als Triple Buffer unter einem serialisierten Schreiber und einem Leser gelesen: Slots werden beim Austausch exklusiv übergeben; kein Fehler an dieser Datenstruktur gefunden. Der Mutex serialisiert State-Schreiber außerhalb des Audiopfads. SPSC-Queue passt zur dokumentierten Aufrufordnung. Normales Laden, Keyboard-Verfall, Flush, Aktivierung und Ratenwechsel bestanden. Die erneuten Atomics-Lesezugriffe nach der Übergabe sind TC-R01. |
| 8: Noten und MIDI | Validierung vor Rundung/Cast, Null-Velocity und Note-off, Begrenzung/Monotonisierung von Noten-Offsets, alle Parameterpunkte, Bend-Mitte einschließlich 8192/16383, Modellgrenzen und MIDI-Mapping im Code geprüft und durch die fensterlosen Plugin-Tests bestätigt. Gleiche Parameter im Quantum dürfen auf ihren letzten Wert zusammenfallen; für diese Engine werden Noten erst beim anschließenden Rendern ausgelöst. Kein zusätzlicher Klangfehler aus deren relativer HandleEvent-Reihenfolge gefunden. |
| 9: Parameter und State-Format | IDs 100–108, Vorgaben, kontinuierliches Tune, Model stepCount 5, Automationsflags, sieben Regler ohne Bend-Regler und fehlende Reverb-/Polyphony-CCs stimmen mit dem Auftrag überein. Gemeinsamer State-Reader validiert vor Übernahme; Pitch Bend wird nicht gespeichert und beidseitig zentriert. 127 beschädigte Processor-Zustände sowie Controller-/Textparser-Fälle bestanden. Getrennte Zoom-/Keyboard-Daten gelesen. Keine pauschale Silence bei fehlenden Events; Infinite Tail ist konservativ. |
| 10: Editor, Codeebene | Destruktor schließt vor Abbau des Editor-Objekts; SDK-Basisklassen halten den Controller und entfernen zerstörte Views aus dessen Liste. Fehlerpfad von `open`, Wiederanhängen, Timer-Abbau und Resize-/onSize-Aufteilung gelesen. Modellwahl hat ein eigenes begin/perform/end, KEYS und Größe benutzen keine Parameter-Edits. Konkrete neue Reglerfehler: TC-R02 und TC-R04. Fensterabläufe wurden nicht ausgeführt. |
| 11: Compileroptionen | Erzeugte vcxproj-Dateien geprüft: Core, Engine, Adapter, Shell, Referenz, Tests und Benchmark mit `FloatingPointModel=Precise`; Release-CRT `MultiThreaded`. Plugin-Ziel mit `Fast` kompiliert `factory.cpp` und SDK-`dllmain.cpp`, dort kein Klang-/State-Rechenpfad. SDK/VSTGUI behalten ihre Optionen. NaN-/Infinity-Tests im tatsächlichen Release-Build bestanden. |
| 12: Integration | Release-Build beider Plugins erfolgreich; beide Validatoren melden 47 bestanden, 0 fehlgeschlagen. Toy-Piano-Dateien gegenüber `e553451` unverändert, dessen Core-Test bestanden. TEST/_USE_MATH_DEFINES werden nicht in Adapter/Shell/Plugin weitergereicht. Fremdcode-Warnungsunterdrückung bleibt bei Core/Engine. Moduleinfo nennt TAUREON, 0.1.0 und die eigenen beiden FUIDs; Stereo-Instrument ohne Audioeingang. `dumpbin /dependents` zeigt nur Windows-System-DLLs. |

### Grenzen der Testaussagen und Dokumentation (Prüfpunkte 12/13)

Der Referenzvergleich ist für seine tatsächlich geprüften Folgen aussagekräftig
und nicht nur ein Vergleich zweier neuer Wrapper. Blocktests mit derselben
Engine prüfen dagegen gezielt die Aufteilung, nicht unabhängig deren Klang.
Die Null-Toleranz bzw. 1e-30-Grenze und die Filtergrenzen stehen mit Begründung
im Quelltext. Ob sie historisch wirklich vor dem ersten Lauf festgelegt
wurden, lässt sich aus dem jetzigen Commit nicht unabhängig belegen.

`FitTone` ist ein Sinus-/Kosinus-Least-Squares-Fit auf dem ausgeschwungenen
Zeitfenster. Der Rest ist ein RMS-Maß für die Summe der unerwünschten Anteile;
es ist keine erschöpfende Spektral-Maximumsuche. Das geometrische
Frequenzraster mit Bandkanten ist sinnvoll, garantiert aber keine Schranke
für jede Zwischenfrequenz, jede Rate oder jede Transientensituation. Bei
Upsampling gibt es unterhalb 24 kHz keinen untersuchten Eingangs-Stopband;
die ausgegebenen −400 dB sind dort der unveränderte Initialwert für einen
leeren Messbereich, keine gemessene Dämpfung. Die Tabelle des Build-Berichts
kennzeichnet das zutreffend. „Vollständig getestet“ in `docs/PLANUNG.md:77`+ist als allgemeine Aussage zu weitgehend; die oben benannten endlichen
Prüfungen sind die belegte Reichweite.

Die New-Zähler sehen nur den aufrufenden Thread und die ersetzten normalen
`new`/`new[]`-Formen, nicht malloc, aligned new, VirtualAlloc oder fremde DLLs.
Sie beweisen keine Sperrfreiheit. Paralleltests können Datenrennen verpassen;
besonders `TestState` prüft während der Konkurrenz keine kohärenten
Engine-Parameter und übersieht TC-R01. Der Build-Bericht beschreibt die
Werkzeuggrenzen überwiegend korrekt, seine Aussage einer bestätigten
State-Übergabe muss nach TC-R01 eingeschränkt werden.

README „never early“ und uneingeschränkte Blockunabhängigkeit gelten mit der
jetzigen Überlastbehandlung nicht (TC-R03). Der Bericht nennt die
Regler-Mausbedienung zwar als offen; die doppelten bzw. fehlenden Host-Edits
sind dennoch konkrete Fehler und nicht bloß ausstehende visuelle Abnahme.
Die Statusaussage „no functional defect is known / ready for the DAW test“
muss nach diesem Review aktualisiert werden.

Kleine Doku-Ungenauigkeit ohne eigenen funktionalen Befund:
`UPSTREAM.md:80` sagt, es gebe keinen lokalen `static`; `InitResources`
enthält bewusst den `static std::once_flag`. Gemeint sein kann hier nur
weiterer veränderlicher Klangzustand. Den Initialisierungs-Guard ausnehmen.

## Fehlende Tests

1. TC-R01: Beim gleichzeitigen Laden während jedes beobachteten Blocks
   prüfen, dass alle sieben Engine-Controls zu einem vollständigen geladenen
   Snapshot gehören; nicht nur Finite-Checks und den letzten Zustand prüfen.
   Zusätzlich einen gezielt synchronisierten Test für alten/neuen Snapshot,
   Automation und anschließendes `getState()` vorsehen.
2. TC-R02: Einen echten VSTGUI-Control samt Frame-Anbindung gegen den
   Host-Recorder testen. Der direkte Aufruf von `ChooseModel` oder
   `controlBeginEdit` deckt die doppelte SDK-Weiterleitung nicht ab.
3. TC-R03: Überlast mit zukünftigen Controls sowohl ohne als auch mit
   wartendem Control; tatsächlichen Anwendungszeitpunkt prüfen. Gegen eine
   Aufteilung mit freier Queue vergleichen und die erlaubten Notenverluste
   ausdrücklich berücksichtigen.
4. TC-R04: Begonnenen Drag bzw. Wheel-Edit beim View-Abbau prüfen; genau ein
   Abschluss je begonnenem Host-Edit, auch ohne `removed()`.
5. Bereits im Prüfauftrag offen: deterministisch erzwungener
   `setActive(true)`-Übernahmepfad bei gleichzeitig zuvor überschriebenen
   State-Atomics; bloßes Lesen ersetzt diesen Regressionstest nicht.

## Abweichungen vom Buildauftrag

| Abschnitt | Bewertung |
| --- | --- |
| 1: Umfang | Produktidentität, sechs Modelle, vier Stimmen, trockener Stereoausgang, Noten/Bend/CCs, C++20/VSTGUI/statische Runtime sind im geprüften Build vorhanden. Keine zusätzliche Funktion außerhalb des beauftragten Umfangs gefunden. Die Gesamtbereitschaft ist wegen der vier Befunde noch nicht erreicht. |
| 3: Schichten, trockener Kern, Instanzen | Schichten, Herkunft, gezielte Port-Patches, trockene Referenz und Instanz-RNG erfüllt, soweit oben geprüft. Die Adapter-Schicht benutzt eine Engine-Schnittstelle statt DSP-Typen; das ist eine sinnvolle Entkopplung. |
| 4: Takt und Echtzeit | Fester Takt/Quantum, 48-kHz-Direktpfad, reguläres Timing, Sampleformate und gemeldete Latenz belegt. Vorzeitige Automation bei Queue-Überlast widerspricht dem Auftrag (TC-R03). Die laufende Übernahme geladener Zustände ist nicht kohärent (TC-R01). Eigener Sinc statt der geforderten kleinen Abhängigkeit ist eine ausdrücklich dokumentierte Abweichung. |
| 5: Parameter und Zustand | Parametervertrag und Binärformat erfüllt; die sichere vollständige Übernahme im laufenden Betrieb ist nur teilweise erfüllt (TC-R01). |
| 6: Editor und Bedienung | Umfang und Host-Resize-Trennung sind im Code vorhanden. Korrekte musikalische Host-Edits und sicherer Abschluss beim Schließen sind nicht erfüllt (TC-R02/TC-R04). Bedienung, Zeichnung und echte Host-Resize-Reaktionen bleiben bei der vom Benutzer übernommenen Abnahme. |

**Bewertung des eigenen Resamplers:** Die Schnittstelle ist klein, die
Koeffizienten werden außerhalb des Audio-Threads aufgebaut, und vorhandene
Messungen prüfen mehr als bloße Klangähnlichkeit. Das macht die Entscheidung
technisch nachvollziehbar. Es bleibt eigener zu wartender DSP-Code und eine
Abweichung vom Wortlaut des Auftrags; die endlichen Messungen ersetzen weder
eine vollständige Fehleranalyse noch einen Vergleich mit einer bewährten
kleinen Bibliothek. Allein daraus folgt hier kein weiterer funktionaler
Fehler. Eine nachträgliche Billigung dieser Abweichung wurde nicht unterstellt.

## Nicht geprüft

- Keine visuellen Tests, kein Editorhost-Aufruf, keine DAW, kein Hörtest.
  Auch die vorhandenen CTests `torus-classic.plugin` und
  `toy-piano-classic.plugin` wurden nicht vollständig gestartet, weil sie
  echte Fenster öffnen. Die oben beschriebenen In-Memory-Sonden öffnen keine.
- Nur Release/VS2026 neu gebaut. Debug, VS2022 und AddressSanitizer nicht
  erneut ausgeführt; die behaupteten früheren Erfolge werden hier nicht als
  unabhängig bestätigt übernommen. Kein zusätzlicher Benchmark-Lauf.
- Keine Worst-Case-Zeitgarantie: SDK-Aufrufe lesen hosteigene Event- und
  Parameterlisten. Deren Schleifen sind durch die gelieferten int32-Anzahlen
  begrenzt, nicht durch ein eigenes konstantes Arbeitsbudget. Interne
  Adapter-/Noten-/Keyboard-Queues haben feste Kapazitäten. Verhalten bei
  beliebig großen fehlerhaften Hostlisten wurde nicht dynamisch geprüft.
- Keine vollständige dynamische Prüfung aller Raten von 1 kHz bis 4 MHz,
  aller Langzeit-Zählerstände und aller möglichen Thread-Verschachtelungen.
  Kein neuer konkreter Firmware-Befund festgestellt; unveränderten Upstream
  nicht vollständig neu auditiert.
- Kein Host-Test für gecachte Latenz nach Ratenwechsel. Die neue Latenz ist
  nach `setupProcessing` abfragbar, aber `restartComponent(kLatencyChanged)`
  fehlt. Die Begründung des Berichts setzt ein erneutes Abfragen des Hosts
  voraus; dessen tatsächliches Verhalten ist nicht durch den Validator oder
  die Tests mit direktem `getLatencySamples()` bewiesen.
- `dist/`, `out/`, installierte VST3-Kopien und fremde Build-Verzeichnisse
  wurden nicht verändert. Das Ergebnisbundle des ursprünglichen Buildauftrags
  und dessen historische Logs wurden nicht als Ersatz für eigene Prüfung
  herangezogen.

### Tatsächlich ausgeführte kurze Prüfungen

```text
cmake -S . -B .work/torus-classic-review/build -G "Visual Studio 18 2026" -A x64
cmake --build .work/torus-classic-review/build --config Release --parallel 4
ctest --test-dir .work/torus-classic-review/build -C Release \
  -R '^(torus-classic\.(engine|adapter|provenance)|toy-piano-classic\.core)$' \
  --output-on-failure --timeout 60
```

Der erste sandboxierte MSBuild-Lauf blieb bei „Checking Build System“ stehen
und wurde beendet. Der danach erlaubte Lauf außerhalb der Sandbox baute
erfolgreich ausschließlich in das Review-Verzeichnis. Beide Plugin-Bundles
wurden dabei durch den Build-Validator geprüft: jeweils **47/47** bestanden.

| Lauf | Ergebnis | Laufzeit |
| --- | --- | --- |
| Toy Piano Core | bestanden | 0,30 s |
| Torus Engine | bestanden | 4,32 s |
| Torus Adapter | bestanden | 23,18 s |
| Torus Provenance | bestanden | 0,19 s |
| Vier CTests insgesamt | 4/4 bestanden | 28,37 s |
| Fensterloser Plugin-Teil | alle zehn nachfolgend genannten Funktionen bestanden | ca. 16 s |
| Zusätzliche kurze Sonden | TC-R01 bis TC-R04 reproduziert | jeweils deutlich unter einer Sekunde für die Ausführung |

Für den fensterlosen Plugin-Lauf wurde nur unter
`.work/torus-classic-review/` eine zusätzliche Übersetzungseinheit angelegt,
die `test_plugin.cpp` mit umbenanntem `main` einbindet. Derselbe Release-Link
und dieselben Compileroptionen wie beim vorhandenen Plugin-Test wurden benutzt.
Aufgerufen wurden unverändert: `TestAgainstFirmware`, `TestBlockSizes`,
`TestNoteHandling`, `TestSampleRates`, `TestInstances`, `TestKeyboardQueue`,
`TestPauseAndFlush`, `TestActivationAndRateChange`, `TestState`,
`TestController`. `TestViewLayout`, `TestEditorResize` und
`TestEditorInWindow` wurden ausgelassen. Das ist ausdrücklich kein vollständiger
Erfolg des originalen Plugin-CTest-Eintrags.

Alle Wegwerfquellen und Review-Build-Artefakte werden nach Übertragung der
Ergebnisse entfernt. Außer dieser Ergebnisdatei wurden keine Produktquellen,
Tests oder Dokumente geändert; nichts committet oder gepusht.
