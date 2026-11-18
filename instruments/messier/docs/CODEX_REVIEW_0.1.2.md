# Nachprüfung Messier 0.1.2 · 18. November 2026

**KEINE FREIGABE für den DAW-Test.** Die Projektübergabe im Audiopfad und die Sperre der Dateireserve sind korrigiert. Eine ausgebliebene Statusantwort kann aber weiterhin die Speichern-Meldung für einen erfolgreichen Samplewechsel unterdrücken. Außerdem bleiben Ausnahmegrenzen für vom Host aufgerufene Parametertexte offen.

Geprüft wurde der anfangs saubere Worktree `.work/messier/wt` auf Branch `messier-0.1.0` bei Commit `381f06a1f9fb335f9d65aff52bf92cd6a02ece96`. Als Basis diente `26b8509` samt dem Bericht zu 0.1.1. Die folgenden Pfade sind relativ zu `instruments/messier/`, sofern anders angegeben.

| Altbefund | Stand | Ergebnis |
| --- | --- | --- |
| MS-R06 · Ausnahme an der Host-Grenze | offen | Der konkrete Ablauf über `setComponentState` und die Editoranzeige ist abgesichert. Zwei Host-Aufrufe für Parametertexte bleiben offen (MS-R11). |
| MS-R07 · Datei und Projektwerte | behoben | Die Engine übernimmt nur passende Projektkennungen. Der alte Zustand bleibt während der Übergabe spielbar. Ein zusätzlich geprüfter älterer Fensterauftrag verdrängt das neue Projekt nicht. Der Fehlerpfad beim Speichern ist gesondert in MS-R14 beschrieben. |
| MS-R08 · Reset | offen | Die Nachricht überlebt das Schließen. Die wirksame Zusammenfassung richtet sich auch nach dem internen 64-Sample-Raster. Reaktivierung verwirft wartende Drucke sofort (MS-R13). |
| MS-R09 · Dirty-Meldung ohne Fenster | offen | Parameter 199 ist korrekt deklariert. Bei erfolgreicher Statusantwort meldet der Controller auch mehrere Ladeerfolge einzeln. Für einen ausgefallenen Abruf fehlt die Wiederholung (MS-R12). |
| MS-R10 · Zugriff auf die Reserve | behoben | Entnahme, Auffüllen und Abfrage laufen unter der Loader-Sperre. Der Konstruktor legt die Reserve an, bevor der Worker startet. |

### MS-R11 [P1] Parametertexte können eine Ausnahme an den Host weitergeben

**Im Code belegt.** `source/controller.cpp:47` und `source/controller.cpp:51` formatieren bzw. parsen Parametertexte mit dynamischen Strings. Die geerbten SDK-Einstiegspunkte `getParamStringByValue` und `getParamValueByString` rufen diese Methoden direkt auf. Das SDK fängt an dieser Stelle keine Ausnahme ab (`external/vst3sdk/public.sdk/source/vst/vsteditcontroller.cpp:89` und `:101`). Ein Host kann diese Aufrufe für seine generische Parameteranzeige oder Werteingabe auch ohne Messier-Fenster benutzen. Scheitert die Konvertierung durch eine Ausnahme, erreicht sie den Host. Der eigene Release-Lauf bestätigt die sechs vorhandenen Fälle. Sie enthalten die beiden Textschnittstellen jedoch nicht (`tests/test_plugin.cpp:1189`). Ein zusätzlicher Sonderlauf zur Speicherausnahme wurde nicht durchgeführt.

Die beiden VST3-Einstiegspunkte im Controller absichern und für Fehler ein VST3-Ergebnis zurückgeben. Beide Aufrufe unabhängig von einem Editor-Objekt prüfen.

### MS-R12 [P2] Eine ausgebliebene Statusantwort kann die Speichern-Meldung dauerhaft verschlucken

**Fensterlos im Release-Bau bewiesen.** `source/processor.cpp:450` merkt den Zähler als gemeldet, sobald der Host die Parameteränderung für den Block annimmt. `source/controller.cpp:161` fragt daraufhin den Status genau beim Wertwechsel ab. Bleibt die Antwort aus, behält der Controller dennoch den neuen normierten Wert des Parameters. Der identische Zähler löst beim Processor keine weitere Ausgabe aus. Ohne Fenster kann die Speichern-Meldung bis zur nächsten Auswahl oder Statusabfrage ganz ausbleiben.

Im Zusatztest kam Parameter 199 für eine erfolgreiche Dateiauswahl einmal an. Für diesen Abruf blieb der Rückweg der Statusnachricht unverfügbar. Nach Wiederherstellen der Verbindung meldeten zehn weitere Blöcke weder Parameter 199 noch `setDirty`. Ein manueller Statusabruf löste sofort genau eine Dirty-Meldung aus. Der Fall unterscheidet sich von der dokumentierten Host-Grenze, weil das Host-Double Parameter 199 tatsächlich weiterreichte.

Die Dirty-Meldung unabhängig von der Textstatusantwort anhand eines ausreichend großen, fortlaufenden Zählers auslösen oder einen offenen Abruf bis zur erfolgreichen Zustellung wiederholen. Auch bei gebündelter Zustellung darf der Zähler keine Erfolge verschlucken. Der aktuelle Wert wiederholt sich nach 1024 Erfolgen (`source/parameters.h:92`). Die Verarbeitung einer Statusantwort kappt außerdem Differenzen bei 1024 (`source/controller.cpp:296`).

### MS-R13 [P3] Reset wird auch über Host-Blockgrenzen hinweg zusammengefasst

**Fensterlos im Release-Bau bewiesen.** Der Message-Pfad umgeht Host-Automation und Fensterlebensdauer wie vorgesehen. Der Processor nimmt Drucke pro Host-Aufruf auf (`source/processor.cpp:466`). Der Adapter ordnet sie anschließend internen DSP-Abschnitten zu und fasst Ereignisse am selben Quantum zusammen (`adapter/stream_adapter.cpp:149`). Bei 48 kHz lösten zwei in getrennten Ein-Sample-Blöcken entgegengenommene Drucke mit anschließendem Rendern nur einen Reset aus. Ein unmittelbar vor der Reaktivierung gesendeter Druck löste ebenfalls keinen Reset aus, weil `setActive(true)` den Zähler löscht (`source/processor.cpp:219`). Der vorhandene Test rendert 64-Sample-Blöcke und prüft den Zeitablauf ohne Reaktivierung (`tests/test_plugin.cpp:990`, `:1016`).

Die Zusammenfassung innerhalb derselben DSP-Position ist klanglich vertretbar, da beide Drucke dort dieselbe Aktion auslösen. Changelog und Bedienbeschreibung sollten das interne Raster sowie das sofortige Verwerfen bei Reaktivierung nennen. Falls unterhalb von 0,5 Sekunden auch über Reaktivierung hinweg eine Auslösung zugesichert wird, den Druck bis zum nächsten verarbeitbaren Block behalten.

### MS-R14 [P2] Ein abgelehnter Projektwechsel kann den gespeicherten Pfad verändern

**Im Code belegt.** Wenn bei leerem Projektpfad sowohl die Anlage des leeren Dateiobjekts als auch die Reserve fehlen, liefert `TakeEmptyLocked` null. Trotzdem leert `LoadProject` den Pfad und markiert den Status als „keine Datei“ (`source/file_loader.cpp:129`). `setState` antwortet dann mit `kOutOfMemory` und reicht das abgelehnte Projekt nicht an die Engine weiter (`source/processor.cpp:523`). Ein nachfolgendes `getState` verwendet aber den geänderten Loader-Pfad (`source/processor.cpp:501`). Ein Benutzer kann so ein noch spielendes Sample beim nächsten Speichern aus dem Projekt verlieren. Die Auslösung setzt anhaltenden Speichermangel und eine bereits verbrauchte Reserve voraus. Sie wurde nicht separat ausgeführt.

Pfad und Soundstatus erst übernehmen, wenn tatsächlich ein Dateiobjekt für das Projekt vorliegt. Bei `kOutOfMemory` den zuletzt gültigen Speicherzustand erhalten und den fehlgeschlagenen Versuch getrennt melden.

### MS-R15 [P3] Die Editor-Erstellung kann bei einem Anzeigefehler ein Objekt verlieren

**Im Code belegt.** `MakeEditor` legt ein Editor-Objekt an und hält danach nur einen Rohzeiger. `ShowFileStatus` kann bei der Pfadkopie weiteren Speicher benötigen (`source/controller.cpp:337`, `source/editor.cpp:514`). Wirft sie, gibt `createView` null zurück, ohne das gerade angelegte Objekt freizugeben. Den Editor bis zur erfolgreichen Rückgabe mit einem besitzenden SDK-Zeiger halten oder den initialen Status getrennt behandeln.

### Geprüft ohne weiteren Befund

* Eigener Release-Bau für Messier mit VS 2026 und MSVC 19.51. CTest meldete 6 von 6 Messier-Tests. Steinbergs Validator meldete 47 von 47. Im eigenen Build gab es Warnungen in externem VSTGUI-Code.
* Projektdatei und Werte erreichen die Engine auch dann als Paar, wenn Audioblöcke zwischen ihren Veröffentlichungen laufen. Der vorhandene Test deckt die Aktivierung in der Lücke ab und der Rückstautest 400 sequenzielle Ladungen. Im zusätzlichen Reihenfolgetest folgten auf eine wartende Fenster-Datei zwei Projektladungen ohne Audioblock dazwischen. Das jüngste Projekt mit 202 Frames blieb aktiv. Eine danach fertig gewordene Fenster-Datei mit 88.217 Frames wurde übernommen. Bei gemäß SDK auf dem UI-Thread serialisierten `setState`-Aufrufen besteht kein dauerhaftes Warten, solange der Audiothread und der Rückgabesammler weiterlaufen und das aktuelle vollständige Projekt veröffentlicht wurde. Parallele Aufrufe von `setState` untereinander fordert die SDK-Schnittstelle nicht.
* Parameter 199 ist verborgen, schreibgeschützt, nicht automatisierbar und von der Liste gespeicherter Parameter ausgeschlossen. Der Processor ignoriert 199 als Eingang. Die vorhandenen Tests bestätigen den Normalfall für zugestellte Ausgabeparameter mit anschließendem Erfolg der Statusabfrage, einschließlich fehlgeschlagener Dateiauswahl und Projektwiederherstellung.
* Die Bewertung der Debug-Abdeckung stimmt mit dem Test und den bereitgestellten Logs überein. `tests/test_loader.cpp:445` prüft die Reserve vor und nach dem 40-Byte-Fall sowie nach Wiederauffüllung. In beiden x64-Konfigurationen läuft dieser Teil, und die bereitgestellte Debug-CTest-Datei bestätigt den bestandenen Ladertest. Der 16-Byte-Fall und die Prüfung der einzelnen Allokationen in den sechs Host-Aufrufen werden bei aktiviertem MSVC-Iterator-Debugging ausgeklammert. Die Begründung mit der Proxy-Allokation in `noexcept`-Konstruktoren bleibt tragfähig. Diese Tests belegen die jeweiligen ausgeführten Fälle. Eine pauschale Zusicherung für alle Host-Aufrufe folgt daraus wegen MS-R11 nicht.
* Beide Hash-Listen stimmen. Alle 46 Paket-Einträge und alle 84 Quell-Einträge entsprachen dem Bundle bzw. dem geprüften Commit-Stand. Das Bundle wurde laut Build-Info vor dem Commit aus dem damaligen Worktree gebaut. Ein binär identischer Neubau war kein Prüfkriterium.

### Fehlende Tests und Prüfgrenzen

Die fehlenden Regressionstests stehen bei den Befunden. Besonders relevant sind Parametertexte an der Host-Schnittstelle, die einmal ausgebliebene Statusantwort, Reset bei kleinen Blöcken und Reaktivierung sowie ein abgelehnter Projektwechsel mit anschließendem Speichern. VS 2026 Debug, VS 2022 Release und ASan wurden anhand der mitgelieferten Logs geprüft und nicht unabhängig wiederholt. Die reguläre Testsuite aller vier Plugins war ebenfalls nur über die mitgelieferten Protokolle abgedeckt. Der Reset-Test und die Zusatztests haben kein Fenster geöffnet. Es gab keine DAW- oder Hörtests. MS-R05 bleibt wie vereinbart zurückgestellt und ist in der Dokumentation als vorläufiges Fenster gekennzeichnet.
