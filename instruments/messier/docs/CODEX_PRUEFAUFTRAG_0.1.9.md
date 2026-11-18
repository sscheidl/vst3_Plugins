# Prüfauftrag für Codex: Messier 0.1.9, kurze Prüfung

Der Nutzer wünscht eine **kurze** Prüfung vor dem nächsten DAW-Testbuild. Bitte nicht die
ganze Klasse der verspäteten Nachrichten neu aufrollen (dafür liegt
[CODEX_PRUEFAUFTRAG_0.1.8.md](CODEX_PRUEFAUFTRAG_0.1.8.md) bereit). Hier geht es um das,
was seit dem installierten 0.1.8 neu ist und im DAW-Test auffallen könnte.

Stand: Worktree `.work/messier/wt`, Branch `messier-0.1.0`, **uncommittet** auf `46c3998`
(0.1.7, 0.1.8 und 0.1.9 zusammen). Was neu ist, steht im
[Build-Bericht 0.1.9](BUILD_REPORT_0.1.9.md), Tabelle am Anfang; das Bundle liegt
in `out/messier-0.1.9/`.

## Prüfpunkte, nach Gewicht

1. **STOP und Bypass** (`source/processor.cpp`: `Render`, `Restart`, `QueueParameter`,
   `ReadEvents`):
   * Blende ohne Klick; im Ruhezustand wird die Engine nicht gerechnet. Geht es danach
     richtig weiter, auch mit Parameteränderungen und Noten während der Pause?
   * Kann der Ruhezustand hängen bleiben, etwa nach Aktivierung, Projektladen oder
     Ratenwechsel?
   * Allokation oder Sperre im Audio-Thread?
2. **Zustand Version 2** (`source/state_io.h`, `parameters.h`): Version 1 wird noch
   gelesen (ohne Bypass), Version 2 mit Bypass. Stimmen Processor und Controller
   überein? Ist der Prüfweg für beschädigte Zustände vollständig?
3. **Prüfung beim Darüberziehen** (`ProbeWavFile`, `JudgeDrop`, `FileDropTarget`):
   * Lehnt sie dieselben Dateien ab wie das Laden?
   * Ausnahmegrenzen.
   * Datei-I/O im UI-Thread beim Eintritt des Ziehens: einmal je Ziehen, nur die
     Kopfteile. Vertretbar?
4. **Nachfragen nach einem Ladeauftrag** (`Controller::LoadFile`, Poll): Kann das
   Nachfragen etwas falsch anzeigen oder endlos laufen?
5. Nur wenn Zeit bleibt: die Punkte aus dem Prüfauftrag 0.1.8.

## Mein Stand

Matrix am 2026-11-18 auf genau diesem Stand, Logs in `out/messier-0.1.9/logs/`:

| Bau | ctest | Validator (alle vier Plugins) |
| --- | --- | --- |
| VS 2026 (MSVC 19.51) Release | 16/16 | 47/0 |
| VS 2026 Debug | 16/16 | 47/0 |
| VS 2022 (MSVC 19.44) Release | 16/16 | 47/0 |
| ASan (die fünf Messier-Testprogramme) | alle Exit 0, kein Bericht | – |

Firmware-Vergleich im Plugin: 0 von 960.000 Samples verschieden.

## Regeln

* Nur Code, Builds und Tests ohne Fenster. Keine Fenster-, DAW- oder Hörtests.
* Ändere nichts außer `instruments/messier/docs/CODEX_REVIEW_0.1.9.md` im Worktree. Kein
  Commit, kein Push, kein Branchwechsel, kein Stash. Eigene Build-Ordner danach entfernen.

## Ergebnis

Kurz: Urteil (FREIGABE oder KEINE FREIGABE für den DAW-Testbuild), Befunde ab **MS-R34**
mit Schwere, Beleg (Datei:Zeile), Auslösung und minimaler Korrektur; was nicht geprüft
wurde.
