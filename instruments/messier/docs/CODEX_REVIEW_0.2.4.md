# Codex-Nachprüfung Messier 0.2.4

Prüfstand ist `b7d7414` auf `086e178` im Branch `messier-gui`. Die Nachprüfung umfasst MS-R53, MS-R54 und den gemeinsamen GUI-Standard. Neue Befunde beginnen bei MS-R55.

**FREIGABE für den DAW-Test.** Drei bestätigte P3-Befunde in der Anzeige.

## Verifizierte Basis

- Der unabhängige Release-Build mit Visual Studio 2026 (x64) ist erstellt. Der beim
  Build gestartete Steinberg-VST3-Validator meldet 47 bestanden und 0 fehlgeschlagen.
  Der Firmwarevergleich meldet 0 von 960.000 abweichenden Samples.
- Alle 72 übergebenen PNGs stimmen bytegenau mit der SHA-256-Liste aus dem
  Übergabe-ZIP überein. Der Satz ist vollständig im Quellbaum, im veröffentlichten
  Bundle und im unabhängigen Prüfbuild vorhanden. CMake bündelt je Preset acht
  Motive und vier gemeinsame Motive, jeweils in 1× und 2×. Die vier Einträge in
  `kPresets` entsprechen `GUI_PRESETS.inc` bis auf Kommentare und Leerraum.
- Die Hashlisten des vorbereiteten Bundles stimmen (171 von 171 Paketdateien und
  213 von 213 Quelldateien). Die Build-Info nennt noch `086e178` plus
  Arbeitsänderungen. Der Quellmanifestvergleich bestätigt den getesteten Messier-Stand.

## Nachprüfung

| Befund | Stand | Ergebnis |
| --- | --- | --- |
| MS-R53 | Teilweise behoben | Der Fall aus 0.2.3 mit den Phasen 0,90 und 0,85 und zehn Renderquanten in 30 ms besteht. Der Zählerabstand ist durch die vorzeichenlose Subtraktion auch beim Überlauf korrekt. Laufende Grains bekommen zuerst einen Treffer und jeder aktuelle Grain wird höchstens einmal beansprucht. Neu angelaufene Grains können dennoch ein Nachleuchten unterdrücken. Siehe MS-R55. |
| MS-R54 | Gemeldeter Fall behoben | Bei vollständig ausbleibenden Antworten altert der Schweif nach Wandzeit über die Vorhersagegrenze von 150 ms hinaus. Der Regressionstest mit 100 ms Restlaufzeit besteht. Bei einer erneut gelieferten identischen Antwort kann die Anzeige auf den alten Grain-Kopf zurückfallen. Siehe MS-R57. |

## Gemeinsames Layout und Zustand

Für Slate, Silver, Copper und Photo plate gelten dieselben Koordinaten. Der Wechsel
tauscht Skin und Farben aus. Die Anordnung habe ich im Code und stichprobenartig in
den vorbereiteten 1×-Vorschauen von Slate und Photo plate kontrolliert. Die
Hauptgruppen, FREEZE und RESET, DETAILS und die Klaviatur belegen getrennte Bereiche.
Die Kopfzeile lässt zwischen Dateibereich und KEYS 14 Layouteinheiten. Die
sekundären Regler samt Werten werden beim Schließen ausgeblendet. VSTGUI
berücksichtigt die Sichtbarkeit beim Hit-Test für Klick und Mausrad.

Die vier Basisgrößen sind 880 × 560, 880 × 644 mit KEYS, 880 × 656 mit DETAILS
und 880 × 740 mit beiden. Größenabruf, Seitenverhältnis, Skalierung und
Griffposition berücksichtigen DETAILS. Der Code nimmt bei abgelehnter
Größenänderung die Umschaltung zurück. Beim Laden eines Projekts passt sich
der Inhalt gegebenenfalls an die vom Host beibehaltene Fenstergröße an.
Die vorhandenen Geometrietests bestehen. Der Host-Resize bleibt im DAW-Test zu prüfen.

Controller-Zustand V3 ist 22 Byte lang und speichert DETAILS als letztes Byte.
V1 und V2 sind lesbar und laden DETAILS geschlossen. Der Controller weist
unbekannte Versionen, ungültige Werte sowie abgeschnittene Zustände vor dem
Übernehmen der Einstellungen zurück. Die Preset-IDs bleiben kompatibel. Der
Vergleich mit `086e178` zeigt keine Änderungen an Core, Engine, Adapter oder
Processor. Der Release-Lauf meldet **6 von 6 Messier-Tests bestanden**.

Bei START verwenden Regler und Klammer dieselbe Schienenbreite und denselben
linken Rand. Die Fälle 0 %, 50 % und 98 % des Tests stimmen. Der Vergleich
weicht dort ab, wo die Engine die Loop-Klammer auf den letzten gültigen Frame
begrenzt. Siehe MS-R56.

## Befunde

### MS-R55 · P3 · Ein neu angelaufener Grain kann einen Schweif unterdrücken

**Belegt** durch fensterlose Nachprüfung mit echten Engine-Snapshots. In
`source/map_view.cpp:117` und `source/map_view.cpp:150` akzeptiert der zweite
Suchdurchlauf einen übrig gebliebenen Grain mit gleichem Panorama, gleicher
Geschwindigkeit und gleicher Phasenrate. Die Phasentoleranz reicht aus, obwohl
dieser Grain im vorigen Snapshot noch nicht klang. Wartende Grains werden in
`engine/looper_engine.cpp:247` aus der Anzeige ausgelassen.

Mit einem Mono-Sample von 96.000 Frames bei 48 kHz, DENSITY 0,85, OVERLAP 0,55
und DISPLACE 0,08 (jeweils normalisiert, übrige Werte aus `EngineSettings`)
lieferten die Renderzähler 111 und 129 einen solchen Fall. Bei einem linken Grain
steigt die rechnerische Phase von 0,092105 über 24 ms auf 1,014757. Ein neuer
linker Grain liegt bei 0,915439. Der Abstand 0,099318 fällt unter die Toleranz
von etwa 0,125332. Von drei nachweislich beendeten Grains werden nur zwei
Schweife gespeichert.

**Korrektur.** Jedem Grain-Start eine stabile Anzeige-Kennung geben und in beiden
Snapshots anhand dieser Kennung zuordnen. Den gemessenen Übergang als Testfall
aufnehmen. Die Auswirkung betrifft das Nachleuchten.

### MS-R56 · P3 · START-Griff und Loop-Klammer trennen sich am rechten Anschlag

**Belegt** mit einem Sample von fünf Frames und START bei 100 %. Die Engine
begrenzt den Loop-Anfang in `engine/looper_engine.cpp:224` auf Frame 4.
`source/map_view.cpp:869` zeichnet den Griff dagegen bei 100 % der nutzbaren
Breite. Bei 100 % Zoom liegen Griff und Klammer dadurch 168 Einheiten
auseinander (852 und 684 relativ zum MapView). Die Abweichung beträgt am
Anschlag 840 geteilt durch die Framezahl und fällt bei üblichen Samplelängen
kaum ins Gewicht. Der Test in `tests/test_plugin.cpp:1832` verwendet nur
künstliche Loop-Anfänge ohne Begrenzung.

**Korrektur.** Für die Griffposition bei geladener Datei dieselbe Begrenzung auf
den letzten gültigen Frame anwenden und das Ergebnis über `FrameX` zeichnen.
Den rechten Anschlag mit einem kurzen Sample testen. Der wirksame Loop-Anfang
und die Automation brauchen dafür keine Änderung.

### MS-R57 · P3 · Eine identische Display-Antwort lässt einen verglühenden Grain wieder erscheinen

**Belegt** fensterlos mit 60 ms anfänglicher Restlaufzeit. Nach 80 ms liefert
`EndedFor` rund 20 ms Schweifalter. Eine identische Antwort nach 125 ms setzt
`resting_` in `source/map_view.cpp:89`. Dadurch springt die Wandzeit in
`source/map_view.cpp:253` auf null und `EndedFor` liefert −1. Der Zeichenpfad
ab `source/map_view.cpp:628` kann den alten Kopf wieder zeichnen, solange
derselbe Snapshot gehalten wird. Dieser Wechsel war schon vor 0.2.4 möglich.
Er ist weiterhin relevant, weil der Processor bei ausbleibenden neuen
Audio-Blöcken die letzte Anzeige erneut senden kann
(`source/processor.cpp:736`).

**Korrektur.** Bereits vorhergesagte Enden beim Übergang in den Ruhezustand
beibehalten und ihre Schweife weiter nach Wandzeit ausblenden. Den Übergang
mit einer identischen Antwort zusätzlich zum Test ohne Antworten abdecken.

## Prüfumfang

Die Freigabe gilt für den DAW-Test. Geprüft wurden der unabhängige Release-Build
und fensterlose Tests. Hostverhalten, Live-Darstellung und Abhören in einer DAW
waren außerhalb dieses Prüfauftrags. Die drei Befunde betreffen
Anzeige und Darstellung.
