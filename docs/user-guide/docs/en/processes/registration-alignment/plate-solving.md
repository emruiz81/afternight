---
kind: process
stage: register
menu: Image > Information > Plate Solve
summary: >-
  Finds where an image points on the sky by matching its stars to a catalog, shows the solution and writes it into
  the image's header, with the internal solver or an external one.
data_in: either
data_out: either
destructive: true
preview: none
sequences: false
masks: none
external: A Gaia DR3 catalog pack, or ASTAP, astrometry.net's solve-field or an astrometry.net API key
glance:
  enabled: >-
    A still image view is active. A sequence view offers neither the menu command nor the view's Plate Solve button.
    The window opens without a solver, but Solve stays disabled until one is set up.
  inputs: >-
    The image the view shows, unsaved edits included, the position and optics its header states or you type, and the
    solvers, catalog and limits chosen in the settings.
  outputs: >-
    The solution's figures in the window. With Write WCS to the current image metadata ticked, the same view with the
    solution in its header and one history step: its pixels, size and pixel type are kept. No file, report or new
    image is written.
  state: >-
    A calibrated image, linear or stretched, in the geometry later steps use, with sharp and unsaturated stars: a
    master or a single frame.
derived: true
---
# Plate Solving

Plate Solving finds where an image points on the sky. It detects the image's stars, matches them to a star catalog
and fits a [plate solution](../../reference/glossary.md#plate-solution), from which the window shows the field's
center, its [pixel scale](../../reference/glossary.md#pixel-scale), its rotation and its size. The internal solver
reads a [Gaia](../../reference/glossary.md#gaia) catalog pack installed on your computer. ASTAP, astrometry.net's
`solve-field` and the astrometry.net web service can solve in its place.

With **Write WCS to the current image metadata** ticked, **Solve** writes the solution into the image's
[header cards](../../reference/glossary.md#header-card) as [WCS](../../reference/glossary.md#wcs) and adds one step to
its [history](../../reference/glossary.md#history). The pixels are not changed, and nothing reaches a file until you
save the image.

```figure
owed: true
image: ../../assets/images/processes/registration-alignment/plate-solving.webp
alt: >-
  The Plate Solve window. Under its title, a line saying what it does. The Acquisition Hints section is open: RA at
  10h 44m 30.671s, Dec at S 59 degrees 53 minutes 31.732 seconds, a Find object button, Focal length near 435 mm and
  Pixel size at 2.31 µm. Below it the Solved Field box lists the field center, image scale, rotation, field of view,
  Internal Gaia as the solver and the SIP distortion. Then the Clear Astrometry button, the ticked Write WCS to the
  current image metadata check box, and Reset Hints, Solve and Close along the bottom.
caption: >-
  Plate Solve after a solve of the Carina master with the Gaia DR3 G<=14 pack: the hints its header gave, the focal
  length the solve derived and the solution it found and wrote.
pins:
  - {n: 3, x: 3.0, y: 22.2}
  - {n: 4, x: 26.0, y: 34.6}
  - {n: 5, x: 3.0, y: 90.4}
  - {n: 6, x: 53.0, y: 95.0}
  - {n: 7, x: 3.0, y: 49.3}
```

## Before you start

- Set up a solver once. The internal solver needs a Gaia catalog pack, installed on the **Catalogs & Data** page of the
  settings. ASTAP, `solve-field` and the web service are set on the **Plate Solving** page. A solver counts as set up
  when its pack is complete, its program exists or its key is filled in: that is not a test that it can solve.
- Solve the image in the geometry later steps use, with sharp and unsaturated stars, and solve it again after an edit
  that leaves it without a solution. Linear and stretched images are both accepted.
- Keep the pointing and the optics in the header: `RA` and `DEC` or `OBJCTRA` and `OBJCTDEC`, `FOCALLEN` and `XPIXSZ`.
  Without a position the solvers search the whole sky, which is slow and can fail for a narrow field.
- A [sequence](../../reference/glossary.md#sequence) cannot be solved here, and a
  [mask](../../reference/glossary.md#mask) on the view is not read. Image Registration and Image Integration solve
  their frames themselves.
- A solve replaces the solution the header had, whichever program wrote it. The history keeps the step before. Save a
  copy first when the old header must stay.
- The web service receives the whole image. Use it only where your rules allow that upload.

## Steps

1. **Set up a solver.** Choose **Edit > Settings...**, open **Catalogs & Data** and press **Download Catalog...** under
   **Gaia DR3 Astrometric Catalogs**. For the recommended pack the page then reads "Selected Gaia catalog: Gaia DR3
   G<=14 (gaia_dr3_g14)".
2. **Open the window.** Make the image the [active image](../../reference/glossary.md#active-image) and choose
   **Image > Information > Plate Solve**, press **Plate Solve** in the view's toolbar, or press ++ctrl+p++ over the
   view. The window opens and names the image in its title, as `Plate Solve <RGB>` for an image named RGB.
3. **Check the hints.** Open **Acquisition Hints** and check that **RA**, **Dec**, **Focal length** and **Pixel size**
   hold the target's position and your optics. The section opens by itself when the header lacks one of them.
4. **Find the target if no position shows.** Press **Find object...**, search for the target's name and press **OK**
   on the right result. **RA** and **Dec** take its coordinates.
5. **Choose whether to write.** Leave **Write WCS to the current image metadata** ticked to put the solution into the
   image. Untick it to see the solution without changing the image.
6. **Solve.** Press **Solve**. A bar runs above **Acquisition Hints**, **Close** reads **Cancel** and the status line
   reads "Plate solving...".
7. **Read the result.** **Solved Field** shows the field's center, scale, rotation and size, the solver and the
   distortion, and **Focal length** the focal length the solve derived. Compare **Image scale** with the scale of your
   optics, equation (1).
8. **Look at it on the view.** Press **Show Astrometry** in the view's toolbar, or ++ctrl+shift+w++, and the view draws
   the sky's coordinate grid over the image. The status bar gives the sky position under the pointer, and **Astrometry
   Info**, ++ctrl+shift+p++, lists the solution.
9. **Save the image.** Save it as [FITS](../../reference/glossary.md#fits) or
   [XISF](../../reference/glossary.md#xisf), which keep every header card. Open the file again later, and Plate Solve
   shows its solution as soon as the window opens.

## Controls

Plate Solve is a window of the image's view, not a process window: it has no preview, no **Target Image** and no
**Apply**. Like a [process window](../../interface/process-windows.md), it names its image in its title and says what
it does under it. The solvers and their limits are set in the [Settings](../../interface/settings-dialog.md): the
block **The settings the solve reads** below gives what each solver is sent, and **Messages** what a failure says.

```controls
window: Plate Solve
controls:
  - name: Status line
    object: plateSolveStatus
    kind: readout
    default: ""
    effect: >-
      Reads "Plate solving..." while a solve runs, then "Solved with" the solver "in" the time in milliseconds, with
      "— focal length updated to" the derived focal length when **Focal length** changed. It also says
      "Cancelling plate solve...", "Plate solve cancelled.", "Filled RA/Dec hints from" an object, "Astrometry data has
      been removed." and "Plate solve finished, but the active image changed.".
    range: The time covers every solver tried, the failed ones included.
    enabled: >-
      Emptied at every change of the view's image. A solution written into the image is such a change, so with **Write
      WCS to the current image metadata** ticked the result shows only for a moment. Unticked, it stays.
    tradeoff: >-
      It is not the solve's log. The Session Console lists each solver tried, its arguments and its answer. A failed
      solve leaves it empty and opens a message instead.
  - name: No plate solving tool is configured. Set up ASTAP or astrometry.net in External Tools settings to enable plate solving.
    kind: readout
    default: ""
    effect: Says why **Solve** is disabled, beside **Configure...**.
    enabled: >-
      Shown while no solver is set up and no solve runs. It is checked again when the settings opened from
      **Configure...** close and when the view's image changes or is renamed.
    tradeoff: >-
      Its text names ASTAP, astrometry.net and the External Tools settings, but the Gaia pack is installed on the
      **Catalogs & Data** page and the external solvers are set on the **Plate Solving** page.
  - name: Configure...
    kind: button
    default: ""
    effect: Opens the settings at their **Plate Solving** page, and checks the solvers again when they close.
    enabled: Shown with the banner above only.
    tradeoff: A solver the check counts as set up can still fail at its first solve.
  - name: Message banner
    object: plateSolveBanner
    kind: readout
    default: ""
    effect: >-
      Shows the blind-solve warning, "No RA/Dec coordinates provided. Blind solving a narrow-field image is very slow
      and may fail. Consider filling in RA and Dec hints, or use “Find object…” to resolve the target name.", the
      solver's words after a cancelled solve, "Plate solve finished for an image that is no longer active. The result
      was not applied.", and "Failed to create a temporary path for plate solving.".
    enabled: >-
      The warning shows when **Solve** starts with no position in the window and none in the header, and stays until
      the solve's answer replaces it. It neither stops the solve nor asks before it starts.
    persistence: Until the next solve, a found object, **Clear Astrometry** or a change of the image while the window shows no solve of its own.
    note: For the temporary path, check that the **Temporary Files** location exists and can be written.
  - name: Progress
    kind: readout
    default: ""
    effect: A bar that runs back and forth while a solve works.
    enabled: Shown only while a solve runs.
    tradeoff: It shows no share done and does not name the solver at work.
  - name: Acquisition Hints
    kind: check
    default: "Off"
    effect: Opens or closes the section that holds the position and the optics. Closing it hides the boxes and keeps their values in use.
    range: >-
      It opens by itself when the header states no position, or not both a focal length and a pixel size, and when a
      solve or **Find object...** fills a box.
    enabled: Disabled, with every box in it, while a solve runs.
    persistence: >-
      The boxes are filled from the header when the window opens, and again when the image changes or is renamed while
      the window shows no solve of its own. Nothing is kept when the window closes.
  - name: RA
    object: plateSolveRaHours
    kind: spinbox
    default: The header's, else 0
    widget: {min: 0, max: 23, step: 1, decimals: 0}
    effect: Sets the hours of the target's right ascension. One hour is 15 degrees of sky.
    range: >-
      When every **RA** and **Dec** box is 0, the window states no position and the solve takes the header's. When any
      box is not 0, the six boxes are the position, a box left at 0 meaning 0.
    tradeoff: >-
      A wrong position sends the search to the wrong sky: it then takes longer or fails. A header that writes its right
      ascension as one decimal number in hours is read as degrees, so check the box against your target.
    scope: >-
      The position every solver is given. A settings file imported with `platesolve/useHints` off, a key no page of the
      settings shows, makes every solver ignore the position, and the external solvers the scale too, while the boxes
      still show them.
    persistence: Until the window closes. **Reset Hints** returns it to 0.
    note: >-
      The window and the solve read the first pointing the header states, a solution's reference direction first:
      `CRVAL1` and `CRVAL2`, `RA` and `DEC`, `OBJCTRA` and `OBJCTDEC`, `OBJRA` and `OBJDEC`, then `TELRA` and `TELDEC`. One
      decimal number is degrees. Two or three fields are hours or degrees, minutes and seconds, separated by blanks,
      colons, the letters `h`, `d`, `m` and `s` in either case, the degree sign or the arcminute and arcsecond marks. A
      right ascension of 24 hours or 360 degrees reads as 0. A fraction in a field that another field follows, as in
      `12.5 30`, gives no position, and the next pair is read.
  - name: RA
    object: plateSolveRaMinutes
    kind: spinbox
    default: The header's, else 0
    widget: {min: 0, max: 59, step: 1, decimals: 0}
    effect: Sets the minutes of the right ascension.
    persistence: As the hours.
  - name: RA
    object: plateSolveRaSeconds
    kind: spinbox
    default: The header's, else 0.000
    widget: {min: 0, max: 59.999, decimals: 3}
    effect: Sets the seconds of the right ascension.
    range: A position from the header or a found object is rounded once to a thousandth of a second, and the rounding carries into the minutes and hours.
    tradeoff: More digits than the pointing is good for do not help the search.
    persistence: As the hours.
  - name: Dec
    object: plateSolveDecHemisphere
    kind: combo
    default: N (+)
    options: [N (+), S (−)]
    effect: Chooses the sign of the declination, north or south of the celestial equator.
    tradeoff: The wrong hemisphere sends the search to the wrong sky.
    persistence: As the right ascension. **Reset Hints** returns it to **N (+)**.
  - name: Dec
    object: plateSolveDecDegrees
    kind: spinbox
    default: The header's, else 0
    widget: {min: 0, max: 90, step: 1, decimals: 0}
    effect: Sets the degrees of the declination.
    range: >-
      At 90 the minutes and seconds can still be set. A declination beyond 90 degrees in all is no position on the sky:
      the solvers then get none, and no warning says so.
    persistence: As the right ascension.
  - name: Dec
    object: plateSolveDecMinutes
    kind: spinbox
    default: The header's, else 0
    widget: {min: 0, max: 59, step: 1, decimals: 0}
    effect: Sets the arcminutes of the declination.
    persistence: As the right ascension.
  - name: Dec
    object: plateSolveDecSeconds
    kind: spinbox
    default: The header's, else 0.000
    widget: {min: 0, max: 59.999, decimals: 3}
    effect: Sets the arcseconds of the declination.
    range: Rounded once to a thousandth of an arcsecond, the rounding carried into the arcminutes and degrees.
    persistence: As the right ascension.
  - name: Find object...
    kind: button
    default: ""
    effect: Opens **Find Object**, which searches for a target by its name, and copies the coordinates of the result you choose into **RA** and **Dec**.
    tradeoff: >-
      **Local Catalog** searches the catalogs that come with AfterNight, without a network. The other servers receive
      the name you type. The window waits while **Find Object** is open.
    scope: The position only. Focal length and pixel size are left as they are.
    note: Its tooltip speaks of online catalogs, but the search starts on **Local Catalog**.
  - name: Focal length
    kind: spinbox
    default: The header's, else 0.00
    widget: {min: 0, max: 50000, decimals: 2}
    effect: >-
      Sets the focal length the scale hint is computed from, in millimetres. With the pixel size, a shorter focal
      length gives a larger scale, equation (1).
    range: >-
      Read from `FOCALLEN`, else `FOCAL`. At 0 it gives no hint of its own: the solve then takes a focal length the
      settings store without showing it on any page, else the header's.
    enabled: A scale needs both a focal length and a pixel size.
    tradeoff: >-
      A wrong scale can keep an external solver from the right answer, which it looks for within 20 % of the hint.
      The internal solver without any scale assumes 2 arcseconds per pixel, or the scale of a solution the header
      already states.
    persistence: >-
      A solve whose answer gives a scale, with a pixel size known, writes the derived focal length into the box,
      equation (2). With **Write WCS to the current image metadata** ticked it is written to `FOCALLEN`, to six
      decimals. The focal length stored in the settings is not changed.
  - name: Pixel size
    kind: spinbox
    default: The header's, else 0.00
    widget: {min: 0, max: 100, decimals: 2}
    effect: Sets the camera's pixel size, in micrometres. With the focal length, larger pixels give a larger scale.
    range: >-
      Read from `XPIXSZ`, else `PIXSIZE`, else `PIXSIZE1`, and rounded to two decimals: a header's 2.315 shows and is
      used as 2.31. At 0 the solve takes a pixel size the settings store, as for the focal length, else the header's.
    tradeoff: >-
      A wrong pixel size, or a binned or drizzled frame read as unbinned, biases the scale. A drizzled header's own
      scale, `D001SCAL`, is used in place of the optics' when it is there. For the derived focal length, the pixel
      size is first divided by the header's `DRIZMULT`, or by how much finer `D001SCAL` is than the optics' scale.
    persistence: >-
      With **Write WCS to the current image metadata** ticked, a value above 0 is written to `XPIXSZ` and `YPIXSZ`, to
      six decimals, rounded as the box shows it.
  - name: Focal length and pixel size are needed...
    kind: readout
    default: ""
    effect: >-
      Says which of the two boxes is 0: "Focal length and pixel size are needed to provide the scale information
      required for plate solving.", or the same for the focal length or the pixel size alone.
    enabled: Shown while either box is 0.
    tradeoff: It looks at the two boxes only, not at values stored in the settings, and it does not disable **Solve**.
  - name: Field center RA
    object: plateSolveFieldCenterRa
    kind: readout
    default: "-"
    effect: >-
      The right ascension at the middle of the frame, which can differ from the solution's reference direction,
      `CRVAL1`. After a solve it reads as `10h 44m 30.67s`. Before one, it shows the header's solution as Astrometry
      Info writes it, `10:44:30.67`.
    range: Seconds to a hundredth, rounded once and carried into the minutes and hours.
    enabled: A dash while the image has no solution and during a solve.
  - name: Field center Dec
    object: plateSolveFieldCenterDec
    kind: readout
    default: "-"
    effect: The declination at the middle of the frame, signed, to a tenth of an arcsecond, in the same two forms.
    enabled: As **Field center RA**.
  - name: Image scale
    kind: readout
    default: "-"
    effect: The scale of a square pixel covering the same sky as one of the image's, in arcseconds per pixel to three decimals.
    tradeoff: >-
      Compare it with the optics, equation (1). A scale far from theirs points to a wrong solution, or to wrong optics
      in the header.
  - name: Rotation
    kind: readout
    default: "-"
    effect: >-
      Which way celestial north points: the angle from the direction of increasing rows toward decreasing columns, at
      the reference pixel, from 0 to 360 degrees. FITS draws the first row at the bottom, so there 0 means north up
      and 90 north to the left.
    range: To two decimals as `107.73 deg` after a solve, to one as `107.7° (N through E)` before one, the angle Astrometry Info gives.
  - name: Field of view
    kind: readout
    default: "-"
    effect: >-
      The width and height of the frame on the sky, in arcminutes: `132.22 x 99.16 arcmin` after a solve,
      `132.22′ × 99.16′` before one.
    tradeoff: A linear estimate along the image's axes, which leaves the projection's curvature and the distortion out.
  - name: Solver
    object: plateSolveSolver
    kind: readout
    default: "-"
    effect: The solver that answered, as `Internal Gaia`, `ASTAP`, `solve-field` or `Astrometry.net (web)`.
    range: >-
      Before a solve, the header's `PLTSOLVR` card, or `Unknown` for a solution written without one.
    tradeoff: It names neither the solver's version nor the catalog or indexes it used.
  - name: SIP distortion
    kind: readout
    default: "-"
    effect: >-
      Whether the solution carries a distortion polynomial: `Yes (A/B order 4/4)` or `No` after a solve, `A/B 4/4, 14
      coeffs` or `None` before one, with `AP/BP` added when the header has the inverse terms.
    tradeoff: >-
      A solution without one is a valid tangent-plane solution. The internal solver keeps a polynomial only when it
      improves the fit, and an external solver's is kept as it answered.
  - name: Clear Astrometry
    kind: button
    default: ""
    effect: >-
      Asks "This will remove all astrometry (WCS) data and the plate solver tag from the current image." and, on
      **Yes**, removes every world-coordinate card and `PLTSOLVR` from the image as one history step.
    range: >-
      It removes the projection, unit, reference, matrix, frame and equinox cards and the distortion's orders, bounds
      and coefficients, the same cards a written solution replaces, and an XISF image's astrometric-solution
      properties. It keeps the pixels, the file's name, the acquisition cards and every other card, `FOCALLEN` and
      `XPIXSZ` included. The question's default answer is **No**, which leaves the image as it is.
    enabled: While the image's header states a solution and no solve runs.
    scope: The image in the view. The view's astrometry buttons and overlays leave with the solution.
    persistence: >-
      The history lists **Clear Astrometry**, described "Removed the astrometric solution", the lineage gains a
      `clearAstrometry` entry, and **Undo** brings the solution back. The image then has unsaved changes, and the file
      keeps the solution until you save.
  - name: Write WCS to the current image metadata
    kind: check
    default: "On"
    effect: >-
      Ticked, a solve writes its solution into the image as one history step. Unticked, the window only shows the
      solution, and the image is not changed.
    enabled: Disabled while a solve runs.
    range: >-
      The solution is written as `RADESYS = 'ICRS'` with no `EQUINOX`, the `RA---TAN` and `DEC--TAN` projection, with
      `-SIP` when it carries a polynomial, `CUNIT1` and `CUNIT2` in degrees, `CRPIX`, `CRVAL`, the four `CD` entries,
      and the polynomial's orders and nonzero coefficients. Each number is the shortest decimal that reads back as
      itself, and the solver's own comments are not copied. Astrometry Info then shows ICRS where an equinox would be.
    scope: >-
      The image the solve read, only when it is still the one the view shows. Every world-coordinate card is replaced
      and every other card kept in its order, whichever solver answered.
    persistence: >-
      The history lists **Plate Solve**, described "Plate solved with" the solver, the lineage gains a `plateSolve`
      entry, and **Undo** brings the image back. The image then has unsaved changes. The box is ticked again in every
      new window, and the file keeps the solution only once you save it.
    note: >-
      The view then starts detecting the image's stars for its **Detected Stars** overlay, but the update that shows
      the solution stops that detection, so the overlay finds no stars to mark.
  - name: Reset Hints
    kind: button
    default: ""
    effect: Sets every **RA** and **Dec** box to 0 and **N (+)**, and **Focal length** and **Pixel size** to 0, and opens **Acquisition Hints**.
    range: >-
      It does not read the header again. With the boxes at 0, the solve still takes the header's position and optics,
      and any focal length and pixel size the settings store. It leaves **Write WCS to the current image metadata** as
      it is.
    enabled: Disabled while a solve runs.
  - name: Solve
    kind: button
    default: ""
    effect: Writes the image the view shows to a temporary file and runs the solvers on it, with the hints the window holds.
    range: >-
      It always solves anew: a solution the header already states is never returned in its place. The file is a
      `solve_input_` file of a new name for each solve, in `.afternight/plate_solve/temp` inside the session's folder
      of the location **Temporary Files** names on the **General** page of the settings.
    enabled: >-
      With a still image in the view, a solver set up and no solve running. Pressed without an image, it says "No
      image is loaded.".
    tradeoff: >-
      A solve uses processor time, reads the catalog or the indexes from disk, may start an external program or upload
      the image, and adds the time of every solver that fails before one answers. The internal solver takes longer for
      a larger image, more channels, more stars and more trial centers: a good position and scale save the most. Try a
      new kind of field on one image before a large registration run depends on it.
    scope: The whole image, every channel. No mask, no part of it, no sequence.
    persistence: >-
      Each external solver works in a folder of its own under the same `.afternight/plate_solve`, removed when it is
      done. While a solve runs,
      the image lies in the temporary folder, so keep that folder private.
  - name: Close
    kind: button
    default: ""
    effect: >-
      Closes the window. While a solve runs it reads **Cancel**: the status line reads "Cancelling plate solve...", and
      no further solver is tried.
    range: >-
      The window's own close button, during a solve, stops it too and closes the window once the solve has stopped. A
      solve that had already finished when the stop arrived is shown and written, and the window stays open.
```

```controls
window: Find Object
controls:
  - name: "Server:"
    kind: combo
    default: Local Catalog
    options: [Local Catalog, SIMBAD, CDS Sesame, VizieR Sesame]
    effect: >-
      Chooses where the name is looked up: the deep-sky and bright-star catalogs that come with AfterNight, or one of
      the three services of the Strasbourg astronomical data center.
    enabled: Disabled while a search runs.
    tradeoff: >-
      The online servers need a network and secure connections, and receive the name you search for: use **Local
      Catalog** when the name must stay private. Each answer is awaited 15 seconds at most.
  - name: "Object:"
    kind: field
    default: The header's OBJECT, else empty
    effect: The name to search for. ++enter++ starts the search.
    range: >-
      It offers the last 20 names you accepted with **OK**, whatever their case, as you type. It is filled from the
      header's `OBJECT` card when the window opens.
    enabled: Disabled while a search runs.
    persistence: The accepted names are kept between sessions.
  - name: Search
    kind: button
    default: ""
    effect: Searches the chosen server for the name, after clearing the last results.
    enabled: While **Object:** holds a name and no search runs.
  - name: Search progress
    object: catalogProgressBar
    kind: readout
    default: ""
    effect: A bar that runs while a search works, without a share done.
  - name: Name, Type, RA and Dec
    kind: action
    default: ""
    effect: >-
      Lists what the server found, one object a row, with its right ascension in hours and its declination in degrees.
      Click a row to choose it, or double-click it to accept it at once.
    tradeoff: Similar names can be different objects. Check the coordinates before you accept one.
  - name: "Description / Notes:"
    kind: readout
    default: ""
    effect: What the server says of the chosen object, such as its magnitude, size or type, when it says anything.
  - name: "Other names:"
    kind: readout
    default: ""
    effect: The chosen object's other designations, one a line.
  - name: Search status
    object: catalogStatusLabel
    kind: readout
    default: Enter an object name and choose a server.
    effect: >-
      Reads "Searching" and the server while a search runs, then "Found" and the count, "No matching objects were
      found.", "Catalog search timed out after 15 seconds." or the server's or the network's error.
    range: >-
      Without the secure-connection libraries it reads "Secure catalog access is unavailable because TLS/SSL support
      could not be initialized.", and only **Local Catalog** can search.
  - name: OK
    kind: button
    default: ""
    effect: Closes the window, copies the chosen object's coordinates into **RA** and **Dec**, and adds its name to the names **Object:** offers.
    enabled: With a row chosen and no search running.
  - name: Cancel
    kind: button
    default: ""
    effect: Closes the window without changing the hints, and stops a search that runs.
```

```controls
window: On the image's view
controls:
  - name: Plate Solve
    kind: toolbutton
    default: ""
    effect: Opens Plate Solve for the view, or brings the window that is already open for it to the front. ++ctrl+p++ over the view does the same.
    enabled: Shown for a still image, hidden for a sequence. The shortcut works only while the button is shown.
  - name: Show Astrometry
    kind: toolbutton
    default: "Off"
    effect: Draws the overlays the view's **Astrometry Settings** menu ticks over the image, the coordinate grid until you choose others. ++ctrl+shift+w++ toggles it.
    enabled: Shown once the image has a solution.
    tradeoff: The overlays draw the solution as it is. They show a wrong one as readily as a right one.
  - name: Astrometry Info
    kind: toolbutton
    default: ""
    effect: Opens the summary of the image's solution, as its header states it. ++ctrl+shift+p++ does the same.
    enabled: Enabled once the image has a solution.
```

```controls
window: The settings the solve reads
controls:
  - name: "Default solver:"
    kind: combo
    default: Internal Gaia
    options: [Internal Gaia, ASTAP, Astrometry.net (local), Astrometry.net (web)]
    effect: >-
      Chooses the solver tried first. If it fails, the others are tried in the order Internal Gaia, ASTAP,
      Astrometry.net (local) and Astrometry.net (web), each one that is set up.
    range: >-
      Only a solver that is set up can be chosen. Without a choice, the first that is set up in that order leads, and
      the box reads "No configured solver" when none is. **Astrometry.net (local)** is offered on Linux only.
    tradeoff: A solver you set up becomes a fallback whether or not you choose it. Remove the web key to keep the image off the network.
    scope: On the **Plate Solving** page of the settings, under **General**.
  - name: "Search radius:"
    kind: spinbox
    default: "10.0"
    widget: {min: 1, max: 180, decimals: 1}
    effect: How far from the hinted position, in degrees, the solvers look for the field.
    range: >-
      The internal solver tries the hint first, then 3 rings of 12 trial centers out to the radius, each ring at its
      distance on the sky: 180 reaches the opposite side of the sky. It reads the Gaia stars once, within the radius
      and one and a half catalog circles of the hint.
    tradeoff: >-
      A larger radius forgives a poor position and costs time, and widens the catalog the internal solver reads, which
      a dense part of the sky can make too large.
    scope: >-
      Every solver given a position: ASTAP's search, `solve-field`'s radius, the web service's, and the internal
      solver's rings of trial centers.
    note: >-
      Without a position, the internal solver tries the centers of a grid over the whole sky, spaced for the field, each
      with the Gaia stars within two catalog circles. It stops at the first trial with at least 12 matched stars and a
      residual of at most 1 pixel, and otherwise keeps the trial with the most matches.
  - name: "Downsample:"
    kind: spinbox
    default: "2"
    widget: {min: 1, max: 8, step: 1}
    effect: How many pixels an external solver bins into one before it searches. Higher is faster and loses small, faint stars.
    tradeoff: >-
      Keep 2 for an ordinary field. Raise it a step for large, well-sampled stars, and lower it after a failure.
    scope: ASTAP, `solve-field` and the web service, which is sent a factor above 1 only. The internal solver ignores it.
  - name: "Timeout:"
    kind: spinbox
    default: "120"
    widget: {min: 10, max: 600, step: 1}
    effect: How long one solver may work, in seconds, before it is stopped and the next one is tried.
    range: >-
      For the web service it bounds each request instead, at 30 seconds or more. Its two waits, for a job and for the
      job's solution, ask every 5 seconds up to 120 times: about ten minutes each.
    tradeoff: A short timeout stops a blind search before it finds the field.
  - name: "Executable:"
    kind: field
    default: ""
    effect: The ASTAP program. ASTAP needs its own star database, installed with it.
    range: Set up when the file exists and can be run. Its button can look for ASTAP where it is usually installed.
    tradeoff: >-
      ASTAP gets a copy of the temporary file with `-r` the search radius, `-z` the downsample and `-wcs`, and with the
      hints `-ra` in hours, `-spd`, the declination plus 90, and `-fov`, the field's width in degrees at the scale hint.
      ASTAP's documentation describes `-fov` as the field's height.
    scope: On the **Plate Solving** page, under **ASTAP**.
    note: >-
      AfterNight reads the `.wcs` file ASTAP writes. Every external answer is read up to its `END` record, and refused
      without one or when it states one keyword twice with two values. An answer that names no frame is read as ICRS,
      and only its solution is written into the image.
  - name: "Local solve-field:"
    kind: field
    default: ""
    effect: Astrometry.net's `solve-field` program, on Linux.
    tradeoff: >-
      It gets a copy of the temporary file with `--radius`, `--downsample` of at least 1 and, with the hints, `--ra`
      and `--dec` in degrees and a scale from 80 to 120 % of the hint in arcseconds per pixel. AfterNight reads
      `solution.wcs`, else the header of `solution.new.fits`.
    scope: On the **Plate Solving** page, under **Astrometry.net**, on Linux only.
  - name: "Local index files:"
    kind: field
    default: ""
    effect: The folder of astrometry.net index files `solve-field` reads. They must cover the scale of your field.
    range: Set, it is passed in a configuration file of its own, with `inparallel`, `cpulimit 0` and `autoindex`.
    scope: As **Local solve-field:**.
  - name: "Web API key:"
    kind: field
    default: ""
    effect: >-
      The key of your account at nova.astrometry.net, which sets the web service up. A solve logs in with it, uploads
      the temporary file, waits for a job and for its solution, and downloads the solution's header.
    range: >-
      The upload asks that the image be not public, not for commercial use and not modified. With the hints it sends
      the center, the search radius and a scale from 80 to 120 % of the hint.
    tradeoff: >-
      Any text sets it up, so a wrong key fails only when a solve logs in. The key is kept in the settings and written
      as plain text by **Export Settings...**. The solve's messages and log do not show it.
    scope: On the **Plate Solving** page, under **Astrometry.net**.
  - name: "Catalog pack:"
    kind: combo
    default: Gaia DR3 G<=14 (gaia_dr3_g14)
    options: [Gaia DR3 G<=12 (gaia_dr3_g12), Gaia DR3 G<=14 (gaia_dr3_g14), Gaia DR3 G<=16 (gaia_dr3_g16), Gaia DR3 G<=18 (gaia_dr3_g18)]
    effect: >-
      Chooses the Gaia pack the internal solver reads, from the folder **Catalog root:** names, or from AfterNight's
      own data folder when that is empty. Deeper packs reach fainter stars.
    range: >-
      A pack is set up when its manifest and every file the manifest lists are there in full. About 3, 17, 78 and 305
      million stars, from 188 MB to 18 GB.
    tradeoff: A deeper pack helps a narrow or sparse field and takes more disk and reading time.
    note: >-
      The internal solver reads the pack's sky tiles that its catalog circles touch, no fainter than the pack's own
      limit. Proper motions move the
      stars from the catalog's epoch, J2016.0, to the exposure start in `DATE-OBS`, taken as a Julian year from
      J2000.0 at noon. Without it they stay at J2016.0.
    scope: On the **Catalogs & Data** page, under **Gaia DR3 Astrometric Catalogs**, with **Download Catalog...** and **Evict**.
```

```controls
window: Messages
controls:
  - name: Plate Solve Failed
    kind: readout
    default: ""
    effect: >-
      A message that opens when no solver answered: "Plate solving failed for" the temporary file, then what each
      solver said, in the order they were tried, separated by `|`, as `Internal Gaia: Internal Gaia solver failed to
      match the field. | ASTAP not configured`.
    range: >-
      A solver that is not set up adds a line saying so, as `ASTAP not configured`. An external program's line ends
      with what it printed, as `stdout=` and `stderr=`. A temporary file that cannot be written gives "Failed to save
      image for plate solving:" and the reason, alone.
    persistence: Nothing is written into the image, no part of a solution included, and the window keeps its hints.
    note: >-
      Read the lines in the order the solvers were tried, with the Session Console beside them, and mend the solver you
      meant to use first. For the temporary file, check the free disk space and the **Temporary Files** location.
  - name: OK
    kind: button
    default: ""
    effect: Closes the message. Press **Solve** again once you have changed what the message names.
```

## How it works

1. **Solve** writes the image the view shows as you press it, unsaved edits and header cards included, to a 32-bit
   floating-point FITS file in AfterNight's temporary folder. Every solver reads that file, and the window removes it
   when the solve ends or the window closes.
2. The position hint is the one the window holds, else the first pointing the header states. The scale hint is the
   pixel scale of the optics, equation (1), or a drizzled image's own.
3. The solvers are tried in turn: **Default solver:** first, then the others in their order. A solver that is not set
   up or does not solve adds a line to the diagnosis, and the first solution ends the search.
4. The internal solver demosaics a raw [CFA](../../reference/glossary.md#cfa) mosaic, then searches one channel at a
   time, and the first channel that solves gives the answer: the channels are not combined. It keeps up to 1,024
   stars, brightest first. A star is the brightest point within 5 pixels, stands 12.5 times the noise above the sky,
   has a roundness of at least 0.45 and is fitted with a Gaussian profile, with the finder's optional checks relaxed.
   On a stretched channel, where that height lies above half the channel's range over the sky, the search is made
   again at half. The solver matches up to 256 of the stars against up to 512 Gaia stars around a trial center by the
   shapes of star triangles. A channel with fewer than 3 stars, or no match, passes to the next.
5. The window shows the solution. With **Write WCS to the current image metadata** ticked, the solution replaces the
   header's world-coordinate cards as one history step, with `PLTSOLVR`, `FOCALLEN`, `XPIXSZ` and `YPIXSZ`, and the
   image's [lineage](../../reference/glossary.md#lineage) gains an entry.

### Model

With \(p\) the pixel size in micrometres and \(f\) the focal length in millimetres, the scale hint in arcseconds per
pixel is

\[ s = 206.264806\,\frac{p}{f} \tag{1} \]

where 206.264806 is the arcseconds in a radian, \(180 \times 3600 / \pi\), divided by 1000. One of the two values
without the other gives no hint. After a solve, with \(s'\) the solved **Image scale**, the window derives the focal
length

\[ f' = 206.264806\,\frac{p}{s'} \tag{2} \]

The internal solver projects each Gaia star onto the plane that touches the sky at the trial center, as the
coordinates \((\xi, \eta)\), and fits the image positions \((x, y)\) of the matched stars as

\[ \begin{bmatrix} x \\ y \end{bmatrix} = \begin{bmatrix} a & b \\ c & d \end{bmatrix} \begin{bmatrix} \xi \\ \eta \end{bmatrix} + \begin{bmatrix} x_0 \\ y_0 \end{bmatrix} \tag{3} \]

Here \((x_0, y_0)\) is the reference pixel, where the trial center lands, and the inverse of the matrix is the
solution's `CD` matrix. AfterNight puts the first pixel's center at one half and the header at one, so `CRPIX1` is
\(x_0 + 0.5\) and `CRPIX2` is \(y_0 + 0.5\). **Image scale** is the square root of the `CD` matrix's determinant, in
arcseconds. The field center is the sky at the middle of the frame, \((w/2, h/2)\) for a frame \(w\) by \(h\).

The Gaia stars of one trial come from a catalog circle about its center, 1.25 times half the field's diagonal at the
scale hint and at least 0.05 degree in radius. After each fit the solver moves the center to the solved middle of the
field and fits again, up to five times, until the center moves 0.05 arcsecond or less.

With \(u = x - x_0\) and \(v = y - y_0\), a [SIP](../../reference/glossary.md#sip) polynomial corrects the plane for the
optics' distortion:

\[ u' = u + A(u, v), \qquad v' = v + B(u, v) \tag{4} \]

\(A\) and \(B\) hold terms of total degree 2 up to their order. The internal solver fits order 2 from 6 matched stars,
3 from 10 and 4 from 24. It keeps the polynomial only when it lowers the fit's
[RMS](../../reference/glossary.md#rms) residual by more than 0.001 pixel and holds a coefficient larger than
\(10^{-12}\). It fits no inverse terms, `AP` and `BP`.

### Boundaries

- A trial of the internal solver is accepted with at least 8 matched stars and an RMS residual of at most 2 pixels.
  The window shows no residual and no count of stars: only the figures, the grid and the header tell a good solution
  from a wrong one.
- An edit, an undo or another image in the view does not stop the solve, but its answer is set aside, neither shown
  nor written. The status line then reads "Plate solve finished, but the active image changed.".
- **Cancel** reaches the internal solver inside its work and between channels and trial centers. It ends an external
  program at its next check, every quarter of a second, and breaks off the web service's request within a tenth of a
  second.
- A header that states the J2000 equinox and no frame, as astrometry.net and ASTAP write it, is read as ICRS. One that
  states FK5, or another equinox and no frame, shows no solution until it is solved again.
- Each solver stops at **Timeout:**, with its message, and the next solver is tried. An external program is held to
  1 to 3,600 seconds, whatever a settings file stores.

### Worked example

The [master](../../reference/glossary.md#master) in the picture above is a linear 32-bit floating-point RGB image of
the Carina Nebula, 7246 by 5429 pixels, shown in the view's [Autostretch](../../reference/glossary.md#autostretch)
display. Its header carries the solution its stacking wrote, without `PLTSOLVR`, and states `FOCALLEN` 432 and
`XPIXSZ` 2.315. With the Gaia DR3 G<=14 pack installed and every setting at its default, open the window. **Solved Field** shows the header's
solution, 1.095 arcsec/px and 107.7° (N through E), with **Solver** at `Unknown`. **Acquisition Hints** stays closed,
and holds the solution's reference direction, 10h 44m 30.671s and S 59° 53′ 31.732″, with 432.00 mm and 2.31 µm.

Press **Solve**. Equation (1) gives a scale hint of \(206.264806 \times 2.31 / 432 = 1.103\) arcseconds per pixel,
within 1 % of the solved 1.095. The internal solver tries the hint first, and an answer there ends the search. No
message opens. **Acquisition Hints** opens, and **Focal length** reads about 435 mm, equation (2) with 2.31 µm and
1.095. **Solved Field** gives nearly the same center, scale, rotation and size in the solve's own form, with
**Solver** at `Internal Gaia`. The status line names the solver and the time for a moment, and is emptied when the
solution lands in the image.

The history lists **Plate Solve**. The header gains `PLTSOLVR = 'Internal Gaia'`, a `FOCALLEN` near 435 and `XPIXSZ`
and `YPIXSZ` of 2.310000, the rounded value of the box. On the view, **Show Astrometry** draws the same coordinate
grid as before the solve: on this master the solve changes the header, not what the view draws.

## Starting points

| Situation | Start | Then adjust |
| --- | --- | --- |
| A frame whose header states its pointing and optics | The defaults, and **Solve** | Check **Image scale** against equation (1) and the grid on the view. |
| No position in the header | **Find object...** for the target, or its coordinates typed into **RA** and **Dec** | **Search radius:** larger, one step at a time, if it still fails. |
| A narrow field or a sparse sky | A deeper Gaia pack in **Catalog pack:** | ASTAP as **Default solver:**, with its own database. |
| An image that must not leave the computer | The internal solver or ASTAP | Clear **Web API key:**, so that the web service is never a fallback. |

## When it goes wrong

!!! warning "A solve replaces the old solution"
    With **Write WCS to the current image metadata** ticked, every world-coordinate card is replaced, whichever program
    wrote it. The history keeps the step before, but the file does not once you save over it. Save a copy first when
    the old header matters.

!!! warning "The web service receives the whole image"
    The temporary FITS file, every pixel and card of it, is uploaded to nova.astrometry.net with the hints. AfterNight
    asks for it to stay private, without commercial use or changes, and the service keeps it under its own terms.
    **Cancel** does not withdraw a job already sent.

!!! danger "A solution is not a check of itself"
    The window shows no residual and no count of matched stars, and a wrong solution looks as tidy as a right one.
    Check **Image scale** against your optics and the grid against the stars before Photometric Color Calibration or
    any other step that trusts it.

!!! warning "The solution lives in the open image"
    Nothing is written to a file until you save. Save as FITS or XISF: other formats may leave the solution out.

**Solve** stays disabled under "No plate solving tool is configured."
:   **Cause.** No Gaia pack is complete, no external program exists at its path and no web key is set, or the window
    has not looked since you set one up.
:   **Fix.** Press **Configure...**, install a pack on **Catalogs & Data** or set a solver on **Plate Solving**, and
    close the settings. When the banner stays, close the window and open it again.

"Internal Gaia catalog directory is not configured", "Internal Gaia catalog directory is invalid" or "Internal Gaia catalog installation is incomplete"
:   **Cause.** No pack is in the folder the settings name, the folder holds something else, or a download did not
    finish.
:   **Fix.** Press **Download Catalog...** on **Catalogs & Data**, or point **Catalog root:** at a complete pack.

"Internal Gaia catalog is a tile catalog and must be re-downloaded as a chunk catalog"
:   **Cause.** The pack was installed by an older version, in a layout this one does not read.
:   **Fix.** Download the pack again with **Download Catalog...**.

"Internal Gaia solver could not detect enough stars."
:   **Cause.** Every channel gave fewer than 3 stars: the image is out of focus, empty of stars, or its stars are
    clipped.
:   **Fix.** Solve a better-focused or calibrated image, or set up ASTAP, whose own detection may find them.

"Internal Gaia solver failed to match the field."
:   **Cause.** Stars were found, but no trial center matched the catalog: the position or the scale is wrong, the
    field lies outside **Search radius:**, or the pack is too shallow for it.
:   **Fix.** Check **RA**, **Dec**, the hemisphere, **Focal length** and **Pixel size**, then try a deeper pack or a
    larger radius.

"Internal Gaia solver timed out after" a number of seconds
:   **Cause.** The search outlasted **Timeout:**, as a blind search or a wide radius can.
:   **Fix.** Give a position and both optics, narrow **Search radius:**, or raise **Timeout:**.

"Internal Gaia solver failed: the region holds more catalog stars than one solve reads; narrow the search radius or use a shallower catalog"
:   **Cause.** The circle of catalog read around the hint, **Search radius:** and a margin, holds more than 4 million
    stars.
:   **Fix.** Lower **Search radius:**, or choose a shallower **Catalog pack:**.

"ASTAP timed out." or "ASTAP did not produce a .wcs solution file."
:   **Cause.** ASTAP found no solution in time, often for want of its star database or a usable scale.
:   **Fix.** Check that ASTAP solves the file on its own, with its database installed, then the hints and
    **Downsample:**.

"solve-field timed out." or "solve-field did not produce a valid WCS solution"
:   **Cause.** The index files do not cover the field's scale, or the search outlasted **Timeout:**.
:   **Fix.** Install index files for your scale, check **Local index files:**, and give the scale hint.

"Astrometry.net web: login failed" or a web solve that waits far beyond **Timeout:**
:   **Cause.** The key is wrong or the network refuses the service, or the service's queue is long: each of its two
    waits can last about ten minutes.
:   **Fix.** Check the key and the network, wait or press **Cancel**, and prefer a local solver.

"Plate solve finished for an image that is no longer active. The result was not applied."
:   **Cause.** The view's image was edited, undone or replaced while the solve ran.
:   **Fix.** Leave the image alone and press **Solve** again.

Other windows, or the saved file, still have no plate solution after a solve
:   **Cause.** **Write WCS to the current image metadata** was unticked, or the image was not saved after the solve, or
    was saved in a format that drops the header.
:   **Fix.** Tick it and solve again, then save as FITS or XISF, open the file again and check **Astrometry Info**.

## Related

- [Settings](../../interface/settings-dialog.md), for the Gaia packs on **Catalogs & Data** and the solvers on **Plate
  Solving**.
- [Astrometry Overlays](../../interface/astrometry-overlays.md) and [Annotate Image](../../interface/annotate-image.md),
  which draw the solution over the image and into a picture.
- [FITS Header, EXIF, And Astrometry Metadata](../../interface/fits-header-viewer.md), for **Astrometry Info** and the
  cards the solve writes.
- [Photometric Color Calibration](../color-calibration/photometric-cc.md) and
  [Spectrophotometric Color Calibration](../color-calibration/spectrophotometric-cc.md), which need the solution.
- [Image Registration](image-registration.md) and
  [Image Integration](../stacking-integration/image-integration.md), which solve their frames with the same solvers.
- [Saving And Exporting](../../interface/saving-and-exporting.md), to keep the solution in a file.

## Credits

- [Gaia Data Release 3](https://gea.esac.esa.int/archive/documentation/GDR3/Miscellaneous/sec_credit_and_citation_instructions/),
  by the European Space Agency's Gaia mission, processed by the Gaia Data Processing and Analysis Consortium
  (ESA/Gaia/DPAC): the positions, proper motions and G magnitudes the catalog packs are made from. Gaia data are open
  and free to use with credit to ESA/Gaia/DPAC, as its credit and citation instructions state.
- Gaia Collaboration, T. Prusti et al., [The Gaia mission](https://doi.org/10.1051/0004-6361/201629272), Astronomy &
  Astrophysics 595, A1 (2016).
- Gaia Collaboration, A. Vallenari et al.,
  [Gaia Data Release 3: Summary of the content and survey properties](https://doi.org/10.1051/0004-6361/202243940),
  Astronomy & Astrophysics 674, A1 (2023).
- K. M. Górski et al.,
  [HEALPix: A Framework for High-Resolution Discretization and Fast Analysis of Data Distributed on the Sphere](https://doi.org/10.1086/427976),
  The Astrophysical Journal 622, 759 (2005), whose numbering of the sky's tiles the catalog packs, their reader and the
  blind search use.
- E. W. Greisen and M. R. Calabretta, and M. R. Calabretta and E. W. Greisen,
  [the FITS World Coordinate System papers I and II](https://fits.gsfc.nasa.gov/fits_wcs.html), Astronomy &
  Astrophysics 395 (2002), which define the tangent-plane projection and the cards the solution is written in.
  AfterNight's engine implements them itself, with no WCS library.
- D. L. Shupe et al.,
  [The SIP Convention for Representing Distortion in FITS Image Headers](https://fits.gsfc.nasa.gov/registry/sip/SIP_distortion_v1_0.pdf),
  version 1.0, the distortion polynomial of equation (4).
- [ASTAP](https://www.hnsky.org/astap.htm), the astrometric stacking program by Han Kleijn, and its documentation of
  the command line AfterNight uses. AfterNight runs it as a separate program, installed by you with its star
  database, and does not ship it.
- [Astrometry.net](https://astrometry.net/), by the Astrometry.net developers, distributed under the GNU General Public
  License version 3 or later: D. Lang, D. W. Hogg, K. Mierle, M. Blanton and S. Roweis,
  [Astrometry.net: Blind astrometric calibration of arbitrary astronomical images](https://doi.org/10.1088/0004-6256/139/5/1782),
  The Astronomical Journal 139, 1782 (2010). AfterNight runs its `solve-field` as a separate program and uses the
  [nova.astrometry.net web API](https://astrometry.net/doc/net/api.html); it ships neither.
