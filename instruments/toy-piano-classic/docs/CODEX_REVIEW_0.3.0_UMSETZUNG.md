# Umsetzung des Codex-Reviews 0.3.0

* **Stand:** 2026-11-14
* **Bericht:** [CODEX_REVIEW_0.3.0.md](CODEX_REVIEW_0.3.0.md), Urteil „Keine Freigabe",
  geprüft auf `main` @ `e3b17a7`
* **Umgesetzt in:** Toy Piano Classic 0.3.1, R07 in 0.3.2
* **Umgesetzt von:** Claude Opus 5.5

Jeder Befund wurde zuerst am Code nachvollzogen. Alle acht sind behoben,
jeweils mit einem Test, der den Fehler in 0.3.0 gefunden hätte. R07 liegt im
Firmware-Kern; behoben ist er nur im Plugin, der Kern bleibt unverändert.

| ID | Befund | Bestätigt | Umsetzung | Test |
| --- | --- | --- | --- | --- |
| R01 | Editor bleibt in `editors_`, wenn der Host `removed()` auslässt; nächste Parameteränderung trifft freigegebenen Speicher | ja: `editorDestroyed` ist im SDK leer | `Controller::editorDestroyed` trägt aus wie `editorRemoved`; Einträge werden über den `EditorView`-Zeiger erkannt, nicht über das schon zerstörte `Editor`-Objekt. `Editor::~Editor` schließt ein noch offenes Fenster. | `TestEditorInWindow`: echtes Fenster in verborgenem Elternfenster, letzte Referenz ohne `removed()` freigegeben, danach `OpenEditorCount() == 0` und Parameteränderung |
| R02 | Volume-Glättung bleibt knapp unter dem Ziel stehen | ja: bei 48 kHz ist der Schritt nahe 1,0 kleiner als die Float-Auflösung | `Approach()` in `engine.cpp`: landet auf dem Ziel, sobald der Schritt den Wert nicht mehr ändert. Gilt auch für die Tonhöhe. | `TestGlidesReachTarget`: Volume und Tonhöhe bei 8–192 kHz exakt am Ziel, in beide Richtungen und von +24 dB; nach einer Volume-Fahrt bitgenau wie ohne |
| R03 | `fromString` nimmt „foo" und „12junk" an | ja, und schlimmer: Der SDK-`RangeParameter` von Master Pitch liest bei „foo" einen nicht initialisierten Wert | `value_text.h`: genau eine endliche Zahl, optional die Einheit, sonst nichts; unabhängig vom C-Locale, Dezimalkomma erlaubt. Eigene Klassen für alle fünf Parameter; „-inf" nur für Volume. Bei Ablehnung bleibt der Wert unverändert. | `TestTypedValues`: Parser direkt und über `getParamValueByString` für alle fünf Parameter |
| R04 | Abgelehnte Größe am Mindestzoom: Klaviatur abgeschnitten | ja | KEYS: Lehnt der Host ab, wird das Umschalten zurückgenommen. Bei geladenen Einstellungen oder vom Host erzwungenen Größen passt sich die Darstellung ins Fenster ein, auch unter 50 %; der gespeicherte Zoom bleibt in den Grenzen. | `TestEditorInWindow`: Ablehnung bei 100 % und bei 50 %, Projekt mit Klaviatur in zu kleinem Fenster |
| R05 | Fenster wird vor der Zustimmung des Hosts verändert | ja | `Resize()` fragt nur noch; das Fenster ändert sich erst in `onSize()`. Auch beim Öffnen wird zuerst die Größe gefüllt, die der Host vergeben hat. | `TestEditorInWindow`: Host liest bei der Anfrage alte Größe und alten Frame |
| R06 | Keine feste Obergrenze für Klaviatur-Noten pro Block | ja, Risiko ohne beobachtete Überlast | Höchstens 64 Noten (Queue-Größe) pro Block. Volle Queue meldet `kResultFalse` statt Erfolg. | `TestKeyboardQueueLimit` |
| R07 | Oberton schaltet beim Tonhöhen-Glide an der Viertel-Abtastrate hart um | ja, im Firmware-Kern (`toy_piano.cpp`, `Modulate`) | 0.3.2, nur im Plugin (Entscheidung des Nutzers): `Engine` übergibt über `Modulate` ein tonhöhenabhängiges Gewicht, das zwischen 0,20 und 0,25 der Abtastrate linear auf 0 fällt. Keine Kosten pro Sample. `core/` bleibt bytegleich. Folgen: Noten im Band (123–126 bei 48 kHz) klingen anders als in der Firmware; ein Sprung von zwei Oktaven durchquert das Band in wenigen Glide-Schritten, die Stufen erreichen dort bis zu einem Drittel des alten Sprungs. Die Firmware hat den Sprung weiterhin. | `TestHarmonicFade`: Gewicht folgt der Tonhöhe exakt, Stufen beim Glide gemessen (Pitch Wheel 6 %, zwei Oktaven ≤ 33 %), alle 128 Noten bitgenau wie die Firmware außer 123–126 |
| R08 | Bitgleichheit „7 Blockgrößen, 32 und 64 Bit" nur teilweise geprüft | ja: 64 Bit nur bei einer Blockgröße | Test erweitert statt Aussage gekürzt: alle sieben Blockgrößen jeweils in 32 und 64 Bit | `TestBitExactAtPitchZero` |

## Weitere Punkte aus dem Bericht

* `drag_` wird beim Öffnen zurückgesetzt (Abschnitt 6 des Berichts).
* Die Testausgabe ist ungepuffert, damit bei einem Absturz sichtbar bleibt,
  bis wohin der Test kam.
* Nicht geändert, weil Produktentscheidung oder ohne Fehler: Pitch Bend bleibt
  automatisierbar; mehrere gleichzeitig offene Fenster gleichen ihre Größe nicht
  untereinander ab; die vier Parameter-Atomics sind keine atomare
  Zustandstransaktion; Laden eines Zustands setzt einen aktuell gebogenen
  Pitch Bend nicht zurück. Alle vier stehen im Bericht unter „Ohne Befund".

## Nachprüfung durch Codex (0.3.1)

Bericht: [CODEX_REVIEW_0.3.1_NACHPRUEFUNG.md](CODEX_REVIEW_0.3.1_NACHPRUEFUNG.md),
Urteil „Freigabe mit Korrekturen", geprüft auf `3aa0333`. R01, R02, R04, R05,
R06 und R08 bestätigt behoben. Daraus in 0.3.2:

| ID | Befund | Umsetzung | Test |
| --- | --- | --- | --- |
| R03-N1 | `+-5` wurde als `-5` angenommen: Nach dem Abschneiden des `+` ist der Rest eine gültige Zahl | `ParseTypedNumber` lehnt nach einem `+` ein weiteres Vorzeichen ab | `TestTypedValues`: `+-5`, `++5`, `-+5`, `+-5 ms`, `+ 5` direkt; `+-5`, `+-5 dB`, `++5` für alle fünf Parameter |
| Test zu R06 | Kein Test belegt die Obergrenze von 64 Noten pro Block bei gleichzeitigem Nachfüllen | Nicht ergänzt. Ein deterministischer Test bräuchte gleichzeitiges Nachfüllen aus einem zweiten Thread während eines `process`-Aufrufs; die Grenze ist eine feste Schleifenbedingung in `Render` und durch den Code belegt | — |

## Offen

R07 ist in 0.3.2 nicht nachgeprüft. Soll die Firmware denselben Ausgleich
bekommen, ist das eine eigene Aufgabe in Daisy-Seed.
