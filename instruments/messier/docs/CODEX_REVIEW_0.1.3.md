# Nachprüfung Messier 0.1.3 · 18. November 2026

**KEINE FREIGABE für den DAW-Test.** Scheitert das Dekodieren eines gültigen WAVs am Speicher, kann Messier das neue Projekt mit Stille übernehmen und dessen Pfad speichern. Nach dem Austausch des Processors können erfolgreiche Dateiauswahlen ohne Dirty-Meldung bleiben. Beide Fälle wurden im fensterlosen Release-Prüfstand nachgestellt. Die reguläre Messier-Testsuite und der Validator bestanden.

Geprüft wurde der anfangs saubere Worktree `.work/messier/wt` auf Branch `messier-0.1.0` bei Commit `160a4786f7f395be9981083af52e188d1bee8931` gegen `381f06a`. Bei der Abschlusskontrolle lagen bereits uncommittete Änderungen für die Engine-Anzeige von 0.1.4 vor. Das Urteil gilt für 0.1.3 bei `160a478`. Pfade zu Messier-Dateien sind relativ zu `instruments/messier/`.

| Altbefund | Stand | Ergebnis |
| --- | --- | --- |
| MS-R11 · Ausnahmen bei Parametertexten | behoben | Beide Textschnittstellen und ihre Formatierungs- und Parse-Funktionen sind abgesichert. Weitere Host-Einstiegspunkte bleiben offen (MS-R19). |
| MS-R12 · Projektänderung ohne Statusantwort | offen | Der ursprüngliche Fall ist korrigiert. Die Float-Übertragung ist exakt für alle 65.536 kodierten Werte. Nach Processor-Wechsel kann der alte Zählerstand neue Erfolge unterdrücken (MS-R17). |
| MS-R13 · Reset und Reaktivierung | offen | Die Zusammenfassung im internen 64-Sample-Raster ist sinnvoll und dokumentiert. Ein Druck übersteht die direkte Reaktivierung, kann aber nach einem Aufruf mit null Samples weiterhin verloren gehen (MS-R18). |
| MS-R14 · Projektzustand bei Speichermangel | offen | Wenn der Lader kein Dateiobjekt zurückgibt, bleiben gespeicherter Pfad und Soundstatus erhalten. Ein Speicherfehler beim WAV-Lesen kann jedoch als akzeptierte stille Datei durchgehen (MS-R16). |
| MS-R15 · Editor-Objekt geht bei Fehler verloren | behoben | Die Statusanzeige nach erfolgreicher Konstruktion fängt ihre eigenen Ausnahmen ab. Damit fällt der verlorene Besitzübergang aus 0.1.2 weg. |

### MS-R16 [P2] Speicherfehler beim WAV-Dekodieren führt zu einem akzeptierten Projekt mit Stille

**Im Release-Ladertest bewiesen.** Der WAV-Leser gibt bei einem fehlgeschlagenen Konvertierungspuffer `kTooLarge` mit „out of memory“ zurück (`source/wav_reader.cpp:77`, `:173`, `:245`). Der gleiche Fehlercode steht auch für eine Datei oberhalb des konfigurierten Limits. `LoadProject` behandelt den Speicherfehler wie jede unlesbare Datei. Wenn Texte und leeres Dateiobjekt noch Platz haben, übernimmt der Lader den neuen Pfad und Soundstatus (`source/file_loader.cpp:142`). `setState` lehnt nur ab, wenn überhaupt kein Dateiobjekt zurückkommt. Das leere Objekt wird daher mit den neuen Projektwerten veröffentlicht. `getState` verwendet den neuen Loader-Pfad (`source/processor.cpp:524`, `:502`).

Im Zusatztest war das alte Projekt mit 4.800 Frames geladen. Beim Laden eines zweiten, gültigen WAVs gleicher Größe scheiterte der Konvertierungspuffer. Der Lader gab ein Dateiobjekt mit **0 Frames** zurück, änderte den Pfad und meldete `too large: out of memory`. Ohne Speicherknappheit lieferte dasselbe WAV 4.800 Frames. Der vorhandene Ladertest prüft den Ausfall des Konvertierungspuffers für Fensterdateien und die Ablehnung eines Projekts bei umfassenderem Speichermangel. Er deckt die Kombination nicht ab (`tests/test_loader.cpp:424`, `:449`).

Der Reader braucht einen eigenen Fehlercode für Speichermangel, auch für eine fehlgeschlagene PCM-Allokation. `LoadProject` sollte bei diesem Ergebnis vor dem Übernehmen von Pfad, Soundstatus und Projektwerten abbrechen. Für eine wirklich fehlende oder unlesbare Datei kann die bestehende stille Wiederherstellung mit gespeichertem Pfad bleiben. Changelog und Planung versprechen die vollständige Ablehnung bei Speichermangel. Das README erklärt die Regel für Projektladungen bisher nicht ausdrücklich.

### MS-R17 [P2] Derselbe Controller zählt Dateiladungen eines neuen Processors zunächst nicht

**Fensterlos im Release-Bau bewiesen.** `CountUserLoads` verwirft rückläufige Zählerwerte und lässt dabei `user_loads_counted_` unverändert (`source/controller.cpp:334`). Auch bei neu verbundener Gegenstelle verwenden Parameter 199 und Statusantwort diesen alten Zählerstand. Dadurch gelten die ersten Ladeerfolge des neuen Processors als alt, bis er den vorigen Stand überschreitet.

Im Zusatztest meldete ein Controller zwei erfolgreiche Dateiauswahlen. Danach wurde er mit einem frisch erzeugten Processor verbunden und erhielt dessen Status mit Zähler null. Trotz Weiterreichen von Parameter 199 und erfolgreicher Statusabfragen ergaben die ersten beiden neuen Dateiauswahlen **0** Dirty-Aufrufe. Erst die dritte Auswahl ergab **1**. Der Test belegt den Fall für einen Host, der den Controller beim Austausch des Processors weiterverwendet. Welche DAWs das tun, war nicht Teil der Prüfung. Der reguläre Test trennt vorübergehend nur den Rückweg zum selben Processor und bestätigt dort korrekterweise die Zustellung ohne Statusantwort und das Ausbleiben einer Doppelmeldung.

Den gezählten Stand an die Identität des Processors binden und beim Wechsel anhand seiner ersten gültigen Meldung initialisieren. Veraltete Antworten derselben Verbindung weiterhin ignorieren. Für stark gebündelte Zustellung zusätzlich die Begrenzung beachten. Die 16-Bit-Normierung selbst rundet korrekt, aber die Differenzlogik verwirft Sprünge ab 32.767 und kürzt selbst den vollen Statuszähler auf 16 Bit (`source/controller.cpp:326`, `:339`).

### MS-R18 [P3] Ein Aufruf mit null Samples kann einen Reset vor der Aktivierung aufbrauchen

**Fensterlos im Release-Bau bewiesen.** `process` nimmt den Druck aus dem Zähler und legt ihn in den Adapter, bevor es die Samplezahl prüft. Bei null Samples rendert der Adapter nicht (`source/processor.cpp:467`, `:481`). Die darauffolgende Aktivierung startet die Engine neu und leert über `StreamAdapter::Reset` die Adapter-Warteschlange (`source/processor.cpp:200`, `:225`, `adapter/stream_adapter.cpp:62`). Eine Aktivierung ohne diesen Zwischenaufruf übersteht der Druck bereits.

Im Zusatztest blieb unmittelbar nach einem Druck und `process(0)` genau ein Adapter-Ereignis übrig. Nach der unmittelbaren Reaktivierung und dem ersten 64-Sample-Block waren **0 Resets** ausgeführt. Für Aufrufe mit null Samples den Druck im Processor belassen. Wenn die Zusicherung auch bereits eingeplante Drucke umfasst, diese bis zur tatsächlichen Ausführung bei einem Neustart erhalten. Die Zusammenfassung innerhalb desselben ungerenderten DSP-Abschnitts bleibt klanglich sinnvoll.

### MS-R19 [P1] Initialisierung und Ratewechsel haben weiterhin offene Ausnahmegrenzen

**Im Code belegt, ohne zusätzlichen Test für diese Aufrufe.** `Processor::initialize` legt Busse an und bereitet den Adapter vor (`source/processor.cpp:60`). Das SDK legt etwa in `external/vst3sdk/public.sdk/source/vst/vstaudioeffect.cpp:37` einen AudioBus mit `new` an. `setupProcessing` und der Ratewechsel-Zweig von `setActive` rufen ebenfalls `StreamAdapter::Prepare` auf. Diese Methode vergrößert dynamische Puffer (`source/processor.cpp:102`, `:211`, `adapter/stream_adapter.cpp:53`). Eine Speicher-Ausnahme kann aus diesen VST3-Aufrufen gelangen. Auch die SDK-Factory ruft die Konstruktoren über einen ungefangenen Callback auf. Der Processor-Konstruktor legt unter anderem die Loader-Reserve an und startet den Worker (`source/processor.h:43`, `source/file_loader.cpp:49`, `external/vst3sdk/public.sdk/source/main/pluginfactory.cpp:229`).

Die neun geprüften Aufrufe decken diese seit früheren Versionen bestehenden Stellen nicht ab. Die Host-Einstiegspunkte sollten einen geeigneten Fehler zurückgeben und bei gescheiterter Vorbereitung keinen halb umgestellten Adapter hinterlassen. Bei der Factory sollten fehlgeschlagene Erzeugungen über den vorgesehenen Fehlerpfad zurückkehren.

### Geprüft ohne weiteren Befund

* Eigener Messier-Release-Bau mit VS 2026, MSVC 19.51, Windows x64. Alle 6 Messier-Tests bestanden. Steinbergs Validator meldete 47 bestandene und 0 fehlgeschlagene Prüfungen. Die reguläre Suite umfasst den Firmwarevergleich, den Rückstau bei Projektladungen und die neun vorgesehenen Host-Aufrufe.
* Die Absicherung der Parametertexte greift auch ohne geöffneten Editor. Die für den Host sichtbaren Rückgaben sind bei intern abgefangenen Fehlern ein leerer Text bzw. ein erfolgloser Parse-Versuch. Das sind keine ungefangenen Ausnahmen. `ShowFileStatus` umfasst die gesamte potenziell fehlschlagende Anzeigeaktualisierung, sodass `MakeEditor` danach den Zeiger zurückgeben kann.
* Zusatzprüfung der Normierung. Alle 65.536 Werte wurden von `SampleChangesValue` als `float` gespeichert und mit `SampleChangesCount` wieder exakt dekodiert. Das prüft die numerische Übertragung. Das Vorhandensein eines Ausgabeparameterpfads hängt weiterhin vom Host ab.
* Alle 48 Paketdateien und alle 87 Quelldateien der SHA-256-Listen stimmen mit dem Paket bzw. dem geprüften Stand überein. Laut Build-Info entstand das Paket vor dem Commit aus dem damaligen Worktree. Ein binär identischer Neubau war kein Prüfkriterium.

### Fehlende Tests und Prüfgrenzen

Als Regressionstests fehlen das Scheitern ausschließlich des WAV-Lesens mit anschließendem Speichern über den Processor, eine neue Processor-Verbindung am bestehenden Controller, Reset über `process(0)` und Reaktivierung sowie die Ausnahmegrenzen der Lebenszyklusaufrufe. VS 2026 Debug, VS 2022 Release, die komplette Testsuite für alle vier Plugins und ASan wurden anhand der bereitgestellten Logs beurteilt. Der eigene Bau beschränkte sich auf Messier Release. Fenster-, DAW- und Hörtests fanden nicht statt. Das für später vorgesehene Atlas/Orion-Fenster bleibt wie dokumentiert vorläufig.
