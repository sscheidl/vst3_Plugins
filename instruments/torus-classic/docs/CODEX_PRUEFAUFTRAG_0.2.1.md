# Prüfauftrag an Codex: Torus Classic 0.2.1 (Nachprüfung, nur Code)

* **Stand:** 2026-11-14
* **Gegenstand:** `instruments/torus-classic/` im Repo
  [sscheidl/vst3_Plugins](https://github.com/sscheidl/vst3_Plugins), Branch
  `torus-classic-0.1.0` (Pull Request 1). Ein Commit nach dem zuletzt
  geprüften `78e5b9a`: Version 0.2.1, die Korrekturen zu deinem Review 0.2.0.
  `git diff 78e5b9a HEAD` zeigt genau diese Runde.
* **Verfasser des Codes:** Claude
* **Grundlage:** dein Bericht [CODEX_REVIEW_0.2.0.md](CODEX_REVIEW_0.2.0.md),
  die [Umsetzung](CODEX_REVIEW_0.2.0_UMSETZUNG.md), das
  [Änderungsprotokoll](../CHANGELOG.md)
* **Ergebnis nach:** `instruments/torus-classic/docs/CODEX_REVIEW_0.2.1.md`

## Auftrag und Grenzen

Nachprüfung der fünf Befunde TC-R05 bis TC-R09: behoben, ohne neue Fehler?
Keine neuen Funktionen in dieser Runde; die drei Folgeanforderungen
(Polyphonie, Plugin-Nap, Schloss-Symbol) sind noch nicht begonnen und nicht
Gegenstand.

Es gelten die Regeln der bisherigen Prüfaufträge
([0.1.0](CODEX_PRUEFAUFTRAG_0.1.0.md), [0.2.0](CODEX_PRUEFAUFTRAG_0.2.0.md)):
nichts ändern außer der Ergebnisdatei, eigenes Build-Verzeichnis unter
`.work/torus-classic-review/` und am Ende löschen; `build/`, `dist/`, `out/`
und die installierten Kopien nicht anfassen; keine Fenster-, GUI-, DAW- oder
Hörtests; Bewiesenes von Vermutetem trennen. Fremde Änderungen im
Arbeitsverzeichnis (z. B. `instruments/elements/`, Wurzel-`CMakeLists.txt`)
nicht anfassen.

Unverändert seit `78e5b9a`: `core/`, `engine/`, Resampler, Controller,
Editor, Zustandsformat. Geändert: `adapter/output_stage.{h,cpp}`,
`source/parameters.h`, `source/engine_bridge.h`,
`source/processor.{h,cpp}`, Tests, Doku, Versionsnummern.

## Bauen und Testen

Wie bisher. Stand des Verfassers für 0.2.1: VS 2026 Release und Debug,
Validator je 47 bestanden und 0 fehlgeschlagen, CTest
`torus-classic.{engine,adapter,plugin,provenance}` je bestanden.
VS 2022 und AddressSanitizer wurden für 0.2.1 nicht ausgeführt.

## Prüfpunkte

| ID | Was geändert wurde | Bitte prüfen |
| --- | --- | --- |
| TC-R05 | Filter-Steuerwert und Glättungsfaktor in `double`; Snap bei Rest unter `kSnap` (1e-6) oder wenn ein Schritt den Wert nicht mehr ändert | Trifft die Glättung bei allen Raten (1 kHz bis 4 MHz) und allen Zielen exakt? Ist der Sprung beim Snap (Mix-Änderung höchstens 5e-5) vertretbar? Dein Sondenlauf von 0.2.0 gegen den neuen Stand |
| TC-R06 | Integratoren laufen immer, auch bei `mix_ == 0`; Ausgang bei Mix 0 exakt der (begrenzte) Eingang | Kommt in irgendeinem Ablauf noch alter Zustand zurück (Bypass, Reset, lange Inaktivität, Ratenwechsel)? Bleibt die Bit-Gleichheit bei offenem Filter? Stabilität mit Grenzfrequenz 20 kHz bzw. 0.45 × Rate im Bypass, auch bei 1 kHz und 4 MHz |
| TC-R07 | `TuneStep()` in `parameters.h` als einzige Rundung (Double); `TuneEvent()` trägt den Halbton in `EngineEvent::data` (Versatz 24) neben dem Float-Wert für freies Stimmen; `EngineBridge::JumpTune` nimmt `double` | Ein Weg, auf dem Anzeige, Texteingabe, Automation und geladener Zustand noch verschieden runden? Überlebt `data` das Zusammenlegen und Ersetzen in der Regler-Queue des Adapters? `ControlEvent(Control::Tune, …)` ohne Halbton ergäbe −24: ist der Hinweis im Kommentar genug, oder ist das eine Falle? |
| TC-R08 | Releases nicht mehr in der Queue: `notify` zählt angenommene Anschläge je Taste (`key_struck_`, nur UI-Thread) und speichert beim Loslassen diesen Stand atomar (`key_released_`); nie abgewiesen. `ReadKeyboard` sendet zuerst fällige Note-offs, dann die Anschläge; `sent_strike_` merkt, zu welchem Anschlag das Note-on des Hosts gehört. `SendKey` ändert `held_keys_` erst nach erfolgreichem `addEvent`; ein Note-off ohne Platz oder Liste kommt über `release_due_` im nächsten Block. Erneuter Anschlag einer gehaltenen Taste, deren Note-off keinen Platz findet: kein zweites Note-on | Ist die Paarung in allen Verschachtelungen von UI-Thread und Audio-Thread richtig: Tippen innerhalb eines Blocks, Loslassen und erneutes Anschlagen zwischen zwei Blöcken, Anschlag noch in der Queue beim Loslassen, verfallene oder vor einem geladenen Zustand liegende Anschläge, Deaktivierung, Host ohne Liste? Kann eine Note beim Host hängen bleiben oder ein Note-off ohne Note-on entstehen? Speicherordnungen (`key_released_` release/acquire, Queue). Annahme: genau ein Thread ruft `notify`, wie schon bei der Queue: haltbar? Bleibt die Arbeit je Block begrenzt? |
| TC-R09 | `OutputStage::Bounded`: NaN und ±Inf werden 0, endliche Werte über ±`kInputLimit` (1e6) werden begrenzt, vor Filter und Limiter | Bleiben Integratoren, Filterausgang und Limiter-Gain bei jedem Eingang endlich (auch bei schnell bewegter Grenzfrequenz)? Ist „nicht endlich wird Stille" als Vertrag sinnvoll und richtig dokumentiert? |

Zusätzlich:

* Die neuen Tests (`TestOutputStage` erweitert, `TestTuneRounding`,
  `TestKeyboardMidiOut` erweitert): Hätten sie die Fehler in 0.2.0 gefunden?
  Der Verfasser hat sie nur gegen 0.2.1 laufen lassen.
* Doku gegen Code: README (Damping-Satz in „Sound", „Level", „Screen
  keyboard"), `CHANGELOG.md` 0.2.1, `docs/CODEX_REVIEW_0.2.0_UMSETZUNG.md`.

## Bekannt und gewollt (nur melden, wenn die Beschreibung nicht stimmt)

* Alles aus den bisherigen Prüfaufträgen, soweit nicht oben ersetzt.
* Neue Anschläge dürfen bei voller Queue oder voller Ausgabeliste des Hosts
  entfallen; der Anschlag klingt dann trotzdem, nur das Note-on fehlt.
* Fehlende Tests 6 und 7 aus deinem Bericht sind nicht umgesetzt (siehe
  Umsetzung).
* Der Host-Refresh des Tune-Texts beim FREE-Umschalten ist unverändert.

## Format des Ergebnisses

```
# Codex Review: Torus Classic 0.2.1
VERDICT: FREIGABE FÜR DAW-TEST | FREIGABE MIT KORREKTUREN | KEINE FREIGABE
Geprüft: <Commit>, Datum, Compiler

## Nachprüfung TC-R05 bis TC-R09
| ID | Ergebnis (behoben / teilweise / nicht behoben) | Beleg | Rest |

## Neue Befunde
| ID | Schwere | Bereich | Befund | Beleg (Datei:Zeile) | Auslösung und Auswirkung | bewiesen/vermutet | minimale Korrektur |

## Ohne Befund geprüft

## Fehlende Tests

## Nicht geprüft
```

Schweregrade wie bisher. Neue Befunde bitte ab `TC-R10` zählen.
