# Planung

Stand: 2026-11-14. Das erste Testobjekt, Toy Piano Classic 0.3.2, ist gebaut
und automatisch geprüft. In Studio One lädt es, die Regler reagieren. Angehört
wurde es noch nicht; Waveform und Live stehen aus. Dazugekommen sind Torus
Classic und, als dritter Port, Mendelejew (Projekt Elements); ihr Stand steht in den
Abschnitten unten.

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

### Torus Classic

Zweiter Port, Auftrag vom 2026-11-14
([Buildauftrag](CLAUDE_BUILDAUFTRAG_TORUS_CLASSIC_0.1.0.md)). Version 0.1.0 lädt
und spielt in Studio One (erster DAW-Test des Nutzers am 2026-11-14). Codex
fand in 0.1.0 vier Fehler; sie sind seit 0.1.1 behoben. 0.2.0 bringt die
Ergänzungen aus dem DAW-Test und ist gebaut und automatisch geprüft, in
einer DAW aber noch nicht ausprobiert. Was sich wann geändert hat, steht im
[Änderungsprotokoll](../instruments/torus-classic/CHANGELOG.md). Einzelheiten und Messwerte stehen im
[Build-Bericht](../instruments/torus-classic/docs/BUILD_REPORT_0.1.0.md), die
offene Abnahme in der
[DAW-Prüfliste](../instruments/torus-classic/docs/DAW_TEST_0.1.0.md).

Vorgegeben waren Umfang, Parameter-IDs, MIDI-Zuordnung, 48 kHz und 12 Samples
intern sowie die Anschlagsregel. Bei der Umsetzung selbst festgelegt, weil der
Auftrag es offen ließ:

| Thema | Festlegung | Begründung |
| --- | --- | --- |
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

### Mendelejew (Projektname Elements)

Dritter Port, Auftrag vom 2026-11-14 („Phase 1 – Mutable Instruments Elements
als natives VST3"). Version 0.1.0 ist gebaut, vom validator geprüft,
automatisch getestet und von Codex geprüft
([Review](../instruments/elements/docs/CODEX_REVIEW_0.1.0.md)); 0.1.1 behebt
dessen fünf Befunde
([Umsetzung](../instruments/elements/docs/CODEX_REVIEW_0.1.0_UMSETZUNG.md)),
die Nachprüfung steht aus. In einer DAW gespielt und angehört ist das Plugin
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
