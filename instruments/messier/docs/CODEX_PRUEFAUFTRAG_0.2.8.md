# Kurzer Prüfauftrag für Codex: Messier 0.2.8, MS-R58 bis MS-R62

Codex hat nur noch wenig Wochenkontingent. Prüfe deshalb nur, ob die fünf Befunde deines
Kurzreviews ([CODEX_REVIEW_0.2.7.md](CODEX_REVIEW_0.2.7.md)) behoben sind. Der vollständige
[CODEX_PRUEFAUFTRAG_0.2.7.md](CODEX_PRUEFAUFTRAG_0.2.7.md) bleibt für später offen.

Prüfe den Arbeitsstand oder den Commit von 0.2.8 auf `messier-gui` (Worktree
`.work/messier-gui/wt`, Pfade unter `instruments/messier/`), verglichen mit `8332f66`:
`git -C .work/messier-gui/wt diff 8332f66 -- instruments/messier/source`. Lies keine
anderen Dateien, solange ein Punkt es nicht verlangt. **Nicht bauen, keine Tests, keinen
Validator laufen lassen.** Claudes Build mit allen Tests, ASan und den Mutationsproben ist grün
(siehe [BUILD_REPORT_0.2.8.md](BUILD_REPORT_0.2.8.md)).

## Die fünf Punkte

1. **MS-R58:** `FileStatus::IsCode` und `FileStatus::HasItsTexts` in `source/file_loader.h`.
   Ihr Aufruf in `Controller::notify` kommt vor dem Seriennummernvergleich.
   `SetText` meldet in `source/message_text.h` einen nicht gespeicherten Text.
   `SendFileStatusUnchecked` in `source/processor.cpp` sendet dann keinen Status.
   Frage: Gibt es einen regulären Status des Loaders, den diese Prüfung jetzt verwirft?
   Die Regel lautet: Ton ≠ „none“ genau dann, wenn ein Pfad da ist, und eine laufende oder
   gescheiterte Anfrage braucht ihren Pfad.
2. **MS-R59:** Bereichsprüfung von `serial` in `Controller::notify`.
3. **MS-R60:** `ReadLimited`, `BitmapFromFolders` und `SkinPngSize` in
   `source/gui_skin.cpp`. Die Datei wird aus dem offenen Stream gelesen, höchstens 32 MiB.
   Danach werden Signatur und IHDR mit Länge 13 gegen das Sollmaß geprüft. Erst dann gehen
   dieselben Bytes an `createBitmapFromMemory`.
4. **MS-R61:** `ReadSkinTable` liest über `ReadLimited` höchstens 64 KiB.
5. **MS-R62:** Der Zoom außerhalb von 0,5 bis 2,0 verwirft in `Controller::setState` den
   ganzen Zustand. Die übrigen Felder wurden schon verworfen und nicht begrenzt.

## Ergebnis

Ergänze `docs/CODEX_REVIEW_0.2.7.md` um einen Abschnitt „Nachprüfung 0.2.8“: je Befund
„behoben“, „teilweise“ oder „offen“ mit einem Satz. Ein neuer Befund bekommt eine Nummer ab
MS-R63, mit Datei und Zeile, Schweregrad P1 bis P3 und einem Satz zur Behebung. Ändere
keinen Code.
