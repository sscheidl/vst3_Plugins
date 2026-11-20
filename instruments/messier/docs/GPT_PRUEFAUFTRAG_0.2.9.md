# Prüfauftrag für ChatGPT: Messier 0.2.9 in sechs Teilen

Codex fehlt bis nächste Woche. ChatGPT im Chat kann nicht bauen und keine Tests laufen
lassen, aber Dateien auf GitHub lesen. Deshalb ist die Prüfung in sechs kleine Teile
zerlegt. Jeder Teil steht für sich. Füge **einen Teil pro Chat** ein (oder nacheinander im
selben Chat) und gib das Ergebnis an Claude weiter; Claude prüft jeden Befund im Code nach
und trägt ihn in `docs/GPT_REVIEW_0.2.9.md` ein.

Alle Links zeigen auf den festen Stand `50fe057` (öffentliches Repository). Wenn der Chat
Links mit Zeilenmarken nicht öffnen kann, nimmt er die Raw-Adresse: dasselbe ohne
`#L…`, mit `raw.githubusercontent.com/sscheidl/vst3_Plugins/50fe05710cd65c54bbca266d883062a6697c3dcb/`
statt `github.com/sscheidl/vst3_Plugins/blob/50fe05710cd65c54bbca266d883062a6697c3dcb/`.

---

## Teil 1 von 6: Dateistatus zwischen den Plugin-Hälften

```text
Du prüfst Code eines eigenen Audio-Plugins (VST3, C++20, Windows). Nur lesen, nichts ausführen.
Lies genau diese Ausschnitte (Commit 50fe057, öffentliches Repository):

1. https://github.com/sscheidl/vst3_Plugins/blob/50fe05710cd65c54bbca266d883062a6697c3dcb/instruments/messier/source/file_loader.h#L62-L110
2. https://github.com/sscheidl/vst3_Plugins/blob/50fe05710cd65c54bbca266d883062a6697c3dcb/instruments/messier/source/file_loader.cpp#L95-L200
3. https://github.com/sscheidl/vst3_Plugins/blob/50fe05710cd65c54bbca266d883062a6697c3dcb/instruments/messier/source/message_text.h
4. https://github.com/sscheidl/vst3_Plugins/blob/50fe05710cd65c54bbca266d883062a6697c3dcb/instruments/messier/source/processor.cpp#L701-L725
5. https://github.com/sscheidl/vst3_Plugins/blob/50fe05710cd65c54bbca266d883062a6697c3dcb/instruments/messier/source/controller.cpp#L448-L500

Zusammenhang: Der Processor schickt dem Controller den Dateistatus als Nachricht. Texte
reisen als Binärattribute; ein leerer Text wird nicht gesendet, ein fehlender gilt als
leer (Studio One behält keine Binärattribute der Länge 0). Der Controller nimmt einen
Status nur, wenn FileStatus::IsCode und FileStatus::HasItsTexts gelten und die
Seriennummer im 32-Bit-Bereich liegt.

Fragen:
a) Gibt es einen regulären Status, den der Loader erzeugt (alle Wege in file_loader.cpp),
   den der Controller jetzt verwirft? Dann bliebe die Anzeige hängen.
b) Gibt es einen Status, der durchkommt, obwohl ein Text unterwegs verloren ging, und
   dann etwas Falsches anzeigt?
c) Wird last_status_serial_ je durch einen verworfenen Status verändert?

Regeln: Nur diese Ausschnitte. Brauchst du mehr, nenne Datei und Zeilen statt zu raten.
Keine Stilhinweise. Antworte auf Deutsch, je Frage "in Ordnung" mit einem Satz Begründung
oder ein Befund im Format:
G1.n · P1–P3 · Titel / Beleg Datei:Zeile / Auslöser / bewiesen oder vermutet / Korrektur in einem Satz.
```

---

## Teil 2 von 6: Skin-Dateien aus einem fremden Ordner

```text
Du prüfst Code eines eigenen Audio-Plugins (VST3, VSTGUI 4, C++20, Windows). Nur lesen.
Lies genau diese Ausschnitte (Commit 50fe057, öffentliches Repository):

1. https://github.com/sscheidl/vst3_Plugins/blob/50fe05710cd65c54bbca266d883062a6697c3dcb/instruments/messier/source/gui_skin.cpp#L350-L485
2. https://github.com/sscheidl/vst3_Plugins/blob/50fe05710cd65c54bbca266d883062a6697c3dcb/instruments/messier/source/gui_skin.cpp#L555-L640

Zusammenhang: Der Ordner Documents\Messier-Skin kann beliebige Dateien enthalten. PNGs
werden höchstens 32 MiB gelesen, Signatur und IHDR gegen die Sollgröße geprüft, dann aus
dem Speicher dekodiert und einmal ganz entpackt (lockPixels). Farbtabellen höchstens
64 KiB, Zeilen "name = #RRGGBB[AA]".

Fragen:
a) Kann eine Datei im Ordner einen Absturz, ein Lesen außerhalb der Grenzen, einen Hänger
   oder großen Speicherverbrauch auslösen? (Ordner statt Datei, Verknüpfung, Datei, die
   während des Lesens wächst, kaputte PNG, riesige IHDR-Maße, Tabelle mit seltsamen Zeilen)
b) Stimmt die Grenze in ReadLimited genau (64 KiB werden gelesen, 64 KiB + 1 Byte nicht)?
   Prüfe die Rechnung "got > limit - bytes.size()" auf Unterlauf.
c) ParseHex und ApplyColourTable: Indizes und Längen in Ordnung?

Regeln: Nur diese Ausschnitte. Brauchst du mehr, nenne Datei und Zeilen statt zu raten.
Keine Stilhinweise. Antworte auf Deutsch, je Frage "in Ordnung" mit Begründung oder:
G2.n · P1–P3 · Titel / Beleg Datei:Zeile / Auslöser / bewiesen oder vermutet / Korrektur in einem Satz.
```

---

## Teil 3 von 6: Dateidialog und Menüs (möglicher Absturz)

```text
Du prüfst Code eines eigenen Audio-Plugins (VST3, VSTGUI 4, C++20, Windows). Nur lesen.
Lies genau diese Ausschnitte (Commit 50fe057, öffentliches Repository):

1. https://github.com/sscheidl/vst3_Plugins/blob/50fe05710cd65c54bbca266d883062a6697c3dcb/instruments/messier/source/editor.cpp#L360-L380
2. https://github.com/sscheidl/vst3_Plugins/blob/50fe05710cd65c54bbca266d883062a6697c3dcb/instruments/messier/source/editor.cpp#L677-L720
3. https://github.com/sscheidl/vst3_Plugins/blob/50fe05710cd65c54bbca266d883062a6697c3dcb/instruments/messier/source/editor.cpp#L950-L1030
4. https://github.com/sscheidl/vst3_Plugins/blob/50fe05710cd65c54bbca266d883062a6697c3dcb/instruments/messier/source/editor.cpp#L1205-L1220
5. https://github.com/sscheidl/vst3_Plugins/blob/50fe05710cd65c54bbca266d883062a6697c3dcb/instruments/messier/source/editor.cpp#L1319-L1365
6. https://github.com/sscheidl/vst3_Plugins/blob/50fe05710cd65c54bbca266d883062a6697c3dcb/instruments/messier/source/widgets.cpp#L432-L456

Zusammenhang: Früher liefen Dateidialog und Menüs direkt im Mausklick des Knopfes. Schloss
der Host das Fenster, während Dialog oder Menü offen waren, arbeitete der Knopf danach auf
freigegebenem Speicher. Jetzt startet Editor::Later Dialog und Menüs aus der Haupt-
Warteschlange (VSTGUI Tasks::mainQueue), nach dem Klick, mit Prüfung von alive_ und
window_serial_. Editor::close gibt den CFrame frei.

Fragen:
a) Ist nach der Änderung noch ein Weg übrig, auf dem nach Rückkehr aus dem Dialog oder
   Menü freigegebener Speicher berührt wird (this, frame, menu, selector, Knopf)?
   Achte besonders auf PopUpMenu: `frame` nach popup(), wenn das Fenster inzwischen zu ist.
b) Kann die Aufgabe laufen, nachdem der Editor zerstört ist (die Lambda fängt this)?
c) Gibt es einen Weg, auf dem zwei Dialoge gleichzeitig offen sind (Doppelklick auf LOAD)?

Regeln: Nur diese Ausschnitte. Brauchst du VSTGUI-Quellen, nenne die Datei statt zu raten.
Keine Stilhinweise. Antworte auf Deutsch, je Frage "in Ordnung" mit Begründung oder:
G3.n · P1–P3 · Titel / Beleg Datei:Zeile / Auslöser / bewiesen oder vermutet / Korrektur in einem Satz.
```

---

## Teil 4 von 6: Neue Datei während STOP

```text
Du prüfst Code eines eigenen Audio-Plugins (VST3, C++20). Nur lesen.
Lies genau diese Ausschnitte (Commit 50fe057, öffentliches Repository):

1. https://github.com/sscheidl/vst3_Plugins/blob/50fe05710cd65c54bbca266d883062a6697c3dcb/instruments/messier/engine/looper_engine.cpp#L186-L245
2. https://github.com/sscheidl/vst3_Plugins/blob/50fe05710cd65c54bbca266d883062a6697c3dcb/instruments/messier/engine/looper_engine.h#L120-L150
3. https://github.com/sscheidl/vst3_Plugins/blob/50fe05710cd65c54bbca266d883062a6697c3dcb/instruments/messier/source/processor.cpp#L598-L620

Zusammenhang: Ein Dateiwechsel blendet die alte Datei im Kern aus (256 Samples), der Kern
quittiert, dann tauscht die Engine. Während STOP oder Bypass ruht der Ausgang und es wird
nichts gerendert. Neu: Ruht der Ausgang zu Blockbeginn und läuft ein Wechsel, rendert
FinishSwapSilently bis zu 16 Quanten (je 64 Samples) ins Leere, bis der Tausch fertig ist.
Das läuft im Audio-Thread.

Fragen:
a) Kann FinishSwapSilently im Audio-Thread allokieren, sperren oder unbegrenzt laufen?
b) Kann ein Wechsel, der während des Ruhens mehrfach ersetzt wird (SwapFile mehrmals),
   eine Datei verlieren oder doppelt freigeben?
c) Was passiert, wenn STOP im selben Block wieder aufgehoben wird, in dem die neue Datei
   ankommt? Ist danach etwas von der alten Datei zu hören?

Regeln: Nur diese Ausschnitte. Brauchst du mehr, nenne Datei und Zeilen statt zu raten.
Keine Stilhinweise. Antworte auf Deutsch, je Frage "in Ordnung" mit Begründung oder:
G4.n · P1–P3 · Titel / Beleg Datei:Zeile / Auslöser / bewiesen oder vermutet / Korrektur in einem Satz.
```

---

## Teil 5 von 6: Fenstereinstellungen über mehrere Fenster

```text
Du prüfst Code eines eigenen Audio-Plugins (VST3, VSTGUI 4, C++20). Nur lesen.
Lies genau diese Ausschnitte (Commit 50fe057, öffentliches Repository):

1. https://github.com/sscheidl/vst3_Plugins/blob/50fe05710cd65c54bbca266d883062a6697c3dcb/instruments/messier/source/controller.cpp#L92-L115
2. https://github.com/sscheidl/vst3_Plugins/blob/50fe05710cd65c54bbca266d883062a6697c3dcb/instruments/messier/source/controller.cpp#L154-L215
3. https://github.com/sscheidl/vst3_Plugins/blob/50fe05710cd65c54bbca266d883062a6697c3dcb/instruments/messier/source/controller.cpp#L548-L615
4. https://github.com/sscheidl/vst3_Plugins/blob/50fe05710cd65c54bbca266d883062a6697c3dcb/instruments/messier/source/editor.cpp#L336-L345
5. https://github.com/sscheidl/vst3_Plugins/blob/50fe05710cd65c54bbca266d883062a6697c3dcb/instruments/messier/source/editor.cpp#L815-L835
6. https://github.com/sscheidl/vst3_Plugins/blob/50fe05710cd65c54bbca266d883062a6697c3dcb/instruments/messier/source/editor.cpp#L1150-L1215

Zusammenhang: Der Controller hält die Fenstereinstellungen (editor_). Eine Änderung in
einem Fenster geht über callbacks.changed an die anderen angemeldeten Fenster
(ApplySettings). Ein Fenster ohne geöffnetes Fenster holt die Einstellungen in getSize und
beim Öffnen (callbacks.current). Nach einem Projekt oder einer neuen Verbindung vergessen
die Fenster eine gehaltene Taste (ForgetHeldKey).

Fragen:
a) Kann ApplySettings auf einem anderen Fenster zurück in callbacks.changed führen
   (Endlosschleife, doppelte Größenänderung)?
b) Bleibt `*self` gültig, solange die Lambda lebt? Was, wenn der Editor zerstört ist?
c) Kann getSize vor dem Öffnen dem Host eine Größe melden, die nicht zu den später
   verwendeten Einstellungen passt?

Regeln: Nur diese Ausschnitte. Brauchst du mehr, nenne Datei und Zeilen statt zu raten.
Keine Stilhinweise. Antworte auf Deutsch, je Frage "in Ordnung" mit Begründung oder:
G5.n · P1–P3 · Titel / Beleg Datei:Zeile / Auslöser / bewiesen oder vermutet / Korrektur in einem Satz.
```

---

## Teil 6 von 6: Abbrechbares Lesen einer WAV-Datei

```text
Du prüfst Code eines eigenen Audio-Plugins (C++20). Nur lesen.
Lies genau diese Ausschnitte (Commit 50fe057, öffentliches Repository):

1. https://github.com/sscheidl/vst3_Plugins/blob/50fe05710cd65c54bbca266d883062a6697c3dcb/instruments/messier/source/wav_reader.h#L28-L40
2. https://github.com/sscheidl/vst3_Plugins/blob/50fe05710cd65c54bbca266d883062a6697c3dcb/instruments/messier/source/wav_reader.cpp#L113-L250
3. https://github.com/sscheidl/vst3_Plugins/blob/50fe05710cd65c54bbca266d883062a6697c3dcb/instruments/messier/source/file_loader.cpp#L55-L70
4. https://github.com/sscheidl/vst3_Plugins/blob/50fe05710cd65c54bbca266d883062a6697c3dcb/instruments/messier/source/file_loader.cpp#L251-L323

Zusammenhang: Ein Arbeits-Thread liest WAV-Dateien, die der Nutzer im Fenster wählt.
Neu: WavCancel wird je Chunk-Kopf und je MiB Audiodaten gefragt; der Thread gibt auf,
wenn eine neuere Anfrage (newest_ticket_) oder das Ende (stopping_) kommt. Ein
abgebrochenes Ergebnis wird über die Ticket-Prüfung in Finish verworfen.

Fragen:
a) Kann ein abgebrochenes Lesen dennoch als Ergebnis angezeigt oder abgespielt werden?
b) Datenwettlauf: Sind newest_ticket_ und stopping_ richtig gesetzt und gelesen, auch im
   Destruktor während eines Lesens?
c) Gibt es eine WAV-Datei, bei der das Lesen trotz Abbruch lange weiterläuft (z. B. sehr
   viele winzige Chunks, sehr großer Chunk ohne Daten)?

Regeln: Nur diese Ausschnitte. Brauchst du mehr, nenne Datei und Zeilen statt zu raten.
Keine Stilhinweise. Antworte auf Deutsch, je Frage "in Ordnung" mit Begründung oder:
G6.n · P1–P3 · Titel / Beleg Datei:Zeile / Auslöser / bewiesen oder vermutet / Korrektur in einem Satz.
```
