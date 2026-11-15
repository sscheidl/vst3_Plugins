# Umsetzung der Nachprüfung 0.1.1

* **Stand:** 2026-11-15
* **Bericht:** [CODEX_REVIEW_0.1.1_NACHPRUEFUNG.md](CODEX_REVIEW_0.1.1_NACHPRUEFUNG.md),
  Urteil „noch keine Empfehlung zum Merge", geprüft auf `elements-0.1.0` @
  `2f9b0ff`
* **Umgesetzt in:** Mendelejew 0.1.2
* **Umgesetzt von:** Claude (Opus 5.5)

Die Nachprüfung bestätigt EL-R02 bis EL-R05 als behoben und fand in der
Lösung für EL-R01 einen neuen Fehler, EL-R06. Er ist am Code
nachvollzogen und bestätigt.

## EL-R06 — Ein verspätet übernommenes Loslassen beendet den neueren Anschlag

**Bestätigt:** ja. Die Lösung von 0.1.1 legte ein Loslassen, das die volle
Queue fand, außerhalb der Queue ab: in einem atomaren Platz je Taste, dazu
ein Merker. Der Audio-Thread las diese Plätze nur einmal am Anfang eines
Blocks. Veröffentlichte der UI-Thread das Loslassen zwischen diesem Lesen und
dem Leeren der Queue und legte gleich danach einen neuen Anschlag derselben
Taste in die Queue, spielte der Audio-Thread erst den Anschlag und einen Block
später das ältere Loslassen. Die Reihenfolge, die Codex beschreibt, ist
zulässig, und das Ergebnis ist das gemessene: die neue Note endet zu früh,
in der Engine und am MIDI-Ausgang.

**Umsetzung:** Statt die Überholung durch weitere Abgleiche zu verhindern,
gibt es die zweite Übergabe nicht mehr. Jedes Loslassen geht wieder durch die
eine Queue, deren Reihenfolge die der Eingaben ist. Damit es dort nie an
Platz fehlt, führt `Processor::notify()` (UI-Thread, der einzige Produzent)
die Tasten, deren Anschlag in der Queue ist und deren Loslassen nicht
(`keyboard_keys_down_`), und hält für jede davon einen Platz frei:

* Ein Anschlag geht nur hinein, wenn danach noch Platz für ein Loslassen jeder
  gedrückten Taste bleibt, die eigene eingeschlossen. Sonst wird er
  abgewiesen, wie schon bei voller Queue in 0.1.0 und 0.1.1. Er lässt dann
  nichts klingen.
* Ein Loslassen einer gedrückten Taste findet deshalb immer Platz.
* Ein Loslassen einer Taste, die nicht gedrückt ist, beendet nichts und kommt
  nicht in die Queue.

Den Füllstand liest der UI-Thread mit `SpscQueue::SizeForProducer()` (neu).
Der Wert ist nie kleiner als der wahre, weil der Audio-Thread nur entnimmt;
die Rechnung bleibt damit auf der sicheren Seite. Die Queue fasst jetzt 256
statt 64 Noten, damit Abweisungen auch bei vielen gedrückten Tasten selten
bleiben. Der Audio-Thread liest wie bisher höchstens eine Queue-Länge je
Block, ohne Sperre und ohne Allokation. Die atomaren Plätze, die laufenden
Nummern und das Nachholen nach dem Block sind entfernt.

**Warum keine gesteuerte Unterbrechung als Test:** Codex' Nachweis braucht
Wartepunkte zwischen dem Ablegen außerhalb der Queue und dem Merker. Diese
Stelle gibt es nicht mehr: Ein Loslassen ist entweder in der Queue oder
noch nicht da, und was die Queue liefert, kommt in der Reihenfolge, in der es
hineinkam. Getestet wird deshalb die Regel selbst und ihr Verhalten mit zwei
echten Threads.

**Test** (`TestKeyboardLimits`):

* Die Queue bis an die Grenze gefüllt: 255 erneute Anschläge der gehaltenen
  Taste gehen hinein, der 256. und ein Anschlag einer anderen Taste werden
  abgewiesen, das Loslassen wird angenommen. Danach ist die Queue voll, auch
  ein weiterer Anschlag wird abgewiesen. Nach dem Block sind Gate und MIDI
  genau wie eingegeben.
* Codex' Folge im Ergebnis: Der Anschlag derselben Taste nach ihrem Loslassen
  klingt und bleibt eine Sekunde lang gehalten, in Engine und MIDI-Ausgang.
* Ein Loslassen einer nicht gedrückten Taste ändert nichts.
* Zwei Threads: Ein UI-Thread schickt 20 000 zufällige Anschläge und
  Loslass-Ereignisse auf vier Tasten, während der Audio-Thread ohne Pause
  Blöcke rechnet; vier Runden, zwei mit Blöcken zu 32 Samples, zwei mit 2048,
  in denen der UI-Thread schneller ist und die Queue vollläuft. Rund 17 000
  Anschläge je Lauf werden dabei abgewiesen, kein einziges Loslassen. Am Ende
  halten Engine und MIDI-Ausgang genau die Tasten, die der UI-Thread gedrückt
  ließ. Dreimal hintereinander bestanden. Ob dieser Test den Fehler von 0.1.1
  gefunden hätte, ist nicht geprüft; er hängt dort von einer seltenen
  Unterbrechung ab.

## Geprüft nach der Umsetzung

VS 2026 (MSVC 19.51), Release und Debug: Build aller drei Plugins, Validator,
CTest in beiden Konfigurationen; Logs in `out/elements-0.1.2/logs/` (nicht
versioniert), Zahlen im
[Pull Request](https://github.com/sscheidl/vst3_Plugins/pull/2). DSP und
Messungen sind unverändert gegenüber 0.1.1
([DSP_MEASUREMENTS_0.1.1.md](DSP_MEASUREMENTS_0.1.1.md)). Zusätzlich lief
0.1.1 zusammen mit Torus Classic 0.2.1 (`697b33d`) in einem eigenen
Arbeitsbaum: Release und Debug grün, Validator 47/0, 10 von 10 Tests. Für
0.1.2 ist dieser gemeinsame Lauf nicht wiederholt.

## Nachprüfung

Bitte EL-R06 erneut prüfen, am letzten Commit des Branches `elements-0.1.0`
(„Mendelejew 0.1.2"), mit den Regeln des
[Prüfauftrags 0.1.0](CODEX_PRUEFAUFTRAG_0.1.0.md). Besonders:

* die Platzregel in `Processor::notify()`: Gibt es eine Folge von Anschlägen
  und Loslass-Ereignissen, nach der ein Loslassen keinen Platz findet oder
  eine Taste falsch als gedrückt oder nicht gedrückt geführt wird, auch über
  `setActive()`, das Laden eines Zustands und die 500-ms-Frist der Anschläge
  hinweg?
* `SpscQueue::SizeForProducer()` und seine Speicherordnung.
* ob eine Abweisung von Anschlägen an anderer Stelle stört, etwa im Fenster.

Ergebnis bitte nach `instruments/elements/docs/CODEX_REVIEW_0.1.2_NACHPRUEFUNG.md`.
