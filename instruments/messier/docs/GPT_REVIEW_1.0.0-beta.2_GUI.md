# MESSIER 1.0.0-beta.2 — Konsolidierter GUI-Code-Review (A + B + C)

**Stand:** 21. November 2026  
**Repository:** `sscheidl/vst3_Plugins`  
**Prüfcommit:** `768cea1eb210fe6ad3708cb86b3c3230f6743fb5` (`claude/messier-beta2-117e35`)  
**Vergleich:** beta.1, Commit `a6823d6` (`messier-v1.0.0-beta.1`)  
**Methode:** Statische Read-only-Prüfung; keine Änderungen, Builds oder Laufzeittests durch GPT  
**Adressat:** Claude (Gegenprüfung und gegebenenfalls Implementierung), danach Codex (unabhängige Tests und Performance-Messung)

## Kurzüberblick

In drei getrennten Prüfungen wurden **20 Befunde bzw. Untersuchungskandidaten** erfasst: Teil A **8**, Teil B **7**, Teil C **5**. Die Einstufungen aus den drei Originalreviews werden beibehalten. **„Proven“ bedeutet Nachweis des beschriebenen Codepfads, nicht automatisch eines hör- oder sichtbaren Fehlers im Host.** Bei als „suspected“ bezeichneten Folgen sind reproduzierbare Tests erforderlich.

| Teil | Schwerpunkt | Befunde | Prioritätsschwerpunkte |
|---|---|---:|---|
| A | GALAXY/WAVE-Darstellung, Grain-Sterne, Rotation, Fischauge | 8 | 3 × P2, 1 × P3, 2 × PERF-high, 2 × PERF-medium |
| B | START/END, Knobs, Umschalter, Tastatur, Fenster | 7 | 2 × P2, 3 × P3, 2 × PERF-low |
| C | GUI-State v1–v9, Presets, Custom Skins, HUE, RMS | 5 | 3 × P3, 2 × PERF-medium |

### Scope und Abgrenzung

- **A:** `source/map_view.cpp`, `source/map_view.h`, `source/fisheye.h`.
- **B:** `source/widgets.cpp`, `source/widgets.h`, `source/editor.cpp`, `source/editor.h`, `source/gui_layout.h`.
- **C:** `source/gui_skin.cpp`, `source/gui_skin.h`, `source/controller.cpp` (GUI-Settings in `getState` / `setState`), `source/parameters.h` (State-Version), `source/wave_overview.cpp`, `source/wave_overview.h`.
- **Ausgeschlossen:** Änderungen am Audio-Core, Processor, MIDI, VST-Shell, File-Loader sowie allgemeines Refactoring außerhalb der angegebenen Dateien. Externe Hilfsfunktionen, deren Definition außerhalb des Scopes liegt, wurden nicht als vollständig geprüft ausgegeben.

---

# A. Glass rendering — GALAXY / WAVE

## B2A.1 · P2 · Sterne können beim Wiedereinsetzen der Bewegung aus falschem Radius starten

**Evidenz — `MapView::DrawStarfield` / `MapView::StarStart`:**

```cpp
const double tempo = drift_speed_ / kDriftPerTempo;
```

```cpp
if(tempo > 0.05)
    return 0.04; // near the centre, flying out
if(tempo < -0.05)
    return 0.97; // at the rim, flying in
return 0.15 + 0.75 * StarHash(number, 2); // standing anywhere
```

**Auslöser:** Nach dem Starten bzw. Wiederanlaufen liegt die geglättete aktuelle Bewegung zunächst nahe null, obwohl vorwärts oder rückwärts abgespielt wird. Für in dieser Phase neu angelegte Sterne wird deshalb ein zufälliger Anfangsradius gewählt.

**Status:** Codepfad nachgewiesen; tatsächliche optische Auffälligkeit ohne Laufzeittest nicht bewiesen.

**Korrekturidee:** Für die Star-Initialisierung die intendierte Bewegungsrichtung von der geglätteten Momentangeschwindigkeit trennen bzw. vorübergehende Nullgeschwindigkeit bewusst behandeln.

## B2A.2 · P2 · Sternpositionen bleiben beim Sample-Wechsel erhalten

**Evidenz — `MapView::ShowAt` / `MapView::StarOf`:**

```cpp
if(display.sequence != display_.sequence || display.frames != display_.frames) {
    ended_count_ = 0;
}
```

```cpp
for(StarState& star : stars_)
    if(star.used && star.number == number) {
        // existing star reused
```

**Auslöser:** Neues Sample, dessen Grain-IDs mit IDs des vorigen Samples übereinstimmen. Nur der Afterglow-Puffer wird gelöscht; der persistente Cache `stars_` behält seine alten Positionen.

**Status:** Fehlende Cache-Invalidierung nachgewiesen; sichtbarer Effekt von der Wiederverwendung der IDs abhängig.

**Korrekturidee:** Beim Wechsel von `sequence` oder `frames` auch die Star-State-Zuordnung und den Ring-Index zurücksetzen.

## B2A.3 · P2 · GALAXY-Nachglühen wird nach verzögerter Anzeige zu früh verworfen

**Evidenz — `MapView::ShowAt`:**

```cpp
if(!(left <= audio) || audio - left >= kTrail)
    continue;
```

Während der Stern selbst laut `StarLook` nach seiner formgebenden Lebensdauer noch `kStarGlow` nachglühen darf (im geprüften Stand etwa **1,2 s**), entscheidet der WAVETrail-Zeitraum `kTrail` (etwa **0,45 s**) darüber, ob er überhaupt in die Sammlung beendeter Grains aufgenommen wird.

**Auslöser:** Ein Grain endet zwischen zwei Displays; die nächste Meldung trifft erst nach mehr als `kTrail`, aber vor Ablauf von `kStarGlow` ein.

**Status:** Unterschiedliche Abschneidebedingungen nachgewiesen; optische Auswirkung ohne Host-Test nicht gemessen.

**Korrekturidee:** Für die GALAXY-Afterglows den längeren Sternzeitraum verwenden oder den konservierten Zustand unabhängig von der WAVE-Trail-Dauer behandeln.

## B2A.4 · PERF-high · Wiederholte Vollbitmap-Rotation auf dem GUI-Thread

**Evidenz — `MapView::TurnedNow`:**

```cpp
if(!layer.drawn || moved >= step) {
    PixelBuffer turned;
    TurnPixels(layer.pixels, angle, turned);
    if(auto bitmap = MakeBitmap(turned)) {
        layer.drawn = bitmap;
        layer.angle = angle;
    }
}
```

**Auslöser:** Animierte Nebel- und Staubschichten. Bei hinreichender Winkeländerung werden vollständige Pixelbuffer erneut rotiert und neue Bitmaps erzeugt. Die ursprüngliche Prüfung schätzte je nach Bewegung mehrere Dutzend solcher Operationen pro Sekunde über die kombinierten Schichten; es gibt **keine Messdaten** zur tatsächlichen Dauer.

**Status:** Rechen- und Allokationspfad nachgewiesen, Performance-Bewertung unter Vorbehalt.

**Korrekturidee:** Vor einem Umbau CPU-Zeit pro Rotation und Anzahl der Neubildungen je Sekunde messen; anschließend GPU-Transformation oder eine günstigere Caching-Strategie prüfen.

## B2A.5 · PERF-high · Periodische Invalidierung der ganzen Ansicht auch ohne aktive Animation

**Evidenz — `MapView::Tick`:**

```cpp
if(Clock::now() - drawn_at_ >= kFrameInterval)
    Guarded([&] { invalid(); });
```

**Auslöser:** Timer/Tick läuft weiter, auch wenn die Ansicht visuell stillsteht oder gerade kein Sample aktiv ist. `kFrameInterval` beträgt ungefähr **25 ms**.

**Status:** Unbedingte Zeitprüfung nachgewiesen; tatsächliche Repaint-Kosten hostabhängig.

**Korrekturidee:** Den Tick an eine echte Animations-/Änderungsbedingung koppeln oder die Invalidation im Stillstand drosseln.

## B2A.6 · PERF-medium · Pfad- und Gradientenerzeugung pro sichtbarem Grain

**Evidenz — `DrawRays` und `MapView::DrawStarfield`:**

```cpp
auto path = owned(context->createGraphicsPath());
auto gradient = owned(CGradient::create(
    0.0, 1.0, WithAlpha(ink, glow), WithAlpha(ink, 0.0)));
```

Vier Strahlen erzeugen jeweils Pfad und Gradient. Dazu kommen zwei radiale Glows, ebenfalls mit Pfad und Gradient. In Summe sind das bis zu **sechs Paare** pro gezeichnetem Stern und Frame; in WAVE werden verwandte Zeichenroutinen genutzt.

**Auslöser:** Viele aktive Grains bei fortlaufendem Neuzeichnen.

**Status:** Wiederholte Objekterzeugung nachgewiesen; Kosten nicht gemessen.

**Korrekturidee:** Nach Profiling die Wiederverwendung von Geometrien bzw. günstigere Zeichenprimitiven prüfen.

## B2A.7 · PERF-medium · Farbige GALAXY-Bitmap bevorzugt 2× selbst bei 1×-Ansicht

**Evidenz — `MapView::DrawGalaxy` / Auswahl der kolorierten Quelle:**

```cpp
CBitmap* source = turned_nebula_.x2
    ? turned_nebula_.x2.get() : turned_nebula_.x1.get();
```

**Auslöser:** Eine eingefärbte Galaxy-Bitmap liegt in beiden Auflösungen vor, die Fensterdarstellung erfolgt aber mit niedriger Skalierung.

**Status:** 2×-Präferenz im betreffenden Pfad nachgewiesen; möglicher Mehraufwand abhängig von zuvor erzeugten Bitmap-Varianten.

**Korrekturidee:** Quellenwahl an die benötigte Device-Skalierung koppeln und unnötige 2×-Pixelarbeit vermeiden.

## B2A.8 · P3 · Fehlgeschlagene Pixel-Leseoperation kann Cache dauerhaft blockieren

**Evidenz — `MapView::TurnedNow`:**

```cpp
if(layer.source != source) {
    layer.source = source;
    layer.drawn = nullptr;
    layer.angle = 1e9;
    if(!ReadPixels(source, layer.pixels))
        return nullptr;
}
```

Ein ähnliches Muster existiert bei `DrawDustLayer`, wo `layer.skin` vor einer erfolgreichen `ReadPixels`-Operation gesetzt wird.

**Auslöser:** `ReadPixels` schlägt transient fehl. Beim nächsten Aufruf gelten `source` beziehungsweise `skin` bereits als unverändert; ein regulärer erneuter Leseversuch bleibt aus.

**Status:** Nachgewiesener Recovery-Pfad-Mangel; Auftreten von Readback-Fehlern unbekannt.

**Korrekturidee:** Cache-Schlüssel erst nach erfolgreicher Erstellung übernehmen oder bei fehlenden Daten einen erneuten Versuch zulassen.

### A — Prüfungen ohne Befund

- Die statische Vorwärts- und Rückwärtsabbildung des Fischauges wird für den normierten Bereich konsistent verwendet.
- Winkel werden um 360° normalisiert.
- Es bestehen bereits mehrere Caches, die unveränderte Skins und Quellbilder nicht unnötig neu behandeln.
- Die Animation erfolgt überwiegend zeitbasiert; `dt` wird an einzelnen Stellen begrenzt, weshalb große Pausen bewusst geglättet werden.

---

# B. Controls and window

## B2B.1 · P2 · Beim Laden von Projekteinstellungen kann eine gehaltene Taste nach KEYS-Ausblendung verbleiben

**Evidenz — `Editor::ApplySettings`:**

```cpp
settings_.keyboard = settings.keyboard;
settings_.details = settings.details;
settings_.transpose = transpose;
if(keyboard_view_)
    keyboard_view_->SetTranspose(transpose);
```

Anders als `Editor::SetKeyboardVisible` ruft dieser Pfad bei `keyboard = false` kein `keyboard_view_->ReleaseHeld()` auf.

**Auslöser:** Projekt stellt unsichtbare Tastatur wieder her, während noch eine Note gehalten wird, insbesondere bei LATCH und unverändertem Transpose.

**Status:** Fehlender Release-Aufruf nachgewiesen; hörbare hängende Note hängt von übergeordnetem Controller-Reset ab. **Bereits in beta.1 vorhanden.**

**Korrekturidee:** Gehaltene Taste freigeben, bevor Projekteinstellungen eine sichtbare Tastatur verbergen.

## B2B.2 · P2 · Projektwiederherstellung kann während asynchroner Host-Größenänderung altes Layout behalten

**Evidenz — `Editor::ApplySettings`:**

```cpp
if(frame && !Resize())
    LayOut(rect.getWidth(), rect.getHeight());
```

`Editor::ShowSections` besitzt dagegen zusätzlich einen Layout-Pfad für Fälle, in denen der Host die Größenänderung akzeptiert hat, `onSize` aber noch aussteht.

**Auslöser:** Asynchron angenommener Resize beim Laden eines Projekts mit geänderten KEYS-, DETAILS- oder Zoom-Einstellungen.

**Status:** Unterschiedliche Codepfade nachgewiesen; sichtbare Fehler hostabhängig. **Bereits in beta.1 vorhanden.**

**Korrekturidee:** Während der Wartezeit auf `onSize` das neue Content-Layout in der alten Fenstergröße darstellen, wie in `ShowSections`.

## B2B.3 · P3 · PITCH-Rasterung könnte feine Reglerbewegungen stören

**Evidenz — `SnappedByKnob` und `Editor::valueChanged`:**

```cpp
const double octaves = std::round(PitchOctaves(normalized) * 24.0) / 24.0;
return PitchNormalized(octaves);
```

```cpp
if(snapped != normalized) {
    normalized = snapped;
    control->setValueNormalized(static_cast<float>(snapped));
}
```

**Auslöser:** Sehr langsames Ziehen oder Feinjustieren in der Nähe einer 50-Cent-Grenze.

**Status:** **Verdacht**, nicht nachgewiesener Interaktionsfehler; hängt vom Drag-Akkumulator in VSTGUI `CKnob` ab. **Neu in beta.2.**

**Korrekturidee:** Nur bei reproduzierbarem Problem den kontinuierlichen Drag-Zwischenwert vom gerasterten Hostwert trennen; ansonsten keine Änderung.

## B2B.4 · P3 · Oktav-Tipps beschreiben noch die frühere Tastaturbreite

**Evidenz — `Editor::OpenUnchecked`:**

```cpp
octave_down_->setTooltipText("Keyboard down one octave (lowest: C2 to C4)");
octave_up_->setTooltipText("Keyboard up one octave (highest: C6 to C8)");
```

**Auslöser:** Maus über einer der Oktavtasten. Die neue beta.2-Tastatur ist als **13 Pads / eine Oktave** spezifiziert.

**Status:** Nachgewiesene Text-/Spezifikationsabweichung.

**Korrekturidee:** Bereiche der Tooltips aus den aktuellen Ein-Oktav-Grenzen generieren.

## B2B.5 · P3 · START/END kann bei Exception ein Edit ohne erfolgreiches Begin beenden

**Evidenz — `StartBar::PressAt`:**

```cpp
dragging_ = true;
dragging_end_ = end;
if(end) {
    if(edits_.size_begin)
        edits_.size_begin();
    edits_.size(SizeAt(x));
}
```

Der Catch-Block setzt `dragging_` nicht zurück; `Release` kann daraufhin `size_end` beziehungsweise `end` auslösen, auch wenn das Begin vorher geworfen hat.

**Auslöser:** Exception in einem Begin-Callback.

**Status:** Fehleranfälliger Ausnahme-Kontrollfluss nachgewiesen; Auftreten bei regulären Callbacks nicht belegt.

**Korrekturidee:** Erfolgreichen Edit-Start separat verfolgen und nur dann beenden.

## B2B.6 · PERF-low · START/END versendet identische Werte bei jedem Maus-Move erneut

**Evidenz — `StartBar::DragTo`:**

```cpp
if(dragging_end_)
    edits_.size(SizeAt(x));
else
    edits_.start(Share(x));
```

**Auslöser:** Gleichbleibende Mausposition oder Bewegung außerhalb des Tracks, während die Werte auf 0 bzw. 1 geklemmt bleiben.

**Status:** Redundante Callback-Möglichkeit nachgewiesen; tatsächlicher Hostaufwand ungemessen.

**Korrekturidee:** Unveränderte normalisierte Werte während des Drags nicht erneut senden; Begin-/End-Paar erhalten.

## B2B.7 · PERF-low · Parametereinheiten werden bei jeder Wertanzeige linear gesucht

**Evidenz — `ParamText`:**

```cpp
const int32 count = controller->getParameterCount();
for(int32 i = 0; i < count; ++i) {
    if(controller->getParameterInfo(i, info) == kResultOk && info.id == id) {
```

**Auslöser:** Häufige Wertewechsel durch Reglerbewegung und Automation.

**Status:** Wiederholter Metadatenzugriff nachgewiesen; Kosten ungemessen. **Bereits in beta.1 vorhanden.**

**Korrekturidee:** Unveränderliche Einheiten pro Parameter-ID zwischenspeichern; gleiche Display-Texte nicht mehrfach invalidieren.

### B — Prüfungen ohne Befund

| Prüffeld | Ergebnis / Begründung |
|---|---|
| START/END-Grenzen | `Share` und `SizeAt` klemmen auf gültige Bereiche; `EndX` begrenzt das Dateiende bei endlichen Werten. |
| Crossing der Griffe | `LoopFrames` erzwingt mindestens einen Frame; END wird aus START und SIZE berechnet. |
| Host-Edit bei Normalbedienung | Separate Begin-/End-Callbacks für START und LOOP END vorhanden. |
| GALAXY/WAVE-Hit-Test | `ViewSwitch::SideAt` und `hitTest` lassen den zentralen BLEND-Knob frei. |
| Host-Automation von PITCH | Externe Werte werden in `ParameterChanged` ohne Rasterung übernommen; `SnappedByKnob` ist nur im GUI-`valueChanged`-Pfad aktiv. |
| Reguläres Keyboard-Release | Maus-Up, Transpose, manuelles KEYS-Ausblenden und Editor-Close haben Release-Pfade. |
| Zoom | Begrenzung für Benutzerzoom 50–200 %; Fit in kleineren Hostfenstern vorgesehen. |
| DETAILS | Sekundäre Controls werden verborgen und verlieren gegebenenfalls den Fokus. |

**Scope-Grenze:** Exakte Notengrenzen und das 13-Pad-Hit-Testing hängen von `view_layout.h` ab; die Panel-Beschriftungen zusätzlich von `panel_view.*`. Diese Dateien wurden im Review B nicht weiter geprüft.

---

# C. Presets, HUE, skins, controller state and RMS

## B2C.1 · P3 · Alte State-Versionen akzeptieren Preset-IDs, die es damals nicht gab

**Evidenz — `Controller::setState`:**

```cpp
!IsGuiPreset(preset)
```

`IsGuiPreset` akzeptiert seit beta.2 die IDs 0–7; State-Versionen vor Version 9 kannten nur 0–3.

**Auslöser:** Beschädigter beziehungsweise manipuliert gespeicherter Legacy-State mit ID 4–7.

**Status:** Validierungslücke nachgewiesen; **keine Beeinträchtigung gültiger beta.1-Projekte**.

**Korrekturidee:** Bei State-Versionen 1–8 Legacy-Grenze verwenden, bei v9 die neue Grenze.

## B2C.2 · P3 · Custom-Skin-Anleitung erzeugt falschen Dateinamen für bemalte Galaxy-Bitmaps

**Evidenz — `SkinFolderGuide`:**

```cpp
(spec.per_variant ? "nebula_<nebula>_" : "") + spec.stem + "@1x.png"
```

Für `SkinImageId::kNebula` ist `spec.stem` leer. So entsteht ein überflüssiger Unterstrich unmittelbar vor `@1x`. `SkinImageFileName` hängt den Unterstrich dagegen nur bei nicht leerem `stem` an:

```cpp
if(*spec.stem)
    name += '_';
```

Die Anleitung nennt zudem nur die vier älteren Nebula-Variantenschlüssel, nicht alle acht Presets.

**Auslöser:** Benutzer erstellt Custom-Galaxy-Bitmaps anhand der generierten Anleitung.

**Status:** Dateinamenabweichung nachgewiesen; anschließend erwarteter Fallback nicht laufzeitgetestet.

**Korrekturidee:** Anleitung über `SkinImageFileName` generieren und alle acht Variantenschlüssel dokumentieren.

## B2C.3 · P3 · Legacy-Farbtabellen von Slate beeinflussen alle neuen Galaxy-Presets

**Evidenz — `Skin::Skin`:**

```cpp
for(const std::string& table :
    {std::string("messier_slate_colours.txt"), ColourTableFileName(preset_)})
```

Die alte Slate-Farbtabelle wird in jedem neuen Preset angewendet, auch bei Blue, Orange, Red und Violet Galaxy. Darin enthaltene Werte für `nebula_a`/`nebula_b` überschreiben jeweils die spezifische Preset-Farbe.

**Auslöser:** Bereits installierter Custom-Skin aus beta.1.

**Status:** Effektive Verarbeitungsreihenfolge nachgewiesen; ob globale Alt-Overrides beabsichtigt sind, ist eine Produktentscheidung.

**Korrekturidee:** Alt-Tabelle gegebenenfalls auf den ursprünglichen Slate-Nachfolger begrenzen, ansonsten ausdrücklich dokumentieren.

## B2C.4 · PERF-medium · Skin-Erzeugung lädt Bitmap-Ressourcen pro Instanz und Preset erneut

**Evidenz — `Skin::Skin`:**

```cpp
for(std::size_t i = 0; i < kSkinImageCount; ++i) {
    const auto id = static_cast<SkinImageId>(i);
    SkinImage& image = images_[i];
    load(image, SkinImageFileName(preset_, id, 1),
         SkinImageFileName(preset_, id, 2), SkinImageSize(id));
}
```

**Auslöser:** Öffnen mehrerer Fenster, Presetwechsel oder Skin-Reload. Auch gemeinsam genutzte Ressourcen werden je Skin-Objekt geprüft beziehungsweise geladen.

**Status:** Wiederholter Ladepfad nachgewiesen; tatsächliche Verzögerung unbekannt.

**Korrekturidee:** Nur bei messbarem Bedarf gemeinsamen, unveränderlichen Bitmap-Cache mit expliziter Invalidierung beim Reload erwägen.

## B2C.5 · PERF-medium · Vollbild-HUE-Umrechnung beim Bewegen des Farbreglers

**Evidenz — `HueTurnedBitmap`:**

```cpp
do {
    from->getColor(c);
    if(c.alpha > 0) {
        double h = 0, s = 0, l = 0;
        c.toHSL(h, s, l);
        // ...
        c.fromHSL(std::fmod(h + degrees + 720.0, 360.0), s, l);
    }
    to->setColor(c);
} while(++(*from) && ++(*to));
```

**Auslöser:** Aufeinanderfolgende HUE-Werte bei bemalter Galaxy. Eine Bitmap von 720 × 720 Pixeln benötigt pro Umrechnung bis zu 518.400 Pixeloperationen.

**Status:** Vollständiger Pixel-Durchlauf nachgewiesen; keine Aussage über tatsächliches Ruckeln möglich. **Nicht automatisch pro 25-ms-Frame**, sondern bei neuer erforderlicher Bitmap-Einfärbung.

**Korrekturidee:** Latenz beim HUE-Drag messen; erst danach Drosselung oder shaderbasierte Transformation prüfen.

### C — Zustandskompatibilität

| Controller-State-Version | Struktur / Änderung | Ergebnis |
|---|---|---|
| v1–v3 | Zoom, Keyboard, Transpose; später Preset und DETAILS | Kein offensichtlicher Lesefehler |
| v4 | Vier HUE-Werte ohne Zähler | Korrekt berücksichtigt |
| v5 | Historischer Trail-Wert | Korrekt gelesen und verworfen |
| v6–v7 | WAVE-Helligkeit, ab v7 auch `kPresetWave` | Korrekt berücksichtigt |
| v8 | Vier HUE-Werte, zusätzlich GALAXY/WAVE | Korrekt berücksichtigt |
| v9 | HUE-Anzahl als `int32` vor den Farbwerten, danach WAVE/GALAXY | Korrekt berücksichtigt |

**Sicherheits-/Konsistenzprüfung:** Bei v9 wird der gespeicherte HUE-Zähler auf 0–256 begrenzt. Überzählige Werte werden gelesen und geprüft, jedoch nicht über die acht vorhandenen Array-Elemente hinaus gespeichert. Kein offensichtlicher Out-of-bounds-Schreibzugriff.

**Bewusste optische Neuzuordnung:** Alte Preset-IDs behalten ihre Zahlen, aber nicht ihre Darstellung:

| beta.1-ID und Preset | beta.2-Darstellung |
|---|---|
| 0 — Slate | Spiral galaxy (M51) |
| 1 — Silver | Barred spiral (M109) |
| 2 — Plum | Ring nebula (M57) |
| 3 — Gold | Diffuse nebula (M42) |

Diese Veränderung ist eine bewusste Designmigration und kein Dekodierfehler. Die Klang-State-Kompatibilität war nicht im Scope.

### C — Custom-Skin- und HUE-Prüfungen ohne Befund

- Bitmaps aus Custom-Skin-Ordnern werden bezüglich Dateigröße, PNG-IHDR, erwarteter Abmessungen und Dekodierbarkeit geprüft; fehlende beziehungsweise fehlerhafte Dateien fallen auf andere Quellen zurück.
- Alte HUE-Werte liegen weiter im erweiterten Wertebereich; `AccentOnScale` bringt Werte außerhalb der neuen Sliderstrecke auf deren nächstes Ende.
- `Skin::SetAccent` aktualisiert Farbfelder ohne erneutes allgemeines Laden aller Bitmaps; das Recoloring kolorierter Nebelbilder erfolgt separat.
- `EditorSettings` speichert acht HUE-Werte; alte States lesen vier und lassen weitere auf dem Presetstandard.

### C — WaveOverview / RMS

**Evidenz — `MakeOverview`:**

```cpp
squares += double(pcm[i]) * pcm[i];
// ...
overview->rms[c] = static_cast<std::uint16_t>(
    std::min(32768.0, std::round(std::sqrt(squares / double(count > 0 ? count : 1)))));
```

| Fall | Ergebnis |
|---|---|
| Mono | Alle Frames der jeweiligen Spalte berücksichtigt |
| Stereo | Interleavte Werte beider Kanäle fließen in den Mittelwert ein |
| Sehr kurze Datei | Ein Frame ergibt eine nichtleere Spalte; Nenner abgesichert |
| Stille | RMS = 0 |
| Negative Vollaussteuerung | Ergebnis auf 32768 begrenzt |
| Sehr lange Datei | Maximal 2048 Übersichtsspalten |

`rms[2048]` fügt 4096 Bytes pro `WaveOverview` hinzu (Array-Nutzlast vorher ca. 8 KiB, danach ca. 12 KiB). Die Erzeugung erfolgt einmal pro Datei und nicht je GUI-Frame. Externe Transfer- und Konsumentenpfade der erweiterten Struktur sind außerhalb dieses Reviews und durch Codex gesondert zu prüfen.

---

# D. Aufgabenliste und Abnahme

Die Liste ist bewusst nach **Prüfbedarf** und nicht als automatischer Reparaturauftrag priorisiert.

## D.1 Claude — Gegenprüfung vor Änderungen

- [ ] **A2:** Star-Cache bei Samplewechsel prüfen, reproduzieren und bei Bestätigung invalidieren.
- [ ] **A3:** Unterschied `kTrail` / `kStarGlow` in verzögerten Displays testen; GALAXY-Afterglows bei Bedarf trennen.
- [ ] **A1:** Sternstart bei Tempo-Einschwingphase visuell prüfen; Richtung und geglätteten Drift sauber unterscheiden.
- [ ] **B1:** Gehaltene/LATCH-Note beim `ApplySettings(keyboard=false)` prüfen; Controller-Reset gegenprüfen.
- [ ] **B2:** Asynchrone Host-Größenannahme bei Projektrestore simulieren; Layout bis `onSize` synchronisieren.
- [ ] **C2:** Falschen Dateinamen in der Custom-Skin-Anleitung korrigieren; acht Varianten aufführen.
- [ ] **C3:** Semantik von `messier_slate_colours.txt` für alle neuen Presets bewusst entscheiden.
- [ ] **B4:** Oktav-Tooltips an Ein-Oktav-Tastatur anpassen.
- [ ] **A8 / B5:** Fehlerpfade bei Pixel-Read und Edit-Begin auf saubere Wiederherstellung prüfen.
- [ ] **C1:** Gültige IDs pro State-Version abgrenzen und per Regressionstest absichern.
- [ ] **B3:** PITCH-Rasterung nur bei reproduziertem Drag-Problem ändern.

## D.2 Codex — Teil 0 und unabhängige Validierung

- [ ] Eigenes lokales Checkout auf den vereinbarten Commit bzw. auf den freigegebenen Folgecommit setzen.
- [ ] Debug- und Release-Build unter VS 2022 sowie die ausstehende ASan-Testmatrix durchführen; Ergebnisse mit genauer Konfiguration protokollieren.
- [ ] Laden alter GUI-Controller-States (v1–v8) und aktueller v9-States prüfen; gültige und ungültige Varianten getrennt testen.
- [ ] Regressionstests für START/END-Drag, Begin/End, PITCH-Fine-Drag, Tastatur mit LATCH, Projektrestore und Host-Resize durchführen.
- [ ] RAM-/CPU-Profil für GALAXY-Rotation, 25-ms-Invaliderung, Grain-Gradienten, 1×/2×-Bitmaps und HUE-Slider erstellen.
- [ ] Mehrere Instanzen, Skin-Reload und fehlende/falsche PNGs prüfen.
- [ ] RMS für Mono, Stereo, Ein-Frame-Datei, Stille und Maximalwerte sowie alle Leser/Transferpfade prüfen.

## D.3 Keine ungeprüften Optimierungen

**Performance-Kandidaten:** A4, A5, A6, A7, B6, B7, C4 und C5. Die statische Prüfung beweist jeweils den betreffenden Rechen-, Redraw-, Lade- oder Callback-Pfad, **nicht** ein reales UI-Ruckeln oder einen relevanten CPU-Mehrverbrauch. Eine Implementierungsentscheidung soll erst nach Messung erfolgen.

**Keine Quellcodeänderungen in diesem Bericht:** Dieser Text konsolidiert die drei ursprünglichen GPT-Reviews und dient als Prüfliste. Er enthält keine Ergebnisse aus ausstehenden Host-, Build-, ASan- oder Hardwaretests.
