# Codex-Nachprüfung Messier 0.2.3 · 19. November 2026

Prüfstand: `cc35386` und `a77041c` auf `085d8ce`, Branch `messier-gui`. Der Auftrag nennt MS-R48 bis MS-R52, die neue Grain-Darstellung und neue Befunde ab MS-R53. Untersuchungen und Prüfstand werden in diesem Bericht fortlaufend ergänzt.


## Urteil

**FREIGABE für den kontrollierten DAW-Test.** Der unmittelbare Erstabruf beim Fensteraufbau ist auf den verzögerten Weg umgestellt. Der unabhängige Release-Lauf ist grün, und der Klangvergleich ist bitgleich. Die zwei verbleibenden Befunde betreffen das Nachleuchten der Grains und die Anzeige, wenn der Host keine neuen Anzeigedaten liefert.

## Nachprüfung von MS-R48 bis MS-R52

| Befund | Stand | Ergebnis |
| --- | --- | --- |
| MS-R48 · Synchroner Erstabruf beim Öffnen | **Behoben für den gemeldeten Pfad** | `OpenUnchecked` ruft `AnimateLater` auf. Der Abruf wird über die Windows-Nachrichtenwarteschlange vorgemerkt. Die Aufgabe prüft Lebensdauer, Rahmen und Fensterseriennummer. Der unmittelbare Rückruf vor dem Zugriff des SDK auf den neuen Rahmen entfällt. Nativer Fensteraufbau und verschachtelte Host-Nachrichten bleiben Codeprüfung (`source/editor.cpp:387–393`, `:467–487`). |
| MS-R49 · Gleichartige Grains verlieren das Nachleuchten | **Teilweise behoben** | Der ursprüngliche Test mit dem alten Grain bei Phase 0,90 und dem weiterlaufenden bei 0,50 liefert nun einen Nachleuchteintrag wie erwartet. Ähnliche Phasen bei verspäteter Antwort können weiterhin verwechselt werden (MS-R53). |
| MS-R50 · Späte Wellenform startet die ruhende Anzeige neu | **Ursprünglicher Fall behoben** | Eine späte Übersicht wird bei identischem Display übernommen, ohne den letzten Audiozeitpunkt neu zu setzen. Im Zusatztest blieb der bereits ruhende Playhead am alten Ort und der Drift bei null. Ohne weitere Anzeigen kam der Nebel im zweiten Zusatztest zum Stillstand. Für einen bereits vorhergesagten Grain-Schweif bleibt MS-R54 offen. |
| MS-R51 · Seitliche Nebelbewegung durch Größenänderung | **Behoben** | Die Texturbreite schwillt weiter, während die Kachelperiode fest bleibt. Jede Lage führt eine eigene auf ihre Kachelperiode bezogene Phase. In der Gegenrechnung mit dem bisherigen großen Versatz blieb der Kachelmittelpunkt bei einem Prozent mehr Breite am selben Ort. Die minimale Texturbreite ist größer als die Kachelperiode, sodass benachbarte Kacheln weiter überlappen (`source/map_view.cpp:178–191`, `:467–486`). |
| MS-R52 · Restlaufzeit vor Umrechnung begrenzen | **Behoben** | Der Reader akzeptiert Phasenrate null als unbekannt und positive Raten ab `0.001` pro Sekunde. Die längste reguläre Grain-Dauer des Kerns bleibt abgedeckt. `ShowAt` prüft Restzeit und Nachleuchtfenster in Sekunden vor der Uhrumrechnung. Die reguläre Grenzprüfung in CTest bestand (`source/display_data.cpp:19–23`, `:45–49`, `source/map_view.cpp:105–115`). |

## Neue Befunde

### MS-R53 [P3] Bei nahen Fensterphasen kann ein weiterlaufender Grain das Nachleuchten eines anderen verdecken

**Fensterlos bestätigt.** Im Zusatztest hatten zwei Grains dasselbe Panorama, dieselbe Geschwindigkeit und dieselbe Phasenrate. Ihre Phasen lagen zunächst bei 0,90 und 0,85. Die nächste Antwort kam nach 30 ms, enthielt aber nur zehn zusätzliche Renderquanten, rund 13,3 ms Audio. Der erste Grain war beendet. Der zweite lief bei ungefähr 0,983 weiter. Diese höhere Phase erfüllt auch die Kriterien für den ersten Grain. `ShowAt` legte daher keinen Nachleuchteintrag für ihn an. Der ursprüngliche Fall mit weiter auseinanderliegenden Phasen ist behoben (`source/map_view.cpp:103–134`).

**Minimale Korrektur.** Die Phasenfortsetzung aus der Differenz der Renderquanten ableiten und jeden aktuellen Kandidaten nur einmal zuordnen. Wo eine eindeutige Zuordnung wichtig ist, eine stabile Kennung pro Grain-Einsatz verwenden.

### MS-R54 [P3] Ein vorhergesagter Grain-Schweif kann ohne weitere Anzeigeantwort stehen bleiben

**Vorhersagegrenze fensterlos bestätigt, Verhalten des Schweifs im Zeichenpfad nachvollzogen.** Ohne weitere Antworten kommt der Nebel jetzt zum Stillstand. Gleichzeitig bleibt `Ahead` bei 150 ms, solange kein wiederholter Display-Frame den Ruhezustand markiert. Hat die Ansicht aus einem Grain mit ungefähr 100 ms Restlaufzeit bereits dessen Ende vorhergesagt, bleibt der für den provisorischen Schweif verwendete Alterswert dann bei rund 50 ms. Das auf 300 ms ausgelegte Verblassen läuft in diesem Zeichenpfad erst nach einer neuen Antwort weiter. Gespeicherte Nachleuchteinträge altern bereits nach Wandzeit (`source/map_view.cpp:82–97`, `:178–196` und `:575–598`).

**Minimale Korrektur.** Bewegungsfortschreibung und Verblassen entkoppeln. Positionen können bei 150 ms stehen bleiben. Bereits vorhergesagte Schweife ab ihrem geschätzten Endzeitpunkt nach Wandzeit ausblenden.

## Beugungsstrahlen und PITCH

PITCH verwendet beim Öffnen und bei Controller-Updates die gleiche Umrechnung `PitchOctaves` wie der angezeigte Reglerwert. Der Zusatztest bestätigte den exakt neutralen Reglerstandard bei null Oktaven. Im Slate-Test wechselte der Schweif bei −3 Oktaven zu RGB `237/166/146`, bei +2 zu `207/189/203`. Die Mischung ist auf 55 % begrenzt, Alpha bleibt gleich und der Grain-Kern nutzt weiter Elfenbein. Farbe bei Controller-Automation folgt demselben Pfad wie bei manueller Bedienung (`source/editor.cpp:225–231`, `:1113–1120`, `source/value_display.h:79`, `source/map_view.cpp:247–261`).

Die horizontalen und vertikalen Strahlen liegen über Wellenform und Nebel. Sie laufen nach außen aus und werden bei mehr als sechs aktiven Grains schwächer. Der Dichtefall des Vorschauwerkzeugs benutzt die tatsächliche Obergrenze von 20 Grains. Der Build-Bericht nennt dafür 24. Die Darstellung des vorgesehenen Maximalfalls ist damit abgedeckt (`source/map_view.cpp:565–570` und `:616–637`, `tests/preview_window.cpp:96–102`).

## Verifikation und Prüfgrenzen

| Prüfung | Ergebnis |
| --- | --- |
| Eigener fensterloser VS-2026-x64-Release-Bau | Messier und die fünf Testprogramme gebaut |
| Eigener CTest-Lauf mit Filter `messier` | 6/6 bestanden |
| Eigener Steinberg-Validator auf dem neu gebauten Bundle | 47 bestanden, 0 fehlgeschlagen |
| Firmwarevergleich im eigenen Plug-in-Test | 0 von 960.000 Samples verschieden, keine Allokationen in `process` |
| Gestagtes Bundle | 110/110 Paket-Hashes und 162/162 Quell-Hashes stimmen. Paket und Worktree enthalten den späteren Auftragsstand `086e178`. Gegenüber dem Prüfcode ändert er nur den Auftrag. |
| Mitgelieferte Läufe | VS 2026 Release und Debug sowie VS 2022 Release je 6/6. Release-Validator für alle vier Plug-ins je 47/0. ASan steht für diese Fassung aus. |

Ergänzend fehlen ein Host-Test des ersten Öffnens und erneuten Öffnens mit vorgemerkter Anfrage, ein Mehrfach-Grain-Test mit nahen Phasen und verspäteten Antworten sowie ein Zeichen-Test zur Schweifdauer bei ausbleibenden Anzeigen. Der DAW-Test kann die erkennbare Wirkung der Strahlen und eine PITCH-Automation über null überprüfen. Die Prüfung blieb fensterlos.
