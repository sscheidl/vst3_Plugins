# Planung

Stand: 2026-11-14. Das erste Testobjekt, Toy Piano Classic 0.3.2, ist gebaut
und automatisch geprüft. In Studio One lädt es, die Regler reagieren. Angehört
wurde es noch nicht; Waveform und Live stehen aus. Dazugekommen sind Torus
Classic und, als dritter Port, Mendelejew (Projekt Elements), seit 2026-11-17 als
vierter Messier (Projekt messier); ihr Stand steht in den Abschnitten unten.

## Entscheidungen

Getroffen am 2026-11-12.

### Technische Basis und Repository

| Thema | Entscheidung |
| --- | --- |
| Framework | Steinberg VST3 SDK direkt, GUI mit VSTGUI |
| Plattform | Nur Windows x64 |
| Formate | Nur VST3 |
| Sichtbarkeit und Lizenz | Öffentlich, MIT |
| Hersteller | TAUREON |
| Code aus Daisy-Seed | Kopie mit Commit-Vermerk |
| SDK-Einbindung | Git-Submodul `external/vst3sdk`, Tag `v3.8.0_build_66`, nur die fünf benötigten Teile |
| C++ und Laufzeit | C++20, statische MSVC-Runtime: kein VC-Redistributable nötig |
| Build-Struktur | Ein Wurzelprojekt, je Plugin ein Unterverzeichnis |
| Installation | Von Hand aus dem Build-Ordner, keine automatische Verknüpfung |
| Dokumentation | README-Dateien englisch, Planung deutsch |

Die letzten fünf Zeilen sind bisher Vorschläge, die so umgesetzt wurden. Sie
lassen sich ändern, solange kein zweites Plugin darauf aufbaut.

### Toy Piano Classic

| Thema | Entscheidung |
| --- | --- |
| „Classic" | Klangkern ohne Modulationsmatrix, Ambient und Tape |
| Note-Off | Wird ignoriert, jeder Anschlag ist ein One-Shot |
| Master Pitch | Halbtonschritte, −24 bis +24 |
| Polyphonie | 16 Stimmen |
| Pegel | Eigener Volume-Regler |
| MIDI | Noten mit Velocity, Pitch Bend, CCs für Attack, Decay und Pitch |
| GUI | Regler mit Wertanzeige. Zunächst feste Größe; seit 2026-11-13 frei skalierbar mit festem Seitenverhältnis, 50 bis 200 % |
| Bildschirm-Klaviatur | Seit 0.3.0: zwei Oktaven unter den Reglern, per Knopf ein- und ausblendbar |
| Test-Hosts | Studio One, Tracktion Waveform 14, Ableton Live 12 Lite |

Bei der Umsetzung selbst festgelegt, weil die Vorgaben es offen ließen:

| Thema | Festlegung | Begründung |
| --- | --- | --- |
| Volume: Bereich | Stille, dann −60 bis +24 dB, Vorgabe 0 dB | 0 dB ist der Pegel der Firmware; damit bleibt die Vorgabe bitgenau gleich |
| Volume: Position | Vor dem Soft-Limiter des Kerns | Der Ausgang bleibt bei jeder Stellung unter 0,8; lauter heißt dichter, nicht übersteuert |
| Pitch-Bend-Bereich | ±2 Halbtöne, stufenlos | Üblicher Standardwert |
| CC-Nummern | Attack 73, Decay 75, Master Pitch 16 | 73 und 75 sind im MIDI-Standard für diese Zeiten vorgesehen; für Transposition gibt es keine Standardnummer |
| Vorgabewerte | Attack 2 ms, Decay 583 ms | Kurzer Anschlag, mittlerer Ausklang |
| Master Pitch: Wirkung | Sofort auf klingende Noten, über etwa 5 ms geglättet | Nur so ist der Regler ohne Knackser automatisierbar |
| Messlatte | Bitgenau gleich dem Kern bei Master Pitch 0 und Volume 0 dB | Schärfster möglicher Nachweis, dass die Hülle den Klang nicht verändert |
| Klaviatur: Umfang | 25 Tasten, C4 bis C6 | Zwei Oktaven samt oberem C; derselbe Tonumfang wie die 16 Hardware-Tasten der Firmware |
| Klaviatur: Anschlag | Feste Velocity 100 | Mit diesem Wert sendet die Firmware ihre Hardware-Tasten |
| Klaviatur: Notenweg | VST3-Nachricht vom Controller an den Processor, dort über die lock-free Queue der Firmware in den Audio-Thread | Der vorgesehene VST3-Weg zwischen den beiden Hälften; die Queue ist unverändert aus der Firmware übernommen |
| Klaviatur: Vorgabe | Eingeblendet; der Zustand wird mit dem Projekt gespeichert | Projekte aus 0.2.0 öffnen mit Klaviatur |
| Oberton an der Abschaltgrenze (Review R07) | Seit 0.3.2 im Plugin linear ausgeblendet zwischen 0,20 und 0,25 der Abtastrate; Firmware vorerst unverändert | Billigste Lösung: keine Rechenlast pro Sample. Vom Nutzer so entschieden (2026-11-14). Noten 123–126 (48 kHz) klingen dadurch anders als in der Firmware |

### Kekulé (Projektname Torus Classic)

Zweiter Port, Auftrag vom 2026-11-14
([Buildauftrag](CLAUDE_BUILDAUFTRAG_TORUS_CLASSIC_0.1.0.md)). Version 0.1.0 lädt
und spielt in Studio One (erster DAW-Test des Nutzers am 2026-11-14). Codex
fand in 0.1.0 vier Fehler; sie sind seit 0.1.1 behoben. 0.2.0 bringt die
Ergänzungen aus dem DAW-Test und ist gebaut und automatisch geprüft, in
einer DAW aber noch nicht ausprobiert. Codex hat 0.2.1 für den DAW-Test
freigegeben; der eine offene Punkt der Priorität P3 (TC-R10) ist seit 0.3.0
behoben. Seit 0.2.2 heißt das Plugin Kekulé. 0.3.0 brachte die drei
Folgeschritte aus der Nachprüfung von 0.2.0 (wählbare Stimmenzahl,
Ruhe-Erkennung für Plugin-Nap, Schloss statt FREE) und dreifarbige
Reglerringe; Codex gab es nicht frei (drei Befunde zur Ruhe-Meldung, einer
zur Stimmenreihenfolge). 0.3.1 nimmt die Ruhe-Meldung wieder heraus (kein
Plugin-Nap), bringt Oktavtasten an der Bildschirm-Klaviatur und die
Reglerfarben von Mendelejew; gebaut und automatisch geprüft, noch nicht in
einer DAW; Codex hat 0.3.1 ohne neue Befunde freigegeben. Was sich wann geändert hat, steht im
[Änderungsprotokoll](../instruments/torus-classic/CHANGELOG.md). Einzelheiten und Messwerte stehen im
[Build-Bericht](../instruments/torus-classic/docs/BUILD_REPORT_0.1.0.md), die
offene Abnahme in der
[DAW-Prüfliste](../instruments/torus-classic/docs/DAW_TEST_0.1.0.md).

Vorgegeben waren Umfang, Parameter-IDs, MIDI-Zuordnung, 48 kHz und 12 Samples
intern sowie die Anschlagsregel. Bei der Umsetzung selbst festgelegt, weil der
Auftrag es offen ließ:

| Thema | Festlegung | Begründung |
| --- | --- | --- |
| Name | Das Plugin heißt seit 0.2.2 „Kekulé" (Anzeigename mit Akzent, Fenstertitel); das Bundle heißt `Kekule.vst3`. „Torus Classic" bleibt interner Projektname: Verzeichnis, Branch, Build-Targets, Namensräume, Testnamen | **Entscheidung des Nutzers vom 2026-11-15:** nach August Kekulé, dem Entdecker des Benzolrings (Rings, Ring, Kekulé). Der Akzent steht nur in der Unicode-Klasseninfo, die heutige Hosts lesen; die 8-Bit-Klasseninfo und der Dateiname bleiben ASCII. Klassen-IDs, Parameter und Zustandsformat sind die von Torus Classic, alte Projekte laden |
| Sampleratenwandlung | Eigener Kaiser-Fenster-Sinc mit 96 Abgriffen im Plugin, keine fremde Bibliothek | Das SDK enthält keinen Resampler. Rund 300 Zeilen, mit Messungen an 15 Raten geprüft (endliches Frequenzraster, keine Schranke für jede Zwischenfrequenz), keine neue Abhängigkeit und kein Download. Abweichung vom Wortlaut des Auftrags („kleine geeignete Abhängigkeit"); bei Bedarf austauschbar, die Schnittstelle des Adapters bleibt |
| Latenz | 0 bei 48 kHz, sonst rund 1 ms (49/90/97/195 Samples bei 44,1/88,2/96/192 kHz) | Kleinste Verzögerung, bei der kein Ausgabe-Sample ein Quantum braucht, in das ein späterer Block noch Ereignisse legen könnte |
| Parameterautomation | Jeder Punkt wirkt als Stufe am nächsten 12-Sample-Raster; keine Rampen zwischen Punkten | Die Engine glättet ohnehin über 5 ms; nur so ist das Ergebnis unabhängig von der Blockgröße |
| Denormale Zahlen | Während des Renderns als null behandelt (FTZ/DAZ) | Gemessen: ausgeklungene Stimmen kosten sonst rund 39 % statt 0,9 % einer CPU. Unterschied zur Firmware-Arithmetik unter 1e-38 |
| Gleitkomma-Modell | Eigener Code mit `/fp:precise`, nicht mit dem `/fp:fast` des SDK | Der NaN-Schutz der Ausgangsstufe und der bitgenaue Referenzvergleich brauchen IEEE-Verhalten |
| Zufallsgenerator | Je Instanz ein eigener, bei jeder Aktivierung und jedem Laden auf denselben Startwert gesetzt | Instanzen stören sich nicht; ein Projekt rendert jedes Mal gleich |
| Reset | Aktivierung und Laden eines Zustands: Stille, Zeit null, keine wartenden Noten | Vom Auftrag verlangt; `setProcessing` löst keinen Reset aus, damit ein Transport-Stopp keine Ausklänge kappt |
| Bildschirm-Klaviatur | Noten, die nach 500 ms kein Audioblock abgeholt hat, verfallen | Tasten, die bei stehendem Audio geklickt werden, sollen nicht später losklingen |
| Sechstes Modell | Anzeige „Stereo Strings" | Der Firmware-Name „String & Reverb" passt ohne Hall nicht mehr; die Synthese ist unverändert |
| Zustandsprüfung | Werte außerhalb 0 bis 1 werden abgelehnt, nicht geklemmt | Ein solcher Zustand ist beschädigt; ein Teilzustand soll nie entstehen |
| Überlast der Ereignis-Warteschlangen (seit 0.1.1, Codex-Befund TC-R03) | 1024 Noten und je Parameter 256 Automationspunkte warten höchstens. Weitere Noten werden verworfen. Ist die Warteschlange eines Parameters voll, weicht der jüngste wartende Punkt dem neuen | Nichts darf vor seinem Zeitpunkt wirken. So geht höchstens eine Zwischenstufe verloren; der letzte Wert kommt an, zu seiner Zeit |
| Gemeinsamer Code | Nichts nach `common/` verschoben; Fenster- und Textcode sind Kopien aus Toy Piano Classic 0.3.2 | Toy Piano wurde parallel geändert. Kandidaten für später: Fenstergröße und -skalierung, strenge Texteingabe, Host-Doppel der Tests |

Vom Nutzer nach dem ersten DAW-Test verlangt und in 0.2.0 umgesetzt; die
Einzelheiten darin selbst festgelegt:

| Thema | Festlegung | Begründung |
| --- | --- | --- |
| Damping | Höherer Wert dämpft stärker; die Engine bekommt das Spiegelbild. Zustände aus 0.1.x werden beim Laden umgerechnet, Automationsdaten in Projekten nicht | Vorgabe des Nutzers. Der Parameter behält seine ID; ein neuer Parameter hätte alte Projekte ohne Wert gelassen |
| Tune | Klingt in Halbtonschritten; Schalter FREE (Parameter 110) gibt stufenloses Stimmen frei. Der Parameter 100 bleibt für den Host stufenlos, gerundet wird an der Engine | Vorgabe des Nutzers. So bleibt vorhandene Automation gültig, und die Stufenzahl eines Parameters muss sich nicht zur Laufzeit ändern |
| Filter | Parameter 109, CC 74: Tiefpass 12 dB/Oktave ohne Resonanz, 100 Hz bis 20 kHz; ganz offen wirkungslos (exakt) | „Einfaches LP-Filtermodell" laut Vorgabe. Offen muss das Signal unverändert bleiben, sonst gälte der Referenzvergleich nie |
| Limiter | Am Ende der Kette, nach Filter und Sampleratenwandlung; Decke −6 dBFS, beide Kanäle gemeinsam, sofortiger Zugriff, Erholung 0,15 s, kein Look-ahead | Vorgabe: einfacher Limiter gegen Übersteuerung. Die Decke ist eine Konstante im Quelltext (`OutputStage::kCeiling`) und bei Bedarf in einer Zeile zu ändern. Ohne Look-ahead keine Latenz; dafür kann ein sehr lauter Anschlag eine hörbare Kante bekommen |
| MIDI aus der Bildschirm-Klaviatur | Event-Ausgang „MIDI Out"; Note-on beim Anschlag, Note-off beim Loslassen | Vorgabe des Nutzers. Ohne Note-off blieben aufgenommene Noten hängen; die Engine selbst kennt weiterhin kein Note-off |
| Tests | Nur automatische Prüfungen des Verhaltens; keine Fenster- oder DAW-Tests der Ergänzungen | Der Nutzer prüft Optik, GUI und DAW selbst |

Auftrag vom 2026-11-15 (`docs/CLAUDE_BUILDAUFTRAG_KEKULE_0.3.0.md`, liegt
beim Nutzer), umgesetzt in 0.3.0. Vorgegeben waren: 4, 8 oder 16 Stimmen,
nur im Fenster wählbar, ohne MIDI-CC und ohne Automation; Plugin-Nap in
Studio One 7; ein Schloss statt FREE; Reglerfarben. Selbst festgelegt:

| Thema | Festlegung | Begründung |
| --- | --- | --- |
| Mehr als vier Stimmen | Bänke: bis zu vier unveränderte Vier-Stimmen-`Part`s im Synth (`engine/synth.h`). Der vendorte Kern ist nicht angefasst | Empfehlung des Auftrags. `kMaxPolyphony` hochzusetzen ginge nicht: Auflösung der Modalfilter, Akkordtabellen und Saitenzahl hängen an der Vier. So klingt jede Stimme wie bisher, und mit einer Bank ist der Ablauf Anweisung für Anweisung der der Firmware |
| Verteilung der Noten | Anschläge füllen die Stimmen reihum, bankweise zu je vier. Im vollen Instrument trifft es die am längsten nicht angeschlagene Stimme; kurz nach einer Erhöhung der Stimmenzahl nur ungefähr (TC-R14, im README beschrieben), weil jeder `Part` seine eigene Reihenfolge behält. Je Bank starten höchstens vier Anschläge je 12-Sample-Quantum, also 4, 8 oder 16 | Die Regel der Firmware, fortgesetzt. Bankweise und nicht reihum über die Bänke, weil der `Part` seine Stimmen abwechselnd links und rechts ausgibt: reihum über vier Bänke lägen vier aufeinanderfolgende Noten alle links |
| Pegel | Keine Pegelkorrektur je Stimmenzahl. Die Bänke werden vor Blende und Ausgangsstufe der Firmware summiert | Empfehlung des Auftrags: Eine Note ist bei jeder Stimmenzahl gleich laut. Gemessen mit 16 lauten Noten auf einmal, alle Modelle: 0,71 bis 0,77 vor dem Limiter (derselbe Akkord mit vier Stimmen: 0,61 bis 0,68). Die Kniekennlinie hält die Summe unter 0,8, der Limiter am Ende bei −6 dBFS |
| Akkordmodelle | Jede Stimme jeder Bank hat ihre zwei Saiten; der mit Structure gewählte Akkord gilt für alle Bänke | Folgt aus den unveränderten Bänken |
| Zufallsgenerator | Je Bank ein eigener Startwert: Startwert der Instanz plus Banknummer × 0x9E3779B9 | Deterministisch. Bank 0 behält den bisherigen Startwert, der Vier-Stimmen-Modus bleibt bitgenau. Gleiche Startwerte ließen zwei Bänke bei gleicher Vorgeschichte dasselbe Rauschen ziehen |
| Umschalten im Spiel | Mehr Stimmen: klingende Noten bleiben unberührt, die nächsten Anschläge gehen zuerst in die neuen Bänke. Weniger: Die Noten der wegfallenden Bänke blenden in 4 ms aus (192 Samples, wie beim Modellwechsel), die der bleibenden klingen weiter. Wirksam am nächsten Quantum | Kein Knacken, und was bleiben kann, bleibt. Das ganze Instrument neu zu starten wäre einfacher gewesen, hätte aber alle Noten gekappt |
| Ruhende Bänke | Eine Bank jenseits der ersten wird nur gerechnet, solange sie klingt: vom ersten Anschlag bis eine Sekunde nach ihrem letzten Sample ab −180 dBFS. Dann wird sie in ihren Anfangszustand zurückgesetzt | 16 Stimmen gewählt und 4 klingend kostet so viel wie 4 Stimmen. Die erste Bank läuft immer: Sonst gälte der Firmware-Vergleich nach einer Pause nicht mehr (LFO-Phasen, Rauschzustand, Stimmenzeiger) |
| Speicher | Alle vier Bänke liegen ab der Konstruktion im Synth: 446 KB statt 111 KB je Instanz. Eine Bank wird an Ort und Stelle zurückgesetzt | Im Audio-Thread wird nichts angelegt. Der Test „no allocation" gilt weiter, jetzt auch für Stimmenwechsel und weggelegte Bänke |
| Stimmenzahl: Bedienung und Weg | Parameter 111 „Polyphony", Liste 4/8/16, ohne `kCanAutomate`, ohne CC. Die Wahl sitzt im Fenster neben dem Titel | Entscheidung des Nutzers vom 2026-11-15: nur GUI, kein CC, keine Automation. Als Parameter und nicht als eigene Nachricht, weil er so auf dem vorhandenen Weg samplegenau beim Prozessor ankommt, mit dem Zustand gespeichert wird und das Undo des Hosts mitmacht. CC 29 der Firmware bleibt frei |
| 32 Stimmen | Nicht angeboten, aber nicht verbaut: `kMaxBanks` 8, ein vierter Listeneintrag, eine neue Zustandsversion | Der Nutzer hält 32 für übertrieben (2026-11-15) |
| Rechenzeit | Gemessen mit `bench_torus_classic` (Release, ein Kern eines i7-8700K, 48 kHz, alle Stimmen zweimal je Sekunde neu angeschlagen): 4 Stimmen 0,9 bis 1,5 %, 8 Stimmen 1,5 bis 2,8 %, 16 Stimmen 2,8 bis 5,3 % je nach Modell (0.3.1); bei 192 kHz mit 16 Stimmen bis 6,9 %. Einzelne Quanten dauerten unter Windows bis rund 440 µs; eine harte Obergrenze ist das nicht | Vom Auftrag verlangt: Die geringe CPU-Anzeige in Studio One ersetzt die Messung nicht. Die Kosten wachsen linear mit der Zahl klingender Bänke |
| Zustand | Version 3 mit elf Werten. Version 1 und 2 laden mit vier Stimmen | Vorgabe |
| Plugin-Nap (0.3.1) | Nicht unterstützt. Fester Tail `kInfiniteTail`, keine exakten Nullen in Ruhe, keine Ruhe-Meldung zwischen den Threads. Weggelegt werden nur stille Bänke jenseits der ersten, im Inneren der Engine | **Entscheidung des Nutzers vom 2026-11-15** nach Codex' Review von 0.3.0 (TC-R11 bis TC-R13): Der Tail ist ein Wert, den Hosts beim Einrichten lesen; eine Ruhe-Meldung über Threads hinweg hatte Lücken, und ausstehende Note-offs gingen nicht ein. Der Gewinn wäre klein: Eine ruhende Instanz kostet rund 1 % eines Kerns. Dafür ist das Plugin wieder ohne Ausnahme bitgleich zur Firmware |
| Oktavtasten (0.3.1) | **<<** und **>>** neben der Klaviatur verschieben sie in Oktaven, ±2 um C4–C6; Start bei C3–C5. Eine gehaltene Taste wird vorher losgelassen. Gespeichert im Fensterzustand (Version 2), Version 1 öffnet bei C3–C5 | Wunsch des Nutzers vom 2026-11-15: wie Mendelejew 0.1.4, dessen Lösung übernommen ist (Code kopiert, nichts nach `common/`) |
| Schloss | Vektorzeichnung in `widgets.cpp`, alle Maße relativ zur Größe. Geschlossen grau, offen bernstein. Tooltip | Vorgabe. Parameter 110 bleibt, wie er ist; das Schloss zeigt ihn invertiert |
| Reglerfarben | Weiß: Tune, Filter, Volume. Magenta: Structure, Brightness, Damping. Türkis: Position, Excitation. Drei benannte Konstanten in `editor.cpp`; seit 0.3.1 genau die Töne von Mendelejew | Anregung des Nutzers, mit Verweis auf Mendelejew. Zuordnung nach Aufgabe: Rahmen, Resonator, Anschlag. Übernommen sind nur Farben; im Produkt steht kein fremder Name, die Hommage steht im README |

### Mendelejew (Projektname Elements)

Dritter Port, Auftrag vom 2026-11-14 („Phase 1 – Mutable Instruments Elements
als natives VST3"). Version 0.1.0 ist gebaut, vom validator geprüft,
automatisch getestet und von Codex geprüft
([Review](../instruments/elements/docs/CODEX_REVIEW_0.1.0.md)); 0.1.1 behebt
dessen fünf Befunde
([Umsetzung](../instruments/elements/docs/CODEX_REVIEW_0.1.0_UMSETZUNG.md)).
Die Nachprüfung fand in der Lösung für die Bildschirm-Klaviatur einen neuen
Fehler (EL-R06); 0.1.2 behebt ihn
([Umsetzung](../instruments/elements/docs/CODEX_REVIEW_0.1.1_UMSETZUNG.md)),
und Codex hat bestätigt, dass er behoben ist
([Nachprüfung 0.1.2](../instruments/elements/docs/CODEX_REVIEW_0.1.2_NACHPRUEFUNG.md)).
0.1.2 ist auf main. 0.1.3 färbt auf Wunsch des Nutzers (2026-11-15) die
Regler der Anreger in Erinnerung an Mutable Instruments: weiß, magenta,
türkis wie auf dem Modul, nur die Farben, nichts vom Panel. Diese Fassung hat
Codex noch nicht geprüft. In einer DAW gespielt und angehört ist das Plugin
noch nicht. Messwerte und Testprotokoll stehen im
[Build-Bericht](../instruments/elements/docs/BUILD_REPORT_0.1.0.md), die
offene Abnahme in der
[DAW-Prüfliste](../instruments/elements/docs/DAW_TEST_0.1.0.md), Herkunft und
Patches in [UPSTREAM.md](../instruments/elements/UPSTREAM.md).

Vorgegeben waren: Original-DSP unter MIT, DSP-Kern ohne Plugin-Framework,
echte 48 kHz intern (kein 32-kHz-Kern mit Upsampling), monophon, Vergleich mit
dem Original bei 32 kHz, 96 kHz mit eigenem Status. Abweichend vom Wortlaut
des Auftrags gibt es im Repository kein JUCE: Das Plugin sitzt wie die beiden
anderen direkt auf dem Steinberg-SDK. Bei der Umsetzung selbst festgelegt,
weil der Auftrag es offen ließ:

| Thema | Festlegung | Begründung |
| --- | --- | --- |
| Name | Das Plugin heißt „Mendelejew" (Anzeigename, Bundle, Fenstertitel). „Elements" bleibt interner Projektname: Verzeichnis, Branch, Build-Targets, Namensräume, Testnamen | **Entscheidung des Nutzers vom 2026-11-14,** nach dem ersten Build unter dem Arbeitstitel „Elements": benannt nach Dmitri Mendelejew, in deutscher Schreibweise, nur der Produktname. Mutable Instruments empfiehlt, Modulnamen für abgeleitete Werke nicht beizubehalten. Die Klassen-IDs sind die des ersten Builds |
| Abtastrate des Kerns | Zur Laufzeit 32, 48 oder 96 kHz; Blockgröße 16, 24 oder 48 Samples, immer 0,5 ms | Bei 32 kHz muss der Port das Original sein: Nur so lässt sich bitgenau prüfen, dass kein Patch den Klang ändert. Mit der mitwachsenden Blockgröße bleiben Hüllkurventabelle und alles, was Blöcke zählt, unverändert |
| Was „gleiches Instrument bei 48 kHz" heißt | Jedes Teil verhält sich in Zeit und Frequenz wie bei 32 kHz: Moden behalten Frequenz, Ausklang und Pegel ihres Ausschwingens (seit 0.1.1 über die Pole, bei jeder Güte; 0.1.0 traf das nur bei hoher Güte, Review-Befund EL-R04), Verzögerungen ihre Dauer, der Tiefpass der Erreger seinen Verlauf bis 16 kHz | Die naheliegende Umrechnung (gleiche Grenzfrequenzen, Güte mal Ratenverhältnis) ergab messbar ein anderes Instrument: obere Teiltöne bis 170 Cent anders gestimmt, kürzer ausklingend und bei hellen Klängen bis 12 dB lauter. Die genaue Umrechnung ist ableitbar und einzeln gemessen; sie ist keine Klangkorrektur nach Gehör |
| Mehr Bandbreite | Keine zusätzlichen Moden oberhalb der 15,7 kHz des Originals | Sonst hätten hohe Töne Teiltöne, die das Original nie hatte |
| Abtastrate im Plugin | Engine immer 48 kHz; jede andere Host-Rate über den Sinc-Wandler aus Torus Classic | Vorgabe: bei 44,1 kHz saubere Wandlung, keine ungetestete Umschaltung. Bei 96-kHz-Hosts deshalb vorerst ebenfalls 48 kHz plus Wandlung |
| 96 kHz | **HOLD.** Der Kern rechnet bei 96 kHz und besteht dort dieselben Messungen; das Plugin nutzt es nicht | Fertig erst, wenn Plugin-Tests und ein Hörvergleich bei 96 kHz vorliegen |
| Zufallsgenerator | Je Instanz ein eigener mit dem Startwert des Originals | Das Original hat einen für das ganze Programm: zwei Instanzen würden sich stören und aus zwei Threads in eine Variable schreiben |
| Nicht übernommen | Saitenmodelle und „Ominous Voice" der späteren Firmware, Audio-Eingänge, die „Signatur" aus der Seriennummer | Nicht Teil von Phase 1; die Saitenmodelle bräuchten eine eigene Ratenanpassung. Im Testbezug sind sie unverändert vorhanden |
| Regler | Die Potis der Hardware als Parameter 0 bis 1; Abschwächer und CV-Eingänge entfallen | Es gibt nichts zu patchen; zusätzliche Modulation ist nicht Teil von Phase 1 |
| Glättung | Wie der Regler-Leser der Hardware: 1 % des Wegs je halbe Millisekunde, Position und Space zusätzlich | Ohne sie knackt Automation; mit den Werten des Originals verhält sich das Plugin wie das Gerät |
| Damping | Höherer Wert klingt länger, wie an der Hardware | Vorgabe „Originalparameter". **Bei Torus Classic ist es seit 0.2.0 auf Wunsch des Nutzers umgekehrt;** ob Mendelejew folgen soll, ist offen |
| Coarse, Fine | ±30 Halbtöne in ganzen Schritten, ±2 Halbtöne stufenlos | Umfang der Hardware-Potis (5 Oktaven, ±2 Halbtöne), um die gespielte Note zentriert |
| Freeze | Eigener Schalter | An der Hardware nur mit Steuerspannung auf SPACE erreichbar |
| Velocity | Regler 0 bis 100 %, Vorgabe 50 %: Anschlag bewegt STRENGTH um die Ruhelage der Hardware, bei 100 % ±15 dB | Bei 0 % ist jede Note so laut wie an der unbeschalteten Hardware; damit bleibt der Vergleich mit dem Original einfach |
| Stimme | Monophon, letzte Note zählt; jede Taste schlägt an (das Gate schließt dafür 0,5 ms); Loslassen fällt ohne Anschlag auf die zuletzt gehaltene Taste zurück | So verhält sich die Hardware an einem MIDI-Interface mit Retrigger |
| Pitch-Rad | ±2 Halbtöne, über den FM-Eingang der Engine | Wie bei den beiden anderen Plugins |
| MIDI-Controller | 21 Geometry, 22 Brightness, 23 Damping, 24 Position, 33 Volume (Nummern wie bei Torus Classic), 91 Space | Gleiche Bedeutung, gleiche Nummer |
| Ausgang | Wandlerkennlinie der Hardware ohne 16-Bit-Rundung (halber Pegel, weich begrenzt, bei Vollaussteuerung abgeschnitten), danach Volume; hinter der Ratenwandlung seit 0.1.1 hart auf ±1 begrenzt | Pegel wie am Gerät; kein Sample über 0 dBFS. Der Sinc-Wandler überschwingt bei voll ausgesteuerten, hellen Klängen; in den Läufen des Reviews um bis zu 1,9 dB (Befund EL-R03); bei 48 kHz wird nichts gewandelt, dort wirkt die Begrenzung nie. Kein zusätzlicher Limiter und kein Filter wie bei Torus Classic |
| Schutz | Liefert die DSP je ein nicht endliches Sample: Stille, DSP neu, Tasten los | Ein Filter oder der Hall käme aus NaN nicht von selbst heraus |
| Gemeinsamer Code | Adapter, Parameterübergabe, Texteingabe und Fenster sind Kopien aus Torus Classic 0.2.0 im eigenen Namensraum; nichts nach `common/` verschoben | Verschieben hätte Torus Classic geändert, dessen Prüfung noch läuft. Kandidaten für `common/` stehen dort im README |
| Tests | Bitgenauer Vergleich bei 32 kHz, Messungen bei 48 und 96 kHz gegen das Original, Engine- und Plugin-Tests; keine Fenstertests | Der Nutzer prüft Optik, GUI und DAW selbst |

### Messier (Projekt messier)

Vierter Port, Auftrag vom 2026-11-17 (`docs/CLAUDE_BUILDAUFTRAG_MESSIER_0.1.0.md`,
liegt beim Nutzer): der Granular Looper `instruments/granular` aus Daisy-Seed (Nebulae
on Daisy 0.2.2) als VST3-Instrument, nur mit der File-Quelle. Codex hat 0.1.0 nicht
freigegeben: vier Fehler und das fehlende Atlas/Orion-Fenster
([Review](../instruments/messier/docs/CODEX_REVIEW_0.1.0.md)). 0.1.1 behebt die vier Fehler
([Umsetzung](../instruments/messier/docs/CODEX_REVIEW_0.1.0_UMSETZUNG.md)). Codex'
Nachprüfung von 0.1.1 fand fünf weitere
([Nachprüfung](../instruments/messier/docs/CODEX_REVIEW_0.1.1.md)); 0.1.2 behebt sie
([Umsetzung](../instruments/messier/docs/CODEX_REVIEW_0.1.1_UMSETZUNG.md)). Die Prüfung
von 0.1.2 bestätigte MS-R07 und MS-R10 als behoben und fand fünf weitere Punkte
([Nachprüfung](../instruments/messier/docs/CODEX_REVIEW_0.1.2.md)); 0.1.3 behebt sie
([Umsetzung](../instruments/messier/docs/CODEX_REVIEW_0.1.2_UMSETZUNG.md)). Die Prüfung von
0.1.3 bestätigte MS-R11 und MS-R15 und fand vier weitere Punkte
([Nachprüfung](../instruments/messier/docs/CODEX_REVIEW_0.1.3.md)); 0.1.5 behebt sie
([Umsetzung](../instruments/messier/docs/CODEX_REVIEW_0.1.3_UMSETZUNG.md)). Die Prüfung von
0.1.5 (mit dem Datenweg aus 0.1.4) fand sechs weitere Punkte
([Nachprüfung](../instruments/messier/docs/CODEX_REVIEW_0.1.5.md)); 0.1.6 behebt sie
([Bericht](../instruments/messier/docs/BUILD_REPORT_0.1.6.md)). Die Prüfung von 0.1.6 fand vier
weitere ([Nachprüfung](../instruments/messier/docs/CODEX_REVIEW_0.1.6.md)); 0.1.7 behebt sie als
Klasse ([Bericht](../instruments/messier/docs/BUILD_REPORT_0.1.7.md)). Die Prüfung von 0.1.7 fand
vier weitere ([Nachprüfung](../instruments/messier/docs/CODEX_REVIEW_0.1.7.md)); 0.1.8 behebt sie
und bringt Drag-and-drop für WAV-Dateien
([Bericht](../instruments/messier/docs/BUILD_REPORT_0.1.8.md)), die nächste Prüfung steht aus. Das Fenster ist vorläufig: Das Atlas/Orion-Fenster aus Abschnitt 8
des Auftrags folgt, wenn Aufteilung und Farben feststehen (Entscheidung des Nutzers vom
2026-11-17). Auf Wunsch des Nutzers (2026-11-18) kam zuerst der Datenweg dafür: 0.1.4
liefert Wellenform, Loop, Abspielposition, Spray und die echten Grains bis ins Fenster und
zeigt sie in einem vorläufigen Kartenstreifen
([Bericht](../instruments/messier/docs/BUILD_REPORT_0.1.4.md)). In einer DAW ist Messier weder ausprobiert noch angehört. Messwerte und
Testprotokoll stehen im
[Build-Bericht](../instruments/messier/docs/BUILD_REPORT_0.1.0.md), die offene Abnahme in
der [DAW-Prüfliste](../instruments/messier/docs/DAW_TEST_0.1.0.md), Herkunft und Patches
in [UPSTREAM.md](../instruments/messier/UPSTREAM.md).

Vom Nutzer am 2026-11-17 entschieden:

| Thema | Entscheidung |
| --- | --- |
| Weg | Natives C++ aus dem Daisy-Kern, keine Cabbage-/Csound-Hülle, keine neue Bibliothek |
| Name | „Messier“, nach Charles Messiers Nebelkatalog; Hersteller TAUREON, Bundle `Messier.vst3`, eigene FUIDs |
| Vocoder | Der geprüfte Kern wird übernommen, obwohl er LGPL-2.1-or-later ist (aus Csound `pvlock.c`); Lizenztexte, Eintrag in `THIRD_PARTY_NOTICES.md`, Bau-Weg im README |
| Umfang 0.1.0 | Nur die File-Quelle; Live, Overdub, Circular, Dry-Blend, Eingangsverstärkung und Grain-FM folgen in 0.2.0 |
| Plugin-Art | Instrument mit einem (noch ungenutzten) Sidechain-Bus |
| Quantum | Fest 64 Samples, die Kontrollperiode des Originals |
| Grain-Pegel (F42) | 0,90 wie das Original statt 0,45 wie der Kern 0.2.2: der einzige Klang-Patch am Kern |
| Dateien | PCM 16/24 Bit, Float 32 Bit, EXTENSIBLE, mono/stereo, 8–192 kHz; mehr als 16 Bit ohne Dither gerundet; höchstens 256 MiB PCM16 |
| Fenster | Ursprünglich: keine Wellenform, keine Positionsanzeige, kein Drag-and-drop in 0.1.0. Später am 2026-11-17 ersetzt (Auftrag Abschnitt 8): Atlas als Bediengrundlage mit Nebelkarte, Wellenform, Loopgrenzen, Abspielposition, Spray und echten Grainpunkten, Orion als Leitmotiv, ein breiter Blend-Regler; Aufteilung und Farben offen. Nach Codex' Review (MS-R05): erst die vier Fehler als 0.1.1, das neue Fenster danach. **Entscheidungen des Nutzers vom 2026-11-18 (nach zwei Mockups, [Fensterstudie](https://claude.ai/artifact/VxHc8Nbapj8ArJvkDQebWG)):** Die Karte ist ein Rechteck genau über der Wellenform, im selben Zeitmaßstab (kein rundes Okular). Der breite Regler unter der Wellenform ist **START** (dieselbe x-Achse wie Platte und Wellenform), nicht Blend: Blend wird ein Drehregler; kein zusätzlicher Scrub-Parameter, kein Kernpatch. Gehäuse aus altem Metall mit Patina und Kratzern, kein Papier; die Karte als Messplatte mit Kreuzraster und Achsen in echten Einheiten (Sekunden, L/R). Grains als Sternschnuppen: der Schweif folgt nur der echten Bewegung durch die Datei (waagerecht), ein Grain flammt mit seinem Fenster auf und verglüht. Der Nebel driftet langsam und tritt mit Blend Richtung Granular zurück. Fest eingebaut: skalierbare Fenstergröße und vier GUI-Presets zur Wahl (Slate, Silver, Copper, Photo plate), gespeichert im Fensterzustand. Grafiken als Bitmaps in 1x und 2x nach dem [Grafikauftrag](../instruments/messier/docs/GRAFIKAUFTRAG_GUI.md). **Drag-and-drop (Wunsch des Nutzers vom 2026-11-18, ersetzt „kein Drag-and-drop in 0.1.0“):** eine WAV-Datei auf Karte oder Dateizeile ziehen lädt sie wie LOAD...; seit 0.1.8 im vorläufigen Fenster, das neue Fenster übernimmt es |
| Zweitfunktionen | Ohne MIDI-CC, wie in der Firmware |

Bei der Umsetzung selbst festgelegt, weil der Auftrag es offen ließ:

| Thema | Festlegung | Begründung |
| --- | --- | --- |
| Quellstand | Daisy-Seed `8226a31` (0.2.2), gelesen mit `git archive`/`git show`; `origin/main` stand beim Kopieren schon bei `53fd8a0` (0.3.0, 24-Bit-WAV) | Der Auftrag nennt 0.2.2 bei `8226a31`. Die übernommenen Dateien sind in beiden Ständen gleich, bis auf den Teil von `wav.h`, den Messier nicht nimmt |
| FFT des Vocoders | Eigenes Backend `core/fft_desktop.cpp` hinter dem `fft.h` der Firmware: reelle 2048-Punkt-FFT über eine komplexe 1024-Punkt-FFT. Konstante Tabellen, kein weiterer Zustand | Die Firmware rechnet seit 0.2.2 mit CMSIS-DSP (Apache-2.0); das wäre eine neue Bibliothek. Der unveränderte Vocodertest der Firmware läuft gegen das Backend und besteht ihre Grenzwerte (FFT-Fehler 2,4e-7). Zwei Instanzen in zwei Threads teilen nichts Beschreibbares |
| Dateipuffer | Patch P1: `Engine::SetFileBuffer()`, eine Zeile in `engine.h`; `wav.h` auf `FileBridge` und `WavInfo` gekürzt (P2) | Die Firmware hat einen festen Puffer und überschreibt ihn bei stummer Engine. Mit einem Puffer je Datei bleibt bei jedem Fehler die alte Datei spielbar, und der Audio-Thread wartet nie auf eine Kopie. Klangneutral; der Referenztest fährt den ungepatchten Kern auf Firmware-Art durch einen Dateiwechsel, bitgenau |
| Referenztest | `tests/reference/` enthält nur die Upstream-Kopien (mit der F42-Zeile); der Treiber `tests/reference_api.cpp` ist eigener Code daneben, unabhängig von `engine/` geschrieben | Ein Test prüft, dass die Kopien bis auf die eine Zeile der Firmware gleichen; der Vergleich ruft nicht zweimal denselben Wrapper |
| Adapter | Kekulés Adapter in der Form, die Mendelejew schon nutzt: mit Note-off, ohne Ausgangsstufe; Quantum 64, Noten mit MIDI-Kanal, das Rad als 14-Bit-Wert, und `Render()` bekommt schon ein Eingangspaar (in 0.1.0 Stille) | Kein neuer Filter, kein Limiter (Auftrag). Der Eingangsweg von 0.2.0 kann so ergänzt werden, ohne die Zeitrechnung umzubauen |
| Pitch Bend | Ein Rad für alle Kanäle: Die Engine bekommt den Wert auf allen 16 MIDI-Kanälen. Er wirkt nur bei gehaltener Note | So rechnet der Kern ohne Änderung; wie in der Firmware beugt das Rad die gehaltene Note |
| Bildschirm-Klaviatur | Noten auf MIDI-Kanal 1, Velocity 100, mit Loslassen; der Weg (Warteschlange mit Platz für jedes Loslassen) aus Mendelejew 0.1.4, Oktavtasten aus Kekulé 0.3.1 | Kekulés Engine kennt kein Note-off, Mendelejews schon |
| Reset | Parameter 121 als Taster: Die Flanke 0→1 wirkt, nicht gespeichert | Ein Knopf im Fenster und automatisierbar; ein gehaltener Wert löst nichts wiederholt aus |
| Volume | Linear 0 bis 100 % hinter dem Kern wie Kekulés Volume, Vorgabe 100 %; Sprünge gleiten mit höchstens 1/240 je Sample (0 bis 100 % in 5 ms) | Bei 100 % bleiben die Samples des Kerns exakt; ohne Gleiten knackt Automation |
| Anzeige | Der Wert, den das Gesetz des Kerns aus dem Poti macht (Speed als Faktor, Pitch in Oktaven, Density in Grains/s, Size als Anteil, Window mit Namen); Texteingabe rechnet zurück, auch für die Rastbänder. Die Gesetze sind für die Anzeige kopiert, ein Test vergleicht sie mit denen des Kerns | Vorgabe „angezeigt wird der wirkliche Wert“ |
| Zustand | Version 1: die 16 gespeicherten Werte als Double, dann der Pfad als UTF-8 (höchstens 32 KiB). Alles wird gelesen und geprüft; ein Wert außerhalb 0 bis 1, ein abgeschnittener Zustand oder ungültiges UTF-8 wird abgelehnt | Wie bei Kekulé: ein Teilzustand soll nie entstehen |
| Fehlende Datei | Stille, der Pfad bleibt im Zustand, das Fenster zeigt ihn mit „File not found“ | Vorgabe; ein erneutes Speichern verliert den Verweis nicht |
| Dateistatus im Fenster | Der Processor antwortet nur im UI-Thread: auf die Ladeanforderung und auf Statusfragen, die das Fenster während eines Ladevorgangs stellt (bis 0.1.3 alle 100 ms, seit 0.1.4 bei jedem Takt des Fenster-Timers, 30 ms) | Nachrichten aus dem Audio- oder Ladethread wären bei manchen Hosts unsicher |
| Neustart | Aktivierung und Projektladen bauen `neb::Engine` an Ort und Stelle neu (placement new, keine Allokation); die Datei bleibt. Eine mit dem Projekt gelesene Datei ist sofort da, ohne Überblendung | Startwert des Zufalls gleich, ein Projekt rendert jedes Mal gleich |
| Freigabe alter Dateien | Der Audio-Thread reicht losgelassene Dateien über eine Warteschlange an den Ladethread, der sie freigibt; bis zu drei Dateien zugleich im Speicher (alte, neue, wartende) | In `process` keine Freigabe, keine Sperre |
| Kein Clip | Hinter der Ratenwandlung wird nicht begrenzt (gemessen bis 1,03 bei dichten Rechteck-Grains und 192 kHz) | Auftrag: kein anderer Limiter. Bei 48 kHz bleibt der Ausgang unter 0,95 |
| Fenster | 700 × 306 Pixel (mit Klaviatur 392; seit 0.1.4 mit dem Kartenstreifen 700 × 436 und 522), neun Spalten: acht Potis und Volume, darunter die sechs Zweitfunktionen klein, Freeze und Reset unter Volume; Reglerfarben von Mendelejew nach Aufgabe | Vorgabe des Auftrags; die Farben wie bei Kekulé 0.3.1 |
| VST3-Kategorie | `Instrument\|Sampler` | Ein Instrument, das auf Samples beruht |
| Last | Gemessen: 1,8 bis 4,2 % eines Kerns (i7-8700K). Bei 192 kHz mit 64er-Blöcken dauert ein Block mit Vocoder-Hop länger als der Block: Der Hop rechnet alle 512 internen Samples zwei Kanäle auf einmal | Befund, kein Fix in 0.1.0: Das Verteilen des Hops über mehrere Blöcke wäre eine Änderung am Kern |
| Gemeinsamer Code | Nichts nach `common/`; Adapter, Parameterübergabe, Texteingabe, Fenster und Klaviatur sind Kopien aus Kekulé 0.3.1 und Mendelejew 0.1.4 | Auftrag |
| Speichermangel (0.1.1, MS-R01) | Leser, Ladethread, Projektladen und die Host-Aufrufe mit Strings fangen fehlgeschlagene Allokationen ab. Eine Datei aus dem Fenster scheitert mit „out of memory“, die alte spielt weiter; ein Projekt wird still (dafür liegt ein leeres Dateiobjekt bereit). Fehlertexte bis 15 Zeichen, damit auch das Melden nicht alloziert | Ein Fehler beim Laden darf den Host nie beenden |
| Projekt und Datei gemeinsam (0.1.1, MS-R02; 0.1.2, MS-R07) | Die Datei eines Projekts hat seit 0.1.2 einen eigenen Übergabeplatz und trägt die Nummer der Projektladung; der Audio-Thread übernimmt sie nur zusammen mit den Werten derselben Nummer. Bis beide da sind (auch bei vollem Rückstau), spielt das alte Projekt vollständig weiter, und keine Fenster-Datei wird eingetauscht. Fenster-Dateien tragen eine Reihenfolgenummer; eine ältere kommt nach einem Projekt nicht zurück | Nie neue Werte mit alter Datei oder umgekehrt. Getestet mit 400 schnellen Projektladungen und mit Audioblöcken zwischen Datei und Werten, auch mit einer Aktivierung dort |
| Reset-Taster (0.1.1, MS-R03; 0.1.2, MS-R08; 0.1.3, MS-R13; 0.1.5, MS-R18; 0.1.6, MS-R22) | Seit 0.1.2 schickt der Taster jeden Druck als Nachricht an den Processor, kein Edit von Parameter 121 mehr. Drucke, die denselben Audioblock oder denselben noch nicht gerechneten 64-Sample-Abschnitt der Engine erreichen, wirken einmal; Drucke ohne laufendes Audio verfallen nach 0,5 s, seit 0.1.3 auch über eine Aktivierung hinweg nicht früher. Parameter 121 bleibt für Automation (Flanke). Seit 0.1.5 lässt ein Aufruf ohne Samples die Drucke liegen, und ein Druck, den der Adapter schon auf einen noch nicht gerechneten Abschnitt gelegt hat, übersteht den Neustart einer Aktivierung, seit 0.1.6 auch einen Wechsel der Abtastrate | Ein Host kann Druck und Loslassen eines Parameters zusammenlegen, das Fenster kann vor dem nächsten Block schließen. Dafür nimmt der Host Drucke im Fenster nicht als Automation auf |
| Projektänderung (0.1.1, MS-R04; 0.1.2, MS-R09; 0.1.3, MS-R12; 0.1.5, MS-R17; 0.1.6, MS-R20; 0.1.7, MS-R26, MS-R27; 0.1.8, MS-R32, MS-R33) | Der Processor zählt die Dateien aus dem Fenster, die zum Klang wurden, und meldet den Zähler (65.536 Stufen, verlustfrei auch bei Hosts mit Float-Werten) über den versteckten, schreibgeschützten Parameter 199; der Host reicht ihn an den Controller, der daraus direkt je neuer Datei `setDirty(true)` ruft und den Status für die Anzeige holt. Eine Statusantwort zählt dieselben Dateien nicht noch einmal. Projektladen und Statusabfragen zählen nicht. Seit 0.1.5 nennt der Status den Processor (eine Kennung je Instanz) und trägt den vollen Zähler; ein neu verbundener Processor wird von seiner eigenen Null an gezählt, auch nur über Parameter 199 vor seinem ersten Status, und das Fenster vergisst die Wellenform des alten. Seit 0.1.7 einfacher: jede Änderung von 199 markiert, ebenso jeder Status mit mehr Dateien als der letzte desselben Processors; abgezogen wird nichts. Eine Datei kann so zweimal markiert werden, was für den Host nichts ändert. Jeder Processor addiert einen eigenen Versatz auf 199, damit ein verspäteter Wert eines alten Processors selten einem neuen gleicht. Seit 0.1.8 liegen die Versätze der Processoren eines Prozesses 4099 auseinander (gleiche Werte erst nach Tausenden Ladungen dazwischen), und der Controller merkt sich den Zählerstand der letzten acht Processoren, damit A → B → A nichts neu markiert |
| Speed und Pitch Random (0.1.8, DAW-Test des Nutzers) | Speed hat 0x genau bei 12 Uhr (-4x bis +4x, +1x als Grundwert bei 7/12), Pitch Random reicht bis ±12 Halbtöne und zeigt Halbtöne. Das Plugin gibt dem Kern die Potistellung, die zum Wert gehört (`PotOf`); Kern und Gesetze bleiben unverändert, der Firmwarevergleich läuft mit denselben Potistellungen | Wunsch des Nutzers vom 2026-11-18; bewusste Abweichung vom Modul wie F42. In 0.1.7 und früher gespeicherte Songs behalten ihre Potiwerte und klingen bei Speed und Pitch Random anders |
| Stop, Bypass und Ablegen (0.1.9, DAW-Test des Nutzers; 0.1.10, MS-R35, MS-R36) | STOP im Kopf (Parameter 131, nicht gespeichert) pausiert mit 5 ms Blende und läuft an derselben Stelle weiter; Bypass (130, VST3-Bypass-Kennzeichen, gespeichert ab Zustandsversion 2) ebenso, damit der Bypass von Studio One wirkt. Seit 0.1.10 wirken beide auf ihr Sample (`OutputGate` plant den Block vorab in Abschnitte); Noten und Parameterpunkte liegen auf der Zeit der Engine, sodass die Ausgabe von der Blockteilung des Hosts nicht abhängt. Während der Ruhe fallen neue Anschläge weg, vom Host wie von der Bildschirm-Klaviatur (ohne MIDI-Ausgabe); Loslassen und Parameter wirken beim Weiterlaufen. Beim Ziehen über das Fenster prüft Messier die WAV-Köpfe und weist Unspielbares schon beim Darüberziehen ab; nach einem Ladeauftrag fragt das Fenster nach, bis er fertig ist | Wünsche des Nutzers vom 2026-11-18 |
| Bildschirm-Klaviatur und Host-MIDI (0.1.10, MS-R34) | Die Bildschirm-Klaviatur spielt auf MIDI-Kanal 1 wie Host-MIDI auf Kanal 1; hält beide dieselbe Note, endet sie erst, wenn beide losgelassen haben, auch wenn ein Disconnect die Tasten der Klaviatur freigibt. Der Processor merkt sich dafür, wer welche Note hält; der Kern bleibt unverändert | Die eigene Tastenquelle des Kerns (16) gehört zu den sechzehn Hardwaretasten mit festen Noten und ignoriert das Pitch-Wheel; sie hätte die Klaviatur verändert |
| WINDOW-Anzeige und LATCH (0.1.8) | WINDOW zeigt im Fenster die gemischte Fensterform als kleine Kurve (die Formen des Kerns, `BuildWindows`/`WinMix`), der Host den Namen der nächsten Form mit „~“ dazwischen. LATCH neben KEYS hält eine angeschlagene Taste der Bildschirm-Klaviatur, bis eine andere, dieselbe oder LATCH aus; nicht gespeichert. Die Karte nimmt seit 0.1.8 die Maus (ersetzt „die Karte dient in 0.1.0 als Anzeige“): Klick oder Ziehen setzt START an die Stelle in der Datei, das Mausrad verbreitert oder verengt SPRAY | Wünsche des Nutzers vom 2026-11-18 | Ohne offenes Fenster fragte 0.1.1 nicht nach. Nachrichten aus dem Audio- oder Ladethread wären bei manchen Hosts unsicher; der Ausgabeparameter ist der vorgesehene Weg vom Processor zum Controller |
| Host-Aufrufe ohne Ausnahmen (0.1.2, MS-R06; 0.1.3, MS-R11, MS-R15; 0.1.5, MS-R19; 0.1.6, MS-R23, MS-R24; 0.1.10, MS-R37) | Jeder Host-Aufruf des Controllers ist abgesichert, seit 0.1.3 auch die Parametertexte und das Anlegen des Fensters; scheitert nur die Anzeige, gilt der geladene Zustand trotzdem. Seit 0.1.5 auch die Erzeugung über die Factory (bei Mangel null, das SDK antwortet dann `kNoInterface`), `initialize`, `setupProcessing` und der Ratenwechsel in `setActive` (`kOutOfMemory`); der Adapter baut eine neue Rate erst vollständig nebenher auf und bleibt bei Mangel unverändert. Seit 0.1.6 auch der Empfang von Nachrichten (der Host darf die Attributliste erst beim Zugriff anlegen) und der Aufbau des Fensters: Teilaufbau wird freigegeben, der Host bekommt einen Fehler statt eines leeren Fensters. Seit 0.1.10 ist jeder Aufruf von VSTGUI, Host oder System in den Code des Fensters eine Ausnahmegrenze (`Guarded`): Zeichnen, Maus, Mausrad, Ziehen und Ablegen, Timer, Dateidialog, Größen und `close` |
| Projekt bei Speichermangel (0.1.3, MS-R14; 0.1.5, MS-R16) | Kann ein Projekt mangels Speicher nicht vollständig übernommen werden, lehnt `setState` es ganz ab (`kOutOfMemory`); das alte Projekt spielt weiter und bleibt, was gespeichert wird. Seit 0.1.5 auch, wenn nur das Dekodieren seiner gültigen Datei am Speicher scheitert: Der Leser meldet dafür einen eigenen Code statt „too large“. Still mit gespeichertem Pfad wird ein Projekt nur bei fehlender oder unbrauchbarer Datei | Ein abgelehntes Projekt darf den gespeicherten Pfad nicht ändern | Getestet, indem jede Allokation eines Aufrufs der Reihe nach fehlschlägt (nur im Release-Bau) |
| Datenweg des Fensters (0.1.4; 0.1.6, MS-R21; 0.1.7, MS-R28; 0.1.8, MS-R30, MS-R31) | Bei offenem Fenster fragt der Controller etwa alle 30 ms per Nachricht; der Processor setzt dann ein Flag, der Audio-Thread legt nach dem nächsten Block den Anzeigezustand der Engine (288 Bytes: Datei-Nummer, Loop, Abspielposition, Spray, bis 20 klingende Grains) in einen `LatestValue`, ohne Sperre und Allokation; die Antwort geht im UI-Thread zurück. Ohne Fenster wird nichts veröffentlicht. Kernpatch P4: drei lesende Zugriffe in `engine.h`. Seit 0.1.6 trägt jede Nachricht des Controllers seine Verbindungsgeneration, der Processor stempelt sie auf Status- und Anzeigeantworten, Anzeigeantworten sind nummeriert: Antworten einer früheren Verbindung oder überholte werden verworfen, die Wellenform muss Dateinummer und Länge der Anzeige tragen. Seit 0.1.7 zählen die Generationen über alle Controller des Prozesses; der Processor weist Aufträge einer älteren Verbindung ab (außer dem Loslassen einer Taste), und jede Antwort trägt die Generation ihres Auftrags. Seit 0.1.8 trägt jeder Auftrag seinen Entstehungszeitpunkt (eine Uhr für den ganzen Prozess): Der Processor weist Aufträge ab, die vor seinem letzten Trennen entstanden sind, und Ladeaufträge von vor seinem letzten Projektladen; beim Trennen lässt er die noch gehaltenen Tasten der alten Verbindung los | Der Auftrag verlangt echte Grainzustände ohne Einfluss auf DSP, Zufall und Klang; Nachrichten nur aus dem UI-Thread wie beim Dateistatus. Der Engine-Test vergleicht mit und ohne Lesen bitgenau |
| Wellenform (0.1.4) | Der Lader rechnet je Datei 2048 Spalten Minimum/Maximum über alle Kanäle aus genau dem PCM16-Puffer, den die Engine spielt, im Ladethread bzw. in `setState`, und hält die vier neuesten unter der Nummer der Datei. Das Fenster zeigt nur die Übersicht der Datei, die der Kern gerade liest; beim Tausch erst die alte, dann die neue | Vorschau und Klang gehören zur selben Dateigeneration; ein Fehlschlag erzeugt keine neue Nummer, also bleibt die alte Übersicht. Zehn Minuten Stereo kosten etwa 52 ms im Ladethread |

## Stand des Fahrplans

| Schritt | Inhalt | Stand |
| --- | --- | --- |
| 1 | Build-Gerüst, SDK, Presets, validator | Erledigt |
| 2 | Klangkern unverändert übernommen, Firmware-Test läuft mit MSVC | Erledigt |
| 3 | Noten, Parameter, Abtastrate des Hosts, Stereo, Zustand | Erledigt |
| 4 | Master Pitch mit Glättung, dazu Pitch Bend und Volume | Erledigt |
| 5 | GUI mit vier Reglern | Erledigt, in Studio One bedient (Version 0.1.0). Größenänderung seit 0.2.0, Klaviatur seit 0.3.0, beides in den DAWs noch nicht ausprobiert |
| 6 | Abnahme in den DAWs, CPU-Last | **Begonnen:** lädt in Studio One. Hörtest, Waveform, Live und CPU-Last offen |
| 7 | CI auf GitHub | **Offen** |

Die Ergebnisse stehen im
[README des Plugins](../instruments/toy-piano-classic/README.md), Abschnitte
„What the study showed" und „Verified".

## Offen

### Abnahme in den DAWs (Schritt 6)

Die Prüfliste steht im README des Plugins unter „Still to be checked". Die
wichtigste Unbekannte ist die MIDI-Steuerung: VST3 reicht Controller nicht roh
durch. Das Plugin meldet dem Host, welcher Controller zu welchem Parameter
gehört; ob Pitch Bend und die drei CCs ankommen, entscheidet der Host. Das
lässt sich nur in Studio One, Waveform und Live selbst feststellen.

### CI (Schritt 7)

Ein Workflow, der bei jedem Push unter Windows baut, den validator laufen lässt
und die Tests ausführt. Zwei Dinge sind vorher zu klären:

* Das Token, mit dem gepusht wird, braucht den Scope `workflow`. Ohne ihn
  lehnt GitHub jeden Push ab, der eine Datei unter `.github/workflows/` anlegt.
  Mit der GitHub-CLI: `gh auth refresh -s workflow`.
* Welche Visual-Studio-Version die Windows-Runner von GitHub mitbringen, ist
  vor dem Einrichten nachzusehen. Presets gibt es für 2026 und 2022; beide
  sind lokal gebaut und getestet.

### Fragen für den nächsten Port

| Nr. | Frage | Empfehlung |
| --- | --- | --- |
| 1 | Welcher Effekt wird das erste Testobjekt unter `effects/`? | `drive` oder `stereo-delay`: wenig Zustand, klar messbar. Reverb und Tape hängen stärker an DaisySP. |
| 2 | Wird DaisySP auf dem Desktop mitgenutzt? | Ja, als Submodul, sobald ein Port mehr als eine Funktion davon braucht. DaisySP ist reines C++ unter MIT. |
| 3 | Kopie oder gemeinsamer DSP-Kern mit Daisy-Seed? | Beim zweiten Port neu bewerten. Toy Piano kam mit drei unveränderten Dateien aus; das spricht bisher für die Kopie. |
| 4 | Was wandert nach `common/`? | Kandidaten nach diesem Port: die Größenabstimmung und das Skalieren des Fensters sowie die Parameterklassen für Zeiten und dB. Erst verschieben, wenn ein zweites Plugin sie braucht. |
| 5 | Hersteller-URL und E-Mail im Plugin | Beide Felder sind leer. Eintragen, sobald feststeht, was dort stehen soll. |
| 6 | Sollen Ergebnisse zurück in die Firmware fließen? | Der Nyquist-Schutz wäre ein Kandidat, falls die Firmware je über ±2 Halbtöne hinaus moduliert. |

## Was der erste Port über das Portieren gezeigt hat

* **Der Klangkern war das kleinste Problem.** Sauber von der Hardware getrennter
  Firmware-Code läuft ohne Änderung. Die Arbeit steckt in der Hülle: Parameter,
  Zustand, Fenster, MIDI-Zuordnung.
* **Feste Blockgröße und Kontrollrate der Firmware entfallen.** Der Host liefert
  beliebige Blockgrößen, und Noten tragen einen Sample-Offset. Wer den Kern
  sampleweise aufruft, bekommt bei jeder Blockgröße dasselbe Ergebnis.
* **Grenzen, die die Hardware garantiert, gelten im Plugin nicht.** Die Firmware
  läuft immer mit 48 kHz und moduliert höchstens ±2 Halbtöne. Im Plugin führen
  niedrige Abtastraten und +24 Halbtöne in einen Bereich, in dem der Kern über
  seine Sinustabelle hinausliest. Bei jedem Port ist zu prüfen, welche
  Annahmen der Firmware-Code über Abtastrate und Wertebereiche macht.
* **Referenz-Hashes der Firmware sind an Compiler und C-Bibliothek gebunden.**
  Als Messlatte taugt der Vergleich mit demselben Kern, auf demselben Rechner
  gerechnet.
* **MIDI-Controller sind in VST3 Parameter.** Pitch Bend und CCs brauchen je
  einen Parameter und die Zuordnung im Controller; was ankommt, hängt vom Host ab.
* **Die Fenstergröße muss mit dem Host ausgehandelt werden.** Bei skalierten
  Anzeigen fragt mancher Host die Größe ab, bevor er den Skalierungsfaktor
  mitteilt. Das Fenster war im ersten Versuch bei 120 % abgeschnitten.
* **Vektorzeichnung macht das Fenster frei skalierbar.** Ohne Bitmaps genügt ein
  Zoomfaktor für das ganze Fenster. Mit dem Host sind drei Dinge abzustimmen:
  dass das Fenster veränderbar ist, welche Größen erlaubt sind, und was bei
  einer neuen Größe geschieht. Ob der Nutzer am Rand ziehen kann, entscheidet
  trotzdem der Host.
* **Ruckeln erst messen, dann beheben.** Ein Größenschritt kostet im Plugin
  3–4 ms. Geruckelt hat in 0.2.0 die Regel: Bei jedem Mausereignis neu zu
  entscheiden, ob Breite oder Höhe führt, ließ das Fenster beim Ziehen an der
  Ecke springen. Die Lösung merkt sich je Ziehvorgang, ob eine Kante oder eine
  Ecke gezogen wird. Außerdem darf „Projekt geändert" nicht bei jedem
  Zwischenschritt an den Host gehen.
* **Eine unabhängige Prüfung findet, was die eigenen Tests nicht sehen.**
  Codex fand in 0.3.0 acht Befunde, die alle Tests und der validator
  übersehen hatten: Lebensdauer des Fensters bei einem nachlässigen Host,
  eine Glättung, die ihr Ziel nie erreicht, zu nachgiebige Texteingabe,
  Größenänderung gegen den VST3-Vertrag. Gemeinsam war allen, dass kein Test
  einen echten Host nachbildete. Seit 0.3.1 gibt es dafür ein Host-Doppel im
  Test. Ein Befund (R07) liegt in der Firmware selbst.
* **Die beiden VST3-Hälften reden über Nachrichten.** Was im Fenster passiert,
  erreicht den Audioteil nur als Parameter oder als Nachricht. Für Noten von
  der Bildschirm-Klaviatur passt die Nachricht; die Firmware-Queue bringt sie
  ohne Sperren in den Audio-Thread.
