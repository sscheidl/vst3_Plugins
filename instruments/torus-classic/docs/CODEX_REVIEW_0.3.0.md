# Codex Review: Kekulé 0.3.0

VERDICT: KEINE FREIGABE

Geprüft: uncommittete Arbeitskopie im Worktree `.work/kekule-0.3.0/wt`,
Branch `kekule-0.3.0`, HEAD/Basis
`490eca8357a7386b98cab0d4b2da2918edcc9f79`, 2026-11-15.
Compiler: Visual Studio 2026 Build Tools, MSVC 19.51.36256.0,
Windows x64, Release und Debug, DSP/Adapter/Tests mit `/fp:precise`.

TC-R10 ist behoben. Die regulären fensterlosen Tests bestehen in beiden
Konfigurationen. Drei neue P2-Befunde betreffen die Tail-/Ruhemeldung:
ungeeignete Setup-Metadaten, eine vorzeitig abgeschlossene Tastenübergabe
und nicht berücksichtigte MIDI-Freigaben. Sie sind vor einer Freigabe zu
korrigieren. TC-R14 (P3) betrifft eine zu weit gehende Zusage zur
Stimmenverdrängung nach einem Wechsel der Stimmenzahl.

Die Befunde sind keine Beobachtungen aus Studio One. Nachgewiesen wurden
die API-Rückgaben und Zustände am Code und mit eigenen Host-Stellvertretern;
die Folgen einer darauf gestützten Host-Optimierung sind jeweils als solche
gekennzeichnet.

## Geprüfter Arbeitsstand

Der Worktree hatte zu Beginn 34 geänderte versionierte Dateien sowie den
neuen Prüfauftrag. Alle 176 vorhandenen versionierten bzw. nicht ignorierten
Dateien dieses Stands wurden vor und nach der Prüfung per SHA-256 verglichen;
keine davon wurde während der Prüfung verändert. Hinzu kam nur dieser Bericht.

SHA-256 von `instruments/torus-classic/components.json`, **vor und nach der
Prüfung identisch**:

```text
dc2f84a934467dad9b217ce119d2b7785e2a5558fa38c28620cdcb8cf81f6d8f
```

Zusätzlicher Fingerabdruck des geprüften Dateisatzes: SHA-256 der
UTF-8-kodierten, nach Pfaden sortierten JSON-Abbildung relativer Pfade auf
ihre SHA-256-Werte, ohne zusätzliche Leerzeichen:

```text
599e8a2c3d64ff99ac849e6896fc4fd39477666e7950084ba8494a226634f61b
```

Alle folgenden Produktpfade beziehen sich auf `instruments/torus-classic/`
dieses Worktrees. SDK-Pfade beziehen sich auf `external/vst3sdk/` im
Haupt-Checkout; dessen SDK wurde nur als Build-Abhängigkeit gelesen.

## Nachprüfung TC-R10

| ID | Ergebnis | Beleg | Rest |
| --- | --- | --- | --- |
| TC-R10 | **behoben** | `source/engine_bridge.h:101`: `ControlEvent(Control::Tune, value)` delegiert an `TuneEvent(double(value))`. `tests/test_adapter.cpp:1206` prüft Payload, tatsächlich ankommenden Tune-Wert, Zusammenlegen im Quantum, Ersetzung bei voller Queue und freies Stimmen. Der Performance-Score benutzt ausdrücklich `TuneEvent`. Release und Debug bestanden. | Float-Aufrufer liefern weiterhin den auf Float gerundeten Eingangswert; der Plugin-Prozessor behält seinen Double-Weg. Keine neue Abweichung gefunden. |

Die Erkennungswirkung wurde zusätzlich geprüft: In einer temporären Kopie
von `engine_bridge.h` wurden ausschließlich die zwei Zeilen der neuen
Tune-Delegation entfernt. `TestTuneEvents()` scheitert damit wie erwartet
an `helper.data == EngineBridge::kTuneStepOffset && helper.value == 0.5f`
(Exitcode 1). Mit dem Produktstand besteht derselbe Test. Dies ist eine
gezielte Rücknahme dieser Korrektur, kein vollständiger Build von 0.2.2.

Der Wechsel der undurchsichtigen Queue-Testmarkierungen von Tune auf
Structure schwächt ihre Kapazitäts-/Reihenfolgeprüfung nicht. Der neue
Test mit echter Engine prüft zusätzlich, was die Markierungen zuvor nicht
zeigen konnten: die Bedeutung der Tune-Payload beim Empfänger.

## Neue Befunde

| ID | Schwere | Bereich | Befund | Beleg (Datei:Zeile) | Auslösung und Auswirkung | bewiesen/vermutet | minimale Korrektur |
| --- | --- | --- | --- | --- | --- | --- | --- |
| TC-R11 | **P2** | VST3-Tail-Vertrag | `getTailSamples()` wird als dynamischer Ruhe-Indikator benutzt und meldet im Setup 0 für ein Instrument mit langen bzw. endlosen Ausklängen. Eine spätere erneute Abfrage durch den Host ist nicht zugesichert. | `source/processor.h:56`; SDK `pluginterfaces/vst/ivstaudioprocessor.h:349`, `public.sdk/source/vst/testsuite/processing/processtail.cpp:58` | Ein Host liest den Tail einmal vor dem Spielen und behält 0 für Export/Optimierung. Die spätere Änderung auf unendlich erreicht ihn nicht; Ausklänge können abgeschnitten werden. Gegenüber dem festen unendlichen Tail von 0.2.x ist dies ein zusätzliches Risiko. | **API-Verlauf bewiesen**, konkrete Abschneidung in einer DAW nicht getestet | Für dieses Instrument konservativ `kInfiniteTail` als Tail-Metadatum beibehalten. Exakte Nullen und korrekte Silence-Flags unabhängig davon weiter liefern; eine hostabhängige Nap-Optimierung erst mit belegtem Vertrag verwenden. |
| TC-R12 | **P2** | Veröffentlichung des Ruhezustands | Ein Anschlag gilt in den Zählern bereits als erledigt, bevor er an Adapter/Engine übergeben und der neue Audiozustand veröffentlicht wurde. Zusätzlich wird der Produzentenzähler erst nach Veröffentlichung des Queue-Eintrags erhöht. | `source/processor.cpp:121`, `:268`, `:375`, `:429`; `source/processor.h:65` | Beim ersten Anschlag nach Ruhe kann der UI-Thread `AtRest() == true` / Tail 0 lesen, während der Audiothread die gerade entnommene Note noch verarbeitet. Im zweiten Fenster steht bereits ein Anschlag in der Queue, den die Zähler noch nicht zeigen. Eine darauf gestützte Suspend-Entscheidung kann Arbeit übersehen. | **bewiesen**, mit gesteuerten zulässigen Unterbrechungen | Die gesamte Übergabe einschließlich laufender Verarbeitung als beschäftigt veröffentlichen. Eine entnommene Note erst zusammen mit dem daraus entstandenen Audio-/Adapterzustand als erledigt bestätigen; eine konsistente Abfrage über Produzent, Queue und Audiozustand herstellen. Stärkere Speicherordnung allein schließt diese Zeitfenster nicht. |
| TC-R13 | **P2** | MIDI-Ausgang / Ruhe | Offene Host-Noten und geschuldete Note-offs gehen nicht in die Ruhemeldung ein. Sie meldet deshalb „nichts wartet“, obwohl noch ein MIDI-Off zugestellt werden muss. | `source/processor.cpp:253`, `:258`, `:375`, `:420`; `source/processor.h:65` | Nach einem erfolgreichen Screen-Note-on, Reaktivierung und einem Block ohne aufnahmefähige Ausgangsliste bleibt das Note-off geschuldet. Trotzdem meldet der Prozessor Ruhe/Tail 0. Stoppt ein Host daraufhin die Aufrufe, bleibt die nachgeschaltete MIDI-Note offen. | **Ruhe trotz ausstehendem Off bewiesen**; tatsächliches Einschlafen eines Hosts nicht getestet | Den MIDI-Ausgangszustand in den veröffentlichten Beschäftigt-/Ruhezustand aufnehmen. Konservativ keine Ruhe, solange beim Host noch Screen-Noten offen sind oder Offs ausstehen; auch Reaktivierung und UI-Freigaben berücksichtigen. |
| TC-R14 | **P3** | Stimmenverteilung / Dokumentation | Nach einer Erhöhung der Stimmenzahl stimmt die uneingeschränkte Zusage „die am längsten nicht angeschlagene Stimme“ vorübergehend nicht. Der globale Bankzeiger wird neu ausgerichtet, die internen Viererzeiger bleiben erhalten. | `engine/synth.cpp:82`, `:98`; `engine/synth.h:14`; README „Voices“; PLANUNG „Verteilung der Noten“ | Mit 8 Stimmen neun Anschläge, dann auf 16 erhöhen und weitere Anschläge: Eine jüngere Note wird ersetzt, während eine ältere in einer anderen Bank erhalten bleibt. Kein Überlauf, Absturz oder Überschreiben eines noch nicht gerenderten Triggers nachgewiesen. | **bewiesen** | Entweder die Alters-/Fortsetzungsposition über die Erhöhung erhalten oder die begrenzte Abweichung nach Stimmenwechseln ausdrücklich dokumentieren. Die derzeitige globale Altersgarantie ist zu weitgehend. |

### TC-R11: Setup-Tail ist keine aktuelle Pegelanzeige

Eigener fensterloser Host: `setupProcessing`/Aktivierung, einmal Tail lesen,
danach Note 57 mit Velocity 127, weitere Blöcke ohne neue Note:

```text
CACHED_TAIL: setup=0 while_sounding=4294967295
energy_0.1_to_0.2s=7.07515332
```

Der zu Beginn gelesene Wert beschreibt den nachfolgenden Ausklang nicht.
Das SDK beschreibt die Tail-Abfrage für den UI-Thread im Zustand „Setup
Done“ und ihre Verwendung für Offline-Verarbeitung und das Vermeiden von
abgeschnittenem Signal. Es verlangt kein Polling nach jedem Audioblock.
Sein eigener Tail-Test liest den Wert im Setup und speichert ihn; der
mitgelieferte Wrapper übernimmt `kNoTail` ebenfalls beim Einrichten
(`public.sdk/source/vst/basewrapper/basewrapper.cpp:476`). Das ist ein
konkreter Beleg für die Annahme eines gecachten Werts, keine Aussage über
Studio Ones Implementierung.

Die grünen Validator-Ergebnisse widerlegen dies nicht: Der SDK-Tail-Test
setzt für die Kategorien Instrument/Generator `mDontTest` und überspringt
seinen eigentlichen Tail-Verarbeitungstest. Die eigene `TestRest`-Prüfung
fragt dagegen fortlaufend ab und setzt damit genau die Hoststrategie voraus,
deren allgemeine Gültigkeit hier fehlt.

Ein dynamischer Rückgabewert verursacht an sich keinen Datenzugriffsfehler;
die Atomics erlauben das Lesen. Das Problem ist seine nicht abgesicherte
Bedeutung für den Host. Ein fixes `kInfiniteTail` ist bei einem potenziell
endlos ausklingenden Resonator ein zutreffendes konservatives Metadatum.
Die Behauptung, es schließe Nap grundsätzlich aus, ist durch die vorliegenden
Studio-One-Quellen nicht belegt.

### TC-R12: die beiden Übergabefenster

Die Zusatzprobe erzeugte eine temporäre Kopie von `processor.cpp` mit
Wartepunkten unmittelbar nach erfolgreichem Queue-Push und unmittelbar nach
`keys_taken_.fetch_add`. Kein Produktalgorithmus wurde verändert; die
Wartepunkte steuern nur Thread-Unterbrechungen.

1. Der UI-Produzent veröffentlicht den Queue-Eintrag und hält vor der
   Erhöhung von `keys_queued_` an. Der dokumentiert von jedem Thread lesbare
   `AtRest()`-Getter meldet weiterhin true. Die Queue enthält schon die Note.
   Diese Probe benutzt für die Abfrage den anderen Thread, nicht einen
   zweiten gleichzeitigen Notify-Produzenten.
2. Noch aussagekräftiger für den öffentlichen SDK-Weg: `notify()` läuft auf
   dem UI-Thread vollständig durch; die Zähler sind zunächst 1/0, Ruhe false.
   Der Audiothread entnimmt die Note, erhöht `keys_taken_` auf 1 und hält vor
   der weiteren Verarbeitung an. Auf dem UI-Thread liefern nun sowohl
   `AtRest()` als auch `getTailSamples()` fälschlich Ruhe. Nach Fortsetzung
   des Audioblocks klingt genau diese Note.

```text
PUBLISHED_KEY: AtRest=1 before queued counter catches up
TAKEN_BUT_UNRENDERED: AtRest=1 tail=0
AFTER_AUDIO_BLOCK: AtRest=0 triggers=1 energy=0.327262391
```

Bei der zweiten Folge entsprechen Notify, Tail-Abfrage und `process()`
ihren jeweiligen SDK-Threadzuordnungen. Es ist kein zweiter UI-Produzent
und kein paralleler Lifecycle-Aufruf erforderlich. Die Unterbrechung
erzeugt kein verlorenes Audio bei einem Host, der einfach weiterrechnet;
sie widerlegt die behauptete zuverlässige Ruhemeldung für eine
Suspend-Entscheidung. Ihre Häufigkeit in einer DAW wurde nicht gemessen.

### TC-R13: Ruhe trotz nachzuholendem MIDI-Off

Diese Probe braucht keine Instrumentierung und keine Thread-Unterbrechung:

1. Screen-Key 60 anschlagen; der Host nimmt `N60` erfolgreich an.
2. Deaktivieren und aktivieren. Die Engine ist wieder still; der Host muss
   seine noch offene Note im nächsten Block freigegeben bekommen.
3. Einen Audioblock ohne MIDI-Ausgangsliste verarbeiten. Die Freigabe kann
   nicht zugestellt werden und bleibt im Prozessor geschuldet.
4. `AtRest()` ist trotzdem true, Tail ist 0.
5. Mit einer wieder vorhandenen Liste kommt im nächsten Block `F60` heraus.

```text
OWED_MIDI_RELEASE: AtRest=1 tail=0 with N60 still held by host
NEXT_AVAILABLE_LIST: F60 delivered
```

Die zuverlässige spätere Zustellung aus TC-R08 funktioniert also weiter,
**wenn weitere Audioblöcke kommen**. Die neue Ruhemeldung darf den Host
nicht gerade vor dieser ausstehenden Arbeit zum Anhalten einladen. Dass
Studio One Screen-Messages eventuell nicht zum Aufwecken benutzt, ist eine
zusätzliche bekannte Hostgrenze und behebt diesen Widerspruch nicht.

### TC-R14: konkrete Notenfolge

Alle Noten sind unterschiedliche MIDI-Töne; die Zahlen sind ihre Tonhöhen:

```text
8 Stimmen: 41,42,43,44,45,46,47,48,49 anschlagen und abarbeiten.
Bank 0 hält danach unter anderem 49 (Anschlag 9), Bank 1 noch 45 (Anschlag 5).
Auf 16 erhöhen. 50..60 anschlagen und abarbeiten.
Als Nächstes 61 anschlagen.
Ergebnis: 49 wird durch 61 ersetzt; die ältere 45 bleibt erhalten.
```

Der zusätzliche Bank-Beobachter bestätigte diese Folge. Er prüfte außerdem
bei 612440 tatsächlich ausgeführten Anschlägen mit wechselnden Modellen,
4/8/16 Stimmen, vollen Queues und raschen Wechseln: je Bank und Quantum
höchstens vier Trigger, `Part::polyphony()` stets 4, endliche Samples,
anschließend vollständig geleerte Note-Queues. Die Vier-Trigger-Grenze ist
von der Altersgarantie zu trennen; sie blieb eingehalten.

## Ohne Befund geprüft

### Bänke, DSP-Pfad und Lebensdauer

* Der vendorte `core/` einschließlich `Part` ist gegenüber `490eca8`
  unverändert. Dasselbe gilt für Sinc-Kernel und Adapter-Zeitrechnung,
  `config.h`, `parameter_manager.h`, `audio_output.h`, Factory und Klassen-IDs.
* Mit einer Bank bleiben Trigger-, Patch-, Performance-, Fade- und
  Output-Reihenfolge des bisherigen Pfads erhalten. Hinzu kommen
  Buchführung/Ruheprüfung und die ausdrücklich dokumentierten Nullen in
  Ruhe. Die pauschale Formulierung „jede Anweisung wie vorher“ ist deshalb
  auf den eigentlichen DSP-Pfad zu beziehen.
* Die vier `Part`s verwenden dieselben unveränderlichen Tabellen, aber
  getrennte DSP-Zustände und getrennte Rauschzustände. `InitResources()`
  läuft weiterhin einmalig im Konstruktor über `call_once`, nicht aus
  `ResetBank`. Die neuen Seeds sind deterministisch. Kein gemeinsam
  veränderter Bankzustand im geprüften Pfad gefunden.
* `Part::Init()` setzt `polyphony_` auf 4; keine neue Stelle ändert das.
  Damit bleiben die Akkordtabellenindizes und `64 / polyphony_ - 4`
  innerhalb ihres bisherigen Bereichs. Die Hook-Probe bestätigt die Vier
  zusätzlich bei allen beobachteten Triggern.
* `strike_` wird bei jedem Anschlag modulo `4 * banks_` gehalten und nach
  Verkleinerung korrigiert. Es wächst nicht bis zum Integerüberlauf. Eine
  zusammenhängende Runde von höchstens `4 * banks_` Triggern trifft jede
  Bank höchstens viermal. Der separate Statistikzähler `triggers_` kann
  nach 2^32 Anschlägen umbrechen; sein Nullwert beeinflusst einmal die
  Abfrage „schon angeschlagen“ beim Erhöhen, aber nicht diese Speichergrenze.
* `Leaving`-Bänke werden weiter gerendert und ihr Gain sinkt pro Sample;
  wartende Trigger blockieren dieses Weiterrechnen nicht. Nach dem Reset
  ist die Bank wieder `Off`. Vorhandene Down-/Up-Wechsel innerhalb der
  Blende und die eigenen wechselnden Überlastfolgen leeren ihre Queues.
* Destruktor, Löschen des Speichers und Placement-new desselben nicht
  const-Arrayelements sind für die hier vorliegende Klasse zulässig.
  Gelöschte Copy-Operationen verhindern diese Rekonstruktion nicht.
  `Init`, Seed, Modell und `Bank{}` werden wiederhergestellt. Keine
  Allokation oder Sperre im neuen Reset-Pfad gefunden; die vorhandenen
  Allokationsproben bestehen auch für weggelegte Bänke. Ihr Zähler erfasst
  `operator new`, nicht sämtliche denkbaren Speicher-APIs.
* Modellwechsel markieren auch unbenutzte `Part`s. Deren erste spätere
  `Process()`-Ausführung konfiguriert die Resonatoren vor dem Rendern neu.
  Kein neuer Pfad mit Ausgabe alter Bankpuffer gefunden. Die zugehörige
  vorhandene Modellwechselprobe prüft Energie und Grenzen, keine
  erschöpfende Samplegleichheit aller Historien.

### Engine-Ruhe und Vergleich mit der Firmware

Die Ruheschwelle wird am Stereoausgang jeder Bank **vor** globaler Blende,
Volume und Kniekennlinie geprüft. Muting durch Volume 0 oder eine
Modellwechselblende kann den Quiet-Zähler daher nicht allein zum Abschluss
bringen. Ein wartender Trigger verhindert `Synth::AtRest()`. Ein Anschlag
ohne Erregung wird angenommen und hält die Engine zunächst wach.

Die Quiet-Prüfung behandelt NaN als nicht ruhig. Bank 0 verwendet nach
Erreichen der Ruhe die dokumentierte Hysterese bis 4e-9; die übrigen Bänke
werden nach 4000 ruhigen Quanten zurückgesetzt. Die erste Bank läuft
darunter weiter. Der gemessene FM-Dauerton nach Damping am Halteende wird
auch in den eigenen regulären Läufen wieder hörbar; Peak der Testfolge
0,338. Keine weitere konkrete unprovozierte Wiederkehr von hörbarem Signal
in den ausgeführten Folgen gefunden.

Die eine Sekunde entspricht 4000 Quanten zu 12 Samples bei 48 kHz und liegt
deutlich über der im Auftrag genannten 2048-Sample-Verzögerung. Das ist eine
plausible Beobachtungszeit, kein allgemeiner Beweis über alle rekursiven
DSP-Zustände, Parameterfolgen oder Auslöschungen zwischen Stimmen. Gemessen
wird die Bankausgabe, kein isolierter Pegelzähler jeder einzelnen Stimme.

Der strenge Enginevergleich gegen die gepinnte Firmware besteht: zunächst
1712712 Samples in den bisherigen Szenarien. Zusätzlich „through a rest“
in Release bei allen sechs Modellen, in Debug bei den vier kürzeren
Modellen: außerhalb der gemeldeten Ruhe alle Samples gleich, innerhalb
der Ruhe ausschließlich Nullen und Firmware-Rest kleiner als 1e-9. Auch
die folgenden Anschläge und der Rauschzustand stimmen. Größter gemessener
abgewichener Firmware-Rest dieser sechs Ruhefolgen: etwa 5,4e-10.

Der Pluginvergleich liefert in Release 5760000, in Debug 864000 gleiche
Samples. Einschränkung seiner Testausnahme siehe „Fehlende Tests“.
Die dokumentierte Zusage zur ersten Note nach Ruhe gilt für den weiter
gerechneten Vier-Stimmen-Pfad mit gleicher verstrichener Samplezahl. Ein
Host, der drei Sekunden keine Aufrufe macht, lässt auch Rausch-/LFO-Zeit
stehen; für alle Modelle ist damit keine Gleichheit zu drei Sekunden
Weiterrechnen zugesagt. Der vorhandene Nap-Vergleich beschränkt sich
entsprechend auf Modal Resonator.

### Adapter und Ausgangsstufe

`silent_` zählt den zusammenhängenden Null-Suffix der gerenderten Samples.
`Compact()` verwirft nur einen Präfix und hält den Suffix intakt. Wenn
`silent_ >= count_` gilt, besteht damit der gesamte noch gehaltene
Samplebereich aus Nullen. Reset füllt beide Puffer mit Nullen und setzt
den Zähler konservativ groß. Ein ausstehendes Event verhindert Ruhe.
Die vorhandenen Impuls-, Filter-, Reset-/Raten- und Blockgrößenprüfungen
bestehen. Diese Aussage über den Puffer ersetzt nicht den zusätzlich
erforderlichen Ruhezustand der Engine.

Die vier Filterintegratoren werden nur bei exaktem Stereo-Nulleingang und
einzeln unter 1e-9 liegenden Zuständen gelöscht. Bei offenem Filter wird
der Eingang weiterhin unverändert ausgegeben. Die neuen Filter-Ruhetests
bestehen bis 4 MHz einschließlich anschließender erneuter Anregung.
Der sehr kleine zusätzliche Zustandsverlust ist als Bestandteil des
Schwellenverfahrens vertretbar; dies ist keine Hörprüfung. Die zusätzlichen
Vergleiche/Nullzuweisungen arbeiten mit fester Grenze je Sample.

`Render()` rechnet am Code auch ohne beschreibbare Ausgangspuffer die
interne Zeit weiter; die Ruhespeicherung hängt an `numSamples > 0`, nicht
an vorhandenen Kanälen. Für den Fall fehlender Audiopuffer wurde keine
eigene neue Laufzeitprobe hinzugefügt. Null-Sample-Flushes sind dagegen
in den ausgeführten Tests enthalten: Parameterereignisse bleiben bis zum
Audioblock erhalten und beenden solange Ruhe. Bildschirmanschläge bleiben
bei einem Flush in ihrer Queue und behalten ihre Verfallsfrist.

### Parameter, Zustand und Fenster nur am Code

* Parameter 111 hat drei Einträge 4/8/16, `kIsList` ohne `kCanAutomate`,
  keinen zugeordneten CC. Bridge-Reglerindex 8 liegt innerhalb der
  vergrößerten neun Control-Kinds; Modell, Bend und Filter erhalten danach
  weiterhin getrennte Queues. Der Weg über Null-Sample-Flush, Audioblock,
  Zustandsladen und Aktivierung besteht in den regulären Tests.
* Die gemeinsamen Funktionen `PolyphonyIndex`, `PolyphonyNormalized` und
  `PolyphonyVoices` halten Controller, Anzeige, Bridge und State konsistent.
  `ParameterState` dimensioniert Werte und Slots mit `kParamCount`, den
  gespeicherten Teil mit `kSavedParamCount` und `kSavedIndex`.
* Version 3 umfasst elf Double-Werte; Version 2 liest zehn und ergänzt
  vier Stimmen. Version 1 konvertiert Damping/Tune-Free wie zuvor und
  ergänzt ebenfalls vier Stimmen. Drei gestufte Werte werden kanonisiert.
  Controller und Prozessor benutzen denselben Leser.
* Zusätzlich zu den regulären State-Tests wurden 389 abgeschnittene oder
  ungültige Zustände der Versionen 1/2/3 bei **beiden** Empfängern geprüft:
  Ablehnung ohne Änderung. Dazu alle Präfixlängen, NaN/±Inf und Werte
  außerhalb 0..1 je Feld. Ein V2-State mit elf Double-Werten behält vier
  Stimmen: das elfte Feld wird als überzähliges Byte-Material ignoriert,
  nicht als V3-Polyphonie gelesen. Das ist mit einem versionsgebundenen
  Leser vereinbar. Zusätzliche Stufengrenzen bestanden ebenfalls.
* Das Schloss zeigt `!TuneFree`; der Klick invertiert zurück.
  `SetLocked` aktualisiert Tooltip und Darstellung ohne Edit-Rückruf.
  `SetChoice` verfährt ebenso. Die aktiven Editorwege für Schloss und
  Stimmenwahl senden begin/set/perform/end; `ParameterChanged` sendet
  keine erneuten Edits. Die Beschriftungstabelle hat statische Lebensdauer.
* Beide neuen View-Zeiger werden beim Schließen über `ForgetViews()`
  gelöscht, bevor der Frame freigegeben wird. Nachfolgende
  Parameter-Rückmeldungen prüfen auf null. Tooltip-Texte folgen dem
  Schlosszustand, Tooltips sind am Frame aktiviert. Kein neuer
  Lebensdauer- oder Rückkopplungsfehler am Code gefunden.
* Die drei Ringfarben entsprechen der dokumentierten Zuordnung. Optische
  Wirkung, Lesbarkeit, Tooltips auf dem Bildschirm und praktische Bedienung
  wurden ausdrücklich nicht getestet.

## Messungen und ausgeführte Läufe

Alle Builds lagen außerhalb des Worktrees unter
`.work/torus-classic-review/kekule-0.3.0-review/build`. MSBuild und die
Testprogramme liefen mit automatisch genehmigter Ausführung außerhalb
der Sandbox. Keine installierte Kopie wurde verwendet oder verändert.

| Eigener Lauf | Release | Debug |
| --- | --- | --- |
| Kekulé-Build und Validator | PASS, 47 bestanden / 0 fehlgeschlagen | PASS, 47 / 0 |
| `torus-classic.engine` | PASS, 33,34 s | PASS, 86,54 s |
| `torus-classic.adapter` | PASS, 32,80 s | PASS, 73,40 s |
| `torus-classic.provenance` | PASS, 0,17 s | PASS, 0,18 s |
| Eigener Runner: 15 unveränderte fensterlose Plugin-Funktionen | PASS | PASS |
| Gesteuerte Tail-/Tasten-/MIDI-Proben | TC-R11 bis TC-R13 wie beschrieben reproduziert | nicht wiederholt |
| Zusätzliche State-/Bank-Proben | PASS im beschriebenen Umfang; TC-R14 reproduziert | nicht wiederholt |
| Gezielte TC-R10-Rücknahme | neuer Test scheitert erwartungsgemäß | nicht wiederholt |

Der Plugin-Runner ruft ausschließlich `TestAgainstFirmware`,
`TestBlockSizes`, `TestNoteHandling`, `TestSampleRates`, `TestInstances`,
`TestParameterState`, `TestKeyboardQueue`, `TestKeyboardMidiOut`,
`TestPauseAndFlush`, `TestActivationAndRateChange`, `TestState`,
`TestPolyphony`, `TestRest`, `TestController`, `TestTuneRounding` auf.
`TestViewLayout`, `TestEditorResize` und `TestEditorInWindow` wurden
nicht aufgerufen; auch der vollständige Plugin-CTest wurde nicht gestartet.

Provenance: 73 Dateien stimmen mit Manifest/Hashliste überein, davon 57
unverändert und 16 gepatcht/abgeleitet; Version 0.3.0 ist konsistent.
Die regulären Compilerwarnungen stammen aus VSTGUI. Die separate Synth-Kopie
sieht zusätzlich die im Produktbuild unterdrückten Upstream-Konvertierungswarnungen.

### Rechenzeit, Pegel und Dokumentation

Der Release-Benchmark wurde nach Abschluss der übrigen Läufe mit 20 s
Audio je Zeile ausgeführt. Wie bei jeder Wall-Clock-Messung wirken
Scheduling und Taktzustand hinein; die Werte sind keine harte
Echtzeitgarantie. Eigene Werte bei 48 kHz:

| Stimmen | Anteil eines Kerns, je nach Modell |
| --- | --- |
| 4 | 0,83 bis 1,50 % |
| 8 | 1,58 bis 2,95 % |
| 16 | 2,88 bis 6,03 % |

Bei 192 kHz und 16 Stimmen lag die größte Zeile bei 7,12 %. Die
16-Stimmen-Einzelquanten lagen im Median bei 6,5 bis 12,6 µs,
99,9-%-Quantile bei 31,6 bis 57,6 µs. Einzelne beobachtete normale
Quanten dauerten bis 385,2 µs; das 250-µs-Quantum-Budget ist daher keine
belegte harte Worst-Case-Grenze. Modellwechselquanten mit 16 Stimmen:
Median 7,0 bis 22,0 µs, größter Messwert 95,9 µs. Beim Wechsel 16 → 4
war das Maximum der 40 Folgequanten einschließlich Bank-Resets je nach
Modell 33,6 bis 175,1 µs. Im Stereo-Strings-Lauf war dies größer als das
Maximum davor (55,7 µs). „Nicht länger als normale Schritte“ ist also
ein damaliges Messergebnis und keine zugesicherte Eigenschaft.

Die Speichermenge der Engine ist 445768 Byte, passend zu den gerundeten
446 kB der Dokumentation. Voller Engine-Reset: Median 12,9 µs,
Maximum 51,2 µs bei 2000 Wiederholungen. Die drei Bank-Resets lagen im
gemessenen Pfad innerhalb eines Quantum-Budgets; zusammen mit Code und
Allokationsprüfungen kein zusätzlicher konkreter Echtzeitdefekt
nachgewiesen. Ein nicht echtzeitfähiges Windows-Timing ist kein Beweis
für eine harte Zeitobergrenze.

Die im README gerundeten Ruhezeiten wurden reproduziert:

| Modell | Damping 0,25 | 0,5 | 0,75 | 1 |
| --- | --- | --- | --- | --- |
| Modal Resonator | 59,6 s | 6,9 s | 1,6 s | 1,1 s |
| Sympathetic Strings | 38,1 s | 38,1 s | 33,8 s | 33,6 s |
| Inharmonic Strings | 18,8 s | 7,3 s | 2,7 s | 2,2 s |
| FM Voice | 16,9 s | 5,0 s | 2,0 s | 1,3 s |
| Western Chords | 40,3 s | 38,1 s | 36,4 s | 29,9 s |
| Stereo Strings | 36,8 s | 18,1 s | 5,4 s | 2,2 s |

In Ruhe: 2,49 µs je Quantum, etwa 1,00 % eines Kerns. Die Behauptung,
dass die erste Bank weiterrechnet und dadurch selbst keine CPU spart,
ist bestätigt. Die Pegelprobe mit 16 lauten Noten bestätigt rund
0,61 bis 0,68 bei vier und 0,71 bis 0,77 bei sechzehn Stimmen vor dem
Limiter; Ausgangsgrenze 0,5 und Blockgrößenunabhängigkeit bestehen.

README, CHANGELOG, PLANUNG und DAW-Liste beschreiben 4/8/16, fehlenden
CC/Automationsflag, V3-State, Schloss und Farbzuordnung im geprüften Umfang
zutreffend, **mit den Ausnahmen TC-R11 bis TC-R14**. Die Installations-
und Staging-Änderungen führen zu keiner automatischen Installation im
Build; das Staging-Script wurde nur gelesen, nicht ausgeführt.

Die [PreSonus-Knowledge-Base](https://support.presonus.com/hc/en-us/articles/29252442184333-Studio-One-Pro-7-Managing-CPU-usage-and-overcoming-high-CPU-problems-on-your-system)
bestätigt die allgemeine Nap-Beschreibung und die Abschaltmöglichkeit pro
Plugin. Sie beschreibt weder VST3-Tail-Polling noch ein Wake-Protokoll
für Instrumente oder Bildschirmtasten. Daraus folgt keine Garantie für
die aktuelle Implementierung. Die beiden weiteren README-Links zu Sound
On Sound und KVR konnten bei dieser Prüfung wegen HTTP 403 nicht erneut
unabhängig nachgelesen werden; deren historische Aussagen sind hier nicht
neu bestätigt. Studio One wurde nicht geöffnet.

## Fehlende Tests

1. TC-R11: Host-Stellvertreter mit einmaliger Setup-Tail-Abfrage und
   nachfolgender Ausklang-/Exportprüfung; keine Annahme fortlaufenden Pollings.
2. TC-R12: Dauerhafte gesteuerte Konkurrenzfolgen vor dem Zählen eines
   veröffentlichten Anschlags und zwischen Entnahme und Abschluss seiner
   Verarbeitung. Der derzeitige `TestRest()` prüft die Tastenzähler nur
   sequentiell. Die Atomics verhindern Datenrennen, aber keine logisch
   vorzeitige Ruhe.
3. TC-R13: Ruhe gemeinsam mit offenen/geschuldeten MIDI-Offs testen,
   Ausgangsliste zeitweise null/voll, Reaktivierung und Laden eines Zustands.
   Die bestehenden MIDI-Tests prüfen spätere Zustellung bei fortlaufenden
   Aufrufen, nicht die Zulässigkeit einer vorherigen Ruhe-/Tail-Meldung.
4. TC-R14: Erhöhen an allen vier Phasen des internen Bankzeigers, danach
   gezielte Altersprüfung aller Stimmen. Die vorhandenen Tests beginnen
   überwiegend an Bankgrenzen und erkennen die gezeigte Übergangsfolge nicht.
5. `TestAgainstFirmware` im Plugin lässt jedes Paar „unser Sample 0,
   Firmwarebetrag < 1e-9“ durch (`tests/test_plugin.cpp:414`), ohne den
   tatsächlichen Engine-Ruhezustand dieses Samples zu prüfen. Die Ausnahme
   ist damit weiter als „nur in Ruhe“. Der Enginevergleich prüft den
   Ruhezustand dagegen ausdrücklich. Kein dadurch versteckter zusätzlicher
   Produktfehler nachgewiesen; den Plugin-Test an ein Zeit-/Ruheorakel binden.
6. Bankmodellwechsel nach allen Kombinationen von Off/On/Leaving stärker
   als nur per Energiefaktor prüfen. Für den vorhandenen Fall ist die
   Spanne 0,5 bis 2 kein Beleg exakter Gleichheit jeder Bank.
7. Keine erschöpfende Prüfung beliebiger DSP-Historien, langsamer
   Auslöschungs-/LFO-Folgen und aller Parameteränderungen nach Ruhe.
   Die ausgeführten Firmware-/Resttests sind konkrete endliche Szenarien.

## Nicht geprüft

* Keine Fenster-, GUI-, DAW- oder Hörtests. Die neuen Widgets wurden nur
  am Code geprüft. Keine automatische Aussage über tatsächliches Nap/Wake,
  Exportverhalten oder Darstellung in Studio One, Waveform oder Live.
* Kein VS-2022-, ARM-, ASan- oder TSan-Lauf. Keine vollständige erneute
  Lizenz-/Herkunftsprüfung gegen heruntergeladene Originalquellen; der
  unveränderte Kern und die lokale Provenance-Prüfung wurden kontrolliert.
* Keine Produktkorrekturen, keine Änderung vorhandener Tests oder Doku,
  kein Commit, Push, Branchwechsel oder Stash. Der Haupt-Checkout und
  `.work/elements-review/` wurden nicht verändert. Vorhandene `build/`,
  `dist/`, `out/` und installierte Plugin-Kopien blieben unberührt.
* Der selbst erzeugte Prüfbereich einschließlich Builddateien, Proben und
  Logs unter `.work/torus-classic-review/kekule-0.3.0-review/` wurde nach
  Abschluss entfernt. Die wesentlichen Reproduktionen und Messergebnisse
  sind oben festgehalten. Im Worktree bleibt nur diese neue Ergebnisdatei.
