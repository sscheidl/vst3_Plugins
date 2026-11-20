# Messier 0.2.7 – Kurzreview

Geprüft per Quelltextlektüre auf `messier-gui` bei `8332f66`.

## 1. Dateistatus

- **MS-R58 · P3 – Unvollständiger Status wird übernommen.** [`controller.cpp:454–482`](../source/controller.cpp#L454) akzeptiert auch `kReady` ohne Dateipfad und `kLoading` ohne Anfragepfad. Geht ein nichtleeres Attribut verloren, etwa nach einem ungeprüften `setBinary`-Fehler ([`message_text.h:16–18`](../source/message_text.h#L16)), kann die Anzeige trotz geladenem Sample auf „No sample“ springen. **Behebung.** Schreibfehler bei Statusattributen abfangen und die je Statuscode nötigen Pfade vor der Übernahme prüfen.
- Ein regulär leerer Pfad ersetzt den alten Dateinamen ([`editor.cpp:1319–1325`](../source/editor.cpp#L1319)). Bei gültigen Seriennummern werden ältere Antworten und Antworten aus früheren Verbindungen verworfen ([`controller.cpp:466–471`](../source/controller.cpp#L466)). `GetText` liest nur den Pflichtpfad der Ladeanfrage ([`processor.cpp:812–816`](../source/processor.cpp#L812)).
- **MS-R59 · P3 – Ungültige Seriennummer kann die Anzeige auf einem alten Status festhalten.** [`controller.cpp:459–471`](../source/controller.cpp#L459) wandelt eine ungeprüfte negative Seriennummer wie `-1` in `UINT32_MAX` um und weist danach reguläre Antworten als älter ab. **Behebung.** `serial` vor der Umwandlung auf den `uint32_t`-Wertebereich prüfen.

## 2. Skin-Dateien

- **MS-R60 · P2 – PNG-Abmessungen werden erst nach dem Start des Decoders geprüft.** [`gui_skin.cpp:351–373`](../source/gui_skin.cpp#L351) begrenzt die Dateigröße auf 32 MiB und übergibt die Datei vor der Pixelprüfung an VSTGUI. Unter Windows öffnet WIC den Decoder und initialisiert den Frame und den Formatkonverter vor der Rückkehr (`d2dbitmap.cpp:133–171` im eingebundenen VSTGUI). Das lässt den Ressourcenverbrauch bis zur Größenprüfung offen. Eine vollständige Pixel-Allokation im Gigabytebereich oder ein Absturz ist anhand dieses Ladepfads nicht belegt. **Behebung.** Höchstens 32 MiB einlesen, PNG-Signatur und ersten `IHDR`-Block mit Datenlänge 13 prüfen, Breite und Höhe mit dem exakten `@1x`- bzw. `@2x`-Sollmaß vergleichen und dieselben geprüften Bytes dekodieren.
- **MS-R61 · P3 – Die 64-KiB-Grenze für Farbtabellen gilt nur vor dem Öffnen.** [`gui_skin.cpp:379–393`](../source/gui_skin.cpp#L379) liest bis zum Dateiende, auch wenn die Datei seit der Größenprüfung gewachsen ist oder ersetzt wurde. Bei einer statischen Tabelle sind für fehlerhafte Zeilen und Hexwerte keine Zugriffe außerhalb der Grenzen erkennbar ([`gui_skin.cpp:325–346`](../source/gui_skin.cpp#L325), [`gui_skin.cpp:524–554`](../source/gui_skin.cpp#L524)). **Behebung.** Aus dem geöffneten Stream höchstens 64 KiB plus ein Prüfbyte lesen und zu große Tabellen verwerfen.

## 3. Fensterzustand

- Das Format ist in Ordnung ([`controller.cpp:277–296`](../source/controller.cpp#L277), [`parameters.h:280–286`](../source/parameters.h#L280)). V5 liest die alte Nachleuchtdauer und behält den WAVE-Presetdefault. V6 verlangt 25–200 %, V7 akzeptiert zusätzlich `kPresetWave = 0`. Lesefehler und ungültige Nachleucht- oder WAVE-Werte führen zum Abbruch vor `editor_ = settings`.
- **MS-R62 · P3 – Endlicher Zoom außerhalb des Bereichs wird übernommen.** [`controller.cpp:274–296`](../source/controller.cpp#L274) prüft nur auf Endlichkeit, begrenzt den Wert anschließend auf 0,5–2,0 und übernimmt den übrigen Zustand ([`editor.h:53–59`](../source/editor.h#L53)). **Behebung.** Für die im Auftrag geforderte vollständige Ablehnung auch die Zoomgrenzen vor `editor_ = settings` prüfen.
