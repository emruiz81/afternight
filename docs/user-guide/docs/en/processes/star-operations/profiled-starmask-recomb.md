---
kind: process
stage: finish
menu: Process > Star Processing > Profiled Starmask Recombination
summary: >-
  Adds a stars image back to a matching starless image only where the stars of a saved Star Profiler list are, through
  a mask it paints from that list.
data_in: nonlinear
data_out: nonlinear
destructive: false
preview: real-time
sequences: false
masks: none
external: none
glance:
  enabled: >-
    Always. The window chooses its images itself and opens without an active image. Preview and OK wait for a
    starless image and a stars image, and the profile controls for a profile with stars.
  inputs: >-
    A starless image and a stars image of the same width, height and channels, one or three, and a star profile saved
    by Star Profiler v1 or v2, as an `.anspv1` or `.anspv2` file, made on the same image.
  outputs: >-
    One new image named after the two images with " - MERGED", beside the starless image, in the higher of their two
    pixel types. The inputs and the profile file stay as they are. No file is written.
  state: >-
    A starless image and a stars image of the same stretched image, aligned and at the same stage, with a profile
    detected before anything cropped, resized or rotated that image.
derived: true
---
# Profiled Starmask Recombination

Profiled Starmask Recombination puts back on a [starless image](../../reference/glossary.md#starless-image) only the
stars of a star profile. It paints a [mask](../../reference/glossary.md#mask) from a profile Star Profiler saved, one
area for each star, and adds the stars image to the starless one through it. The rest of the stars image stays out.
Each area is a filled ellipse of the star's fit, softened, or one of six falloffs, and the stars can have their edges
tightened first. The window chooses its images itself, and opens without an
[active image](../../reference/glossary.md#active-image).

**OK** opens the result as a new image beside the starless image and closes the window. The two images and the
profile file stay as they were. The result is recorded as nonlinear.

```figure
image: ../../assets/images/processes/star-operations/profiled-starmask-recomb.webp
alt: >-
  The Profiled Starmask Recombination window. On the left, the Image Composition group names a starless image and a
  stars image of the Carina nebula. The Stars Profile group shows a file path, a star count, the Stars Filter button,
  Show Star Profiles ticked, Invert Selection, Scale Factor at 1.00 and Tighten Edges at 0.00 with their sliders, and
  the Mask Generation group with Shape at Binary Areas, Soften Edges at 2.00, and two dimmed sliders. Preview is
  ticked. On the right, the preview shows the nebula with its brighter stars, circled by the profile's areas. Reset
  runs along the bottom on the left, OK and Close on the right.
caption: >-
  Profiled Starmask Recombination of the starless Carina image and its star image through a profile of the brighter
  stars, with Binary Areas at their defaults. The preview shows what OK writes.
pins:
  - {n: 2, x: 3.5, y: 21.9}
  - {n: 3, x: 3.5, y: 37.2}
  - {n: 4, x: 31.1, y: 39.7}
  - {n: 5, x: 3.5, y: 60.6}
  - {n: 6, x: 3.5, y: 51.4}
  - {n: 7, x: 13.5, y: 89.8}
  - {n: 8, x: 75.0, y: 96.1}
```

## Before you start

- Make the two images from one image at the same stage, so they are aligned pixel for pixel. Use a stars image made
  for addition, the image less its starless version, as Starnet++ makes with **Subtractive Mask**.
- Make the profile with [Star Profiler](../registration-alignment/star-profiler.md) on that image, before anything
  crops, resizes or rotates it, and save it. Its positions are not checked against the images.
- Keep floating-point images when the stars may add up above 1. A pair that includes an integer image is added as
  [fractions of full scale](../../reference/glossary.md#fraction-of-full-scale), and the sum is held at 1.
- Use still images. The window is made for one image, not a [sequence](../../reference/glossary.md#sequence).
- Without a profile, or with one that holds no star, the window puts every star back. Check **Current Stars**
  before OK.

## Steps

1. **Open the window.** Choose **Process > Star Processing > Profiled Starmask Recombination**. The image boxes read
   `<--NO IMAGE-->`, the profile's controls are disabled and **Preview** is off.
2. **Choose the images.** Pick the starless image in **Starless Image** and the stars in **Stars Image**. **Preview**
   and **OK** enable.
3. **Load the profile.** Choose **...** and pick the `.anspv1` or `.anspv2` file, or type its path in **Path**.
   **Current Stars** shows how many stars it holds, and the profile's controls enable.
4. **Keep the stars you want.** To put back only some, choose **Stars Filter**, narrow its ranges and confirm with
   **OK**. **Current Stars** shows the new count.
5. **Shape the mask.** Leave **Shape** at **Binary Areas** and adjust **Scale Factor** and **Soften Edges**, or choose
   a falloff and adjust **% Pixel Increase** and **Pixel Increase**.
6. **Tighten the stars, if their edges are wide.** Raise **Tighten Edges** in small steps.
7. **Check the preview.** Tick **Preview**. **Show Star Profiles** draws the profile's areas over it. Zoom to 100 %
   on the stars and hold ++space++ to see the starless image.
8. **Choose OK.** The result opens beside the starless image as `<starless> - <stars> - MERGED`, and the window
   closes.

## Controls

The preview, its toolbar and its progress window are those of every process window: see
[Process Windows](../../interface/process-windows.md). The original the preview switches to is the starless image,
and its display follows the starless image's view.

```controls
window: Profiled Starmask Recombination
controls:
  - name: Starless Image
    kind: combo
    default: "<--NO IMAGE-->"
    options: ["<--NO IMAGE-->", Every open image or sequence]
    effect: Chooses the base the stars are added to. It becomes the window's target, gives the result its first name, its header and its record, and sets the preview's display.
    range: The list holds every open view, whatever its size, channels, pixel type or state.
    tradeoff: A pair that is not aligned, or not at the same stage, gives doubled, shifted or missing structure.
    scope: The preview, a snapshot and OK. Its view is never changed.
    persistence: Until the window closes. Closing the image returns the box to `<--NO IMAGE-->`.
  - name: Stars Image
    kind: combo
    default: "<--NO IMAGE-->"
    options: ["<--NO IMAGE-->", Every open image or sequence]
    effect: Chooses the stars added to the starless image, as the image its view shows when a run is asked for. The mask is painted at its size.
    range: The run refuses an image whose width, height or channels differ from the starless image's.
    tradeoff: Whatever it holds besides stars is added too, where the mask lets it. A pixel type higher than the starless image's becomes the result's.
    scope: The preview, a snapshot and OK, cropped to a preview area as the starless image is.
    persistence: Until the window closes. Its name is the second part of the result's.
  - name: Path
    kind: field
    default: ""
    effect: Names the profile file. Each change of the text reads the file again, in the background, and the last change wins. Empty, the profile is unloaded.
    range: >-
      A `.anspv1` file of Star Profiler v1 or a `.anspv2` file of Star Profiler v2. The version is read from the file's
      content, not its name. A v1 star is a circle of its radius, and a v2 star an ellipse of its fit.
    tradeoff: A path typed by hand is read at each keystroke, and a file that cannot be read reports an error each time.
    scope: The star list and the mask. The file itself is never written.
    persistence: Until the window closes. The history of the result does not record the path.
    note: >-
      A file that cannot be read shows "Could NOT load the star profile file. Error message:" and the reason, under
      **Load Star Profile**. The list is then empty, and a run puts every star back.
  - name: "..."
    kind: button
    default: ""
    effect: Opens a file dialog, **Load star profile...**, that shows `.anspv1` and `.anspv2` files, and writes the file you choose into **Path**.
    persistence: The dialog opens in the folder you used last, also in a later session.
  - name: Reload stars profile file
    kind: button
    default: ""
    effect: Reads the file in **Path** again, which brings back the stars a **Stars Filter** removed.
    enabled: While **Path** holds text.
  - name: Unload stars profile
    kind: button
    default: ""
    effect: Empties **Path**, closes an open **Stars Filter** and unloads the star list, so a run puts every star back.
    enabled: While **Path** holds text.
  - name: Current Stars
    kind: readout
    default: "0"
    effect: Shows how many stars the list holds. While a **Stars Filter** previews into the window's preview, it shows the filtered count.
    enabled: Dimmed until a profile with stars is loaded.
    tradeoff: At 0 no mask is painted and the whole stars image is added, even when the file was read.
  - name: Stars Filter
    kind: button
    default: ""
    effect: Opens the **Stars Filter** dialog, which keeps a part of the list.
    enabled: While both images are chosen and the list holds stars. Changing **Path**, or clearing an image box, closes the dialog.
  - name: Show Star Profiles
    kind: check
    default: "On"
    effect: Draws each star of the list over the preview, at the size **Scale Factor** gives it. It changes nothing OK writes.
    enabled: While the list holds stars. It draws only while the preview is shown.
    tradeoff: A dense field of areas can hide what you want to look at.
    persistence: Until the window closes. **Reset** leaves it as it is.
  - name: Invert Selection
    kind: check
    default: "Off"
    effect: Off, the stars come back inside the areas and the starless image stays outside them. On, the mask is read the other way round, so everything but the listed stars comes back.
    enabled: While the list holds stars.
    tradeoff: The wrong way round, it brings back the unwanted stars and whatever else the stars image holds.
    persistence: Until the window closes. **Reset** turns it off.
  - name: Scale Factor
    kind: spinbox
    default: "1.00"
    widget: {min: 0, max: 2, step: 0.05, decimals: 2}
    effect: Multiplies the axes of each **Binary Areas** ellipse. Higher gives larger areas, lower smaller ones.
    range: >-
      At 0, a run with **Binary Areas** and a profile is refused with "the size scale is finite and positive". The
      falloffs do not use it.
    enabled: While the list holds stars.
    tradeoff: Large areas merge in a crowded field and bring back more of the stars image around them.
    scope: The mask, the drawing of **Show Star Profiles** and the neighbors **Include Neighbors** of the **Stars Filter** finds.
    persistence: Until the window closes. **Reset** returns it to its default.
  - name: Tighten Edges
    kind: spinbox
    default: "0.00"
    widget: {min: 0, max: 2, step: 0.03, decimals: 2}
    effect: Darkens the edges of the stars before they are added, by subtracting their edge strength. Higher shrinks halos more.
    range: Equation (4) gives it. Beyond every edge of the frame the edge is read against 0.
    enabled: While the list holds stars.
    tradeoff: Too high, it leaves dark rims, erases edges and fringes colors. The frame's borders can darken a little.
    scope: The whole stars image, every channel on its own, before the mask is applied.
    persistence: Until the window closes. **Reset** returns it to 0.
  - name: Shape
    kind: combo
    default: Binary Areas
    options: [Binary Areas, Bell, Truncated Bell, Linear, Truncated Linear, Cubic, Quadratic]
    effect: Chooses the area of each star. **Binary Areas** paints a filled ellipse of the star's fit, then softens it. Every other choice paints a round falloff from the center, by the formula of table (3).
    enabled: The group waits for a list with stars. **Binary Areas** enables **Soften Edges**, the falloffs **% Pixel Increase** and **Pixel Increase**.
    tradeoff: The falloffs are round, so they drop the shape and angle of a v2 star. Where areas overlap, the larger value counts.
    persistence: Until the window closes. **Reset** returns it to **Binary Areas**.
    note: Quadratic uses a fourth power.
  - name: Soften Edges
    kind: spinbox
    default: "2.00"
    widget: {min: 0, max: 10, step: 0.03, decimals: 2}
    effect: Blurs the ellipses of **Binary Areas** with a Gaussian of this standard deviation, in pixels. Higher gives a wider, softer transition. At 0 the edges stay hard.
    enabled: With **Binary Areas**.
    tradeoff: Wide softening also brings back the stars image's background around each star.
    persistence: Until the window closes. **Reset** returns it to its default.
  - name: "% Pixel Increase"
    kind: spinbox
    default: "0.50"
    widget: {min: 0, max: 1, step: 0.03, decimals: 2}
    effect: Multiplies the star's larger full width to give the falloff's radius. 0.50 is half the width, not an increase of half.
    range: A value below 0.01 counts as 0.01.
    enabled: With a falloff in **Shape**.
    tradeoff: Small values cut the stars short. Large ones merge their wings.
    persistence: Until the window closes. **Reset** returns it to its default.
  - name: Pixel Increase
    kind: spinbox
    default: "0"
    widget: {min: 0, max: 20, step: 1}
    effect: Adds this many pixels to the falloff's radius, after **% Pixel Increase**.
    enabled: With a falloff in **Shape**.
    tradeoff: It counts most on small stars, and large values add overlap and work.
    persistence: Until the window closes. **Reset** returns it to 0.
  - name: Preview
    kind: check
    default: "Off"
    effect: Shows the recombined image beside the controls, as in every process window. It opens off.
    enabled: While both images are chosen. A profile is not needed.
    tradeoff: Each change paints the mask again and adds the images again over the whole frame. On a large image, set the mask with it off, then check.
    persistence: Until the window closes. **Reset** leaves it as it is.
  - name: OK
    kind: button
    default: ""
    effect: Paints the mask from the list, adds the stars through it and opens the result beside the starless image as `<starless> - <stars> - MERGED`. The window then closes.
    enabled: While both images are chosen. A profile is not needed, and the images are not checked until the run.
    tradeoff: The run holds several copies of the image at once, such as the mask, the tightened stars and the sum. Painting takes longer with more stars and larger areas, and its result does not depend on the order of the list.
    scope: The whole image, with the stars image, the list and every setting as they are when you press it. A change made afterwards reaches the next run only.
    persistence: >-
      The new image's history starts with a **Profiled Starmask Recombination** step that reads
      `ProfiledStarmaskRecombinationProcess: Image created from starless image '<starless>' and starmask image '<stars>'
      using operation add;` followed by the number of profiled stars, the shape with its settings, the tightening, the
      inversion, or `unmasked` for a list without stars.
    note: The painted mask is not kept as an image.
  - name: Close
    kind: button
    default: ""
    effect: Closes the window without a run, as in every process window.
    note: An image made by an earlier OK or a snapshot stays as it is.
  - name: Reset
    kind: button
    default: ""
    effect: Returns **Scale Factor**, **Tighten Edges**, **Invert Selection**, **Shape**, **Soften Edges**, **% Pixel Increase** and **Pixel Increase** to their defaults.
    range: It leaves the images, **Path**, the star list, a filtered list included, **Show Star Profiles** and **Preview** as they are.
```

```controls
window: The Stars Filter dialog
controls:
  - name: Mag Min
    kind: spinbox
    default: The list's faintest magnitude
    widget: {step: 0.1, decimals: 2}
    effect: Keeps the stars at least this bright. Lower keeps fewer, fainter ones go first.
    range: >-
      Between the list's faintest and brightest instrumental magnitudes. A star is kept while
      **Mag Max** ≤ magnitude ≤ **Mag Min**, each end widened by 0.01. A v1 profile holds no magnitudes, so the box
      does not filter it.
  - name: Mag Max
    kind: spinbox
    default: The list's brightest magnitude
    widget: {step: 0.1, decimals: 2}
    effect: Keeps the stars at most this bright. Higher drops the brightest.
    range: Between the list's brightest and faintest magnitudes.
  - name: FWHMx Min
    kind: spinbox
    default: The list's smallest width
    widget: {step: 0.1, decimals: 2}
    effect: Keeps the stars whose full width along their major axis is at least this many pixels.
    range: Between the list's smallest and largest widths, each end widened by 0.01. A v1 star's width is half its radius.
  - name: FWHMx Max
    kind: spinbox
    default: The list's largest width
    widget: {step: 0.1, decimals: 2}
    effect: Keeps the stars whose full width along their major axis is at most this many pixels.
    range: Between the list's smallest and largest widths.
  - name: FWHMy Min
    kind: spinbox
    default: The list's smallest width
    widget: {step: 0.1, decimals: 2}
    effect: Keeps the stars whose full width along their minor axis is at least this many pixels.
    range: Between the list's smallest and largest widths.
    tradeoff: Narrowing one axis alone favors round or elongated stars. Narrow both alike.
  - name: FWHMy Max
    kind: spinbox
    default: The list's largest width
    widget: {step: 0.1, decimals: 2}
    effect: Keeps the stars whose full width along their minor axis is at most this many pixels.
    range: Between the list's smallest and largest widths.
  - name: Random selection
    kind: spinbox
    default: "100.00"
    widget: {min: 0, max: 100, step: 0.5, decimals: 2}
    effect: Keeps this share, in percent, of the stars the ranges keep, chosen at random. 100 keeps them all, 0 none.
    range: The draw is fixed when the dialog opens, so the same stars stay chosen while it is open.
  - name: Preview
    kind: check
    default: "On"
    effect: While the window's **Preview** is on, draws the filtered list over the preview and uses it for the preview as you change the ranges. Off draws the whole list again.
    tradeoff: A long list can make each change slow.
    scope: The drawing and the preview only. Nothing is kept until **OK**.
  - name: Include Neighbors
    kind: check
    default: "Off"
    effect: Also keeps every star left out whose area overlaps a kept star's, and then their neighbors in turn. An area is two full widths on each side of the center, times **Scale Factor**.
    enabled: It acts while a range is narrowed or **Random selection** is below 100.
    tradeoff: In a crowded field it can bring back most of the list.
  - name: "Filtered Stars:"
    kind: readout
    default: kept/total
    effect: Shows how many stars the filter keeps, out of the list it started from.
  - name: OK
    kind: button
    default: ""
    effect: >-
      Asks "This change will discard the filtered stars from the list, if you want those stars back you'll need to
      load the profile again, or run a new detection. Do you want to continue?" Yes replaces the window's star list
      with the filtered one and closes the dialog. No changes nothing.
    persistence: The profile file is not changed. **Reload stars profile file** brings the whole list back.
  - name: Reset
    kind: button
    default: ""
    effect: Returns the ranges to the list's extremes and **Random selection** to 100.
    range: It leaves **Preview** and **Include Neighbors** as they are.
  - name: Cancel
    kind: button
    default: ""
    effect: Closes the dialog and puts the list it started from back, in the drawing and the preview. Closing the dialog another way does the same.
```

## How it works

1. The profile is read in the background, and its stars become the window's list. A confirmed **Stars Filter**
   replaces the list in memory only.
2. On each run, the mask is painted from the list over the whole stars image, one channel of 32-bit floating point,
   and cropped like the images to a [preview area](../../reference/glossary.md#preview-area). Without stars, no mask is
   painted.
3. Both images are read in one way: two floating-point images that state a
   [physical quantity](../../reference/glossary.md#physical-quantity) as they are, any other pair as fractions of full
   scale. With **Tighten Edges** above 0, the stars' edges are darkened first.
4. Each sample of the stars is added to the starless image in double precision, held at 1 for fractions, and blended
   over the starless image by the mask or its inverse. The result is written once in the higher of the two
   [pixel types](../../reference/glossary.md#pixel-type), rounded to the nearest code.
5. The result keeps the starless image's [header cards](../../reference/glossary.md#header-card), acquisition data,
   exposure included, and [plate solution](../../reference/glossary.md#plate-solution). Its
   [lineage](../../reference/glossary.md#lineage) adds the recombination, and it is recorded as nonlinear with the
   operation `profiled-starmask-recombination`.

### Model

Let \(W_x\) and \(W_y\) be a star's full widths along its two axes, in pixels, its
[FWHM](../../reference/glossary.md#fwhm), and \(k\) the value of **Scale Factor**. **Binary Areas** paints, at full
value, every pixel at least half inside the ellipse of axes

\[ 4\,k\,W_x \quad\text{and}\quad 4\,k\,W_y \tag{1} \]

turned by the star's fitted angle, then blurs the mask by **Soften Edges**. A v1 star of radius \(r\) has
\(W_x = W_y = r/2\), so it paints a circle of radius \(k\,r\). A falloff takes the star's larger width, the value
\(p\) of **% Pixel Increase** and the \(q\) pixels of **Pixel Increase**, for the radius

\[ \rho = p \max(W_x, W_y) + q \tag{2} \]

and gives a pixel at the distance \(d\) from the center the value below, over the square of \(3\rho\) on each side of
it. A star whose larger width is over 50 pixels is not painted by a falloff.

| Shape | Value at the distance \(d\) (3) |
| --- | --- |
| Bell | \( e^{-d^2 / 2\rho^2} \) |
| Truncated Bell | \( \min\bigl(1,\ e^{1/2}\,e^{-d^2 / 2\rho^2}\bigr) \) |
| Linear | \( \max\bigl(0,\ 1 - d / 3\rho\bigr) \) |
| Truncated Linear | \( \min\bigl(1,\ \max(0,\ 1.5 - 1.5\,d / 3\rho)\bigr) \) |
| Cubic | \( \max\bigl(0,\ 1 - (d / 3\rho)^3\bigr) \) |
| Quadratic | \( \max\bigl(0,\ 1 - (d / 3\rho)^4\bigr) \) |

Let \(S\) be a stars sample, \(E(S)\) the sum of the absolute values of its four 3 by 3 Sobel responses, horizontal,
vertical and the two diagonals, in its own channel, and \(\tau\) the value of **Tighten Edges**. The stars added are

\[ S' = S - 0.05\,\tau\,E(S) \tag{4} \]

held between 0 and 1 for fractions. With \(B\) the starless sample and \(A\) the mask there,

\[ R = \min(B + S',\ 1), \qquad O = B\,(1 - A) + R\,A \tag{5} \]

where two images of physical quantities are not held at 1, and **Invert Selection** replaces \(A\) by \(1 - A\).
Without a mask, \(O = R\).

### Boundaries

- A pair whose size or channels differ is refused with "the star image does not have the starless image's size and
  channels", and an image of other than one or three channels with "a recombination reads one channel or three".
- The profile's positions are not checked against the images. Areas outside the frame are cut off, and a profile of
  another image puts the areas in the wrong places.
- A list without stars paints no mask, and the whole sum comes back, with or without **Invert Selection**.
- A pixel the stars image marks as invalid keeps the starless image's samples, and a sample the starless image marks
  as invalid is written as 0. A sample that is not a number in a valid pixel stops the run with "a source holds a
  sample that is not finite in a pixel its validity admits".
- Painting the mask and adding the images stop when the run is stopped, and a stopped run writes nothing.

### Worked example

The two images come from the Carina [master](../../reference/glossary.md#master) after Histogram Transformation with
**Shadows** and **Midtones** at 0.02: [Starnet++](starnet.md) made the starless image and, with **Subtractive Mask**,
the star image. Star Profiler v2 detected the profile on the same stretched master with **Threshold** at 3.00, so it
holds the brighter stars, 2,187 of them, and saved it as an `.anspv2` file. Choose the two images, load the file, leave **Shape**,
**Scale Factor**, **Soften Edges** and **Tighten Edges** at their defaults, and choose **OK**.

A star of 3 pixels full width gets an ellipse 12 pixels across by equation (1), softened over about 2 pixels. Where a
starless sample of 0.20 meets a stars sample of 0.30, equation (5) gives 0.50 at the star, where the mask is 1, 0.35
on its softened edge at 0.5, and 0.20 away from every star. **Invert Selection** turns these into 0.20, 0.35 and
0.50.

```figure
images:
  - image: ../../assets/images/processes/star-operations/profiled-starmask-recomb-before.webp
    alt: A crop at 100 % of the middle of the starless Carina image, the nebula and its dark lanes with no stars.
    label: Before
  - image: ../../assets/images/processes/star-operations/profiled-starmask-recomb-after.webp
    alt: The same crop of the merged image, with the brighter stars back in place and the faint ones still absent.
    label: After
caption: >-
  The middle of the starless Carina image at 100 %, and of the image OK made by adding back the stars of a profile of
  the brighter stars. Both crops are shown in the Linear display mode.
```

## Starting points

| Situation | Start | Then adjust |
| --- | --- | --- |
| Put back the listed stars | **Binary Areas**, **Scale Factor** and **Soften Edges** at their defaults | Filter the list, then **Scale Factor**, then **Soften Edges**, then **Tighten Edges**. |
| A graded transition around each star | **Truncated Bell**, **% Pixel Increase** and **Pixel Increase** at their defaults | Filter the list, then **% Pixel Increase**, then **Pixel Increase**. |
| Halos come back too wide | **Tighten Edges** at 0 | Fix the areas first, then raise **Tighten Edges** in small steps at 100 %. |
| Everything but the listed stars | A mask checked without **Invert Selection** | Then tick **Invert Selection**. |

## When it goes wrong

!!! warning "Without stars in the list, every star comes back"
    No profile, a profile that could not be read, one that holds no star or a filter that kept none: the window
    then adds the whole stars image, as Starmask Recombination does. Check **Current Stars** before OK.

Every star comes back
:   **Cause.** **Current Stars** reads 0, so no mask was painted.
:   **Fix.** Load a profile with stars, or **Reload stars profile file**, and check the count.

"Could NOT load the star profile file. Error message:" with a reason
:   **Cause.** The file in **Path** is not a profile the window can read.
:   **Fix.** Choose a `.anspv1` or `.anspv2` file saved by Star Profiler.

The areas miss the stars
:   **Cause.** The profile was made on another image, or one of the images was cropped, resized or rotated after it.
:   **Fix.** Detect the profile again on the image the two images came from, as it is now.

"the size scale is finite and positive"
:   **Cause.** **Scale Factor** is at 0 with **Binary Areas**.
:   **Fix.** Raise **Scale Factor**.

"the star image does not have the starless image's size and channels"
:   **Cause.** The two images differ in width, height or number of channels.
:   **Fix.** Make both from the same image, with the same size and channels.

Dark rims or colored borders around stars
:   **Cause.** **Tighten Edges** is too high.
:   **Fix.** Lower it, and look at the stars and the frame's borders at 100 %.

A falloff mask looks round on elongated stars
:   **Cause.** The falloffs use the larger width as a radius and drop the star's shape.
:   **Fix.** Use **Binary Areas**, which keeps the ellipse of the fit.

Bright cores are flat at the top
:   **Cause.** The pair includes an integer image, or one in fractions of full scale, so the sum is held at 1.
:   **Fix.** Convert both images to floating point first, or dim the stars image before you add it.

## Related

- [Star Profiler](../registration-alignment/star-profiler.md), to detect, filter and save the profile.
- [Starnet++](starnet.md), to make the starless image and a stars image to add back.
- [Starmask Recombination](starmask-recombination.md), to add the whole stars image, with a mask image of your own.
- [Star Mask](star-mask.md), to paint the same kinds of star areas into a mask image without a profile.
- [Process Windows](../../interface/process-windows.md), for the preview and what every process window shares.

## Credits

- The six falloff shapes of a shaped star mask, their reach of three radii and the larger value kept where stars
  overlap are adapted from the star mask of [DeepSkyStacker](https://github.com/deepskystacker/DSS), Copyright (c)
  2006-2019 Luc Coiffier and Copyright (c) 2018-2023 David C. Partridge, Tony Cook, Mat Draper, Simon C. Smith,
  Vitali Pelenjow, Michal Schulz and Martin Toeltsch, under the BSD 3-Clause License, whose notice **About >
  About AfterNight** reproduces.
