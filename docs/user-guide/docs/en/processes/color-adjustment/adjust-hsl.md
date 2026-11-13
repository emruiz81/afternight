---
kind: process
stage: finish
menu: Process > Adjustments > Hue/Saturation/Lightness
summary: >-
  Turns the hue, scales the saturation and moves the lightness of every pixel of an RGB image, in the HSL model of its
  stored values, for a last color adjustment after the stretch.
data_in: nonlinear
data_out: nonlinear
destructive: true
preview: real-time
sequences: true
masks: post-blend
external: none
glance:
  enabled: >-
    A color image or sequence view is active: red, green and blue, with or without alpha. The action is disabled for
    a grayscale image. A second window of this process on the image, or one beside a window that needs the image for
    itself, is refused with a warning.
  inputs: >-
    One RGB image or sequence, of any pixel type, and an optional mask on its view. The whole image is adjusted: the
    window has no Channel box.
  outputs: >-
    The same view, replaced, with one history step. Its size, its channels and its pixel type are kept. No file,
    preset or report is written.
  state: >-
    A stretched RGB image after color calibration, whose colors need a last adjustment. A linear image is accepted
    too, and its result is recorded as nonlinear all the same.
derived: false
---
# Adjust HSL

This window changes the colors of an RGB image in the [HSL](../../reference/glossary.md#hsl) model of its stored
values. **Hue** turns every color around the color wheel, **Saturation** makes the colors stronger or weaker, and
**Lightness** moves every pixel toward white or black. A gray pixel has no hue and no saturation, so only
**Lightness** changes it. The window opens under the title **Adjust Hue/Saturation/Lightness**.

**Apply** replaces the image or [sequence](../../reference/glossary.md#sequence) with the adjusted one, keeps its size
and [pixel type](../../reference/glossary.md#pixel-type), and adds one step to its
[history](../../reference/glossary.md#history). The result is recorded as nonlinear.

```figure
image: ../../assets/images/processes/color-adjustment/adjust-hsl.webp
alt: >-
  The Adjust Hue/Saturation/Lightness window. On the left, a histogram with three colored peaks about a quarter of
  the way across and the lines Clipping Low and Clipping High in its corner. Under it a toolbar, then the Hue,
  Lightness and Saturation boxes with their sliders, Saturation at 25 and the other two at 0, Target Image and
  Preview. On the right, the preview shows a red nebula in strong colors on a dark gray background. Reset, Close and
  Apply run along the bottom.
caption: >-
  Adjust HSL on a stretched master with Saturation at 25. The histogram is counted from the adjusted preview, and the
  preview shows what Apply writes.
pins:
  - {n: 2, x: 6.6, y: 54.0}
  - {n: 3, x: 44.7, y: 64.1}
  - {n: 5, x: 33.0, y: 20.0}
  - {n: 6, x: 44.8, y: 18.2}
  - {n: 7, x: 91.0, y: 96.0}
```

## Before you start

- Finish color calibration and the main [stretch](../../reference/glossary.md#stretch) first. This window calibrates
  nothing, and after **Apply** the image is recorded as nonlinear.
- **Apply** replaces the image or sequence. Duplicate it first, or use its history, which keeps the step before.
- Use an image of red, green and blue with samples between 0 and 1. Every adjusted pixel is cut to that range, so
  floating-point values above 1 or below 0 do not survive. An image with an alpha channel opens the window but is
  refused.
- The hue, saturation and lightness are those of the values as they are stored. No sRGB curve is decoded and no color
  profile is read.
- A [mask](../../reference/glossary.md#mask) on the view blends the result with the original after the adjustment.
- On a sequence, every frame gets the same three values and must be RGB. For a mosaic sequence, its automatic
  [debayer](../../reference/glossary.md#debayer) decides whether the frames are read as RGB.

## Steps

1. **Open the window.** Make the stretched RGB image the [active image](../../reference/glossary.md#active-image) and
   choose **Process > Adjustments > Hue/Saturation/Lightness**. The preview shows the image unchanged, and **Apply**
   stays disabled while the three values are 0.
2. **Turn the hue, if you must.** Move **Hue** only to correct colors that lean the wrong way, or to remap them on
   purpose. Every color turns by the same angle, and gray areas do not change.
3. **Set the saturation.** Raise **Saturation** a few steps at a time until the colors are as strong as you want
   them, or lower it to mute them. The colors in the preview grow stronger, and so does the color noise of the
   background.
4. **Adjust the lightness last.** Move **Lightness** a few steps at most, as a finishing touch. The peaks of the
   histogram move right or left together.
5. **Read the histogram.** Check **Clipping Low** and **Clipping High** in its corner. A share that grows means
   channels of the strongest colors were cut at 0 or 1.
6. **Check the preview at 100 %.** Zoom the preview into the background, the colored stars and the brightest nebula,
   and press ++space++ over it to compare with the original. Look for color noise in the background, for stars cut
   to a flat color and, with a mask, for its edges.
7. **Apply.** Choose **Apply**. The image in its view is the adjusted one, its history lists
   **Adjust Hue/Saturation/Lightness** with the three values, and the three values return to 0.

## Controls

The histogram with its toolbar, the preview pane and the progress window of a sequence are the same in every process
window: see [Process Windows](../../interface/process-windows.md).

```controls
window: Adjust Hue/Saturation/Lightness
controls:
  - name: Hue
    kind: spinbox
    default: "0"
    widget: {min: -180, max: 180, step: 1}
    effect: Turns every color around the color wheel by this many degrees. Positive turns red toward yellow and green, negative turns red toward magenta and blue. Gray pixels have no hue and do not change.
    range: The turn wraps around the circle, so 180 and -180 give the same colors. The slider beside the box moves in whole degrees over the same range.
    enabled: Disabled while Apply runs.
    tradeoff: A large turn moves every color, of the stars and of the nebula alike, to an unrelated hue. It does not remove a cast from a tinted background, which keeps its saturation.
    scope: The preview, snapshots and Apply, on every pixel of images and sequences. A mask is blended afterwards.
    persistence: Until the window closes. Each preview and each Apply takes the value as it is when it starts. **Reset** and a finished Apply return it to 0.
  - name: Lightness
    kind: spinbox
    default: "0"
    widget: {min: -100, max: 100, step: 1}
    effect: Moves the HSL lightness of every pixel by this many hundredths of the range. Higher moves the pixels toward white, lower toward black, and at either end of the range every pixel is white or black.
    range: The lightness is held between 0 and 1 after the move. The slider beside the box moves in whole steps over the same range.
    enabled: Disabled while Apply runs.
    tradeoff: HSL lightness is the mean of a pixel's largest and smallest channel, not a perceived lightness, so pixels of equal lightness can look very different across hues. What reaches 0 or 1 loses its detail for good. It is no stretch and no change of exposure.
    scope: The preview, snapshots and Apply, on every pixel of images and sequences. A mask is blended afterwards.
    persistence: Until the window closes. Each preview and each Apply takes the value as it is when it starts. **Reset** and a finished Apply return it to 0.
  - name: Saturation
    kind: spinbox
    default: "0"
    widget: {min: -100, max: 100, step: 1}
    effect: Multiplies the saturation of every pixel. Higher makes the colors stronger, lower makes them weaker, and -100 turns the image gray. Gray pixels stay gray.
    range: The two sides are not alike. A negative value multiplies by 1 plus a hundredth of it, so -50 halves the saturation. A positive value multiplies by 1 plus a twenty-fifth of it, so 25 doubles it and 100 multiplies it by 5. The saturation stops at 1.
    enabled: Disabled while Apply runs.
    tradeoff: The positive side is four times steeper than a percentage, and 5 already multiplies by 1.2. A strong value cuts the most colorful pixels at full saturation, and raises color noise and colored halos with the colors.
    scope: The preview, snapshots and Apply, on every pixel of images and sequences. A mask is blended afterwards.
    persistence: Until the window closes. Each preview and each Apply takes the value as it is when it starts. **Reset** and a finished Apply return it to 0.
  - name: Target Image
    kind: combo
    default: The active image
    options: [The active image, Every other open color image or sequence]
    effect: Chooses the image Apply replaces, with the mask on its view. The histogram and the preview follow it.
    range: It lists color images and sequences only.
    tradeoff: Apply replaces the image chosen here, so check it before you Apply.
    persistence: Until the window closes. **Reset** leaves it as it is.
    note: When the target becomes a grayscale image, the window closes without a message.
  - name: Preview
    kind: check
    default: "On"
    effect: Shows the adjusted image beside the controls, with the mask blended in. Off removes the preview, and the histogram returns to the image as it is.
    tradeoff: Each change adjusts the image again and counts the histogram again, on a copy of the image or of the **Preview area**. An integer image adds a floating-point working copy. With the three values at 0 nothing is computed, and the preview shows the image itself.
    scope: The window only. On a sequence it shows the frame the view displays. Inside a **Preview area** the result is what Apply makes of that part of the image.
    persistence: Until the window closes. **Reset** leaves it as it is.
    note: The pane shows the samples as they are, without a display stretch, so compare it with the view in its Linear display mode. While you drag a slider, the preview is computed on the image reduced to the pane's size, or on the part in view at 100 % and closer, and on the whole image when you let go. A snapshot is a new image named as `M42 - Adjust Hue/Saturation/Lightness - SNAPSHOT`. It keeps the target's metadata and is not recorded as nonlinear.
  - name: Reset
    kind: button
    default: ""
    effect: Returns **Hue**, **Lightness** and **Saturation** to 0.
    range: It leaves the target and **Preview** as they are.
    enabled: Disabled while Apply runs.
    scope: The window only. The image is not touched, and Apply is disabled afterwards.
  - name: Apply
    kind: button
    default: ""
    effect: Adjusts the image, or every frame of the sequence, with the three values as they are when you press it, and then returns them to 0.
    enabled: When any of the three values is not 0, so it is disabled when the window opens. While a run works it is disabled too, as the three values, **Reset** and **Close** are.
    tradeoff: One pass over the pixels on the threads the run is given, and a second copy of the image in memory. An integer image adds a floating-point working copy, and a mask adds one more pass. A sequence is decoded, adjusted and written again in a temporary place.
    scope: The whole image or sequence, every pixel, with the mask and its inversion blended afterwards. Every frame of a sequence must be RGB.
    persistence: >-
      The image and its history keep the result. The history step is **Adjust Hue/Saturation/Lightness** and reads,
      for example, `Adjust hue to 30 saturation to 40 and lightness to -10`. It does not record the mask, or whether
      the automatic debayer of a sequence made its frames RGB. Undoing the step restores the image before.
    note: Closing the window instead discards the values. The window has no presets, and nothing of it is saved between sessions.
```

## How it works

1. Every sample is read as a [fraction of full scale](../../reference/glossary.md#fraction-of-full-scale): an
   unsigned integer divided by its largest value, a signed one counted from its smallest value, a floating-point
   sample as it is.
2. The red, green and blue of each pixel, as they are stored, give its hue, saturation and lightness.
3. The three values turn the hue, scale the saturation and move the lightness. The pixel is rebuilt as red, green
   and blue, each channel is cut to the range 0 to 1, and the result is written in the image's own pixel type.
4. A mask on the view then blends the result with the original.
5. The image replaces the target and its history gets one step. Its header, its
   [plate solution](../../reference/glossary.md#plate-solution) and its acquisition data are kept. A saved file
   records the new state in its header: `ANLIN` is `nonlinear`, `ANASRC` is `process`, `ANOP` is `hsl-adjustment`,
   and `ANBASIS` is `display-rgb`, or `monochrome` where the image stated it. A linear encoding the image stated is
   no longer stated, and its [lineage](../../reference/glossary.md#lineage) gains an entry with the three values.

### Model

For a pixel whose channels \(r\), \(g\) and \(b\) lie between 0 and 1, let \(v\) be the largest of the three and
\(m\) the smallest. Its lightness \(l\) and its saturation \(s\) are

\[ l = \frac{v + m}{2}, \qquad s = \frac{v - m}{v + m} \ \text{for}\ l \le 0.5, \qquad s = \frac{v - m}{2 - v - m} \ \text{for}\ l > 0.5 \tag{1} \]

and \(s = 0\) when \(v = m\). Its hue \(h\) is a fraction of a turn: 0 for red, 1/3 for green and 2/3 for blue.
With \(S\) the value of **Saturation**, the saturation is multiplied by

\[ k = 1 + \frac{S}{25} \ \text{for}\ S > 0, \qquad k = 1 + \frac{S}{100} \ \text{for}\ S \le 0 \tag{2} \]

and, with \(H\) the value of **Hue** and \(L\) that of **Lightness**, the three components become

\[ h' = \Bigl(h + \frac{H}{360}\Bigr) \bmod 1, \qquad s' = \min(1,\ k\,s), \qquad l' = \min\Bigl(1,\ \max\Bigl(0,\ l + \frac{L}{100}\Bigr)\Bigr) \tag{3} \]

The pixel is rebuilt from \(h'\), \(s'\) and \(l'\). A gray pixel, whose \(s\) is 0, stays gray, and only \(L\)
moves it. With **Saturation** alone, every channel \(c\) moves away from the lightness by the factor \(k\):

\[ c' = l + k\,(c - l) \tag{4} \]

as long as \(k\,s\) is at most 1. Past that the saturation stops at 1: the lowest channel reaches 0, or the highest
reaches 1 in a pixel brighter than one half. HSL is not perceptually uniform: equal steps of hue or of lightness do
not look equal.

### Boundaries

- A pixel that holds a value that is not a finite number is left as it was, and so is a pixel the image marks as
  invalid.
- The image must have exactly red, green and blue. A grayscale frame of a sequence, or an image with an alpha
  channel, is refused with "the HSL adjustment needs an image of red, green and blue", and nothing changes.
- Integer samples are adjusted in a 32-bit floating-point copy, or a 64-bit one for 32-bit integers, and rounded
  back to the nearest value, a tie to the even one. Floating-point samples are adjusted where they are. Each pixel
  is converted and adjusted in double precision.

### Worked example

The image in the picture above is the linear [master](../../reference/glossary.md#master) after Histogram
Transformation with **Shadows** and **Midtones** at 0.02, which leaves the background near 0.28 in each channel. With
**Saturation** at 25, equation (2) gives \(k = 2\). The gray background keeps its level, but the small color
differences of its noise double. A nebula pixel of (0.40, 0.30, 0.26) has \(l = 0.33\) and \(s = 0.21\). By
equation (4) it becomes (0.47, 0.27, 0.19), with twice the saturation and the same lightness.

```figure
images:
  - image: ../../assets/images/processes/color-adjustment/adjust-hsl-before.webp
    alt: The whole frame before the adjustment, the red and white clouds of the nebula in muted colors across a field of stars on a dark gray background.
    label: Before
  - image: ../../assets/images/processes/color-adjustment/adjust-hsl-after.webp
    alt: The whole frame after the adjustment, the same nebula in stronger reds and blues, the stars more colored, on a dark gray background with a little more color in its noise.
    label: After
caption: >-
  The stretched Carina Nebula master before and after Apply with Saturation at 25. Both are the whole frame as the
  view shows it in the Linear display mode, without a display stretch.
```

Pure red, (1, 0, 0), has \(h = 0\), \(s = 1\) and \(l = 0.5\). **Hue** at 120 turns it into pure green, (0, 1, 0).
**Saturation** at -50 halves its saturation and gives (0.75, 0.25, 0.25). **Lightness** at 25 raises its lightness
to 0.75 and gives (1, 0.5, 0.5).

## Starting points

| Situation | Start | Then adjust |
| --- | --- | --- |
| Colors too weak after the stretch | **Saturation** from 5 to 10 | Raise it slowly. 10 already multiplies by 1.4. Watch the background noise, the colored stars and the clipping shares. |
| Colors that lean toward the wrong hue | **Hue** from -5 to 5 | Watch the stars and the nebula: a larger turn moves them into unrelated hues. A tinted background needs color calibration instead. |
| A small change of tone at the end | **Lightness** from -5 to 5 | Prefer the main stretch for tone. Watch the shadows and the whites for clipping. |
| A change through a mask, or tried on one region | The mask checked first, or a **Preview area** over gray areas, stars and strong colors | Weaken the change if the mask's edges show at 100 %. Check the whole preview before Apply. |

## When it goes wrong

!!! warning "Apply records the image as nonlinear"
    Every Apply marks the result nonlinear, with or without a mask, and a later step treats it so. Finish the steps
    that need linear data first, or undo the step in the image's history.

The menu item is disabled, or the window closed by itself
:   **Cause.** The image is grayscale, or the target became grayscale while the window was open.
:   **Fix.** Use an RGB image. Convert or debayer a grayscale one only when it is meant to become RGB.

Apply is disabled
:   **Cause.** **Hue**, **Lightness** and **Saturation** are all 0, which changes nothing.
:   **Fix.** Set any of them to another value.

A small Saturation looks far too strong
:   **Cause.** The positive side multiplies by 1 plus a twenty-fifth of the value.
:   **Fix.** Use smaller positive values. 5 already multiplies by 1.2.

Gray areas do not change with Hue or Saturation
:   **Cause.** A gray pixel has no hue and no saturation for the window to turn or scale.
:   **Fix.** Leave them as they are: only **Lightness** moves a gray pixel.

The preview or Apply reports "the HSL adjustment needs an image of red, green and blue"
:   **Cause.** The target, or a frame of the sequence, is not three-channel RGB: it is grayscale, or color with an
    alpha channel.
:   **Fix.** Use an image of red, green and blue only, or turn on the automatic debayer of a mosaic sequence. The
    target was not changed.

Strong colors or bright areas lose their detail
:   **Cause.** The saturation stops at 1, the lightness at 0 and 1, and every channel at 0 and 1.
:   **Fix.** Undo the step, and use smaller values of **Saturation** and **Lightness**.

A few pixels are unchanged after Apply
:   **Cause.** They hold a value that is not a finite number, or the image marks them as invalid.
:   **Fix.** Repair or replace those pixels first if they must be adjusted.

## Related

- [Photometric Color Calibration](../color-calibration/photometric-cc.md) and
  [Spectrophotometric Color Calibration](../color-calibration/spectrophotometric-cc.md), which need the image linear
  and come before.
- [Adjust LCh](adjust-lch.md), the same kind of change in the CIE lightness, chroma and hue.
- [Color Saturation](color-saturation.md), to raise the saturation of a range of hues only.
- [Histogram Transformation](../stretching/histogram-transformation.md),
  [Generalized Hyperbolic Transformation](../stretching/gh-transformation.md) and
  [Exposure Correction](../stretching/exposure-correction.md), for tone, which HSL lightness does not replace.
- [Image History](../../interface/image-history.md) and [Image Statistics](../../interface/image-statistics.md), to
  undo the step and to check the range of the samples.
- [Process Windows](../../interface/process-windows.md), for the preview, the histogram and what every process window
  shares.

## Credits

- [W3C CSS Color 4: HSL colors](https://www.w3.org/TR/css-color-4/#the-hsl-notation) describes the HSL model, its
  circular hue, the conversion from RGB and its perceptual limits.
