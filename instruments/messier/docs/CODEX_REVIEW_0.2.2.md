# Codex-Nachprüfung Messier 0.2.2 · 19. November 2026

**KEINE FREIGABE für den DAW-Test.** Der ursprünglich gemeldete Rückruf während der Parameteränderung ist behoben. Beim erstmaligen Öffnen bleibt aber ein synchroner Anzeigeabruf übrig, durch den der Host das Fenster schließen kann, bevor das SDK den Öffnungsvorgang abgeschlossen hat (MS-R48). Dieser Pfad ist im Code belegt. Sein Auftreten in einer DAW wurde fensterlos nicht geprüft. Release-Bau, sechs Messier-Tests, Validator und Firmwarevergleich sind grün.

Geprüft wurde **34a5a70** gegen **69aa73b** auf dem Branch `messier-gui`. Der Arbeitsstand `085d8ce` unterscheidet sich nur im Prüfauftrag. Pfade in Befunden beziehen sich auf `instruments/messier/`. SDK-Pfade liegen unter `external/vst3sdk/`.

## Nachprüfung der bisherigen Befunde

| Befund | Urteil | Ergebnis |
| --- | --- | --- |
| MS-R44 · Rückruf mitten in der Parameterverteilung | **Ursprünglicher Fehler behoben, Restpfad beim Öffnen** | Controller-Updates rufen den Processor über `AnimateLater` erst verzögert auf. Der Controller hält beim Verteilen eigene Referenzen und prüft die Registrierung vor jedem Aufruf. Der Release-Test meldet den zweiten Editor während des ersten Aufrufs ab und gibt ihn frei. Er erhält danach keinen Aufruf. Die vorgemerkte Aufgabe prüft Editor-Lebensdauer, offenen Rahmen und Fensterseriennummer. Ein altes Fenster kann dadurch keinen späteren Poll für ein neu geöffnetes Fenster auslösen. Der Controller begrenzt die Abfragen über alle Fenster einer Instanz gemeinsam auf frühestens alle 20 ms. Der direkte Erstaufruf in `OpenUnchecked` ist MS-R48 (`source/controller.cpp:193–217` und `:533–546`, `source/editor.cpp:113–122` und `:444–485`). |
| MS-R45 · Falsche Playhead-Richtung bei kurzen Loops | **Behoben für Tempo und Richtung** | Die Ansicht übernimmt die aktuelle Geschwindigkeit aus dem Processor-Frame und wickelt die Position im dazugehörigen Loop. Die früheren Fälle mit kurzem Loop und Richtungswechsel bestanden. Für die Erkennung des Ruhestands bleibt MS-R50 offen (`source/map_view.cpp:166–202`). |
| MS-R46 · Loop-Anzeige bleibt bei rein audioseitiger Automation zurück | **Behoben** | Die GUI zeichnet START und SIZE bis 300 ms nach dem letzten lokalen Wertwechsel als Vorschau. Danach gelten Start und Länge aus dem Processor. Der Playhead verwendet dabei durchgehend die eigene Loopgeometrie des Processor-Frames. Der Rückfall ist fensterlos getestet (`source/map_view.cpp:182–202`). |
| MS-R47 · Wellenform-Cache und doppelte Deckkraft | **Behoben** | Fehlgeschlagene Offscreen-Erstellung wird für denselben Schlüssel zwei Sekunden lang zurückgestellt. Bei neuem Schlüssel kann der Aufbau sofort erneut versucht werden. Die Segmente links, innen und rechts werden einzeln mit 16 %, 40 % und 16 % gezeichnet. Die Referenzen halten Übersicht, Skin und Bitmap gültig (`source/map_view.cpp:254–318`). |
| MS-R40 · Menü-Rückbau zwischen Einfügen und Anbinden | **Rest behoben** | `PopUpMenu` prüft die tatsächliche Zugehörigkeit zum Rahmen per `isChild`. Der native Fehlstart bleibt Codeprüfung (`source/editor.cpp:632–658`). |
| MS-R41 · Farbe der Dateizeile hängt an der Textaufbereitung | **Rest behoben** | Die Infofarbe erhält einen eigenen geschützten Aufruf im Skinwechsel. Die früheren Text- und Tooltip-Operationen können diesen Schritt dort nicht mehr überspringen (`source/editor.cpp:436–441`). |
| MS-R43 · Falsche Richtung der Grain-Schweife | **Richtungsfehler behoben** | Grain-Tempo und Phasenrate stammen aus dem Kern. Die Ansicht muss die Richtung auch bei kurzen Dateien nicht mehr erraten. Die Erkennung eines beendeten Grains hat noch eine separate Mehrdeutigkeit (MS-R49). |

## Neue Befunde

### MS-R48 [P2] Der erste Anzeigeabruf läuft noch vor Abschluss des Fensteröffnens

**Im Code belegt.** `OpenUnchecked` ruft `callbacks_.poll()` direkt vor der Rückkehr aus `open()` auf. Das SDK nutzt erst danach den Rahmen, um die Größe zu lesen, und registriert den Editor zum Schluss. Bei der ersten Anzeigeabfrage kann eine Dateistatusantwort den Host synchron über eine Projektänderung informieren. Schließt der Host dabei den gerade geöffneten Editor, setzt `close()` den Rahmen zurück. Der Öffnungspfad des SDK setzt einen weiter gültigen Rahmen voraus. Der Test für MS-R44 deckt die Verteilung an vorhandene Editoren ab. Das native Verhalten beim Öffnen steht aus (`source/editor.cpp:152–175`, `:379–391` und `:854–865`, `source/controller.cpp:533–546`, SDK `public.sdk/source/vst/vstguieditor.cpp:169–189`).

**Minimale Korrektur.** Den ersten Poll nach erfolgreichem `attached()` und Registrierung auf denselben verzögerten Weg wie Parameteränderungen legen. Nach einem Host-Größenrückruf beim Öffnen den Rahmenzustand erneut prüfen.

### MS-R49 [P3] Bei identischen Grains kann das Nachleuchten eines beendeten Grains fehlen

**Fensterlos bestätigt.** Ein alter Grain wird als noch laufend behandelt, wenn in der neuen Anzeige irgendein Grain mit gleichem Panorama, gleicher Geschwindigkeit und gleicher Phasenrate vorkommt. Im Zusatztest endete einer von zwei gleichartigen Grains. Das Nachleuchten wurde verworfen. Mit verschiedenem Panorama wurde es wie vorgesehen gespeichert. Die gleichen Werte sind bei stabilen Reglern und Panorama-Streuung null im selben Grain-Kanal plausibel. In der Praxis können Schweife dann abrupt verschwinden. Das wirkt sich auf die Anzeige aus (`source/map_view.cpp:86–112`, `core/engine.cpp:119–132`).

**Minimale Korrektur.** Einen laufenden Grain nur bei plausibler Phasenfortsetzung zuordnen und jeden aktuellen Kandidaten höchstens einmal verwenden. Eine stabile Grain-Kennung je Einsatz beseitigt die Mehrdeutigkeit vollständig.

### MS-R50 [P3] Eine verspätete Wellenform kann die ruhende Anzeige erneut bewegen

**Fensterlos bestätigt.** Die Ruheerkennung hängt von einem identischen Display und demselben Übersichtszeiger ab. Nach erkannter Ruhe wurde im Zusatztest dieselbe Audioanzeige nochmals mit einer jetzt verfügbaren Wellenform zugestellt. Der Zähler der gerenderten Blöcke blieb gleich. Trotzdem starteten Fortschreibung und Drift erneut. Ein zweiter Zusatztest ohne weitere Antworten zeigte, dass der Drift nach zehn Sekunden noch mit praktisch vollem Tempo weiterlief. Die Positionsfortschreibung war bereits an ihrer Zeitgrenze angekommen. Ein vorhergesagtes Grain-Ende kann in diesem Zustand mit gleichbleibendem Schweif gezeichnet werden, bis wieder eine Antwort angenommen wird (`source/map_view.cpp:73–120`, `:144–168` und `:535–549`).

**Minimale Korrektur.** Wellenform-Updates unabhängig von der Audio-Zeitbasis übernehmen. Den letzten echten Fortschritt aus Datei und Renderzähler führen. Die Ruhegrenze beim Zeichnen auch bei ausbleibenden Antworten anwenden und beendete Schweife nach Wandzeit ausblenden.

### MS-R51 [P3] Nach längerer Wiedergabe kann der Nebel trotz ruhendem Drift seitlich wandern

**Rechenweg bestätigt.** Die Wiederholungsperiode der Nebeltextur hängt an ihrer momentan geänderten Breite. Bei größerem aufgebautem Versatz verschiebt schon die normale Breitenänderung den gezeichneten Mittelpunkt. Bei 10.000 Einheiten Versatz führte ein Prozent mehr Breite in der verwendeten Formel zu rund 95 Einheiten seitlicher Verschiebung desselben Kachelmittelpunkts, obwohl der eigentliche Drift stillstand. Das ist nach einigen Minuten Vorlauf möglich. Die Begrenzung des Gesamtversatzes auf eine Million hält die Zahl klein, kann beim Erreichen dieser Grenze aber auch die Phase beider Texturen versetzen (`source/map_view.cpp:156–164` und `:424–442`).

**Minimale Korrektur.** Für jeden Nebel eine eigene fortlaufende Phase mit fester Wiederholungsperiode führen und genau auf diese Periode begrenzen. Die Breitenanimation um die daraus ermittelte Position zentrieren. Der seitliche Vorzeichenwechsel hängt damit allein vom Tempo ab.

### MS-R52 [P3] Die Grain-Endzeit braucht vor der Zeitumrechnung eine Begrenzung

**Grenze fensterlos bestätigt.** Der Reader begrenzt die Phasenrate nach oben und auf endliche Werte. Bei einer sehr kleinen positiven Rate kann die berechnete Restlaufzeit trotzdem außerhalb der darstellbaren Uhrdauer liegen. `ShowAt` rechnet diese Zeit in die feste Uhrdarstellung um, bevor es prüft, ob das Ende überhaupt in den betrachteten Zeitraum fällt. Reguläre Grains haben eine begrenzte Laufzeit im Kern. Die Prüfung bestätigte die Lücke zwischen Reader und Zeitdarstellung (`source/display_data.cpp:39–46`, `source/map_view.cpp:89–97`, `core/engine.cpp:122–126`).

**Minimale Korrektur.** Die berechnete Zeit in Sekunden zuerst mit der tatsächlich verstrichenen Zeit und dem Nachleuchtfenster vergleichen. Nur darstellbare Zeitpunkte innerhalb dieses Fensters in den Uhrtyp übernehmen.

## Weitere Prüfergebnisse

- **Display-Daten und Klang.** Die 456-Byte-Struktur setzt das Playheadtempo in Dateiframes pro Sekunde als wirksames Tempo mal Dateirate um. Der Grain-Schritt und sein Phasenzuwachs werden mit 48.000 Engine-Samples pro Sekunde multipliziert. Das entspricht dem Kern ohne Grain-FM. Der ganzzahlige Renderzähler zählt Engine-Quanten und sein Umlauf ist definiert. Nichtendliche oder zu große Geschwindigkeiten und negative Phasenraten werden verworfen. Die verbleibende Zeitgrenze steht in MS-R52. Der eigene Firmwarevergleich ergab 0 von 960.000 abweichenden Samples und keine Allokationen in `process`.
- **Grain-Speicher und Dateiwechsel.** 48 Einträge sind eine feste, ausreichend begrenzte Kapazität für die Darstellung. Bei Überlauf ersetzt ein jüngerer Eintrag den am längsten zurückliegenden. Gespeicherte Schweife altern nach Wandzeit. Eine neue Dateisequenz oder Länge verwirft den gesamten Bestand. Freeze hält den Playhead und lässt die Grains weiterlaufen, entsprechend dem Kern. Die offenen Ruhesituationen stehen in MS-R49 und MS-R50.
- **Drift und Kachelung.** Das Driftziel verwendet die vorzeichenbehaftete Core-Geschwindigkeit relativ zur Sample-Rate. Loop- und Datei-Umläufe beeinflussen das Vorzeichen nicht. Beim sichtbaren STOP und bei einem gemeldeten Freeze ist das Solltempo null. Die Tempoänderung wird geglättet. Die Breite beträgt mehr als die Wiederholungsperiode. Geometrisch überlappen sich die Nebelkacheln und decken die Platte ab. MS-R51 betrifft ihre horizontale Verschiebung bei veränderlicher Breite. Falls die Dateirate in der Übersicht fehlt, verwendet die Driftberechnung vorläufig 48 kHz.
- **Layout und Farben.** SPRAY bleibt der bisherige Parameter und steht jetzt als großer Regler in Spalte 0. LATCH und KEYS sitzen darunter. Bei ausgeblendeter Klaviatur bleibt LATCH gedimmt sichtbar und nimmt keine Mausereignisse an. Beide Oktavtasten haben dieselben Maße wie die großen Tasten, und jeweils die erreichte Grenze ist gedimmt. Die Dreiecksspur liegt unter den Zeitwerten und oberhalb des Bezels. Die Unterkante der Platte sowie die Basisgrößen 880 × 724 und 880 × 640 bleiben erhalten. Blauviolett wird für Bedienelemente, Werte und START verwendet. Orange markiert Loop, Legende und Positionsdreieck. Die Ersatzpaletten verwenden ihren bisherigen Akzent in allen drei Rollen. Statischer Abgleich mit den mitgelieferten Bildern `keys_lowest_1x` und `nokeys_freeze_0.5x`. Gestagtes und neu gebautes Bundle enthalten je 24 PNG und keine Schraubengrafiken.
- **Metallgenerator.** Zwei unabhängige Ausgaben mit den lokal verfügbaren Versionen Pillow 12.3.0 und NumPy 2.0.2 ergaben zehn bitgleiche Dateien. Sie entsprechen bytegenau den eingecheckten 1×- und 2×-Dateien.
- **Vorschauwerkzeug.** Das Werkzeug wurde erfolgreich gebaut. Im Code setzt es den Ressourcenpfad auf `resource/gui`, gibt erfolgreich angehängte Views über `removed()` frei und beendet VSTGUI nach den Editoren. Der COM-Aufruf prüft sein Ergebnis nicht und hat kein entsprechendes `CoUninitialize()`. Für das kurzlebige Werkzeug genügt als Korrektur ein lokaler COM-Lebensdauerwächter mit sauberer Fehlermeldung vor dem Rendern. Die Ausführung blieb wie beauftragt außen vor (`tests/preview_window.cpp:150–209`).

## Verifikation und Prüfgrenzen

| Prüfung | Ergebnis |
| --- | --- |
| Eigener fensterloser VS-2026-x64-Release-Bau | Messier, fünf Testprogramme und Vorschauwerkzeug gebaut |
| Eigener CTest-Lauf mit Filter `messier` | 6/6 bestanden |
| Eigener Steinberg-Validator auf neu gebautem Bundle | 47 bestanden, 0 fehlgeschlagen |
| Mitgelieferte Matrix | VS 2026 Release und Debug sowie VS 2022 Release laut Logs je 16/16. Alle vier Validatorläufe je Konfiguration melden 47/0. Die fünf ASan-Läufe melden Exit 0. |
| Paket- und Quellprüfsummen | 106/107 Paketdateien und 158/159 Quellpfade stimmen direkt. Beide Abweichungen betreffen den Prüfauftrag. Die beiden erwarteten Hashes stimmen mit dessen Fassung aus `34a5a70` überein. Die Kopie im Bundle und die Worktree-Datei sind auf dem späteren Auftragsstand. Der Binärstand und alle übrigen Dateien stimmen. |

Für die Nachprüfung fehlen ein Host-Test beim Öffnen und Schließen mit ausstehendem Poll, ein Mehrfach-Grain-Test mit gleichem Panorama und gleichen Raten, ein Test verspäteter Wellenform und ausbleibender Anzeigeantworten sowie ein Drift-Test mit großer Phase bei STOP. Die Prüfung beschränkte sich auf Code, fensterlose Ausführung und bereitgestellte Bilder. Native Menüs, echte Fensterbewegung, DAW-Verhalten und Hörtests stehen aus. Die nachträglich beauftragten Beugungskreuze und die Pitch-Färbung der Grains gehören zur nächsten Designrunde.
