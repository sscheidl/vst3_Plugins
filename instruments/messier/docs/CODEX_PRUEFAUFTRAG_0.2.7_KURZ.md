# Kurzer Prüfauftrag für Codex: Messier 0.2.7, drei Stellen

Codex hat nur noch wenig Wochenkontingent. Dieser Auftrag ersetzt deshalb für jetzt den
vollständigen [CODEX_PRUEFAUFTRAG_0.2.7.md](CODEX_PRUEFAUFTRAG_0.2.7.md). Der vollständige
Auftrag bleibt für später offen.

Prüfe nur den Code, den die drei Punkte unten nennen, im Commit `8332f66` auf `messier-gui`
(Worktree `.work/messier-gui/wt`, Pfade unter `instruments/messier/`). Lies keine anderen
Dateien, solange ein Punkt es nicht verlangt. **Nicht bauen, keine Tests, keinen Validator
laufen lassen.** Claudes Build mit allen Tests ist grün; hier geht es um Fehler, die
keine Tests finden.

## 1. Dateistatus ohne leere Texte (Studio One)

Studio One gibt binäre Attribute der Länge 0 nicht weiter. Bisher hat der Controller
deshalb jeden Status verworfen.

* `source/message_text.h` (48 Zeilen): `SetText`, `GetText`, `GetOptionalText`.
* `source/processor.cpp` ab Zeile 697 (`SendFileStatusUnchecked`).
* `source/controller.cpp` ab Zeile 445 (der Statuszweig in `notify`).

Frage: Kann ein Status jetzt falsch angenommen werden? Zum Beispiel ein alter Name, der
stehen bleibt, weil ein fehlender Pfad als leer gilt, oder ein Pfad, der fehlt, wo er Pflicht
ist. Gibt es eine weitere Nachricht mit einem Text, der leer sein kann und noch über
`GetText` gelesen wird?

## 2. Skin-Dateien aus fremden Ordnern

`Documents\Messier-Skin` kann jeder Benutzer füllen. Dort liegen fremde Daten.

* `source/gui_skin.cpp`, Zeilen 324 bis 400 (`ParseHex`, `BitmapFromFile`,
  `BitmapFromFolders`, `ReadSkinTable`) und der `Skin`-Konstruktor ab Zeile 559.

Frage vor allem: `BitmapFromFile` begrenzt die Datei auf 32 MB, prüft die Pixelgröße aber
erst nach dem Dekodieren. Kann eine kleine PNG-Datei mit riesigen Abmessungen beim
Dekodieren Gigabytes belegen oder abstürzen, bevor `getSize()` sie verwirft? Falls ja:
Wie lässt sich die Größe vorher sicher prüfen, zum Beispiel aus dem IHDR-Block? Ist außerdem
ein Lesen außerhalb der Grenzen oder ein Absturz durch eine kaputte Farbtabelle möglich?

## 3. Fensterzustand Version 7 (dein Handoff vom 19.11.)

* `source/controller.cpp`, Zeilen 263 bis 300 (`setState`), `source/parameters.h` (Bereich von WAVE
  und `kPresetWave`).

Frage: Lesen sich die Versionen 5, 6 und 7 korrekt? Wird ein Zustand mit einem Wert außerhalb
des Bereichs ganz verworfen und nicht halb übernommen?

## Ergebnis

Schreibe das Ergebnis nach `docs/CODEX_REVIEW_0.2.7.md`, kurz: je Punkt „in Ordnung“ oder
ein Befund ab MS-R58, mit Datei und Zeile, Schweregrad P1 bis P3 und einem Satz zur
Behebung. Ändere keinen Code.
