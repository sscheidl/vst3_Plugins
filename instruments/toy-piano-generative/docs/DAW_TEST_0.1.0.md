# DAW-Prüfliste: Toy Piano Generative 0.1.0

Was automatisch nicht prüfbar ist und in der DAW (Studio One) noch aussteht.
Ergebnisse bitte hier eintragen: PASS, FAIL mit kurzer Beschreibung, oder
offen. Bisher hat niemand das Plugin gehört oder sein Fenster gesehen.

Installation: den Ordner `Toy Piano Generative.vst3` aus
`out\toy-piano-generative-0.1.0\` nach `C:\Program Files\Common Files\VST3\`
kopieren (braucht Adminrechte), DAW neu scannen lassen.

## Laden und Fenster

| Nr. | Prüfung | Ergebnis |
| --- | --- | --- |
| 1 | Plugin erscheint als Instrument „Toy Piano Generative" (Hersteller TAUREON), neben „Toy Piano Classic" | offen |
| 2 | Lädt auf einer Instrumentenspur ohne Fehlermeldung | offen |
| 3 | Fenster öffnet: obere Reihe fünf Regler (Master Pitch, Tuning, Attack, Decay, Volume), untere Reihe acht (Algorithm, Mutation, Notes, Scale, Tonic, Octave, Length, Divide); nichts überlappt, alles lesbar | offen |
| 4 | Schalter RUN, TRANSPOSE und KEYS sichtbar und bedienbar; `<<` und `>>` an den Enden der Klaviatur | offen |
| 5 | Fenster lässt sich ziehen (50 bis 200 %), bleibt scharf | offen |
| 6 | KEYS blendet die Klaviatur samt `<<`/`>>` aus und ein; das Fenster folgt | offen |

## Sequencer und Transport

| Nr. | Prüfung | Ergebnis |
| --- | --- | --- |
| 10 | Transport starten: Das Muster spielt in Sechzehnteln im Tempo des Projekts | offen |
| 11 | Transport stoppen: keine neuen Noten, klingende klingen aus | offen |
| 12 | Erneut starten: Das Muster beginnt bei Schritt 1, auch mitten im Takt | offen |
| 13 | RUN aus bei laufendem Transport: Stille; RUN an: Muster beginnt bei Schritt 1 | offen |
| 14 | Tempo ändern (auch per Tempospur): Die Schritte folgen ohne Aussetzer oder Doppelschläge | offen |
| 15 | Loop im Host: Am Sprung fällt kein Schritt aus und keiner kommt doppelt; das Muster läuft weiter, es beginnt nicht neu | offen |
| 16 | Notes 0 %: nach dem nächsten Skalenwechsel Stille; 100 %: jeder Schritt klingt | offen |
| 17 | Scale wechseln: neues Muster; jede der sieben Skalen klingt plausibel (Octave: nur Oktaven, Tonic: ein Ton) | offen |
| 18 | Tonic drehen: Das Muster wandert mit, der Rhythmus bleibt | offen |
| 19 | Octave 0 bis 5: sechs Lagen; oben keine Aussetzer | offen |
| 20 | Length 1 bis 16 und Divide 1 bis 64 wirken wie beschriftet | offen |
| 21 | Mutation aufdrehen, Algorithmen 1 bis 5: 1 ändert einzelne Schritte; 2 streut Dreierläufe ein; 3 spielt die Skala selbst; 4 schweigt jeden zweiten Durchlauf; 5 ändert bis 50 % nur Tonhöhen | offen |
| 22 | Klingt es musikalisch brauchbar? Lage (zwei Oktaven über der Spezifikation) passend? | offen |

## Transponieren

| Nr. | Prüfung | Ergebnis |
| --- | --- | --- |
| 30 | TRANSPOSE an, Taste auf der Bildschirm-Klaviatur: Sequenz springt mit der Tonika auf diese Taste; Tonic- und Octave-Regler folgen; die Taste ist getönt | offen |
| 31 | Schwarze Taste: Tonika mit Vorzeichen (z. B. F#) | offen |
| 32 | `>>` und `<<`: Klaviatur und Sequenz wandern je eine Oktave; an den Grenzen (C1, C7, Oktave 0 und 5) bleibt die jeweilige Seite stehen | offen |
| 33 | MIDI-Keyboard oder MIDI-Spur, TRANSPOSE an: Note transponiert und klingt nicht selbst; zeigt Studio One die neuen Werte von Tonic und Octave? | offen |
| 34 | TRANSPOSE aus: Bildschirm-Tasten und MIDI-Noten spielen wie in Toy Piano Classic | offen |
| 35 | Eine aufgenommene MIDI-Spur mit einzelnen Noten steuert die Sequenz taktgenau | offen |

## Klang und Stimmung

| Nr. | Prüfung | Ergebnis |
| --- | --- | --- |
| 40 | Tuning 0 %: stimmt mit anderen Instrumenten überein | offen |
| 41 | Tuning 100 %: hörbar tiefer, in tiefen Lagen deutlich (A1 54 statt 55 Hz); gefällt die Wirkung? | offen |
| 42 | Master Pitch, Attack, Decay, Volume wirken wie in Toy Piano Classic | offen |

## Automation und Projekt

| Nr. | Prüfung | Ergebnis |
| --- | --- | --- |
| 50 | Regler, Automationsspur und Parameterliste des Hosts bleiben für alle Sequencer-Parameter im Gleichschritt | offen |
| 51 | Automation von Scale, Tonic und Mutation aufnehmen und abspielen | offen |
| 52 | Projekt speichern und neu laden: gleiches Muster wie vor dem Speichern, gleiche Regler, gleiche Fenstergröße und Klaviaturlage | offen |
| 53 | Zwei Instanzen: verschiedene Startmuster, stören sich nicht | offen |
| 54 | Export (Bounce) läuft durch; mit Mutation 0 zweimal gleich | offen |
| 55 | CPU-Last unauffällig | offen |
