---
kind: interface
summary: >-
  Makes a picture of a plate-solved image with its deep-sky objects, bright stars and coordinates named on it, ready
  to share or print, and exports it or opens it as an image.
menu: Image > Information > Annotate Image...
rewritten: true
---
# Annotate Image

Annotate Image makes a picture of a plate-solved image with its galaxies, nebulae and clusters, the names of its bright
stars and a grid of right ascension and declination drawn on it, with a compass, a scale bar and a panel of facts. It
shows what an image holds, which way is north and how large the field is. The picture is the image as its view shows
it, with the annotations drawn over it, written to a PNG, JPEG or TIFF file or opened as an image of its own. The image
you annotate never changes. It may be gray or color, of any [pixel type](../reference/glossary.md#pixel-type), linear
or not, and needs a [plate solution](../reference/glossary.md#plate-solution), the one the
[Astrometry Overlays](astrometry-overlays.md) draw.

```figure
owed: true
image: ../assets/images/interface/annotate-image.webp
alt: >-
  The Annotate Image window. On the left, a column of choices: four level-of-detail buttons, Essential, Standard,
  Detailed and Complete, with Complete highlighted; six ticked check boxes for the layers; a Label text box; then the
  Style section with a Palette box showing Aurora, and its Text and Lines settings. On the right, a preview of the
  Carina Nebula with a blue coordinate grid, a dashed pink circle labelled NGC 3372 and Carina Nebula, white rings and
  labels on stars, a compass at the top right, a scale bar at the bottom right and a panel of facts at the bottom left.
  Under the preview, a line counts the objects and gives the picture's size. Reset, Open as Image, Export and Close
  run along the bottom.
caption: >-
  Annotate Image over the Carina master at the Complete level of detail, with a credit typed for the panel.
pins:
  - {n: 1, x: 11.3, y: 17.6}
  - {n: 2, x: 28.0, y: 24.8}
  - {n: 3, x: 9.0, y: 39.6}
  - {n: 4, x: 48.4, y: 14.7}
  - {n: 5, x: 56.7, y: 92.3}
  - {n: 6, x: 74.9, y: 96.4}
```

## What you see

1. **Level of detail.** Four presets of how much of the bundled catalogs the picture names, from the objects everyone
   knows to a dense chart.
2. **The layers.** What the picture draws: the coordinate grid, the deep-sky objects, the stars, the compass, the scale
   bar and the info panel.
3. **The sections.** **Style**, **Info Panel and Placement**, **Catalogues and Limits** and **Output** hold the rest of
   the choices. Click a section's header to open or close it, and scroll the column to reach them all.
4. **The preview.** The picture as it will be drawn, over the image itself. It zooms and pans as an image view does,
   and a small "Updating…" badge shows in its corner while a new layout is on its way.
5. **The summary.** How many objects the picture marks, how many of them are labelled when some are not, and the
   picture's size in pixels.
6. **The buttons.** **Reset** returns the choices to their defaults, **Open as Image** and **Export…** make the
   picture, and **Close** keeps your choices for the next time.

## What you can do

### Make a picture

1. Make the solved image's view active and choose **Annotate Image** in the view's toolbar, press ++ctrl+shift+l++ over
   the picture, or choose **Image > Information > Annotate Image...**. The preview fills with the image while the
   catalogs are read.
2. Look at the image in its view first. With **Image** at **As the view shows it**, the picture takes the view's
   display mode and [Autostretch](../reference/glossary.md#autostretch) settings, and the image itself does not change.
3. Choose a **Level of detail**, then the layers and **Label text**. The preview is laid out again after each change.
4. Under **Style**, choose a **Palette**, then the text and the lines. No choice of the text changes the lines, and none
   of the lines changes the text.
5. Under **Info Panel and Placement**, type a **Title** or leave it empty, type a **Credit** if you want your name on
   the picture, and choose the facts and the corners.
6. Under **Output**, choose the **Size**, how the **Image** is presented and the **Background**.
7. Choose **Export…** and a PNG, JPEG or TIFF file, or **Open as Image**. A progress window shows while the picture is
   drawn, and the message strip then says where it was saved or which view it opened in.

### Choose how much is named

A level of detail writes its values into **Catalogues and Limits**. Change any of them and no level is highlighted: the
values are then your own.

| Level | Deep-sky objects | Faintest magnitude | Smallest object | Stars |
| --- | --- | --- | --- | --- |
| Essential | Messier, Caldwell, and named NGC and IC objects | Any | Any size | Proper names to magnitude 2.5 |
| Standard | Every catalog; of no measured magnitude only when named | 12 | 0.30 % | Names and designations to magnitude 4.5 |
| Detailed | Every catalog, measured or not | 14 | 0.12 % | Names and designations to magnitude 6, the unnamed stars marked |
| Complete | Every catalog, with its other entries | Any | Any size | Every star to magnitude 9, by catalog number when unnamed |

Labels are placed one by one, the most prominent object first. A label that finds no room beside its object moves out
on a leader line, or is left out, and the summary counts it.

### Annotate the Carina master

The [master](../reference/glossary.md#master) is a linear RGB image of the Carina Nebula, 7246 by 5429 pixels, solved at
1.10″ a pixel and shown in its view's Autostretch display. Open Annotate Image and choose **Reset**. At **Standard**
the preview marks one object, NGC 3372, the Carina Nebula. It is larger than much of the frame, so it is a dashed region
labelled inside itself, which **Fill objects with their colour** never tints. No star of the field is as bright as
magnitude 4.5, so no star is named.

Open **Info Panel and Placement**, type a credit and choose **Complete**. Every star of the catalog in the frame is now
ringed and named, η Car at the catalog's magnitude 6.2 and the others by their HIP or HR numbers. The panel is titled
Carina Nebula, from the header's `OBJECT` card. It states the center, `10h 44m 31s   −59° 53′ 32″`, the field,
`2.20° × 1.65°  ·  1.10″/px`, the rotation, the optics and the camera. The header gives its exposure in no card the
panel reads, so the panel has no integration line. Trumpler 14 and 16, the Keyhole and other objects of no NGC, IC,
Messier or Caldwell number are not in the bundled catalog and are not named.

## Controls

### Annotations

```controls
window: Annotations
controls:
  - name: Level of detail
    kind: radio
    default: Standard
    options: [Essential, Standard, Detailed, Complete]
    effect: Writes a level's catalogs, kinds, magnitude limits, smallest object and stars into **Catalogues and Limits**, as the table above gives them.
    enabled: >-
      The chosen level's button is highlighted. While the values below are your own, none is, and the line
      "Custom: the catalogues and limits below are your own." shows under the buttons.
    tradeoff: More detail adds labels, and a crowded field leaves more of them out.
    persistence: Every choice of the window but **Title** is kept under `annotation/settings` half a second after you make it and when the window closes, and the next window opens with them.
    note: Each button's tooltip says what the level names.
  - name: Coordinate grid
    kind: check
    default: "On"
    effect: Draws lines of right ascension and declination at a round step, in seconds, minutes and hours of time and in arcseconds, arcminutes and degrees, as many as **Grid spacing** asks for across the field.
    range: A field that holds a celestial pole draws every hour of right ascension. Labels sit just inside the picture's edge, declinations at the left and right ascensions at the bottom where the lines meet those edges, another edge otherwise.
  - name: Deep-sky objects
    kind: check
    default: "On"
    effect: Marks and labels the galaxies, nebulae and clusters of the bundled catalog that **Catalogues and Limits** keeps.
    range: The catalogs come with AfterNight and are read without a network, once for the solution, for the field's radius and two degrees more, so a large object centered outside the frame is found. An entry is kept when its catalog and kind are chosen, it passes the magnitude and size limits, and some of it lies in the frame. Entries of one object in several catalogs, as `M 42` and `NGC 1976`, or `NGC 3372` and `C 92`, are one mark under the designation used most - Messier, then NGC, then IC, then Caldwell.
    tradeoff: An object larger than much of the frame is labelled inside itself and outlined dashed, as a region rather than an object.
    scope: The NGC, IC, Messier and Caldwell objects alone. Sharpless, Barnard, Collinder and Trumpler objects are not in the catalog.
  - name: Stars
    object: annotateStars
    kind: check
    default: "On"
    effect: Marks and labels the bright stars that **Faintest star**, **Star labels** and **Mark the stars left unnamed** choose.
    range: A star is kept when it is bright enough, lies in the frame, and **Star labels** names it - by proper name, Bayer or Flamsteed designation or catalog number - or **Mark the stars left unnamed** asks for it.
    scope: The positions are J2000 without proper motion, and a star is drawn where the catalog puts it.
  - name: Compass
    kind: check
    default: "On"
    effect: Draws arrows toward celestial north and east, as the solution places them in the compass's corner.
    range: Measured where it is drawn, so it stays right near a pole and in a wide field.
  - name: Scale bar
    kind: check
    default: "On"
    effect: Draws a bar of the round angle nearest a sixth of the picture's width, from an arcsecond to twenty degrees, labelled as `20′`.
    range: Measured where it is drawn, as the compass is.
  - name: Info panel
    kind: check
    default: "On"
    effect: Draws a panel with the title, the credit and the facts chosen under **Info Panel and Placement**.
    enabled: A panel with nothing to state is not drawn.
  - name: Label text
    kind: combo
    default: Designation and name
    options: [Designation and name, Designation, Name]
    effect: Chooses what a label says - `M 42` with `Orion Nebula` under it, the designation alone, or the name, which is the designation when there is none.
    tradeoff: Two-line labels take more room. The name under a designation is drawn a little smaller.
```

### Style

```controls
window: Style
controls:
  - name: Palette
    kind: combo
    default: Aurora
    options: [Aurora, Moonlight, Glacier, Amethyst, Amber, Verdant, Rose, Atlas, "Ember - for dark-adapted eyes", "Ink - for bright images and print", Custom]
    effect: Sets every color of the picture. **Aurora** gives each kind of object its own color, **Moonlight** draws in white, and **Glacier**, **Amethyst**, **Amber**, **Verdant** and **Rose** keep to blues, violets, golds, greens and pinks, a shade for each kind. **Atlas** takes a printed star atlas's colors - red galaxies, green nebulae, yellow clusters. **Ember** is red for eyes adapted to the dark, and **Ink** draws dark lines on a light halo for a bright image or print.
    range: Each palette's swatch shows its colors for galaxies, nebulae, planetary nebulae and open and globular clusters, over its panel color.
    enabled: It shows **Custom** once you set a color.
    persistence: "**Custom** is your own colors: chosen, it brings them back and opens **Colours**. The first time, it starts from the palette shown. Choosing another palette keeps your colors for the next time you choose **Custom**."
  - name: Font
    kind: combo
    default: The system's sans-serif face
    options: [The system's sans-serif face, Every scalable font installed]
    effect: Sets the typeface of every text of the picture - the labels, the grid's labels, the compass, the scale bar and the panel.
    range: The list shows the system's face under the name of the application's own font.
    persistence: A typeface missing where the choices are read again is replaced by the system's.
  - name: Weight
    kind: combo
    default: Regular
    options: [Light, Regular, Medium, Bold]
    effect: Sets how heavy the letters are. A designation and the title are drawn a step heavier than the rest.
  - name: Italic labels
    kind: check
    default: "Off"
    effect: Slants the labels of objects and stars. The grid's labels and the panel stay upright.
  - name: Colour
    kind: combo
    default: Each object's colour
    options: [Each object's colour, The text colour]
    effect: Draws each label in its object's color, or every label in the palette's text color.
  - name: Backing
    kind: combo
    default: Halo
    options: [Halo, Badge, Plain]
    effect: Sets a soft outline around each letter, a rounded box behind each label, or the letters alone.
    tradeoff: A badge covers more of the image.
    scope: The letters only. The lines keep their own dark edge.
  - name: Label size
    kind: slider
    default: "100"
    widget: {min: 50, max: 250, step: 1}
    effect: Scales the labels, the grid's labels, the compass and the scale bar, in percent. At 100 a label is 1.4 % of the image's reference length, the square root of its width times its height.
    tradeoff: Larger text leaves room for fewer labels in a crowded field.
    scope: The text only, never the width of a line.
  - name: Panel size
    kind: slider
    default: "100"
    widget: {min: 50, max: 200, step: 1}
    effect: Scales the info panel's text, in percent of the label size.
  - name: Objects
    kind: combo
    default: Outlines
    options: [Outlines, Brackets]
    effect: Marks a deep-sky object by its outline, its catalog size at its position angle, or by four corner brackets.
    range: An object that covers the frame is drawn as a dashed outline whatever you choose.
    note: The catalog's sizes and position angles are approximations.
  - name: Stars
    object: annotateStarMarker
    kind: combo
    default: Rings
    options: [Rings, Reticles]
    effect: Marks a star by a ring around it, or by four short ticks pointing at it that leave the star itself clear.
  - name: Line width
    kind: slider
    default: "100"
    widget: {min: 50, max: 300, step: 1}
    effect: Scales every line, in percent - outlines, star marks, grid, leader lines, compass and scale bar.
    scope: The lines only, never the text.
  - name: Line opacity
    kind: slider
    default: "100"
    widget: {min: 10, max: 100, step: 1}
    effect: Sets how opaque the outlines, the star marks and the leader lines are, in percent.
    scope: The grid has its own opacity, and the compass and the scale bar stay opaque.
  - name: Fill objects with their colour
    kind: check
    default: "Off"
    effect: Tints the area of every outlined galaxy, nebula and cluster in its color.
    enabled: Disabled with **Brackets**. An object whose outline is wider or taller than half the picture's shorter side is left unfilled, so it does not tint the whole image.
    tradeoff: At a high opacity the tint hides some of a small object.
    note: The view's deep-sky annotations are never filled.
  - name: Fill opacity
    kind: slider
    default: "15"
    widget: {min: 0, max: 100, step: 1}
    effect: Sets how opaque that tint is, in percent.
    enabled: While **Fill objects with their colour** is ticked.
  - name: Grid opacity
    kind: slider
    default: "45"
    widget: {min: 0, max: 100, step: 1}
    effect: Sets how opaque the grid's lines are, in percent. Its labels are drawn more opaque.
  - name: Dark edge behind lines
    kind: check
    default: "Off"
    effect: Draws a soft edge in the halo color behind every line, so that it reads over a bright part of the image.
    scope: Apart from the labels' **Backing**.
  - name: Leader lines for crowded labels
    kind: check
    default: "On"
    effect: Lets a label that finds no room beside its object sit farther away, joined to it by a line.
    tradeoff: Off leaves more labels out of a crowded field.
    note: Labels are placed most prominent first - Messier, Caldwell and named objects before the others, larger and brighter before smaller and fainter. A label tries the places beside its object in a fixed order and takes the first that stays inside the picture and covers no other label and no decoration. A place that covers a smaller object or crosses another outline is taken only when no leader line reaches a free one.
  - name: Mark objects whose labels do not fit
    kind: check
    default: "On"
    effect: Keeps the outline or ring of an object whose label was left out.
  - name: Colours
    kind: button
    default: ""
    effect: "Fourteen buttons, each showing its color: **Grid**, **Galaxies**, **Nebulae**, **Planetary nebulae**, **Open clusters**, **Globular clusters**, **Other objects**, **Stars**, **Text**, **Halo**, **Compass and scale**, **Panel**, **Panel text** and **Accent**. Each opens a color window, with opacity."
    enabled: The section is closed until you open it, or choose **Custom** in **Palette**.
    persistence: Setting any color makes the palette **Custom** and keeps your colors.
```

### Info Panel and Placement

```controls
window: Info Panel and Placement
controls:
  - name: Title
    kind: field
    default: ""
    effect: Sets the panel's title. Empty takes the target the header's `OBJECT` card names, else the most prominent named object, and the field shows that title, or "No title", as its placeholder.
    persistence: Not kept, since it belongs to one image.
  - name: Credit
    kind: field
    default: ""
    effect: Sets a line under the title, such as your name. Its placeholder reads "Your name, your observatory".
    tradeoff: It is drawn into the picture. Leave it empty to share a picture without a name.
  - name: Center
    kind: check
    default: "On"
    effect: States the field's center, as `10h 44m 31s   −59° 53′ 32″`.
    note: Each fact is stated only when the image states it.
  - name: Field and scale
    kind: check
    default: "On"
    effect: States the field's width and height with the mean pixel scale, as `2.20° × 1.65°  ·  1.10″/px`, and on a line of its own the **Rotation** of north, to a tenth of a degree.
  - name: Optics
    kind: check
    default: "On"
    effect: States `TELESCOP`, with `FOCALLEN` in millimetres and the focal ratio of `FOCRATIO`, or of `FOCALLEN` over `APTDIA`.
  - name: Camera
    kind: check
    default: "On"
    effect: States `INSTRUME`.
  - name: Filter
    kind: check
    default: "On"
    effect: States `FILTER`.
  - name: Date
    kind: check
    default: "On"
    effect: States the date of `DATE-OBS`.
  - name: Integration
    kind: check
    default: "On"
    effect: States `TOTEXP`, else `EXPTIME`, as `2 h 30 min`, with the frames of `STACKCNT`, else `NCOMBINE`. A single frame with `EXPTIME` alone reads as **Exposure**.
  - name: Panel
    kind: combo
    default: Bottom left
    options: [Top left, Top right, Bottom left, Bottom right]
    effect: Chooses the panel's corner.
    note: Decorations that share a corner are stacked inward from it.
  - name: Compass
    kind: combo
    default: Top right
    options: [Top left, Top right, Bottom left, Bottom right]
    effect: Chooses the compass's corner.
  - name: Scale bar
    kind: combo
    default: Bottom right
    options: [Top left, Top right, Bottom left, Bottom right]
    effect: Chooses the scale bar's corner.
```

### Catalogues and Limits

```controls
window: Catalogues and Limits
controls:
  - name: Messier, Caldwell, NGC, IC
    kind: check
    default: "On"
    effect: Choose the catalogs objects are taken from.
  - name: Only named NGC and IC objects
    kind: check
    default: "Off"
    effect: Keeps an NGC or IC object only when it has a common name.
    note: "**Essential** ticks it."
  - name: Galaxies, Nebulae, Planetary nebulae, Open clusters, Globular clusters
    kind: check
    default: "On"
    effect: Choose the kinds of object marked. Nebulae include dark nebulae and clusters with nebulosity.
  - name: Other entries
    kind: check
    default: "Off"
    effect: Marks the catalog's stars, asterisms, knots and entries of no known kind.
    note: "**Complete** ticks it."
  - name: Faintest magnitude
    kind: spinbox
    default: "12.0"
    widget: {min: -2, max: 20, step: 0.5, decimals: 1}
    effect: Leaves out objects fainter than this magnitude.
    enabled: Disabled while **Any** is ticked, when it shows 16.0.
  - name: Any
    kind: check
    default: "Off"
    effect: Marks objects of every magnitude.
  - name: Objects of no measured magnitude
    kind: check
    default: "Off"
    effect: Marks objects no catalog measured. A named object is always marked.
  - name: Smallest object
    kind: spinbox
    default: "0.30"
    widget: {min: 0, max: 5, step: 0.05, decimals: 2}
    effect: Leaves out objects whose major axis is smaller than this share of the image's reference length, in percent.
    range: Messier, Caldwell and named objects are kept whatever their size, and an object of no stated size is judged by its magnitude alone.
  - name: Faintest star
    kind: spinbox
    default: "4.5"
    widget: {min: -2, max: 9, step: 0.5, decimals: 1}
    effect: Sets the faintest star marked.
    range: The bundled catalog stops at magnitude 9.
    tradeoff: A faint limit adds many stars to a wide field.
  - name: Star labels
    kind: combo
    default: Names and designations
    options: [Proper names, Names and designations, "Every star, by number when unnamed"]
    effect: Chooses which stars are named and by what - a proper name, as `Sirius`; also a Bayer or Flamsteed designation, as `η Car` or `61 Cyg`; or every star, one with neither named by its catalog number, as `HIP 52468`.
    tradeoff: Every star adds many labels to a wide field.
  - name: Mark the stars left unnamed
    kind: check
    default: "Off"
    effect: Rings the stars **Star labels** gives no name, without a label.
    note: "**Detailed** ticks it."
  - name: Grid spacing
    kind: combo
    default: Normal
    options: [Coarse, Normal, Fine]
    effect: Draws about four, six or ten grid lines across the field, at round steps of time and arc.
  - name: Label the grid at the edges
    kind: check
    default: "On"
    effect: Labels each grid line where it meets the picture's edge.
```

### Output

```controls
window: Output
controls:
  - name: Size
    kind: combo
    default: Full size
    options: [Full size, 4096 px, 3840 px, 2560 px, 2048 px, 1600 px, 1280 px, 1024 px]
    effect: Sets the picture's size by its long edge, each item followed by the picture's width and height. The picture keeps the image's aspect ratio and is never larger than the image.
    range: Only the long edges below the image's own are offered. A size kept from another image is added to them when it is below the image's own, and reads as **Full size** otherwise.
    tradeoff: A full-size picture holds four bytes a pixel while it is drawn, about 160 MB for a 40-megapixel image, counted against the session's memory for display. A smaller picture is drawn faster from a reduced copy of the whole image, stretched as the whole image is.
  - name: Image
    kind: combo
    default: As the view shows it
    options: [As the view shows it, "As stored, unstretched"]
    effect: Presents the image under the annotations as its view shows it - its display mode and Autostretch settings, the whole image rather than the view's channel - or every channel at full scale with nothing stretched.
    tradeoff: A linear image looks dark as stored, and so does one its view shows in **Linear**.
  - name: Background
    kind: combo
    default: The image
    options: [The image, "Transparent: annotations only"]
    effect: Draws the annotations over the image, or alone over transparent pixels, to lay over the image in another program.
    range: The preview shows a transparent picture over a checkerboard.
    enabled: A JPEG file cannot hold a transparent picture, and the file window then offers PNG and TIFF alone.
```

### The preview and the buttons

```controls
window: The preview and the buttons
controls:
  - name: Preview
    kind: readout
    default: ""
    effect: Shows the picture the choices make over the view's own image, shared rather than copied, presented as the Image Viewer presents an image at every zoom.
    range: Its toolbar has **Fit To Window**, **Zoom In**, **Zoom Out** and **Zoom 100%**. The mouse wheel zooms, a middle-button drag moves the picture, and holding the left button opens the lens. It stays fitted to the window until you zoom.
    tradeoff: The catalogs, the layout, the preview's image and the picture each work in the background, and the window never waits. A field of a few hundred objects is laid out in tens of milliseconds. A change of color only draws again.
    note: Every length - a label's height, a line's width, a mark's radius, the margins - is a share of the image's reference length, so the picture at any size and the preview are one drawing at different scales, and the labels stay sharp at every zoom.
  - name: Summary
    kind: readout
    default: ""
    effect: Reads as `18 objects  ·  picture 7246 × 5429 px`, with `3 labelled` when some labels found no room, and `5 more stars marked` for the stars ringed without a name.
  - name: Message strip
    kind: readout
    default: ""
    effect: Says "Saved" and the file, or "Opened" and the view, after a picture, and "Stopped." after a **Cancel**.
    range: >-
      It also reads "The image states no astrometric solution. Plate solve it first.", "The catalogues could not be
      read:" and the reason, "The annotations could not be laid out.", "Choose a PNG, JPEG or TIFF file.", "A JPEG file
      cannot keep transparency: choose a PNG or TIFF file.", "The picture is larger than an image may be.", "The
      picture cannot be written there:" and the reason, "The image is being changed; try again once it is shown.",
      "The picture could not be made:" and the reason, "No view could be opened for the picture." and "The picture
      could not be opened:" and the reason.
    enabled: Hidden until there is something to say.
    note: >-
      When the catalogs cannot be read, the catalog files that come with AfterNight are damaged: install AfterNight
      again. Choose a file ending in `.png`, `.jpg` or `.tif` when the format is refused. When the image is being
      changed, wait until its view shows the change, then make the picture again.
  - name: Reset
    kind: button
    default: ""
    effect: Returns every choice to its default, the title included, and keeps the credit and your own colors.
  - name: Open as Image
    kind: button
    default: ""
    effect: Draws the picture and opens it in a new view named `<image>_annotated`.
    enabled: Disabled until the preview is ready, and while a picture is drawn. Asked while a layout is on its way, it waits for it and draws the choices as they are.
    scope: The new image is 8-bit display RGB, with alpha over a transparent background, recorded as nonlinear, and its history starts with **Annotate Image**. It keeps the telescope, camera, site, pointing, date and integration cards of the image and its solution moved to the picture's size, so it can be plate solved, measured and annotated again. A picture smaller than its image may keep the solution without its distortion terms.
    tradeoff: It is display-referred data with the annotations burned in. Keep the original for any further processing.
  - name: Export…
    kind: button
    default: ""
    effect: Draws the picture and writes it to the PNG, JPEG or TIFF file you choose in **Export Annotated Image**, which suggests `<image>_annotated.jpg`, or `.png` for a transparent background, in the folder of the last export.
    enabled: As **Open as Image**.
    scope: The file holds 8-bit RGB, or RGBA for a transparent background, written as the **Export** page of the settings writes that format - its JPEG quality, its TIFF compression and whether a raster file carries the image's metadata.
    tradeoff: A full-size picture of the 7246 by 5429 Carina master is drawn and written as an uncompressed TIFF in about half a second on a 16-thread desktop, and compression adds its own time. The file chosen is overwritten without a second question once the file window has asked.
  - name: Close
    kind: button
    default: ""
    effect: Closes the window and keeps the choices.
    note: One window per view. Asking for it again brings it forward, and it closes with its view. Its title, `Annotate Image <name>`, follows a rename of the image. The window can be maximized, and the rest of AfterNight stays at hand while it is open.
  - name: Progress
    kind: readout
    default: ""
    effect: A window titled **Export Annotated Image**, or **Annotate Image** when it opens the picture, with a bar that runs while the picture is drawn and written.
    note: Its **Cancel** stops the work, and nothing is written or opened.
```

## When it goes wrong

The **Annotate Image** button is missing, or the menu command is greyed
:   **Cause.** The view shows a [sequence](../reference/glossary.md#sequence), or its image has no solution AfterNight
    reads.
:   **Fix.** Plate solve the image with [Plate Solving](../processes/registration-alignment/plate-solving.md), or open
    a solved file.

"The image states no astrometric solution. Plate solve it first."
:   **Cause.** The image lost its solution while the window was open, by an edit, an undo or **Clear Astrometry**.
:   **Fix.** Plate solve it again. The window starts over by itself.

"A JPEG file cannot keep transparency: choose a PNG or TIFF file."
:   **Cause.** **Background** is **Transparent: annotations only** and the file named is a JPEG file.
:   **Fix.** Choose a PNG or TIFF file, or **The image** as the background.

"The picture could not be made:" and a reason
:   **Cause.** A step was refused, for memory, the file or the format, as the reason says.
:   **Fix.** Choose a smaller **Size** or another folder, and try again.

Some objects are marked but not labelled
:   **Cause.** Their labels found no room beside them or on a leader line.
:   **Fix.** Lower **Label size**, tick **Leader lines for crowded labels**, or choose less detail.

Stars are ringed but not named
:   **Cause.** **Mark the stars left unnamed** marks the stars **Star labels** does not name.
:   **Fix.** Choose **Every star, by number when unnamed**, or untick the marking.

## Related

- [Plate Solving](../processes/registration-alignment/plate-solving.md), which writes the solution this window needs.
- [Astrometry Overlays](astrometry-overlays.md), to see the same annotations in the view at the levels and palettes of
  this window.
- [Image Viewer](image-viewer.md), for the display mode and Autostretch the picture takes.
- [Saving And Exporting](saving-and-exporting.md), to save an opened picture in any format.
- [Settings](settings-dialog.md), for the **Export** page an exported picture is written by.
- [FITS Header, EXIF, And Astrometry Metadata](fits-header-viewer.md), for the cards the panel states.

## Credits

- E. W. Greisen and M. R. Calabretta, and M. R. Calabretta and E. W. Greisen,
  [the FITS World Coordinate System papers I and II](https://fits.gsfc.nasa.gov/fits_wcs.html), Astronomy &
  Astrophysics 395 (2002), which define the tangent-plane projection through which every position is drawn.
- [NGC 2000.0, catalog VII/118 at CDS/VizieR](https://cdsarc.cds.unistra.fr/viz-bin/cat/VII/118), by R. W. Sinnott
  after J. L. E. Dreyer, the source of the bundled deep-sky catalog's NGC and IC rows. The Messier names, common names,
  some sizes and the Caldwell rows are the project's additions; the Caldwell rows carry the limits
  [Astrometry Overlays](astrometry-overlays.md) states.
- [HYG Database 4.2](https://www.astronexus.com/projects/hyg), by Astronexus, under CC BY-SA 4.0, the source of the
  bundled bright-star catalog: the 83,479 stars to visual magnitude 9.
