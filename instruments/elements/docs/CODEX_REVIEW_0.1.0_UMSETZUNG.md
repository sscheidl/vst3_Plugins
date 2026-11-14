# Umsetzung des Codex-Reviews 0.1.0

* **Stand:** 2026-11-14
* **Bericht:** [CODEX_REVIEW_0.1.0.md](CODEX_REVIEW_0.1.0.md), Urteil
  „Änderungen vor dem Merge empfohlen", geprüft auf `elements-0.1.0` @
  `c3c3623`
* **Umgesetzt in:** Mendelejew 0.1.1
* **Umgesetzt von:** Claude (Fable 5.2 für 0.1.0, Opus 5.5 für diese
  Umsetzung)

Jeder Befund wurde zuerst am Code nachvollzogen; alle fünf sind bestätigt
und behoben. Zu jedem gibt es einen Test, der den Fall des Reviews
nachstellt. Gegen den unkorrigierten Code von 0.1.0 wurden diese Tests nicht
laufen gelassen; dass sie die Fehler dort gefunden hätten, stützt sich auf
die Reproduktionen des Reviews, denen sie folgen.

| ID | Befund | Bestätigt | Umsetzung | Test |
| --- | --- | --- | --- | --- |
| EL-R01 | Volle Klaviatur-Queue verliert auch Note-Offs | ja: `notify()` verwarf jedes Ereignis bei voller Queue, die Callbacks des Fensters ignorierten das Ergebnis | Jede Note in der Queue trägt eine laufende Nummer. Ein Loslassen, das keinen Platz findet, kommt in einen eigenen Platz je Taste (atomar, ohne Sperre) mit der Nummer der letzten Note davor. Der Audio-Thread spielt es, sobald er diese Note aus der Queue genommen hat: nach allem, was vorher kam, und vor allem, was danach Platz fand. Ein Anschlag, der nicht passt, wird weiter verworfen; er lässt nichts klingen. Die Aktivierung leert auch die neuen Plätze | `TestKeyboardLimits`: Taste 60, dann vor dem nächsten Block 32 Paare aus Loslassen und Anschlag (64 Ereignisse, die volle Queue), ein weiterer Anschlag (abgewiesen) und das Loslassen der letzten Taste (angenommen). Danach Gate zu, keine Taste gehalten, MIDI-Ausgang genau in der Reihenfolge der Eingaben, keine Allokation |
| EL-R02 | Abgewiesene MIDI-Note-Offs gelten als gesendet | ja: `SendKey()` löschte die Taste vor dem ungeprüften `addEvent()` | Zwei Mengen statt einer: Tasten, deren Note-On der Host hat (`host_keys_`), und davon die, deren Note-Off fällig ist, aber nicht durchging (`owed_releases_`). Ein Note-Off gilt erst als gesendet, wenn `addEvent()` Erfolg meldet; geschuldete gehen am Anfang jedes Blocks zuerst hinaus. Eine Taste mit geschuldetem Note-Off bekommt am Ausgang kein neues Note-On (die Engine spielt sie trotzdem). Ein Block ohne Ausgangsliste zählt wie eine volle Liste; nach einer Aktivierung werden alle gehaltenen Tasten geschuldet | `TestKeyboardLimits`: Ausgangsliste mit Platz für ein Ereignis; Anschlag und Loslassen im selben Block, Note-Off im nächsten; Anschlag, Loslassen, Anschlag derselben Taste; ein Block ohne Ausgangsliste |
| EL-R03 | Hinter dem Wandler überschreitet das Plugin ±1 | ja | Die letzte Stufe in `Processor::Render()` begrenzt beide Kanäle hart auf ±1. Bei 48 kHz wird nichts gewandelt und die Begrenzung wirkt nie (die Ausgabe bleibt bitgleich); sonst nur bei voll ausgesteuerten, hellen Klängen. Eine weichere Begrenzung hätte jeden lauten Klang an umgerechneten Raten verändert | `TestOutputBound`: der Zustand und die Note des Reviews bei 44,1, 48, 88,2, 96 und 192 kHz, 0,435 s in Blöcken zu 48. Spitze überall genau 1,0; bei 44,1 kHz prüft der Test, dass die Grenze erreicht wird. Dazu die Grenzprüfung des Host-Doubles in allen übrigen Tests |
| EL-R04 | Moden-Umrechnung nur bei hoher Güte genau | ja: die Herleitung über `sin(Winkel) / (2 Q)` gilt nur für hohe Güten, und der Winkel hielt die Bandmitte statt des Pols fest | Neu `Resonator::ConvertMode`: die Pole der Mode bei 32 kHz in geschlossener Form (Radius über `atanh`, Winkel über `atan2`), auf die neue Rate übertragen (Logarithmus des Radius und Winkel durch das Ratenverhältnis) und daraus `g` und Güte zurückgerechnet; `1 - rho²` über `expm1`, damit hohe Güten genau bleiben. Die Verstärkung gleicht die Amplitude des Ausschwingens nach einem Impuls gleicher Fläche an. Für hohe Güten ergibt das die alte Umrechnung. Die gestrichenen Moden behalten die alte: Ihr normierter Bandpass sitzt in einer Verzögerungsschleife, dort zählen Bandmitte und Bandbreite | `TestModeConversion`: 10 Frequenzen von 16 Hz bis 15,7 kHz, Güte 1 bis 20000, bei 48 und 96 kHz; Pole aus den Koeffizienten (Wurzeln des Nenners), Amplitude aus der gerechneten Impulsantwort. Größte Abweichung von Frequenz, Abklingrate und Amplitude rund 3e-7, Float-Genauigkeit; die alte Umrechnung lag bis 8 % daneben. Der Fall des Reviews (Güte 1, 8 kHz): T60 0,392982 ms bei 32 und bei 48 kHz |
| EL-R05 | Startfilter im ersten Quantum unskaliert | ja: `Svf::Init()` legt 0,01 der Rate fest, unabhängig von der Rate | `Resonator::Init()` setzt bei anderen Raten jede Mode auf den mit `ConvertMode` umgerechneten Startfilter des Originals (0,01 von 32 kHz, Güte 100) samt Verstärkung. Bei 32 kHz unverändert | `TestStartFilters`: die Mode 27 allein (Differenz zweier Resonatoren mit 26 und 27 Moden), Impuls bei Sample 0, ohne Vorlauf: bei 48 und 96 kHz innerhalb 6e-6 des umgerechneten Startfilters; der unumgerechnete wäre deutlich anders, das zeigt der Test mit |

## Auswirkung auf den Klang

* **32 kHz:** weiterhin das Original, bitgenau (48 Szenarien, 6 144 000
  Samples). Die neuen Wege laufen nur bei anderen Raten.
* **48 und 96 kHz:** Die Messungen gegen das Original
  ([DSP_MEASUREMENTS_0.1.1.md](DSP_MEASUREMENTS_0.1.1.md)) haben sich in
  Tonhöhe und Ausklang nicht verändert und in den Bandpegeln um höchstens
  0,1 dB. Die Messfälle haben überwiegend hohe Güten, bei denen alte und
  neue Umrechnung übereinstimmen. Die Änderung wirkt bei niedriger Güte, also
  breiten, kurz klingenden Moden: Damping und Brightness niedrig (in Elements
  klingt ein niedriger Damping-Wert kurz), dort vor allem die oberen Moden.
  Angehört ist das nicht.
* **Last:** 3,2 % eines Kerns bei 48 kHz, 5,8 % bei 96 kHz (`MeasureLoad`),
  wie vorher.

## Weitere Punkte aus dem Bericht

* **Messbereich des Erreger-Tiefpasses:** Die 1,3-dB-Angabe gilt für 100 Hz
  bis 14 kHz, den Bereich des Tests; [UPSTREAM.md](../UPSTREAM.md) sagt das
  jetzt, mit dem Wert von 1,7 dB bei 14,95 kHz aus dem Review.
* **Abdeckung des 32-kHz-Vergleichs** (keine vollständige Zweigabdeckung,
  kein erzwungener Panik-Fall, NOISE-Erreger nicht über `Part` erreichbar):
  zur Kenntnis genommen, nicht geändert.
* **Zustand:** zusätzliche Bytes am Ende werden nicht geprüft; kein Befund,
  nicht geändert.
* **Torus Classic**: Der Code hier stammt aus Torus Classic 0.2.0. Torus
  Classic 0.2.1 (`697b33d` auf `torus-classic-0.1.0`, aus einer anderen
  Sitzung) behandelt Loslassen und MIDI-Ausgabe inzwischen auf eigene Weise
  (`release_due_`, Note-Off erst nach erfolgreichem `addEvent`). Dieser
  Branch enthält 0.2.1 nicht und ändert an Torus Classic nichts; die beiden
  Lösungen sind nicht aufeinander abgestimmt.
* **Build-Bericht 0.1.0** bleibt als Dokument seines Stands; ein Hinweis am
  Anfang verweist auf den Review und diese Umsetzung. Die Messungen von
  0.1.0 bleiben in [DSP_MEASUREMENTS_0.1.0.md](DSP_MEASUREMENTS_0.1.0.md).

## Geprüft nach der Umsetzung

VS 2026 (MSVC 19.51), Release und Debug: Build aller drei Plugins, Validator,
CTest 10 von 10 in beiden Konfigurationen; die Logs liegen in
`out/elements-0.1.1/logs/` (nicht versioniert), die Zahlen im
[Pull Request](https://github.com/sscheidl/vst3_Plugins/pull/2). VS 2022 und
AddressSanitizer wurden nicht ausgeführt.

## Nachprüfung

Bitte EL-R01 bis EL-R05 erneut prüfen, am letzten Commit des Branches
`elements-0.1.0` („Mendelejew 0.1.1"), mit den Regeln des
[Prüfauftrags 0.1.0](CODEX_PRUEFAUFTRAG_0.1.0.md). Besonders:

* die Herleitung in `resonator.cc` (`ConvertMode`), auch für Moden nahe
  der halben Abtastrate und für die Güten, die der Resonator tatsächlich
  erzeugt;
* die Reihenfolge zwischen Queue und zurückgestellten Loslass-Ereignissen in
  `processor.cpp`, auch wenn der UI-Thread schreibt, während der Audio-Thread
  die Queue leert;
* ob die Begrenzung hinter dem Wandler (EL-R03) an anderer Stelle eine Zusage
  bricht, etwa die Blockgrößen-Unabhängigkeit.

Ergebnis bitte nach `instruments/elements/docs/CODEX_REVIEW_0.1.1_NACHPRUEFUNG.md`.
Codex hat seinen
Arbeitsordner `.work/elements-review/` nicht aufräumen können; er liegt noch
im Repo-Verzeichnis und ist von Git ausgenommen.
