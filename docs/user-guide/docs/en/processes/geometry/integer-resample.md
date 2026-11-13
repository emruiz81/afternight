---
kind: process
stage: any
menu: Process > Geometry > Integer Resample
summary: >-
  Makes an image, or every frame of a sequence, smaller or larger by a whole factor: it combines each block of samples
  into one, or copies each sample into a block, without interpolating.
data_in: either
data_out: either
destructive: true
preview: none
sequences: true
masks: none
external: none
glance:
  enabled: >-
    An image or a sequence view is active, no Integer Resample window is open on it yet, and no window that needs the
    image to itself is open on it.
  inputs: >-
    One image or sequence of any pixel type and any number of channels. A raw color-filter mosaic only after you
    answer Yes to the Integer Resample CFA question.
  outputs: >-
    The same view, replaced, with one history step: smaller by the factor, rounded down, or larger by it. Its pixel
    type, its channels and the shape of its pixels are kept. No file, preset or report is written.
  state: >-
    Any image: a calibrated linear image to bin for less noise in each pixel, or a duplicate to enlarge by whole
    blocks.
derived: false
---
# Integer Resample

Integer Resample changes an image's size by a whole factor. **Downsample** combines each square block of samples, the
factor wide, into one sample, by the average, the median, the maximum or the minimum. **Upsample** copies each sample
into such a block, without interpolating. Use it when an exact block rule matters more than a size of your choice.

**OK** replaces the image, or every frame of a [sequence](../../reference/glossary.md#sequence), with the resampled
one, adds one step to its [history](../../reference/glossary.md#history) and closes the window. The
[pixel type](../../reference/glossary.md#pixel-type), the channels and the shape of the pixels are kept, and a
[plate solution](../../reference/glossary.md#plate-solution) is scaled with the pixels, so no new plate solve is
needed.

```figure
image: ../../assets/images/processes/geometry/integer-resample.webp
alt: >-
  The Integer Resample window. In the Resample group, Resample Factor at 2, Downsample Mode at Average, and the
  Downsample and Upsample buttons with Downsample chosen. In the New Size group, Original Px reads 7246 and 5429 and
  Target Px 3623 and 2714, with Current Size at 450.19 MB and New Size at 112.53 MB. Under them Target Image names the
  master, and Reset, OK and Close run along the bottom.
caption: >-
  Integer Resample on the linear Carina master, binning by 2 with Average, as the window opens: Target Px and New Size
  show the result before OK.
pins:
  - {n: 2, x: 28.0, y: 35.5}
  - {n: 3, x: 5.5, y: 28.4}
  - {n: 5, x: 71.5, y: 58.6}
  - {n: 6, x: 58.5, y: 94.8}
```

## Before you start

- **OK** replaces the image. **Downsample** discards detail, and **Upsample** multiplies the memory without adding
  any. The image's history keeps the step before.
- A side that the factor does not divide loses its last rows or columns in **Downsample**. Crop the image to a
  multiple of the factor first when every edge matters.
- A [CFA](../../reference/glossary.md#cfa) mosaic must stay a mosaic for
  [CFA Integer Resample](../calibration/cfa-integer-resample.md), or be debayered first. Integer Resample mixes its
  filters into one gray plane.
- Every channel is resampled, and a [mask](../../reference/glossary.md#mask) on the view is not used.
- On a sequence, every frame gets the same factor, direction and reducer, whatever frames its player has selected.
  When the view [debayers](../../reference/glossary.md#debayer) the frames, they are debayered first, and the new
  sequence holds color frames.

## Steps

1. **Open the window.** Make the image the [active image](../../reference/glossary.md#active-image) and choose
   **Process > Geometry > Integer Resample**. The window opens ready to bin by 2 with **Average**, so **OK** is
   enabled at once.
2. **Choose the direction.** Choose **Downsample** to combine each block into one sample, or **Upsample** to copy each
   sample into a block.
3. **Set the factor.** Type **Resample Factor**. For **Downsample** it must be no larger than the image's shorter
   side.
4. **Choose how a block is combined.** For **Downsample**, choose **Downsample Mode**. **Upsample** disables it.
5. **Check the size.** **Target Px** shows the result's width and height, and **New Size** the memory it will hold.
6. **Resample.** Choose **OK**. The window closes, the view shows the image at its new size, and its history lists
   **Integer Resample** with the factor and the size.

## Controls

What every process window shares is on [Process Windows](../../interface/process-windows.md).

```controls
window: Integer Resample
controls:
  - name: Resample Factor
    kind: spinbox
    default: "2"
    widget: {min: 2, max: 100, step: 1}
    effect: Sets the factor. Higher makes the image smaller with **Downsample**, larger with **Upsample**.
    range: The window lets it exceed a side of the image, and the run then refuses a **Downsample**.
    tradeoff: A larger **Downsample** factor loses more detail. An **Upsample** factor multiplies the memory by its square.
    scope: OK, on every channel and every frame of a sequence.
    persistence: Until the window closes. **Reset** and another target return it to 2.
  - name: Downsample Mode
    kind: combo
    default: Average
    options: [Average, Median, Maximum, Minimum]
    effect: Chooses how the samples of a block become one. **Average** takes their mean, **Median** their middle value, **Maximum** the brightest and **Minimum** the faintest.
    enabled: With **Downsample** chosen.
    tradeoff: "**Average** smooths. **Median** removes an isolated outlier, and can soften small stars and peaks. **Maximum** raises the image and **Minimum** lowers it."
    scope: Each channel on its own.
    persistence: Until the window closes. **Reset** and another target return it to **Average**.
  - name: Downsample / Upsample
    kind: radio
    default: Downsample
    options: [Downsample, Upsample]
    effect: "**Downsample** divides both sides by the factor, rounded down, and combines each block by **Downsample Mode**. **Upsample** multiplies both sides by the factor and copies each sample into a block."
    tradeoff: "**Downsample** leaves fewer samples and less work, and what it merges or discards is gone. **Upsample** makes visible blocks and takes the factor's square in memory, with no new detail."
    scope: OK, on every channel and every frame of a sequence.
    persistence: Until the window closes. **Reset** and another target return it to **Downsample**.
  - name: Original Px
    kind: readout
    default: The image's width and height
    effect: Shows the width and the height of the image now, in pixels.
    scope: Read again when the image changes or another target is chosen.
  - name: Target Px
    kind: readout
    default: Half the image's width and height, rounded down
    effect: Shows the result's width and height, which the history records too.
    range: A **Downsample** factor larger than a side shows 1 for it, a size the run then refuses. A very large **Upsample** shows at most 2147483647.
    scope: Counted again with the factor, the direction and any change of the image's size.
  - name: Current Size
    kind: readout
    default: The image's size in memory
    effect: Gives the memory the image's samples hold now, for all the frames of a sequence together.
    range: In MB with two decimals, and in GB above 1024 MB.
    tradeoff: It counts the samples only, not the working memory of a run or the compression of a file.
  - name: New Size
    kind: readout
    default: A quarter of Current Size
    effect: Gives the memory the result's samples will hold, from **Target Px**, the channels and the bytes of one sample, for every frame of a sequence.
    range: In MB with two decimals, and in GB above 1024 MB.
    tradeoff: It can read too little for a very large **Upsample**, whose **Target Px** is cut. It leaves out the image the history keeps, the temporary sequence a sequence needs and the compression of a file.
  - name: Target Image
    kind: combo
    default: The active image
    options: [The active image, Every other open image or sequence the window can work on]
    effect: Chooses the image or sequence OK resamples. Choosing another one returns every control to its default.
    tradeoff: The image chosen here is the one replaced.
    persistence: Until the window closes. **Reset** leaves it as it is.
  - name: OK
    kind: button
    default: ""
    effect: Reads the factor, the direction and the reducer and resamples the image, or every frame of the sequence. The window closes once the view shows the result.
    enabled: When **Target Px** differs from the image's size, which it does as the window opens. Its tooltip then reads "Apply integer resampling to the target image.", and otherwise "Target dimensions match the current image. Change the resample factor to enable."
    tradeoff: The history keeps the image as it was beside the result, so the run needs the memory of both. A sequence needs the disk space of the resampled copy.
    scope: The whole image, on every channel, without the mask. A change made in the window after you press it does not reach the run.
    persistence: "The image and its history keep the result, as the step **Integer Resample** with the text `Binning factor: <f>X - New size: <w>x<h> - Binning method: <mode>` for **Downsample** or `Resample factor: <f>X - New size: <w>x<h>` for **Upsample**."
    note: "For a mosaic the view does not debayer, it first asks \"Integer Resample CFA\" whether to run anyway, and **No** starts nothing. OK, **Close** and **Reset** are disabled while the run works, and the other controls stay editable. A single image shows no progress window, and its run cannot be stopped once it starts. A run that fails leaves the window open."
  - name: Close
    kind: button
    default: ""
    effect: Closes the window without resampling.
    enabled: Disabled while a run works.
    note: Closing the window another way while a run works hides it, and it closes when the run has ended. The run is not stopped, on a sequence either.
  - name: Reset
    kind: button
    default: ""
    effect: Returns **Resample Factor** to 2, the direction to **Downsample** and **Downsample Mode** to **Average**.
    range: It leaves **Target Image** as it is.
    enabled: Disabled while a run works.
```

```controls
window: Sequence progress
controls:
  - name: Progress
    kind: readout
    default: ""
    effect: "Shows the share of the frames done, from 0 to 100 percent, under the title **Integer Resample** and the text `Executing integer resample to sequence <name>`."
    enabled: Shown only while OK works on a sequence. A single image has no progress window.
    tradeoff: The resampled sequence is written to a temporary file, which needs its disk space until the run ends. An **Upsample** multiplies that space by the factor's square.
  - name: Cancel
    kind: button
    default: ""
    effect: Stops the run, before the next frame at the latest. The sequence stays as it was, with no history step, and the temporary file is removed.
    note: Closing the Integer Resample window does not stop the run.
```

## How it works

1. **OK** reads the factor, the direction and the reducer.
2. A mosaic is read as one gray plane, its filters forgotten, only after you answer **Yes** to the question.
3. **Downsample** combines each complete block of every channel with the reducer, and discards the rows and columns at
   the right and bottom edges that fill no block. **Upsample** copies each sample into a block.
4. The result is written in the image's pixel type, an average or a median rounded to the nearest value.
5. The header follows the rule of every geometry change, which [Crop](crop.md#how-it-works) describes with the
   [header cards](../../reference/glossary.md#header-card) it keeps. The plate solution is scaled, the pixel size is
   restated, and the [processing state](../../reference/glossary.md#processing-state) cards go with the other cards.
   The image replaces the target with one history step, or a sequence is written whole into a new
   [temporary sequence](../../reference/glossary.md#temporary-sequence).

### Model

Let \(f\) be the factor, and \(W\) and \(H\) the image's width and height. **Downsample** gives a result of
\(\lfloor W/f \rfloor\) by \(\lfloor H/f \rfloor\) pixels. Its pixel \((x, y)\) combines the block \(B\) of samples
\((i, j)\) with \(fx \le i < f(x+1)\) and \(fy \le j < f(y+1)\) that the image's
[validity rule](../../reference/glossary.md#validity-rule) calls valid. With \(|B|\) their number, **Average** gives

\[ O_c(x, y) = \frac{1}{|B|} \sum_{(i, j) \in B} I_c(i, j) \tag{1} \]

computed in double precision, and rounded to the nearest code, ties to even, for an integer image. **Median** gives
the middle sample, or the mean of the two middle samples of an even count. **Maximum** and **Minimum** give the
largest and the smallest. A 13 by 8 image at a factor of 3 gives 4 by 2, without its last column and its last two
rows.

**Upsample** gives a result of \(fW\) by \(fH\) pixels, every sample repeated:

\[ O_c(fx + i,\ fy + j) = I_c(x, y), \qquad 0 \le i, j < f \tag{2} \]

**Average** keeps a local mean, so the background's level, not the total of the samples. When the noise of the
\(n = f^2\) samples of a block is independent and equal, with deviation \(\sigma\), equation (1) leaves

\[ \sigma_B = \sigma / \sqrt{n} \tag{3} \]

Structure and correlated noise depart from that, and **Median**, **Maximum** and **Minimum** bias the noise each in
their own way. **Upsample** repeats signal and noise alike: it adds no independent sample, no
[SNR](../../reference/glossary.md#snr), and multiplies the total of the samples by \(f^2\).

A plate solution is carried with its degrees per pixel multiplied by \(f\) for **Downsample** and divided by it for
**Upsample**, its distortion rescaled, and its reference pixel moved with the grid, so each new pixel points where its
block pointed. The `XPIXSZ` and `YPIXSZ` cards are multiplied by the exact factor for **Downsample** and divided by it
for **Upsample**, whatever edge a **Downsample** discards: a frame of 2.315 µm pixels states 4.63 binned by 2 and
1.1575 enlarged by 2.

### Boundaries

- The run refuses a **Downsample** factor larger than either side, and an **Upsample** larger than the engine's
  limits or the memory, as "Error running integer resample" and in the log. The image, its history and the window
  stay as they were. When the history cannot keep the image, it says so once in a message titled "Image History".
- A block without a valid sample is left unwritten and marked invalid in the result's mask. Where every block had one,
  the image's rule is kept, and a mask of valid samples is resampled with the samples. Under a rule that calls every
  sample valid, a sample that is not a number, or an infinity, takes part, and can make its block's answer one too.
- There is no partial block and no fill at the edges.
- When another run holds a sequence, Integer Resample shows "Another process is executing for this sequence, please
  cancel that job first before running this process" under its own title, and starts nothing.
- A sequence keeps its container, with the frames stored as [Crop](crop.md#boundaries) describes.

### Worked example

The linear Carina [master](../../reference/glossary.md#master) is 7246 by 5429 pixels, shown in the view's
[Autostretch](../../reference/glossary.md#autostretch) display. The window opens with **Downsample**, a factor of 2
and **Average**. **Target Px** reads 3623 by 2714, since 5429 rows leave one over, and **New Size** reads 112.53 MB.
Each new pixel is the mean of four, so the background keeps its level, near 0.028. By equation (3), its deviation of
about 0.0026 would fall to 0.0013 if the noise of neighboring pixels were independent. Registration correlates it,
so measure the result rather than expect that. After **OK** the history reads
`Binning factor: 2X - New size: 3623x2714 - Binning method: Average`.

## Starting points

| Situation | Start | Then adjust |
| --- | --- | --- |
| Less noise in each pixel | **Downsample**, factor 2, **Average**, on sides divisible by 2 | Look at star profiles and the background's noise before you try a larger factor. |
| Isolated hot or dead pixels | **Downsample**, factor 2, **Median**, on a duplicate | Compare with **Average**: check that small stars and peaks survive. |
| An exact enlargement | **Upsample** with the smallest factor the destination needs | Check that every pixel became one block, and the memory **New Size** shows. |

## When it goes wrong

!!! warning "A Downsample cannot be undone by an Upsample"
    The samples a block merged are gone, and an **Upsample** only repeats the result. Use the image's history to
    return to the full-size image.

"Error running integer resample", with a factor larger than a side
:   **Cause.** **Downsample** needs a whole block, and **Target Px** shows 1 for the side the factor exceeds.
:   **Fix.** Lower **Resample Factor** to no more than the image's shorter side.

The last rows or columns are missing
:   **Cause.** The factor does not divide that side, and the samples that fill no block are discarded.
:   **Fix.** Undo the step, crop the image to a multiple of the factor, and resample again.

The result is darker or brighter than expected
:   **Cause.** **Minimum** or **Maximum** was chosen, or a total was compared with a mean.
:   **Fix.** Choose **Average** or **Median**, and compare local means rather than totals.

The result holds samples that are not a number
:   **Cause.** A block held such a sample, or an infinity, under a rule that calls every sample valid.
:   **Fix.** Repair those samples first, then resample the original again.

"Integer Resample CFA" asks whether to run anyway
:   **Cause.** The image is a mosaic its view does not debayer.
:   **Fix.** Choose **No**, then use CFA Integer Resample, or debayer the mosaic first.

"Error running integer resample" for an Upsample
:   **Cause.** The result would be larger than the engine accepts, or than the memory holds.
:   **Fix.** Choose a smaller factor, or free memory, and choose **OK** again.

The astrometry overlay is gone after the resample
:   **Cause.** The image's header cards stated no plate solution, so there was none to carry.
:   **Fix.** Plate-solve the resampled image.

## Related

- [Crop](crop.md), to make the sides a multiple of the factor without changing a sample, and for the header cards
  every geometry change keeps.
- [Resize](resize.md), for a size of your choice through an interpolation.
- [CFA Integer Resample](../calibration/cfa-integer-resample.md), to bin or enlarge a mosaic and keep its filters.
- [Image Statistics](../../interface/image-statistics.md), to measure the noise and the level of the result.
- [Process Windows](../../interface/process-windows.md), for what every process window shares.
