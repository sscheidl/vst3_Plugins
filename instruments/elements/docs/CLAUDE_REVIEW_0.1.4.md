# Prüfung: Mendelejew 0.1.4

Datum: 2026-11-15. Umsetzung: Codex. Prüfer: Claude.
Grundlage: [Prüfauftrag](CLAUDE_PRUEFAUFTRAG_0.1.4.md) und
[Untersuchung hoher Töne](DSP_INVESTIGATION_0.1.4.md).

**Ergebnis: HOLD.** Es gibt keinen Fehler, der einen Ton hängen lässt oder
einen Zustand beschädigt. Ein Befund betrifft aber eine Entscheidung, die
der Nutzer treffen sollte, bevor 0.1.4 committet wird (CL-R01, P2): Damping
ist nur im Fenster umgedreht, im Host läuft der Parameter weiter in die alte
Richtung, und Kekulé macht es anders. Daneben drei kleinere Punkte (P3).

## Geprüfter Stand

Codex' Änderungen lagen nicht committet im gemeinsamen Arbeitsbaum. Damit
während der Prüfung nichts nachrutscht, habe ich sie unverändert als
Schnappschuss eingefroren (`git stash create`, Commit `e4d69ec`, nur lokal,
kein Branch, kein Push) und in `.work/mendelejew-0.1.4-review/wt` mit eigenem
Build-Verzeichnis gebaut. Grundlage ist `3634b5f` (0.1.3). Die zwei
unversionierten Dokumente (`CLAUDE_PRUEFAUFTRAG_0.1.4.md`,
`DSP_INVESTIGATION_0.1.4.md`) sind nicht im Schnappschuss, für den Build
aber auch nicht nötig. Am Arbeitsbaum selbst habe ich nichts geändert,
außer diesen Bericht anzulegen.

Der Diff ändert 18 Dateien. Nicht geändert sind `core/`, `engine/`,
`adapter/`, `processor.*`, `state_io.h` und die Parameter- und Klassen-IDs.
Das passt zu Punkt 6 des Auftrags.

## Befunde

### CL-R01 (P2): Damping zeigt im Fenster und im Host in verschiedene Richtungen

Fundstellen: `source/controller.cpp:214` (LinearParameter mit `zero` 1,
`scale` −1), `source/parameters.h:83` (`KnobValue`), `source/editor.cpp`
(`valueChanged`, `ParameterChanged`, Default).

Die Umsetzung ist in sich richtig gerechnet: angezeigter Wert = 1 −
normalisiert, Text, Eingabe, Knopf, Default-Reset (0,75 angezeigt = 0,25
gespeichert) und Mausrad passen zusammen. Alte Klänge klingen gleich.

Die Richtung des Parameters, wie der Host ihn sieht, bleibt aber die alte:

* In einer Automationsspur heißt „nach oben" weiter: weniger Dämpfung. Die
  Beschriftung der Spur kommt aus `toString` und zählt dabei rückwärts
  (oben 0,000, unten 1,000).
* MIDI CC 23 dreht gegen den Knopf: ein höherer CC-Wert dämpft weniger,
  während der Knopf dabei nach links läuft.
* Hosts mit generischer Oberfläche (Schieberegler aus der Parameterliste)
  zeigen einen Regler, dessen Wert beim Hochziehen sinkt.

Die Entscheidung des Nutzers war „Damping höher = stärkere Dämpfung". Im
Fenster stimmt das, im Host nicht.

Dazu kommt: **Kekulé hat dieselbe Umkehr in 0.2.0 anders gelöst**, über den
Parameter selbst. Dort ist der normalisierte Wert schon im neuen Sinn, die
Engine bekommt `1 − x` (`torus-classic/source/parameters.h:128`), und ein
alter Zustand der Version 1 wird beim Laden gespiegelt
(`torus-classic/source/state_io.h:34`). Damit laufen Fenster, Spur, CC und
generische Oberfläche in dieselbe Richtung. Die beiden Schwester-Plugins
würden sich bei Damping im Host also verschieden verhalten.

Was man mit Kekulés Weg aufgibt: Automation, die mit 0.1.0 bis 0.1.3
aufgezeichnet wurde, liefe danach umgekehrt. Mendelejew ist bisher nur
heute einmal in der DAW gespielt worden; viel aufgezeichnete Automation
dürfte es nicht geben. Gespeicherte Klänge blieben mit der Migration
erhalten.

Empfehlung: Kekulés Weg übernehmen. Das hieße Klang-Zustand Version 2 mit
gespiegeltem Damping beim Lesen von Version 1, `1 − x` an der Stelle, wo die
Engine Damping bekommt, normalisierter Default 0,75, und `KnobValue` sowie
die Sonderbehandlung im LinearParameter entfallen. Das ist eine Entscheidung
des Nutzers; ich habe nichts davon umgesetzt.

### CL-R02 (P3): POLY ist schon mit dem GEN-Panel verdrahtet

Fundstelle: `source/editor.cpp`, Schleife über `panel_names`:
`const int panel = i < 2 ? i : 2;` gibt für POLY (`i == 2`) und GEN
(`i == 3`) beide `2`. Der Callback von POLY ruft also
`SetPanel(EditorPanel::Gen)`.

Heute ohne Folgen, weil POLY mit `setMouseEnabled(false)` keine Klicks
bekommt und auch nicht in `panel_buttons_` landet. Wer POLY später
einschaltet, bekommt aber zuerst einen Panelwechsel nach GEN statt eines
Stimmenwechsels. Vorschlag: POLY ohne Callback anlegen oder mit einem
eigenen, der vorerst nichts tut, und den Index nur für ENG/MOD/GEN
ausrechnen.

### CL-R03 (P3): Alte Projekte öffnen die Klaviatur eine Oktave tiefer

Fundstelle: `source/controller.cpp`, `setState`: Bei Version 1 bleibt
`transpose` auf `kKeyboardDefaultTranspose` (−12). Ein mit 0.1.3 oder
früher gespeichertes Projekt zeigte C4–C6 und zeigt nach dem Laden C3–C5.

Das ist vertretbar, weil der Nutzer den tieferen Start wollte, und der Test
erwartet es so (`test_plugin.cpp`, `CHECK(... transpose == -12)`). Es ist
aber eine Entscheidung, die nirgends als solche dokumentiert ist. Entweder
im README nennen oder für Version 1 `0` setzen. Klang und MIDI ändert es
nicht, nur welche Noten die Bildschirm-Klaviatur spielt.

### CL-R04 (P3): Der Tooltip von POLY erscheint nicht

Fundstelle: `source/editor.cpp`, POLY: `setTooltipText(...)` und
`setMouseEnabled(false)`. VSTGUI sucht den View unter der Maus für Tooltips
nur unter den Views, die Mausereignisse annehmen
(`vstgui4/vstgui/lib/cframe.cpp:400`, `GetViewOptions().mouseEnabled()`).
Der Hinweis „Planned: cycle 1 / 2 / 4 / 8 voices" ist damit nie zu sehen,
ebenso der Tooltip von `<<` bzw. `>>`, solange die Taste am Rand gesperrt
ist. Aus dem Code gelesen, nicht am laufenden Fenster geprüft. Vorschlag:
POLY aktiv lassen, aber im Callback nichts tun und nur grau zeichnen.

## Ohne Befund geprüft

* **Oktaven:** `ClampKeyboardTranspose` rundet auf ganze Oktaven in
  −24..+24. Die Klaviatur spielt damit 36..108, also immer gültige Noten.
  `SendKey` und der Prozessor prüfen weiterhin 0..127, nicht den alten
  Bereich 60..84. `NoteAtPoint`, Hervorhebung und C-Beschriftung rechnen
  die Verschiebung gleich ein. `SetTranspose` lässt zuerst die alte Note
  los, auch mitten im Ziehen (`Release` setzt `dragging_` zurück). Die
  Tasten `<<`/`>>` sind am Rand gesperrt; `SetKeyboardTranspose` klemmt
  zusätzlich. Der Oktavbereich betrifft nur die Bildschirm-Klaviatur.
* **Ausblenden und Schließen:** `LayOut` lässt eine gehaltene Taste los,
  wenn KEYS die Klaviatur ausblendet; `close()` wie bisher.
* **Panels:** genau eines sichtbar; ein Klick auf das aktive stellt es
  wieder her. Unsichtbare Views bekommen in VSTGUI keine Maus- und
  Radereignisse. Beim Wechsel weg von ENG beendet `ShowPanel` offene
  Gesten. Ein danach weiterlaufender Mausdrag schickt keine Werte mehr:
  `CKnobBase::onMouseMoved` prüft `isEditing()`, und `CControl::endEdit`
  ist gegen doppeltes Beenden geschützt (auch der Radtimer). Das gilt auch
  für `ApplySettings` mitten in einem Edit.
* **Fenster-Zustand v2:** schreibt Zoom, Klaviatur, Verschiebung, Panel;
  liest Version 1 weiter; lehnt unbekannte Versionen, Werte außerhalb,
  Nicht-Oktaven und abgeschnittene Daten ab, bevor etwas übernommen wird.
  Der Klang-Zustand bleibt Version 1. Ein mit 0.1.4 gespeichertes Projekt
  verliert in 0.1.3 nur die Fenstereinstellungen.
* **Schneeflocke:** Kreis 20 × 20 rechts neben SPACE in der Namenszeile;
  sein unterer Rand liegt 5 px über dem Rand des Reglerrings. Ein- und
  Ausschalten geht über `SetFreeze` wie zuvor, also als Edit mit Geste.
* **Kopfzeile:** fünf gleich breite Tasten, Abstände gehen auf (5 × 52 +
  4 × 6); Platz für Firmenname und Version bleibt.
* **Untersuchung hoher Töne:** Die Schlüsse sind vorsichtig formuliert und
  halten. Der Sinustest zeigt richtig, dass die dritte Harmonische von
  10 kHz (30 kHz) bei 48 kHz auf 18 kHz fällt und bei 192 kHz nicht; das
  beweist den Mechanismus in der Ausgangskennlinie, nicht die Ursache im
  Patch des Nutzers. Zur Abweichung von 6,7 bis 7,3 dB zwischen 48 und
  96 kHz bei MIDI 96 mit Bow/Blow: Eine naheliegende Ursache ist, dass bei
  96 kHz mehr Moden unter Nyquist bleiben und die gestrichenen Moden nur
  über die Bandmitte umgerechnet werden (siehe 0.1.1). Das ist eine
  Vermutung, nicht gemessen; sie passt zum bekannten HOLD für 96 kHz.

## Builds und Tests

Eigener Build des Schnappschusses `e4d69ec`, VS 2026, Windows x64, in
`.work/mendelejew-0.1.4-review/build`, SDK aus dem Haupt-Checkout.

| Prüfung | Release | Debug |
| --- | --- | --- |
| Build | Exit 0 | Exit 0 |
| Validator, alle drei Plugins | je 47 bestanden, 0 fehlgeschlagen | je 47 bestanden, 0 fehlgeschlagen |
| Compilerwarnungen | 3, alle aus VSTGUI (`ctexteditor.cpp`, `cshadowviewcontainer.cpp`), wie in früheren frischen Builds | ebenso |
| CTest | 10 von 10, darunter `elements.plugin` mit `TestEditorControls` | 10 von 10 |

Messbericht: Alle 101 Tabellenzeilen von
[DSP_MEASUREMENTS_0.1.1.md](DSP_MEASUREMENTS_0.1.1.md) kommen unverändert
wieder; dazu kommen die neuen Zeilen. Alle 24 Zeilen der Tabelle in
[DSP_INVESTIGATION_0.1.4.md](DSP_INVESTIGATION_0.1.4.md) und die beiden
Werte des Sinustests (−47,05 und −182,84 dBFS) habe ich Zeichen für Zeichen
reproduziert. Die Tests sagen nur, dass das Geprüfte stimmt; die Befunde
oben stammen aus dem Lesen des Codes.

Die Build-Ordner und den Arbeitsbaum habe ich danach entfernt. Die Logs und
der Diff liegen in `.work/mendelejew-0.1.4-review/logs/` und
`diff-0.1.4.patch`.

## Nachtrag: Umsetzung nach der Entscheidung des Nutzers

Der Nutzer hat zu CL-R01 entschieden: „Ja, wie Kekulé". Codex' Stand ist
unverändert als Commit `6f2b090` auf dem Branch `elements-0.1.4`
festgehalten (identisch mit dem geprüften Schnappschuss `e4d69ec`). Darauf
setzt meine Änderung auf; sie ist damit **Code von Claude** und braucht
Codex' Prüfung (Auftrag am Ende).

* **CL-R01 umgesetzt.** `parameters.h`: `EngineValue(index, normalized)`
  gibt für Damping `1 − x`, sonst `x`; `kDefaults` hat Damping 0,75,
  `DefaultOf` liest jetzt `kDefaults` statt der Engine-Defaults;
  `KnobValue` entfällt. Klang-Zustand Version 2 (`kStateVersion`), Version 1
  wird gelesen und dabei gespiegelt (`state_io.h`, für Prozessor und
  Controller gemeinsam). Der Prozessor gibt Werte nur über `EngineValue` an
  die Engine (`QueueParameter`, `Restart`). Der LinearParameter von Damping
  ist wieder ein gewöhnlicher (0, 1); im Fenster entfällt jede
  Sonderbehandlung. Engine, Kern und Adapter bleiben unverändert.
* **CL-R02 behoben.** POLY bekommt keinen Callback mehr; der Panel-Index
  wird nur für ENG, MOD, GEN gebildet.
* **CL-R03 dokumentiert** (README, CHANGELOG): alte Projekte öffnen die
  Klaviatur bei C3–C5.
* **CL-R04 offen gelassen.** Ein sichtbarer Hinweis für POLY bräuchte eine
  Entscheidung, wie die Taste aussehen soll; das gehört zum späteren POLY.

Tests (`tests/test_plugin.cpp`): Default 0,75 und Engine-Wert 0,25; Text
und Eingabe ohne Spiegelung; der Damping-Knopf setzt den Parameter
unverändert; CC 23 ist Damping zugeordnet; ein Zustand der Version 1 mit
Damping 0,9 kommt als 0,1 zurück, wird als Version 2 gespeichert und gibt
der Engine wieder 0,9, ein Zustand der Version 2 mit 0,9 gibt ihr 0,1; der
Controller spiegelt Version 1 genauso; Version 0 und −1 werden abgelehnt.
Testpatches, die bisher den Hardware-Wert setzten, setzen jetzt den
gespiegelten (0,6 → 0,4, 0,8 → 0,2), damit sie gleich klingen. Der Zustand
aus Codex' Review von 0.1.0 (`TestOutputBound`) wird als Version 1 geladen
und klingt damit unverändert.

Der erste Testlauf scheiterte in `TestState` an der Tonhöhenmessung: Der
Test setzt jeden Parameter auf einen eigenen Wert, für Damping 0,655, und
die Engine bekam damit jetzt 0,345, einen zu kurzen Ton zum Messen. Der Test
setzt Damping jetzt gespiegelt (Engine wie zuvor 0,655). Dabei fiel auf:
Ein fehlschlagender `CHECK` endet seit 0.1.4 nicht mit Exitcode 1, sondern
mit einem Absturz (Release `0xc0000374`, Debug SegFault), weil `std::exit`
läuft, während VSTGUI noch initialisiert ist (`InitModule` ohne
`DeinitModule`). Der Test gilt trotzdem als fehlgeschlagen; die Meldung
`FAIL …` steht vorher im Log. Kein Produktfehler, aber für die Fehlersuche
irreführend; für eine spätere Fassung vorgemerkt.

Zweiter Lauf, gemeinsames Build-Verzeichnis, Stand `6f2b090` plus diese
Änderung: Release und Debug Exit 0, Validator für alle drei Plugins je
47 bestanden, 0 fehlgeschlagen, keine Warnung, CTest 10 von 10 in beiden.
Der Messbericht enthält alle 101 Zeilen von 0.1.1 und alle 24 Zeilen der
Untersuchung unverändert; die Umkehr ändert am DSP nichts.

Auf Wunsch des Nutzers installiert in
`C:\Program Files\Common Files\VST3\Mendelejew.vst3` (aus
`out/elements-0.1.4`, Hashes gleich, Version 0.1.4); das alte Bundle (0.1.2)
liegt im Papierkorb.

## Grenzen

Keine Prüfung im laufenden Fenster, keine DAW, kein Hören: Das prüft der
Nutzer (Prüfliste 3a, 9a–9c, 15, 16, 16a). Kein ASan/TSan. Den Patch, bei
dem der Nutzer die Verzerrung hörte, gab es nicht; die Untersuchung bleibt
damit offen, wie Codex schreibt.
