---
kind: process
stage: any
menu: Process > Geometry > Rotate
summary: >-
  Turns an image, or every frame of a sequence, about its center by any angle up to half a turn, on its own canvas or
  a larger one, and can then crop the turned result.
data_in: either
data_out: either
destructive: true
preview: static
sequences: true
masks: none
external: none
glance:
  enabled: >-
    An image or a sequence view is active and no other process window is open on it. Rotate needs the image to
    itself, so no other window opens on it while Rotate is open.
  inputs: >-
    One image or sequence of any pixel type and any number of channels, with square pixels. A raw color-filter mosaic
    only after you answer Yes to the Rotate CFA question.
  outputs: >-
    The same view, replaced, with one history step: the turned image, on its own or a larger canvas, cropped if you
    ask. No file, preset or report is written.
  state: >-
    Any image, preferably debayered, whose framing needs a turn other than a quarter or a half turn.
derived: false
rewritten: true
---
# Rotate

Rotate turns an image about its center by an angle you type, clockwise for a positive angle. It can grow the canvas
so that no corner is cut, and crop the turned image to remove the black corners the turn leaves. While you set the
angle, the image's view shows its picture turned.

**OK** replaces the image, or every frame of a [sequence](../../reference/glossary.md#sequence), with the turned one,
adds one step to its [history](../../reference/glossary.md#history) and closes the window. The
[pixel type](../../reference/glossary.md#pixel-type) and the channels are kept. Every sample is interpolated, and the
[plate solution](../../reference/glossary.md#plate-solution) is dropped.

```figure
image: ../../assets/images/processes/geometry/rotate.webp
alt: >-
  The Rotate window. In the Rotation group, the Rotation Angle box at 10.00 with Resize image ticked beside it, a
  slider under them, and Interpolation at Auto (Lanczos4). In the Crop group, ticked, Pos X at 1040, Width at 6000,
  Pos Y at 1102 and Height at 4400. Under them Current Size reads 450.19 MB and New Size 302.12 MB, Target Image
  names the master, and Reset, OK and Close run along the bottom.
caption: >-
  Rotate on the linear Carina master, turned 10 degrees on a larger canvas and cropped to a rectangle of 6000 by 4400
  pixels that the turned image fills.
pins:
  - {n: 2, x: 8.0, y: 29.0}
  - {n: 3, x: 61.0, y: 29.0}
  - {n: 4, x: 8.0, y: 42.0}
  - {n: 5, x: 28.5, y: 51.0}
  - {n: 6, x: 43.0, y: 94.5}
```

## Before you start

- For a quarter or a half turn, choose **Rotate 90º Left**, **Rotate 90º Right** or **Rotate 180º** under
  **Image > Fast Geometry**. They move every sample without interpolating it and keep a mosaic's filters under their
  samples, while Rotate interpolates at every angle.
- **OK** replaces the image, and turning back does not restore it. Its history keeps the step before.
- Rotate drops the plate solution, even at 0 degrees with only a crop. Do a crop, a resize or an integer resample
  before or after it as the framing needs, since those keep the solution, and plate-solve once at the end.
- [Debayer](../../reference/glossary.md#debayer) a raw [CFA](../../reference/glossary.md#cfa) mosaic first. Rotate
  turns it as one gray plane, and the result states no filters.
- Rotate needs square pixels and refuses an image that states another shape.
- Every channel is turned, and a [mask](../../reference/glossary.md#mask) on the view is not used. On a sequence, every
  frame gets the same turn and crop, whatever frames its player has selected.

## Steps

1. **Open the window.** Make the image the [active image](../../reference/glossary.md#active-image) and choose
   **Process > Geometry > Rotate**. **OK** stays disabled while the angle is 0 and no crop is set.
2. **Set the angle.** Type **Rotation Angle**, positive to turn clockwise, or drag the slider for a first look. The
   view turns its picture as the value changes.
3. **Keep the corners.** Tick **Resize image**, so that the canvas grows to hold the whole turned image. Leave it off
   only when the corners may be cut.
4. **Choose the interpolation.** Leave **Interpolation** at **Auto (Lanczos4)**, or choose **Bilinear** where rings
   around stars matter more than sharpness.
5. **Remove the black corners.** Tick **Crop** and type **Pos X**, **Pos Y**, **Width** and **Height** in the pixels
   of the turned canvas, or drag the frame on the view. **New Size** gives the memory the result will hold.
6. **Rotate.** Choose **OK**. The window closes, the view shows the turned image, and its history lists **Rotate**
   with the angle and the interpolation.

## Controls

What every process window shares is on [Process Windows](../../interface/process-windows.md).

```controls
window: Rotate
controls:
  - name: Rotation Angle
    kind: spinbox
    default: "0.00"
    widget: {min: -180, max: 180, step: 1, decimals: 2}
    effect: Sets the turn in degrees. A positive angle turns the image clockwise and a negative one counterclockwise. The view shows its picture turned as the value changes.
    range: The slider beside it moves in steps of 0.36 degrees. Type an exact angle in the box.
    tradeoff: Any angle interpolates every sample and leaves corners the image does not cover. Each turn adds its loss to the last one.
    scope: OK, on every channel and every frame of a sequence.
    persistence: Until the window closes. **Reset** returns it to 0.00.
  - name: Resize image
    kind: check
    default: "Off"
    effect: On, grows the canvas to the smallest whole-pixel size that holds every turned pixel, equally on each side. Off, keeps the image's width and height and cuts the corners that turn out of them.
    tradeoff: On keeps the whole image, and adds memory, time and black corners, most near a diagonal angle. It grows the canvas but never scales the image.
    scope: OK, and the size of the turned picture on the view, the limits of the crop and **New Size**.
    persistence: Until the window closes. **Reset** turns it off.
  - name: Interpolation
    kind: combo
    default: Auto (Lanczos4)
    options: [Auto (Lanczos4), Nearest Neighbor, Bilinear, Bicubic, Pixel Area Relation, Lanczos4, Bit Exact Bilinear, Bit Exact Nearest Neighbor]
    effect: Chooses how each sample of the turned image is computed from the samples around its place. Auto is always **Lanczos4**.
    range: "**Nearest Neighbor** and **Bit Exact Nearest Neighbor** take the nearest sample. **Bilinear**, **Pixel Area Relation** and **Bit Exact Bilinear** weigh the four around, **Bicubic** sixteen and **Lanczos4** sixty-four."
    tradeoff: "**Nearest Neighbor** is blocky. **Bilinear** smooths. **Bicubic** and **Lanczos4** keep edges sharper, read more samples and take longer, and can ring around stars. Every choice but the nearest ones correlates the noise of neighboring pixels."
    scope: OK, on each channel.
    persistence: Until the window closes. **Reset** returns it to Auto.
    note: The history names the interpolation that ran, never Auto.
  - name: Crop
    kind: check
    default: "Off"
    effect: On, crops the turned image to the rectangle of the four boxes below, drawn as a frame on the view. Off, keeps the whole turned canvas and disables the boxes.
    range: The run keeps the part of the rectangle the turned canvas holds.
    tradeoff: The rectangle can remove the black corners, or the science at the edges.
    scope: OK, after the turn, every sample copied exactly.
    persistence: Until the window closes. **Reset** leaves it as it is. Another target turns it off.
  - name: Pos X
    kind: spinbox
    default: "0"
    widget: {min: 0, step: 1}
    effect: Places the rectangle's left edge, in pixels of the turned canvas.
    range: Up to the canvas's width. **Width** cannot reach past the canvas from it.
    enabled: With **Crop** on.
    persistence: Until the window closes. **Reset** returns it to 0.
  - name: Width
    kind: spinbox
    default: The image's width
    widget: {min: 0, step: 1}
    effect: Sets the result's width, from the left edge **Pos X** places.
    range: Up to the canvas's width less **Pos X**. The window lets it reach 0, which the run refuses.
    enabled: With **Crop** on. Ticking **Crop** keeps the image's own width here, even on a larger canvas.
    persistence: Until the window closes. **Reset** returns it to the image's width.
  - name: Pos Y
    kind: spinbox
    default: "0"
    widget: {min: 0, step: 1}
    effect: Places the rectangle's top edge, in pixels of the turned canvas.
    range: Up to the canvas's height. **Height** cannot reach past the canvas from it.
    enabled: With **Crop** on.
    persistence: Until the window closes. **Reset** returns it to 0.
  - name: Height
    kind: spinbox
    default: The image's height
    widget: {min: 0, step: 1}
    effect: Sets the result's height, from the top edge **Pos Y** places.
    range: Up to the canvas's height less **Pos Y**. The window lets it reach 0, which the run refuses.
    enabled: With **Crop** on. Ticking **Crop** keeps the image's own height here, even on a larger canvas.
    persistence: Until the window closes. **Reset** returns it to the image's height.
  - name: Current Size
    kind: readout
    default: The image's size in memory
    effect: Gives the memory the image's samples hold now, for all the frames of a sequence together.
    range: In MB with two decimals, and in GB above 1024 MB.
    tradeoff: It counts the samples only, not the working memory of a run or the compression of a file.
  - name: New Size
    kind: readout
    default: The image's size in memory
    effect: Gives the memory the result's samples will hold - the crop's rectangle with **Crop** on, the larger canvas with **Resize image** on, the image's own size otherwise - for every frame of a sequence.
    range: The canvas it counts is the one the run makes.
    tradeoff: It leaves out the working planes, the image the history keeps, the temporary sequence a sequence needs, the header and the compression of a file.
  - name: Target Image
    kind: combo
    default: The active image
    options: [The active image, Every other open image or sequence the window can work on]
    effect: Chooses the image or sequence OK turns. Choosing another one returns every control to its default, turns **Crop** off, and moves the turned picture to that image's view.
    tradeoff: The image chosen here is the one replaced.
    persistence: Until the window closes. **Reset** leaves it as it is.
  - name: OK
    kind: button
    default: ""
    effect: Reads the window, turns the image or every frame of the sequence, then crops it when **Crop** is on. The window closes once the view shows the result.
    enabled: When the angle is not 0, or when **Crop** is on and its rectangle is not the whole image. Its tooltip then reads "Apply the rotation to the target image.", and otherwise "Set a non-zero rotation angle or adjust the crop region to enable."
    tradeoff: The history keeps the image as it was beside the result, so the run needs the memory of both, and of the crop's copy. A sequence needs the disk space of the turned copy.
    scope: The whole image, on every channel, without the mask. A change made in the window after you press it does not reach the run.
    persistence: "The image and its history keep the result, as the step **Rotate** with the text `Rotated <angle> degrees - Interpolation: <name>`, or `Rotated <angle> degrees - Cropped area : X: <x> Y: <y> W:<w> H:<h> - Interpolation: <name>` with **Crop** on."
    note: "For a mosaic the view does not debayer, it first asks \"Rotate CFA\" whether to run anyway, and **No** starts nothing. OK, **Close** and **Reset** are disabled while the run works, and the other controls stay editable. A single image shows no progress window, and its run cannot be stopped once it starts. A run that fails leaves the window open."
  - name: Close
    kind: button
    default: ""
    effect: Closes the window without turning, and the view shows its picture unturned again.
    enabled: Disabled while a run works.
    note: Closing the window another way while a run works hides it, and it closes when the run has ended. The run is not stopped, on a sequence either.
  - name: Reset
    kind: button
    default: ""
    effect: Returns the angle to 0.00, the four boxes to the whole image, **Interpolation** to Auto and **Resize image** to off.
    range: It leaves **Crop** and **Target Image** as they are.
    enabled: Disabled while a run works.
```

```controls
window: The view while Rotate is open
controls:
  - name: Turned picture
    kind: readout
    default: Unturned
    effect: Shows the picture the view displays, turned by the angle about its center, on the larger canvas with **Resize image** on and cut to the image's own size with it off.
    tradeoff: It turns what the view shows, with its display stretch and its mask overlay, and does not compute the interpolation. The corners it leaves empty show the view's background, where the result's are black.
    scope: The view only, until **OK**.
  - name: Crop frame
    kind: readout
    default: Hidden
    effect: Shows the rectangle of the four boxes over the turned canvas while **Crop** is on, and follows them.
  - name: Drag inside the frame
    kind: action
    default: ""
    effect: Moves the frame without changing its size. **Pos X** and **Pos Y** follow.
    enabled: While **Crop** is on.
  - name: Drag an edge or a corner
    kind: action
    default: ""
    effect: Moves that edge, or the two edges of that corner. The pointer changes shape within three screen pixels of an edge.
    enabled: While **Crop** is on.
```

```controls
window: Sequence progress
controls:
  - name: Progress
    kind: readout
    default: ""
    effect: "Shows the share of the frames done, from 0 to 100 percent, under the title **Rotate** and the text `Executing rotate to sequence <name>`."
    enabled: Shown only while OK works on a sequence. A single image has no progress window.
    tradeoff: The turned sequence is written to a temporary file, which needs its disk space until the run ends.
  - name: Cancel
    kind: button
    default: ""
    effect: Stops the run, before the next frame at the latest. The sequence stays as it was, with no history step, and the temporary file is removed.
    note: Closing the Rotate window does not stop the run.
```

## How it works

1. **OK** reads the angle, the canvas, the interpolation, with Auto as **Lanczos4**, and the crop.
2. The image is turned clockwise by the angle about its center, the middle of its pixel centers, on its own canvas or
   the larger one. A mosaic is read as one gray plane only after you answer **Yes** to the question.
3. Each sample of the turned image is computed from the samples around its place in the image, in floating point. A
   place outside the image reads black, which is 0.
4. The result is written back in the image's pixel type, rounded to the nearest value and held to the type's range.
   A floating-point image of
   [fractions of full scale](../../reference/glossary.md#fraction-of-full-scale) is held to 0 to 1. One of
   [sensor codes](../../reference/glossary.md#sensor-code) or of a
   [physical quantity](../../reference/glossary.md#physical-quantity) keeps what was computed. With **Crop** on, the
   part of the rectangle the canvas holds is then copied exactly.
5. The header follows the rule of every geometry change, which [Crop](crop.md#how-it-works) describes with the
   [header cards](../../reference/glossary.md#header-card) it keeps, and the plate solution and the card that names
   its solver are dropped. The [processing state](../../reference/glossary.md#processing-state) cards go with the
   other cards. The image replaces the target with one history step, or a sequence is written whole into a new
   [temporary sequence](../../reference/glossary.md#temporary-sequence).

### Model

Let \(W\) and \(H\) be the image's width and height, \(\theta\) the angle, and \(W'\) and \(H'\) the canvas. With
**Resize image** off the canvas is \(W\) by \(H\). With it on, it is

\[ W' = \lceil\, |W \cos\theta| + |H \sin\theta| \,\rceil, \qquad H' = \lceil\, |W \sin\theta| + |H \cos\theta| \,\rceil \tag{1} \]

The image's center is \(c_x = (W - 1)/2\), \(c_y = (H - 1)/2\), and the canvas's center \(c'_x\), \(c'_y\) likewise,
in the image's coordinates, with \(x\) to the right and \(y\) down. The pixel \((x, y)\) of the canvas reads the
image at

\[ u = c_x + (x - c'_x)\cos\theta + (y - c'_y)\sin\theta, \qquad v = c_y - (x - c'_x)\sin\theta + (y - c'_y)\cos\theta \tag{2} \]

through the interpolation, and reads 0 where that place lies outside the image. **Bicubic** uses the cubic kernel with
\(a = -0.75\), and **Lanczos4** a Lanczos window of radius 4.

A turn keeps no sum. The interpolation, the black corners and the range a result is held to change the total of the
samples, and a crop can remove signal. The noise of neighboring pixels becomes correlated, and no resolution,
[flux](../../reference/glossary.md#flux) or [SNR](../../reference/glossary.md#snr) is gained.

### Boundaries

- The corners are always black: there is no other fill and no extension of the edges.
- A sample the image's [validity rule](../../reference/glossary.md#validity-rule) calls invalid leaves unwritten every
  turned sample whose kernel reaches it, and the result marks those invalid in a mask. The black corners count as
  written, so where every sample was written the image's rule is kept, and a mask of valid samples is turned with the
  samples. Under a rule that calls every sample valid, a sample that is not a number takes part, and spreads.
- An image that states pixels of another shape, a crop of 0 pixels, a crop wholly outside the canvas, or a result
  there is no memory for is reported as "Error running rotate" and in the log. The image, its history and the window
  stay as they were. When the history cannot keep the image, it says so once in a message titled "Image History".
- When another run holds a sequence, Rotate shows "Another process is executing for this sequence, please cancel that
  job first before running this process" under its own title, and starts nothing.
- A sequence keeps its container, with the frames stored as [Crop](crop.md#boundaries) describes.

### Worked example

The linear Carina [master](../../reference/glossary.md#master) is 7246 by 5429 pixels, shown in the view's
[Autostretch](../../reference/glossary.md#autostretch) display. Set **Rotation Angle** to 10 and tick
**Resize image**. By equation (1) the canvas is 8079 by 6605 pixels, with black corners. Tick **Crop** and type
**Pos X** 1040, **Pos Y** 1102, **Width** 6000 and **Height** 4400. That rectangle is centered on the canvas and lies
inside the turned image, more than 25 pixels from its edges, so the result has no black corner. **New Size** reads
302.12 MB. After **OK** the history reads
`Rotated 10 degrees - Cropped area : X: 1040 Y: 1102 W:6000 H:4400 - Interpolation: Lanczos4`.

```figure
images:
  - image: ../../assets/images/processes/geometry/rotate-before.webp
    alt: >-
      The master as the view shows it while the window is open, turned 10 degrees clockwise on a larger canvas with
      black corners, and the crop rectangle drawn over it in amber dashes, divided in thirds.
    label: Before
  - image: ../../assets/images/processes/geometry/rotate-after.webp
    alt: The turned and cropped image, the nebula tilted 10 degrees clockwise and filling the frame without black corners.
    label: After
caption: >-
  The Carina master while Rotate is open, the view showing the turn and the crop to come, and after OK: turned 10
  degrees on a larger canvas and cropped to 6000 by 4400 pixels. Both are the whole frame in the view's Autostretch
  display.
```

## Starting points

| Situation | Start | Then adjust |
| --- | --- | --- |
| A small tilt of the framing | **Resize image** on, the angle, and Auto or **Bilinear** | Look at the corners on the view, then tick **Crop** and set a rectangle inside the turned image. |
| A quarter or a half turn | **Image > Fast Geometry** | Use Rotate only to add another angle or a crop. |
| Black corners to remove | **Resize image** on and the final angle first | **Crop** last, with a rectangle that stays inside the turned image on every side. |

## When it goes wrong

!!! warning "The plate solution is gone after a turn"
    Coordinates, the astrometry overlay and the photometric color calibrations need a solution, and Rotate drops it.
    Plate-solve the turned image before you use them.

**OK** stays disabled
:   **Cause.** The angle is 0, and **Crop** is off or its rectangle is the whole image.
:   **Fix.** Set an angle other than 0, or a rectangle smaller than the image.

The corners of the image are cut
:   **Cause.** **Resize image** was off, so the canvas kept the image's size.
:   **Fix.** Undo the step, tick **Resize image**, and choose **OK** again.

Black triangles at the corners
:   **Cause.** The turned image does not cover the whole canvas, and places outside it read black.
:   **Fix.** Undo the step and add a rectangle with **Crop** that stays inside the turned image, or keep the corners.

"Error running rotate", and the reason speaks of square source pixels
:   **Cause.** The image states pixels that are not square.
:   **Fix.** Turn an image of square pixels. A resize states the shape of its source's pixels.

"Error running rotate" with **Crop** on
:   **Cause.** **Width** or **Height** is 0, or the rectangle lies outside the turned canvas.
:   **Fix.** Set a rectangle of at least one pixel inside the canvas.

"Rotate CFA" asks whether to run anyway
:   **Cause.** The image is a mosaic its view does not debayer.
:   **Fix.** Choose **No** and debayer it, or use a quarter or half turn of **Image > Fast Geometry**.

The result holds samples that are not a number
:   **Cause.** Such samples in the image took part in the interpolation under a rule that calls every sample valid.
:   **Fix.** Repair the samples that are not a number first, then turn the original again.

The turned image on the view differs from the result
:   **Cause.** The view turns the picture it shows, with its
    [display stretch](../../reference/glossary.md#display-stretch) and its mask overlay, and the run interpolates the
    image itself.
:   **Fix.** Judge the framing on the view, and the samples on the result at 100 %.

## Related

- [Crop](crop.md), to cut without interpolating, and for the header cards every geometry change keeps.
- [Resize](resize.md), to change the size of the pixels' grid.
- [Non-Interactive Processes](../utility/non-interactive-processes.md), for the exact quarter and half turns of
  **Image > Fast Geometry**.
- [Image Registration](../registration-alignment/image-registration.md), to align many frames, each by its own
  transform.
- [Plate Solving](../registration-alignment/plate-solving.md), to solve the turned image.
- [Process Windows](../../interface/process-windows.md), for what every process window shares.
