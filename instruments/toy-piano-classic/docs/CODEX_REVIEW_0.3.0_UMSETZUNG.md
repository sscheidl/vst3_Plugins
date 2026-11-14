# Umsetzung des Codex-Reviews 0.3.0

* **Stand:** 2026-11-14
* **Bericht:** [CODEX_REVIEW_0.3.0.md](CODEX_REVIEW_0.3.0.md), Urteil „Keine Freigabe",
  geprüft auf `main` @ `e3b17a7`
* **Umgesetzt in:** Toy Piano Classic 0.3.1
* **Umgesetzt von:** Claude Opus 5.5

Jeder Befund wurde zuerst am Code nachvollzogen. Sieben von acht sind
behoben, jeweils mit einem Test, der den Fehler in 0.3.0 gefunden hätte. R07
liegt im unveränderten Firmware-Kern und ist hier nur dokumentiert.

| ID | Befund | Bestätigt | Umsetzung | Test |
| --- | --- | --- | --- | --- |
| R01 | Editor bleibt in `editors_`, wenn der Host `removed()` auslässt; nächste Parameteränderung trifft freigegebenen Speicher | ja: `editorDestroyed` ist im SDK leer | `Controller::editorDestroyed` trägt aus wie `editorRemoved`; Einträge werden über den `EditorView`-Zeiger erkannt, nicht über das schon zerstörte `Editor`-Objekt. `Editor::~Editor` schließt ein noch offenes Fenster. | `TestEditorInWindow`: echtes Fenster in verborgenem Elternfenster, letzte Referenz ohne `removed()` freigegeben, danach `OpenEditorCount() == 0` und Parameteränderung |
| R02 | Volume-Glättung bleibt knapp unter dem Ziel stehen | ja: bei 48 kHz ist der Schritt nahe 1,0 kleiner als die Float-Auflösung | `Approach()` in `engine.cpp`: landet auf dem Ziel, sobald der Schritt den Wert nicht mehr ändert. Gilt auch für die Tonhöhe. | `TestGlidesReachTarget`: Volume und Tonhöhe bei 8–192 kHz exakt am Ziel, in beide Richtungen und von +24 dB; nach einer Volume-Fahrt bitgenau wie ohne |
| R03 | `fromString` nimmt „foo" und „12junk" an | ja, und schlimmer: Der SDK-`RangeParameter` von Master Pitch liest bei „foo" einen nicht initialisierten Wert | `value_text.h`: genau eine endliche Zahl, optional die Einheit, sonst nichts; unabhängig vom C-Locale, Dezimalkomma erlaubt. Eigene Klassen für alle fünf Parameter; „-inf" nur für Volume. Bei Ablehnung bleibt der Wert unverändert. | `TestTypedValues`: Parser direkt und über `getParamValueByString` für alle fünf Parameter |
| R04 | Abgelehnte Größe am Mindestzoom: Klaviatur abgeschnitten | ja | KEYS: Lehnt der Host ab, wird das Umschalten zurückgenommen. Bei geladenen Einstellungen oder vom Host erzwungenen Größen passt sich die Darstellung ins Fenster ein, auch unter 50 %; der gespeicherte Zoom bleibt in den Grenzen. | `TestEditorInWindow`: Ablehnung bei 100 % und bei 50 %, Projekt mit Klaviatur in zu kleinem Fenster |
| R05 | Fenster wird vor der Zustimmung des Hosts verändert | ja | `Resize()` fragt nur noch; das Fenster ändert sich erst in `onSize()`. Auch beim Öffnen wird zuerst die Größe gefüllt, die der Host vergeben hat. | `TestEditorInWindow`: Host liest bei der Anfrage alte Größe und alten Frame |
| R06 | Keine feste Obergrenze für Klaviatur-Noten pro Block | ja, Risiko ohne beobachtete Überlast | Höchstens 64 Noten (Queue-Größe) pro Block. Volle Queue meldet `kResultFalse` statt Erfolg. | `TestKeyboardQueueLimit` |
| R07 | Oberton schaltet beim Tonhöhen-Glide an der Viertel-Abtastrate hart um | ja, im Firmware-Kern (`toy_piano.cpp`, `Modulate`) | Nicht geändert: `core/` bleibt bytegleich mit Daisy-Seed. README-Aussage eingeschränkt und Grenze beschrieben. Behebung gehört in Daisy-Seed; die Firmware-Modulation ist davon ebenso betroffen. | — |
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

## Nachprüfung

Bitte R01–R06 und R08 erneut prüfen, gegen den Commit von 0.3.1. Für R07 ist
eine eigene Aufgabe in Daisy-Seed nötig.
