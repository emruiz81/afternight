---
kind: interface
summary: >-
  What the view of a plate-solved image can draw over it: the equatorial grid, the deep-sky objects and bright stars
  of the bundled catalogs, the SIP distortion map and the detected stars. The image itself never changes.
---
# Astrometry Overlays

When an image carries a [plate solution](../reference/glossary.md#plate-solution), its view can draw the sky over the
picture: a grid of right ascension and declination, the galaxies, nebulae and clusters in the field, the names of its
bright stars and the distortion the solution corrects. **Show Astrometry**, on the view's floating toolbar, shows and
hides them; the Astrometry card of the [readout](../reference/glossary.md#readout) chooses each one and opens the
windows about the solution. The overlays are drawn on the view alone. The pixels, the header and the
[history](../reference/glossary.md#history) of the image never change, and the image is never marked unsaved. Use them
to check a solution, to find your way around a field and to name what it holds. [Annotate Image](annotate-image.md)
draws the same annotations into a picture you can keep.

```figure
owed: true
image: ../assets/images/interface/astrometry-overlays.webp
alt: >-
  The AfterNight main window with the Carina Nebula master in its view, shown in the Autostretch display mode. Thin
  blue lines of right ascension and declination cross the picture, labelled 10h 40m, 10h 45m and −59° 00′ at its
  edges. NGC 3372 is written in pink over the core of the nebula, and a large dashed pink circle around it runs off the
  top and bottom of the view. On the floating toolbar near the bottom, the star-shaped Show Astrometry button is
  pressed. In the readout at the right, the Astrometry card reads Solved · SIP 4/4 beside a settings button, then the
  field centre 10:44:30.67 and −59:53:31.7, a scale of 1.095″/px and a rotation of 107.75 deg; under them the chips
  Grid and Annotations, selected, Star labels, Detected stars and SIP map, and the buttons Plate Solve, Annotate... and
  Info. The status line at the bottom reads RA − and Dec −.
caption: >-
  The linear Carina master in Autostretch, fitted to its workspace, with the grid and the deep-sky annotations at the
  Standard level of detail, turned on from the readout's Astrometry card.
pins:
  - {n: 1, x: 5.3, y: 37.3}
  - {n: 2, x: 36.0, y: 48.2}
  - {n: 3, x: 54.3, y: 89.6}
  - {n: 4, x: 75.8, y: 64.5}
  - {n: 5, x: 96.6, y: 64.5}
  - {n: 6, x: 75.8, y: 76.8}
  - {n: 7, x: 75.8, y: 84.2}
  - {n: 8, x: 84.7, y: 98.5}
```

## What you see

1. **The equatorial grid.** Lines of constant right ascension and declination at round steps, labelled where they meet
   the edge of the part of the image the view shows.
2. **The deep-sky annotations.** Each galaxy, nebula and cluster of the bundled catalog in the field, outlined at its
   catalog size and labelled. An object larger than much of the image, as the Carina Nebula here, is a dashed region
   labelled inside itself.
3. **Show Astrometry.** On the floating toolbar: the switch that draws the overlays chosen, or hides them all.
4. **The Astrometry card.** "Solved", the solver and the [SIP](../reference/glossary.md#sip) orders, then the field
   centre, the scale and the rotation of the solution.
5. **Astrometry Settings.** The menu that ticks each overlay and chooses what the annotations name and how they look.
6. **The overlay chips.** **Grid**, **Annotations**, **Star labels**, **Detected stars** and **SIP map**, each the switch
   of one overlay. A chosen chip is filled.
7. **Plate Solve, Annotate... and Info.** The windows about the solution: Plate Solve, Annotate Image and Astrometry
   Info.
8. **The sky under the pointer.** The status line gives the right ascension and declination of the pixel under the
   pointer, which helps you check a position.

## What you can do

### Show the overlays

1. Make a solved still image the [active image](../reference/glossary.md#active-image). When its Astrometry card reads
   "Not solved", solve the image with **Plate Solve**, as
   [Plate Solving](../processes/registration-alignment/plate-solving.md) says, with
   **Write WCS to the current image metadata** ticked. Or open a [FITS](../reference/glossary.md#fits) or
   [XISF](../reference/glossary.md#xisf) file saved after a solve.
2. Click a chip of the Astrometry card, such as **Annotations**. **Show Astrometry** turns on and the overlay appears.
   On a new profile **Grid** is the one chip chosen, so **Show Astrometry** alone, or ++ctrl+shift+w++ over the
   picture, shows the grid.
3. Add the overlays one at a time, so that the labels stay readable. Ticking one in **Astrometry Settings** does the
   same and turns **Show Astrometry** on too. Over the picture, ++ctrl+1++ to ++ctrl+5++ do the same while **Show
   Astrometry** is on.
4. Open **Astrometry Settings** in the card's header, and choose **Level of Detail**, **Labels**, **Palette** and **Text
   Size**. The view follows each choice, and every view you open later starts with it.
5. Zoom and pan. The grid is spaced again for the part you see, and zooming in names smaller objects.
6. Press **Show Astrometry** again to judge the pixels alone. Your chips stay chosen for the next time.

### Check a solution

An overlay draws whatever the solution says, and draws a wrong solution as tidily as a right one. A ring on its star
or an outline on its nebula shows where the solution places the catalog entry: it does not show that the object is
really there. Check the center, the scale and the rotation in the Astrometry card, or in **Astrometry Info**. Then hold
the pointer over several known stars and objects across the field and compare their positions. When something is off,
turn the chips off one at a time to find the overlay that disagrees.

### Read the distortion map

Choose the **SIP map** chip. Each arrow starts at a pixel and points to where the SIP
polynomial of the solution moves it. With \((x, y)\) a pixel's position, the first pixel's center at \((0.5, 0.5)\),
and \(\mathrm{CRPIX1}\) and \(\mathrm{CRPIX2}\) the header's reference pixel, counted from one, the view measures from
the reference pixel

\[ u = x - (\mathrm{CRPIX1} - 0.5), \qquad v = y - (\mathrm{CRPIX2} - 0.5) \tag{1} \]

and, with \(A\) and \(B\) the forward polynomials of the header's `A_p_q` and `B_p_q` cards, draws the arrow from
\((x, y)\) toward

\[ x' = x + A(u, v), \qquad y' = y + B(u, v) \tag{2} \]

The arrows are scaled for the screen, so that small corrections can be seen. Read the size of the largest correction
from the legend, and compare directions and colors across the field, not lengths, to judge whether the distortion
model looks plausible for your optics.

```figure
owed: true
image: ../assets/images/interface/astrometry-overlays-sip.webp
alt: >-
  The Carina Nebula master in its view in the Autostretch display mode, covered by a regular grid of twenty by twenty
  arrows. Over most of the field the arrows are short and green. Toward the left and right edges they grow and turn
  yellow and orange, and the longest sit in the top left and bottom right corners. A small box at the top left of the
  picture reads SIP distortion, max, and a number of pixels, over the AUTOSTRETCH banner. The floating toolbar lies
  along the foot, its Show Astrometry button pressed.
caption: >-
  The SIP distortion map of the Carina master's solution, its grid chip turned off, with the legend that gives the
  largest correction in pixels.
```

## Controls

### Show Astrometry and the Astrometry card

```controls
window: Floating toolbar
controls:
  - name: Show Astrometry
    kind: toolbutton
    default: "Off"
    effect: Draws the overlays the chips of the Astrometry card choose over the picture, or hides them all and leaves the chips as they are.
    enabled: Shown only for a still image whose header states a solution AfterNight reads, never for a sequence, a task's canvas or a still preview. It appears and disappears as a solution is written into the image or removed from it. ++ctrl+shift+w++ over the picture does the same while the button is shown, and turning an overlay on - by its chip or by its entry in **Astrometry Settings** - turns it on.
    tradeoff: Several overlays at once can hide the image and add drawing work to every repaint. They are drawn without the graphics processor, and never wait for a network.
    scope: The view only. No pixel, header card, history step, mask, file or new view comes of it, and the image is not marked unsaved. Pixel type, channels, linear or stretched data, the view's channel and display mode, the mask and the zoom do not change the solution the overlays draw.
    persistence: Off in every new view, and never kept. Each view has its own.
    note: The view reads the solution once from the header, each time the image changes, and every overlay draws through it. The header must state a tangent-plane projection, `RA---TAN` and `DEC--TAN`, with `-SIP` or without, in degrees, in the ICRS. A header that states the J2000 equinox and no frame is read as ICRS. One that states FK5, another equinox and no frame, another projection or another axis order gives no solution and no overlay.
```

```controls
window: Astrometry
controls:
  - name: Astrometry Settings
    kind: toolbutton
    default: ""
    effect: 'Opens the menu of the overlays and of what the annotations name and how they look, the menu below. Its tooltip reads "Astrometry Settings: level of detail, labels, palette, text size".'
    enabled: While the readout reads an image's own view. An overlay ticked in the menu turns **Show Astrometry** on, as its chip does. The menu's shortcuts work only while **Show Astrometry** is on, over the picture.
    persistence: The menu closes after each choice. Every choice is kept at once, as the menu's block below says.
  - name: Grid
    kind: button
    default: "On"
    effect: Shows or hides the equatorial grid, as **Equatorial Grid** of the menu does, and turns **Show Astrometry** on when it turns the grid on.
    enabled: Shown for a solved image, as every chip is. Each chip follows its menu entry and its shortcut, whichever you use.
  - name: Annotations
    kind: button
    default: "Off"
    effect: Shows or hides the deep-sky annotations, as **Deep-Sky Annotations** does.
  - name: Star labels
    kind: button
    default: "Off"
    effect: Shows or hides the star labels, as **Star Labels** does.
  - name: Detected stars
    kind: button
    default: "Off"
    effect: Shows or hides the detected stars, as **Detected Stars** does.
  - name: SIP map
    kind: button
    default: "Off"
    effect: Shows or hides the distortion map, as **SIP Distortion Map** does.
  - name: Plate Solve
    kind: button
    default: ""
    effect: Opens Plate Solve, which can write a solution with its distortion into the image as a step of its history.
    enabled: Shown whether the image is solved or not. ++ctrl+p++ and **Image > Information > Plate Solve** do the same for a still image, never for a sequence. The window opens when no solver is set up, and its **Solve** stays disabled until one is.
    tradeoff: A solve uses processor time and catalog files on disk, and may run another program or upload the image, as the solver chosen does. The overlays add nothing to that cost.
    persistence: The solvers' settings are kept apart. A written solution, and its removal with **Clear Astrometry**, leave the image with unsaved changes; Saving And Exporting says how to keep it in a file before registration or color calibration read it.
  - name: Annotate...
    kind: button
    default: ""
    effect: Opens Annotate Image, which draws the annotations into a picture you export or open as an image.
    enabled: Shown for a solved image. ++ctrl+shift+l++ does the same over a solved still image.
  - name: Info
    kind: button
    default: ""
    effect: Opens Astrometry Info, the details of the solution. FITS Header, EXIF, And Astrometry Metadata describes the window.
    enabled: Shown for a solved image. ++ctrl+shift+p++ does the same over a solved still image.
```

### The Astrometry Settings menu

```controls
window: The Astrometry Settings menu
controls:
  - name: Equatorial Grid
    kind: check
    default: "On"
    effect: Draws lines of constant right ascension and declination over the part of the image the view shows, at the round step that puts about six of each across it, each labelled where it meets the edge of that part.
    range: The steps are hours, minutes and seconds of time for right ascension, and degrees, arcminutes and arcseconds for declination. The labels read as `08h 00m` or `+10° 00′`, with a true minus sign, primes and double primes, and seconds at the finer steps. A value is rounded once and the rounding carries, so 10h 29m 59.7s reads `10h 30m 00s`.
    enabled: ++ctrl+1++ over the picture, while **Show Astrometry** is on.
    tradeoff: The grid is measured from the sky the part shown covers, from its center and points along its edges and inside it, and is laid out again for every zoom, scroll and resize. A corner you zoom into gets a grid spaced for that corner. A fine grid of a narrow field adds work to each repaint.
    scope: Right ascension runs on across 0h, and a part of the image that holds a celestial pole draws every hour. Where the solution cannot place a part of a line, that part is left out, not joined across the view.
    persistence: Kept for every view opened later, under `platesolve/coordinateGridEnabled`, as soon as you choose it. A view already open keeps its own ticks.
    note: Drawn in the colors of **Palette**, with its labels at **Text Size**. It is the grid Annotate Image draws in a picture.
  - name: Deep-Sky Annotations
    kind: check
    default: "Off"
    effect: Outlines and labels the galaxies, nebulae and clusters of the bundled catalog that lie in the image and that **Level of Detail** names, with the text **Labels** chooses.
    range: An object of known size is an ellipse of its axes at its position angle, and one of no stated size a small circle. NGC 2000.0 gives one diameter and no angle, so an object is a circle of that diameter unless the project curated its extent; only those objects and the Caldwell rows carry an axis ratio and an angle. An object larger than much of the image is a dashed region labelled inside itself.
    enabled: ++ctrl+2++ over the picture, while **Show Astrometry** is on.
    tradeoff: Labels never cover one another. A label that finds no room beside its object moves out on a leader line, or is left out while the object keeps its outline. Wide fields and fuller levels give more marks and more layout work.
    scope: The catalog holds the NGC and IC objects of NGC 2000.0, their Messier names, 109 Caldwell rows, and common names and sizes the project added. The entries that name one object in several catalogs, as M 42 and NGC 1976, or NGC 3372 and C 92 for the Carina Nebula, are one annotation under the designation used most - Messier, then NGC, then IC, then Caldwell.
    persistence: As the grid, under `platesolve/catalogAnnotationsEnabled`.
    note: The outlines help you find your way and are no measured outlines of the objects. The Caldwell rows were added by the project with no record of their source or licence, and the generator that makes the catalog again does not make them, so take their positions, magnitudes, sizes and names as orientation data.
  - name: Star Labels
    kind: check
    default: "Off"
    effect: Rings and labels the stars of the bundled bright-star catalog that **Level of Detail** names, by proper name (`Vega`), by Bayer or Flamsteed designation (`α Lyr`) or by catalog number, with the text **Labels** chooses.
    range: The catalog holds the 83,479 stars of HYG 4.2 down to visual magnitude 9, where its Hipparcos core is close to complete. **Complete** names every one of them in the field, so it gets no denser.
    enabled: ++ctrl+3++ over the picture, while **Show Astrometry** is on. It labels the catalog's stars, not the ones **Detected Stars** marks.
    tradeoff: Stars are labelled in order, a star with a proper name before one with a designation alone, each brighter before fainter. A star whose label finds no room is not drawn, so a rich field shows its most prominent stars legibly. A star the level marks without a name is a ring with no label.
    scope: The positions are J2000 in the ICRS, without proper motion, so a fast-moving star in an image taken long after 2000 can sit a little off its ring.
    persistence: As the grid, under `platesolve/starLabelsEnabled`.
  - name: SIP Distortion Map
    kind: check
    default: "Off"
    effect: Draws an arrow at each of 20 by 20 points spread from the first pixel's center to the last's, from the pixel to where the solution's forward SIP polynomial carries it, and the largest correction in a legend.
    enabled: ++ctrl+5++ over the picture, while **Show Astrometry** is on. It draws nothing unless the header states forward `A` or `B` terms, with `A_ORDER` or `B_ORDER` from 2 to 6 and the reference pixel. Inverse terms alone, `AP` and `BP`, are not enough.
    scope: The whole image's model. The 400 arrows are worked out once for the image's size and solution and kept, and each repaint draws the ones in sight. No image is warped, and no map is exported.
    persistence: As the grid, under `platesolve/distortionMapEnabled`.
    note: Neither **Level of Detail**, **Labels**, **Palette** nor **Text Size** changes it.
  - name: Detected Stars
    kind: check
    default: "Off"
    effect: Draws an ellipse of each star the view detected in the image, of the star's fitted center, axes and angle, in the accent color, and in the error color for a saturated star.
    enabled: ++ctrl+4++ over the picture, while **Show Astrometry** is on. The view holds no stars for it to draw, as the note says.
    tradeoff: Up to 5,000 ellipses would be drawn at every repaint.
    persistence: As the grid, under `platesolve/detectedStarsEnabled`. Stars a view held would last until an edit of the image, **Clear Astrometry** or another image in the view.
    note: After a solve that writes its solution, the view starts detecting the image's stars for this overlay, but the update that shows the solution stops the detection, so the overlay stays empty. Opening a solved file starts no detection, and a duplicated view takes only the stars of its source. The detection reads channel 0 of a grayscale image, and the green channel of a color one, then the red when green gives fewer than 25 stars the solution places on the sky. It searches within 5 pixels at a noise sigma of 1 and a roundness of 0.4, fits a Gaussian and keeps at most 5,000 stars. A mosaic is not detected, a failure is written to the Session Console, and the stars are matched to no catalog.
  - name: Level of Detail
    kind: radio
    default: Standard
    options: [Essential, Standard, Detailed, Complete]
    effect: "Chooses how much of the two catalogs the annotations name, as the levels of Annotate Image do. **Essential**: the Messier and Caldwell objects and the named ones, and stars to magnitude 2.5 by proper name. **Standard**: every catalog to magnitude 12 and objects large enough to show, and stars to 4.5 by name and designation. **Detailed**: to magnitude 14 with smaller objects and objects of no measured magnitude, and stars to 6, the unnamed ones ringed. **Complete**: every galaxy, nebula and cluster the catalogs hold in the field, and every star, by catalog number when it has no name."
    range: A lower magnitude is brighter. The smallest object a level names is a share of the part of the image the view shows, as it is of a picture's frame, so zooming in names smaller objects. A Messier, Caldwell or named object is named whatever its size, and a named object of no measured magnitude at every level.
    tradeoff: Fuller levels crowd a wide field and take longer to lay out.
    scope: The deep-sky annotations and the star labels. The grid, the distortion map and the detected stars do not change with it.
    persistence: Kept for every view opened later under `annotation/view`, with **Labels**, **Palette** and **Text Size**, while a view already open keeps its own. Settings that kept a magnitude limit from before the levels start at the level that names about as much - 6 or brighter **Essential**, up to 12 **Standard**, up to 14 **Detailed**, fainter **Complete**.
    note: Each level's tooltip says what it names.
  - name: Labels
    kind: radio
    default: Designation
    options: [Designation, Name, Designation and Name]
    effect: Chooses what every catalog label says - the designation, or the name when there is none (`M 42`, `α Lyr`); the common name, or the designation when there is none (`Orion Nebula`, `Vega`); or the designation with the common name under it, as large as the designation.
    tradeoff: Two-line labels take more room, so fewer of them fit in a crowded field.
    scope: Both catalog overlays. The grid's labels are coordinates.
    persistence: As **Level of Detail**.
  - name: Palette
    kind: radio
    default: Aurora
    options: [Aurora, Moonlight, Glacier, Amethyst, Amber, Verdant, Rose, Atlas, "Ember - for dark-adapted eyes", "Ink - for bright images and print"]
    effect: Chooses the colors of the grid, the outlines, the star rings and the labels, the palettes of Annotate Image, with a color for each kind of object. **Ember** draws in reds for an eye adapted to the dark, and **Ink** dark lines for a bright image.
    scope: The grid and the catalog overlays. It is the view's own - a palette chosen in Annotate Image does not recolor a view.
    persistence: As **Level of Detail**.
  - name: Text Size
    kind: radio
    default: Medium
    options: [Small, Medium, Large]
    effect: Sets how large the catalog labels and the grid's labels are on the screen, at every zoom alike - **Small** as large as the interface's captions, **Medium** a quarter larger, **Large** half as large again. The star rings and the lines grow with the text.
    tradeoff: Larger text leaves room for fewer labels in a crowded field.
    scope: The view's own. The **Label size** of Annotate Image sets a picture's text, which follows the picture, not the screen.
    persistence: As **Level of Detail**. Settings kept before the sizes existed start at **Medium**.
```

### What the overlays draw

```controls
window: The overlays
controls:
  - name: The annotations
    kind: readout
    default: ""
    effect: The deep-sky outlines, the star rings and their labels, laid out together as one overlay, in the colors of **Palette**, Aurora until you choose another.
    range: Each catalog is read the first time its overlay is drawn and kept while the view is open. It is asked once for each solution and level, for the entries in the image's field and a margin, 2 degrees for deep-sky objects and 1 for stars, so an object centered outside the image that reaches into it is found. No network is needed, and the Gaia packs of the settings serve the solver alone.
    tradeoff: The reading, the asking and the layout run in the background, and the view never waits for them, so the labels appear on the repaint after the answer lands. A new zoom, **Labels**, **Level of Detail** or **Text Size** lays the labels out again, and the old layout is drawn until the new one lands. Scrolling moves the layout there is.
    note: A position is drawn at the center of the pixel it names. Renaming or changing the image sets the annotations aside, and they are drawn again. A catalog that cannot be read is written once to the Session Console, and its overlay draws nothing.
  - name: SIP distortion max
    kind: readout
    default: ""
    effect: The legend at the top left of the part shown, as `SIP distortion  max 3.42 px`, the largest correction among the 400 points, in pixels of the image, to two decimals.
    range: An arrow's color runs from the success color through the warning color to the error color as its correction nears that largest one.
    tradeoff: The arrows are scaled for the screen. The longest is drawn at about 12 % of the view's shorter side, held between 24 and 96 screen pixels, by a factor held between 0.75 and 40. Only the legend is a measure.
```

## When it goes wrong

**Show Astrometry** and the overlay chips are missing
:   **Cause.** The view shows a [sequence](../reference/glossary.md#sequence), or the header states no solution
    AfterNight reads: none, a projection other than the tangent plane, or the FK5 frame. The Astrometry card then
    reads "Not solved".
:   **Fix.** Make a still image active, look at its header with **FITS Header** in the readout's Image card, and solve
    it with **Plate Solve**.

++ctrl+1++ to ++ctrl+5++ do nothing
:   **Cause.** **Show Astrometry** is off, or the picture does not have the keyboard.
:   **Fix.** Press **Show Astrometry**, or a chip, then click the picture before you use a shortcut.

The grid shows but no object or star is named
:   **Cause.** The catalog overlay is not chosen, the level names nothing in this field, or the catalog could not be
    read.
:   **Fix.** Choose the **Annotations** or **Star labels** chip, choose **Detailed** or **Complete** in **Astrometry
    Settings**, and look in the Session Console for a catalog message. The catalog holds NGC, IC, Messier and Caldwell
    objects alone, so the Carina field, for one, holds a single object.

Small objects and some stars are named only when you zoom in
:   **Cause.** The smallest object a level names is a share of what the view shows, and a star whose label finds no
    room is not drawn.
:   **Fix.** Zoom in, choose a fuller level, or a smaller **Text Size**.

**Detected stars** is chosen but no ellipse appears
:   **Cause.** The view holds no detected stars: the detection a solve starts is stopped when the solution lands.
:   **Fix.** Use [Star Profiler](../processes/registration-alignment/star-profiler.md), which detects the stars and
    draws an ellipse on each over the view.

**SIP map** is chosen but no arrow appears
:   **Cause.** The solution has no forward distortion terms, only inverse ones, or terms that correct nothing.
:   **Fix.** Check `A_ORDER` and `B_ORDER` in the header. The other overlays work without them, and a solve can fit
    them.

## Related

- [Plate Solving](../processes/registration-alignment/plate-solving.md), to write the solution the overlays draw.
- [Annotate Image](annotate-image.md), to draw the same annotations into a picture.
- [Readout](readout.md), for the Astrometry card beside the other cards of the active image.
- [Image Viewer](image-viewer.md), for the floating toolbar, the zoom and the display mode.
- [FITS Header, EXIF, And Astrometry Metadata](fits-header-viewer.md), for the solution's cards and **Astrometry Info**.
- [Star Profiler](../processes/registration-alignment/star-profiler.md), to see the stars detected in an image.

## Credits

- E. W. Greisen and M. R. Calabretta, and M. R. Calabretta and E. W. Greisen,
  [the FITS World Coordinate System papers I and II](https://fits.gsfc.nasa.gov/fits_wcs.html), Astronomy &
  Astrophysics 395 (2002), which define the tangent-plane projection the overlays draw through.
- D. L. Shupe and R. N. Hook,
  [The SIP Convention for Representing Distortion in FITS Image Headers](https://fits.gsfc.nasa.gov/registry/sip/SIP_distortion_v1_0.pdf),
  version 1.0, submitted to the IAU FITS convention registry, and D. L. Shupe et al.,
  [the same convention at ADASS XIV](https://authors.library.caltech.edu/records/fqnyw-qdw79/latest) (2005): the
  polynomial of equations (1) and (2).
- [NGC 2000.0, catalog VII/118 at CDS/VizieR](https://cdsarc.cds.unistra.fr/viz-bin/cat/VII/118), by R. W. Sinnott
  after J. L. E. Dreyer, the source of the bundled deep-sky catalog's NGC and IC rows. The Messier names, common names,
  some sizes and the Caldwell rows are AfterNight's additions.
- [HYG Database 4.2](https://www.astronexus.com/projects/hyg), by Astronexus, under CC BY-SA 4.0, the source of the
  bundled bright-star catalog.
