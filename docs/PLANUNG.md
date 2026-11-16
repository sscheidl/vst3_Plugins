# Planung

Stand: 2026-11-14. Das erste Testobjekt, Toy Piano Classic 0.3.2, ist gebaut
und automatisch geprüft. In Studio One lädt es, die Regler reagieren. Angehört
wurde es noch nicht; Waveform und Live stehen aus. Dazugekommen sind Torus
Classic und, als dritter Port, Mendelejew (Projekt Elements); ihr Stand steht in den
Abschnitten unten. Seit 2026-11-16 gibt es als viertes Plugin Toy Piano
Generative: kein Port, sondern ein generativer Sequencer vor dem Klangkern des
ersten.

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

### Toy Piano Generative

Viertes Plugin, Auftrag vom 2026-11-15, gebaut am 2026-11-16: der Klangkern
von Toy Piano Classic, gespielt von einem generativen Schritt-Sequencer.
Version 0.1.0 spielt in Studio One; der Nutzer am 2026-11-16: „Das Plugin und
der Algorithmus funktioniert. Es entstehen melodische, zufällige Melodien."
Version 0.2.0 bringt die Ergänzungen, die er danach verlangt hat; sie ist
gebaut und automatisch geprüft, in einer DAW aber noch nicht. Die Prüfung
durch Codex steht für beide Versionen aus
([Prüfauftrag 0.2.0](../instruments/toy-piano-generative/docs/CODEX_PRUEFAUFTRAG_0.2.0.md),
[DAW-Prüfliste](../instruments/toy-piano-generative/docs/DAW_TEST_0.1.0.md)).

Der Sequencer ist ein statistischer Nachbau nach einer Verhaltensspezifikation
des Nutzers, die nicht in diesem Repository liegt: gleiche Regeln und
Wahrscheinlichkeiten, eigener Zufallsgenerator, kein fremder Code. Die Regeln
stehen in eigenen Worten im
[README des Plugins](../instruments/toy-piano-generative/README.md).

Vom Nutzer entschieden (2026-11-15 und 2026-11-16):

| Thema | Entscheidung |
| --- | --- |
| Form | Neues Instrument `toy-piano-generative` mit eigenen Klassen-IDs; Toy Piano Classic bleibt unverändert |
| Nachbau | Statistisch, nicht 1:1: „Der statistische Ansatz passt für mich" |
| Stimmung | In der Vorgabe exakt; stufenlos bis zum ganzzahligen Hertz darunter |
| Startmuster | Pausen statt eines tiefen Tons |
| Fenster | Wie Toy Piano Classic, dazu eine Reihe für den Sequencer; keine Schrittanzeige |
| Transponieren | Schon in 0.1.0. Ein Schalter: an setzt eine Taste Tonika und Oktave der Sequenz, aus spielt sie eine Note. MIDI-Noten ebenso. Wirkt sofort |
| Schwarze Tasten | Alle zwölf Tonika, obwohl die Spezifikation nur die sieben Stammtöne kennt |
| Oktavknöpfe | Verschieben den sichtbaren Tastenbereich und transponieren die Sequenz um dieselbe Oktave |
| Gemeinsamer Code | Oktavknöpfe aus Mendelejew nachgebaut, nichts nach `common/`; der Sequencer-Kern bleibt im Plugin-Verzeichnis |
| Vier unklare Stellen der Spezifikation | Lesarten des Bauenden als Annahmen übernommen, im README unter „Choices and deviations" 1 bis 4; noch gegen das Original zu prüfen |

Bei der Umsetzung selbst festgelegt, weil der Auftrag es offen ließ:

| Thema | Festlegung | Begründung |
| --- | --- | --- |
| Klangkern | Das Plugin linkt das Target `toy_piano_core` aus Toy Piano Classic, statt die vier Firmware-Dateien ein zweites Mal abzulegen | Zwei Kopien derselben unveränderten Dateien müssten gemeinsam gepflegt werden; Classic ändert sich dadurch nicht |
| Hülle | Kopie aus Toy Piano Classic 0.3.2 im eigenen Namensraum | Wie bei Kekulé und Mendelejew: Verschieben nach `common/` hätte ein geprüftes Plugin geändert |
| Sequencer-Kern | Reines C++20 ohne SDK, ohne Allokation, eigenes Target `generative_sequencer` | Auf dem Host einzeln testbar und später auf Daisy übernehmbar |
| Tonlage | Sequencer-Note + 24 Halbtöne; Noten über 127 eine Oktave tiefer | Vorgabe (Tonika A, Oktave 2) liegt dann bei A3 bis G5, um die Tasten des Toy Piano. Der Nutzer hatte keine Vorgabe; eine Konstante (`kSequencerTranspose`) |
| Transport | Schritte nur bei laufendem Host-Transport und RUN an, auf den Sechzehnteln der Songposition, samplegenau. Jeder Start beginnt bei Schritt 1; Muster und Zufallsgenerator laufen weiter. Sprünge (Loop) starten nicht neu | Ein Sequencer ohne eigene Uhr soll dem Projekt folgen; ein Neustart je Durchlauf wäre bei Schleifen ungewollt |
| Ohne Tempo oder Position | 130 BPM (Vorgabe der Spezifikation); Zählung ab Start | Der validator und einfache Hosts melden beides nicht |
| Takt über Blockgrenzen | Jede Sechzehntel genau einmal: eine knapp verpasste kommt noch, eine schon gespielte nicht erneut | Bei Tempowechseln stimmt die Hochrechnung des letzten Blocks nicht mit der neuen Position überein |
| Zufall | Je Instanz ein eigener Startwert aus Zufallsquelle und Uhr, beim Erzeugen der Instanz. Zustand des Generators im Projekt | Zwei Instanzen sollen nicht dasselbe spielen. Anders als bei Kekulé und Mendelejew, wo der Zufall Teil des Klangs ist und jedes Rendern gleich sein soll |
| Folge für Exporte | Mit Mutation über 0 klingen zwei Durchläufe verschieden | Das Muster läuft weiter, es spult nicht mit dem Song zurück. Im README unter „Limits" |
| Zustand | Klangwerte, Sequencer-Einstellungen, die 16 Schritte, Zufallszustand; nicht die Position. Werte außerhalb der Bereiche werden abgelehnt, nicht geklemmt | Ein geladenes Projekt startet ohnehin bei Schritt 1; ein beschädigter Zustand soll nie teilweise wirken |
| Übergabe zwischen den Threads | Der Sequencer gehört dem Audio-Thread. Er veröffentlicht seinen Zustand über eine Sequenzsperre aus atomaren Worten; `setState` hinterlegt einen Zustand, den der Audio-Thread am nächsten Blockanfang übernimmt; eine Anfragenummer sagt `getState`, welcher von beiden gilt | Keine Sperre im Audio-Thread, und `getState` direkt nach `setState` liefert das Geladene, auch wenn kein Audio läuft |
| Laden zieht kein Muster | Der Skalenparameter zieht nur neu, wenn sich die Skala ändert | Sonst wäre das gespeicherte Muster nach jedem Laden verloren |
| Sequencer-Einstellungen | Wirken am Blockanfang, hörbar ab dem nächsten Schritt | Wie die übrigen Parameter; Schritte liegen mindestens 15 ms auseinander |
| MIDI-Transposition | Im Processor, am Sample der Note; neue Werte von Tonic und Octave gehen über die Ausgangs-Parameteränderungen an den Host | Nur so wirkt die Note taktgenau; ob der Host die Werte anzeigt, entscheidet er |
| Tasten der Bildschirm-Klaviatur | Ändern Tonic und Octave als gewöhnliche Parameter-Edits über den Controller | Der vorgesehene VST3-Weg; der Host kann sie aufnehmen |
| Oktavknöpfe an den Grenzen | Klaviatur (C1 bis C7) und Oktave (0 bis 5) halten getrennt an | Eine Taste kann die Oktave ohnehin unabhängig vom sichtbaren Bereich setzen |
| Stimmung | Je Stimme ein Faktor, beim Anschlag aus der klingenden Note berechnet, vor Master Pitch; gilt für alle Noten | Kleine Ergänzung im vorhandenen Weg der Tonhöhe; bei 0 % ist der Faktor exakt 1 und der bitgenaue Vergleich mit dem Kern gilt weiter |
| Anschlag der Sequencer-Noten | Velocity 100 | Wie die Tasten der Firmware und die Bildschirm-Klaviatur |
| Skala ohne Namen | Anzeige „Odd" | Die Spezifikation nennt für die vierte Liste keinen Namen |
| Gleitkomma-Modell | `/fp:precise` für Kern, Hülle und Tests | Zustandsprüfung und Kennlinie verlassen sich auf NaN-Vergleiche |
| Regler-Edits | Ein Ziehen meldet dem Host Anfang und Ende (`beginEdit`, `endEdit`) | Für die Schalter nötig; für die Regler mit eingebaut. Toy Piano Classic tut das nicht, siehe „Offen" |
| Tests | Statistik mit festen Startwerten und Schranke von fünf Standardabweichungen; Schritte bitgenau gegen den Kern; keine neuen Fenstertests außer den Oktavknöpfen | Der Nutzer prüft Optik, GUI und DAW selbst |

Vom Nutzer nach dem ersten Test verlangt und in 0.2.0 umgesetzt (2026-11-16);
die Einzelheiten selbst festgelegt:

| Thema | Festlegung | Begründung |
| --- | --- | --- |
| Mutationssperre | Schloss-Symbol unten rechts am Mutation-Regler, Parameter 121. Zu: kein Algorithmus ändert das Muster, und der Zufallsgenerator steht. Tonic und Scale wirken weiter | Vorgabe des Nutzers: „Sequenz eingefroren und nicht mutiert bis der Lock aufgehoben wird". Tonika und Skalenwahl sind gezielte Eingriffe, keine Mutation; ein Schloss, das sie sperrt, würde verwirren |
| Namen der Algorithmen | Drift, Runs, Ladder, Breathe, Focus; die Erklärung als Tooltip über dem Namen und dem Regler. Eingabe per Name oder Zahl | Vorgabe: Namen, „unter denen man sich etwas vorstellen kann", ersatzweise Elementnamen. Beschreibende Namen gingen; Elementnamen bleiben als Möglichkeit, falls diese nicht gefallen |
| Weitere Skalen | Zehn dazu, 17 insgesamt: Kirchentonarten (ohne Lokrisch), harmonisch und melodisch Moll, Dur-Pentatonik, Blues, Ganzton, Hirajoshi. Jede Liste aufwärts und mit der Tonika am Ende, wie die Listen der Spezifikation | Vorgabe: „weitere geeignete / sinnvolle Skalen". Die Regel der zusätzlichen Tonika hält den Anteil der Tonika bei allen Skalen ähnlich |
| Reihenfolge der Skalen | Erst die üblichen, dann die besonderen; „Pentatonic" heißt jetzt „Minor Pent." | Eine Liste von 17 Einträgen braucht eine Ordnung; 0.1.0 ist noch nicht gemergt |
| Mutation nach Durchläufen | Kleiner Regler links unten an Mutation, Parameter 122: „-" (Vorgabe) mutiert wie in der Spezifikation bei jeder Sechzehntel; 1 bis 16 lässt das Muster so oft unverändert laufen, dann beginnt der nächste Durchlauf mit allen Versuchen eines Durchlaufs auf einmal | Vorschlag des Nutzers: „nach wie vielen Wiederholungen der Sequenz eine Mutation erfolgen soll", 0 = wie gesperrt. Statt 0 gibt es „-", weil das Schloss schon einfriert und die laufende Mutation der Spezifikation sonst verloren ginge. Ein Durchlauf auf einmal, damit Mutation weiter „wie viel" heißt. Der Nutzer war unsicher und entscheidet nach dem Ausprobieren |
| Abdunkeln bei Ladder | Mutation, Notes, Schloss und kleiner Regler halbdurchsichtig, solange Algorithmus 3 gewählt ist; bedienbar bleiben sie | Der Nutzer hielt ein Muster, das bei voller Mutation stehen blieb, für einen Fehler: Ladder war gewählt, das nach der Spezifikation nie mutiert |
| Alte Projekte | Zustand Version 2; Version 1 lädt mit offener Sperre, laufender Mutation und ihrer Skala unter der neuen Nummer. Automation von Scale aus 0.1.0 zeigt danach auf andere Skalen | Der Regler hat jetzt 17 statt 7 Stufen; normierte Automationswerte lassen sich nicht umrechnen. Betrifft nur Testprojekte |

Nicht in 0.2.0: Parameter-Locks, Gate-, Hüllkurven- und Glide-Regeln, zweiter
Oszillator, LFO-Wege, Sync-Impulse, Schrittanzeige, Feinstimmung, die
„Age"-Idee, der Bau für Daisy.

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

### Toy Piano Generative: Schritt-Sequencer (für einen späteren Build)

Wunsch des Nutzers vom 2026-11-16, wörtlich: „für späteres Build zusätzlicher
monophoner / polyphoner Stepsequencer mit 16, später 32 Steps, dazu weiter
Button (Step) mit eigener Ansichtsseite. Dieser sollte auch generativ
‚Mutieren' können: a) Tonhöhen innerhalb der ausgewählten Skala b) Rhythmus der
Noten." Vorbild ist ein Sequencer-Modul, dessen Firmware als Nächstes in L2A
untersucht wird; daraus soll, wie beim jetzigen Sequencer, eine
Verhaltensspezifikation entstehen. Noch nichts entschieden oder gebaut.

Ebenfalls offen: eine Schrittanzeige für den generativen Sequencer. Der
Nutzer denkt über die Form nach.

### Toy Piano Classic: Regler ohne `beginEdit`/`endEdit`

Beim Bau von Toy Piano Generative aufgefallen, dort behoben, in Classic nicht
angefasst: Die Regler melden dem Host Wertänderungen, aber weder Anfang noch
Ende eines Edits. Manche Hosts brauchen beides, um Automation aufzunehmen. Ob
es in Studio One stört, ist nicht geprüft.

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
