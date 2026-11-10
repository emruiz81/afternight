---
kind: process
stage: calibrate
menu: Process > Geometry > CFA Integer Resample
summary: >-
  Bins or enlarges a raw Bayer mosaic, or every frame of a sequence, by a whole factor, combining or copying samples
  only among the sites of one filter, so the result is a mosaic of the same pattern.
data_in: cfa
data_out: cfa
destructive: true
preview: none
sequences: true
masks: none
external: none
glance:
  enabled: >-
    An image or a sequence view is active. Target Image lists only the open one-channel mosaics, and OK refuses a
    target that states no two-by-two Bayer pattern.
  inputs: >-
    One undebayered mosaic of an RGGB, BGGR, GRBG or GBRG pattern at any phase, as a still image or a sequence, in any
    pixel type.
  outputs: >-
    The same view, replaced, with one history step: a mosaic of the same pattern and phase, smaller or larger by the
    factor, in the same pixel type. No file, preset or report is written.
  state: >-
    A raw or calibrated mosaic of a one-shot color camera, before the debayer, to bin for less noise in each pixel or
    to enlarge by whole blocks.
derived: false
rewritten: true
---
# CFA Integer Resample

This process changes the size of a raw [CFA](../../reference/glossary.md#cfa) mosaic by a whole factor and keeps its
filters. **Downsample** combines a square block of one filter's sites, the factor wide, into one site, by the average,
the median, the maximum or the minimum. **Upsample** copies each site into such a block. Two filters are never mixed,
and nothing is interpolated.

**OK** replaces the image, or every frame of a [sequence](../../reference/glossary.md#sequence), with a mosaic of the
same [Bayer pattern](../../reference/glossary.md#bayer-pattern) and phase, adds one step to its
[history](../../reference/glossary.md#history) and closes the window. The
[pixel type](../../reference/glossary.md#pixel-type) is kept.

```figure
owed: true
image: ../../assets/images/processes/calibration/cfa-integer-resample.webp
alt: >-
  The CFA Integer Resample window. Under the title, the process name and the line Resample CFA frames by integer
  factors while preserving the CFA pattern. In the Resample group, Resample Factor at 2, Downsample Mode at Average,
  and the Downsample and Upsample buttons with Downsample chosen. In the New Size group, Original Px reads 5544 and
  3684 and Target Px 2772 and 1842, with Current Size at 38.96 MB and New Size at 9.74 MB. Under them Target Image
  names the light, and Reset, OK and Close run along the bottom.
caption: >-
  CFA Integer Resample on a raw NGC 253 light, binning by 2 with Average, as the window opens: Target Px and New Size
  show the result before OK.
pins:
  - {n: 2, x: 28.0, y: 35.5}
  - {n: 3, x: 5.5, y: 28.4}
  - {n: 5, x: 71.5, y: 58.6}
  - {n: 6, x: 58.5, y: 94.8}
```

## Before you start

- **OK** replaces the mosaic. **Downsample** discards detail, and **Upsample** multiplies the memory without adding
  any. The image's history keeps the step before.
- The image is a mosaic that states its pattern. Do not [debayer](../../reference/glossary.md#debayer) it: when a raw
  [FITS](../../reference/glossary.md#fits) file opens with the question "Do you want to automatically debayer it before
  opening?", answer **No**.
- Prefer a factor that divides half the mosaic's width and half its height. Otherwise the last rows and columns of
  each filter's sites are dropped, and the result is smaller than **Target Px** shows.
- The result states no [plate solution](../../reference/glossary.md#plate-solution). Plate-solve it again when you
  need one.
- A [mask](../../reference/glossary.md#mask) on the view is not used. On a sequence, every frame gets the same factor,
  direction and reducer, whatever frames its player has selected.

## Steps

1. **Open the window.** Make the mosaic the [active image](../../reference/glossary.md#active-image) and choose
   **Process > Geometry > CFA Integer Resample**. The window opens ready to bin by 2 with **Average**, so **OK** is
   enabled at once.
2. **Choose the direction.** Choose **Downsample** to combine each block of one filter's sites into one site, or
   **Upsample** to copy each site into a block.
3. **Set the factor.** Type **Resample Factor**. For **Downsample**, twice the factor must not exceed the mosaic's
   width or height.
4. **Choose how a block is combined.** For **Downsample**, choose **Downsample Mode**. **Upsample** disables it.
5. **Check the size.** **Target Px** shows the width and height the window expects, and **New Size** the memory the
   result will hold.
6. **Resample.** Choose **OK**. The window closes, the view shows the mosaic at its new size, and its history lists
   **CFA Integer Resample** with the factor and the size.

## Controls

The window has no preview and no **Channel**. What every process window shares is on
[Process Windows](../../interface/process-windows.md).

```controls
window: CFA Integer Resample
controls:
  - name: Resample Factor
    kind: spinbox
    default: "2"
    widget: {min: 2, max: 100, step: 1}
    effect: Sets the factor. Higher makes the mosaic smaller with **Downsample**, larger with **Upsample**. Every whole factor keeps the pattern's phase, as the tooltip says, an odd one as well as an even one.
    range: For **Downsample**, twice the factor must be no larger than the mosaic's even width and height. The window lets you type more, and the run refuses it.
    tradeoff: A larger **Downsample** factor loses more detail, and can drop more rows and columns at the edges. An **Upsample** factor multiplies the memory and the disk by its square.
    scope: OK, on the mosaic or on every frame of the sequence.
    persistence: Until the window closes. **Reset** and another target return it to 2.
  - name: Downsample Mode
    kind: combo
    default: Average
    options: [Average, Median, Maximum, Minimum]
    effect: Chooses how the sites of one filter in a block become one. **Average** takes their mean, **Median** their middle value, **Maximum** the largest and **Minimum** the smallest.
    enabled: With **Downsample** chosen.
    tradeoff: "**Average** keeps the mean level and lowers random noise. **Median** removes an isolated outlier and can soften small stars. It costs more time and holds eight bytes for each pixel it reads while it works. **Maximum** and **Minimum** pick the extremes, which raise or lower the level and change the noise."
    scope: The sites of each filter on their own.
    persistence: Until the window closes. **Reset** and another target return it to **Average**.
  - name: Downsample / Upsample
    kind: radio
    default: Downsample
    options: [Downsample, Upsample]
    effect: "**Downsample** divides the mosaic by the factor and combines each block of one filter's sites by **Downsample Mode**. **Upsample** multiplies it by the factor and copies each site into a block of its own filter's sites."
    tradeoff: "**Downsample** leaves fewer samples, and what it merges or discards is gone. **Upsample** adds no detail, and takes the factor's square in memory and disk."
    scope: OK, on the mosaic or on every frame of the sequence.
    persistence: Until the window closes. **Reset** and another target return it to **Downsample**.
  - name: Original Px
    kind: readout
    default: The mosaic's width and height
    effect: Shows the width and the height of the mosaic now, in pixels.
    range: From 1 to 100,000. A wider or taller mosaic shows 100,000.
    scope: Read again when the image changes or another target is chosen.
  - name: Target Px
    kind: readout
    default: Half the mosaic's width and height, rounded down
    effect: Shows the width and the height the window expects, which the history records too. It is the mosaic's divided by the factor, rounded down, for **Downsample**, or multiplied by it for **Upsample**.
    range: From 1 to 100,000. The result can be smaller, as the Model says, and a factor the run refuses still shows a size.
    scope: Counted again with the factor, the direction and any change of the mosaic's size.
  - name: Current Size
    kind: readout
    default: The mosaic's size in memory
    effect: Gives the memory the mosaic's samples hold now, for all the frames of a sequence together.
    range: In MB with two decimals, and in GB above 1024 MB.
    tradeoff: It counts the samples only, not the working memory of a run, a container's overhead or the compression of a file.
  - name: New Size
    kind: readout
    default: A quarter of Current Size
    effect: Gives the memory the result's samples will hold, from **Target Px** and the bytes of one sample, for every frame of a sequence.
    range: In MB with two decimals, and in GB above 1024 MB.
    tradeoff: It reads too little when **Target Px** is cut at 100,000. It leaves out the working memory, the image the history keeps and the temporary sequence.
  - name: Target Image
    kind: combo
    default: The active mosaic
    options: [The active mosaic, Every other open one-channel mosaic]
    effect: Chooses the mosaic or mosaic sequence OK resamples. Choosing another one reads its size and returns every control to its default.
    range: It lists the views that show a one-channel mosaic, a still image that states a filter array, or a sequence whose view does not debayer its frames. An X-Trans mosaic is listed, and OK refuses it.
    enabled: Disabled while a run works.
    tradeoff: The mosaic chosen here is the one replaced. A longer sequence takes more time and more temporary disk.
    persistence: Until the window closes. **Reset** leaves it as it is.
    note: Opened on an image that is no mosaic, the window works on that image, which its title names, and OK refuses it even while this box shows a mosaic.
  - name: OK
    kind: button
    default: ""
    effect: Checks that the target is a two-by-two mosaic, reads the factor, the direction and the reducer, and resamples the mosaic or every frame of the sequence. The window closes once the view shows the result.
    enabled: When **Target Px** differs from the mosaic's size, which it does as the window opens. Its tooltip then reads "Apply integer resampling to the target image.", and otherwise "Target dimensions match the current image. Change the resample factor to enable."
    tradeoff: The history keeps the mosaic as it was beside the result, so the run needs the memory of both, and of a cut copy when a side is odd. A still image shows no progress window, and its run cannot be stopped once it starts.
    scope: The whole mosaic, without the mask. A change made in the window after you press it does not reach the run.
    persistence: "The view and its history keep the result, as the step **CFA Integer Resample** with the text `Binning factor: <f>X - New size: <w>x<h> - Binning method: <mode>` for **Downsample** or `Resample factor: <f>X - New size: <w>x<h>` for **Upsample**, with the size **Target Px** shows."
    note: OK, **Close**, **Reset** and **Target Image** are disabled while the run works, and the other controls stay editable. A run that fails leaves the window open.
  - name: Close
    kind: button
    default: ""
    effect: Closes the window without resampling.
    enabled: Disabled while a run works.
    note: Closing the window another way while a run works hides it, and it closes when the run has ended. The run is not stopped, on a sequence either.
  - name: Reset
    kind: button
    default: ""
    effect: Returns **Resample Factor** to 2, the direction to **Downsample** and **Downsample Mode** to **Average**, and reads the mosaic's size again.
    range: It leaves **Target Image** as it is, and undoes no resample already applied, which the image's history does.
    enabled: Disabled while a run works.
```

```controls
window: Sequence progress
controls:
  - name: Progress
    kind: readout
    default: ""
    effect: "Shows the share of the frames done, from 0 to 100 percent, under the title **CFA Integer Resample** and the text `Executing CFA integer resample to sequence <name>`."
    enabled: Shown only while OK works on a sequence. A still image has no progress window.
    tradeoff: The resampled sequence is written to a temporary file, which needs its disk space until the run ends. An **Upsample** multiplies that space by the factor's square.
  - name: Cancel
    kind: button
    default: ""
    effect: Stops the run, before the next frame at the latest. The sequence stays as it was, with no history step, and the temporary file is removed.
    note: Closing the CFA Integer Resample window does not stop the run.
```

## How it works

1. **OK** checks that the target is a one-channel mosaic of a two-by-two pattern, and reads the factor, the direction
   and the reducer.
2. A mosaic of an odd width or height first loses its last column or row. The cut starts at the top-left pixel, so
   the pattern's phase is kept.
3. Each position of the cell is a lattice of one filter's sites. **Downsample** combines each complete block of a
   lattice with the reducer, and drops the sites of an incomplete block at the right and bottom. **Upsample** copies
   each site into a block of its lattice. The result is a mosaic of the same pattern and phase, written in the
   mosaic's pixel type, an average or a median rounded to the nearest value.
4. The header follows the rule of every geometry change, which [Crop](../geometry/crop.md#model) describes with the
   [header cards](../../reference/glossary.md#header-card) it keeps. `XPIXSZ` and `YPIXSZ` are restated, and the
   [processing state](../../reference/glossary.md#processing-state) cards go with the other cards. The plate solution
   is dropped: the four filters' sites of a block lie at different places, which one solution cannot describe. The
   [lineage](../../reference/glossary.md#lineage) records the change under the process's name.
5. The mosaic replaces the target with one history step. A sequence's frames are read as recorded, and resampled into
   a new [temporary sequence](../../reference/glossary.md#temporary-sequence), which replaces the view's sequence once
   every frame is written.

### Model

Let \(f\) be the factor, \(W\) and \(H\) the mosaic's width and height, and \(w = \lfloor W/2 \rfloor\) and
\(h = \lfloor H/2 \rfloor\) the width and height of each lattice. **Downsample** gives a mosaic of

\[ W_d = 2\lfloor w/f \rfloor, \qquad H_d = 2\lfloor h/f \rfloor \tag{1} \]

pixels, and **Upsample** one of \(2wf\) by \(2hf\). **Target Px** shows \(\lfloor W/f \rfloor\) by
\(\lfloor H/f \rfloor\), or \(Wf\) by \(Hf\). The two agree for **Downsample** when \(W\) and \(H\) are multiples of
\(2f\), and for **Upsample** when they are even. The light below, 5544 by 3684, at a factor of 4 shows 1386 by 921
and gives 1386 by 920.

In one lattice \(L\), let \(B\) be the sites \((i, j)\) with \(fx \le i < f(x+1)\) and \(fy \le j < f(y+1)\) that
the image's [validity rule](../../reference/glossary.md#validity-rule) calls valid, and \(|B|\) their number.
**Average** gives

\[ L'(x, y) = \frac{1}{|B|} \sum_{(i, j) \in B} L(i, j) \tag{2} \]

in double precision, rounded to the nearest code, ties to even, for an integer mosaic. **Median** gives the middle
site, or the mean of the two middle ones of an even count, and **Maximum** and **Minimum** the largest and the
smallest. **Average** keeps the lattice's mean level, not the total of its sites. **Upsample** sets
\(L'(fx + i,\ fy + j) = L(x, y)\) for \(0 \le i, j < f\): each cell becomes a block of \(2f\) by \(2f\) pixels whose
four lattices repeat its four sites, which is not a nearest-neighbour enlargement of the whole mosaic.

`XPIXSZ` and `YPIXSZ` are multiplied by the exact factor for **Downsample** and divided by it for **Upsample**,
whatever edges are dropped, so the plate scale the optics imply is still the result's.

### Boundaries

- **OK** refuses an image that states no filter array, one of more than one channel and an X-Trans mosaic, each by
  name. The run refuses a **Downsample** whose double factor exceeds a side, and an **Upsample** beyond the engine's
  limits or the memory. The image, its history and the window stay as they were.
- A refusal or a failure is reported in a message box titled "Error running integer resample", which starts "An
  error occurred when executing the integer resample process:", and in the log, not in the window's message strip.
  When the history cannot keep the mosaic, it says so once in a message titled "Image History".
- A block without a valid site is left unwritten and marked invalid in the result's mask of valid samples. Where every
  block had one, the image's rule is kept, and such a mask is resampled with the samples. Under a rule that calls every
  sample valid, a sample that is not a number takes part, and can make its block's answer one too.
- When another run holds a sequence, a box titled **CFA Integer Resample** says "Another process is executing for
  this sequence, please cancel that job first before running this process", and nothing starts.
- A sequence keeps its container, with the frames stored as [Crop](../geometry/crop.md#boundaries) describes.

### Worked example

The data is a raw light of NGC 253 from a QHY183C: a FITS file of 5544 by 3684 16-bit
[sensor codes](../../reference/glossary.md#sensor-code) whose header states `BAYERPAT = 'RGGB'`, opened with **No**
to the debayer question. The window opens with **Downsample**, a factor of 2 and **Average**. **Target Px** reads 2772
by 1842, which equation (1) gives too, since both sides are multiples of 4. **Current Size** reads 38.96 MB and
**New Size** 9.74 MB.

After **OK** the mosaic is 2772 by 1842 pixels, still RGGB. By equation (2), its first red site is the mean of the
light's red sites at (0, 0), (2, 0), (0, 2) and (2, 2): \((1920 + 1856 + 2064 + 2080)/4 = 1980\) codes. Measured
between neighbouring red sites, the noise falls from about 168 codes in the light to about 96, close to the half that
independent noise would give. `XPIXSZ` reads 4.8, and the history reads
`Binning factor: 2X - New size: 2772x1842 - Binning method: Average`.

## Starting points

| Situation | Start | Then adjust |
| --- | --- | --- |
| Less noise in each pixel of a mosaic | **Downsample**, factor 2, **Average**, on sides that are multiples of 4 | Look at star profiles and the background's noise before you try a larger factor. |
| Isolated hot or dead pixels | **Downsample**, factor 2, **Median**, on a duplicate | Compare with **Average**: check that small stars survive. |
| An exact enlargement of a mosaic | **Upsample** with the smallest factor you need | Debayer a copy at its own size first to compare, and check **New Size**. |

## When it goes wrong

!!! warning "A Downsample cannot be undone by an Upsample"
    The sites a block merged are gone, and an **Upsample** only repeats the result. Use the image's history to return
    to the full-size mosaic.

!!! warning "The result has no plate solution"
    The filters' sites of a block lie at different places, so no solution is carried. Plate-solve the result, or its
    debayered image, with [Plate Solving](../registration-alignment/plate-solving.md).

**Target Image** does not list the mosaic
:   **Cause.** The view shows no one-channel mosaic: the file was debayered as it opened, or a sequence's view
    debayers its frames.
:   **Fix.** Open the raw file or the sequence again, and answer **No** when AfterNight asks whether to debayer it.

"the image states no colour filter array"
:   **Cause.** The target states no Bayer pattern, or the window opened on an image that is no mosaic.
:   **Fix.** Open the mosaic from a file that states its pattern, make its view active, and open the window again.

"a CFA reduction factor leaves at least one complete block in every phase"
:   **Cause.** Twice the **Downsample** factor exceeds the mosaic's even width or height.
:   **Fix.** Lower **Resample Factor** to no more than half the mosaic's shorter side.

The result is smaller than **Target Px** showed
:   **Cause.** A side is not a multiple of twice the factor, and the sites of incomplete blocks are dropped.
:   **Fix.** Undo the step, crop the mosaic at an even corner to multiples of twice the factor, and resample again.

**Target Px** or **New Size** stops growing
:   **Cause.** The readouts stop at 100,000 pixels a side, and **New Size** follows them.
:   **Fix.** Work the size out with equation (1), and lower the factor when it is more than you can hold.

An "Image History" error, and the mosaic is not resampled
:   **Cause.** The history could not keep the mosaic as it was before the change.
:   **Fix.** Close images you do not need, or raise the memory limit on the **Processing** page of the settings, and
    choose **OK** again.

The result looks blocky after an Upsample
:   **Cause.** Each site is repeated in its block, on purpose: nothing is interpolated.
:   **Fix.** Debayer the mosaic at its own size, and enlarge the color image with Resize when you need a smooth one.

"Another process is executing for this sequence, please cancel that job first before running this process"
:   **Cause.** Another run holds the sequence, and nothing started.
:   **Fix.** Wait for that run to end, or cancel it, then choose **OK** again.

## Related

- [Image Calibration](image-calibration.md), to calibrate the mosaic before you change its size.
- [Debayer](debayer.md), to make a color image of the result.
- [Crop](../geometry/crop.md), to cut the sides to multiples of twice the factor, and for the header cards every
  geometry change keeps.
- [Integer Resample](../geometry/integer-resample.md), to bin or enlarge an image that is no longer a mosaic.
- [Split CFA](split-cfa.md) and [Merge CFA](merge-cfa.md), for the mosaic's positions as images of their own.
- [Process Windows](../../interface/process-windows.md), for what every process window shares.
