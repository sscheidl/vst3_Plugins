# Umsetzung des Codex-Reviews 0.2.0

* **Stand:** 2026-11-14
* **Bericht:** [CODEX_REVIEW_0.2.0.md](CODEX_REVIEW_0.2.0.md), Urteil „Freigabe
  mit Korrekturen", geprüft auf `torus-classic-0.1.0` @ `78e5b9a`
* **Umgesetzt in:** Torus Classic 0.2.1
* **Umgesetzt von:** Claude

Jeder Befund wurde zuerst am Code nachvollzogen. Alle fünf sind bestätigt und
behoben, jeweils mit einem Regressionstest nach dem Szenario des Berichts.
Die neuen Tests wurden nur gegen den korrigierten Code ausgeführt, nicht
zusätzlich gegen 0.2.0. Ihr Szenario ist dasselbe, mit dem Codex die Fehler
reproduziert hat.

| ID | Befund | Bestätigt | Umsetzung | Test |
| --- | --- | --- | --- | --- |
| TC-R05 | Filter-Glättung bleibt bei hohen Raten vor dem Ziel stehen | ja: Wert und Glättungsfaktor in `float`; nahe 1 ist der Schritt kleiner als ein halbes ULP | `OutputStage`: Steuerwert und Glättungsfaktor in `double`. Exakter Snap, wenn der Rest unter 1e-6 liegt oder ein Schritt den Wert nicht mehr ändert | `TestOutputStage`: je sechs Übergänge in beide Richtungen (u. a. 0.2 → 1, 1 → 0, 0.5 → 0.99) bei 1 kHz, 44.1, 48, 88.2, 96, 192 kHz und 4 MHz. Der Wert trifft das Ziel exakt; bei 1 ist der Mix 0 und das Sample unverändert |
| TC-R06 | Alter Filterzustand kehrt nach langem Bypass zurück | ja: bei `mix_ == 0` wurden die Integratoren übersprungen | Die Integratoren laufen immer, auch wenn der Filter ganz offen ist. Ausgang bei Mix 0 weiterhin exakt der Eingang. Der Zustand folgt so stets dem aktuellen Signal; ein Reset beim Bypass war damit unnötig | `TestOutputStage`: Codex' Szenario mit 1-kHz-Sinus und mit Gleichanteil, je vier Stopp-Phasen. Filter 0.5, öffnen, zehn Sekunden Nullen (exakt null), wieder schließen: Spitze unter 1e-20 statt −39,75 dBFS |
| TC-R07 | Anzeige und Engine runden Tune nahe einer Halbton-Schwelle verschieden | ja: Controller rundete den Double-Wert, die Bridge erst nach der Umwandlung in `float` | Neu `TuneStep()` in `parameters.h`: die einzige Rundung, aus dem Double-Wert des Hosts. `TuneSounding()` (Anzeige, Texteingabe) und die neue `TuneEvent()` benutzen sie; das Tune-Ereignis trägt den Halbton in `EngineEvent::data` neben dem Wert für freies Stimmen. `EngineBridge::JumpTune` nimmt den Double-Wert des geladenen Zustands | Neu `TestTuneRounding`: 962 Werte an allen 48 Halbton-Schwellen. Dabei die Schwelle selbst, ihr Float-Bild, dessen Nachbarn, jeweils ±1 ULP und ±1e-9, sowie Codex' zwei Werte. Für jeden Wert müssen angezeigter Halbton, Engine nach Automation und Engine nach geladenem Zustand gleich sein. Mit FREE an bekommt die Engine den Wert selbst |
| TC-R08 | Note-offs der Bildschirmtasten verlierbar | ja, beide Wege: (a) Release in voller Queue abgewiesen; (b) Held-Status vor `addEvent` gelöscht, Fehlschlag ignoriert | (a) Releases laufen nicht mehr durch die Queue. `notify` zählt die angenommenen Anschläge je Taste; ein Release speichert diesen Stand atomar und wird nie abgewiesen. Der Audio-Thread vergleicht ihn mit dem Anschlag, zu dem das ausgegebene Note-on gehört. Kurze Tipper (Anschlag und Release im selben Block) ergeben Note-on und Note-off. (b) `SendKey` ändert den Held-Status erst nach erfolgreichem `addEvent`. Ein Note-off ohne Platz oder ohne Liste wird vorgemerkt und im nächsten Block wiederholt. Ein Note-on, das der Host nicht nimmt, braucht kein Note-off. Bei erneutem Anschlag einer gehaltenen Taste, deren Note-off keinen Platz findet, behält der Host die alte Note bis zum nächsten Block und bekommt kein zweites Note-on. Neue Anschläge dürfen bei Überlast weiter entfallen | `TestKeyboardMidiOut` prüft jetzt über alle Blöcke die vom Host angenommene Geschichte: nie ein zweites Note-on für eine gehaltene Taste, nie ein Note-off ohne Note-on. Neu: Release bei voller Queue (64 Anschläge), Liste mit Platz für ein Event (zwei Releases, Release plus neuer Anschlag, erneuter Anschlag bei vollem Platz), Block ganz ohne Liste, kurzer Tipper. Am Ende hält der Host keine Note mehr |
| TC-R09 | Ausgangsstufe macht NaN/Inf nicht endlich | ja | Eingangssamples der Ausgangsstufe: NaN und ±Inf werden 0, endliche Werte über ±1e6 werden auf ±1e6 begrenzt. Damit bleiben Integratoren und Limiter-Gain endlich, und der Limiter begrenzt wie gewohnt auf 0.5. Der Weg bei regulären Signalen ist unverändert | `TestOutputStage`: alle Paare aus NaN, ±Inf, ±FLT_MAX, ±1e30, kleinstem Denormal und 0, mit offenem und mit geschlossenem Filter. Ausgang stets endlich und ≤ 0.5; danach vier Sekunden normales Signal, Gain wieder 1, offen bit-gleich und geschlossen innerhalb 1e-6 gleich einer Stufe, die nie einen Fehler sah |

## Weitere Punkte aus dem Bericht

* **Damping-Wortlaut:** README („Sound") und der Kommentar zu
  `getTailSamples` in `source/processor.h` sagen jetzt, dass Damping 0
  unendlich hält.
* **Kosten:** Der Filter rechnet jetzt auch im Bypass, etwa zehn
  Gleitkommaoperationen je Kanal und Hostsample. Die Glättung läuft im
  Endzustand nicht mehr weiter (TC-R05), so dass `pow`/`tan` je Sample nach
  jeder Bewegung nach etwa 70 ms enden statt nie.
* **Fehlende Tests 6 und 7** (V1-Präfixe, gezielt synchronisierter
  Slot-Test, FREE-Text im generischen Host-Editor, MIDI-Routing,
  Suspend/Wake): nicht umgesetzt. 6 betrifft unveränderten Code ohne Befund;
  7 gehört zur DAW-Abnahme des Nutzers.
* **Host-Refresh des Tune-Texts beim FREE-Umschalten:** unverändert, kein
  Befund; im DAW-Test zu beobachten.

## Folgeanforderungen (nicht Teil dieser Umsetzung)

Die drei im Bericht festgehaltenen Anforderungen sind noch nicht begonnen:
wählbare Polyphonie 4/8/16/32, Plugin-Nap in Studio One 7 und das Schloss im
Kreis statt FREE. Sie folgen getrennt, nach Freigabe durch den Nutzer.

## Geprüft nach der Umsetzung

VS 2026 Build Tools (MSVC 19.51), eigener Build-Ordner aus einem separaten
Worktree des Branches. Ergebnis:

* Release: Build von Torus Classic, Validator 47 bestanden und 0
  fehlgeschlagen; CTest `torus-classic.engine`, `.adapter`, `.plugin`,
  `.provenance`: 4 von 4.
* Debug: dasselbe, Validator 47/0, CTest 4 von 4.
* Benchmark (Release, i7-8700K): 0,9 bis 3,1 % eines Kerns je nach Modell
  und Rate. Das liegt im Rahmen von 0.2.0 (0,8 bis 2,7 %) und der
  Streuung zwischen Läufen.

Nicht erneut ausgeführt: Toy Piano (unverändert), VS 2022,
AddressSanitizer. GUI-, DAW- und Hörtests macht der Nutzer.

## Nachprüfung

Bitte TC-R05 bis TC-R09 erneut prüfen, besonders das neue Release-Verfahren
der Bildschirmtasten (TC-R08). Dessen Annahme: genau ein Thread ruft
`notify`, wie schon bei der Queue.
