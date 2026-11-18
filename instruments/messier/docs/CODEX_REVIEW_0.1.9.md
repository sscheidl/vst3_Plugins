# Kurze Nachprüfung Messier 0.1.9 · 18. November 2026

**KEINE FREIGABE für den DAW-Testbuild.** Automatisiertes STOP und Bypass können bis zum Anfang des aktuellen Host-Blocks vorgezogen werden. Die Ausgabe hängt dadurch von der Blockgröße ab. Außerdem können Bildschirmtasten während STOP weiterhin MIDI ausgeben und beim Fortsetzen die Messier-Wiedergabe transponieren.

Geprüft wurde der uncommittete Stand `0.1.9` im Worktree `.work/messier/wt` über `46c3998`. Der Schwerpunkt ist der Funktionszuwachs seit 0.1.8. Die frühere Nachrichtenprüfung steht in [CODEX_REVIEW_0.1.8.md](CODEX_REVIEW_0.1.8.md). Pfade unten sind relativ zu `instruments/messier/`.

### MS-R35 [P2] STOP und Bypass ignorieren den Sampleoffset der Automation

**Im eigenen Release-Zusatztest bewiesen.** `ReadParameterChanges` verarbeitet alle Punkte vor dem Rendern. `QueueParameter` schreibt bei STOP und Bypass unmittelbar die Schalterwerte und ignoriert deren Offset. `Render` verwendet anschließend den Endstand des Blocks (`source/processor.cpp:339`, `:345`, `:480`, `:569`). Ein Ein- und Ausschalten desselben Schalters innerhalb eines Blocks kann dadurch vollständig verschwinden.

Bei einem Einschaltpunkt an Sample 512 renderte der Test einmal 1024 Samples am Stück und einmal zweimal 512 Samples. Bei **beiden** Schaltern war im großen Block der Bereich 256 bis 511 schon still. Im geteilten Lauf lag sein RMS bei etwa **0,156**. Der vorhandene Regressionstest sendet ausschließlich an Offset null (`tests/test_plugin.cpp:974`). Die Schaltpunkte beim Rendern an ihren Offsets anwenden und die Blende über diese Teilabschnitte fortführen. Dieselbe Automationsfolge mit unterschiedlichen Blockteilungen prüfen.

### MS-R36 [P2] Das Bildschirmkeyboard wird während STOP anders behandelt als Host-MIDI

**Im eigenen Release-Zusatztest bewiesen.** `ReadEvents` verwirft Host-Note-Ons im vollständig ausgeblendeten Ruhezustand. `PlayKeyboardNote` plant Bildschirm-Note-Ons dennoch im stillstehenden Adapter ein und reicht sie gleichzeitig an den Host weiter (`source/processor.cpp:430`, `:468`). Im Zusatztest sendete das Plugin im Ruhezustand tatsächlich MIDI-Note-On für Taste 72. Nach dem Fortsetzen spielte die Engine eine Oktave höher. Damit bleibt ein während der Pause ausgelöster Anschlag hörbar vorgemerkt.

Den Ruhezustand auch für Bildschirm-Note-Ons anwenden und Note-Offs für bereits klingende Noten weiterhin verarbeiten. Das speicherbare Verhalten von Bypass und der Neustart ohne STOP hängen davon unabhängig an der Zustandsversion.

### MS-R37 [P2] Ein Drop kann vor seiner Ausnahmegrenze Speicher anfordern

**Im Code belegt, ohne Fehler-Injektion.** Nach einem normalen Drag-Enter kopiert `onDrop` den gespeicherten `DropVerdict` samt Pfad und Begründung. Die Kopie steht vor dem `try`, das nur die Callback-Ausführung umfasst (`source/file_drop.cpp:110`). Scheitert die Allokation, kann die Ausnahme in die VSTGUI-Zustellung gelangen. Den gespeicherten Verdict verschieben oder den gesamten Drop-Pfad absichern. Einen weiterhin als Hover markierten Zustand auch im Fehlerfall zurücksetzen.

### Weitere Prüfergebnisse

* Eigener VS-2026-Release-Bau. Alle **6 von 6 Messier-Tests** und **47 von 47 Validator-Prüfungen** bestanden. Der Firmwarevergleich meldete **0 von 960.000** abweichende Samples. Die Messier-Tests zählten keine Allokationen in `process`. STOP und Bypass blenden bei Punkten am Blockanfang aus, halten den DSP im Ruhezustand an und blenden wieder ein.
* Zustandsversion 1 liest dieselben 16 Werte und setzt Bypass auf aus. In Version 2 hängt der gemeinsame Parser einen validierten Bypass-Wert an. Processor und Controller verwenden ihn beide. STOP wird nicht gespeichert und beim Laden zurückgesetzt. Die Tests für beschädigte Zustände und die Rückwärtskompatibilität bestanden (`source/state_io.h:82`, `tests/test_plugin.cpp:1005`).
* Die WAV-Vorprüfung verwendet die Struktur- und Formatprüfung des Readers. Sie kann Ausfälle beim späteren Lesen der PCM-Daten, Speichermangel oder eine zwischenzeitliche Dateiänderung nicht ausschließen. Sie läuft einmal pro Drag-Enter und kann mehrere Kandidaten prüfen. Lokale WAVs mit normalem Header sind ein kurzer Vorgang. Langsame Netzwerkpfade oder viele zu überspringende Chunks können den UI-Thread blockieren. Ein Zeitbudget wird nicht erzwungen.
* Die zusätzliche schnelle Statusabfrage hat einen Zähler von rund zwölf Sekunden. Ein alter Status mit demselben Pfad kann sie früher beenden, aber die regelmäßige Abfrage alle 16 Ticks bleibt aktiv. Steht der letzte empfangene Status dauerhaft auf „loading“ oder wurde noch keiner empfangen, fragt das offene Fenster weiterhin bei jedem Tick nach. Das ist begrenzte Arbeit pro Tick, keine blockierende Schleife (`source/controller.cpp:449`, `:523`).
* Der Befund zur Freigabe gleich hoher Host-MIDI-Noten aus der 0.1.8-Prüfung besteht auch im eigenen 0.1.9-Bau. Beim abschließenden Paketabgleich enthielt `out/messier-0.1.9/` nur Logs. Eine Paket- oder Quell-Hash-Liste lag noch nicht vor.

Geprüft wurde ohne Fenster, DAW und Hören. Native Drag-and-drop-Pfade, Mixer-Routing in Studio One und die UI-Latenz bei langsamen Dateien sind offen.
