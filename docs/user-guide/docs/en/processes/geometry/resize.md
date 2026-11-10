---
kind: process
stage: any
menu: Process > Geometry > Resize
summary: >-
  Resamples an image, or every frame of a sequence, to a new width and height in pixels, through an interpolation you
  choose, for a delivery size or a lighter image to work on.
data_in: either
data_out: either
destructive: true
preview: none
sequences: true
masks: none
external: none
glance:
  enabled: >-
    An image or a sequence view is active, no Resize window is open on it yet, and no window that needs the image to
    itself is open on it.
  inputs: >-
    One image or sequence of any pixel type and any number of channels. A raw color-filter mosaic only after you
    answer Yes to the Resize CFA question.
  outputs: >-
    The same view, replaced, with one history step, at 1 to 100000 pixels a side. Its pixel type, its channels and the
    shape of its pixels are kept. No file, preset or report is written.
  state: >-
    Any image: a calibrated, registered linear image for a resize you will measure, or a duplicate of a finished
    picture for its display size.
derived: false
rewritten: true
---
# Resize

Resize gives an image a new width and height in pixels. You type the new size, or a percentage of the old one, and
choose how each new sample is computed from its neighbors. A resize makes new values: it reveals no detail and adds
no signal that the image did not hold.

**OK** replaces the image, or every frame of a [sequence](../../reference/glossary.md#sequence), with the resized one,
adds one step to its [history](../../reference/glossary.md#history) and closes the window. The
[pixel type](../../reference/glossary.md#pixel-type), the channels and the shape of the pixels are kept, and a
[plate solution](../../reference/glossary.md#plate-solution) is scaled with the pixels, so no new plate solve is
needed.

```figure
image: ../../assets/images/processes/geometry/resize.webp
alt: >-
  The Resize window. A grid with the columns Original Px, Target Px and % Adjust and the rows Width and Height: 7246
  and 5429 under Original Px, 3623 and 2714 under Target Px, 50.00 and 50.00 under % Adjust. Under it Keep aspect
  ratio is ticked and Interpolation reads Auto. Current Size reads 450.19 MB and New Size 112.53 MB. Target Image
  names the master, and Reset, OK and Close run along the bottom.
caption: >-
  Resize on the linear Carina master at 50 percent with Keep aspect ratio on: the Target Px column and New Size show
  the result before OK.
pins:
  - {n: 2, x: 2.0, y: 55.5}
  - {n: 3, x: 2.0, y: 38.6}
  - {n: 4, x: 2.0, y: 64.0}
  - {n: 5, x: 51.0, y: 93.0}
```

## Before you start

- **OK** replaces the image. Its history keeps the step before: duplicate an image you could not load again.
- Choose the size from what comes next: a file to publish, a lighter image to process. An enlarged image takes more
  memory and time and holds no more detail or [SNR](../../reference/glossary.md#snr).
- Every channel is resized, and a [mask](../../reference/glossary.md#mask) on the view is not used.
- A [CFA](../../reference/glossary.md#cfa) mosaic must stay a mosaic for
  [CFA Integer Resample](../calibration/cfa-integer-resample.md), or be debayered first. Resize mixes its filters
  into one gray plane.
- On a sequence, every frame gets the same size and interpolation, whatever frames its player has selected. When the
  view [debayers](../../reference/glossary.md#debayer) the frames, they are debayered before they are resized, and the
  new sequence holds color frames.

## Steps

1. **Open the window.** Make the image the [active image](../../reference/glossary.md#active-image) and choose
   **Process > Geometry > Resize**. **Original Px** shows its size, and **OK** stays disabled while **Target Px** is
   the same.
2. **Keep the proportions.** Leave **Keep aspect ratio** ticked, so one percentage sets both sides. Untick it only to
   change one axis on purpose.
3. **Set the size.** Type the new **Width** or **Height** under **Target Px**, or a percentage under **% Adjust**. The
   other side follows, and **New Size** gives the memory the result will hold.
4. **Choose the interpolation.** Leave **Interpolation** at Auto, which reduces with **Pixel Area Relation** and
   enlarges with **Lanczos4**, or choose another.
5. **Resize.** Choose **OK**. The window closes, the view shows the image at its new size, and its history lists
   **Resize** with both sizes and the interpolation that ran.
6. **Check at 100 %.** Zoom the view to 100 % over bright stars and sharp edges. Blocks or rims around them show what
   the interpolation cost.

## Controls

**Target Px** and **% Adjust** each have a **Width** and a **Height** box, in the rows of the window. What every
process window shares is on [Process Windows](../../interface/process-windows.md).

```controls
window: Resize
controls:
  - name: Original Px
    kind: readout
    default: The image's width and height
    effect: Shows the width and the height of the image now, in pixels.
    scope: Read again when the image changes or another target is chosen.
  - name: Width
    object: targetWidth
    kind: spinbox
    default: The image's width
    widget: {min: 1, max: 100000, step: 1}
    effect: Sets the result's width in pixels, under **Target Px**. The **% Adjust** box of the row shows it as a percentage of the image's width.
    range: With **Keep aspect ratio** ticked, the height takes the same percentage, cut to a whole pixel.
    tradeoff: More pixels take more memory and time, and an enlargement adds no information.
    scope: OK, on every channel and every frame of a sequence.
    persistence: Until the window closes. **Reset** and another target return it to the image's width.
  - name: Width
    object: targetWidthPercent
    kind: spinbox
    default: "100.00"
    widget: {min: 1, max: 1000, step: 1, decimals: 2}
    effect: Sets the result's width as a percentage of the image's, under **% Adjust**. Below 100 it shrinks the image, above 100 it enlarges it.
    range: The width it gives is the image's width times the percentage, cut to a whole pixel. With **Keep aspect ratio** ticked, it sets the height and its percentage too.
    tradeoff: Above 100 the image takes more memory and time without new detail.
    persistence: Until the window closes. **Reset** and another target return it to 100.00.
  - name: Height
    object: targetHeight
    kind: spinbox
    default: The image's height
    widget: {min: 1, max: 100000, step: 1}
    effect: Sets the result's height in pixels, under **Target Px**. The **% Adjust** box of the row shows it as a percentage of the image's height.
    range: With **Keep aspect ratio** ticked, the width takes the same percentage, cut to a whole pixel.
    tradeoff: A height and a width of different percentages stretch every star and object along one axis.
    scope: OK, on every channel and every frame of a sequence.
    persistence: Until the window closes. **Reset** and another target return it to the image's height.
  - name: Height
    object: targetHeightPercent
    kind: spinbox
    default: "100.00"
    widget: {min: 1, max: 1000, step: 1, decimals: 2}
    effect: Sets the result's height as a percentage of the image's, under **% Adjust**.
    range: The height it gives is the image's height times the percentage, cut to a whole pixel. With **Keep aspect ratio** ticked, it sets the width and its percentage too.
    tradeoff: Unequal percentages change the shape of every object.
    persistence: Until the window closes. **Reset** and another target return it to 100.00.
  - name: Keep aspect ratio
    kind: check
    default: "On"
    effect: Ties both sides to one percentage, so a change of either one sets the other.
    range: Ticked again, it keeps the percentage of the side that is larger in pixels and sets the other side to it.
    tradeoff: Whole pixels can leave the two sides scaled by very slightly different amounts. The result's pixels keep their shape all the same, and a plate solution follows the exact scales.
    persistence: Until the window closes. **Reset** ticks it.
  - name: Interpolation
    kind: combo
    default: "Auto (Lanczos4 to enlarge, Pixel Area Relation to reduce)"
    options: ["Auto (Lanczos4 to enlarge, Pixel Area Relation to reduce)", Nearest Neighbor, Bilinear, Bicubic, Pixel Area Relation, Lanczos4, Bit Exact Bilinear, Bit Exact Nearest Neighbor]
    effect: Chooses how each new sample is computed from the samples around it. Auto chooses **Lanczos4** when either side grows, even when the other shrinks, and **Pixel Area Relation** otherwise.
    range: "**Nearest Neighbor** copies one sample. **Bilinear** weighs the four around, **Bicubic** sixteen and **Lanczos4** sixty-four. **Pixel Area Relation** averages the area each new pixel covers when no side grows, and weighs like **Bilinear** when one does. The two **Bit Exact** choices follow the fixed-point rules of OpenCV's `INTER_LINEAR_EXACT` and `INTER_NEAREST_EXACT`."
    tradeoff: "**Nearest Neighbor** is fast and blocky. **Bilinear**, **Bicubic** and **Lanczos4** smooth across their neighbors. **Bicubic** and **Lanczos4** keep edges sharper and can ring, which a guard against dark rings tempers, and they take longer, the guard included. **Pixel Area Relation** suits a reduction."
    scope: OK, on each channel.
    persistence: Until the window closes. **Reset** returns it to Auto.
    note: The history names the interpolation that ran, never Auto.
  - name: Current Size
    kind: readout
    default: The image's size in memory
    effect: Gives the memory the image's samples hold now, for all the frames of a sequence together.
    range: In MB with two decimals, and in GB above 1024 MB.
    tradeoff: It counts the samples only, not the working memory of a run or the compression of a file.
  - name: New Size
    kind: readout
    default: The image's size in memory
    effect: Gives the memory the result's samples will hold, which is the two **Target Px** values times the channels times the bytes of one sample, for every frame of a sequence.
    range: In MB with two decimals, and in GB above 1024 MB.
    tradeoff: It leaves out the working planes, the image the history keeps, the temporary sequence a sequence needs, the header and the compression of a file.
    scope: Counted again with every change of the size.
  - name: Target Image
    kind: combo
    default: The active image
    options: [The active image, Every other open image or sequence the window can work on]
    effect: Chooses the image or sequence OK resizes. Choosing another one returns every control to its default for that image.
    tradeoff: The image chosen here is the one replaced.
    persistence: Until the window closes. **Reset** leaves it as it is. When the image's own size changes, **Original Px** follows, and **Target Px** follows the percentages.
  - name: OK
    kind: button
    default: ""
    effect: Reads the size and the interpolation and resizes the image, or every frame of the sequence. The window closes once the view shows the result.
    enabled: When either **Target Px** value differs from the image's size. Its tooltip then reads "Apply the resize to the target image.", and otherwise "Target dimensions match the current image. Change width or height to enable."
    tradeoff: The history keeps the image as it was beside the result, so the run needs the memory of both. A sequence needs the disk space of the resized copy.
    scope: The whole image, on every channel, without the mask. A change made in the window after you press it does not reach the run.
    persistence: "The image and its history keep the result, as the step **Resize** with the text `Previous size: <w>x<h> - New size: <w>x<h> - Interpolation: <name>`."
    note: "For a mosaic the view does not debayer, it first asks \"Resize CFA\" whether to run anyway, and **No** starts nothing. OK, **Close** and **Reset** are disabled while the run works, and the other controls stay editable. A single image shows no progress window, and its run cannot be stopped once it starts. A run that fails leaves the window open."
  - name: Close
    kind: button
    default: ""
    effect: Closes the window without resizing.
    enabled: Disabled while a run works.
    note: Closing the window another way while a run works hides it, and it closes when the run has ended. The run is not stopped, on a sequence either.
  - name: Reset
    kind: button
    default: ""
    effect: Returns both sides to the image's size, both percentages to 100.00, **Keep aspect ratio** to ticked and **Interpolation** to Auto.
    range: It leaves **Target Image** as it is.
    enabled: Disabled while a run works.
```

```controls
window: Sequence progress
controls:
  - name: Progress
    kind: readout
    default: ""
    effect: "Shows the share of the frames done, from 0 to 100 percent, under the title **Resize** and the text `Executing resize to sequence <name>`."
    enabled: Shown only while OK works on a sequence. A single image has no progress window.
    tradeoff: The resized sequence is written to a temporary file, which needs its disk space until the run ends.
  - name: Cancel
    kind: button
    default: ""
    effect: Stops the run, before the next frame at the latest. The sequence stays as it was, with no history step, and the temporary file is removed.
    note: Closing the Resize window does not stop the run.
```

## How it works

1. **OK** reads the size and the interpolation, and resolves Auto by whether either side grows.
2. A mosaic is read as one gray plane, its filters forgotten, only after you answer **Yes** to the question.
3. Each new sample is computed from the samples around its place in the image, through the interpolation, in
   floating point. A tap beyond the edge reads the edge sample. **Bicubic** and **Lanczos4** then pass the dark-ring
   guard.
4. The result is written back in the image's pixel type, rounded to the nearest value and held to the type's range.
   A floating-point image of
   [fractions of full scale](../../reference/glossary.md#fraction-of-full-scale) is held to 0 to 1. One of
   [sensor codes](../../reference/glossary.md#sensor-code) or of a
   [physical quantity](../../reference/glossary.md#physical-quantity) keeps what was computed.
5. The header follows the rule of every geometry change, which [Crop](crop.md#how-it-works) describes with the
   [header cards](../../reference/glossary.md#header-card) it keeps. The plate solution is scaled, the pixel size is
   restated, and the [processing state](../../reference/glossary.md#processing-state) cards go with the other cards.
   The image replaces the target with one history step, or a sequence is written whole into a new
   [temporary sequence](../../reference/glossary.md#temporary-sequence).

### Model

Let \(W\) and \(H\) be the image's width and height, and \(W'\) and \(H'\) the **Target Px** values. The scales are

\[ s_x = W / W', \qquad s_y = H / H' \tag{1} \]

The new pixel at \((x, y)\) is computed around the place \((u, v)\) of the image, pixel centers being whole numbers:

\[ u = (x + 0.5)\,s_x - 0.5, \qquad v = (y + 0.5)\,s_y - 0.5 \tag{2} \]

Each interpolation but **Nearest Neighbor** weighs the samples \(I_c\) of channel \(c\) around that place with its
kernel \(K\):

\[ O_c(x, y) = \sum_i \sum_j I_c(i, j)\; K(i - u)\; K(j - v) \tag{3} \]

**Nearest Neighbor** takes the sample at column \(\lfloor x\,s_x \rfloor\) and row \(\lfloor y\,s_y \rfloor\).
**Bicubic** uses the cubic kernel with \(a = -0.75\) over four by four samples, and **Lanczos4** a Lanczos window of
radius 4 over eight by eight. **Bit Exact Bilinear** computes in the image's own integers for 8-bit, unsigned 16-bit
and signed 16-bit images, and as **Bilinear** for the other types. **Bit Exact Nearest Neighbor** places its sample
by the rule of equation (2) in 16-bit fixed point and copies a stored value exactly.

The dark-ring guard compares each **Bicubic** or **Lanczos4** sample with a guide: the area average of its pixel when
no side grows, a **Bilinear** sample otherwise. A sample below 0.98 times its guide is replaced by the guide, with its
four neighbors. The guard removes dark rims beside bright stars and does not limit a bright overshoot.

A plate solution's degrees per pixel are multiplied by \(s_x\) and \(s_y\), and its reference pixel moves with the
grid. A distortion is kept, rescaled by the mean of the two scales, while \(s_x\) and \(s_y\) agree within a thousandth of
the larger. **Keep aspect ratio** keeps them that close for a result of 1000 pixels or more a side, and a smaller one
can miss by a pixel. Otherwise the linear solution stays and the distortion goes. The `XPIXSZ` and `YPIXSZ` cards are multiplied by \(s_x\) and \(s_y\),
so the plate scale the optics give is still the result's: a half-size frame of 2.315 µm pixels states 4.63. The
result states the shape of the image's pixels, so a square-pixel image stays square, and can be rotated, after any
two scales.

A resize keeps no sum. **Pixel Area Relation** keeps a local mean, so the background's level, not the total of the
samples. Averaging lowers the noise of each sample but correlates neighbors, and an enlargement spreads the same
values over more pixels: measure noise and [flux](../../reference/glossary.md#flux) again on the result.

### Boundaries

- Integer results are held to their type's range, and a floating-point image of fractions to 0 to 1, so a ring or a
  star above 1 is clipped there.
- A sample the image's [validity rule](../../reference/glossary.md#validity-rule) calls invalid leaves unwritten every
  new sample whose kernel reaches it, and the result marks those invalid in a mask. Where every sample was written,
  the image's rule is kept, and a mask of valid samples is resampled with the samples. Under a rule that calls every
  sample valid, a sample that is not a number takes part, and spreads to its neighbors.
- A rejected size, or a result there is no memory for, is reported as "Error running resize" and in the log, and the
  image, its history and the window stay as they were. When the history cannot keep the image, it says so once in a
  message titled "Image History", and nothing changes.
- When another run holds a sequence, Resize shows "Another process is executing for this sequence, please cancel
  that job first before running this process" under its own title, and starts nothing.
- A sequence keeps its container, with the frames stored as [Crop](crop.md#boundaries) describes.

### Worked example

The linear Carina [master](../../reference/glossary.md#master) is 7246 by 5429 pixels, shown in the view's
[Autostretch](../../reference/glossary.md#autostretch) display. With **Keep aspect ratio** ticked, type 50 in the
**% Adjust** box of **Width**. **Target Px** reads 3623 and 2714: half of 5429 is cut to a whole pixel, and **New
Size** reads 112.53 MB. Auto chooses **Pixel Area Relation**, since no side grows. By equation (1), \(s_x = 2\) and
\(s_y = 5429 / 2714 = 2.00037\), so each new pixel averages a block of two columns and slightly more than two rows.
The background keeps its level, near 0.028. After **OK** the history reads
`Previous size: 7246x5429 - New size: 3623x2714 - Interpolation: Pixel Area Relation`.

## Starting points

| Situation | Start | Then adjust |
| --- | --- | --- |
| A smaller image | **Keep aspect ratio**, the final size, and Auto, which reduces with **Pixel Area Relation** | Compare **Bilinear** and **Lanczos4** at 100 % where stars merge or edges alias. |
| A larger image | Only the size the destination needs, and Auto, which enlarges with **Lanczos4** | Look for rings around stars, then compare **Bicubic** and **Bilinear**. |
| One axis wrong by a known factor | **Keep aspect ratio** unticked, each side set from the measured scales | Check the shape of stars, and plate-solve again if the field had a distortion. |

## When it goes wrong

!!! warning "Resizing back does not undo a resize"
    Each resize computes new values, and a second one does not bring the first image back. Use the image's history to
    return to it, and resize once, to the size you need.

**OK** stays disabled
:   **Cause.** Both **Target Px** values equal the image's size.
:   **Fix.** Change at least one side.

The linked side is one pixel off
:   **Cause.** A percentage times the size is cut to a whole pixel.
:   **Fix.** Type the exact **Target Px** you need in both boxes, or accept the nearest whole pixel.

"Resize CFA" asks whether to run anyway
:   **Cause.** The image is a mosaic its view does not debayer.
:   **Fix.** Choose **No**, then debayer it with Debayer, or resize the mosaic with CFA Integer Resample.

Stars and fine detail look blocky
:   **Cause.** **Nearest Neighbor** enlarged them, or the scale is larger than needed.
:   **Fix.** Undo the step, then compare **Bilinear**, **Bicubic** or **Lanczos4**, and enlarge less.

Star cores look flat, or dark rims became smooth patches
:   **Cause.** The result was held to its range, or the dark-ring guard replaced a ring with its guide.
:   **Fix.** Undo the step and compare **Bilinear** or **Pixel Area Relation**, or resize less.

The result holds samples that are not a number
:   **Cause.** Such samples in the image took part in the interpolation under a rule that calls every sample valid.
:   **Fix.** Repair the samples that are not a number first, then resize again from the original.

"Error running resize"
:   **Cause.** The run refused the size or found no memory for the result.
:   **Fix.** Read the reason after "An error occurred when executing the resize process:", choose a smaller size or
    free memory, and choose **OK** again.

The astrometry overlay is gone after the resize
:   **Cause.** The image's header cards stated no plate solution, so there was none to carry.
:   **Fix.** Plate-solve the resized image.

## Related

- [Crop](crop.md), to remove borders first without changing a sample, and for the header cards every geometry change
  keeps.
- [Integer Resample](integer-resample.md), to enlarge by whole blocks or bin by a whole factor without interpolating.
- [Rotate](rotate.md), to turn the image by any angle.
- [CFA Integer Resample](../calibration/cfa-integer-resample.md) and [Debayer](../calibration/debayer.md), for a raw
  mosaic.
- [Plate Solving](../registration-alignment/plate-solving.md), to solve again when a distortion was dropped.
- [Process Windows](../../interface/process-windows.md), for what every process window shares.
