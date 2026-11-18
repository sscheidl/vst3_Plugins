# Nachprüfung Messier 0.1.8 · 18. November 2026

**KEINE allgemeine FREIGABE für den DAW-Test.** Der in 0.1.7 reproduzierte Verlust des gespeicherten Projektpfads ist im ausgelieferten 0.1.8-Bundle behoben. Beim Trennen kann aber eine noch gehaltene Host-MIDI-Note enden, wenn dieselbe Note auch am Bildschirmkeyboard gehalten wurde. Der Befund wurde am Bundle nachgestellt. Ein vollständiger Quellabgleich und Neubau von 0.1.8 waren nicht möglich.

Prüfgegenstand war das Paket `out/messier-0.1.8/` aus dem uncommitteten Stand über `46c3998`, ergänzt um gezielte Lektüre des fortgeschriebenen Worktrees. Alle 63 Paket-Hashes stimmen. Im bereits auf 0.1.9 weiterentwickelten Worktree weichen 26 der 109 Einträge der 0.1.8-Quellliste ab. Nach Abgleich mit Paketkopien und lokalen Git-Objekten fehlen weiterhin 18 Originaldateien. Codezeilen aus dem fortgeschriebenen Stand sind unten als solche gekennzeichnet.

| Altbefund | Stand | Ergebnis |
| --- | --- | --- |
| MS-R30 · Alter Ladeauftrag ersetzt ein gerade wiederhergestelltes Projekt | behoben für den nachgestellten Fall | Im 0.1.8-Bundle hielt der Processor den neuen Pfad mit und ohne Disconnect. Die verspätete alte Anfrage wurde jeweils abgelehnt. |
| MS-R31 · Alte Freigabe beendet eine neue Note | behoben für den alten Ablauf | Der mitgelieferte Regressionstest und der eigene Nachlauf auf 0.1.9 bestätigen die Abweisung verspäteter Freigaben. Die Freigabe beim Disconnect selbst kann eine gleich hohe Host-MIDI-Note beenden (MS-R34). |
| MS-R32 · Alte Statusmeldung bei A → B → A | behoben innerhalb des gespeicherten Verlaufs | Der Bericht zu 0.1.8 und der fortgeschriebene Code zeigen ein Gedächtnis für acht Processoren. Der A → B → A-Test besteht in den 0.1.8-Logs und im eigenen 0.1.9-Bau. Ältere verdrängte Einträge können wieder als neu gelten. |
| MS-R33 · Kollision auf Parameter 199 | offen als Garantie | Der Prozessversatz schließt die adressierte Kollision direkt aufeinanderfolgender Instanzen im üblichen Bereich aus. Er bleibt ein modularer Wert ohne Herkunftskennung. |

### MS-R34 [P2] Bildschirmkeyboard kann beim Disconnect eine gehaltene Host-MIDI-Note beenden

**Am ausgelieferten 0.1.8-Bundle ohne Fenster bewiesen.** Der Processor gibt beim Trennen seine Bildschirmtasten frei. Im fortgeschriebenen Quellstand führt `disconnect` dazu eine Freigabe in die Queue ein. `ReleaseKey` sendet sie als Note-Off auf Kanal 0 an den Kern. Host-MIDI auf Kanal 0 verwendet denselben Notenschlüssel (`source/processor.cpp:411`, `:448`, `:699`, `source/processor.h:88`, Zeilen des 0.1.9-Worktrees). Der Kern kann die beiden Quellen dann nicht unterscheiden.

Im binären Zusatztest wurden Bildschirmtaste 72 und danach Host-MIDI-Note 72 auf Kanal 0 angeschlagen. Die Host-Note wurde weiter gehalten. Die nach dem Disconnect gerenderten Samples waren bitgenau gleich wie mit einem ausdrücklichen Host-Note-Off. Sie unterschieden sich vom Kontrolllauf mit weiter gehaltener Note. Im 0.1.9-Quellbuild blieb eine Host-Note alleine nach dem Disconnect hörbar. Nur mit der zusätzlichen Bildschirmtaste sprang die Tonhöhe von Faktor 2 auf 1.

Intern den Besitz einer Note pro Quelle trennen, damit beim Disconnect ausschließlich die Bildschirmnote endet. Die Bildschirm-MIDI-Ausgabe an den Host kann weiter ihren bisherigen Kanal nutzen. Den Test um dieselbe Note aus beiden Quellen erweitern.

### Geprüft ohne weiteren Befund

* Der eigene Steinberg-Validatorlauf direkt auf dem gestagten 0.1.8-Bundle bestand **47 von 47** Prüfungen. Die mitgelieferten Logs melden dreimal 16 von 16 CTest-Tests und Exit 0 für alle fünf Messier-ASan-Programme.
* Der binäre Zusatztest lud über VST3-Zustandsversion 1 ein Projekt mit Datei B. Ein zuvor datierter Auftrag für Datei A wurde mit und ohne vorheriges Trennen abgewiesen. `getState` gab in beiden Fällen B zurück. Im fortgeschriebenen Code greifen die Zeitgrenzen für erkennbare eigene Nachrichten vor ihren Seiteneffekten. Im selben Prozess behandelt der Zeitstempel Aufträge bis einschließlich des Grenzwerts als alt. Verbindungen über Prozessgrenzen sind davon nicht garantiert abgedeckt. Eine Garantie für beliebig umgeordnete Aufträge innerhalb einer unveränderten Verbindung folgt ebenfalls nicht daraus.
* Die Bildschirm-Queue reserviert bei jeder Annahme von Note-On je einen Platz für alle danach gehaltenen Noten. Das begrenzt belegte Plätze plus nötige Freigaben. Wird beim Trennen eine gehaltene Taste freigegeben, steigt die Belegung um eins und die Anzahl gehaltener Tasten fällt um eins. Der normale Ablauf kann deshalb die reservierte Kapazität nicht überschreiten. MS-R34 betrifft die Zuordnung der Freigabe.
* Die Offset-Rechnung stimmt. Bei einem Abstand von `m` Erzeugungen gilt für eine Kollision `alte Ladungen − neue Ladungen ≡ 4099·m (mod 65535)`. Für den direkt nächsten Processor und dessen erste Ladung sind damit 4100 ältere Ladungen der erste positive Treffer. Die Einschränkung „direkt nächster Processor“ ist wesentlich. Bei 16 zwischen den Erzeugungsnummern beträgt der Rest beispielsweise 49. Eine gültige Statusantwort bietet einen unabhängigen Weg für die Änderungsmeldung.
* Die paketierten Tests zu Speed, Pitch Random, LATCH, WINDOW und Karte sind grün. Im fortgeschriebenen Stand laufen Hostparameter und damit die zugeordneten MIDI-CCs über `PotOf` zur Engine. Wiederherstellung und Neustart gehen über `SettingsFrom`, die Wertanzeige und Eingabe verwenden die dazugehörige Umrechnung. Die dokumentierte Klangänderung beim Öffnen älterer Zustände mit denselben normierten Werten bleibt bestehen. Der Vergleich bei identischen Potistellungen meldet 0 von 960.000 abweichende Samples.
* Der fortgeschriebene Editor beendet START-Bearbeitungen bei Mouse-Up, Mouse-Cancel und Close. SPRAY verwendet ein kurzes begin/perform/end-Paar. LATCH gibt beim Ausschalten, Transponieren und Schließen die gehaltene Bildschirmtaste frei. Die Dateiauswahl aus dem 0.1.8-Bundle ist anhand des mitgelieferten Tests und Berichts belegt. Das Drop-Ziel verwendet laut Bericht denselben Ladecallback wie LOAD. Der 0.1.9-Drop-Pfad ist separat geprüft.

### Fehlende Tests und Prüfgrenzen

Ein unabhängiger Neubau, Fehler-Injektion am kompletten 0.1.8-Drop-Pfad und die native Drag-and-drop-Integration waren ohne exakten Quellstand bzw. ohne Fenster nicht Teil dieses Laufs. Die Belege aus 0.1.8-Logs, Tests direkt gegen das paketierte Plugin und Codelektüre auf 0.1.9 sind oben getrennt ausgewiesen. Es gab keine DAW-, Fenster- oder Hörtests und keine Bewertung der Optik.
