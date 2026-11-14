# Umsetzung des Codex-Reviews 0.1.0

* **Stand:** 2026-11-14
* **Bericht:** [CODEX_REVIEW_0.1.0.md](CODEX_REVIEW_0.1.0.md), Urteil „Keine
  Freigabe", geprüft auf `torus-classic-0.1.0` @ `d2e62ab`
* **Umgesetzt in:** Torus Classic 0.1.1
* **Umgesetzt von:** Claude (Fable 5.2)

Jeder Befund wurde zuerst am Code und am SDK-Quelltext nachvollzogen. Alle
vier sind bestätigt und behoben, jeweils mit einem Test, der den Fehler in
0.1.0 gefunden hätte.

| ID | Befund | Bestätigt | Umsetzung | Test |
| --- | --- | --- | --- | --- |
| TC-R01 | Ein geladener Zustand wird beim Neustart der Engine mit gleichzeitig geschriebenen Werten vermischt | ja: `Restart()` las neun Atomics einzeln, die ein zweites `setState` gerade überschrieb | Neu `source/parameter_state.h`. Jeder geladene Zustand bekommt eine Generationsnummer und geht vollständig an den Audio-Thread; die Engine startet allein aus diesem Schnappschuss. Automationswerte tragen die Generation, die der Audio-Thread übernommen hat. Der aktuelle Zustand (`getState`, Neustart bei Aktivierung) ist der zuletzt geladene plus die Automation derselben Generation; ältere Automation zählt nicht mehr. Der Audio-Thread sperrt und wartet dabei nicht | `TestState`: zwei Zustände im Wechsel aus einem zweiten Thread bei Ein-Sample-Blöcken, nach jedem Block alle sieben Regler aus einem Zustand (rund eine Million Ladevorgänge, 100 000 Blöcke, nie eine Mischung); `getState` liefert stets den eben geladenen Zustand. `TestParameterState`: die Reihenfolgen, die sich mit echten Threads nicht erzwingen lassen, Schritt für Schritt |
| TC-R02 | Jeder Regler meldet Beginn und Ende eines Edits doppelt | ja: `CControl::beginEdit` ruft den Listener und den Frame; beide Wege führten zum Controller | `Editor` überschreibt `controlBeginEdit`/`controlEndEdit` nicht mehr; es bleibt der Weg über den Frame (`VSTGUIEditor::beginEdit/endEdit`) | `TestEditorInWindow`: echter `CKnob` im Fenster, Folge genau „begin, perform, end", auch bei verschachteltem Edit |
| TC-R03 | Bei voller Adapter-Queue wirkt Automation vor ihrem Zeitpunkt | ja; der alte Test erwartete dieses Verhalten sogar | Noten und jeder Parameter haben eigene begrenzte Warteschlangen (1024 Noten; je 256 Punkte für jeden Regler, das Modell, den Pitch Bend). Volle Noten-Queue: Note wird verworfen und gezählt. Volle Parameter-Queue: der jüngste wartende Punkt weicht dem neuen, der seinen eigenen, späteren Zeitpunkt behält. Sofortiges Anwenden und Vorziehen gibt es nicht mehr. Punkte eines Parameters, die rückwärts in der Zeit kommen, wirken mit ihrem Vorgänger, nicht davor | `TestQueueLimits`: volle Noten-Queue plus Regler, nichts erreicht die Engine vor dem Quantum; 300 Punkte eines Reglers, jeder trifft genau sein Quantum oder entfällt; Vergleich mit blockweiser Zufuhr ohne Überlast |
| TC-R04 | Ein beim Schließen laufender Regler-Edit wird nicht beendet | ja: `removeAll()` und der Destruktor von `CControl` senden kein `endEdit` | `Editor::close()` beendet vor dem Abbau jeden noch laufenden Regler-Edit, auch den eines Mausrads in seiner Nachlaufzeit | `TestEditorInWindow`: begonnener Edit, dann `removed()` und, getrennt, Freigabe ohne `removed()`: jeweils genau ein Ende je Regler |

## Weitere Punkte aus dem Bericht

* **Aktivierung mit noch nicht übernommenem Zustand** (im Prüfauftrag als
  ungetestet genannt): Der Weg läuft jetzt über dieselbe Zustandsverwaltung;
  `TestParameterState` und ein neuer Abschnitt in `TestState` decken ihn ab.
* **Kommentar in `TestState`**, jeder Block werde aus einem vollständigen
  Wertesatz gerendert: war in 0.1.0 unzutreffend; jetzt prüft der neue Test
  genau das.
* **`UPSTREAM.md`** nennt den `std::once_flag` als einziges lokales `static`.
* **`docs/PLANUNG.md`**: „vollständig getestet" für den Resampler ersetzt
  durch die tatsächliche Reichweite der Messungen.
* **README**: Die Überlastregel steht jetzt unter „Timing"; „never early"
  gilt damit ohne Einschränkung.
* **Build-Bericht 0.1.0** bleibt als Dokument seines Stands unverändert; ein
  Hinweis am Anfang verweist auf den Review und diese Umsetzung.
* Nicht geändert: der eigene Resampler (Abweichung vom Wortlaut des
  Buildauftrags, vom Review als nachvollziehbar, aber nicht gebilligt
  eingestuft; Entscheidung des Nutzers offen) und das fehlende
  `restartComponent(kLatencyChanged)` (im Review als ungeprüft vermerkt, kein
  Befund).

## Geprüft nach der Umsetzung

VS 2026 Release (MSVC 19.51): Build beider Plugins, Validator je 47 bestanden
und 0 fehlgeschlagen, CTest 6 von 6. Debug, VS 2022 und AddressSanitizer
wurden für 0.1.1 nicht erneut ausgeführt; sie laufen über den nächsten Stand.

## Nachprüfung

Bitte TC-R01 bis TC-R04 erneut prüfen. Codex hat seinen Arbeitsordner
`.work/torus-classic-review/` nicht mehr aufräumen können; er liegt noch im
Repo-Verzeichnis und ist von Git ausgenommen.
