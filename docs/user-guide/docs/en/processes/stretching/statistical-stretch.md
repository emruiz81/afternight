---
kind: process
stage: stretch
menu: Process > Intensity Transformations > Statistical Stretch
summary: >-
  Measures the median and a black point of the image, then stretches it with one curve that places the median at the
  level you choose.
data_in: linear
data_out: nonlinear
destructive: true
preview: real-time
sequences: true
masks: post-blend
external: none
glance:
  enabled: >-
    An image or a sequence is active, no other Statistical Stretch window is open on it, and no window that needs the
    image for itself is open on it.
  inputs: >-
    One grayscale or RGB image or sequence, and an optional mask of the same size on its view. Any other number of
    channels is refused.
  outputs: >-
    The same view, replaced, with one history step. Its size and pixel type are kept. No file, preset or report is
    written.
  state: A linear master, mono or RGB, after gradient removal and color calibration.
derived: false
---
# Statistical Stretch

Statistical [Stretch](../../reference/glossary.md#stretch) stretches a
[linear image](../../reference/glossary.md#linear-image) from its own statistics. It measures the median and a
[black point](../../reference/glossary.md#black-point), sends everything at or below the black point to black, and
places the median at the level you set in **Target Median**. An RGB image gets one curve for each channel, or one for
the three. Two optional stages follow: a curve that lifts the range above the target, and a division by the
brightest sample.

The preview shows a result as soon as the window opens, stretched from the starting values. **Apply**
replaces the image or [sequence](../../reference/glossary.md#sequence) with the stretched one, keeps its size and
[pixel type](../../reference/glossary.md#pixel-type), and adds one step to its
[history](../../reference/glossary.md#history). The result is recorded as nonlinear.

```figure
image: ../../assets/images/processes/stretching/statistical-stretch.webp
alt: >-
  The Statistical Stretch window. On the left, a histogram of three overlapping peaks in the lower part of the range,
  with two clipping shares written in its corner. Under it a toolbar, the Target Median box and slider, the Linked
  Stretch check box, the Black Point Sigma box and slider, three more check boxes, the Curves Boost box and slider,
  a line of text with three black points and a count of pixels, Target Image and Preview. On the right, the preview
  shows a red nebula on a dark background. Reset, Close and Apply run along the bottom.
caption: >-
  Statistical Stretch on a linear master with Target Median at 0.15. The histogram is the result's, the line under
  Curves Boost gives the black points, and the preview shows what Apply writes.
pins:
  - {n: 2, x: 10.9, y: 45.3}
  - {n: 3, x: 1.9, y: 49.2}
  - {n: 4, x: 1.9, y: 73.8}
  - {n: 6, x: 70.4, y: 54.4}
  - {n: 7, x: 90.3, y: 96.7}
```

## Before you start

- Finish the work that needs linear data first: gradient removal, color calibration, measurement,
  [deconvolution](../../reference/glossary.md#deconvolution) and linear noise reduction. The process finds no
  gradient, removes no [pedestal](../../reference/glossary.md#pedestal) and calibrates no color.
- **Apply** replaces the image. Its history keeps the step before, and this window has no inverse. Duplicate the
  image first to compare two stretches.
- The image is grayscale or RGB. Prefer a floating-point one: an integer image is rounded back to its own values.
- Every statistic is the whole image's, also for a [**Preview area**](../../reference/glossary.md#preview-area) and
  under a [mask](../../reference/glossary.md#mask), which blends the result with the original afterwards.
- On a sequence, every frame is measured and stretched alone.

## Steps

1. **Open the window.** Make the linear [master](../../reference/glossary.md#master) the
   [active image](../../reference/glossary.md#active-image) and choose **Process > Intensity Transformations >
   Statistical Stretch**. The preview shows the image already stretched, and the histogram shows that result.
2. **Set the background level.** Move **Target Median** to the brightness the median should have. On most deep-sky
   images the median is the sky background, and the peak of the histogram moves with it.
3. **Choose one curve or three.** On an RGB image, leave **Linked Stretch** off to bring the three backgrounds to
   one level, or select it to give the channels one curve. Compare the colors of the background and the stars.
4. **Check the black point.** Read the line under **Curves Boost**: it gives the black point and the number of pixels
   that become black. If that is more than the background can lose, raise **Black Point Sigma** or select **No
   Black Clipping**.
5. **Leave the finishing stages off at first.** Select **Apply Curves Adjustment** and raise **Curves Boost** only
   when the range above the target needs more lift. Select **Normalize Image** only when the brightest sample must
   become white.
6. **Check the preview at 100 %.** Zoom into the background, the faint areas and the brightest stars, and press
   ++space++ over the preview to compare with the original.
7. **Apply.** Choose **Apply**. The image in its view is the stretched one, its history lists **Statistical
   Stretch**, and the controls return to their starting values. The preview then shows a second stretch, of the
   stretched image: close the window.

## Controls

The toolbar under the histogram - zoom, grid, clipping shares, scales, areas and **Plot resolution** - changes only
how the histogram is drawn, and is the same in every window that has one: see
[Process Windows](../../interface/process-windows.md).

```controls
window: The histogram
controls:
  - name: Histogram
    kind: readout
    default: ""
    effect: Shows the counts of the preview, which is the stretched image with the mask blended in. With **Preview** off it shows the image as it is.
    range: One line for a grayscale image, three for RGB, in the bins of the toolbar's **Plot resolution**.
    enabled: Always shown. The lines it draws follow the channel the preview displays.
    tradeoff: It is counted again after every preview, which adds to the work of each change.
    scope: The window only, and it never changes a sample. While the preview shows the original, so does the histogram.
    persistence: Counted again when the target, the preview or **Plot resolution** changes.
    note: Holding the left button shows the coordinates under the pointer, which does not set the black point. Dragging with the middle button pans, and ++ctrl++ with the wheel zooms around the pointer.
  - name: Clipping Low and Clipping High
    kind: readout
    default: ""
    effect: Give the share of the plotted samples at or below 0 and at or above 1, over every channel.
    enabled: Shown while the toolbar's **Show clipping info** button is on, as it is when the window opens.
    tradeoff: They count samples of the result, with the mask. The line under **Curves Boost** counts pixels of the image at or below the black point, without it. The two numbers answer different questions and need not agree.
    scope: The image the histogram plots.
```

```controls
window: Statistical Stretch
controls:
  - name: Target Median
    kind: spinbox
    default: "0.250"
    widget: {min: 0.01, max: 0.99, step: 0.01, decimals: 3}
    effect: Sets the level the median of the image lands at. Higher gives a brighter background and less room for everything above it. Lower gives a darker result.
    range: The slider beside the box moves the same value in 1,000 equal steps.
    tradeoff: A high target brightens the background with its noise and presses the bright signal together.
    scope: The preview and Apply. Each channel's median with **Linked Stretch** off, the median of the three channels together with it on.
    persistence: Until the window closes. **Reset** and a finished Apply return it to its default.
    note: The median lands on the target before **Normalize Image**, the mask and the rounding of an integer image. Each of them can move the median you measure afterwards.
  - name: Linked Stretch
    kind: check
    default: "Off"
    effect: Off measures each channel alone and gives it its own black point and curve. On measures the three channels as one set of samples and gives them one black point and one curve.
    enabled: For an RGB image. It is disabled for a grayscale image, which has one curve in any case.
    tradeoff: Off puts every channel's median on the target, which removes a color cast from the background and can shift the color of everything brighter. On keeps a neutral sample neutral, but one curve on each channel still changes the ratios between the colors and their saturation.
    scope: The preview, Apply and the line under **Curves Boost**.
    persistence: Until the window closes. **Reset** and a finished Apply clear it. The history does not record it.
    note: It is not a stretch of the luminance. Either way, calibrate color first.
  - name: Black Point Sigma
    kind: spinbox
    default: "5.00"
    widget: {min: 0.5, max: 10, step: 0.1, decimals: 2}
    effect: Sets how far below the median the black point lies, in units of the spread of the dark half of the image. Higher lowers the black point and softens the stretch. Lower raises it, sends more of the background to black and steepens the stretch.
    range: The black point is never placed below the smallest sample or above 0.99. The slider moves the same value in 1,000 equal steps.
    enabled: While **No Black Clipping** is off.
    tradeoff: A low value cuts faint signal for good. The spread is estimated from at most 400,000 samples of each channel, or of the three together when linked, always the same ones for one image.
    scope: The preview, Apply and the line under **Curves Boost**, for each channel or for the three together.
    persistence: Until the window closes. **Reset** and a finished Apply return it to its default.
  - name: No Black Clipping
    kind: check
    default: "Off"
    effect: Takes the smallest sample of the image, or of each channel, as the black point in place of the one **Black Point Sigma** sets.
    enabled: Always. Selecting it disables **Black Point Sigma**.
    tradeoff: Nothing lies below the black point, so no faint signal is cut. One dark outlier then sets the black point, and the farther it lies below the background, the softer the stretch of everything else.
    scope: The preview, Apply and the line under **Curves Boost**, for each channel or for the three together.
    persistence: Until the window closes. **Reset** and a finished Apply clear it.
    note: Despite its label, the smallest sample itself still becomes black and is counted by the line.
  - name: Normalize Image
    kind: check
    default: "Off"
    effect: Divides the result by its brightest sample, which becomes white.
    range: One maximum over every channel, taken after the curve of **Apply Curves Adjustment**. Nothing is divided when it is not above zero.
    tradeoff: It moves the median off the target and amplifies the noise with everything else, and one hot pixel can set the scale. When a sample already reaches white after the stretch, it changes nothing.
    scope: The preview and Apply. Every channel is divided by the same number, which keeps the ratios between the channels at this stage.
    persistence: Until the window closes. **Reset** and a finished Apply clear it.
  - name: Apply Curves Adjustment
    kind: check
    default: "Off"
    effect: Adds a fixed curve after the stretch that leaves everything up to the target as it is and lifts the range above it.
    enabled: Always. Selecting it enables **Curves Boost**.
    tradeoff: It brightens the upper midtones, can flatten the highlights and can raise the brightest sample.
    scope: The preview and Apply, with the same curve on every channel.
    persistence: Until the window closes. **Reset** and a finished Apply clear it.
  - name: Curves Boost
    kind: spinbox
    default: "0.20"
    widget: {min: 0, max: 1, step: 0.05, decimals: 2}
    effect: Sets how far the curve lifts the range above the target. Higher lifts more. At 0 the curve changes nothing, and at 1 everything above the first quarter of that range is white.
    range: The slider beside the box moves the same value in 1,000 equal steps.
    enabled: While **Apply Curves Adjustment** is on. Its value has no effect while that is off.
    tradeoff: A high value makes a steep step just above the target and wide areas near white.
    scope: The preview and Apply, on every channel.
    persistence: Until the window closes. **Reset** and a finished Apply return it to its default. The history does not record the value.
  - name: Black clipping
    kind: readout
    default: ""
    effect: Gives the black point and the number of pixels at or below it, which the stretch sends to black, with their share of the image.
    range: The black point has six decimals and the share four. An RGB image with **Linked Stretch** off shows one black point for each of R, G and B, and counts a pixel once when any of its channels is at or below its black point.
    enabled: Always shown, under **Curves Boost**. With **No Black Clipping** it begins "Black clipping disabled". When there is nothing to measure it reads "No clipping statistics available."
    tradeoff: The count is exact, over every valid pixel of the image. The black point rests on the sampled spread.
    scope: The whole image as it is, without the mask and whatever the **Preview area**. It follows the target, **Linked Stretch**, **Black Point Sigma** and **No Black Clipping**, and not **Target Median** or the finishing stages.
    persistence: Measured again when one of those changes. It is not saved, and its text can be selected and copied.
  - name: Target Image
    kind: combo
    default: The active image
    options: [The active image, Every other open image or sequence the window can work on]
    effect: Chooses the image Apply replaces. The statistics, the line under **Curves Boost**, the histogram and the preview follow it.
    tradeoff: The image you choose is the one replaced. A larger image or a sequence takes more time, memory and disk.
    persistence: Until the window closes. **Reset** leaves it as it is.
  - name: Preview
    kind: check
    default: "On"
    effect: Shows the stretched image beside the controls, with the mask blended in. Off removes it, and the histogram then shows the image as it is.
    tradeoff: Each change stretches the whole image again, with a **Preview area** too, and counts the histogram again. The statistics are measured once for each state of the image and kept. On a large image, turn it off while you adjust and on again for the last check.
    scope: The window only. On a sequence it shows the frame the view displays.
    persistence: Until the window closes. **Reset** leaves it as it is.
    note: A snapshot taken from the preview is stretched as the preview is, from the whole image's statistics.
  - name: Reset
    kind: button
    default: ""
    effect: Returns the three values to their defaults, clears the four check boxes and measures the line under **Curves Boost** again.
    range: It leaves the target, **Preview**, the toolbar's choices and the zoom of the histogram as they are.
    scope: The window only. The image is not changed, the preview shows the defaults again and Apply stays enabled.
  - name: Apply
    kind: button
    default: ""
    effect: Stretches the image, or every frame of the sequence, with the values the window shows, and then returns the controls to their defaults.
    enabled: As soon as the window opens. Untouched, it writes the stretch of the starting values, which is what the preview shows. While it works every control is disabled, and when another process is working on the image it only shows a warning.
    tradeoff: The whole image is stretched on several threads at once, and the result does not depend on how many.
    scope: The whole image, or every frame of the sequence measured and stretched alone, with the mask blended afterwards. White in the mask takes the stretched image, black keeps the original, gray blends the two, and **Invert Mask** swaps them. Set the stretch with the mask off, then enable it and check its edges.
    persistence: The image and its history keep the result. The history records the target median, the black point sigma, and whether **No Black Clipping**, **Normalize Image** and the curve were on. The image keeps its header, its acquisition data, its plate solution and the record of its color calibration, and is recorded as display RGB when it is a color image and as monochrome when it is grayscale. It states no transfer function afterwards.
    note: Closing the window instead discards the values. Nothing of the window is saved between sessions.
```

```controls
window: Sequence progress
controls:
  - name: Progress
    kind: readout
    default: ""
    effect: Shows the preparation and the frame being stretched, from 0 to 100 percent.
    enabled: Shown only while Apply works on a sequence. A single image has no progress window.
    tradeoff: The sequence is written again in a temporary place, which needs the disk space of a second copy.
    note: The stretched sequence takes the place of the original only after every frame succeeded, with one step in its history.
  - name: Cancel
    kind: button
    default: ""
    effect: Asks the run to stop. The temporary sequence is removed and the original stays as it was.
    range: The request is noticed before each frame, and while one is measured and stretched.
```

## How it works

1. The valid pixels of the whole image are measured, once for each state of the image: the smallest sample and the
   median of each channel exactly, and the spread of its dark half from a sample of the image. A linked stretch
   measures the three channels as one.
2. The black point is placed below the median by **Black Point Sigma** times that spread, or at the smallest sample
   with **No Black Clipping**.
3. Every sample is placed between the black point and 1, held to that range, and sent through one curve that keeps
   black and white where they are and moves the median to **Target Median**.
4. The optional curve and the optional normalization follow. The result is held to the range 0 to 1 and written
   back in the image's own pixel type, rounded to the nearest value.
5. A mask on the view then blends the result with the original, and the image replaces the target. Its
   [processing state](../../reference/glossary.md#processing-state) is recorded as nonlinear with the operation
   `statistical-stretch`.

### Model

For one channel, or for the three of a linked stretch, let \(x_{\min}\) be the smallest sample, \(M\) the median
and \(L\) the median of the samples at or below \(M\). The spread of the dark half is

\[ \sigma = 1.4826 \, \operatorname{median}_{x \le M} \lvert x - L \rvert \tag{1} \]

With \(k\) the value of **Black Point Sigma**, the black point is

\[ B = \min\bigl(0.99,\ \max(x_{\min},\ M - k\,\sigma)\bigr) \tag{2} \]

and with **No Black Clipping** it is \(B = x_{\min}\). A sample \(x\) and the median are then placed between the
black point and 1:

\[ z = \frac{x - B}{1 - B}, \qquad m = \frac{M - B}{1 - B} \tag{3} \]

A sample at or below \(B\) gives \(z = 0\), and one at or above 1 gives \(z = 1\). With \(t\) the value of **Target
Median**, the result is

\[ f(z) = \frac{(m - 1)\,t\,z}{m\,(t + z - 1) - t\,z} \tag{4} \]

Equation (4) sends the black point to 0, the median to \(t\) and 1 to 1 in one pass, with nothing to iterate. It is
a [midtones transfer function](../../reference/glossary.md#midtones-transfer-function) whose midtones balance is
\(m(1 - t) / (m + t - 2mt)\).
The smaller \(m\) is, the steeper the curve: a black point close to the median makes a hard stretch, and a higher
**Black Point Sigma** or **No Black Clipping** a softer one.

With \(q\) the value of **Curves Boost**, the curve of **Apply Curves Adjustment** runs in straight lines through
six points. Four lie on the diagonal, at 0, \(t/2\), \(t\) and 1. The other two, at \(a = t + 0.25\,(1 - t)\) and
\(b = t + 0.75\,(1 - t)\), are raised:

\[ a \mapsto a^{\,1 - q}, \qquad b \mapsto b^{\,(1 - q)^{2}} \tag{5} \]

### Boundaries

- Pixels the image marks as invalid are left out of the statistics and left unchanged. An image without one valid
  pixel is refused with "no pixel participates in this statistical-stretch analysis", as when a registration border
  covers the whole frame.
- A valid sample that is not a finite number stops the run with "a selected statistical-stretch sample is not
  finite", and nothing is written.
- An unsigned integer image is read as fractions of its full scale, a signed one from its smallest value to its
  largest, and a floating-point one as it is. A 64-bit floating-point or 32-bit integer image is computed in double
  precision, every other type in single precision.
- Samples above 1 become white. The divisor of equation (3) and the denominator of equation (4) are kept at least
  \(10^{-12}\) away from zero.
- Equation (1) uses the median of the sample it is taken on. The median of an even number of samples is the mean of
  the two middle ones, so a median measured after the stretch can differ from the target in its last digits.

### Worked example

The master in the picture above is an RGB image whose three channels have their median at 0.02766. With **Target
Median** at 0.15 and every other control at its starting value, the line under **Curves Boost** reads "Black
clipping @ R=0.023308, G=0.021179, B=0.021947: 4087/39338534 pixels (0.0104%)".

For the red channel, equation (3) gives \(m = 0.0045\), and equation (4) moves the median to 0.15, as in green and
blue. A red sample of 0.05, less than twice the median, gives \(z = 0.027\) and lands at 0.53. The same value in
green lands at 0.45, because the green black point is lower: with **Linked Stretch** off, a neutral sample above
the background takes a color.

```figure
images:
  - image: ../../assets/images/processes/stretching/statistical-stretch-before.webp
    alt: The whole frame before the stretch, black except for a few of the brightest stars.
    label: Before
  - image: ../../assets/images/processes/stretching/statistical-stretch-after.webp
    alt: The whole frame after the stretch, the red and white clouds of the nebula across a field of stars on a dark gray background.
    label: After
caption: >-
  The Carina Nebula master before and after Apply with Target Median at 0.15 and every other control at its starting
  value. Both are the whole frame as the view shows it in the Linear display mode, without a display stretch.
```

Equations (1) and (2) can be followed by hand on eight samples: 0.02, 0.08, 0.15, 0.22, 0.30, 0.42, 0.62 and 0.85.
Their median is 0.26, the dark half is the first four with a median of 0.115, and \(\sigma = 0.0964\). With **Black
Point Sigma** at 0.5 the black point is 0.2118. The first three samples become black, which is what the line
counts, and at the default target 0.22 lands at 0.051.

## Starting points

| Situation | Start | Then adjust |
| --- | --- | --- |
| A linear master | The starting values | The black point first, then **Target Median**. |
| A calibrated RGB master | **Linked Stretch** off, then on | Keep the one whose background and stars look right at 100 %. |
| Faint signal is cut | **Black Point Sigma** raised | **No Black Clipping**, if the line still counts too many pixels. |
| The range above the background is flat | **Apply Curves Adjustment**, **Curves Boost** near 0.05 | Raise the boost slowly. **Normalize Image** last, if at all. |

## When it goes wrong

!!! warning "Apply cannot be taken back from this window"
    The stretch replaces the image and records it as nonlinear. Finish color calibration and every other linear
    step first, and use the image's history to return to the linear master.

!!! warning "A low Black Point Sigma cuts faint signal"
    Every sample at or below the black point becomes black. Raise **Black Point Sigma** or select **No Black
    Clipping** when the line under **Curves Boost** counts more pixels than the background should lose.

!!! warning "The history leaves out two settings"
    The step records neither **Linked Stretch** nor the value of **Curves Boost**. Note them yourself when you must
    be able to repeat the stretch.

"a statistical stretch needs a mono or RGB source"
:   **Cause.** The image has a number of channels other than one or three.
:   **Fix.** Extract or combine its channels into a grayscale or an RGB image first.

After Apply the preview no longer looks like the image
:   **Cause.** A finished Apply returns the controls to their starting values, and the preview shows them applied to
    the stretched image.
:   **Fix.** Close the window. To stretch another image, set your values again.

The median measured after Apply is not the Target Median
:   **Cause.** **Normalize Image**, a mask or the rounding of an integer image moved it, or the stretch was linked
    and you measured one channel.
:   **Fix.** Turn **Normalize Image** off and compare on an unmasked floating-point image.

The colors shifted after the stretch
:   **Cause.** With **Linked Stretch** off each channel got its own curve, and with it on one curve still changes
    the ratios between the colors.
:   **Fix.** Undo the step, compare the two settings in the preview, and calibrate color before you stretch.

Normalize Image changes nothing, or brightens everything
:   **Cause.** It divides by the brightest sample of the result, which is already white or lies below it.
:   **Fix.** Leave it off unless the brightest sample must be white, and judge it after the curve is set.

The clipping shares in the histogram and the line under Curves Boost disagree
:   **Cause.** The histogram counts samples of the result at 0 and at 1, and the line counts pixels of the image at
    or below the black point.
:   **Fix.** Read the line for what the stretch cuts, and use Image Statistics after Apply for the result.

## Related

- [Background Neutralization](../color-calibration/background-neutralization.md),
  [Photometric Color Calibration](../color-calibration/photometric-cc.md) and
  [Spectrophotometric Color Calibration](../color-calibration/spectrophotometric-cc.md), which need the image linear
  and come before.
- [Histogram Transformation](histogram-transformation.md), to set the black point and the midtones of the same kind
  of curve yourself.
- [Generalized Hyperbolic Transformation](gh-transformation.md) and [ArcSinh Stretch](arcsinh-stretch.md), stretches
  with other curves.
- [Curves Transformation](curves-transformation.md), to shape tone and color after the stretch.
- [Image Statistics](../../interface/image-statistics.md) and [Image History](../../interface/image-history.md), to
  measure the result and to return to the linear master.
- [Process Windows](../../interface/process-windows.md), for the preview, the histogram toolbar and what every
  process window shares.
