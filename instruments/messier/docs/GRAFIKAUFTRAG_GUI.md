# Grafikauftrag: Bitmaps für das Messier-Fenster

Stand 2026-11-18. Für ein Bildmodell (GPT mit Bildgenerierung). Claude prüft die
gelieferten Bilder und baut sie ins Plugin ein.

**Vorlage:** die Fensterstudie, Fassung 3 ([Mockup](https://claude.ai/artifact/VxHc8Nbapj8ArJvkDQebWG)).
Die Entscheidungen dazu stehen in `docs/PLANUNG.md`, Abschnitt Messier, Zeile „Fenster“.

## Was gebraucht wird und was nicht

Gebraucht werden nur **Oberflächen und Material**: Metall, Glas, Nebel, Knopfkappen, Schrauben,
Tasten. Alles, was Information trägt, zeichnet das Plugin selbst, damit es an echte Werte
gebunden bleibt und scharf skaliert:

* Text, Zahlen und Skalen;
* das Raster und die Achsen der Messplatte;
* Wellenform, Grains, Loop, Spray und Abspielposition;
* Zeiger und Wertbögen der Regler.

Deshalb gilt für **jedes** Bild:

* **Kein Text**, keine Buchstaben, keine Zahlen, keine Logos, keine Markennamen.
* **Keine Bedienelemente**, die es nicht gibt: keine Schalter, Anzeigen, Pegel, Menüs, LEDs.
* **Keine punktförmigen Sterne** und keine hellen Punkte in Nebel und Platte. Sie wären mit
  Grains zu verwechseln.
* **Nicht im Stil eines bestimmten Produkts, Herstellers oder Künstlers.** Die Beschreibung
  steht unten in eigenen Worten.
* Licht kommt von **oben links**, weich, ohne harte Spiegelungen.
* PNG, sRGB, 8 bit je Kanal. Mit Alphakanal, wo es unten steht.
* Jedes Bild in **zwei Größen**: `@1x` in den genannten Pixelmaßen und `@2x` in doppelter
  Größe. Die 2x-Fassung ist gleich komponiert, nur feiner, nicht hochskaliert.

## Die vier GUI-Presets

| Preset | Gehäuse | Messplatte | Nebel (Tönung im Plugin) | Kappen und Schrauben | Schrift, Akzent |
| --- | --- | --- | --- | --- | --- |
| **Slate** | dunkles, kühles Stahlgrau `#2E3235`, Wasserflecken `#3E4A52` | dunkles Glas `#0B0F1A` | Violett `#68529C` / Blau `#384480` | Stahl | `#DCD6CA`, Akzent `#E3733D` |
| **Silver** | helles, gebürstetes Aluminium `#B0B2B1`, Flecken `#969A99` | dunkles Glas `#0B0F1A` | wie Slate | Nickel, glänzend | `#1C1F21`, Akzent `#C9531C` |
| **Copper** | dunkles Kupfer `#382822`, Patina `#583A2C` | dunkles, warmes Glas `#0F0909` | Weinrot `#8A3442` / `#602A42` | Kupfer | `#E4C7AD`, Akzent `#F17C3E` |
| **Photo plate** | dunkle Bronze `#3A342A`, grünliche Patina `#40604F` | **helles** Glas `#CDD1CA`, wie eine Fotoplatte | Grautöne `#78746C` / `#968E80` | Messing | `#E2D9C6`, Akzent `#D0A85C` |

Die Farbwerte sind Richtwerte. Das Material soll glaubwürdig wirken, nicht flach eingefärbt.

## Bilder

Maße in Pixeln für `@1x`. Dateinamen: `messier_<preset>_<bild>@1x.png` und `@2x.png`, mit
`<preset>` = `slate`, `silver`, `copper`, `plate`. Bilder ohne Preset im Namen gelten für alle.

| Nr. | Bild | Maß @1x | Je Preset | Alpha | Inhalt |
| --- | --- | --- | --- | --- | --- |
| 1 | `chassis` | 880 × 724 | ja (4) | nein | Die ganze Metallfläche des Fensters, ohne jede Einteilung: keine Vertiefungen, Linien, Schrauben oder Kanten. Leichte Vignette zu den Rändern. Unten 84 px mit, für die einblendbare Klaviatur. |
| 2 | `bezel` | 64 × 64 | ja (4) | ja | Neun-Felder-Rahmen um die Messplatte. Gefräste Metallkante, 12 px breit, innen leicht vertieft, Ecken mit 8 px Radius, Mitte transparent. Das Plugin dehnt die Seiten. |
| 3 | `plate` | 840 × 186 | ja (4) | nein | Die Glasfläche der Messplatte: feines Korn, ganz leichte Ungleichmäßigkeit, zum Rand minimal dunkler (bei Photo plate: minimal grauer). Kein Raster, keine Punkte, keine Kratzer, keine Schrift. |
| 4 | `nebula_a`, `nebula_b` | 1000 × 260 | nein (2) | ja | Zwei verschiedene, weiche, diffuse Gaswolken, **weiß auf transparent**: die Helligkeit ist die Deckkraft. Ein waagerechtes Band durch die Mitte, ausfransend, mit feinen Filamenten, ohne Sterne, ohne Kanten. Das Plugin tönt sie je Preset und lässt sie gegeneinander driften. |
| 5 | `knob_l`, `knob_s` | 72 × 72 und 48 × 48 (Kappe 48 und 30 px Durchmesser, Rest Rand) | je Oberfläche (4: Stahl, Nickel, Kupfer, Messing) | ja | Drehknopfkappe von oben: gedrehtes Metall mit konzentrischen Drehriefen, gerändelter Rand. **Drehsymmetrisch**, ohne Zeiger, Punkt oder Markierung (der Zeiger kommt vom Plugin). Kein Schatten, kein Hintergrund. |
| 6 | `thumb` | 16 × 28 | je Oberfläche (4) | ja | Griff des breiten START-Reglers: ein gefrästes Metallklötzchen von vorn, Kanten leicht gebrochen, ohne Rille und Markierung. |
| 7 | `button`, `button_down` | 48 × 28 | ja (4) | ja | Taste als Neun-Felder-Bild (Ränder 8 px): erhaben bzw. gedrückt, ohne Text. Aus demselben Metall wie das Gehäuse, Kante etwas heller. |
| 8 | `screw` | 14 × 14 | je Oberfläche (4) | ja | Linsenkopfschraube mit Schlitz, von oben, leicht abgenutzt. |
| 9 | `keys` | 36 × 66 (weiß), 22 × 40 (schwarz) | nein | ja | Je eine weiße und eine schwarze Taste der Bildschirm-Klaviatur von oben, leicht gealtert, ohne Beschriftung. |

Zusammen 40 Bilder je Größe:

* 20 je Preset: Gehäuse, Rahmen, Platte und die Taste in zwei Zuständen, mal vier;
* 16 je Oberfläche: zwei Knopfkappen, Reglergriff und Schraube, mal vier;
* 2 Nebel und 2 Klaviertasten.

Erhält GPT nur einen Teil, dann zuerst Slate vollständig. Daran prüfe ich den Einbau, bevor die
übrigen Presets entstehen.

## Prompts (englisch, je Bild anzupassen)

Jeder Prompt beginnt mit dem gemeinsamen Teil:

> Seamless texture asset for a software audio instrument interface, photographic realism,
> soft light from the top left. No text, no letters, no numbers, no logos, no symbols, no
> buttons, no dials, no stars or bright dots. Not in the style of any existing product.

Dann je Bild, zum Beispiel:

1. **chassis, Slate:** *A flat rectangular panel of old cold-rolled steel, dark cool grey
   (#2E3235), faint water stains and patina (#3E4A52), fine scratches in many directions,
   a few longer scratches, subtle horizontal brushing, slight darkening toward the edges.
   Even, uncluttered, no recesses, no lines, no screws. 880 × 724 pixels.*
2. **chassis, Photo plate:** *… old dark bronze (#3A342A) with green-grey patina
   (#40604F) in soft patches, worn edges of the patina, fine scratches …*
3. **plate, Photo plate:** *A rectangular sheet of clear photographic glass with a pale
   grey-green emulsion (#CDD1CA), extremely fine grain, very slight unevenness, the
   border a touch greyer. Completely empty: no grid, no marks, no dots, no scratches.
   840 × 186 pixels.*
4. **nebula_a:** *Soft diffuse cloud of interstellar gas, white on a fully transparent
   background, brightness meaning opacity, a horizontal band through the middle with
   fraying edges and fine wispy filaments, no stars, no hard edges, no colour.
   1000 × 260 pixels, PNG with alpha.*
5. **knob_l, Brass:** *Top view of a round turned brass knob cap, concentric lathe
   grooves, knurled rim, rotationally symmetric, no pointer, no marking, no shadow, on a
   transparent background, 48 px cap centred on 72 × 72 pixels.*

## Abnahme durch Claude

* Maße und Alphakanal stimmen; `@2x` ist kein hochskaliertes `@1x`.
* Kein Text, keine Punkte, keine Bedienelemente; Kappen sind wirklich drehsymmetrisch (eine
  Drehung um 90° sieht gleich aus).
* Kontrast: Schrift und Werte in den Farben der Tabelle bleiben auf dem Gehäuse lesbar
  (mindestens 4,5 : 1 an der dunkelsten bzw. hellsten Stelle hinter dem Text).
* Größe: alle Bilder zusammen unter 15 MB im Bundle, sonst werden sie komprimiert oder die
  Gehäuse als kachelbare Fläche neu angefordert.

## Hinweise

* **Urheberrecht:** Für KI-erzeugte Bilder ist der Schutz unsicher. Benutzen darfst du sie
  nach den Nutzungsbedingungen in der Regel, andere könnten sie aber unter Umständen auch frei
  verwenden. Wenn dir eigener Schutz wichtig ist, die Endfassung von Hand nachbearbeiten.
* **Skalierung:** `@2x` deckt 200 % auf einem normalen Bildschirm ab. Auf einem
  hochauflösenden Bildschirm bei 200 % wird `@2x` leicht hochskaliert. Ist das sichtbar, kann
  eine `@3x`-Fassung nachgeliefert werden.
