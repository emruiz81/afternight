---
kind: process
stage: noise
menu: Process > Filters > Banding Reduction Filter
summary: >-
  Evens out horizontal or vertical banding: measures the median of every row or column and adds to it the one constant
  that brings it to the level of the brightest.
data_in: linear
data_out: linear
destructive: true
preview: real-time
sequences: true
masks: post-blend
external: none
glance:
  enabled: >-
    An image or a sequence view is active, no Banding Reduction window is open on it yet, and no window that needs the
    image for itself is open on it.
  inputs: >-
    One grayscale or RGB image or sequence of any pixel type, and an optional mask of the image's size on its view.
  outputs: >-
    The same view, replaced, with one history step. Its size, channels, pixel type, header and recorded processing
    state are kept, and a floating-point result is not held between 0 and 1. No file is written.
  state: >-
    A calibrated linear frame or master, before the stretch, whose bands run the whole length of its rows or of its
    columns.
derived: false
---
# Banding Reduction

Banding Reduction evens out bands that run along the rows or down the columns of an image. It measures the median of
every row, or of every column with **Vertical Banding**, and adds to every sample of that line the one constant that
brings its median up to the brightest line's. Each channel is measured and corrected on its own. **Protect From
Highlights** leaves bright samples out of the measurement, so that a nebula or a galaxy does not count as a band.

**Apply** replaces the image or [sequence](../../reference/glossary.md#sequence) with the corrected one, keeps its
size and [pixel type](../../reference/glossary.md#pixel-type), and adds one step to its
[history](../../reference/glossary.md#history).

```figure
image: ../../assets/images/processes/noise-reduction/banding-reduction.webp
alt: >-
  The Banding Reduction window. On the left, the Amount box with its slider, the ticked Protect From Highlights box,
  the Sigma box with its slider, the Vertical Banding box, Target Image, Channel at RGB and, at the bottom, the Preview
  check box. On the right, a toolbar of zoom buttons, a channel box and a display mode box reading Autostretch over
  the preview of a star field crossed by thin horizontal stripes, with the word PREVIEW in its corner and a large
  framed area in its middle, where the stripes are gone. Reset, Close and Apply run along the bottom.
caption: >-
  Banding Reduction on a corner of the linear master with bands added, Protect From Highlights ticked and Sigma at
  1.00. The framed area shows what Apply writes there, in the Autostretch display mode.
pins:
  - {n: 1, x: 46.5, y: 54.6}
  - {n: 2, x: 16.0, y: 28.4}
  - {n: 3, x: 20.0, y: 19.9}
  - {n: 4, x: 34.2, y: 24.2}
  - {n: 5, x: 34.2, y: 15.7}
  - {n: 6, x: 35.2, y: 36.9}
  - {n: 8, x: 88.8, y: 96.0}
```

## Before you start

- Use it for bands at one level along the whole of each row or column. Diagonal or curved bands, a level that changes
  along a line and a pattern that moves from frame to frame need another tool. A real change of level from line to
  line, such as a gradient, vignetting or the glow of a nebula, is flattened with the bands.
- **Apply** replaces the image. Duplicate it first, or use its history, which keeps the step before.
- [Debayer](../../reference/glossary.md#debayer) a [CFA](../../reference/glossary.md#cfa) mosaic first. A raw mosaic is
  corrected as one plane, whose rows alternate colors of different levels, and that difference is taken for bands.
  The frames of a sequence are read with the automatic debayer setting of its view.
- A sample that is not a finite number, such as in the empty border of a registered frame, is left out of every
  median and left where it is.
- A [mask](../../reference/glossary.md#mask) on the view blends the result with the original afterwards, and is the
  only way to keep chosen pixels from the correction. On a sequence, every frame gets the same values, **Channel**
  and mask.

## Steps

1. **Open the window.** Make the image with the bands the
   [active image](../../reference/glossary.md#active-image) and choose **Process > Filters > Banding Reduction
   Filter**. The preview shows the corrected image inside a framed
   [preview area](../../reference/glossary.md#preview-area) in the middle of the image.
2. **Choose the direction.** Leave **Vertical Banding** unticked for bands that run across the image, along its rows.
   Tick it for bands that run down its columns.
3. **Protect the measurement.** Tick **Protect From Highlights** whenever a nebula, a galaxy or bright stars cross
   the lines. **Sigma** becomes available.
4. **Set Sigma.** Raise **Sigma** when the lines through bright objects still come out darker than the rest. Lower it
   when a band through a bright area is left as it was.
5. **Set the amount.** Leave **Amount** at 1 to bring every line to the brightest. Lower it to correct part of the way.
6. **Choose the channel.** Leave **Channel** at **RGB** to correct each channel by its own lines. Choose
   **CIE L* (Lightness)** when the bands have no color.
7. **Look at the whole frame.** Untick **Preview area**, zoom out and hold ++space++ to compare with the original.
   Check the edges, the bright objects and any broad change of level.
8. **Apply.** Choose **Apply**. A progress window follows the run, the view shows the corrected image, and its history
   lists **Banding Reduction** with the values.

## Controls

**Target Image**, the preview pane, **Close** and the progress window work as in every process window: see
[Process Windows](../../interface/process-windows.md). What is particular to this window is below.

```controls
window: Banding Reduction
controls:
  - name: Amount
    kind: spinbox
    default: "1.00"
    widget: {min: 0, max: 4, step: 0.05, decimals: 2}
    effect: Sets how much of each line's correction is added. At 1 every line reaches the level of the brightest line. Below 1 the lines move part of the way. Above 1 they overshoot, and the bands come back the other way round.
    range: The slider moves it in 1,000 equal steps of 0.004, and the box keeps two decimals.
    enabled: Disabled while Apply runs. **Apply** stays enabled at 0.
    tradeoff: At 0 nothing is added, but every line is still measured. In an integer image a high value can push bright samples to the top of the type, where they are cut.
    scope: The preview and Apply, on each channel **Channel** selects, before the mask.
    persistence: Until the window closes. **Reset** and a finished Apply return it to its default.
  - name: Protect From Highlights
    kind: check
    default: "Off"
    effect: Leaves the samples at or above a point that **Sigma** sets out of each line's median, so that a nebula, a galaxy or bright stars do not raise the level the line is measured at.
    range: The point lies above the channel's median by 1.4826 times its median absolute deviation, divided by **Sigma**, as equation (1) gives.
    enabled: Disabled while Apply runs. Ticking it enables **Sigma**.
    tradeoff: It changes only the measurement. Every sample of a line, a bright one included, gets the line's correction, whatever its tooltip says about keeping the filter from bright areas. A line whose every sample lies above the point is left as it was.
    scope: The preview and Apply, on each channel **Channel** selects.
    persistence: Until the window closes. **Reset** and a finished Apply untick it. The history shows it only through the `Inv Sigma` value it adds.
  - name: Sigma
    kind: spinbox
    default: "1.00"
    widget: {min: 0, max: 5, step: 0.05, decimals: 2}
    effect: Sets the point above which samples are left out, as a divisor. Higher brings the point down toward the median and leaves more samples out. Lower raises it.
    range: The slider moves it in 1,000 equal steps of 0.005. At 0 no sample is left out, as with **Protect From Highlights** unticked.
    enabled: While **Protect From Highlights** is ticked, and not while Apply runs.
    tradeoff: Its tooltip calls it a standard deviation for the detection, but it divides, so a higher value leaves out more samples, not fewer. Set high, the lines are measured on the darker half of the noise alone, and more lines through bright areas lose every sample and are left uncorrected.
    scope: The preview and Apply, on each channel **Channel** selects.
    persistence: Until the window closes. **Reset** and a finished Apply return it to its default and disable it. While **Protect From Highlights** is ticked, the history records it as `Inv Sigma`, also at 0.
  - name: Vertical Banding
    kind: check
    default: "Off"
    effect: Off, every row is measured and corrected, for bands that run across the image. On, every column, for bands that run down it. The image is not turned.
    enabled: Disabled while Apply runs.
    tradeoff: A real change of level from line to line, such as a gradient, vignetting or a nebula, is flattened with the bands.
    scope: The preview and Apply, on each channel **Channel** selects.
    persistence: Until the window closes. **Reset** and a finished Apply untick it.
  - name: Target Image
    kind: combo
    default: The active image
    options: [The active image, Every other open image or sequence the window can work on]
    effect: Chooses the image or sequence Apply replaces, with the mask on its view. The choices of **Channel** and the preview follow it.
    persistence: Until the window closes. **Reset** leaves it as it is.
  - name: Channel
    kind: combo
    default: RGB
    options: [RGB, Red, Green, Blue, CIE L* (Lightness), CIE a*, CIE b*, CIE c*, CIE h*, CIE X, CIE Y (Luminance), CIE Z, HSL Hue, HSL Saturation, HSL Lightness, HSV Hue, HSV Saturation, HSV Value, YCbCr Luma, YCbCr Blue Chroma, YCbCr Red Chroma]
    effect: Chooses what is corrected. **RGB** measures and corrects each of the three channels on its own, against its own median and its own brightest line. **Red**, **Green** or **Blue** corrects that channel and keeps the other two. Every other choice corrects one component of another color model and converts the image back.
    enabled: For an RGB image. A grayscale image shows a disabled **Grayscale**. Disabled while Apply runs.
    tradeoff: With **RGB**, a channel banded more than another is lifted more, which can shift the color balance. A single component corrects that one plane, at the cost of two conversions and a working copy.
    scope: The preview and Apply, before the mask. Process Windows describes each component.
    persistence: Until the window closes. **Reset** leaves it as it is. A finished Apply and a new target return it to **RGB**.
  - name: Preview
    kind: check
    default: "On"
    effect: Shows the corrected image beside the controls, with the mask blended in.
    range: >-
      The pane opens with a **Preview area** of 700 by 700 pixels in the middle of the image, labelled "Preview:
      Banding Reduction", or the whole image when it is smaller. At the window's default size, **Preview area** and
      **Take snapshot** are in the menu at the end of the pane's toolbar.
    tradeoff: The area is measured as an image of its own, so its median, its spread and its lines' medians are the area's, and the preview can differ from Apply. Every change measures it again. **Apply**, or closing the window, stops a preview on its way at the next group of lines.
    scope: The window only. On a sequence it shows the frame the view displays. **Display Mode** and **Autostretch Settings** change only how the preview is shown.
    persistence: Until the window closes. **Reset** leaves it as it is.
  - name: Reset
    kind: button
    default: ""
    effect: Returns **Amount** and **Sigma** to their defaults, unticks both boxes and disables **Sigma**, and one preview of them follows. It leaves **Channel** as it is.
    enabled: Disabled while Apply runs.
  - name: Apply
    kind: button
    default: ""
    effect: Corrects the whole image, or every frame of the sequence, with the values and **Channel** as they are when you press it, and then returns the four values to their defaults.
    enabled: From the moment the window opens, at any values.
    tradeoff: A single image shows a progress window too. Its bar stays at the start, because the filter reports no share done within a frame, and its **Cancel** stops the run at the next group of lines. The lines are measured in groups, in parallel on the threads the run is granted, with a working plane of one channel, one buffer for each line of a group and one value for each line, beside the second copy every run needs.
    persistence: "The history step is **Banding Reduction** and reads, for example, `Amount 1, Inv Sigma 1, Vertical Banding: false in channel: RGB`. Without **Protect From Highlights** the `Inv Sigma` part is missing, and for a grayscale image it ends after the `Vertical Banding` value. The mask is not recorded. The header cards, the plate solution and the recorded processing state are kept."
```

## How it works

1. Each channel that **Channel** selects is measured and corrected on its own, in its own units, in single
   precision. Its background is the median of its valid samples.
2. With **Protect From Highlights**, the samples at or above the point of equation (1) are left out of the lines'
   measurement.
3. Each row, or each column with **Vertical Banding**, is measured by the median of its remaining samples. Its
   correction is the distance from its median up to the median of the brightest line of the channel, so no
   correction is below zero.
4. **Amount** times its correction is added to every sample of the line. The result is written back in the image's
   pixel type, and a mask on the view then blends it with the original.
5. The image replaces the target and its history gets one step. Its header, its
   [plate solution](../../reference/glossary.md#plate-solution) and its recorded
   [processing state](../../reference/glossary.md#processing-state) are kept.

### Model

With \(b\) the median of a channel, \(d\) its
[median absolute deviation](../../reference/glossary.md#median-absolute-deviation) and \(s\) the value of **Sigma**,
the samples at or above

\[ t = b + \frac{1.4826\,d}{s} \tag{1} \]

are left out of the measurement while **Protect From Highlights** is ticked. With \(m_j\) the median of what remains
of line \(j\) and \(a\) the value of **Amount**, every sample of that line gets

\[ c_j = a\,\bigl(\max_k m_k - m_j\bigr) \tag{2} \]

At \(a = 1\) every line's median ends at the brightest line's, not at \(b\), so the background rises a little. No line
is ever lowered.

### Boundaries

- A line with no sample left to measure takes no part in the maximum of equation (2) and is left as it was.
- A sample that is not a finite number, and a pixel the image marks as invalid, are left out of every median and left
  where they are. A channel without a valid sample is refused.
- A floating-point result is not held between 0 and 1. An integer one is rounded and held to its type's range. A
  64-bit floating-point image is worked in single precision.
- One constant goes to each line. A level that changes along a line is not corrected, and a smooth change across the
  lines is flattened with the bands.
- **Cancel**, or closing the window, stops the run at its next group of lines. A stopped run writes nothing.

### Worked example

The linear [master](../../reference/glossary.md#master) has no bands of its own: the medians of neighbouring rows
differ by about 0.00003, no more than the noise of a median. The pair is therefore made on a crop of 1,100 by 820
pixels from the top right corner of the master, a star field with little nebula, starting at x 5800 and y 200.
[Pixel Math](../pixel-math/pixel-math.md) then adds bands to the crop: `$T + 0.001*sin(y()*y())` moves every row by
its own amount between -0.001 and 0.001, `y()` counting the crop's own rows from 0. The window has **Protect From
Highlights** ticked and **Sigma** at 1.00.

The banded crop's green channel has its median at 0.0283 and \(1.4826\,d\) at 0.0017, so by equation (1) the point is
0.0299, and at a **Sigma** of 2.00 it would be 0.0291. The stars and the upper part of the noise are left out of the
rows' medians. By equation (2) the green corrections run from 0 to 0.0020, the spread of the added bands, and every
row's median ends at the brightest row's. Without the bands they would run to 0.0005, the slow change of level the
crop has from row to row. A row 0.002 below the brightest gets 0.001 at an **Amount** of 0.50, and 0.004 at 2.00,
which leaves it 0.002 above.

The middle of the master is no place for this. The nebula fills its rows, their medians differ by several
thousandths even without the bright samples, and the process would draw stripes of its own. The plain crop and the
result are both stretched by [Histogram Transformation](../stretching/histogram-transformation.md) with **Shadows**
and **Midtones** at 0.02.

```figure
images:
  - image: ../../assets/images/processes/noise-reduction/banding-reduction-before.webp
    alt: >-
      A crop at 100 % of a star field from the corner of the master, stretched: many small stars on a dark background
      crossed by thin horizontal stripes of slightly different brightness.
    label: Before
  - image: ../../assets/images/processes/noise-reduction/banding-reduction-after.webp
    alt: >-
      The same crop after Banding Reduction, the stripes gone, the stars and the background as they were.
    label: After
caption: >-
  A star field from the top right corner of the linear Carina Nebula master at 100 %, with bands added by Pixel Math,
  without and with Banding Reduction at Protect From Highlights ticked and Sigma 1.00, both then stretched by Histogram
  Transformation with Shadows and Midtones at 0.02.
```

## Starting points

| Situation | Start | Then adjust |
| --- | --- | --- |
| Bands along the rows of a frame or master | **Protect From Highlights** ticked, **Sigma** at 1.00, **Amount** at 1.00 | **Sigma** up if lines through bright objects come out darker, down if a band through them stays. |
| Bands down the columns | **Vertical Banding** ticked, with the same values | The same. |
| Bands without color | **CIE L* (Lightness)** in **Channel** | Compare with **RGB** on a duplicate. |
| Quick tries on a large image | The preview area | **Preview area** unticked for the last check. |

## When it goes wrong

!!! warning "Protect From Highlights does not protect the highlights"
    It only leaves bright samples out of the measurement. Every sample of a line, a star's included, gets the line's
    correction. Protect bright areas from the correction with a mask.

!!! warning "A real change of level is flattened too"
    Every line is brought to the brightest line's level, so a gradient, vignetting or a nebula that changes the level
    from line to line is evened out with the bands. Remove the gradient first, or lower **Amount**.

Bands remain
:   **Cause.** **Vertical Banding** is set for the other direction, or the level of a band changes along its length.
:   **Fix.** Tick or untick **Vertical Banding** to match the bands. A band that changes along its line needs another
    tool.

A dark band runs through a bright object, and the rest of the frame brightens
:   **Cause.** The lines through the object are measured high, so every other line is raised to their level.
:   **Fix.** Tick **Protect From Highlights**, and raise **Sigma** until those lines match the rest.

A band through a bright area is left as it was
:   **Cause.** Every sample of that line lies above the point **Sigma** sets, so the line is not measured.
:   **Fix.** Lower **Sigma**, or untick **Protect From Highlights** for that frame.

Raising Sigma leaves out more samples, not fewer
:   **Cause.** **Sigma** divides the distance of the point from the median.
:   **Fix.** Lower **Sigma** to leave out fewer samples, and keep it above 0.

Colors shift after the correction
:   **Cause.** Each channel is lifted by its own bands, and by different amounts.
:   **Fix.** Undo the step, and correct **CIE L* (Lightness)** or one channel at a time.

The preview area differs from the result of Apply
:   **Cause.** The area is measured alone, with its own median and its own shorter lines.
:   **Fix.** Untick **Preview area** for the last check before **Apply**.

Samples that are not numbers are still there after Apply
:   **Cause.** They are left out of every median and left where they are.
:   **Fix.** Replace or crop away those samples first when the result must hold numbers only.

## Related

- [Cosmetic Correction](../calibration/cosmetic-correction.md), for [hot pixels](../../reference/glossary.md#hot-pixel),
  [cold pixels](../../reference/glossary.md#cold-pixel) and other defects of single pixels rather than of whole lines.
- [Background Extraction](../background-extraction/background-extraction.md) and
  [Auto Gradient Removal](../background-extraction/auto-gradient-removal.md), which model a gradient in two
  dimensions instead of evening out lines.
- [Pixel Math](../pixel-math/pixel-math.md), which added the bands of the worked example.
- [Masks](../../interface/masks.md), to keep bright areas from the correction.
- [Image History](../../interface/image-history.md), to undo the step.
- [Process Windows](../../interface/process-windows.md), for the preview, the preview area and what every process
  window shares.

## Credits

- The [GNU Scientific Library statistics reference](https://www.gnu.org/software/gsl/doc/html/statistics.html), for
  the median and the median absolute deviation the measurement uses. AfterNight's engine computes them exactly
  itself.
