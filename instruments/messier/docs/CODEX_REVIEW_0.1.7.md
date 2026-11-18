# Nachprüfung Messier 0.1.7 · 18. November 2026

**KEINE FREIGABE für den DAW-Test nach dem Prüfauftrag.** 0.1.7 behebt den 0.1.6-Fall, in dem ein alter Ladeauftrag nach einem bereits verarbeiteten Auftrag der neuen Verbindung eintrifft. Kommt er jedoch vor der ersten Nachricht der neuen Verbindung am Processor an, kann er die WAV-Datei eines inzwischen wiederhergestellten Projekts ersetzen. Im Zusatztest wurde der alte Pfad danach tatsächlich von `getState` ausgegeben. Ob Studio One solche Nachrichten über den Wechsel hinweg in dieser Reihenfolge zustellt, ist offen.

Geprüft wurde der uncommittete Stand von 0.1.7 auf `46c399868f6b4170a3886286ed8ec9a0191bd0bf` im Worktree `.work/messier/wt`. Pfade sind relativ zu `instruments/messier/`.

| Altbefund | Stand | Ergebnis |
| --- | --- | --- |
| MS-R26 · Alte Parameterausgabe verdeckt neue Projektänderung | offen als Garantie | Die bestätigte Statusmeldung wird unabhängig ausgewertet. Der konkrete alte Testfall besteht, solange die beiden Werte verschieden sind. Ohne Statusrückweg bleibt eine Kollision möglich (MS-R33). |
| MS-R27 · Eine Ladung wird zweimal gemeldet | gegenstandslos | Eine Markierung über beide Zustellwege ist jetzt ausdrücklich erlaubt. Eine Abfrage ohne neuen Ladevorgang kann in einem anderen Ablauf dennoch markieren (MS-R32). |
| MS-R28 · Alter Ladeauftrag ersetzt die neue Datei | offen | Nach Eingang einer höheren Generation wird der alte Auftrag abgewiesen. Vor deren Eingang wird er weiterhin angenommen (MS-R30). |
| MS-R29 · Ausnahmemaske des WAV-Streams | behoben | Der fertig konstruierte Wächter setzt die gespeicherte Maske zurück, auch wenn das Scharfstellen der neuen Maske oder der Read wirft. Beide Regressionstests bestanden. |

### Grundentscheidung zur Projektänderung

**Die beiden Meldewege unabhängig auszuwerten ist richtig.** `IComponentHandler2::setDirty(true)` signalisiert dem Host einen geänderten Projektzustand. Die VST3-Schnittstelle definiert den Aufruf als Zustandsmeldung, nicht als Zähler für Dateiwechsel (`external/vst3sdk/pluginterfaces/vst/ivsteditcontroller.h:311`). Daher darf dasselbe Ereignis über Parameter 199 und Status gemeldet werden. Solange das Projekt als geändert gilt, ist die zweite Meldung redundant. Speichert der Host dazwischen, kann eine verspätete zweite Meldung wieder einen Speichern-Hinweis auslösen. Das ist ein vertretbarer Preis für die robustere Zustellung und macht MS-R27 nach dem vereinbarten Vertrag gegenstandslos.

Die beiden Wege gegeneinander aufzurechnen war der Fehler aus 0.1.6. In 0.1.7 kann ein alter Parameterwert eine aktuelle, akzeptierte Statusmeldung mit erhöhtem Zähler nicht mehr aufzehren. Eine Garantie für den Parameterweg allein folgt daraus nicht. Haben der verspätete Wert und der neue Wert dieselbe Kodierung, sieht der Controller keine Wertänderung. Ohne Statusantwort kann die Meldung weiterhin ausbleiben (MS-R33). Umgekehrt zeigte der Zusatztest A → B → A eine erneute Markierung allein durch einen alten Zählerstand bei einer gültigen Statusabfrage. Das ist ein gesonderter Effekt der Zählung pro zuletzt gesehenem Processor (MS-R32).

Die Entscheidung über doppelte Meldungen ist damit tragfähig. Der noch blockierende Befund betrifft den Ladeauftrag selbst. Eine verspätete Nachricht kann die gespeicherte Dateiauswahl ändern (MS-R30).

### MS-R30 [P2] Alter Ladeauftrag wirkt vor der ersten Nachricht der neuen Verbindung

**Fensterlos im eigenen Release-Bau bewiesen.** `latest_generation_` steigt beim Processor erst durch eine eingegangene Messier-Nachricht. Ein Disconnect und Reconnect auf beiden Seiten setzen keine neue Untergrenze. Solange die neue Generation noch unterwegs ist, gilt der alte Auftrag als gleich aktuell (`source/processor.cpp:685`, `:693`). Ein regulärer VST3-`setState`-Aufruf trägt keine Controller-Generation.

Im Zusatztest hielt das Host-Double einen echten Ladeauftrag für Datei A zurück. Danach wurden beide Verbindungsrichtungen getrennt und wieder verbunden. Das Double hielt auch die erste Statusanfrage der neuen Verbindung zurück. Der Host stellte dann ein Projekt mit Datei B über `setState` wieder her und renderte es. Als der alte Auftrag für A eintraf, antwortete der Processor **OK**. Nach weiteren Audioblöcken las die Engine A und `getState` lieferte den Pfad von A. Die spätere Zustellung der neuen Statusanfrage stellte B nicht wieder her. Der vorhandene Test lässt den neuen Auftrag zuerst am Processor eintreffen (`tests/test_plugin.cpp:1396`).

Die alte Verbindung bereits beim erfolgreichen Trennen am Processor für zustandsändernde Nachrichten schließen. Neue Aufträge erst nach einer gültigen Anmeldung für die neue Verbindung annehmen. Wo der Processor keinen Wechsel über seinen Lebenszyklus erkennen kann, braucht die Zustellung eine ausdrückliche Schranke durch den Host oder ein bestätigtes Protokoll. Den nachgestellten Ablauf mit `setState` als Regressionstest aufnehmen.

### MS-R31 [P3] Eine alte Tastenfreigabe kann eine neue Note beenden

**Fensterlos im eigenen Release-Bau bewiesen.** Die Ausnahme für eine alte Tastenfreigabe vermeidet eine hängen gebliebene Note aus der vorigen Verbindung. Intern ist der Haltezustand aber nur nach Tonhöhe indiziert. Nach einem erneuten Druck derselben Taste aus der neuen Verbindung kann die alte Freigabe daher die neue Note beenden (`source/processor.cpp:685`, `:736`).

Im Zusatztest wurde nach dem Reconnect erneut Taste 72 gedrückt und gerendert. Die verspätete Freigabe aus der alten Verbindung wurde angenommen und ließ den Tonhöhenfaktor von **2,0** auf **1,0** zurückfallen, obwohl die neue Taste noch gehalten wurde. `TestLateRequest` prüft den Rückgabewert der Freigabe, aber keinen erneut gehaltenen Ton. Eine Freigabe der alten Generation sollte nur eine noch von dieser Generation gehaltene Note beenden.

### MS-R32 [P3] Beim Wiederverbinden mit einem früheren Processor zählt ein alter Status erneut

**Fensterlos im eigenen Release-Bau bewiesen.** `CountFromStatus` speichert nur die zuletzt gesehene Processor-ID samt Zähler (`source/controller.cpp:444`). Der Ablauf A → B → A vergisst dadurch den Stand von A. Im Zusatztest lud nur A einmal ein Sample. Beim Rückwechsel von B zu A löste die reine Statusantwort **einen zusätzlichen** Dirty-Aufruf aus, obwohl keine neue Datei geladen wurde und kein Ausgabeparameter im Spiel war. Das kann einen unnötigen Speichern-Dialog verursachen. Den bestätigten Stand je wiederverwendeter Instanz behalten oder den Anschluss an eine bereits geladene Instanz ausdrücklich als erneute Markierung definieren.

### MS-R33 [P3] Der Versatz von Parameter 199 reduziert Kollisionen und schließt sie nicht aus

**Aus der Rechnung belegt.** Bei identischen Resten modulo 65.535 und identischem Ladezähler liefern zwei unterschiedliche Processor-IDs denselben Wert (`source/parameters.h:97`). Wenn der Host nur Parameter 199 zustellt und der verspätete Wert von A schon im Controller steht, löst der identische erste Wert von B keine Änderung aus (`source/controller.cpp:200`). Mit gültiger Statusantwort von B holt der unabhängige Weg die Markierung nach. Ohne Status ist die Garantie weiterhin nur probabilistisch, wie im Build-Bericht für die Kollision bereits beschrieben. Die reguläre Gegenprobe deckt den Fall unterschiedlicher Werte ab. Für eine harte Garantie braucht es eine Herkunftskennung oder die Zusicherung des Hosts, dass alte Ausgaben beim Wechsel verworfen werden. Das ist dieselbe Fehlerklasse wie MS-R26.

### Geprüft ohne weiteren Befund

* Eigener Release-Prüfbuild mit VS 2026, MSVC 19.51, Windows x64. Alle **6 Messier-Tests** und **47 Validator-Prüfungen** bestanden. Der Firmwarevergleich meldete **0 von 960.000** unterschiedlichen Samples und keine Allokationen in `process`.
* Innerhalb desselben Prozesses erhalten Controller beim Initialisieren und Verbinden fortlaufende Generationen. Nachdem der Processor eine höhere Generation gesehen hat, zeigt die Prüfung auf niedrigere Generationen für Load, Reset, Note-On, Status und Display in die richtige Richtung. Die Antworten übernehmen die Generation des jeweiligen Auftrags. Bei einem Controller, der in einem anderen Prozess neu startet, ist die Monotonie über den Prozesswechsel nicht zugesichert. Das ist im Build-Bericht dokumentiert und wurde nicht nachgestellt.
* Wiederholte Statusabfragen desselben aktuell verbundenen Processors mit unverändertem Zähler, allgemeine Lesefehler und eine Projektwiederherstellung ohne ausstehende alte Nachrichten lösten in den vorhandenen Tests keine zusätzliche Dirty-Meldung aus. Eine Meldung per Parameter und eine per Status für dieselbe Ladung sind nach dem neuen Vertrag zulässig.
* Die bereitgestellte Matrix meldet 16 von 16 für VS 2026 Release, VS 2026 Debug und VS 2022 Release sowie Exit 0 für alle fünf Messier-ASan-Programme. Bei der nachträglichen Kontrolle stimmten alle 59 Einträge der Paket-Hash-Liste. Der Worktree enthält inzwischen 0.1.9. Beim Vergleich mit den 104 Quell-Hashes des Pakets weichen daher 27 Dateien ab. Die eigene Build- und Funktionsprüfung bezieht sich auf den zuvor geprüften 0.1.7-Worktree. Das Paket wurde später geprüft, soweit seine eigene Hash-Liste reicht.

### Fehlende Tests und Prüfgrenzen

Die Zusatztests liefen ohne Fenster mit einem gezielt verzögernden Host-Double. Die drei ausgeführten Fälle sollten in die Regressionstests aufgenommen werden. Ein Test mit künstlich gleich gewählten Ausgabewerten kann die Grenze des Parameterwegs festhalten. Die Zustellreihenfolge von Studio One, echtes Speichern in einer DAW, getrennte Prozesse, Fenster und Hören wurden nicht geprüft.
