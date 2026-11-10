---
kind: process
stage: stretch
menu: Process > Intensity Transformations > ArcSinh Stretch
summary: >-
  Lifts faint linear signal with an inverse hyperbolic sine transfer that compresses the highlights and keeps the
  color ratios of an RGB image.
data_in: linear
data_out: nonlinear
destructive: true
preview: real-time
sequences: true
masks: post-blend
external: none
glance:
  enabled: >-
    An image or a sequence view is active, no ArcSinh Stretch window is open on it yet, and no window that needs
    the image for itself is open on it. The number of channels is not checked.
  inputs: >-
    One grayscale or three-channel RGB image or sequence with samples between 0 and 1, and an optional mask on its
    view.
  outputs: >-
    The same view, replaced, with one history step. Its size and pixel type are kept. No file, preset or report is
    written.
  state: A linear master, mono or RGB, after background correction and color calibration.
derived: false
rewritten: true
---
# ArcSinh Stretch

ArcSinh Stretch brightens a [linear image](../../reference/glossary.md) with an inverse hyperbolic sine curve. The
curve lifts faint signal strongly and compresses the highlights, and it leaves black and white where they are. On
an RGB image the curve is applied to a luminance, and the three channels are multiplied by one factor. The ratios
between the colors are kept until a channel clips.

**Black Point** first sends every sample at or below a level you choose to black. It estimates nothing from the
image.

**Apply** replaces the image or sequence with the stretched one, keeps its size and pixel type, and adds one step to
its history. The result is recorded as nonlinear.

```figure
image: ../../assets/images/processes/stretching/arcsinh-stretch.webp
alt: >-
  The ArcSinhStretch window. On the left, a histogram with one peak near its left fifth and the lines Clipping Low
  and Clipping High in its corner. Under it a toolbar, Stretch Factor at 200.00 and Black Point at 0.02000 with
  their sliders, Human-weighted luminance checked, Target Image and Preview. On the right, the preview shows a red
  nebula on a dark gray background. Reset, Close and Apply run along the bottom.
caption: >-
  ArcSinh Stretch on a linear master with Stretch Factor at 200 and Black Point at 0.02. The histogram is counted
  from the stretched preview, and the preview shows what Apply writes.
pins:
  - {n: 2, x: 2.0, y: 54.0}
  - {n: 3, x: 2.0, y: 59.0}
  - {n: 5, x: 2.0, y: 63.8}
  - {n: 6, x: 73.0, y: 26.0}
  - {n: 7, x: 90.9, y: 96.0}
```

## Before you start

- Finish background correction and color calibration first. The process estimates no background, decodes no
  encoded RGB and calibrates no channel weights, and after **Apply** the image is nonlinear.
- **Apply** replaces the image or sequence. Duplicate it first, or use its history, which keeps the step before.
- Use a grayscale or a three-channel RGB image with samples between 0 and 1.
- A raw color-filter mosaic is stretched as a grayscale image, which breaks the relation between its sensor values:
  debayer it first. For a mosaic sequence, decide whether its automatic debayer turns the frames into RGB.
- Prefer a floating-point image. Integer samples are stretched in a floating-point copy and rounded back.
- A [mask](../../reference/glossary.md) on the view blends the stretched image with the original after the
  stretch: white takes the result, black keeps the original, gray mixes them, and **Invert Mask** swaps the two.

## Steps

1. **Open the window.** Make the linear master the active image and choose **Process > Intensity Transformations >
   ArcSinh Stretch**. The window opens under the title **ArcSinhStretch**, with a narrow peak at the left of the
   histogram and a nearly black preview.
2. **Raise the stretch factor.** Increase **Stretch Factor** until the faint signal shows in the preview. The
   background turns gray and the peak of the histogram moves to the right.
3. **Set the black point.** Raise **Black Point** until the background is dark again. Stop while **Clipping Low**
   in the histogram still reads near zero: what it counts is lost.
4. **Adjust the stretch factor again.** The black point changes what the curve receives, so the same factor now
   lifts less. Raise it until the faint signal is back in the preview.
5. **Choose the luminance.** On an RGB image, leave **Human-weighted luminance** on, or clear it to weigh the three
   channels equally. The preview shows the difference in strongly colored regions.
6. **Check the preview at 100 %.** Zoom the preview into the faint areas and the brightest colored stars, and press
   ++space++ over it to compare with the original. **Clipping High** tells how many samples reached 1.
7. **Apply.** Choose **Apply**. The image in its view is the stretched one, its history lists `ArcSinhStretch` with
   the three values, and the controls return to their defaults.

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
    effect: Shows how the samples of the image are spread between 0 and 1. With **Preview** on it is counted from the stretched preview, mask included. With **Preview** off it is counted from the image as it is.
    range: One line for a grayscale image, three for RGB, at the toolbar's **Plot resolution**.
    enabled: Shown while the toolbar's histogram button is on. Its lines follow the channel the preview displays, and it shows the original while the preview does.
    tradeoff: It is counted again after every preview, which adds to the time each change takes.
    scope: The window only. It never changes a sample.
    note: Holding the left button shows the coordinates under the pointer, and does not pick a black point. Dragging with the middle button pans, and ++ctrl++ with the wheel zooms about the pointer.
  - name: Clipping Low
    kind: readout
    default: ""
    effect: Gives the share of the samples the histogram counts that are at or below 0. In the stretched preview those are the samples at or below the black point.
    range: A percentage with three decimals, over all the channels together. It is counted from the samples, not from the bins, so it does not change with **Plot resolution**.
    enabled: Shown while the toolbar's clipping button is on.
    scope: The window only.
  - name: Clipping High
    kind: readout
    default: ""
    effect: Gives the share of the samples the histogram counts that are at or above 1. In the stretched preview those are the samples that reached 1 in their channel.
    range: Counted as **Clipping Low** is.
    enabled: Shown while the toolbar's clipping button is on.
    scope: The window only.
```

```controls
window: ArcSinhStretch
controls:
  - name: Stretch Factor
    kind: spinbox
    default: "0.00"
    widget: {min: 0, max: 1000, step: 1, decimals: 2}
    effect: Sets the strength of the curve. Higher lifts the shadows and the midtones more and compresses the highlights more. Lower brings the curve toward a straight line, and at 0 only the black point acts.
    range: The slider beside it moves in whole steps over the same range. Black stays black and white stays white at every value.
    enabled: Disabled while Apply runs. With **Black Point** it decides whether **Apply** is enabled.
    tradeoff: A strong factor lifts the background and its noise with the signal, and clips a channel of the bright colored stars. It does not change the time a run takes.
    scope: The preview and Apply, on images and sequences. A grayscale image gets the curve itself, an RGB image one factor for its three channels. A mask is blended afterwards.
    persistence: Until the window closes. Each preview and each Apply takes the value as it is when it starts. **Reset** and a finished Apply return it to its default.
  - name: Black Point
    kind: spinbox
    default: "0.00000"
    widget: {min: 0, max: 0.2, step: 0.001, decimals: 5}
    effect: Sets the level that becomes black before the curve. Higher sends more of the low samples to black and spreads the rest over the range. Lower keeps more of the shadows.
    range: The slider beside it moves in steps of 0.0002. A sample of 1 stays 1.
    enabled: Disabled while Apply runs. Any value above 0 enables **Apply**, even with **Stretch Factor** at 0.
    tradeoff: Set above the background, it erases faint signal for good. It is taken from each channel alone, so it changes the ratios between the colors before the common factor is computed.
    scope: The preview and Apply, on every channel of images and sequences, before the curve. A mask is blended afterwards.
    persistence: Until the window closes. Each preview and each Apply takes the value as it is when it starts. **Reset** and a finished Apply return it to its default.
    note: It is a level you choose, and nothing is measured from the image. Used alone, with **Stretch Factor** at 0, it only cuts and rescales, and the result is still recorded as nonlinear.
  - name: Human-weighted luminance
    kind: check
    default: "On"
    effect: Chooses the luminance that sets the common factor of an RGB image. On weighs red, green and blue by 0.2126, 0.7152 and 0.0722. Off takes the plain mean of the three.
    enabled: Disabled while Apply runs. It changes nothing on a grayscale image.
    tradeoff: The weights are applied to the samples as they are stored and mean a luminance only for linear RGB. With it on, green structures drive the stretch more. The plain mean is not a perceptual luminance.
    scope: The preview and Apply, on images and sequences of exactly three channels. A mask is blended afterwards.
    persistence: Until the window closes. Each preview and each Apply takes the choice as it is when it starts. **Reset** and a finished Apply turn it on.
  - name: Target Image
    kind: combo
    default: The active image
    options: [The active image, Every other open image or sequence the window can work on]
    effect: Chooses the image Apply replaces, with the mask on its view. The histogram and the preview follow it.
    range: The number of channels of the image is not checked here.
    tradeoff: Apply replaces the image chosen here, so check it before you Apply.
    persistence: Until the window closes. **Reset** leaves it as it is.
  - name: Preview
    kind: check
    default: "On"
    effect: Shows the stretched image beside the controls, with the mask blended in. Off removes the preview, and the histogram returns to the image as it is.
    enabled: It stays enabled while Apply runs.
    tradeoff: Each change stretches the whole image again, or the **Preview area** alone, and counts the histogram again. On a large image or sequence, turn it off or use a **Preview area** while you adjust, and check the whole image before Apply.
    scope: The window only. A **Preview area** shows the result inside the area over the original, and does not limit Apply.
    persistence: Until the window closes. **Reset** leaves it as it is.
    note: An image made with the preview's snapshot button is a separate image, and is not recorded as nonlinear.
  - name: Reset
    kind: button
    default: ""
    effect: Returns **Stretch Factor** and **Black Point** to 0 and turns **Human-weighted luminance** on.
    range: It leaves the target, **Preview**, the toolbar's choices and the zoom of the histogram as they are.
    enabled: Disabled while Apply runs.
    scope: The window only. The image is not touched, the preview is computed once more, and Apply is disabled afterwards.
  - name: Apply
    kind: button
    default: ""
    effect: Stretches the image, or every frame of the sequence, with the three values as they are when you press it, and then returns the controls to their defaults.
    enabled: When **Stretch Factor** or **Black Point** is above 0. It is disabled while the window opens and while a run works.
    scope: The whole image or sequence, with the mask and its inversion as they are when you press it, blended afterwards.
    persistence: The image and its history keep the result. The history step is `ArcSinhStretch` and reads, for example, `Stretch 50, Black Point 0.001, Human Weight true`. It does not record the toolbar's choices or the mask.
    note: Closing the window instead discards the values. The window has no presets, and nothing of it is saved between sessions.
```

```controls
window: Sequence progress
controls:
  - name: Progress
    kind: readout
    default: ""
    effect: Shows the preparation and the frame being stretched, from 0 to 100 percent.
    enabled: Shown only while Apply works on a sequence. A single image has no progress window.
    tradeoff: The sequence is written again in a temporary place, which needs the disk space of a second copy until the run ends.
    scope: Every frame, with the values and the mask taken when you pressed Apply. The frames are read with the automatic debayer setting of the view.
  - name: Cancel
    kind: button
    default: ""
    effect: Asks the run to stop. The temporary sequence is removed and the original stays as it was.
    note: The sequence is replaced only when every frame has been stretched. After a cancelled run, Apply again and let it finish.
```

## How it works

1. Every sample is read as a fraction of full scale: an unsigned integer divided by its largest value, a signed
   one counted from its smallest value, a floating-point sample as it is.
2. A black point above 0 is taken from every channel, and the rest is rescaled so that 1 stays 1. Every sample is
   then held between 0 and 1.
3. A grayscale image goes through the curve. For an RGB image the curve is applied to a luminance of the three
   channels, the channels are multiplied by the ratio of the stretched luminance to the original one, and a
   channel that passes 1 is cut to 1.
4. The result is written back in the image's own pixel type. A mask on the view then blends it with the original.
5. The image replaces the target and its history gets one step. Its header, its acquisition data, its plate
   solution and its color calibration record are kept. A saved file records the new state in its header: `ANLIN`
   is `nonlinear`, `ANASRC` is `process`, `ANOP` is `arcsinh-stretch`, and `ANBASIS` is `monochrome` or
   `display-rgb`. A linear encoding the image stated is no longer stated, and its lineage gains an entry with the
   three values.

### Model

A sample \(x\) is first moved off the black point \(B\) of **Black Point**:

\[ x' = \min\Bigl(1,\ \max\Bigl(0,\ \frac{x - B}{1 - B}\Bigr)\Bigr) \tag{1} \]

With \(S\) the value of **Stretch Factor**, the curve is

\[ f(x') = \frac{\operatorname{asinh}(S\,x')}{\operatorname{asinh}(S)} \tag{2} \]

and \(f(x') = x'\) when \(S = 0\). Equation (2) keeps 0 at 0 and 1 at 1, rises everywhere and bends downward, so it
lifts low and middle values more than values near white.

For an RGB pixel, with \(r'\), \(g'\) and \(b'\) the channels after equation (1) and \(w_R\), \(w_G\) and \(w_B\)
the weights **Human-weighted luminance** chooses, the luminance \(L\) and the common factor \(k\) are

\[ L = w_R\,r' + w_G\,g' + w_B\,b', \qquad k = \frac{f(L)}{L}, \qquad (r, g, b)_{\mathrm{out}} = \min\bigl(1,\ k\,(r', g', b')\bigr) \tag{3} \]

One factor keeps the ratios between \(r'\), \(g'\) and \(b'\). It does not keep the ratios the pixel had before
the black point, and it does not keep them in a pixel where a channel reaches 1. A pixel whose luminance is at
most \(10^{-12}\) is written black.

### Boundaries

- A floating-point sample above 1 is treated as 1, and one below 0 as 0, before the curve. Nothing brings a color
  that leaves the range back into it: each channel is cut alone.
- A pixel that holds a value that is not a finite number is left as it was, and so is a pixel the image marks as
  invalid.
- Only an image of exactly three channels is stretched through a luminance. An image of two or four channels is
  stretched channel by channel, a fourth channel gets the black point but not the curve, and an image of more than
  four channels is refused with a message.
- Floating-point samples are worked in their own precision, 8-bit and 16-bit integers in a single-precision copy
  and 32-bit integers in a double-precision copy. The curve and the luminance are computed in double precision,
  and an integer result is rounded to the nearest value, a tie to the even one.
- A mosaic image is stretched as grayscale and recorded as nonlinear and `monochrome`. A mosaic sequence whose
  automatic debayer is on is read as RGB and replaced as RGB.

### Worked example

The master in the picture above is an RGB image whose three channels have their median near 0.028. With
**Stretch Factor** at 200 and **Black Point** at 0.02, equation (1) places the median at \(x' = 0.0078\) and
equation (2) lifts it to 0.21: the background becomes a dark gray and the nebula spreads over the range above it.
Without the black point the same factor puts the median at 0.40, a washed-out gray.

Equation (3) shows where colors are lost. A pixel of luminance 0.29 is multiplied by 2.7, so any of its channels
above 0.37 becomes 1.

```figure
images:
  - image: ../../assets/images/processes/stretching/arcsinh-stretch-before.webp
    alt: The whole frame before the stretch, black except for a few of the brightest stars.
    label: Before
  - image: ../../assets/images/processes/stretching/arcsinh-stretch-after.webp
    alt: The whole frame after the stretch, the red clouds of the nebula around a blue and violet core, in strong colors, across a field of stars on a dark gray background.
    label: After
caption: >-
  The Carina Nebula master before and after Apply with Stretch Factor at 200 and Black Point at 0.02. Both are the
  whole frame as the view shows it in the Linear display mode, without a display stretch.
```

On a grayscale image, with **Stretch Factor** at 10 and no black point, a sample of 0.01 becomes 0.033, 0.10
becomes 0.294, 0.50 becomes 0.771 and 0.90 becomes 0.965. The lowest one is multiplied by 3.3 while the highest
hardly moves. With **Black Point** at 0.05, a sample of 0.10 first becomes
0.0526 and then 0.168, and every sample at or below 0.05 becomes black. With **Stretch Factor** at 0 it stays at
0.0526.

## Starting points

| Situation | Start | Then adjust |
| --- | --- | --- |
| A linear RGB master | **Black Point** at 0, **Human-weighted luminance** on, and **Stretch Factor** up from 1 | **Black Point** up to the foot of the background, then the factor again. Watch the background, the star cores and the channels that clip. |
| A background level you measured | **Black Point** at that level | **Stretch Factor** again. Check the faint signal and the colors near black. |
| Choosing the luminance | Both numbers fixed, then **Human-weighted luminance** on and off | Compare strongly colored regions. Green structures weigh more with it on. |
| A stretch through a mask | The values first, with the mask off | Enable the mask and check the preview. The common factor is computed before the blend, and gray mask values weaken the stretch. |

## When it goes wrong

!!! warning "Apply cannot be taken back from this window"
    The stretch replaces the image and records it as nonlinear, and a later calibration treats it so. Finish color
    calibration and every other linear step first, and use the image's history to return to the linear master.

!!! warning "Samples above 1 are cut to 1"
    The stretch works between 0 and 1. In a floating-point master, star cores above 1 are cut to 1 before the
    curve, and those above 1 in every channel come out white.

Apply is disabled
:   **Cause.** **Stretch Factor** and **Black Point** are both 0, which changes nothing.
:   **Fix.** Raise either of them.

The background is a washed-out gray
:   **Cause.** **Black Point** is below the background, so the curve lifts the background with the signal.
:   **Fix.** Raise **Black Point** until the background is dark and **Clipping Low** still reads near zero.

Faint structure disappears into the background
:   **Cause.** **Black Point** is above that signal.
:   **Fix.** Lower it toward 0, and use a mask when only part of the image needs a darker background.

Bright colored stars turn white or change hue
:   **Cause.** A channel reached 1 and was cut, or the black point changed the ratios between the channels.
:   **Fix.** Lower **Stretch Factor** or **Black Point**, and check the stars at 100 %.

Human-weighted luminance changes nothing
:   **Cause.** The image is grayscale, does not have exactly three channels, or has little color where you look.
:   **Fix.** Confirm that the target is RGB, and compare strongly colored regions with the same two numbers.

The result of Apply differs from the preview
:   **Cause.** The target, its mask or the frame the sequence shows changed, or a **Preview area** showed the
    result in one area only.
:   **Fix.** Confirm the target and the mask, wait for the last preview, and compare with **Preview area** off.

A few pixels are unchanged after Apply
:   **Cause.** They hold a value that is not a finite number, or the image marks them as invalid.
:   **Fix.** Repair or replace those pixels before the stretch if they must be stretched.

A mosaic sequence is RGB after Apply
:   **Cause.** The automatic debayer of the sequence was on, so its frames were read as RGB before the stretch.
:   **Fix.** Turn the automatic debayer off only when the frames must stay one-channel.

## Related

- [Spectrophotometric Color Calibration](../color-calibration/spectrophotometric-cc.md), which needs the image
  linear and comes before.
- [Histogram Transformation](histogram-transformation.md), a stretch through a black point, a white point and a
  midtones balance, by hand or from the image's statistics.
- [Generalized Hyperbolic Transformation](gh-transformation.md), a stretch with more curve families, channel and
  color-component modes and an inverse.
- [Curves Transformation](curves-transformation.md), to shape tone and color after the stretch.
- [Histogram Analysis](../../interface/image-histogram.md) and
  [Image Statistics](../../interface/image-statistics.md), to check black and white clipping before you accept it.
- [Process Windows](../../interface/process-windows.md), for the preview, the histogram toolbar and what every
  process window shares.

## Credits

- [W3C WCAG 2.2: relative luminance](https://www.w3.org/TR/WCAG22/#dfn-relative-luminance) gives the coefficients
  0.2126, 0.7152 and 0.0722 for linear RGB that **Human-weighted luminance** uses.
