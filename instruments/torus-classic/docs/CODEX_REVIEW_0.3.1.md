# Codex Review: Kekulé 0.3.1

**VERDICT: FREIGABE — für den beauftragten Codeumfang.**

Datum: 2026-11-16. Geprüfter Produktstand:
`27ea200a84d54bb467d02bb4582fb2e0265fa6a6`, Branch `kekule-0.3.0`.
Grundlagen: [Prüfauftrag 0.3.1](CODEX_PRUEFAUFTRAG_0.3.1.md),
[Review 0.3.0](CODEX_REVIEW_0.3.0.md) und
[Umsetzungsbericht](CODEX_REVIEW_0.3.0_UMSETZUNG.md).

TC-R11 bis TC-R13 sind durch die vollständige Entfernung der Ruhe-Meldung
gegenstandslos. TC-R14 ist entsprechend der Nutzerentscheidung dokumentiert;
das bekannte Übergangsverhalten bleibt erhalten. **Keine neuen konkreten
Befunde ab TC-R15.** Release und Debug bestehen die ausgeführten fensterlosen
Prüfungen. Diese Freigabe umfasst keine Fenster-, DAW- oder Hörabnahme.

## Prüfstand und Unverändertheit

Der Worktree `.work/kekule-0.3.0/wt` stand bei Beginn auf `d3f5ed8`.
`git diff 27ea200 HEAD --name-status` enthält ausschließlich den neuen
Prüfauftrag. Der gebaute Produktcode entspricht damit dem angeforderten
Commit; kein Branchwechsel oder Checkout war erforderlich.

Geprüft wurden die Änderungen seit `490eca8`, die Nachbesserung gegenüber
dem dokumentierten 0.3.0-Review und die zwei Ergänzungen Oktavtasten/Farben.
Vorhandene Quelltexte, Tests und Dokumente wurden nicht geändert. Ein
SHA256-Snapshot erfasste vor Beginn alle 179 vorhandenen versionierten bzw.
nicht ignorierten Dateien. Der Abgleich nach den Prüfungen ergab für alle
179 Dateien identische Bytes. Hinzu kommt ausschließlich dieser Bericht.

Fingerprint des Snapshots, SHA256 der nach Pfaden sortierten JSON-Abbildung
`Pfad → SHA256`, UTF-8, `ensure_ascii=False`, ohne Zwischenraum:

```text
96452f3e725a9a7172f1e9b9ad13d1f2da0818fa19b99588a3105f9ead177b76
```

Alle nachfolgenden Produktpfade beziehen sich auf
`instruments/torus-classic/` dieses Worktrees.

## Nachprüfung TC-R11 bis TC-R14

| ID | Status | Nachweis und Einordnung |
| --- | --- | --- |
| TC-R11 (P2) | **gegenstandslos** | `source/processor.h:54` liefert immer `kInfiniteTail`. Die dynamische Tail-Meldung entfällt. Eigene Laufzeitprobe bestätigt den Wert nach Initialisierung, Aktivierung, Anschlag, bei stillem Audio mit ausstehendem MIDI-Off und bei einem Flush ohne Audiosamples. |
| TC-R12 (P2) | **gegenstandslos** | Keine `AtRest()`-Methode, kein `at_rest_`, keine Tastenübergabezähler `keys_queued_`/`keys_taken_` und kein `PopKeyboardNote()` mehr in Engine, Adapter oder Prozessor. Damit existiert die beanstandete Veröffentlichung vorzeitig abgeschlossener Arbeit nicht mehr. Die reguläre Queue wird weiterhin direkt und begrenzt geleert (`processor.cpp:261`). |
| TC-R13 (P2) | **gegenstandslos** | Es gibt keine Meldung „nichts wartet“ mehr, die geschuldete MIDI-Offs übersehen könnte. Die bestehende `release_due_`-Behandlung bleibt erhalten. Eigene Probe: nach erfolgreichem N60 und Reaktivierung einen Audioblock ohne Ausgangsliste verarbeiten; Audio exakt still, `silenceFlags == 3`, Tail weiterhin unendlich; im folgenden aufnahmefähigen Block wird F60 zugestellt. |
| TC-R14 (P3) | **behoben durch Dokumentation** | README „Voices“, `engine/synth.h:12–19` und PLANUNG beschreiben die Ausnahme unmittelbar nach einer Erhöhung ausdrücklich. Eigene Wiederholung der Notenfolge bestätigt: 8 Stimmen, Noten 41–49, Erhöhung auf 16, Noten 50–60; anschließend ersetzt 61 die 49, während 45 erhalten bleibt. Dieses Verhalten ist bekannt und gewollt; der vendorte Stimmenverteiler wurde nicht verändert. |

Die Silence-Flags bleiben reine Aussagen über die ausgegebenen Audioblöcke:
`processor.cpp:318–342` prüft jedes Stereo-Sample auf exakte Null und setzt
die Flags nur dann. Der Code ist insoweit derselbe wie in `490eca8`.
Ein stiller Audioblock kann weiterhin ein ausstehendes MIDI-Off haben;
die neue Lösung veröffentlicht daraus keinen Ruhezustand und keinen Tail 0.
Was ein konkreter Host aus Silence-Flags macht, wurde nicht getestet.

## Neue Befunde ab TC-R15

Keine neuen P0-, P1-, P2- oder P3-Befunde im geprüften Umfang nachgewiesen.
Die unten eingeordneten Null-Vorzeichen sind kein neuer Unterschied
gegenüber 0.2.2 und kein wieder eingeführtes Ruhe-Schwellwertverfahren.

## Ohne Befund geprüft

### Entfernung der Ruhe und Firmware-Vergleich

* Alle sechs Dateien in `adapter/` haben zwischen `490eca8` und `27ea200`
  identische Git-Blobs. Der Adapter ist damit vollständig auf den
  0.2.2-Stand zurückgeführt, einschließlich Ausgangsstufe, Filterzuständen,
  Quantenpuffern, Zeitrechnung und Version 0.2.1.
* `core/` ist gegenüber `490eca8` unverändert. Bank 0 wird in
  `engine/synth.cpp:105` bei jedem Render-Aufruf verarbeitet. Ihr Signal
  wird nicht mehr anhand einer Ruhe-Schwelle durch Null ersetzt. Natürliche
  exakte Nullen vor der ersten Anregung oder durch Float-Unterlauf bleiben
  selbstverständlich möglich.
* `CompareWithFirmware` im Engine-Test zählt wieder jeden numerisch
  unterschiedlichen Samplewert; die bisherigen Ruhe-Ausnahmen fehlen.
  `TestAgainstFirmware` im Plugin verlangt wieder `different == 0`
  (`tests/test_plugin.cpp:411–413`). Insbesondere gibt es keinen Durchlass
  mehr für „Plugin-Sample 0, Firmwarebetrag kleiner 1e-9“.
* Die regulären Engine-Folgen vergleichen 1 712 712 Kanal-Samples. Der
  Pluginvergleich bestätigt in Release 5 760 000 und in Debug 864 000
  gleiche Kanal-Samplewerte bei beidseitig geflushten Denormalen. Der
  zusätzliche Vergleich gegen den ungeflushten Firmwaremodus meldet in
  Release maximal 1,26e-44 Abweichung. Das ist die bestehende explizite
  Denormalen-Einordnung, keine Ruhe-Toleranz im Gleichheitsvergleich.

**Zusätzliche längere Folge:** Modelle 0/2/3, je acht Sekunden, Damping 1
auf der Pluginseite, eine Note bei Sample 240 und die nächste nach sieben
Sekunden. Nach demselben Vorlauf wie im regulären Test stimmen alle
2 304 000 numerischen Kanal-Samplewerte ohne Schwellwertausnahme überein.
Der neue Anschlag nach der langen leisen Strecke bleibt eingeschlossen.

**Grenze des Wortes „bitgleich“:** In dieser zusätzlichen FM-Folge haben
387 Nullen ein anderes Vorzeichen als in der Firmware (`+0` gegenüber
`−0`), bei null numerisch unterschiedlichen Samples. Ein zunächst
ausgeführter `memcmp` erkennt diese IEEE-Bitdifferenz. Der vorhandene
Engine-Test protokolliert in seinen Standardfolgen ebenfalls Null-Vorzeichen
separat, ohne sie als numerische Abweichung zu zählen.

Zur Eingrenzung wurde die Vier-Stimmen-Engine aus `490eca8` ausschließlich
in temporäre Dateien exportiert, ihr Namensraum zur parallelen Verlinkung
umbenannt und dieselbe Folge gegen die aktuelle Engine gerechnet:

```text
BASELINE 490eca8 vs 27ea200:
2304000 channel samples bit-identical, including all signed zeros
```

Zusammen mit dem identischen Adapter und dem unveränderten
Vier-Stimmen-Prozessorpfad ordnet das die Null-Vorzeichen als bestehenden
Unterschied zum Firmwarepfad ein. Der wiederhergestellte Vergleich ist so
streng wie bis 0.2.2. Eine universelle Gleichheit sämtlicher IEEE-Bitmuster
einschließlich Null-Vorzeichen ist eine weitergehende Zusage, die die Tests
auch damals nicht verlangten. Keine reguläre Produkt-Assertion wurde für
diese Nachprüfung gelockert.

### Bankverhalten und Polyphonie

* `Quiet()` prüft beide unbearbeiteten Bankausgänge vor globaler Blende,
  Volume und Ausgangskennlinie auf Betrag kleiner 1e-9. NaN gilt dabei
  als nicht ruhig. `kQuietQuanta == 4000` entspricht bei 12 Samples und
  48 kHz einer Sekunde. Nur Zusatzbänke ab Index 1 durchlaufen diese
  Prüfung (`synth.cpp:106–116`); Bank 0 läuft ständig.
* Ein Anschlag setzt die getroffene Bank auf `On` und ihren Quiet-Zähler
  auf 0. `ResetBank()` rekonstruiert Part, Seed, Modell und Bankzustand wie
  zuvor. Ein nach Verkleinerung noch ausblendender Part wird weiter
  gerendert; wartende Anschläge auf ihn bleiben bis zu seiner Rückkehr
  in der Queue. Die 192-Sample-Blende und die Begrenzung auf höchstens
  vier Trigger je Bank und Quantum bleiben erhalten.
* Die regulären Polyphonieprüfungen bestehen: 4/8/16 Stimmen, fünfte Note,
  Erhöhen während des Klingens, Verkleinern mit anschließend samplegleichem
  Rest der ersten Bank, schnelles Ab-/Aufschalten innerhalb der Blende,
  Weglegen und erneute Anregung von Zusatzbänken, Bounds und Allokationen.
  Die kurze zweite Bank wird in der ausgeführten Folge nach rund 1,07 s
  weggelegt. Ihr Rauschzustand wird korrekt auf den Bank-Seed zurückgesetzt.
* Keine neue Unterbrechung des Bankverhaltens durch das Entfernen der
  Ruhe-Meldung gefunden. Die dokumentierte TC-R14-Ausnahme bleibt Teil
  der Freigabe; eine exakte globale Altersverwaltung wurde nicht ergänzt.

### Oktavtasten und gespeicherte Fensterdaten — nur Codeprüfung

* `view_layout.h` definiert den Startversatz −12 und die Grenze ±24.
  `ClampKeyboardTranspose` begrenzt auf ganze Oktaven. Zusammen mit den
  Basistasten 60–84 ergibt das ausschließlich MIDI 36–108, innerhalb des
  zulässigen Bereichs 0–127 für Controller und Prozessor.
* `KeyboardView::pressed_` enthält bereits die transponierte Tonhöhe.
  `SetTranspose()` ruft vor dem Ändern des Versatzes `Release()` auf
  (`widgets.cpp:66–74`). `Release()` sendet genau `pressed_`, setzt es
  auf −1 und beendet das Dragging. Ein späteres Mouse-Up kann damit keine
  Freigabe der neuen statt der alten Tonhöhe erzeugen. Eine unveränderte
  Transposition löst keine zusätzliche Freigabe aus.
* `Editor::SetKeyboardTranspose()` delegiert vor dem Speichern des neuen
  Versatzes an die Tastatur. `ApplySettings()` benutzt denselben
  Tastaturpfad beim Laden eines Projekts (`editor.cpp:326–337, 393–407`).
  Beim Schließen wird `ReleaseHeld()` vor `ForgetViews()` und vor dem
  Entfernen des Frames aufgerufen (`:432–434`). Das schützt die alte
  Tonhöhe auch bei einer geladenen anderen Lage und beim Schließen.
* Die Oktav-Callbacks verändern den Versatz um ±12. `Style::Action`
  wechselt den internen Toggle-Zustand nicht. `LayOut()` behandelt
  Sichtbarkeit und Mausfreigabe beider Oktavtasten; am jeweiligen Endpunkt
  ist die entsprechende Richtung deaktiviert. `ForgetViews()` entfernt
  beide neuen View-Zeiger. Kein neuer Lebensdauerfehler am Code gefunden.
* `Controller::setState()` liest und validiert alle Felder vor der
  Übernahme (`controller.cpp:364–387`). Fensterzustand V2 hat 17 Bytes;
  V1 hat 13 und erhält den neuen Default −12. V2 erlaubt ausschließlich
  −24/−12/0/12/24; falsche Version, fehlende Werte, ungültige Lage, nicht
  endlicher Zoom und ungültiges Keyboard-Flag werden abgelehnt. Endliche
  Zoomwerte außerhalb des Bereichs werden wie bisher geklemmt.
* Neben `TestController` wurden alle 17 verkürzten V2-Präfixe und acht
  ungültige Transpositionen einschließlich Int32-Grenzen geprüft:
  25 Ablehnungen ohne Änderung des gespeicherten Zustands. Alle fünf
  gültigen Lagen bestehen den Roundtrip; V1 lädt wieder mit −12. Dafür
  wurde kein Fenster erzeugt oder geöffnet.

Die Freigabepfade der Oktavtasten wurden am Code geprüft. Es wurden keine
Mausereignisse an Widgets simuliert und kein Editorfenster geöffnet.

### Farben, Versionen und Dokumentation

* Die drei RGB-Konstanten stimmen mit den entsprechenden Konstanten in
  Mendelejew im Haupt-Checkout überein: Weiß `(226,228,234)`, Magenta
  `(222,78,162)`, Türkis `(52,196,196)`. Die Zuordnung zu Pitch/Output,
  Resonator und Anregung bleibt wie dokumentiert. Der Haupt-Checkout wurde
  für diesen Vergleich nur gelesen.
* Produkt und Shell 0.3.1, Engine 0.2.1 und Adapter 0.2.1 stimmen zwischen
  Headern, CMake und `components.json` überein. Klangzustand V3 und dessen
  Parameter-/Migrationsregeln bleiben erhalten; der neue Fensterzustand V2
  ist davon getrennt. Das erzeugte Release-Bundle heißt `Kekule.vst3`;
  `moduleinfo.json` enthält Version 0.3.1.
* Provenance besteht in beiden Konfigurationen: 73 Dateien passen zu
  Manifest und Hashliste, davon 57 unverändert und 16 gepatcht, adaptiert
  oder abgeleitet. Keine erneute externe Herkunfts-/Lizenzprüfung nötig
  oder durchgeführt; der vendorte Kern ist gegenüber der Basis unverändert.
* README, aktueller CHANGELOG-Eintrag, UPSTREAM, PLANUNG und Abschnitt 9
  der DAW-Liste beschreiben den Verzicht auf eine eigene Nap-Meldung und
  die verbleibende interne Bank-Optimierung konsistent. Aussagen zur
  Ruhe-Meldung und zur damaligen Vergleichsausnahme unter dem ausdrücklich
  historischen, unveröffentlichten Stand 0.3.0 sind als Historie erkennbar.
  Die DAW-Liste verspricht keinen Nap-Erfolg, sondern verlangt eine
  Beobachtung ohne abgeschnittenen Ausklang. Keine aktuelle Ruhe-Ausnahme
  als weiterhin gültige Eigenschaft gefunden.

## Ausgeführte Builds und Tests

Visual Studio 2026 Build Tools, MSVC 19.51.36256.0, Windows x64.
Eigener Bereich ausschließlich im Worktree:
`.work/codex-review-0.3.1/`. Das SDK wurde über `VST3_SDK_ROOT` aus
`external/vst3sdk` im Haupt-Checkout eingebunden und nur gelesen. Build und
Testprogramme liefen mit automatisch genehmigter Ausführung außerhalb der
Sandbox. Keine installierte Plugin-Kopie wurde verwendet oder geändert.

| Prüfung | Release | Debug |
| --- | --- | --- |
| Kekulé-Build einschließlich Validator | PASS, 47 bestanden / 0 Fehler | PASS, 47 / 0 |
| `torus-classic.engine` | PASS, 4,88 s | PASS, 16,87 s |
| `torus-classic.adapter` | PASS, 45,18 s | PASS, 71,61 s |
| `torus-classic.provenance` | PASS, 0,19 s | PASS, 0,17 s |
| Eigener Runner mit 14 unveränderten fensterlosen Plugin-Funktionen | PASS | PASS |
| Zusätzliche Tail-, Fensterdaten-, Langzeitvergleichs- und TC-R14-Proben | PASS im oben beschriebenen Umfang | nicht wiederholt |
| Bitvergleich mit der Engine aus `490eca8` | 2 304 000 Kanal-Samples identisch | nicht wiederholt |

Der eigene Runner ist eine temporäre Kopie von `tests/test_plugin.cpp`.
Aus seinem `main()` wurden ausschließlich die drei Aufrufe
`TestViewLayout`, `TestEditorResize`, `TestEditorInWindow` entfernt. Die
übrigen 14 Funktionen blieben unverändert: `TestAgainstFirmware`,
`TestBlockSizes`, `TestNoteHandling`, `TestSampleRates`, `TestInstances`,
`TestParameterState`, `TestKeyboardQueue`, `TestKeyboardMidiOut`,
`TestPauseAndFlush`, `TestActivationAndRateChange`, `TestState`,
`TestPolyphony`, `TestController`, `TestTuneRounding`.

Der reguläre Plugin-CTest wurde nicht gestartet, weil er Fensterprüfungen
enthält. Die ausgeführten normalen CTest-Ziele waren deshalb gezielt
`^torus-classic\.(engine|adapter|provenance)$`; den Pluginumfang deckt der
separate Runner ab. Ein pauschales „CTest 4/4“ würde den ausgeführten
Umfang hier unzutreffend darstellen.

Die regulären Builds melden drei Warnungen aus unverändertem VSTGUI-Code;
keine Warnung im geprüften Produktcode. Der separate Baseline-Prüfer sieht
zusätzlich die im Produktbuild unterdrückten Upstream-Konvertierungswarnungen.

## Fehlende Tests und nicht Geprüftes

* Keine Fenster-, Widgetbedienungs-, DAW- oder Hörtests; insbesondere keine
  praktische Prüfung der Oktavtasten, Maus-Capture, Tooltips, Farben oder
  Skalierung. Die entsprechenden DAW-Prüfschritte bleiben beim Nutzer.
* Keine Prüfung tatsächlicher Studio-One-Nap-/Wake- oder Exportstrategien.
  Ein unendlicher Tail ist kein Beweis, dass ein Host niemals suspendiert.
* Kein VS-2022-, ASan-, TSan-, ARM- oder anderer Plattformlauf. Kein neuer
  harter Echtzeit-/Worst-Case-Zeitnachweis.
* Die regulären Bank-, Modell- und Blockgrößentests sowie die genannten
  eigenen Folgen sind endliche Szenarien. Keine erschöpfende Prüfung aller
  Bankhistorien, beliebiger Auslöschungen oder sämtlicher Parameterfolgen.
* Die Nutzerentscheidung zu TC-R14 akzeptiert die dokumentierte
  Altersabweichung. Ihre exakte Beseitigung und ein entsprechender neuer
  globaler Altersbeweis sind nicht Teil dieser Freigabe.

## Abschluss

Keine Produktkorrektur, kein Commit, Push, Branchwechsel oder Stash. Der
Haupt-Checkout, `.work/elements-review/`, vorhandene `build/`, `out/`,
`dist/` und installierte Plugins blieben unverändert. Der eigene temporäre
Prüfbereich einschließlich Builddateien, Quellenkopien und Protokollen wurde
nach Übernahme der wesentlichen Nachweise in diesen Bericht entfernt.
Im Worktree bleibt nur die gewünschte neue Ergebnisdatei.
