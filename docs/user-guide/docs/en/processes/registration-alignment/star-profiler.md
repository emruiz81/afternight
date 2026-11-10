---
kind: process
stage: register
menu:
  - Process > Star Processing > Star Profiler v1
  - Process > Star Processing > Star Profiler v2
summary: >-
  Finds and fits the stars of an image, lists their positions, widths and shapes over its view, and saves the list as
  a profile for the processes that work around stars.
data_in: either
data_out: either
destructive: false
preview: none
sequences: false
masks: none
external: An installed Gaia catalog, optional, for the catalog matches of v2
glance:
  enabled: >-
    An image or a sequence view is active and no other process window is open on it. Each window needs the image to
    itself while it is open.
  inputs: >-
    One grayscale or RGB image of any pixel type, or the frame a sequence view shows. A mask on the view is not used.
    For sky positions, v2 needs a plate solution in the image's header, and for catalog matches an installed Gaia
    catalog.
  outputs: >-
    A list of stars drawn over the view and, when you ask, a `.anspv1` or `.anspv2` profile file and a new grayscale
    mask named after the image with " - STARMASK". The image and its history are not changed.
  state: >-
    A calibrated linear image to measure widths and shapes. The stretched image itself for a profile that Profiled
    Starmask Recombination uses on that image.
derived: true
controls_ignore:
  - label: "0"
    object: zoomDetectedStars
    reason: >-
      The zoom box beside Center Selected Star, shown as x2.00, which the inventory names after the count to its left.
      The entry of Center Selected Star describes it.
rewritten: true
---
# Star Profiler

Star Profiler finds the stars of an image, fits a [PSF](../../reference/glossary.md#psf) to each and lists what the
fit measured: position, widths, shape and brightness. The list is drawn over the image's view, where you inspect,
sort, filter and delete stars, then save it as a profile file or paint it into a
[mask](../../reference/glossary.md#mask).

Two windows share the work. **Star Profiler v2** offers the detector's own controls, places each star on the sky
through the image's [plate solution](../../reference/glossary.md#plate-solution) and writes `.anspv2` profiles.
**Star Profiler v1** asks four simpler questions and writes `.anspv1` profiles. Neither window changes the image or
adds a step to its [history](../../reference/glossary.md#history).

```figure
owed: true
image: ../../assets/images/processes/registration-alignment/star-profiler.webp
alt: >-
  The StarProfiler window. At the top, the Detected Stars group: a table of narrow columns headed x, y, Mag, FWHMx,
  FWHMy, Ra, Dec and on, filled with rows of figures, then the Load Profile, Save Profile, Stars Filter and Create Mask buttons, a
  button with a cross and Clear, and a line that reads Detected Stars 2187 beside the Center Selected Star check box
  and a zoom box. Below, the Star Detection group: Radius at 5, Threshold at 3.00, the unticked Amplitude Range group,
  Roundness Threshold at 0.50, the unticked Roundness Range group, Convergence at 1, Relax PSF Checks, Profile Type at
  Gaussian with a dimmed Minimum Beta, then Detect Stars, Use Lightness and Reset. Close sits at the bottom right.
caption: >-
  Star Profiler v2 after a detection on the stretched Carina master with Threshold at 3.00: the table lists 2,187
  stars, brightest first.
pins:
  - {n: 2, x: 3.4, y: 52.1}
  - {n: 3, x: 3.4, y: 68.1}
  - {n: 4, x: 3.4, y: 89.5}
  - {n: 5, x: 3.4, y: 24.0}
  - {n: 6, x: 71.0, y: 34.8}
  - {n: 8, x: 53.5, y: 34.8}
```

## Before you start

- To measure widths and shapes, use the calibrated [linear image](../../reference/glossary.md#linear-image). A
  [stretch](../../reference/glossary.md#stretch) widens the stars the fit measures and changes which ones count as
  saturated. Strong gradients, [hot pixels](../../reference/glossary.md#hot-pixel) and clipping change what is found.
- For a profile you will use on stretched images, detect on the stretched image they come from. A stretch does not
  move the stars. The view's display mode changes nothing the detector reads.
- Use v2 for new work, and v1 where an `.anspv1` file is needed. Each reads only its own profiles, and renaming a
  file does not convert it. Profiled Starmask Recombination reads both.
- For sky positions, plate solve the image before you open v2. For catalog matches, install a
  [Gaia](../../reference/glossary.md#gaia) catalog on the **Catalogs & Data** page of **Edit > Settings...**.
- The window needs the image to itself: close the other process windows on it first
  ([Windows that differ](../../interface/process-windows-reference.md#windows-that-differ)).

## Steps

1. **Open the window.** Make the image the [active image](../../reference/glossary.md#active-image) and choose
   **Process > Star Processing > Star Profiler v2**. The table is empty and **Detected Stars:** reads 0.
2. **Size the search.** Raise **Radius** for large stars, or lower it for a crowded field. Raise **Threshold** to keep
   only the clear stars, or lower it to find fainter ones.
3. **Leave out what is not a star.** Raise **Roundness Threshold** to refuse elongated fits. Tick **Amplitude Range**
   only to keep a band of brightness.
4. **Detect.** Choose **Detect Stars**. A progress window shows while the run works, then a message gives the count:
   choose **OK**. The table lists the stars, and an ellipse marks each one on the view.
5. **Inspect.** Click a row, or a star on the view, and a red cross marks it. Click a column's header to sort by it.
   Tick **Center Selected Star** to center the view on each star you select.
6. **Keep the stars you want.** Select a wrong star and choose **Remove selected star**, or choose **Stars Filter**,
   narrow its ranges and confirm with **OK**. **Detected Stars:** shows the new count.
7. **Save the profile.** Choose **Save Profile**, name the file in the file dialog and save it. A message says
   "Star profile saved successfully!".
8. **Make a mask, if you need one.** Choose **Create Mask**, set the areas and choose **OK**. The mask opens beside
   the image as `<image> - STARMASK`.
9. **Close.** Choose **Close** and answer **Yes**. The window closes, and the ellipses leave the view.

## Controls

The window has no preview, no **Target Image** and no **Apply**: it reads the image it opened on and never changes it.
Its header and **Close** work as in every [process window](../../interface/process-windows.md).

### Star Profiler v2

```controls
window: StarProfiler
controls:
  - name: Table of stars
    object: tableWidget
    kind: readout
    default: Empty
    effect: Lists the stars, one row each, under the columns x, y, Mag, FWHMx, FWHMy, Ra, Dec, Angle, B, A, Beta, r, RMSE, FWHM and Circularity.
    range: "**x** and **y** are the position in pixels, the first pixel's center at 0.5. **Mag** is an instrumental magnitude, -2.5 log10 of the box's sum above the sky, not a catalog magnitude, even for a star with a catalog match. **FWHMx** and **FWHMy** are the full widths at half maximum along the star's long and short axes, shown in pixels with `px`. **Ra** and **Dec** are the sky position in degrees. **Angle** is the long axis's angle in degrees, **B** the fitted sky, **A** the fitted height above it and **Beta** the Moffat exponent. **r** is FWHMy over FWHMx, **RMSE** the fit's residual, **FWHM** the mean of the two widths and **Circularity** the figure C of equation (3)."
    enabled: Filled by a detection or a loaded profile. A cell without a figure shows `-`, as **Beta** does for a Gaussian fit and **Ra** and **Dec** do without a plate solution.
    tradeoff: Sorting changes the order of the list, which a saved profile keeps, and none of its figures.
    note: "A detection shows the brightest stars first. Click a row to select its star. Click a header to sort largest first, and again for smallest first: the header then reads `*Mag <` or `*Mag >`. A star without the figure comes last smallest first, and first largest first. A new detection or load clears the sort."
  - name: Load Profile
    kind: button
    default: ""
    effect: Opens a file dialog, **Load star profile...**, that shows `.anspv2` files, and reads the file you choose in the background. Its stars replace the list and the drawing, and a message says "Star profile loaded successfully!" with the count.
    range: "The window reads profiles of Star Profiler v2 only, as the file's content says, whatever its name. A v1 file, or one that cannot be read, is refused with \"Could NOT load the star profile file. Error message:\" and the reason, and changes nothing. A file of more than 64 MB or one million stars is refused."
    enabled: Always, also while a detection works.
    scope: The list and the drawing. The settings the file records do not reach the controls.
    persistence: The dialog opens in the folder last used for v2 profiles, also in a later session. A loaded star keeps every figure, sky position and catalog match the file holds.
    note: A file the window accepts stops a detection on its way. One it refuses leaves the detection running, and its stars show as usual. A detection asked for after the file stops the read.
  - name: Save Profile
    kind: button
    default: ""
    effect: Opens a file dialog, **Save star profile...**, for `.anspv2` files, and writes the list there in the background. A message then says "Star profile saved successfully!".
    range: "The file is a JSON document of schema 2, engine `v2`. It holds the stars in the table's order with every fitted figure and its uncertainty, the sky position and the catalog match of each, the detection's settings and the frame's figures: the sky, its noise, the median width, roundness and signal-to-noise, a quality figure that is the number of unsaturated stars times their median **r** over their median **FWHM**, and, with a catalog, the number of matches, the G zero point with its uncertainty and the faintest match. A star's match holds the entry's identifier, G magnitude, BP-RP color and separation, and its separate BP and RP magnitudes are written as 0. The file holds no pixels, no image size and nothing that names the image."
    enabled: While the list holds stars.
    tradeoff: The frame's figures are the detection's. Deleting or filtering stars counts only the catalog matches again.
    persistence: The file is written beside its place first and renamed into place when complete, so a failed save leaves an older file as it was. The dialog starts in the folder last used for v2 profiles.
    note: "A failure says \"The star profile could not be saved.\" with the reason. A v2 file does not record whether you changed the list by hand. After **Load Profile**, the settings of the loaded file are written again, not the controls'."
  - name: Stars Filter
    kind: button
    default: ""
    effect: Opens the **Stars Filter** dialog beside the window, to keep a part of the list. The dialog is not modal.
    enabled: While the list holds stars.
    persistence: A filter confirmed with **OK** replaces the list in the window, not in a file. Save the profile to keep it.
  - name: Create Mask
    kind: button
    default: ""
    effect: Opens **Profiled Mask Generation** beside the window, over the list as it is, filtered or sorted. The dialog is not modal. Its **OK** paints the mask in the background and opens it beside the image.
    enabled: While the list holds stars.
    scope: A new image. The image and its history are not changed.
    persistence: The mask is not saved to a file.
  - name: Remove selected star
    kind: button
    default: ""
    effect: Deletes the selected star from the list and the drawing, and marks the list as changed by hand.
    enabled: After you select a row or a star, until the list changes.
    tradeoff: The frame's figures a saved profile holds stay those of the detection.
    persistence: Until you load or detect again. Save the profile to keep the change.
  - name: Clear
    kind: button
    default: ""
    effect: Asks "This will remove all the stars from the list, are you sure?" and, on **Yes**, empties the list and the drawing and stops a detection or a read on its way.
    enabled: While the list holds stars.
    persistence: The stars come back only from a file or a new detection.
  - name: "Detected Stars:"
    kind: readout
    default: "0"
    effect: Counts the stars of the list. It follows every deletion and every confirmed filter.
  - name: Center Selected Star
    kind: check
    default: "Off"
    effect: Ticked, each star you select is brought to the middle of the view, at the zoom of the box beside it.
    range: The box beside it, `x2.00` when the window opens, sets the zoom from 1 to 20, where 2 shows the image at 200 %. Changing it centers the selected star again.
    enabled: While the list holds stars. The zoom box only while this is ticked.
    scope: The view only.
    persistence: Until the window closes. **Reset** leaves both.
  - name: Radius
    kind: spinbox
    default: "5"
    widget: {min: 3, max: 32, step: 1}
    effect: Sets half the width, in pixels, of the box a candidate must be the brightest point of. At 5 the box is 11 pixels across. Higher asks a candidate to dominate a wider box. Lower separates compact stars in a crowded field.
    range: The image must be more than 4 × **Radius** + 8 pixels wide and tall. The search skips a border of **Radius** pixels.
    tradeoff: Higher costs more work and merges crowded stars. Lower admits more candidates.
    persistence: Until the window closes. **Reset** returns it. A profile records it as `radius`, and a loaded profile does not set it.
  - name: Threshold
    kind: spinbox
    default: "1.00"
    widget: {min: 0.01, max: 15, step: 0.05, decimals: 2}
    effect: Sets how far above the sky a candidate's peak must stand, in units of five noise deviations, by equation (1). Lower finds fainter stars and more noise. Higher keeps only the clearer stars.
    range: The slider moves in steps of 0.001.
    tradeoff: A low value turns noise into candidates, and every candidate is fitted, which costs time. A high value misses faint stars.
    scope: The plane the stars are searched in.
    persistence: Until the window closes. **Reset** returns it. A profile records it as `sigma`.
  - name: Amplitude Range
    kind: check
    default: "Off"
    effect: Ticked, keeps only the stars whose fitted height above the sky, **A** in the table, lies between **Minumum** and **Maximum**, in fractions of full scale. Unticked, no height is refused.
    enabled: Its two boxes are disabled while it is unticked.
    tradeoff: A narrow window removes real stars. On a linear image the brightest stars fit above 1, above any **Maximum**.
    persistence: Until the window closes. **Reset** unticks it. A profile records the bounds as `min_A` and `max_A`, both 0 while it is unticked.
  - name: Minumum
    kind: spinbox
    default: "0.00"
    widget: {min: 0, max: 0.99, step: 0.05, decimals: 2}
    effect: Sets the smallest height a star may have. Higher leaves out the fainter stars, and the noise fitted as stars.
    range: The window spells the label so.
    enabled: With **Amplitude Range** ticked.
    persistence: Until the window closes. **Reset** returns it.
  - name: Maximum
    object: thresholdMaximumValue
    kind: spinbox
    default: "1.00"
    widget: {min: 0.01, max: 1, step: 0.05, decimals: 2}
    effect: Sets the largest height a star may have. Lower leaves out the brighter stars, which an alignment may need.
    range: Below **Minumum**, the detection is refused with "the amplitude window's upper bound is at or above its lower bound".
    enabled: With **Amplitude Range** ticked.
    persistence: Until the window closes. **Reset** returns it.
  - name: Roundness Threshold
    kind: spinbox
    default: "0.50"
    widget: {min: 0.01, max: 0.95, step: 0.05, decimals: 2}
    effect: Sets the smallest ratio of a star's short width to its long one, **r** in the table. Higher keeps only rounder stars. Lower admits elongated stars, trails and pairs of stars.
    tradeoff: A high value can leave too few stars on trailed or distorted data.
    persistence: Until the window closes. **Reset** returns it. A profile records it as `roundness`.
  - name: Roundness Range
    kind: check
    default: "Off"
    effect: Ticked, sets an upper limit on the same ratio with its **Maximum**. Unticked, the limit is 1, a perfectly round star.
    enabled: Its box is disabled while it is unticked.
    tradeoff: It refuses the roundest stars, which is rarely wanted, and a narrow band can empty the list.
    persistence: Until the window closes. **Reset** unticks it. A profile records the limit as `max_r`, 1 while it is unticked.
  - name: Maximum
    object: roundnessThresholdMaximumValue
    kind: spinbox
    default: "1.00"
    widget: {min: 0.01, max: 1, step: 0.05, decimals: 2}
    effect: Sets the largest ratio a star may have. Lower refuses the roundest fits.
    range: Below **Roundness Threshold**, the detection is refused with "the largest roundness lies within the unit interval and at or above the smallest".
    enabled: With **Roundness Range** ticked.
    persistence: Until the window closes. **Reset** returns it.
  - name: Convergence
    kind: spinbox
    default: "1"
    widget: {min: 1, max: 3, step: 1}
    effect: Multiplies how many iterations each fit may take to settle. A fit that does not settle is refused.
    tradeoff: Higher can rescue difficult fits, at the cost of time.
    persistence: Until the window closes. **Reset** returns it. A profile records it as `convergence`.
  - name: Relax PSF Checks
    kind: check
    default: "Off"
    effect: Skips four checks, the scan's tests of a candidate's symmetry and local contrast, and the fit's tests of its width against the scan's estimate and of its residual.
    range: The amplitude, roundness and beta limits and the tests of a finite position, width and magnitude still apply.
    tradeoff: It finds more of the difficult stars, and more pairs of stars and poor fits, and more candidates reach the fit, which costs time.
    persistence: Until the window closes. **Reset** unticks it. A profile records it as `relax_checks`.
  - name: Profile Type
    kind: combo
    default: Gaussian
    options: [Gaussian, Moffat]
    effect: Chooses the profile each candidate is fitted with, by equation (2). **Gaussian** has wings that fall fast. **Moffat** has wings that fall as a power, whose exponent beta is fitted with the rest.
    enabled: "**Moffat** enables **Minimum Beta**."
    tradeoff: "**Moffat** has one more parameter and costs more time."
    persistence: Until the window closes. **Reset** returns it to **Gaussian**. A profile records it as `gaussian` or `moffat_bfree`.
  - name: Minimum Beta
    kind: spinbox
    default: "1.50"
    widget: {min: 0.05, max: 10, step: 0.05, decimals: 2}
    effect: Sets the smallest Moffat exponent a star may have. A lower beta means broader wings, so higher refuses the stars with the broadest wings.
    enabled: With **Profile Type** at **Moffat**.
    tradeoff: Too high refuses real stars with wide wings.
    persistence: Until the window closes. **Reset** returns it. A profile records it as `min_beta`.
  - name: Detect Stars
    kind: button
    default: ""
    effect: Finds the stars of the image with the controls as they are when you press it, and replaces the list. On a plate-solved image it then places each star on the sky and matches it against an installed Gaia catalog.
    enabled: Disabled while a detection works. The other controls stay enabled, and a change reaches the next detection.
    tradeoff: The run works in the background behind a progress window, and the application stays responsive. Its time grows with the image's area and the number of candidates. A larger **Radius**, a lower **Threshold**, **Moffat**, a higher **Convergence**, **Relax PSF Checks** and a dense field cost more. A catalog match reads only the part of the catalog inside the field.
    scope: The image the view shows, or the frame a sequence view shows. It is read and never changed, and the mask on its view is not used.
    persistence: The list stays until a detection, a load, **Clear** or **Close** replaces it.
    range: Look for missed stars, hot pixels, saturated cores and elongated fits, change one control at a time and detect again.
    note: "When the run ends, a message titled \"Detect star profile\" says \"Star profile detected successfully!\" with the count, or \"Could NOT detect any star\". A detection that is refused shows \"Error running Star Profiler\" with the reason, or \"There is no image to detect stars on.\" when the view holds none."
  - name: Use Lightness
    kind: check
    default: "Off"
    effect: Ticked, finds the stars of an RGB image in its CIE lightness, L*. Unticked, in its green channel. A grayscale image is searched in its one channel either way.
    tradeoff: L* draws on the three channels and is less tied to one of them. Green often has the best signal of a one-shot color camera.
    scope: Only the plane the stars are searched in.
    persistence: Until the window closes. **Reset** leaves it, and a profile does not record it.
    note: Searched in the green channel, a star fitted 1 pixel wide or less on either axis is refused.
  - name: Reset
    kind: button
    default: ""
    effect: Returns **Radius**, **Threshold**, the two bounds of **Amplitude Range**, **Roundness Threshold**, its **Maximum**, **Convergence**, **Relax PSF Checks**, **Profile Type** and **Minimum Beta** to their defaults, and unticks the two range groups.
    range: It leaves **Use Lightness**, **Center Selected Star**, its zoom and the list as they are.
  - name: Close
    kind: button
    default: ""
    effect: Closes the window. While the list holds stars it first asks "All non saved data will be lost. Do you want to exit this process?", and **No** keeps the window open.
    range: It asks whether or not the list was saved. A detection on its way stops.
    persistence: The list, the drawing and an open **Stars Filter** close with the window. Saved profiles and masks stay.
```

```controls
window: The stars on the view
controls:
  - name: Ellipses
    kind: readout
    default: ""
    effect: Draws each star of the list over the image's view, as an ellipse of the star's shape, four of its long widths across, turned to its angle and centered on its position.
    range: Magenta for a saturated fit, whose height above the sky exceeds the range from the sky to full scale, or to the brightest sample when that is lower. Yellow for the others.
    enabled: From a detection or a load until the window closes. A **Stars Filter** preview draws only the stars it keeps.
    tradeoff: In a dense field the ellipses crowd each other.
    scope: The view's drawing only. Nothing is written into the image.
  - name: Click on a star
    kind: action
    default: ""
    effect: A left click inside a drawn star selects its row in the table.
    range: A star answers within two of its widths of its center, along each axis. It is found in the list by its position, so a click selects the right row during a filter's preview too.
  - name: Red cross
    kind: readout
    default: ""
    effect: Marks the selected star with a horizontal and a vertical red line across the whole image.
```

```controls
window: Detection progress
controls:
  - name: Progress
    kind: readout
    default: ""
    effect: Shows "Detecting stars..." with a moving bar, without a share done, while the detection works.
    range: Its title names the window and the image, as `StarProfiler <RGB>`.
  - name: Cancel
    kind: button
    default: ""
    effect: Stops the detection. The list stays as it was, and no message shows.
```

### Star Profiler v1

```controls
window: StarProfilerV1
controls:
  - name: Table of stars
    object: tableWidget
    kind: readout
    default: Empty
    effect: Lists the stars under the columns x, y, intensity, percentage, deltaRadius, quality, radius, meanRadius, largeMajorAxis, smallMajorAxis, largeMinorAxis, smallMinorAxis and majorAxisAngle, each a figure of the fit.
    range: "**intensity** is the fitted height above the sky, **percentage** 1 for every star and **deltaRadius** half the difference of the two full widths. **quality** is the figure C of equation (3). **radius** is half the mean full width and **meanRadius** the radius times 1.5/2.35. **largeMajorAxis** and **smallMajorAxis** are both half the long width, **largeMinorAxis** and **smallMinorAxis** both half the short width, and **majorAxisAngle** is the long axis's angle in degrees."
    note: A detection shows the highest **intensity** first. The table sorts and selects as in Star Profiler v2.
  - name: Load Profile
    kind: button
    default: ""
    effect: Reads a `.anspv1` file through **Load star profile...**, as the v2 window reads its own.
    range: A v2 file is refused. A v1 star is read back round, at its position, with its **intensity** and its mean width. Its **quality** reads 1, its angle 0 and its four axes the same.
    persistence: The dialog opens in the folder last used for v1 profiles.
  - name: Save Profile
    kind: button
    default: ""
    effect: Writes the list as a `.anspv1` file through **Save star profile...**, as the v2 window writes its own.
    range: The file is a JSON document of schema 1, engine `v1`. It holds each star's columns as the table shows them, except **radius** and **meanRadius**, which it holds at four times the table's figures. It adds the four detection controls, the frame's median width, quality, sky and noise, and whether you changed the list by hand.
    tradeoff: A star read back and saved again is written round, without its own quality, axes and angle.
  - name: Stars Filter
    kind: button
    default: ""
    effect: Opens the **Stars Filter** dialog with ranges of **Intensity**, **Quality** and **Radius**, as in Star Profiler v2.
  - name: Create Mask
    kind: button
    default: ""
    effect: Opens **Profiled Mask Generation**, as in Star Profiler v2.
  - name: Remove selected star
    kind: button
    default: ""
    effect: Deletes the selected star, as in Star Profiler v2.
  - name: Clear
    kind: button
    default: ""
    effect: Empties the list after a question, as in Star Profiler v2.
  - name: "Detected Stars:"
    kind: readout
    default: "0"
    effect: Counts the stars of the list.
  - name: Center Selected Star
    kind: check
    default: "Off"
    effect: Brings each star you select to the middle of the view, at the zoom of the box beside it, as in Star Profiler v2.
  - name: Detection Threshold
    kind: spinbox
    default: "0.10"
    widget: {min: 0.01, max: 1, step: 0.01, decimals: 2}
    effect: Sets the smallest fitted height above the sky, **intensity** in the table, a star may have, in fractions of full scale. Higher keeps only brighter stars.
    tradeoff: Low values keep faint stars and the fits of noise. High values miss faint stars.
    scope: The search itself stands five noise deviations above the sky, as v2 does at a **Threshold** of 1 and a **Radius** of 5, and fits a Gaussian.
    persistence: Until the window closes. **Reset** returns it. A profile records it, and a loaded profile does not set it.
  - name: Roundness Tolerance
    kind: spinbox
    default: "2.00"
    widget: {min: 0, max: 10, step: 0.01, decimals: 2}
    effect: Sets the largest difference, in pixels, between a star's two fitted standard deviations, by equation (4). Lower keeps only rounder stars.
    range: At 0 no star is kept.
    tradeoff: High values admit elongated stars and objects that are not stars. Very low values refuse real stars.
    persistence: Until the window closes. **Reset** returns it. A profile records it, and a loaded profile does not set it.
  - name: Min Size
    kind: spinbox
    default: "2"
    widget: {min: 0, max: 99, step: 1}
    effect: Sets the smallest mean full width, in pixels, a star may have. Higher leaves out small stars and hot pixels.
    range: Above **Max Size**, no star is kept. The window does not hold the two in order.
    tradeoff: High values leave out small and faint stars.
    persistence: Until the window closes. **Reset** returns it. A profile records it.
  - name: Max Size
    kind: spinbox
    default: "25"
    widget: {min: 0, max: 99, step: 1}
    effect: Sets the largest mean full width, in pixels, a star may have. Lower leaves out large stars.
    tradeoff: High values keep large, saturated stars and broad structure taken for stars.
    persistence: Until the window closes. **Reset** returns it. A profile records it.
  - name: Detect Stars
    kind: button
    default: ""
    effect: Finds the stars as the v2 window does, then keeps those equation (4) allows, and replaces the list.
    note: It places no star on the sky. Its progress window, its messages and what it reads are those of Star Profiler v2.
  - name: Reset
    kind: button
    default: ""
    effect: Returns the four detection controls to their defaults. It leaves the list as it is.
  - name: Close
    kind: button
    default: ""
    effect: Closes the window, after the same question as in Star Profiler v2.
```

### Stars Filter

```controls
window: The Stars Filter dialog
controls:
  - name: Intensity, Quality and Radius
    kind: spinbox
    default: The list's extremes
    widget: {step: 0.1, decimals: 2}
    effect: In v1, each has a **Min** and a **Max** that keep the stars whose **intensity**, **quality** or **radius** lies between them.
    range: Each pair runs between the list's smallest and largest figure, and each end is widened by 0.01. A pair left at the extremes filters nothing.
    tradeoff: "**intensity** can remove faint or bright stars, and **quality** mixes brightness and shape. Narrowing **radius** leaves out hot pixels or very large structures."
  - name: Mag, FWHMx and FWHMy
    kind: spinbox
    default: The list's extremes
    widget: {step: 0.1, decimals: 2}
    effect: In v2, each has a **Min** and a **Max** that keep the stars whose **Mag**, **FWHMx** or **FWHMy** lies between them. The widths are in pixels.
    range: "**Mag** runs the other way: its **Min** starts at the faintest magnitude and its **Max** at the brightest. A star without a magnitude is not kept once **Mag** is narrowed."
    tradeoff: The magnitude is not calibrated. Narrowing **FWHMx** keeps the sharp stars or leaves out broad fits. Narrowing one width alone favors round or elongated stars, so narrow both alike.
  - name: Random selection
    kind: spinbox
    default: "100.00"
    widget: {min: 0, max: 100, step: 0.5, decimals: 2}
    effect: Keeps this share, in percent, of the stars the ranges keep, chosen at random.
    range: The draw is fixed when the dialog opens, so the same settings keep the same stars until it is opened again.
    tradeoff: For a sparse mask, or to try a smaller list.
  - name: Include Neighbors
    kind: check
    default: "Off"
    effect: Also keeps every star left out whose box overlaps a kept star's, then their neighbors in turn. A box reaches two full widths from the star's center on each axis.
    enabled: It acts while a range is narrowed or **Random selection** is below 100.
    tradeoff: In a dense field it can bring back most of the list.
  - name: "Filtered Stars:"
    kind: readout
    default: kept/total
    effect: Shows how many stars the filter keeps, out of the list it started from.
  - name: Preview
    kind: check
    default: "On"
    effect: Draws only the stars the filter keeps over the view, as you change the ranges. Off draws the whole list again.
    scope: The drawing only. The list changes only with **OK**.
  - name: OK
    kind: button
    default: ""
    effect: Asks "This change will discard the filtered stars from the list, if you want those stars back you'll need to load the profile again, or run a new detection. Do you want to continue?" **Yes** replaces the window's list with the filtered one and closes the dialog.
    persistence: The list is marked as changed by hand. Save the profile to keep it.
  - name: Reset
    kind: button
    default: ""
    effect: Returns the ranges to the list's extremes and **Random selection** to 100.
  - name: Cancel
    kind: button
    default: ""
    effect: Closes the dialog and draws the whole list again. Closing it another way does the same.
```

### Profiled Mask Generation

```controls
window: The Profiled Mask Generation dialog
controls:
  - name: Binary Areas
    kind: check
    default: "On"
    effect: Paints each star as a filled ellipse of its fit, at full value. Ticking it unticks **Shaped Areas**, and unticking it ticks that group.
  - name: Scale Factor
    kind: spinbox
    default: "1.00"
    widget: {min: 0, max: 2, step: 0.01, decimals: 2}
    effect: Multiplies the ellipse's axes, which are four of the star's full widths at 1. Higher gives larger areas.
    enabled: With **Binary Areas** ticked.
  - name: Soften Edges
    kind: spinbox
    default: "0.00"
    widget: {min: 0, max: 10, step: 0.01, decimals: 2}
    effect: Blurs the ellipses with a Gaussian of this standard deviation, in pixels. At 0 their edges stay hard.
    enabled: With **Binary Areas** ticked.
  - name: Shaped Areas
    kind: check
    default: "Off"
    effect: Paints each star as a round falloff from its center, of the **Shape** chosen, over three radii. Where stars overlap, the larger value counts.
    range: A star whose long width is above 50 pixels is not painted.
  - name: Shape
    kind: combo
    default: Truncated Bell
    options: [Bell, Truncated Bell, Linear, Truncated Linear, Cubic, Quadratic]
    effect: Chooses the falloff, as Profiled Starmask Recombination paints it.
    enabled: With **Shaped Areas** ticked.
  - name: "% Pixel Increase"
    kind: spinbox
    default: "0.50"
    widget: {min: 0, max: 1, step: 0.01, decimals: 2}
    effect: Multiplies the star's long width to give the falloff's radius. 0.50 halves it, it does not add half.
    enabled: With **Shaped Areas** ticked.
  - name: Pixel Increase
    kind: spinbox
    default: "0"
    widget: {min: 0, max: 20, step: 1}
    effect: Adds this many pixels to the radius, after **% Pixel Increase**.
    enabled: With **Shaped Areas** ticked.
  - name: OK
    kind: button
    default: ""
    effect: Closes the dialog and paints the list in the background into a new grayscale image of the image's size, 32-bit floating point between 0 and 1, whatever the image's pixel type. It opens beside the image as `<image> - STARMASK`.
    persistence: "The mask opens when it is painted, also after the window has closed. Its history starts with `Star Profiler: Starmask image created from '<image>'`, and it is not saved to a file. A failure shows its reason under \"Profiled Mask Generation\"."
  - name: Reset
    kind: button
    default: ""
    effect: Ticks **Binary Areas** and returns every value to its default.
  - name: Cancel
    kind: button
    default: ""
    effect: Closes the dialog without a mask.
```

## How it works

1. The image is copied and read as [fractions of full scale](../../reference/glossary.md#fraction-of-full-scale).
   The stars are searched in one plane: the green channel of an RGB image, its lightness with **Use Lightness**, or
   the one channel of a grayscale image.
2. The detector measures the sky and the noise of that plane and smooths it. Candidates are the points above the
   threshold of equation (1) that are the brightest of their box, have enough bright neighbors and are symmetric enough.
   A candidate on a saturated plateau is moved to its center, and one too near an earlier candidate is dropped.
3. Each candidate is fitted with a rotated Gaussian or [Moffat profile](../../reference/glossary.md#moffat-profile),
   equation (2), brightest first. A fit that does not settle, or fails a check, is refused. Star Profiler v1 then
   keeps the stars equation (4) allows.
4. Star Profiler v2 places each star on the sky through the plate solution in the image's header. With an installed
   Gaia catalog, it matches the stars one to one with the catalog's stars of the field, within 5 arcseconds and down
   to G magnitude 16. The catalog's stars are first carried to the Julian epoch of the exposure's start, when the
   image records one. The matched stars fit a G zero point.
5. The stars become the window's list and are drawn over the view. A saved profile holds them in the list's order,
   at the positions the table shows.

### Model

With \(B_0\) the median of the plane, \(n\) its noise and \(s\) the value of **Threshold**, a candidate's peak in the
smoothed plane must exceed

\[ T = B_0 + 5\,s\,n \tag{1} \]

In the star's own axes \((x', y')\), with \(B\) the fitted sky, \(A\) the height above it, \(S_x\) and \(S_y\) the
two scales and \(\beta\) the Moffat exponent, the two profiles are

\[ G = B + A\,\exp\!\left[-\left(\frac{x'^2}{S_x} + \frac{y'^2}{S_y}\right)\right], \qquad
   M = B + A\left(1 + \frac{x'^2}{S_x} + \frac{y'^2}{S_y}\right)^{-\beta} \tag{2} \]

With \(W_x \ge W_y\) the fitted full widths at half maximum, the [FWHM](../../reference/glossary.md#fwhm), and \(b\)
the samples of the fitted box, the table's figures are

\[ r = \frac{W_y}{W_x}, \qquad W = \frac{W_x + W_y}{2}, \qquad C = \frac{A}{0.1 + |W_x - W_y|}, \qquad
   \mathrm{Mag} = -2.5 \log_{10} \sum (b - B) \tag{3} \]

\(C\) weighs roundness by brightness, so it is no ratio. With \(t\) the value of **Detection Threshold**, \(d\) that
of **Roundness Tolerance** and \(m_1\), \(m_2\) those of **Min Size** and **Max Size**, Star Profiler v1 keeps a star
when

\[ A \ge t, \qquad \frac{|W_x - W_y|}{2.35482} < d, \qquad m_1 \le W \le m_2 \tag{4} \]

A fit whose \(A\) exceeds \(1 - B_0\), or the brightest sample less \(B_0\) when that is lower, is saturated: it is
drawn magenta, and its residual is not held against it.

### Boundaries

- The search skips a border of **Radius** pixels. An image that is not more than 4 × **Radius** + 8 pixels wide and
  tall is refused with "the searched rectangle is too small for the search radius".
- A raw color-filter mosaic is refused with "stars are measured after the mosaic is resolved, not on the filter
  array": [debayer](../../reference/glossary.md#debayer) it first.
- At most 200,000 candidates are taken from a frame, and every one of them is fitted.
- A pixel the image marks as invalid, or a sample that holds no number, takes the value of the sky, so it stops
  nothing and becomes no star.
- **Cancel**, **Clear** and **Close** stop a detection, which then shows nothing.

### Worked example

The image is the Carina [master](../../reference/glossary.md#master) after Histogram Transformation with **Shadows**
and **Midtones** at 0.02, whose stars [Profiled Starmask Recombination](../star-operations/profiled-starmask-recomb.md)
adds back. The example uses Star Profiler v2, because the master is plate solved and that page loads an `.anspv2`
file. Set **Threshold** to 3.00 and choose **Detect Stars**.

The detection measures the sky of the green channel at 0.278 and its noise at 0.0282, which the saved file records as
`background` and `background_noise`. Equation (1) then gives

\[ T = 0.278 + 5 \times 3 \times 0.0282 = 0.70 \]

where a **Threshold** of 1 gives 0.42, so only the brighter stars pass. The message reads "Star profile detected
successfully! 2187 Detected stars.". The first row is the brightest star, with **Mag** at -8.12 and **FWHMx** at
44.77px, and every row has its **Ra** and **Dec**.

On the stretched master nearly every star core is clipped at 1, so an ellipse's color follows the sky around it. On
the dark sky a star's height exceeds the 0.72 between 0.278 and 1, and it is drawn magenta. On the bright nebula it
stays below, and the star is drawn yellow. Save the list as `carina-stars.anspv2`.

```figure
owed: true
images:
  - image: ../../assets/images/processes/registration-alignment/star-profiler-before.webp
    alt: >-
      A crop at 100 % of the middle of the stretched Carina master, pink clouds of nebula beside a dark dust
      lane and many stars.
    label: Image
  - image: ../../assets/images/processes/registration-alignment/star-profiler-after.webp
    alt: >-
      The same crop with about thirty stars circled, yellow ellipses on the bright nebula and magenta ones on the
      darker part at the lower left, while the fainter stars stay unmarked.
    label: Detected stars
caption: >-
  The middle of the stretched Carina master at 100 % in the Linear display mode, before the detection and with the
  stars Star Profiler v2 found at Threshold 3.00 drawn over it.
```

## Starting points

| Situation | Start | Then adjust |
| --- | --- | --- |
| A first detection | Either window at its defaults | In v2, **Radius** for the crowding of the field, then **Threshold** down in small steps. In v1, the size window, then **Detection Threshold** down. |
| Trails, pairs or poor fits in the list | **Relax PSF Checks** off | **Roundness Threshold** up first. Tick **Relax PSF Checks** only for stars you know it refuses. |
| Stars with broad wings fit with a large **RMSE** | **Gaussian** | **Moffat** on the same stars, then **Minimum Beta** last. |
| A list for a mask or a recombination | A broad detection you trust | Inspect the table, preview a **Stars Filter**, then save. |

## When it goes wrong

!!! warning "A profile is not tied to its image"
    A profile holds positions and nothing that names its image, and loading it checks nothing. On another image, or
    after a crop, a resize or a rotation, its ellipses miss the stars. Load it on the image it was made on, or detect
    again.

!!! warning "Close loses what you did not save"
    Deletions, filters and a detection live only in the window. Save the profile before you close: the question
    **Close** asks does not know whether you did.

"Could NOT detect any star"
:   **Cause.** The threshold is too high, the v1 size window leaves every star out, or the searched plane holds little
    signal.
:   **Fix.** Press **Reset**, then lower the threshold or widen the size window. On an RGB image, try **Use
    Lightness**.

"Error running Star Profiler" with a reason
:   **Cause.** The image is too small for **Radius**, a **Maximum** lies below its lower value, or the image is a raw
    mosaic.
:   **Fix.** Correct what the reason names, and detect again.

Hot pixels, noise or knots of nebula in the list
:   **Cause.** The threshold is low, **Min Size** admits fits narrower than a star, or **Relax PSF Checks** is on.
:   **Fix.** Raise the threshold, raise **Min Size** in v1 or untick **Relax PSF Checks** in v2, then sort by **A**
    and **FWHM** to check what is left.

**Ra** and **Dec** show `-`
:   **Cause.** The image's header holds no plate solution.
:   **Fix.** Plate solve the image, then detect again.

The saved v2 profile holds no catalog matches
:   **Cause.** The image has no plate solution, or no Gaia catalog is installed and ready. A catalog that cannot be
    read writes `StarProfilerV2: Gaia enrichment unavailable:` and the reason to the Session Console.
:   **Fix.** Install the catalog, plate solve the image and detect again.

"Could NOT load the star profile file. Error message:" with a reason
:   **Cause.** The file belongs to the other window, which "The file holds a profile of the second star profiler, and
    this one reads the first's." says, or it is no profile the window can read.
:   **Fix.** Open the file in the window of its version, or use a profile as the window saved it.

The mask covers only the cores
:   **Cause.** **% Pixel Increase** multiplies the star's width, and 0.50 halves it.
:   **Fix.** Raise it toward 1.00, and add **Pixel Increase**.

## Related

- [Profiled Starmask Recombination](../star-operations/profiled-starmask-recomb.md), which puts back the stars of a
  saved profile.
- [Star Mask](../star-operations/star-mask.md), for a star mask made by the same detector without a list.
- [Plate Solving](plate-solving.md), to give the image the plate solution v2 places its stars with.
- [Image Registration](image-registration.md), to align images rather than measure their stars.
- [Settings](../../interface/settings-dialog.md), to install the Gaia catalog on the **Catalogs & Data** page.
- [Process Windows](../../interface/process-windows.md), for what every process window shares.

## Credits

- The star detector is a derived work of the star finder and point-spread-function fit of
  [Siril](https://gitlab.com/free-astro/siril), by Francois Meyer and team free-astro, under the GNU General Public
  License, version 3 or later, and is distributed under that licence. Siril's are the candidate scan and its checks,
  the saturated plateau, the duplicate radius, and the Gaussian and Moffat models with their starting point.
- The six falloff shapes of **Shaped Areas**, their reach of three radii and the larger value kept where stars overlap
  are adapted from the star mask of [DeepSkyStacker](https://github.com/deepskystacker/DSS), by Luc Coiffier and the
  DeepSkyStacker contributors, under the BSD 3-Clause License.
- **About > About AfterNight** credits both works and reproduces the DeepSkyStacker licence's notice.
- The catalog matches of v2 use
  [Gaia Data Release 3](https://gea.esac.esa.int/archive/documentation/GDR3/Miscellaneous/sec_credit_and_citation_instructions/),
  by the European Space Agency's Gaia mission, processed by the Gaia Data Processing and Analysis Consortium
  (ESA/Gaia/DPAC). Gaia data are open and free to use with credit to ESA/Gaia/DPAC.
