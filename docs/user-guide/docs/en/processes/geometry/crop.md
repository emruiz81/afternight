---
kind: process
stage: any
menu: Process > Geometry > Crop
summary: >-
  Cuts an image, or every frame of a sequence, to a rectangle you type or draw on its view, to remove stacking borders
  or frame a region without resampling a single pixel.
data_in: either
data_out: either
destructive: true
preview: none
sequences: true
masks: none
external: none
glance:
  enabled: >-
    An image or a sequence view is active and no other process window is open on it. Crop needs the image to itself,
    so no other window opens on it while Crop is open.
  inputs: One image or sequence of any pixel type and any number of channels, a raw color-filter mosaic included.
  outputs: >-
    The same view, replaced, with one history step: the rectangle, every sample in it unchanged. No file, preset or
    report is written.
  state: >-
    Any image, linear or not: a stacked master whose edges registration left ragged, or a finished picture to frame
    for export.
derived: false
rewritten: true
---
# Crop

Crop cuts an image to a rectangle. You type the rectangle's top-left corner and its size, or draw it on the image's
view, where a frame shows the part that stays. Every sample inside the rectangle is kept exactly as it was: nothing is
interpolated.

**OK** replaces the image, or every frame of a [sequence](../../reference/glossary.md#sequence), with the rectangle,
adds one step to its [history](../../reference/glossary.md#history) and closes the window. The
[pixel type](../../reference/glossary.md#pixel-type) and the channels are kept, and a
[plate solution](../../reference/glossary.md#plate-solution) moves with the pixels, so the cut image needs no new
plate solve.

```figure
image: ../../assets/images/processes/geometry/crop.webp
alt: >-
  The Crop window. On the left, the Pos X, Pos Y, Width and Height boxes at 1623, 1214, 4000 and 3000. On the right,
  the Current Size field at 450.19 MB, the New Size field at 137.33 MB and the New Selection button. Under them the
  Target Image box names the master, and Reset, OK and Close run along the bottom.
caption: >-
  Crop on the linear Carina master with a rectangle of 4000 by 3000 pixels at its middle: New Size shows what the
  result will hold.
pins:
  - {n: 2, x: 17.5, y: 34.0}
  - {n: 3, x: 17.8, y: 53.5}
  - {n: 4, x: 61.0, y: 54.0}
  - {n: 5, x: 60.5, y: 43.5}
  - {n: 6, x: 37.0, y: 91.5}
```

## Before you start

- **OK** replaces the image rather than making a new one. Its history keeps the step before: duplicate an image you
  could not load again.
- Crop needs the image to itself. Close the other process windows open on it first, and expect no other window to
  open on it while Crop is open.
- Every channel is cut, and a [mask](../../reference/glossary.md#mask) on the view is not used.
- On a sequence, every frame is cut to the same rectangle, whatever frames its player has selected. Crop finds no
  border and aligns nothing: register the frames first.
- A [CFA](../../reference/glossary.md#cfa) mosaic is cut as the mosaic it is, without a question, and keeps every
  filter under its sample.
- When a sequence's view [debayers](../../reference/glossary.md#debayer) its frames, they are debayered before they
  are cut, and the new sequence holds color frames.

## Steps

1. **Open the window.** Make the image the [active image](../../reference/glossary.md#active-image) and choose
   **Process > Geometry > Crop**. A frame around the whole image appears on its view, and **OK** stays disabled while
   the rectangle is the whole image.
2. **Place the corner.** Type **Pos X** and **Pos Y**, the pixels to cut from the left and from the top. The frame on
   the view moves with them.
3. **Set the size.** Type **Width** and **Height**. Each one stops at what is left of the image beyond the corner.
4. **Or draw the rectangle.** Choose **New Selection** and drag a rectangle on the view. The four boxes follow it, and
   once you release the button you can still move the frame or drag its edges.
5. **Check the result's size.** **New Size** gives the memory the result will hold, beside **Current Size**.
6. **Crop.** Choose **OK**. The window closes, the view shows the cut image, and its history lists **Crop** with the
   rectangle.

## Controls

The rectangle is counted in the image's own pixels from its top-left pixel, which is at 0, 0. What every process
window shares is on [Process Windows](../../interface/process-windows.md).

```controls
window: Crop
controls:
  - name: Pos X
    kind: spinbox
    default: "0"
    widget: {min: 0, step: 1}
    effect: Places the rectangle's left edge, in pixels from the image's left edge. Higher cuts more from the left.
    range: Up to the image's width. **Width** cannot reach past the image's right edge from it.
    tradeoff: The columns left of it are gone from the result, and no later step can use them.
    scope: OK, on every channel and every frame of a sequence.
    persistence: Until the window closes. **Reset** and another target return it to 0.
  - name: Pos Y
    kind: spinbox
    default: "0"
    widget: {min: 0, step: 1}
    effect: Places the rectangle's top edge, in pixels from the image's top edge. Higher cuts more from the top.
    range: Up to the image's height. **Height** cannot reach past the image's bottom edge from it.
    tradeoff: The rows above it are gone from the result, and no later step can use them.
    scope: OK, on every channel and every frame of a sequence.
    persistence: Until the window closes. **Reset** and another target return it to 0.
  - name: Width
    kind: spinbox
    default: The image's width
    widget: {min: 0, step: 1}
    effect: Sets the result's width, from the left edge **Pos X** places.
    range: Up to the image's width less **Pos X**. OK is disabled at 0.
    tradeoff: The result's memory, and the time of every later step, grow with its area.
    scope: OK, on every channel and every frame of a sequence.
    persistence: Until the window closes. **Reset** and another target return it to the image's width.
  - name: Height
    kind: spinbox
    default: The image's height
    widget: {min: 0, step: 1}
    effect: Sets the result's height, from the top edge **Pos Y** places.
    range: Up to the image's height less **Pos Y**. OK is disabled at 0.
    tradeoff: The result's memory, and the time of every later step, grow with its area.
    scope: OK, on every channel and every frame of a sequence.
    persistence: Until the window closes. **Reset** and another target return it to the image's height.
  - name: Current Size
    kind: readout
    default: The image's size in memory
    effect: Gives the memory the image's samples hold now, for all the frames of a sequence together.
    range: In MB with two decimals, and in GB above 1024 MB.
    tradeoff: It counts the samples only, not the working memory of a run or the compression of a file.
    scope: Counted again when the image changes or another target is chosen.
  - name: New Size
    kind: readout
    default: The image's size in memory
    effect: Gives the memory the result's samples will hold, which is **Width** times **Height** times the channels times the bytes of one sample, for every frame of a sequence.
    range: In MB with two decimals, and in GB above 1024 MB.
    tradeoff: It leaves out the image the history keeps beside the result, the temporary sequence a sequence needs, the header and the compression of a file.
    scope: Counted again with every change of the rectangle.
  - name: New Selection
    kind: button
    default: "Off"
    effect: Lets you draw the rectangle on the view. Choose it, then drag on the image. The four boxes read 0 until you press the mouse button, and follow the rectangle as you drag.
    range: Choosing it again before you drag cancels the drawing and returns the rectangle to the whole image.
    scope: The frame on the view and the four boxes. Nothing is cut until OK. On a sequence you draw on the frame the view shows.
    persistence: It turns itself off when the drag ends. **Reset** cancels a drawing.
  - name: Target Image
    kind: combo
    default: The active image
    options: [The active image, Every other open image or sequence the window can work on]
    effect: Chooses the image or sequence OK cuts. Choosing another one moves the frame to it and returns the rectangle to the whole of that image.
    tradeoff: The image chosen here is the one replaced.
    persistence: Until the window closes. **Reset** leaves it as it is.
  - name: OK
    kind: button
    default: ""
    effect: Reads the rectangle and cuts the image, or every frame of the sequence. The window closes once the view shows the result.
    enabled: When **Width** and **Height** are above 0 and the rectangle is not the whole image. Its tooltip says why it is disabled - "Crop width and height must be greater than zero." or "The crop region matches the full image. Adjust the region to enable."
    tradeoff: The history keeps the image as it was beside the result, so the run needs the memory of both. A sequence needs the disk space of the cut copy.
    scope: The whole image, on every channel, without the mask. A change made to a box after you press it does not reach the run.
    persistence: "The image and its history keep the result, as the step **Crop** with the text `Cropped area : X: <x> Y: <y> W:<width> H:<height>`."
    note: OK, **Close** and **Reset** are disabled while the run works, and the four boxes stay editable. A single image shows no progress window, and its run cannot be stopped once it starts. A run that fails leaves the window open.
  - name: Close
    kind: button
    default: ""
    effect: Closes the window without cutting, and the frame leaves the view.
    enabled: Disabled while a run works.
    note: Closing the window another way while a run works hides it, and it closes when the run has ended. The run is not stopped, on a sequence either.
  - name: Reset
    kind: button
    default: ""
    effect: Cancels a drawing begun with **New Selection** and returns the rectangle to the whole image.
    range: It leaves **Target Image** as it is.
    enabled: Disabled while a run works.
```

```controls
window: The frame on the view
controls:
  - name: Crop frame
    kind: readout
    default: Around the whole image
    effect: Shows the rectangle on the image's view, as an amber dashed frame, and follows the four boxes.
    scope: The view only. It changes no sample.
  - name: Drag inside the frame
    kind: action
    default: ""
    effect: Moves the frame without changing its size. **Pos X** and **Pos Y** follow.
    range: The frame stays inside the image.
  - name: Drag an edge or a corner
    kind: action
    default: ""
    effect: Moves that edge, or the two edges of that corner. The pointer changes shape within three screen pixels of an edge.
  - name: Click outside the frame
    kind: action
    default: ""
    effect: Centers the frame on the point you click, as far as the image allows.
  - name: Drag with New Selection on
    kind: action
    default: ""
    effect: Draws a new rectangle from the point where you press the button to the point where you release it.
    note: While the Crop window is open, holding the left button does not open the view's lens.
```

```controls
window: Sequence progress
controls:
  - name: Progress
    kind: readout
    default: ""
    effect: "Shows the share of the frames done, from 0 to 100 percent, under the title **Crop** and the text `Executing crop to sequence <name>`."
    enabled: Shown only while OK works on a sequence. A single image has no progress window.
    tradeoff: The cut sequence is written to a temporary file, which needs its disk space until the run ends.
  - name: Cancel
    kind: button
    default: ""
    effect: Stops the run, before the next frame at the latest. The sequence stays as it was, with no history step, and the temporary file is removed.
    note: Closing the Crop window does not stop the run.
```

## How it works

1. **OK** reads the four boxes, and the run works on that rectangle away from the window.
2. The run refuses a rectangle that holds no pixel, starts outside the image or reaches past its edge.
3. Every sample of every channel inside the rectangle is copied, unchanged, into a new image of the same pixel type.
   A mosaic states the [Bayer pattern](../../reference/glossary.md#bayer-pattern) that starts at its new corner.
4. The header follows the rule of every geometry change, which Resize, Rotate, Integer Resample, CFA Integer
   Resample and **Image > Fast Geometry** share. The acquisition record and the image's meaning stay. Of the
   [header cards](../../reference/glossary.md#header-card), the plate solution and the frame's observing context
   stay, as the model lists. The [processing state](../../reference/glossary.md#processing-state) cards go with the
   rest: a record that the image is linear, or that a [stretch](../../reference/glossary.md#stretch) made it
   nonlinear, is lost, unless the image's meaning states it, as a camera file's does. A decision on how to write the
   image is withdrawn, and the [lineage](../../reference/glossary.md#lineage) records the change under the process's
   name.
5. The cut image replaces the target with one history step. On a sequence, every frame is cut and written into a new
   [temporary sequence](../../reference/glossary.md#temporary-sequence), which is published whole and shown in the
   view on the frame it showed.

### Model

Let \(X\) and \(Y\) be **Pos X** and **Pos Y**, and \(W\) and \(H\) be **Width** and **Height**. A sample \(I_c\) of
channel \(c\) of the image becomes the sample \(O_c\) of the result, for \(0 \le x < W\) and \(0 \le y < H\):

\[ O_c(x, y) = I_c(x + X,\ y + Y) \tag{1} \]

The result holds the columns \(X\) to \(X + W - 1\) and the rows \(Y\) to \(Y + H - 1\). There is no kernel, no
average, no fill and no clipping, and a sample that is not a number, or an infinity, is copied as it is. Crop changes
no sample's signal or noise. An object's total [flux](../../reference/glossary.md#flux) is unchanged only when its
whole aperture and the background around it stay inside the rectangle.

On a mosaic, the filter at \((x, y)\) of the result is the filter at \((x + X, y + Y)\) of the image. An odd
**Pos X** turns RGGB into GRBG, an odd **Pos Y** into GBRG, and both into BGGR. The six-by-six table of an X-Trans
sensor is carried the same way, over its six-pixel repeat.

A plate solution the header cards state is carried with its reference pixel moved by \((-X, -Y)\), so every pixel
points where it pointed. Its scale, its orientation and its distortion are kept, with the `PLTSOLVR` card that names
the solver. The other cards that stay are those of the frame's observing context:

- the telescope and optics: `TELESCOP`, `LENSMODE`, `FOCALLEN`, `FOCRATIO`, `APTDIA`, `APTAREA`, `XPIXSZ`, `YPIXSZ`,
  `XBINNING`, `YBINNING` and `EGAIN`;
- the site: `SITELAT`, `SITELONG` and `SITEELEV`, and their other spellings;
- where the mount pointed: `RA`, `DEC`, `OBJCTRA`, `OBJCTDEC`, `OBJCTROT`, `OBJRA`, `OBJDEC`, `TELRA`, `TELDEC`,
  `CENTALT`, `CENTAZ`, `AIRMASS` and `PIERSIDE`;
- the camera and its settings: `CAMERAID`, `CAMMAKE`, `INSTRUME`, `SET-TEMP`, `CCD-TEMP`, `GAIN` or `ISOSPEED`,
  `OFFSET` and `USBLIMIT`;
- the filter wheel and the focuser: `FWHEEL`, `FOCNAME`, `FOCPOS`, `FOCUSPOS`, `FOCUSSZ`, `FOCTEMP` and `FOCUSTEM`;
- `FILTER`, `OBJECT`, `IMAGETYP`, `SWCREATE`, `DATE-OBS`, `DATE-LOC`, and `EXPOSURE` when the frame states no
  `EXPTIME`;
- the light the frame holds, as it states it: `EXPTIME`, `TOTEXP`, `LIVETIME`, `STACKCNT` and `NCOMBINE`, a stacked
  [master](../../reference/glossary.md#master)'s whole integration and frame count.

For a camera file, what its [EXIF](../../reference/glossary.md#exif) states - exposure, ISO speed, camera, lens,
software, sensor temperature, focal length, aperture, pixel size, dates and GPS position - is written as those cards.
Every other card read from the file is dropped. The acquisition record is written into the file when the image is
saved.

### Boundaries

- A rectangle the run refuses, or a result it has no memory for, is reported as "Error running crop" and in the log.
  The image, its history and the window stay as they were.
- Every sample of the result comes from a sample of the image, so the image's
  [validity rule](../../reference/glossary.md#validity-rule) is kept, and a mask of valid samples is cut with them.
- A sequence keeps its container, and its frames are stored as the container stores frames:
  [SER](../../reference/glossary.md#ser) at 8 or 16 unsigned bits, AVI and the video containers at 8 bits, and a video
  at an even width and height, so the odd last column or row of a video frame is dropped. A video in a container
  AfterNight does not write becomes Matroska.
- When the history cannot keep the image as it was, it says so once in a message titled "Image History", and the
  image is not cut.

### Worked example

The linear Carina master is 7246 by 5429 pixels, shown in the view's
[Autostretch](../../reference/glossary.md#autostretch) display. With **Pos X** at 1623,
**Pos Y** at 1214, **Width** at 4000 and **Height** at 3000, the frame sits at the middle of the field, and
**New Size** reads 137.33 MB against the 450.19 MB of **Current Size**. By equation (1), pixel (2000, 1500) of the
result is pixel (3623, 2714) of the master, with the same value. After **OK** the image is 4000 by 3000 pixels and its
history reads `Cropped area : X: 1623 Y: 1214 W:4000 H:3000`.

```figure
images:
  - image: ../../assets/images/processes/geometry/crop-before.webp
    alt: The whole master as the view shows it, with Crop's amber dashed frame, divided in thirds, around the middle of the field.
    label: Before
  - image: ../../assets/images/processes/geometry/crop-after.webp
    alt: The cut image, the part of the nebula the frame held, filling the view.
    label: After
caption: >-
  The Carina master before and after OK with a rectangle of 4000 by 3000 pixels at its middle. Both are the whole
  frame in the view's Autostretch display.
```

## Starting points

| Situation | Start | Then adjust |
| --- | --- | --- |
| A stack whose edges registration left ragged | **Pos X** and **Pos Y** at the first column and row that every frame covers | **Width** and **Height** to end before the opposite edges. Check all four edges at 100 %. |
| A raw mosaic | Even **Pos X** and **Pos Y**, which keep the pattern's name | Even **Width** and **Height**, so no incomplete two-by-two cell is left at the right or bottom edge. |
| A plate-solved image | The smallest cut that removes what you must | Keep stars enough for a new plate solve later, and check the carried solution on the overlay. |

## When it goes wrong

!!! warning "What you cut is gone for this image"
    A later step may need what the rectangle leaves out: background for gradient removal, stars for a plate solve,
    an object's whole aperture for photometry. Crop last what you can, and use the history to return to the uncut
    image.

**OK** stays disabled
:   **Cause.** **Width** or **Height** is 0, or the rectangle is the whole image.
:   **Fix.** Move at least one edge inside the image. The button's tooltip says which case it is.

**Width** or **Height** will not go higher
:   **Cause.** Each one stops at what is left of the image beyond **Pos X** or **Pos Y**.
:   **Fix.** Lower **Pos X** or **Pos Y** first, or choose a smaller rectangle.

The cut mosaic states another Bayer pattern
:   **Cause.** **Pos X** or **Pos Y** was odd, and the pattern is stated from the new corner.
:   **Fix.** Nothing is wrong: the filters are still under their samples. Use even values to keep the pattern's name.

"Error running crop"
:   **Cause.** The run refused the rectangle, or found no memory for the result.
:   **Fix.** Read the reason after "An error occurred when executing the crop process:", press **Reset**, set a
    rectangle inside the image, free memory, and choose **OK** again.

An "Image History" error, and the image is not cut
:   **Cause.** The history could not keep the image as it was before the cut.
:   **Fix.** Close images you do not need, or raise the memory limit on the **Processing** page of the settings, and
    choose **OK** again.

The astrometry overlay is gone after the crop
:   **Cause.** The image's header cards stated no plate solution, so there was none to carry.
:   **Fix.** Plate-solve the cut image.

"Another process is executing for this sequence, please cancel that job first before running this process"
:   **Cause.** Another run holds the sequence, and Crop started nothing.
:   **Fix.** Wait for that run to end, or cancel it, then choose **OK** again.

## Related

- [Image Stacking](../stacking-integration/image-stacking.md), to stack registered frames that one rectangle cuts to
  their common area, or to cut the master it makes before slow filters.
- [Resize](resize.md), [Rotate](rotate.md) and [Integer Resample](integer-resample.md), the other geometry changes,
  which share Crop's rules for the header.
- [Plate Solving](../registration-alignment/plate-solving.md), to solve again after a change that drops the solution.
- [Image Viewer](../../interface/image-viewer.md), for the look of the frame and the view's own gestures.
- [Image History](../../interface/image-history.md), to undo the crop.
- [Process Windows](../../interface/process-windows.md), for what every process window shares.
