# DAW-Prüfliste: Mendelejew 0.1.1 (Projekt Elements)

Gilt für 0.1.1, die Fassung mit den Korrekturen aus dem Codex-Review; der
Dateiname bleibt der der ersten Fassung.

Was automatisch nicht prüfbar ist und in der DAW (Studio One) noch aussteht.
Ergebnisse bitte hier eintragen: PASS, FAIL mit kurzer Beschreibung, oder
offen.

Installation: den Ordner `Mendelejew.vst3` aus
`build\windows-vs2026-x64\VST3\Release\` (oder aus `out\elements-0.1.1\`) nach
`%LOCALAPPDATA%\Programs\Common\VST3\` kopieren, DAW neu scannen lassen. Eine
dort liegende Kopie unter dem Arbeitstitel (`Elements.vst3`) vorher entfernen:
Beide haben dieselben Klassen-IDs.

## Laden und Spielen

| Nr. | Prüfung | Ergebnis |
| --- | --- | --- |
| 1 | Plugin erscheint als Instrument „Mendelejew" (Hersteller TAUREON) | offen |
| 2 | Lädt auf einer Instrumentenspur ohne Fehlermeldung | offen |
| 3 | Fenster öffnet, Regler und Beschriftung vollständig sichtbar, nichts überlappt | offen |
| 4 | Fenster lässt sich an der Ecke ziehen (50 bis 200 %) | offen |
| 5 | MIDI-Keyboard: jede Taste schlägt an (Standardklang: Strike) | offen |
| 6 | Loslassen der Taste: Bow/Blow klingen aus (Bow oder Blow aufdrehen, Contour mittig) | offen |
| 7 | Anschlagdynamik hörbar; Regler Velocity auf 0 %: alle Töne gleich laut | offen |
| 8 | Pitch-Rad verstimmt um ±2 Halbtöne und kehrt zurück | offen |
| 9 | Bildschirm-Klaviatur spielt; Taste KEYS blendet sie aus und ein | offen |
| 10 | Legato (zweite Taste bei gehaltener erster): neuer Anschlag; Loslassen der zweiten: Rückfall auf die erste ohne Anschlag | offen |
| 10a | Neu in 0.1.1: mit gedrückter Maus schnell über viele Tasten der Bildschirm-Klaviatur fahren, dann loslassen: kein Ton bleibt hängen, auch nicht in einer mitlaufenden MIDI-Aufnahme | offen |

## Klang

| Nr. | Prüfung | Ergebnis |
| --- | --- | --- |
| 11 | Bow allein (Strike 0, Bow auf, Contour mittig): gestrichener Dauerton | offen |
| 12 | Blow allein: Luftrauschen durch den Resonator; über etwa 2/3 kommt die Röhre hinzu | offen |
| 13 | Strike mit Mallet von links nach rechts: Samples, Schlägel, Plektrum, Partikel | offen |
| 14 | Geometry, Brightness, Damping, Position verändern den Klang wie erwartet | offen |
| 15 | Damping: höherer Wert klingt länger (Hardware-Sinn; bei Torus Classic seit 0.2.0 umgekehrt, siehe Bericht) | offen |
| 16 | Space: unten trocken, Mitte breit, oben Hall; FREEZE hält den Hall | offen |
| 17 | Kein Knacksen beim Drehen der Regler während eines Tons | offen |
| 18 | Klangvergleich mit einer bekannten Elements-Referenz (Hardware, VCV Rack), falls vorhanden | offen |

## Host

| Nr. | Prüfung | Ergebnis |
| --- | --- | --- |
| 19 | Statuszeile im Fenster zeigt die Raten: bei 48 kHz „no conversion", bei 44,1 kHz „converted, latency 49 samples" | offen |
| 20 | Projekt mit 44,1 kHz und mit 96 kHz: spielt, Tonhöhe stimmt. Neu in 0.1.1: sehr laute, helle Einstellungen werden dort hinter der Ratenwandlung bei 0 dBFS hart begrenzt; darauf achten, ob das hörbar zerrt | offen |
| 21 | Automation eines Reglers aufzeichnen und abspielen | offen |
| 22 | Projekt speichern, schließen, öffnen: alle Regler wie zuvor | offen |
| 23 | Preset der DAW speichern und laden | offen |
| 24 | Transport Stopp/Start: kein hängender Ton | offen |
| 25 | Zwei Instanzen gleichzeitig | offen |
| 26 | CPU-Last im Leerlauf und beim Spielen plausibel (Messwert im Bericht: rund 3 % eines Kerns) | offen |
| 27 | Toy Piano Classic und Torus Classic laden und spielen weiterhin | offen |
