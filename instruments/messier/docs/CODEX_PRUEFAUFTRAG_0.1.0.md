# Prüfauftrag für Codex: Messier 0.1.0

Erste unabhängige Prüfung von Messier, einem neuen VST3-Instrument im Repository
`vst3_Plugins`: der Granular Looper aus Daisy-Seed `instruments/granular` (Nebulae on
Daisy 0.2.2) als Plugin, nur mit der File-Quelle. Geschrieben von Claude nach dem
Buildauftrag des Nutzers vom 2026-11-17. Du bist das zweite Augenpaar; DAW-, Fenster-
und Hörtests macht der Nutzer selbst.

## Was du prüfst

* Repository `vst3_Plugins`, Branch `messier-0.1.0`, abgezweigt von `main` bei
  `27a00b3` (Merge von Kekulé 0.3.1).
* **Geprüfter Stand: die uncommittete Arbeitskopie im Worktree**
  `.work/messier/wt` des Haupt-Checkouts
  `D:\Eigene Dateien\Eigene Dokumente\Playground\vst3_Plugins`. Der Nutzer committet erst
  auf Zuruf; deshalb gibt es noch keinen Commit. Alles Neue zeigt
  `git -C .work/messier/wt status` und `git -C .work/messier/wt diff 27a00b3`: das
  Verzeichnis `instruments/messier/` (neu), dazu kleine Einträge in `CMakeLists.txt`,
  `README.md`, `THIRD_PARTY_NOTICES.md`, `instruments/README.md`, `docs/PLANUNG.md`.
  Falls der Nutzer vor deiner Prüfung committet, prüfe diesen Commit und nenne ihn.
* Der Buildauftrag selbst liegt ungetrackt im Haupt-Checkout unter
  `docs/CLAUDE_BUILDAUFTRAG_MESSIER_0.1.0.md`; lies ihn, er ist die Vorgabe.
* Der Kern stammt aus Daisy-Seed `8226a31cf48a25236970dc55b96b4046b24ddffa`
  (Nachbarrepository `..\Daisy-Seed`; sein Checkout steht auf einem anderen Branch,
  also `git -C ..\Daisy-Seed show 8226a31:<Pfad>`). Vergleiche die Kopien dort.
* Einstieg: [README](../README.md), [UPSTREAM.md](../UPSTREAM.md),
  [Build-Bericht](BUILD_REPORT_0.1.0.md), `docs/PLANUNG.md` Abschnitt „Messier“.

## Prüfpunkte

1. **Herkunft und Kern.** Stimmen `core/` und `tests/reference/` mit `8226a31` überein,
   bis auf die drei in UPSTREAM.md genannten Patches (P1 `SetFileBuffer`, P2 gekürztes
   `wav.h`, P3 `GRAIN_AMP 0.90`) und in `tests/reference/` nur P3? Ist P1 klangneutral
   und wird er nur im erlaubten Moment benutzt (`engine/looper_engine.cpp`, `Render`
   und `SwapFile`)? Gibt es im Kern veränderliche globale oder `static`-Zustände, die
   zwei Instanzen teilen?
2. **Der Referenzvergleich.** Beweist `messier.engine` (`TestAgainstFirmware`) und
   `messier.plugin` (`TestAgainstFirmware`) wirklich, was er behauptet? Ist der Treiber
   `tests/reference_api.cpp` unabhängig genug vom Plugin-Code, oder bildet er dessen
   Fehler nach? Werden alle Regler, Zweitfunktionen, Noten, Bend, Freeze, Reset, mono,
   stereo, 44,1 und 48 kHz und ein Dateiwechsel tatsächlich erreicht? Dass beide Seiten
   dasselbe FFT-Backend nutzen, ist gewollt (es gibt nur eins); ist der Ausgleich dafür,
   der unveränderte Vocodertest der Firmware (`tests/core/test_vocoder.cpp`), ausreichend?
3. **FFT-Backend** `core/fft_desktop.cpp`: Rechnet es die Csound-Packung und die
   Skalierung der Firmware? Ist es threadsicher (statische Tabellen, erste Benutzung)?
4. **Echtzeit.** Kein Allozieren, Freigeben, Sperren oder Dateizugriff in `process()`
   auf irgendeinem Weg: Neustart durch Projektladen (placement new, `Engine::Init`),
   Dateitausch, Rückgabe alter Dateien (`FileExchange`, `backlog_`), Klaviatur,
   Parameter. Was passiert, wenn die Rückgabe-Warteschlange voll ist?
5. **Besitz der Dateien.** Kann eine Datei freigegeben werden, solange die Engine sie
   liest? Kann eine verloren gehen (Leck) oder doppelt freigegeben werden: bei
   `setActive`, bei `setState` während des Spielens, bei schnell folgenden Ladeanforderungen,
   im Destruktor des Processors, bei einem Tausch während eines Tauschs?
6. **Threads.** `setState` liest die Datei im aufrufenden Thread; der Ladethread liest
   Anforderungen aus dem Fenster; `notify` und die Statusantworten laufen im
   UI-Thread. Gibt es Datenrennen zwischen `FileLoader`, `FileExchange`,
   `ParameterState` und dem Audio-Thread? Gewinnt immer die neueste Anforderung, auch
   gegen ein gleichzeitig geladenes Projekt?
7. **Zustand.** Version 1 mit Pfad: wird alles geprüft, bevor etwas gilt? Lehnen Processor
   und Controller dieselben beschädigten Zustände ab? Was geschieht bei fehlender Datei,
   bei leerem Pfad, bei ungültigem UTF-8?
8. **WAV-Leser** `source/wav_reader.cpp`: Überläufe, riesige Chunk-Größen,
   abgeschnittene Dateien, `WAVE_FORMAT_EXTENSIBLE`, ungerade Chunks, Rundung von 24 Bit
   und Float, Größengrenze vor dem Allozieren.
9. **Parameter und MIDI.** IDs, Vorgaben, CC 20–27 und 33, ein Bend für alle Kanäle,
   Reset als Taster (Flanke), Freeze, Volume hinter dem Kern (bei 100 % exakt). Zeigen
   Anzeige und Texteingabe (`source/value_display.h`) den Wert, den der Kern spielt?
10. **Bus und Zeit.** Sidechain-Bus (aux, inaktiv, stereo/mono/keiner) in
    `setBusArrangements`; gemeldete Latenz; Ergebnis unabhängig von der Blockgröße.
11. **Lizenz.** Bleibt der LGPL-Teil auf `core/reference_dsp.*` begrenzt, mit seinem
    Kopf? Stimmen `THIRD_PARTY_NOTICES.md`, `licenses/` (der Qu-Bit-Text byteweise, samt
    Leerzeichen am Zeilenende) und das, was `tools/stage_result.ps1` ins Bundle legt
    (`BUILD_INFO.txt`, Lizenztexte), mit dem, was der Buildauftrag in Abschnitt 3 verlangt?
    Eine Rechtsberatung erwartet niemand; Abweichungen von der dort beschriebenen Regelung
    bitte benennen.
12. **Fenster, nur im Code.** Lebensdauer des Dateidialogs (`Editor::ChooseFile`, `alive_`),
    Statusabfrage im Timer, Freeze- und Reset-Knöpfe. Keine Fenstertests.

## Bekannt und gewollt

* Grain-Pegel 0,90 (F42): Entscheidung des Nutzers; bis zu 6 dB lauter als die Firmware.
* Keine Normalisierung (F58), Vocoder bei 44,1-kHz-Dateien richtig gestimmt (F44),
  Displacement sättigt den Grain-Vorrat (F41): wie der Kern.
* Vocoder auf dem gemeinsamen Takt (`kScheduleCommon`), nicht der versetzte der Firmware.
* Kein Clip hinter der Ratenwandlung (gemessen bis 1,03). Kein Plugin-Nap.
* Bei 192 kHz mit 64er-Blöcken dauert ein Block mit Vocoder-Hop länger als die
  Blockdauer (Build-Bericht, Abschnitt Last). Kein Fix in 0.1.0.
* Der Bend wirkt nur bei gehaltener Note, wie in der Firmware.
* Datei über 48 kHz ohne Tiefpass; Mehr-als-16-Bit ohne Dither.
* Nichts nach `common/`; Kopien aus Kekulé und Mendelejew sind Absicht.

## Bauen und testen

Der Worktree hat kein SDK; nimm das des Haupt-Checkouts und einen eigenen
Build-Ordner, z. B. `.work/messier-review/build`:

```text
cmake -S .work/messier/wt -B .work/messier-review/build -G "Visual Studio 18 2026" -A x64 -DVST3_SDK_ROOT="<Haupt-Checkout>/external/vst3sdk"
cmake --build .work/messier-review/build --config Release --parallel 4 --target Messier test_messier_vocoder test_messier_engine test_messier_adapter test_messier_loader test_messier_plugin
ctest --test-dir .work/messier-review/build -C Release -R messier --output-on-failure
```

Meine Läufe stehen im Build-Bericht (Abschnitt Build-Matrix); die Logs liegen unter
`out/messier-0.1.0/logs/` im Haupt-Checkout.

## Regeln

* Nur Code, Builds und Tests ohne Fenster. Keine Fenster-, DAW- oder Hörtests.
* Ändere nichts außer deiner Ergebnisdatei
  `instruments/messier/docs/CODEX_REVIEW_0.1.0.md` im Worktree `.work/messier/wt`.
  Kein Commit, kein Push, kein Branchwechsel, kein Stash, weder im Worktree noch im
  Haupt-Checkout. Daisy-Seed und den L2A-Fall nur lesen; aus dem L2A-Fall nichts ins
  Repository übernehmen.
* Den Haupt-Checkout und fremde `.work/`-Ordner nicht verändern. Eigene Build-Ordner
  danach entfernen.

## Ergebnis

Urteil (FREIGABE oder KEINE FREIGABE für den DAW-Test), dann Befunde ab **MS-R01** mit
Schwere P0 bis P3, Beleg (Datei:Zeile), Auslösung, bewiesen oder vermutet und minimaler
Korrektur; dann ohne Befund Geprüftes, fehlende Tests, nicht Geprüftes.
