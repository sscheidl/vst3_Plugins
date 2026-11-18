# Nachprüfung Messier 0.1.1 · 18. November 2026

**KEINE FREIGABE für den DAW-Test.** Bei der Projektübernahme kann die neue Datei mit dem alten Freeze-Wert spielen. Beim Schließen gehen Reset-Drucke verloren. Beim Wiederherstellen des Controller-Zustands kann eine Speicher-Ausnahme bis zum Host gelangen. Alle drei Befunde wurden in einem fensterlosen Release-Prüfstand beobachtet.

Geprüft wurde der uncommittete Worktree auf Branch `messier-0.1.0` gegen `26b85096d83974d113b4dff3000f57cc378c085e`, einschließlich der drei zu Beginn unversionierten Review-Dokumente. Die Pfade bei den Befunden beziehen sich auf `instruments/messier/`.

| Altbefund | Stand | Ergebnis |
| --- | --- | --- |
| MS-R01 · Speicherfehler | offen | Die getesteten Loader-Fälle bestehen in Release. Die Ausnahmegrenze im Controller bleibt unvollständig (MS-R06). Zur Reserve siehe MS-R10. |
| MS-R02 · Datei und Projektwerte | offen | Der sequenzielle Rückstautest besteht. Bei gleichzeitig laufender Audiowiedergabe ist die Veröffentlichung weiterhin teilbar (MS-R07). |
| MS-R03 · Reset | offen | Zwei schnelle Drucke können ankommen, wenn zwischen den Übergängen Audioblöcke verarbeitet werden. Schließen und weitere Drucke während der Haltezeit verlieren Ereignisse (MS-R08). |
| MS-R04 · Projektänderung | offen | Bei einzeln abgefragten Ergebnissen stimmt die Anzahl. Die Zustellung hängt am Fenster und mehrere Erfolge können zu einer Meldung zusammenfallen (MS-R09). |

### MS-R06 [P1] Speicher-Ausnahme verlässt den Controller

**Im Release-Bau bewiesen.** In `source/controller.cpp:104` umfasst der Schutz nur das Lesen. Danach aktualisiert der Controller Parameter und ruft in `source/controller.cpp:119` die Anzeige auf. Schon die Pfadkopie in `source/editor.cpp:554` kann Speicher benötigen. Auch die anschließende Statusanforderung in `source/controller.cpp:205` erzeugt eine Nachricht. Im Prüfstand war ein Editor-Objekt beim Controller registriert, ohne ein Fenster zu öffnen. Bei gültigem Zustand und fehlendem Speicher nach dem Lesen des Pfads erreichte eine Ausnahme den Aufrufer von `setComponentState`. Ein Hostabbruch wurde nicht beobachtet. **Korrektur:** Die vollständige VST3-Methode absichern, auch die Anzeigeaktualisierung und die Statusanforderung. Einen darstellungsbedingten Fehler von einem unlesbaren Projekt unterscheiden.

### MS-R07 [P2] Projektdaten erreichen den Audiothread in zwei Schritten

**Mit kontrollierter Thread-Pause bewiesen.** Der Lader bietet die Datei in `source/processor.cpp:455` an. Erst die nächste Zeile veröffentlicht die Werte. In der Zwischenzeit behandelt `process()` die Projektdatei wie eine gewöhnliche Fenster-Datei (`source/processor.cpp:401`). Auch bei vorliegenden Werten prüft `AdoptPendingProject` die Projektkennung des Dateizeigers nicht (`source/processor.cpp:143`). Der einzige Dateiplatz kann inzwischen ein neueres Angebot enthalten. `setActive(true)` greift ebenfalls getrennt auf Werte und Datei zu (`source/processor.cpp:168`, `source/processor.cpp:178`). Die bekannte Vorrangregel bei einer gleichzeitigen Fensterauswahl löst schon den Fall ohne Fensterauswahl nicht.

Für die Nachstellung pausierte ausschließlich der ladende Thread in einer temporären Kopie zwischen den beiden Veröffentlichungen. A hatte 101 Frames und Freeze aus, B hatte 202 Frames und Freeze an. Während der Pause spielte Datei B mit 202 Frames und **Freeze aus**. Nach Fortsetzung spielte B mit Freeze an. Ein uninstrumentierter Stresslauf allein entdeckte die Lücke nicht. Der vorhandene Test in `tests/test_plugin.cpp:942` ruft den Audioblock erst nach Rückkehr von `setState` auf.

**Korrektur:** Projektwerte und Dateibesitz zusammen veröffentlichen und dasselbe vollständige Projekt in `process` und `setActive` übernehmen. Fenster-Dateien getrennt behandeln. Bei Rückstau das vollständige Angebot behalten. Ein dauerhaftes Warten auf eine bestimmte Kennung ist im aktuellen Code nicht erkennbar, weil die Übernahme keine passende Kennung abwartet.

### MS-R08 [P2] Schließen und mehrere Drucke verlieren Reset-Ereignisse

**Mit der SDK-Parameterwarteschlange bewiesen.** Bei einem Druck und anschließendem `close()` vor dem nächsten Block kamen **0 von 1** Resets an. Das sofortige Loslassen überschreibt den Druck an derselben Sampleposition. Beim Schließen mit vorgemerktem zweiten Druck kam **1 von 2** an. Drei schnelle Drucke bei offen bleibendem Editor ergaben **2 von 3**. Die Vormerkung ist ein einzelnes Bit und wird beim Schließen gelöscht (`source/editor.cpp:442`, `source/editor.cpp:497`). In diesen Fällen waren die tatsächlich begonnenen und beendeten Host-Edits paarig. Der vorhandene Test verarbeitet vor dem Schließen einen Block und zählt die Edit-Paare nicht (`tests/test_plugin.cpp:892`, `tests/test_plugin.cpp:1006`).

**Korrektur:** Jeden angenommenen Fensterdruck unabhängig von Fensterlebensdauer und Timer bis zur Verarbeitung festhalten. Die Flankensemantik der Automation für Parameter 121 erhalten und Doppeltrigger bei der Rückmeldung des Hosts vermeiden.

### MS-R09 [P2] Eine erfolgreiche Fensterauswahl kann ohne Dirty-Meldung bleiben

**Aus dem Kontrollfluss belegt, ohne separaten Lauf.** Ein unmittelbarer Status kann noch „loading“ melden. Beendet sich der Ladevorgang nach dem Schließen des letzten Editors, zählt der Lader den Erfolg (`source/file_loader.cpp:166`), aber der Fenster-Timer fragt ihn nicht mehr ab (`source/controller.cpp:260`). Bis zur nächsten Statusantwort erfährt der Host nichts von der Änderung. Treffen mehrere Erfolge zwischen zwei zugestellten Statusständen ein, setzt `source/controller.cpp:234` jede positive Differenz in genau einen Aufruf um. Der vorhandene Test fragt nach jedem Erfolg ausdrücklich ab (`tests/test_plugin.cpp:1031`).

**Korrektur:** Abschluss über einen vom Editor unabhängigen Weg auf dem passenden Host-Thread zustellen. Seit der letzten bestätigten Meldung hinzugekommene Erfolge einzeln berücksichtigen. Erfolgloses Laden, Projektwiederherstellung und wiederholtes Lesen desselben Status dürfen keine Meldung auslösen.

### MS-R10 [P2] Zugriff auf die Reserve ist nicht durchgehend synchronisiert

**Im Code belegt, Laufzeitfolge nicht nachgestellt.** Bei leerem Projektpfad ruft `LoadNow` nach Verlassen seiner Sperre `TakeEmpty` auf (`source/file_loader.cpp:112`). Schlägt die reguläre Anlage fehl, bewegt dieses die gemeinsame Reserve (`source/file_loader.cpp:74`). Gleichzeitig kann ein zuvor gestarteter Fensterauftrag in `Finish` dieselbe Reserve prüfen oder auffüllen (`source/file_loader.cpp:140`). Auch ein überholter Auftrag erreicht die Auffüllprüfung. Der konkurrierende Zugriff auf denselben `unique_ptr` ist dann nicht synchronisiert. **Korrektur:** Jede Entnahme und Auffüllung unter derselben Loader-Sperre ausführen. Ein für die Übergabe entnommenes Ersatzobjekt bis zur Veröffentlichung behalten.

Der Ersatzweg in `Finish` ist wörtlich nicht allokationsfrei. Die kurze Fehlermeldung passt bei MSVC in den vorhandenen String, aber `TakeEmpty` versucht zuerst eine neue Allokation. Auch das Auffüllen versucht eine Allokation. Beide Versuche fangen Fehler ab. Der Release-Test zeigt Entnahme und spätere erneute Nutzung der Reserve nach Speichererholung. Die im Auftrag akzeptierte Grenze bei anhaltend leerer Reserve bleibt davon getrennt.

### 16-Byte-Test und Übergabe an Claude

**Die Beschränkung des globalen 16-Byte-Tests ist für die verwendete MSVC-STL technisch begründet.** Der tatsächliche Schalter in `tests/test_loader.cpp` ist der Iterator-Debug-Level. Beim installierten MSVC 14.51 verwendet der voreingestellte Dateisystempfad einen String. Dessen Standardkonstruktor ist für den Standardallokator `noexcept` (`xstring:758`), seine Initialisierung ruft `_Alloc_proxy` auf (`xstring:907`). Mit Iterator-Debugging wird dafür ein Proxy mit zwei Zeigern angelegt, auf x64 also 16 Bytes. Eine Ausnahme an dieser Grenze kann der äußere Leser nicht auffangen. Im ausgelieferten Release-Bau entfällt diese Proxy-Allokation. Daraus folgt keine allgemeine Aussage über die Robustheit der eigenen Fehlerbehandlung im Debug-Bau.

**Offener Teilauftrag für Claude:** Nach der wiederholten App-Meldung wurden weitere Sonderprüfungen zum Speicherfehlerfall auf Nutzerwunsch beendet. Die Meldung selbst benennt die auslösende Stelle nicht. Als abgegrenzte Übergabe bleibt die Bewertung der Debug-Abdeckung von MS-R01 offen. Bitte anhand von Code und vorhandenen Logs beurteilen, welche Zusicherung für die Reserve im Debug-Bau dokumentiert werden kann, und die Dokumentation mit den tatsächlich geprüften Konfigurationen abgleichen. Der Auftrag steht hier zur Übernahme. Die Ergebnisse der bereits abgeschlossenen Release-Prüfung gelten unabhängig davon.

### Verifikation und Grenzen

* Eigener Windows-x64-Release-Bau mit VS 2026, MSVC 19.51. Alle 16 CTest-Tests bestanden. Steinbergs Validator meldete für jedes der vier Bundles 47 bestandene und 0 fehlgeschlagene Tests. Die regulären Messier-Tests einschließlich Firmwarevergleich und Loader-Test liefen erfolgreich.
* Alle 42 Einträge in `out/messier-0.1.1/SHA256SUMS.txt` und alle 81 Einträge in `SOURCE_SHA256SUMS.txt` stimmten mit den Dateien im Paket bzw. dem geprüften Worktree überein. Das bestätigt die aufgelisteten Inhalte. Ein bitidentischer Neubau wurde nicht geprüft.
* Auf dem geänderten Audiopfad waren keine neue dynamische Allokation, Löschung oder Mutex-Sperre zu erkennen. Der eigene Stresslauf zählte null reguläre C++-Allokationen in `process`. Die Sonde misst weder Freigaben noch Sperren direkt.
* Fehlende Regressionstests betreffen die geteilte Veröffentlichung auch bei Aktivierung, das Schließen zwischen Druck und Audioblock, die Dirty-Zustellung ohne Fenster und die gemeinsam benutzte Reserve. Der Debug-Bau, VS 2022 und ASan wurden in dieser Nachprüfung nicht erneut ausgeführt.
* MS-R05 ist wie vereinbart zurückgestellt. README, Planung, Changelog und DAW-Prüfliste kennzeichnen das Fenster als vorläufig. Es gab keine Fenster-, DAW- oder Hörtests.
