# Codex Review: Toy Piano Classic 0.3.0

VERDICT: KEINE FREIGABE

Geprüft: `main @ e3b17a72d88ce3fe1de0b107ab58a425e97a600a`, 2026-11-14,
Windows x64, MSVC 19.51.36256.0 (VS Build Tools 2026), zusätzlich
MSVC 19.44.35227.0 (VS Build Tools 2022).

Der Vergleich `13519b1..e3b17a7` enthält ausschließlich den Prüfauftrag. Der
geprüfte Plugin-Code entspricht damit dem im Auftrag genannten `13519b1`.

Nachtrag des Auftraggebers: Starten und Spielen mit Tastatur in Studio One 7
sind bestätigt. Dies ergänzt die Codeprüfung um einen berichteten Praxistest.

Die vorhandenen Tests und der Validator bestehen. Trotzdem bleiben die unten
reproduzierten Fehler. Für die Freigabe sollten insbesondere R01–R05 geklärt
beziehungsweise korrigiert werden. **R01 ist ein nachgewiesener Speicherfehler,
setzt aber einen Host voraus, der `removed()` auslässt.** Im regelkonformen
Lebenszyklus wurde dieser Fehler nicht gefunden. Die Einstufung P0 folgt der
im Auftrag vorgegebenen Kategorie „Speicherfehler“ und behauptet keinen
Absturz in einer bereits getesteten DAW. R07 ist ein Firmware-Befund.

## Befunde

Alle Dateiverweise sind relativ zu `instruments/toy-piano-classic/`, sofern
nicht ausdrücklich mit `external/` bezeichnet.

| ID | Schwere | Bereich | Befund | Beleg (Datei:Zeile) | Auslösung und Auswirkung | bewiesen/vermutet | minimale Korrektur |
| --- | --- | --- | --- | --- | --- | --- | --- |
| R01 | P0, bedingt durch Host-Protokollverletzung | Editor-Lebensdauer | Zerstörung entfernt den Editor nicht aus `editors_`; die nächste Parameteränderung liest freigegebenen Speicher. Außerdem fehlt eine eigene Zerstörungsbereinigung des noch offenen Frames. | `source/controller.cpp:138`, `:196`, `:201`; `source/editor.cpp:333`; `external/vst3sdk/public.sdk/source/vst/vsteditcontroller.cpp:266`; zugehöriger Header `:78` | `createView` → erfolgreiches `attached` → letzte View-Referenz freigeben **ohne** `removed` → `Controller::setParamNormalized`. Der SDK-Destruktor ruft `editorDestroyed`, dessen geerbte Implementierung leer ist. ASan meldet `heap-use-after-free` in `Editor::ParameterChanged`. | Bewiesen durch ASan mit tatsächlich angehängtem, verborgenem Testfenster; kein DAW-Nachweis. | `editorDestroyed` überschreiben und denselben, idempotenten Austrageweg wie `editorRemoved` verwenden. Zusätzlich im abgeleiteten Editor-Destruktor einen noch offenen Frame sicher schließen; die Bereinigung muss auch nach regulärem `removed` gefahrlos sein. |
| R02 | P1, insbesondere falsche Bitgleichheitszusage | Volumeglättung | Der Float-Schritt kann auf null runden, obwohl der Abstand noch größer als `kVolumeSnap` ist. Die Glättung erreicht dann ihr Ziel nie. | `source/engine.cpp:16`, `:60`; `source/engine.h:25`; `README.md:113`; `tests/test_plugin.cpp:415` | Nach Volume `0.25` zurück auf `1.0` bleibt der Wert bei 48 kHz auf `0.999985695`, bei 192 kHz auf `0.99994278`. Weitere Sekunden ändern ihn nicht. Kein blockierender Loop, aber dauerhaft zusätzlicher Glättungsschritt und keine bitgenaue Rückkehr auf Firmware-Pegel. | Bewiesen, auch mit unveränderter Release-Bibliothek. Die Pegelabweichung ist sehr klein; Hörbarkeit wurde nicht behauptet. | Den nächsten Wert berechnen und auch bei `next == current` auf das Ziel setzen, oder mit ausreichend genauer Akkumulation arbeiten. Beide Annäherungsrichtungen und hohe Zielpegel prüfen. |
| R03 | P1 | Parametertexte | `fromString` akzeptiert Unsinn und gültige Zahlenpräfixe mit beliebigem Rest. | `source/controller.cpp:40`, `:79`; `external/vst3sdk/pluginterfaces/base/ustring.cpp:146` | `foo` wird für Attack/Decay erfolgreich zu Minimum und für Volume zu 0 dB. `12junk` wird ebenfalls akzeptiert. Die Windows-SDK-Hilfe prüft `swscanf(...) != -1`; auch null Konvertierungen gelten so als Erfolg. Die eigene Implementierung übernimmt diesen Fehler. | Bewiesen über `Controller::getParamValueByString` gegen die Release-Bibliothek. | Eigenen Parser mit Prüfung auf genau eine Konvertierung und vollständigen Verbrauch verwenden. Nicht endliche Zahlen ablehnen; `-inf` für Volume ausdrücklich zulassen. Bei Ablehnung den Ausgabewert unverändert lassen. |
| R04 | P1 | Abgelehnte Größenänderung | Der Rückfall „Inhalt passt in vorhandenes Fenster“ funktioniert am Mindestzoom nicht. | `source/editor.cpp:201`; `source/editor.h:54`; `source/editor.cpp:275` | Fenster ohne Klaviatur bei 50 %: `230×95`. Klaviatur einblenden, Host lehnt `230×136` ab. `EditorZoomForSize` müsste etwa `0.3493` liefern, wird aber auf `0.5` begrenzt. Die Klaviatur benötigt weiterhin 136 Pixel Höhe im 95 Pixel hohen Frame und wird abgeschnitten. Dasselbe Grundproblem besteht bei vom Host erzwungenen Größen unter dem Minimum. | Bewiesen mit verborgenem HWND und ablehnendem `IPlugFrame`; Größen und Transformationsmaßstab geprüft, keine optische Bewertung. | Bei Ablehnung entweder die Änderung einschließlich Toggle zurücknehmen oder einen gesonderten Darstellungsmaßstab zum Einpassen zulassen. Gespeicherten Nutzerzoom und vorübergehenden Darstellungsmaßstab getrennt behandeln. |
| R05 | P2 | Host-Vertrag beim Resize | Die native View wird schon vor der Zustimmung des Hosts auf die gewünschte Größe gesetzt. | `source/editor.cpp:215`, `:286`, `:368`; `:177`; `external/vst3sdk/pluginterfaces/gui/iplugview.h:123` | Beim Aufruf von `resizeView` meldet `getSize` noch `460×190`, der native Frame ist aber bereits `460×272`. Das SDK verlangt die Änderung der Plattformdarstellung erst in `onSize`. Bei Ablehnung wird die Änderung nachträglich zurückgenommen; bis dahin können Host und Plugin verschiedene Geometrien beobachten. | Vertragsverletzung und vorzeitige Größenänderung bewiesen. Konkrete DAW-Folge nicht getestet. | Vor der Host-Anfrage nur den gewünschten Zustand vorbereiten. Native Größe/Zoom bei Zustimmung in `onSize` anwenden; bei Ablehnung unmittelbar den Rückfall für die bisherige Größe ausführen. Auch den Initialisierungsweg berücksichtigen. |
| R06 | P2 | Echtzeitbegrenzung der UI-Queue | Die endliche Queue begrenzt die Arbeit eines `Render`-Aufrufs nicht auf 64 Nachrichten, wenn der Erzeuger gleichzeitig nachfüllt. | `source/processor.cpp:114`; `core/spsc_queue.h:27` | `while(Pop(...))` kann neue, während desselben Aufrufs produzierte Noten ebenfalls verarbeiten. Es fehlt ein festes Arbeitsbudget. Für gewöhnliche Mausereignisse wurde keine Überlastung beobachtet; ein Hänger ist nicht nachgewiesen. | Fehlende feste Obergrenze durch Code bewiesen; praktische Überlastung nur ein Risiko. | Die Anzahl der Pops pro Block begrenzen, etwa auf die Queue-Kapazität. Dies ist eine Änderung im Plugin, kein Fehler der Firmware-Queue. |
| R07 | P1, Firmware-Befund | Oberton beim Pitch-Glide | Das Obertongewicht springt an der Viertel-Samplerate-Grenze sofort zwischen `0.12` und `0`; die Pegelrampe glättet diesen Wechsel nicht. Die allgemeine Zusage „no jump in the waveform“ ist dadurch nicht abgedeckt. | `core/toy_piano.cpp:124`, `:156`; `source/engine.cpp:93`; `README.md:165`; `tests/test_plugin.cpp:256` | 48 kHz, Note 115, Velocity 127, Attack 2 ms, Decay 3 s; nach 4800 Samples Pitch von 0 auf +24. Beim 160. Glide-Sample verschwindet der Oberton. Gegen eine Kopie des Kernzustands unmittelbar vor dem Schritt beträgt die zusätzliche Sampleänderung etwa `-0.00149732`. Die Sampleberechnung nutzt noch dieselbe Phase, der Unterschied entsteht durch den Obertonwechsel. | Sprung bewiesen durch Code und Messung; hörbarer Klick nicht durch Abhören geprüft. | In Daisy-Seed den Obertonanteil kontinuierlich und unterhalb der Alias-Grenze ausblenden beziehungsweise einblenden; danach die abgestimmten Firmware-Dateien übernehmen. Bis dahin die Zusage auf den tatsächlich geprüften Glide-Fall einschränken. |
| R08 | P1, Dokumentation | Umfang des Bitgleichheitstests | Die Formulierung suggeriert sieben Blockgrößen jeweils mit 32- und 64-Bit-Verarbeitung. Das ist nicht der ausgeführte Testumfang. | `README.md:113`, `:162`; `tests/test_plugin.cpp:208`, `:212` | Die sieben Größen `1,16,48,64,333,1024,2048` laufen nur mit 32 Bit. Für 64 Bit läuft zusätzlich genau eine Größe: 256. Damit sind acht Kombinationen belegt, nicht vierzehn. | Bewiesen durch Testcode und erfolgreichen Testlauf. Kein nachgewiesener 64-Bit-Audiofehler. | Dokumentation auf den wirklichen Umfang beschränken oder später alle sieben Größen auch mit 64 Bit testen. |

### Reproduktionsdetails und Grenzen der Befunde

- **R01:** Eine separate ASan-EXE kompilierte die unveränderten fünf
  Plugin-Implementierungen und `core/toy_piano.cpp`; SDK-Bibliotheken waren
  nicht instrumentiert. `/fp:fast` entsprach dem SDK-Build. STL-Container-
  Annotationen waren wegen der nicht instrumentierten Bibliotheken deaktiviert;
  die gemeldete Heap-Lebensdauerverletzung benötigt diese Annotationen nicht.
  Ein erfolgreicher `attached`-Aufruf wurde verwendet, kein bloß manuell
  eingetragener Zeiger. Ohne den absichtlich fehlerhaften Host-Abbau endete die
  Zusatzsonde mit Exitcode 0; mit ihm mit Exitcode 1 und obigem ASan-Stack.
- **R02:** Je Samplerate `Engine::Init(rate)`, `SetVolume(0.25f)`, eine Sekunde
  `Process`, `SetVolume(1.0f)`, zwei Sekunden `Process`, danach eine weitere
  Sekunde zum Stillstandsnachweis. Bei 8 kHz wird 1 exakt erreicht; bei
  44.1/48/96/192 kHz bleiben die Werte `0.999986887`, `0.999985695`,
  `0.999971390`, `0.999942780`. Die anfängliche Bitgleichheit bei unberührtem
  Volume 1 wird dadurch nicht widerlegt.
- **R03:** Leere Strings wurden abgelehnt. `-inf` bei Volume ergab korrekt
  normalized 0. `+inf` wurde zum Maximum, `nan` im regulären `/fp:fast`-Build
  ebenfalls erfolgreich zu 0. Das ist keine verlässliche Eingabevalidierung.
- **R04/R05:** Ein eigener `IPlugFrame` las beim Eintritt in `resizeView` sowohl
  `getSize` als auch die Frame-Größe und gab `kResultFalse` zurück. Gemessen:
  `old=460×190, native=460×272, wanted=460×272`; am Minimum entsprechend
  `230×95, 230×136, 230×136`. Der Rückfall verkleinerte den Frame, konnte aber
  den laut Mindestzoom benötigten Inhalt nicht einpassen.

## Ohne Befund geprüft

### 1. Audio-Thread und Atomics

Der eigene Audio-Pfad enthält keine Allokation, Sperre, Ausgabe, Message-
Allokation oder `std::function`-Verwendung. Die Lambdas in `Render` sind lokale
Closures ohne Typauslöschung. `pow`, `lround`, `sqrt` und die DSP-Schleifen
arbeiten ohne erkennbare Heap-Operationen; Host-Interfaces liefern Events und
Parameter per virtuellen Aufrufen. Deren beliebige Host-Implementierungen
wurden nicht instrumentiert. R06 bleibt als fehlende Arbeitsbegrenzung.

`std::atomic<double>::is_always_lock_free` wurde auf dem geprüften Windows-x64-
Compiler als wahr gemessen. Die Queue sichert lockfreie 32-Bit-Atomics bereits
per `static_assert`. Es gibt keine Engine-Mutation aus `setState` oder `notify`.

Die vier einzelnen Parameter-Atomics ergeben **keine Zustandstransaktion**:
`process` und `getState` können beim gleichzeitigen Laden eine Mischung alter
und neuer Werte sehen; Automation und State-Laden können sich überschreiben.
Jeder einzelne Wert bleibt jedoch begrenzt, und Pitch/Volume werden geglättet.
Ein Speicherfehler oder ungültiger DSP-Zustand folgt daraus nicht. Eine
garantiert atomare Preset-Umschaltung ist damit nicht belegt.

### 2. SPSC- und Host-Annahmen

`IConnectionPoint::notify` ist im lokalen SDK ausdrücklich als UI-Thread-
Methode dokumentiert (`ivstmessage.h:86`); `setActive` gehört in den Setup-
Zustand (`ivstcomponent.h:195`). Bei regelkonformen, nicht überlappenden
Aufrufen gibt es einen Erzeuger und zeitlich getrennte Verbraucher in
`Render` beziehungsweise `setActive`. Der Konsumentenwechsel erfolgt ohne
ausstehenden parallelen `Pop`.

Mehrere gleichzeitige `notify`-Erzeuger oder `setActive` parallel zu `process`
wären nicht sicher: nichtatomare Queue-Slots können konkurrierend benutzt
werden; bei Aktivierung konkurriert außerdem `Engine::Init` mit dem Renderer.
Die Atomics machen solche Host-Protokollverletzungen nicht sicher. Ein bloßer
Wechsel des Erzeugerthreads bei korrekt serialisierten Aufrufen ist etwas
anderes als mehrere gleichzeitige Erzeuger. Kein solcher DAW-Ablauf wurde
nachgewiesen.

Wenn ein aktiver Host keine Audioblöcke liefert, kann die Queue beliebig lange
voll bleiben. Nach 64 angenommenen Noten werden weitere verworfen; `notify`
meldet trotzdem Erfolg. Parameter-Flushes mit `numSamples == 0` leeren sie
nicht. Beim nächsten normalen Render werden alte Noten nachgeholt und die
Queue wieder frei; ein dauerhafter Defekt der Queue folgt daraus nicht.
`setActive` leert sie, das geerbte `setProcessing` allein nicht. Die Zusage
„nächster Block“ ist daher keine garantierte maximale Wartezeit in einem
suspendierenden Host. Das Verwerfen bei voller Queue ist wie beauftragt kein
eigener Befund.

### 3. Klaviatur-Nachrichten und MIDI

Controller und Processor begrenzen Notennummern; `notify` prüft Attribute und
Velocity 1–127. Ohne Host-Message-Allokation bricht `PlayNote` ab; ohne
Verbindung scheitert `sendMessage` ohne Audio. Der Fehler wird nicht an die
Oberfläche weitergegeben. Die Klaviatur benötigt deshalb Host-Unterstützung
für die Controller/Processor-Kommunikation.

Für gleiche Vorgeschichte, Parameter und dieselbe Reihenfolge gleichzeitiger
Anschläge benutzen Bildschirm- und MIDI-Note denselben Engine-Weg. Der
vorhandene Test belegt die frische Einzelnote bei 48 kHz; mehrere gleichzeitig
wartende UI-Noten werden vor allen MIDI-Events des Blocks abgearbeitet. Eine
andere Reihenfolge bei Voice-Stealing ist keine Bitgleichheitszusage.

In `Render` wird jeder Note-on-Offset auf `[position,numSamples]` begrenzt:
negative Offsets beginnen frühestens an der bereits erreichten Position;
unsortierte Events werden nicht rückwärts gerendert, sondern verspätet dort
ausgelöst. Zu große Offsets lösen den Anschlag erst nach dem letzten Sample
dieses Blocks aus, hörbar ab dem nächsten. Kein Buffer-Überlauf entsteht durch
diese Offsets. Noten außerhalb 0–127 sowie Velocity 0 werden ignoriert.
Note-off bleibt absichtlich wirkungslos. Velocity wird auf ganzzahlige MIDI-
Velocity quantisiert; beliebige VST3-Float-Velocities sind nicht verlustfrei.
NaN/Infinity oder enorme Velocity-Werte liegen außerhalb des Event-Vertrags;
die Rundung erfolgt vor der Validierung und wurde dafür nicht als robust
nachgewiesen. Die im Auftrag genannten gültigen MIDI-Fälle sind abgedeckt.

### 4. Parameter und Zustände

Die Reader lesen erst vollständig in lokale Variablen und veröffentlichen
danach Werte. Alle 36 echten Präfix-Abschneidungen des Processor-States wurden
zusätzlich abgelehnt. Versionen und die abschließende Keyboard-Byte-Länge des
Controller-States werden geprüft; Version 1 wird weiterhin gelesen, mit
eingeblendeter Klaviatur. Fehler ändern nicht erst einen Teil der Einstellungen.
Numerische Ausreißer werden auf die erlaubten Bereiche begrenzt. Ein
Format mit passender Version und zufällig passenden Nutzdaten ist mangels
Signatur/Prüfsumme nicht als „fremd“ oder beschädigt erkennbar; diese allgemeine
Integritätsgarantie liefern die Tests nicht.

`MasterPitchSemitones` entspricht für gültige Normalwerte exakt der lokalen
SDK-Formel `FromNormalized(value,48)` aus `futils.h:94`. Alle 49 Stufen und die
128 CC-Werte werden im Controller-Test mit der Anzeige verglichen. Eine
Umstellung auf gewöhnliches `round(value*48)` wäre hier eine Änderung der
VST3-Stufenregel.

Pitch Bend mit `kCanAutomate` und ohne eigenes gespeichertes Feld ist nicht
allein ein SDK-Verstoß: das Flag beschreibt Automation, keine automatische
Persistenz durch das Plugin. Es bietet bewusst eine Automationsspur an. Wenn
dies unerwünscht ist, wäre Flags 0 eine Produktentscheidung; `kIsHidden`
impliziert laut SDK Read-only und ist kein gleichwertiger Ersatz für einen
schreibbaren MIDI-Parameter. Zu beachten: State-Laden in dieselbe bereits
gebogene Instanz setzt den aktuellen Bend nicht zurück. Das unterscheidet
sich vom Laden in eine neue Instanz mit zentriertem Default und muss beim
DAW-Test berücksichtigt werden.

Pitch Wheel und CC 16/73/75 werden auf Bus 0 für sämtliche gültigen Kanäle
0–15 gleich zugeordnet; andere Busse und nicht zugeordnete CCs werden
abgelehnt. Die Textformatierung gültiger Werte entspricht den bestehenden
Tests; die Eingabeseite hat R03.

### 5. Klangteil und Tabellensicherheit

Für gültige Noten und 8–192 kHz sind Basisinkremente positiv und endlich. Bei
Pitch −26 bis +26 sind die Verhältnisse endlich. `ApplyPitch` setzt jedes
MIDI-Inkrement entweder unter 0.5 fort oder hält es bei etwa 0.49 und mutet
die Stimme. Der Divisor `base` ist im Guard-Zweig größer null. Die unbenutzten
16 Hardware-Stimmen bleiben inaktiv. Somit bleibt die erste Phase nach ihrem
einmaligen Wrap in `[0,1)`; auch die zweite Phase wird dort berechnet. Die
Tabelle besitzt den zusätzlichen Interpolationswert am Ende.

Zusätzlich lief ein ASan-Raster mit allen 128 Noten, initialem Pitch
−26/0/+26 und 8/11.025/16/22.05/32/44.1/48/88.2/96/192 kHz: 3840 Fälle,
jeweils 128 Samples nach dem Anschlag, mit aufeinanderfolgender Belegung des
Voice-Pools. Alle MIDI-Inkremente lagen unter 0.5; kein ASan-Tabellenzugriff
außerhalb des Speichers. Die vorhandenen Debug-Tests prüfen außerdem laufende
Glides bis +24. Dies ist ein Raster plus Codeargument, keine erschöpfende
Messung sämtlicher kontinuierlichen Sampleraten und Pitch-Verläufe.

Pitch-Snap beendet die Annäherung bei den geprüften Raten und gültigen
Zielwerten. Die Hüllkurven enden nach einer festen Samplezahl; Volume zum
Ziel null snappt vor einer denormalen Restfahne. Ein denormalbedingtes
Laufzeitproblem wurde nicht nachgewiesen oder auf der CPU vermessen. R02 und
R07 begrenzen die Aussagen zu exakter Rückkehr und allgemeiner Sprungfreiheit.

### 6. Fenster, Lambdas und Größenlogik

Der SDK-`EditorView` hält den Controller per `IPtr`; ein normal lebender Editor
hält damit die in `createView` eingefangene Controller-Instanz am Leben.
Bei regulärem `removed` stoppt `VSTGUIEditor` den Timer, dann löscht `close`
die Views und nullt alle geliehenen Control-Zeiger. Der Timer wird beim
Destruktor freigegeben; die Windows-Timerimplementierung entfernt ihn aus
ihrer Callback-Tabelle. Für den gewöhnlichen UI-Thread-Ablauf wurde kein
nachträglicher Lambda-Zugriff gefunden. R01 betrifft den übersprungenen Abbau.

`close` setzt `settle_pending_` zurück und meldet eine noch ausstehende
Änderung. `open` setzt `applied_zoom_` auf 1 und baut sämtliche Control-Zeiger
neu auf. Das reicht für die normale Wiederöffnung; `drag_` wird nicht explizit
zurückgesetzt und verliert seinen alten Corner-Zustand erst nach der Pause.
Mehrere korrekt an- und abgemeldete Views werden in `editors_` getrennt
gehalten. Ihre lokalen Fenster-Einstellungen werden bei Änderungen nicht
sofort gegenseitig synchronisiert; der zuletzt meldende Editor bestimmt den
gespeicherten Wert. Kein Speicherfehler folgt daraus allein.

`onSize` ruft nicht erneut `Resize` auf. Der direkte Kreis
`Resize` → `resizeView` → `onSize` hat daher keinen eigenen Endlos-
Rekursionsweg. Ein bloßer `checkSizeConstraint`-Aufruf verändert die
Fenster-Einstellungen nicht. Positive, unerwartete Größen werden in `onSize`
angenommen und der Zoom aus der kleineren Dimension bestimmt; dabei gilt
R04 für zu kleine Rechtecke. Gültige DPI-Skalierung wird getrennt vom
Nutzerzoom berücksichtigt; die 50–200-%-Grenzen werden auf den Nutzerzoom
angewendet. Vor `open` wird die neue Skalierung beim nächsten `getSize`
berücksichtigt. Nach `open` gilt zusätzlich R05.

`DragConstraint` ist eine Zeitheuristik, keine Erkennung echter Gesten:
zwei verschiedene Ziehvorgänge innerhalb von 250 ms können einen Corner-
Zustand teilen; eine Pause innerhalb derselben Geste kann ihn verwerfen.
Auch Hosts, die die jeweils andere Dimension vorab anpassen, können als
Corner eingestuft werden. Der aktuelle Test modelliert nur die dokumentierte
Anfragefolge und eine neue Geste nach einer Sekunde. Das Host-Verhalten dazu
ist noch offen; eine verlässliche gestengenaue Erkennung ist nicht belegt.

### 7. Klaviatur und Build

Die Trefferprüfung nutzt halboffene Rechtecke; Schwarz wird zuerst geprüft,
rechte/untere Außenkanten gehören nicht zur Klaviatur. `Press` verhindert
wiederholte Anschläge derselben gehaltenen Taste, ein neuer Mausklick setzt
diese Sperre zurück. Verlassen und Wiedereintritt können erneut anschlagen;
Mouse-up und Cancel setzen Dragging und Highlight zurück. VSTGUI transformiert
Mauskoordinaten invers zum Container-Zoom (`cviewcontainer.cpp:1038`, `:1108`),
bevor `NoteAtPoint` den View-Ursprung abzieht. Die Ereigniskette wurde am Code,
nicht durch manuelle Mausbedienung geprüft.

`ToggleButton` erbt ausschließlich von `CView`; der eigene Code ruft weder
`beginEdit` noch `performEdit` auf. Damit wurde kein Weg gefunden, bei dem
seine Nicht-Parameter-ID an den Host geht.

Die generierten MSVC-Projekte markieren SDK-/VSTGUI-Includepfade mit
`/external:I` und ExternalWarningLevel 0, die eigenen Source-/Core-Pfade
bleiben normale Includes. Die Shell behält `/W4`. `SYSTEM TRUE` betrifft die
Weitergabe der SDK-Includes, nicht pauschal alle Consumer-Quelldateien.
SDK-eigene Übersetzungen können weiterhin SDK-Warnungen erzeugen; solche
Warnungen traten auf, keine eigenen Plugin-Warnungen im erfolgreichen Build.

Die Projekte verwenden `/MT` beziehungsweise `/MTd`. `dumpbin /DEPENDENTS`
der Release-VST3 zeigt Windows-System-DLLs und keine MSVC-CRT-DLL-Abhängigkeit.
`moduleinfo.json` enthält Version 0.3.0 und die zu `plugin_ids.h` passenden
Klassen-IDs. Validator und Moduleinfo werden im Build ausgeführt;
Plugin-Links in Installationsordner sind abgeschaltet.

`moduleHandle = nullptr` in der Test-EXE ersetzt nur das fehlende DLL-Symbol.
Die EXE prüft keine echte DLL-Initialisierung, Factory-Registrierung oder
Plattform-Ressourceninitialisierung. Diese Lücke wird teilweise durch den
Validator am echten Bundle geschlossen; für GUI-Initialisierung reicht er
nicht. Die Zusatzsonde initialisierte VSTGUI für ihr eigenes HWND ausdrücklich.

Die vier Core-Git-Blobs stimmen mit den in `core/README.md` genannten Hashes
überein. Ein zusätzlicher Abruf des Daisy-Seed-Repos erfolgte nicht.

### Durchgeführte Builds und Tests

| Konfiguration | Build | Validator am Plugin | CTest |
| --- | --- | --- | --- |
| VS 2026, Release | bestanden | 47 bestanden, 0 fehlgeschlagen | 2/2 bestanden |
| VS 2026, Debug | bestanden | 47 bestanden, 0 fehlgeschlagen | 2/2 bestanden |
| VS 2022, Release | bestanden | 47 bestanden, 0 fehlgeschlagen | 2/2 bestanden |

Verwendet wurden die vorhandenen Configure-, Build- und Test-Presets, beim
Build zusätzlich `--parallel 4`. Der erste Sandbox-Build scheiterte am MSVC-
Programmdatenbankdienst mit C1902; dieselbe Konfiguration lief außerhalb der
Sandbox erfolgreich. Dies war kein Befund am Plugin-Code.

### Jede Zeile der README-Tabelle „Verified“

| README-Zeile | Aussage | Ergebnis dieser Prüfung |
| --- | --- | --- |
| 159 | Validator Release/Debug, 47/0 | Für VS 2026 erneut bestätigt. |
| 160 | Build, Validator und Tests auch VS 2022 | Release erneut bestätigt; kein VS-2022-Debug-Preset vorhanden oder ausgeführt. |
| 161 | Firmware-Kerntest mit MSVC | In allen drei oben genannten Konfigurationen bestanden. |
| 162 | Bitgleich, sieben Blockgrößen, 32/64 Bit | Test besteht; Umfang ist enger als die Formulierung, siehe R08. |
| 163 | A4 bei vier Sampleraten | Genau diese vier Raten und die Frequenzschranke werden getestet; bestanden. |
| 164 | Master Pitch und Bend, Frequenzfehler <0.01 % | Die acht Master-Pitch- und fünf Bend-Kombinationen bestehen. Keine erschöpfende Frequenzmessung jeder Kombination; +26 ist im zusätzlichen Sicherheitsraster, nicht in der Frequenzmessung. |
| 165 | Pitch-/Volume-Änderung ohne Waveform-Sprung | Die vorhandenen beiden konkreten Fälle bestehen; die allgemeine Aussage ist durch R07 eingeschränkt. Der Volume-Test prüft nur die Fahrt nach null, nicht R02. |
| 166 | Oberhalb Nyquist bei vier Raten | Vorhandene Tests bestehen; Debug und ASan-Raster liefern zusätzliche Sicherheit. Kein Beweis für beliebige ungültige Eingaben. |
| 167 | State-Roundtrip, beschädigte Daten abgelehnt | Roundtrip, leere/falsche Version und Controller-Abschneidung bestätigt; zusätzlich alle Processor-Präfixe. Beliebige Beschädigung mit noch gültigem Format wird nicht erkannt. |
| 168 | Parametertexte und MIDI-Map | Gültige Beispiele/Map bestehen; ungültige Texte fehlen im Test und zeigen R03. |
| 169 | Editorhost bei 120 % wie abgebildet | Historische Sichtprüfung/Skriptaufnahme; nicht erneut ausgeführt. Kein Beleg allein durch den Unit-Test. |
| 170 | Drei per Skript erzwungene Fenstergrößen | Historische Skriptprüfung nicht wiederholt. Code kann größere fremde Seitenverhältnisse auffüllen, aber keine uneingeschränkte Einpassgarantie, siehe R04. |
| 171 | Größenregel und monotone Corner-Drags | Geometrietests bestehen. Echte Host-Gesten werden nicht getestet; Grenzen der 250-ms-Heuristik siehe oben. |
| 172 | Fenster-State einschließlich 0.2.0 | Entsprechende Tests für Version 1/2, Grenzen und Fehler bestehen. |
| 173 | Resize-Kosten mit Median/Perzentil | Historische Messwerte, nicht durch CTest belegt und hier nicht nachgemessen. Keine Aussage zur aktuellen CPU-/DAW-Laufzeit daraus abgeleitet. |
| 174 | Klaviaturgeometrie, Schwarztasten oben | Geometrietest besteht; Zeichnung und Hit-Test benutzen `KeyRectFor`. Native Mausereignisse/Zoom werden dadurch nicht getestet. |
| 175 | Bildschirmnote erreicht Engine, bitgleich zu MIDI am Blockanfang | Einzelfall frischer Instanz bei 48 kHz bestätigt. Keine vollständige Prüfung aller Parameter, Queue-Zustände oder gemischter Anschlagreihenfolgen. |
| 176 | Beleuchtete Tasten, KEYS und Fenster in Editorhost | Historische Skriptaufnahme nicht wiederholt; keine automatische Ereignisprüfung im Projekttest. |
| 177 | Studio One 0.1.0 lädt, Regler reagieren | Wie ausdrücklich bezeichnet ein Bericht des Bedieners. Ergänzend bestätigt der Auftraggeber Starten und Spielen mit Tastatur in Studio One 7; von Codex nicht selbst ausgeführt. |

## Fehlende Tests

1. **Lebensdauer:** tatsächliches `attached`/`removed`, Wiederöffnung derselben
   View, mehrere Views, letzter `release` ohne `removed`, Fehler von `open`.
   ASan muss auch spätere Parameter-/State-Updates einschließen. Das hätte R01
   gefunden; reine Pointer-Registrierungstests reichen nicht.
2. **Volumeglättung:** Fahrten zu und von Unity, auch von oberhalb Unity,
   sämtliche unterstützten Raten und hoher Gain. Endliche Zeit bis zum exakt
   erreichten Ziel prüfen, nicht nur ungefähren Pegel. Das findet R02.
3. **Texteingaben:** leer, Whitespace, `foo`, Zahl mit Rest, `nan`, `+inf`,
   `-inf`, normale Grenzwerte. Rückgabecode, endlichen Normalwert und
   unveränderten Ausgabewert bei Fehler prüfen. Das findet R03.
4. **Host-Doppel für Resize:** ablehnen/akzeptieren; `getSize` während
   `resizeView`; native Geometrie vor `onSize`; KEYS am Mindestzoom;
   DPI-Meldungen vor/nach `open`; wiederholtes `onSize`; State-Laden bei
   geöffneten Views. Das findet R04/R05. Der jetzige `TestEditorResize`
   instanziiert keinen Editor und kann diese Fälle nicht sehen.
5. **Queue:** voller Zustand mit ausbleibenden Audioblöcken, Parameter-Flush
   ohne Audio, `setProcessing(false/true)`, Wiederaufnahme, überlappender
   Erzeuger während des Leerens und ein festes Verarbeitungsbudget. Dies
   konkretisiert R06 und die Latenz-/Verwerfungsgrenzen.
6. **Firmware-Klangtest:** Pitchfahrt über die Oberton-Grenze in beide
   Richtungen, einschließlich hoher Stimmen und Retrigger/Voice-Stealing
   nahe der Grenze. Das findet R07; in Daisy-Seed ergänzen.
7. **Audio-/MIDI-Robustheit:** unsortierte und außerhalb des Blocks liegende
   Offsets, ungültige Note-on-Daten, mehrere UI- und MIDI-Noten gleichzeitig,
   Nyquist-Grenzübertritt unter zusätzlichem Bend und Rückkehr aus dem Mute.
8. **State:** jede Abschneidung auch des Controller-States, NaN/Infinity und
   Bereichsausreißer als Binärdaten, Ladefehler bei offenen Views, Bend vor
   und nach Laden in dieselbe Instanz. Falls eine atomare Preset-Umschaltung
   gefordert wird, braucht sie einen geeigneten Nebenläufigkeitstest.
9. **Aussageumfang:** Für R08 fehlen die sieben 64-Bit-Blockgrößen. Die
   historischen GUI-/Performance-Zeilen brauchen eigene reproduzierbare
   Ergebnisprotokolle; sie werden durch bestandene CTests nicht bestätigt.

## Nicht geprüft

- Keine DAW gestartet; keine Hörprüfung, keine Bewertung von Farben,
  Anordnung, Schärfe oder sonstiger Gestaltung.
- Die historischen Editorhost-Screenshots und Resize-Benchmarks wurden nicht
  wiederholt. Die verborgenen HWNDs dienten ausschließlich der Prüfung von
  Host-Aufrufreihenfolge, Geometriezustand und Lebensdauer.
- Keine vollständige Heap-/Systemcall-Instrumentierung des Audio-Threads,
  keine CPU-Lastmessung, kein ThreadSanitizer und kein Nachweis beliebiger
  reentranter oder gleichzeitig gegen den SDK-Vertrag laufender Host-Aufrufe.
- Kein VS-2022-Debug-Build und keine anderen Betriebssysteme/Architekturen.
- Kein erneuter GitHub-Abgleich: der lokale Commitvergleich und die Angabe
  des Auftraggebers dienten als Grundlage. Der Submodul-Arbeitsbaum wurde
  gebaut; dessen Git-Status war wegen gesperrter `.git/modules`-Metadaten
  nicht vollständig lesbar.
- `dist/` und die installierte VST3-Kopie wurden nicht angefasst. Es wurden
  weder Code noch bestehende Dokumentation oder Tests verändert, nichts
  committet, gepusht oder als GitHub-Workflow ausgeführt. Die Wegwerf-Sonden
  und Build-Artefakte unter `.work/` und `build/` wurden nach der Prüfung
  entfernt; dauerhaftes Ergebnis ist ausschließlich diese Datei.
