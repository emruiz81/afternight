---
kind: process
stage: finish
menu: Process > Adjustments > Gamma Correction
summary: >-
  Raises the image, or one channel or color component of it, to a power that brightens or darkens the midtones and
  leaves black and white where they are.
data_in: nonlinear
data_out: nonlinear
destructive: true
preview: real-time
sequences: true
masks: post-blend
external: none
glance:
  enabled: >-
    An image or a sequence view is active, no Gamma Correction window is open on it yet, and no window that needs
    the image for itself is open on it.
  inputs: >-
    One grayscale or RGB image or sequence with samples between 0 and 1, and an optional mask of the image's size on
    its view.
  outputs: >-
    The same view, replaced, with one history step. Its size, its channels and its pixel type are kept. No file,
    preset or report is written.
  state: >-
    A stretched image, mono or RGB, whose midtones need a last adjustment. A linear image is accepted too, and its
    result is recorded as nonlinear all the same.
derived: false
---
# Gamma Correction

This window applies a gamma correction: it raises every sample to a power. **Gamma** above 1 brightens the
[midtones](../../reference/glossary.md#midtones) and below 1 darkens them, while black and white stay where they are.
**Channel** applies the power to the whole image, to one channel, or to one component of a
[color model](../../reference/glossary.md#color-model).

**Apply** replaces the image or [sequence](../../reference/glossary.md#sequence) with the adjusted one, keeps its size
and [pixel type](../../reference/glossary.md#pixel-type), and adds one step to its
[history](../../reference/glossary.md#history). The result is recorded as nonlinear, whatever the image was before.

```figure
image: ../../assets/images/processes/stretching/gamma-correction.webp
alt: >-
  The Gamma Correction window. On the left, a histogram with one peak close to its left edge and the lines Clipping
  Low and Clipping High in its corner. Under it a toolbar, the Gamma box at 0.50 with its slider, Target Image,
  Channel at RGB and Preview. On the right, the preview shows a red nebula on a nearly black background. Reset,
  Close and Apply run along the bottom.
caption: >-
  Gamma Correction on a stretched master with Gamma at 0.50. The histogram is counted from the darkened preview, and
  the preview shows what Apply writes.
pins:
  - {n: 2, x: 8.5, y: 84.3}
  - {n: 3, x: 2.0, y: 54.0}
  - {n: 4, x: 2.0, y: 19.0}
  - {n: 5, x: 73.0, y: 26.0}
  - {n: 6, x: 90.9, y: 96.0}
```

## Before you start

- [Stretch](../../reference/glossary.md#stretch) the image first, and finish background correction and color
  calibration before that. The power recovers no clipped value and calibrates no channel.
- **Apply** replaces the image or sequence. Duplicate it first, or use its history, which keeps the step before.
- Use samples between 0 and 1. The power clamps nothing that lies outside that range.
- The power is applied to the values as they are stored. It decodes and encodes no sRGB or monitor curve, reads no
  color profile, and is no change of exposure.
- A [mask](../../reference/glossary.md#mask) on the view blends the result with the original afterwards: white takes the
  result, black keeps the original, gray mixes them, and **Invert Mask** swaps the two. The mask must have the
  image's size.
- On a sequence, every frame gets the same **Gamma**, the same **Channel** and the mask. For a mosaic sequence,
  decide first whether its automatic [debayer](../../reference/glossary.md#debayer) turns the frames into RGB.

## Steps

1. **Open the window.** Make the stretched image the active one and choose **Process > Adjustments > Gamma
   Correction**. **Target Image** names it, the preview shows it unchanged, and **Apply** stays disabled until
   **Gamma** leaves 1.
2. **Choose what to adjust.** Leave **Channel** at **RGB** to give the three channels the same curve. Choose
   [**HSL Lightness**](../../reference/glossary.md#hsl), **CIE Y (Luminance)** or
   [**HSV Value**](../../reference/glossary.md#hsv) to change the tone through one component instead.
3. **Set the gamma.** Lower **Gamma** below 1 to darken the midtones, or raise it above 1 to brighten them. The
   preview follows, and the peak of the histogram moves left or right.
4. **Read the histogram.** Check where the peak of the background has landed. With **RGB**, **Clipping Low** and
   **Clipping High** read about what they read before: the curve keeps black and white where they are.
5. **Check the preview at 100 %.** Zoom the preview into the background, the smooth gradients and the bright colored
   stars, and press ++space++ over it to compare with the original. Look for bands between tones, for colors that
   changed and, with a mask, for its edges.
6. **Apply.** Choose **Apply**. The image in its view is the adjusted one, its history lists **Gamma Correction**
   with the value and the channel, and **Gamma** and **Channel** return to their defaults.

## Controls

The toolbar under the histogram - zoom, grid, clipping lines, scales, areas, the histogram itself and **Plot
resolution** - changes only how the histogram is drawn, and is the same in every window that has one: see
[Process Windows](../../interface/process-windows.md).

```controls
window: The histogram
controls:
  - name: Histogram
    kind: readout
    default: ""
    effect: Shows how the samples are spread between 0 and 1. With **Preview** on it is counted from the preview, which is the adjusted image with the mask blended in. With **Preview** off, or with **Gamma** at 1, it is counted from the image as it is.
    range: One line for a grayscale image, three for RGB, in the bins of the toolbar's **Plot resolution**. The lines are the red, green and blue of the result, also when **Channel** names a color component.
    enabled: Shown while the toolbar's **Show histogram** button is on. Its lines follow the channel the preview displays, and it shows the original while the preview does.
    tradeoff: It is counted again with every preview, which adds to the work of each change. While you drag **Gamma** it still stands for the whole image, not for the part the preview pane shows.
    scope: The window only. It never changes a sample.
    persistence: Counted again when the target, the preview or **Plot resolution** changes.
    note: Holding the left button shows the coordinates under the pointer. Dragging with the middle button pans, and ++ctrl++ with the wheel zooms about the pointer. **Reset** leaves the zoom as it is.
  - name: Clipping Low
    kind: readout
    default: ""
    effect: Gives the share of the samples the histogram counts that are at or below 0.
    range: A percentage with three decimals, over all the channels together. It is counted from the samples, not from the bins, so it does not change with **Plot resolution**.
    enabled: Shown while the toolbar's **Show clipping info** button is on, as it is when the window opens.
    tradeoff: The power keeps 0 at 0, so with **RGB** or a single channel the reading hardly moves. A strong darkening rounds the lowest values of an integer image to 0, and floating-point samples below 0 leave the count when they become not-a-number. With a color component it also counts what the conversion back to RGB put at or below 0.
    scope: The window only.
  - name: Clipping High
    kind: readout
    default: ""
    effect: Gives the share of the samples the histogram counts that are at or above 1.
    range: Counted as **Clipping Low** is.
    enabled: Shown while the toolbar's **Show clipping info** button is on.
    tradeoff: The power keeps 1 at 1 and leaves a sample above 1 above 1, so with **RGB** or a single channel the reading hardly moves. A strong brightening rounds the highest values of an integer image to white. With a color component it also counts what the conversion back to RGB put at or above 1.
    scope: The window only.
```

```controls
window: Gamma Correction
controls:
  - name: Gamma
    kind: spinbox
    default: "1.00"
    widget: {min: 0.01, max: 9.9, step: 0.03, decimals: 2}
    effect: Sets the power of the curve. Above 1 brightens the midtones, below 1 darkens them, and at 1 nothing changes. Samples of 0 and 1 stay where they are.
    range: It has no unit. The slider beside the box moves the same value in 1,000 equal steps over the whole range.
    enabled: Disabled while Apply runs. At exactly 1, **Apply** is disabled.
    tradeoff: A value far from 1 presses most tones against white or against black, and the steps between the tones that remain show as bands.
    scope: The preview and Apply, on what **Channel** selects, for images and sequences. A mask is blended afterwards.
    persistence: Until the window closes. Each preview and each Apply takes the value as it is when it starts. **Reset** and a finished Apply return it to its default.
    note: While you drag the slider, the preview is computed on what the preview pane shows, which is the image reduced to the pane's size or the part in view at 100 % and closer. When you stop, it is computed on the whole image.
  - name: Target Image
    kind: combo
    default: The active image
    options: [The active image, Every other open image or sequence the window can work on]
    effect: Chooses the image Apply replaces, with the mask on its view. The histogram, the choices of **Channel** and the preview follow it.
    tradeoff: Apply replaces the image chosen here, so check it before you Apply. A sequence is written again in a temporary place and replaced only when every frame is done.
    persistence: Until the window closes. **Reset** leaves it as it is.
    note: Choosing another target keeps **Gamma** and returns **Channel** to its first choice.
  - name: Channel
    kind: combo
    default: RGB
    options: [RGB, Red, Green, Blue, CIE L* (Lightness), CIE a*, CIE b*, CIE c*, CIE h*, CIE X, CIE Y (Luminance), CIE Z, HSL Hue, HSL Saturation, HSL Lightness, HSV Hue, HSV Saturation, HSV Value, YCbCr Luma, YCbCr Blue Chroma, YCbCr Red Chroma]
    effect: Chooses what the power is applied to. **RGB** applies it to each of the three channels on its own. **Red**, **Green** or **Blue** applies it to that channel and copies the other two. Every other choice converts the image to that color model, applies the power to the one component and converts back to RGB.
    range: The components are the CIE lightness, a, b, chroma and hue, the CIE X, Y and Z, and those of HSL, HSV and YCbCr. Each is scaled to the range 0 to 1 first. Lightness runs from 0 to 100, chroma from 0 to 200 and the CIE hue from -180 to 180 degrees. X, Y and Z are divided by those of the white, so white is 1 in all three. The HSL and HSV hues are a share of a full turn. The a and b components and the two YCbCr chromas are signed, with neutral near one half.
    enabled: For an RGB image. A grayscale image shows a disabled **Grayscale** instead, and the power is applied to its one channel. Disabled while Apply runs.
    tradeoff: The same power on each channel changes the ratios between them, so **RGB** changes saturation, and a single color shifts the color balance. **RGB** and **Grayscale** cost the least, and a single color needs a temporary channel. A component costs two conversions of the whole image and a working copy of it, in 32-bit floating point, or 64-bit for a 64-bit image. A color the conversion back puts outside the range is not brought back. A floating-point image keeps it outside 0 to 1, and an integer image cuts it at the ends of its type.
    scope: The preview and Apply, for images and sequences. A mask is blended afterwards. It does not choose the lines of the histogram.
    persistence: Until the window closes. **Reset**, a finished Apply and a new target return it to **RGB**.
    note: Hue is treated as a line from 0 to 1 and not as a circle. A power on a signed component moves neutral colors too, because the middle of its range means no color. The image's red, green and blue are converted as linear when the image is described as linear RGB, and as sRGB-encoded otherwise. The component of an integer image is not rounded before the power, the image is rounded once on the way back, and the conversion back is the exact inverse of the conversion there.
  - name: Preview
    kind: check
    default: "On"
    effect: Shows the adjusted image beside the controls, with the mask blended in. Off removes the preview, and the histogram returns to the image as it is.
    enabled: It stays enabled while Apply runs.
    tradeoff: Each change computes the image again and counts the histogram again, which takes time and memory. On a large image, turn it off or use a **Preview area** while you adjust, and check the whole image before Apply.
    scope: The window only. On a sequence it shows the frame the view displays. A **Preview area** shows the result inside the area over the original, and does not limit Apply.
    persistence: Until the window closes. **Reset** leaves it as it is.
    note: The preview runs the operation Apply runs, so it shows what Apply writes. With the preview pane in Autostretch, the stretch is the target's and does not move while you adjust. A snapshot of the preview is a new image in the target's pixel type, named after the target, as in `M42 - Gamma Correction - SNAPSHOT`. It takes the mask and the preview area as they are, leaves the target alone, and is not recorded as nonlinear.
  - name: Reset
    kind: button
    default: ""
    effect: Returns **Gamma** to 1 and, for an RGB image, **Channel** to **RGB**.
    range: It leaves the target, **Preview**, the toolbar's choices and the zoom of the histogram as they are.
    enabled: Disabled while Apply runs.
    scope: The window only. The image is not touched, and Apply is disabled afterwards.
  - name: Apply
    kind: button
    default: ""
    effect: Applies the power to the image, or to every frame of the sequence, with **Gamma** and **Channel** as they are when you press it, and then returns both to their defaults.
    enabled: When **Gamma** is not 1, so it is disabled when the window opens. While a run works it is disabled too, as **Gamma**, **Target Image**, **Channel**, **Reset** and **Close** are.
    tradeoff: One pass over the samples on the threads the run is given, and a second copy of the image in memory. Signed and 32-bit integer images add a floating-point working copy, a color component adds its conversions, and a mask adds one more pass.
    scope: The whole image or sequence, with the mask and its inversion as they are when you press it, blended afterwards. While another process works on the target, Apply shows "Another process is executing for this image, please cancel that job first before running this process" and does nothing.
    persistence: "The image and its history keep the result. The history step is **Gamma Correction** and reads, for example, `Adjust gamma to 0.5 in channel: RGB`. For a grayscale image it ends after the value. It does not record the mask or the toolbar's choices, and undoing the step restores the image before."
    note: Closing the window instead discards the values. The window has no presets, and nothing of it is saved between sessions.
```

```controls
window: Sequence progress
controls:
  - name: Progress
    kind: readout
    default: ""
    effect: Shows the preparation and then the frame being processed, as in `Executing process in sequence. Frame 3/120`, from 0 to 100 percent.
    enabled: Shown only while Apply works on a sequence. A single image has no progress window.
    tradeoff: Every frame is decoded, processed and written again in a temporary place, which takes time and the disk space of a second copy until the run ends.
    scope: Every frame, processed as an image of it would be, with the gamma, the channel and the mask taken when you pressed Apply. The frames are read with the automatic debayer setting of the view, which can turn mosaic frames into RGB.
  - name: Cancel
    kind: button
    default: ""
    effect: Asks the run to stop. The temporary sequence is removed and the original stays as it was.
    range: The request is noticed before each frame and inside the work on one.
    note: The sequence is replaced, and its one history step recorded, only when every frame has succeeded. A frame that fails ends the run as Cancel does. After either, Apply again and let it finish.
```

## How it works

1. Every sample is read as a [fraction of full scale](../../reference/glossary.md#fraction-of-full-scale): an
   unsigned integer divided by its largest value, a signed one counted from its smallest value, a floating-point
   sample as it is.
2. **Channel** decides what the power is given: every channel, one channel, or one component of the image
   converted to a color model.
3. Each of those values is raised to the power and written back in the image's own pixel type. A component is
   converted back to RGB first.
4. A mask on the view then blends the result with the original, pixel by pixel.
5. The image replaces the target and its history gets one step. Its header, its
   [plate solution](../../reference/glossary.md#plate-solution) and its acquisition data are kept with every choice
   of **Channel**. A saved file records the new state in its header: `ANLIN` is `nonlinear`, `ANASRC` is `process`,
   `ANOP` is `gamma-correction`, and `ANBASIS` is `monochrome` or `display-rgb`. A linear encoding the image stated is
   no longer stated, and its [lineage](../../reference/glossary.md#lineage) gains an entry with the gamma.

### Model

A sample \(x\), as a fraction of full scale, becomes \(y\), with \(g\) the value of **Gamma**:

\[ y = x^{1/g} \tag{1} \]

The exponent is the inverse of the value you set. For \(x\) between 0 and 1, equation (1) keeps 0 at 0 and 1 at 1.
With \(g\) above 1 every value between them rises, and with \(g\) below 1 every value between them falls. The
sample that lands at one half is \(0.5^{g}\).

With **RGB**, equation (1) is applied to red, green and blue separately. A pixel whose three channels are equal
stays neutral. In every other pixel the ratio between two channels \(r\) and \(b\) becomes

\[ \frac{r_{\mathrm{out}}}{b_{\mathrm{out}}} = \Bigl(\frac{r}{b}\Bigr)^{1/g} \tag{2} \]

so a value below 1 widens the differences between the channels and strengthens the colors, and a value above 1
weakens them.

### Boundaries

- Nothing is clamped. A floating-point sample above 1 is raised as it is and stays above 1, and the preview
  treats a sample outside the range as Apply does.
- A pixel the image marks as invalid is left as it was.
- An unsigned 8-bit or 16-bit channel goes through a table with one entry for each of its values, computed in
  single precision. Signed and 32-bit integers are raised in a floating-point copy, in double precision for the
  32-bit ones. Floating-point samples are raised in their own precision, the 32-bit ones by a fast routine that
  stays within a small error of the exact power.
- An integer result is rounded to the nearest value, a tie to the even one.
- A run that is stopped writes nothing, and the image and its history stay as they were. A run stops when its
  target closes, or with **Cancel** for a sequence. A single image has no progress window and cannot be cancelled
  once Apply starts.

### Worked example

The image in the picture above is the linear [master](../../reference/glossary.md#master) after Histogram
Transformation with **Shadows** and
**Midtones** at 0.02, which leaves the background near 0.28. With **Channel** at **RGB** and **Gamma** at 0.50,
the exponent of equation (1) is 2. The background drops to \(0.28^2 = 0.078\), a sample of 0.50 becomes 0.25 and
one of 0.90 becomes 0.81. The sky turns nearly black, the nebula keeps its bright parts, and the stars that were
white stay white. By equation (2), a pixel that was twice as red as blue is now four times as red.

```figure
images:
  - image: ../../assets/images/processes/stretching/gamma-correction-before.webp
    alt: The whole frame before the adjustment, the red and white clouds of the nebula across a field of stars on a dark gray background.
    label: Before
  - image: ../../assets/images/processes/stretching/gamma-correction-after.webp
    alt: The whole frame after the adjustment, the same nebula darker and redder on a nearly black background, with its core and the stars still bright.
    label: After
caption: >-
  The stretched Carina Nebula master before and after Apply with Gamma at 0.50. Both are the whole frame as the view
  shows it in the Linear display mode, without a display stretch.
```

The other way, **Gamma** at 2 takes the square root. Samples of 0, 0.25, 0.5, 0.75 and 1 become 0, 0.5, 0.7071,
0.8660 and 1, and the background of this image would rise to 0.53. With **Gamma** at 0.50 the same five samples
become 0, 0.0625, 0.25, 0.5625 and 1. The preview shows the numbers Apply writes.

## Starting points

| Situation | Start | Then adjust |
| --- | --- | --- |
| A stretched image a little too dark or too bright | **RGB**, and **Gamma** from 1.05 to 1.20 to brighten or from 0.95 to 0.85 to darken | Stay near 1, and check the histogram, the preview at 100 % and the colors of the stars. Too strong a value lifts the background with its noise, or presses the tones together. |
| The tone must change through one component | **HSL Lightness**, **CIE Y (Luminance)** or **HSV Value**, with a mild value | Check the bright colored regions for colors that left the range on the way back to RGB. |
| A hue or a chroma component | No change, unless you mean to remap colors | Use very small changes and watch the neutral areas and the hues near the wrap. A tool made for color does it better. |
| An adjustment through a mask | The value first, near 1 | Check the edges of the mask in the preview. If they show, weaken the change or correct the mask itself. |

## When it goes wrong

!!! warning "Apply records the image as nonlinear"
    Every Apply marks the result nonlinear, with any choice of **Channel** and with or without a mask, and a later
    step treats it so. Finish the steps that need linear data first, or undo the step in the image's history.

!!! danger "Negative samples become not-a-number"
    In a floating-point image, a sample below 0 has no power and is written as not-a-number, at every value of
    **Gamma**. A linear master often holds some after background correction. Use this window after the stretch,
    and check the lowest value of the image first when in doubt.

Apply is disabled
:   **Cause.** **Gamma** is exactly 1, which changes nothing.
:   **Fix.** Set another value.

Gamma above 1 brightens when you expected it to darken
:   **Cause.** The image is raised to the inverse of the value, as in equation (1).
:   **Fix.** Use a value below 1 to darken.

The colors are stronger or weaker after the adjustment
:   **Cause.** With **RGB** the power is applied to each channel alone, which changes the ratios between them.
:   **Fix.** Use a value nearer 1, or try a lightness, luminance or value component on a duplicate and compare.

Bands show in smooth gradients
:   **Cause.** A value far from 1 presses many tones into few, and the steps between them become visible.
:   **Fix.** Use a value nearer 1.

Colors jump after a power on a hue
:   **Cause.** The hue is treated as a line from 0 to 1, although its two ends are the same color.
:   **Fix.** Leave the hue components alone, and use a tool made for hue.

A floating-point image has samples outside 0 to 1 after a color component
:   **Cause.** The conversion back to RGB is not clamped.
:   **Fix.** Use a value nearer 1, or choose **RGB** or a single channel.

Apply reports "An error occurred when running the process:" and names the memory budget
:   **Cause.** The memory limit cannot hold the copy of the image the run needs, so nothing was written.
:   **Fix.** Close other images or work on a smaller one, or raise the memory limit on the Processing page of the
    settings, which takes effect at the next start.

## Related

- [Background Neutralization](../color-calibration/background-neutralization.md),
  [Photometric Color Calibration](../color-calibration/photometric-cc.md) and
  [Spectrophotometric Color Calibration](../color-calibration/spectrophotometric-cc.md), which need the image
  linear and come before.
- [Histogram Transformation](histogram-transformation.md),
  [Generalized Hyperbolic Transformation](gh-transformation.md), [ArcSinh Stretch](arcsinh-stretch.md) and
  [Statistical Stretch](statistical-stretch.md), for the main stretch.
- [Exposure Correction](exposure-correction.md), which multiplies and clamps instead of keeping the two ends, by a
  value that is not a number of photographic stops.
- [Curves Transformation](curves-transformation.md), to shape tone and color point by point.
- [Image History](../../interface/image-history.md) and [Image Statistics](../../interface/image-statistics.md), to
  undo the step and to check the range of the samples.
- [Process Windows](../../interface/process-windows.md), for the preview, the histogram toolbar and what every
  process window shares.
