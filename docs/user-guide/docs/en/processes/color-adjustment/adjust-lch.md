---
kind: process
stage: finish
menu: Process > Adjustments > Lightness/Chroma/Hue
summary: >-
  Moves the CIE lightness, scales the chroma and turns the hue of every pixel of an RGB image read as sRGB, for a last
  color adjustment after the stretch.
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
rewritten: true
---
# Adjust LCh

This window changes the colors of an RGB image in the lightness, chroma and hue of
[CIE Lab](../../reference/glossary.md#cie-lab). **Lightness** moves every pixel toward white or black, **Chroma**
makes the colors stronger or weaker at the same lightness, and **Hue** turns every color around the circle. The red,
green and blue are read as sRGB, and the result is cut to what RGB can hold. The window opens under the title
**Adjust Lightness/Chroma/Hue**.

**Apply** replaces the image or [sequence](../../reference/glossary.md#sequence) with the adjusted one, keeps its size
and [pixel type](../../reference/glossary.md#pixel-type), and adds one step to its
[history](../../reference/glossary.md#history). The result is recorded as nonlinear.

```figure
image: ../../assets/images/processes/color-adjustment/adjust-lch.webp
alt: >-
  The Adjust Lightness/Chroma/Hue window. On the left, a histogram with three colored peaks about a quarter of the
  way across and the lines Clipping Low and Clipping High in its corner. Under it a toolbar, then the Hue, Chroma and
  Lightness boxes with their sliders, Chroma at 25 and the other two at 0, Target Image and Preview. On the right,
  the preview shows a red nebula in strong colors on a dark gray background. Reset, Close and Apply run along the
  bottom.
caption: >-
  Adjust LCh on a stretched master with Chroma at 25. The histogram is counted from the adjusted preview, and the
  preview shows what Apply writes.
pins:
  - {n: 2, x: 6.5, y: 54.0}
  - {n: 3, x: 44.7, y: 59.1}
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
- The values are read as sRGB with a D65 white, or as linear when the image is described as linear RGB. No color
  profile is read, so the result is only as right as that reading.
- A [mask](../../reference/glossary.md#mask) on the view blends the result with the original after the adjustment.
- On a sequence, every frame gets the same three values and must be RGB. For a mosaic sequence, its automatic
  [debayer](../../reference/glossary.md#debayer) decides whether the frames are read as RGB.

## Steps

1. **Open the window.** Make the stretched RGB image the [active image](../../reference/glossary.md#active-image) and
   choose **Process > Adjustments > Lightness/Chroma/Hue**. The preview shows the image unchanged, and **Apply** stays
   disabled while the three values are 0.
2. **Turn the hue, if you must.** Move **Hue** a few degrees to correct colors that lean the wrong way. Every color
   turns by the same angle, and gray areas hardly change.
3. **Set the chroma.** Raise **Chroma** a few steps at a time until the colors are strong enough, or lower it to mute
   them. The colors in the preview grow stronger at the same brightness, and so does the color noise of the
   background.
4. **Adjust the lightness last.** Move **Lightness** a few units at most, as a finishing touch. The peaks of the
   histogram move right or left together.
5. **Read the histogram.** Check **Clipping Low** and **Clipping High** in its corner. A share that grows means
   colors left what RGB can hold and were cut at 0 or 1.
6. **Check the preview at 100 %.** Zoom the preview into the background, the colored stars and the brightest nebula,
   and press ++space++ over it to compare with the original. Look for color noise in the background, for stars cut to
   a flat color and, with a mask, for its edges.
7. **Apply.** Choose **Apply**. The image in its view is the adjusted one, its history lists
   **Adjust Lightness/Chroma/Hue** with the three values, and the three values return to 0.

## Controls

The histogram with its toolbar, the preview pane and the progress window of a sequence are the same in every process
window: see [Process Windows](../../interface/process-windows.md).

```controls
window: Adjust Lightness/Chroma/Hue
controls:
  - name: Hue
    kind: spinbox
    default: "0"
    widget: {min: -180, max: 180, step: 1}
    effect: Turns every color around the CIE hue circle by this many degrees. Positive turns red toward yellow and green, negative turns red toward magenta and blue. A gray pixel has almost no chroma, and a turn hardly changes it.
    range: The turned hue wraps around the circle, so 180 and -180 give the same colors. The slider beside the box moves in whole degrees over the same range.
    enabled: Disabled while Apply runs.
    tradeoff: A large turn moves every color, of the stars and of the nebula alike, to an unrelated hue. Near gray the hue is set by noise, so a turn there moves the colors of the noise.
    scope: The preview, snapshots and Apply, on every pixel of images and sequences. A mask is blended afterwards.
    persistence: Until the window closes. Each preview and each Apply takes the value as it is when it starts. **Reset** and a finished Apply return it to 0.
  - name: Chroma
    kind: spinbox
    default: "0"
    widget: {min: -100, max: 100, step: 1}
    effect: Multiplies the chroma of every pixel, its distance from gray at the same lightness. Higher makes the colors stronger, lower makes them weaker, and -100 turns the image gray at its own lightness.
    range: The two sides are not alike. A negative value multiplies by 1 plus a hundredth of it, so -50 halves the chroma. A positive value multiplies by 1 plus a twenty-fifth of it, so 25 doubles it and 100 multiplies it by 5. The chroma stops at 200.
    enabled: Disabled while Apply runs.
    tradeoff: The positive side is four times steeper than a percentage, and 5 already multiplies by 1.2. Strong colors soon leave what RGB can hold, and each channel is then cut alone, which changes their lightness and hue. Color noise and colored halos grow with the colors.
    scope: The preview, snapshots and Apply, on every pixel of images and sequences. A mask is blended afterwards.
    persistence: Until the window closes. Each preview and each Apply takes the value as it is when it starts. **Reset** and a finished Apply return it to 0.
  - name: Lightness
    kind: spinbox
    default: "0"
    widget: {min: -100, max: 100, step: 1}
    effect: Adds this many units of CIE L* to every pixel, on a scale where black is 0 and white is 100. Higher brightens, lower darkens, and the chroma and the hue stay as they were.
    range: The lightness is held between 0 and 100 after the move. The slider beside the box moves in whole steps over the same range.
    enabled: Disabled while Apply runs.
    tradeoff: What reaches 0 or 100 loses its detail for good, and a color made brighter or darker can leave what RGB can hold and be cut. The value is added, not multiplied, so it is no change of exposure and no stretch.
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
    tradeoff: Each change converts and adjusts the image again and counts the histogram again, on a copy of the image or of the **Preview area**. No converted copy is kept between previews. An integer image adds a floating-point working copy. With the three values at 0 nothing is computed, and the preview shows the image itself.
    scope: The window only. On a sequence it shows the frame the view displays. Inside a **Preview area** the result is what Apply makes of that part of the image.
    persistence: Until the window closes. **Reset** leaves it as it is.
    note: The pane shows the samples as they are, without a display stretch, so compare it with the view in its Linear display mode. While you drag a slider, the preview is computed on the image reduced to the pane's size, or on the part in view at 100 % and closer, and on the whole image when you let go. A snapshot is a new image named as `M42 - Adjust Lightness/Chroma/Hue - SNAPSHOT`. It keeps the target's metadata and is not recorded as nonlinear.
  - name: Reset
    kind: button
    default: ""
    effect: Returns **Hue**, **Chroma** and **Lightness** to 0.
    range: It leaves the target and **Preview** as they are.
    enabled: Disabled while Apply runs.
    scope: The window only. The image is not touched, and Apply is disabled afterwards.
  - name: Apply
    kind: button
    default: ""
    effect: Adjusts the image, or every frame of the sequence, with the three values as they are when you press it, and then returns them to 0.
    enabled: When any of the three values is not 0, so it is disabled when the window opens. While a run works it is disabled too, as the three values, **Reset** and **Close** are.
    tradeoff: Two conversions of every pixel on the threads the run is given, and a second copy of the image in memory. An integer image adds a floating-point working copy, and a mask adds one more pass. A sequence is decoded, adjusted and written again in a temporary place.
    scope: The whole image or sequence, every pixel, with the mask and its inversion blended afterwards. Every frame of a sequence must be RGB.
    persistence: >-
      The image and its history keep the result. The history step is **Adjust Lightness/Chroma/Hue** and reads, for
      example, `Adjust lightness to 10 chroma to 30 and hue to -20`. It does not record the mask, how many colors were
      cut, how the values were read, or whether the automatic debayer of a sequence made its frames RGB. Undoing the
      step restores the image before.
    note: Closing the window instead discards the values. The window has no presets, and nothing of it is saved between sessions.
```

## How it works

1. Every sample is read as a [fraction of full scale](../../reference/glossary.md#fraction-of-full-scale): an
   unsigned integer divided by its largest value, a signed one counted from its smallest value, a floating-point
   sample as it is.
2. The red, green and blue of each pixel are decoded from sRGB, unless the image is described as linear RGB, and
   converted to CIE lightness, chroma and hue under the D65 white.
3. The three values move the lightness, scale the chroma and turn the hue. The pixel is converted back the same way,
   each channel is cut to the range 0 to 1, and the result is written in the image's own pixel type.
4. A mask on the view then blends the result with the original.
5. The image replaces the target and its history gets one step. Its header, its
   [plate solution](../../reference/glossary.md#plate-solution) and its acquisition data are kept. A saved file
   records the new state in its header: `ANLIN` is `nonlinear`, `ANASRC` is `process`, `ANOP` is `lch-adjustment`,
   and `ANBASIS` is `display-rgb`, or `monochrome` where the image stated it. A linear encoding the image stated is
   no longer stated, and its [lineage](../../reference/glossary.md#lineage) gains an entry with the three values.

### Model

A stored red, green or blue value \(u\), between 0 and 1, is first decoded from sRGB:

\[ u_{\mathrm{lin}} = \frac{u}{12.92} \ \text{for}\ u \le 0.04045, \qquad u_{\mathrm{lin}} = \Bigl(\frac{u + 0.055}{1.055}\Bigr)^{2.4} \ \text{otherwise} \tag{1} \]

An image described as linear RGB skips this step. The three linear values give the tristimulus values \(X\), \(Y\)
and \(Z\) through the matrix of the sRGB primaries and the D65 white, shown here rounded to seven places:

\[ \begin{pmatrix} X \\ Y \\ Z \end{pmatrix} = \begin{pmatrix} 0.4124564 & 0.3575761 & 0.1804375 \\ 0.2126729 & 0.7151522 & 0.0721750 \\ 0.0193339 & 0.1191920 & 0.9503041 \end{pmatrix} \begin{pmatrix} r_{\mathrm{lin}} \\ g_{\mathrm{lin}} \\ b_{\mathrm{lin}} \end{pmatrix} \tag{2} \]

With the D65 white \((X_n, Y_n, Z_n) = (0.95047, 1, 1.08883)\), or \((95.047, 100, 108.883)\) on a scale of 100,
and \(f(t) = t^{1/3}\) above \((6/29)^3\) and \(t / (3\,(6/29)^2) + 4/29\) below it, the lightness \(L^*\), the
chroma \(C^*\) and the hue \(h\) in degrees from 0 to 360 are

\[ L^* = 116\,f\Bigl(\frac{Y}{Y_n}\Bigr) - 16, \quad a^* = 500\Bigl(f\Bigl(\frac{X}{X_n}\Bigr) - f\Bigl(\frac{Y}{Y_n}\Bigr)\Bigr), \quad b^* = 200\Bigl(f\Bigl(\frac{Y}{Y_n}\Bigr) - f\Bigl(\frac{Z}{Z_n}\Bigr)\Bigr), \quad C^* = \sqrt{a^{*2} + b^{*2}}, \quad h = \operatorname{atan2}(b^*, a^*) \tag{3} \]

With \(c\) the value of **Chroma**, the chroma is multiplied by

\[ k = 1 + \frac{c}{25} \ \text{for}\ c > 0, \qquad k = 1 + \frac{c}{100} \ \text{for}\ c \le 0 \tag{4} \]

and, with \(\Delta L\) the value of **Lightness** and \(\Delta h\) that of **Hue**, the three components become

\[ L' = \min\bigl(100,\ \max(0,\ L^* + \Delta L)\bigr), \qquad C' = \min(200,\ k\,C^*), \qquad h' = (h + \Delta h) \bmod 360 \tag{5} \]

The pixel is converted back by the exact inverse of equations (3), (2) and (1), and each channel is cut to 0 to 1.
That cut is the only gamut handling: a color outside what RGB can hold loses the part outside, channel by channel,
which also moves its lightness and hue. A gray pixel has \(C^*\) near 0, so only \(\Delta L\) moves it much. CIE
L* follows the eye more closely than the lightness of [HSL](../../reference/glossary.md#hsl), but CIE Lab is only
approximately uniform, least for extreme colors.

### Boundaries

- A pixel that holds a value that is not a finite number is left as it was, and so is a pixel the image marks as
  invalid.
- The image must have exactly red, green and blue. A grayscale frame of a sequence, or an image with an alpha
  channel, is refused with "the LCh adjustment needs an image of red, green and blue", and nothing changes.
- Integer samples are adjusted in a 32-bit floating-point copy, or a 64-bit one for 32-bit integers, and rounded
  back to the nearest value, a tie to the even one. Floating-point samples are adjusted where they are.
- A 32-bit floating-point image, and the working copy of an 8-bit or 16-bit one, are converted in single precision,
  within 0.0001 of full scale of the double-precision result. A 64-bit image, a 32-bit integer one, and an image
  whose [validity rule](../../reference/glossary.md#validity-rule) is a mask, a range or exact zero, are converted
  in double precision.

### Worked example

The image in the picture above is the linear [master](../../reference/glossary.md#master) after Histogram
Transformation with **Shadows** and **Midtones** at 0.02, which leaves the background near 0.28 in each channel. The
master states no color space, so its values are read as sRGB. With **Chroma** at 25, equation (4) gives \(k = 2\).
By equations (1) to (3) the gray background has \(L^* = 30.3\) and a chroma near 0: it keeps its level, and only the
color of its noise doubles. A nebula pixel of (0.40, 0.30, 0.26) has \(L^* = 34.9\),
\(C^* = 13.7\) and \(h = 48\) degrees. Its chroma becomes 27.5, and it returns as (0.46, 0.27, 0.20), with the same
lightness and hue.

```figure
images:
  - image: ../../assets/images/processes/color-adjustment/adjust-lch-before.webp
    alt: The whole frame before the adjustment, the red and white clouds of the nebula in muted colors across a field of stars on a dark gray background.
    label: Before
  - image: ../../assets/images/processes/color-adjustment/adjust-lch-after.webp
    alt: The whole frame after the adjustment, the same nebula in stronger reds and blues at the same brightness, the stars more colored, on a dark gray background with a little more color in its noise.
    label: After
caption: >-
  The stretched Carina Nebula master before and after Apply with Chroma at 25. Both are the whole frame as the view
  shows it in the Linear display mode, without a display stretch.
```

A gray of 0.5 has \(L^* = 53.4\) and almost no chroma. **Lightness** at 10 makes it a lighter gray of 0.60, while
**Chroma** at 25 leaves it as it is. A pixel whose hue is 170 degrees turns to 200 with **Hue** at 30, the direction
a signed scale calls -160. Pure red, (1, 0, 0), has \(C^* = 104.6\). Doubled, it lies outside what RGB can hold, and
the cut returns it as pure red: the change is lost.

## Starting points

| Situation | Start | Then adjust |
| --- | --- | --- |
| Colors too weak after the stretch | **Chroma** from 5 to 10 | Raise it slowly. Watch the strongest colors, which are cut first, and the clipping shares. |
| Colors that lean toward the wrong hue | **Hue** from -5 to 5 | Turn it before the other two. A larger turn carries colors far round the circle. Watch the stars and the nebula. |
| A small change of tone at the end | **Lightness** from -3 to 3 | Prefer the main stretch for tone. Watch black and white for clipping. |
| A change through a mask, or tried on one region | The mask checked first, or a **Preview area** over gray areas, stars and strong colors | Weaken the change if the mask's edges show at 100 %. Check the whole preview before Apply. |

## When it goes wrong

!!! warning "Apply records the image as nonlinear"
    Every Apply marks the result nonlinear, with or without a mask, and a later step treats it so. Finish the steps
    that need linear data first, or undo the step in the image's history.

The menu item is disabled, or the window closed by itself
:   **Cause.** The image is grayscale, or the target became grayscale while the window was open.
:   **Fix.** Use an RGB image. Convert or debayer a grayscale one only when it is meant to become RGB.

Apply is disabled
:   **Cause.** **Hue**, **Chroma** and **Lightness** are all 0, which changes nothing.
:   **Fix.** Set any of them to another value.

A small Chroma flattens the strongest colors
:   **Cause.** The positive side multiplies by 1 plus a twenty-fifth of the value, and colors pushed past what RGB
    can hold are cut.
:   **Fix.** Use smaller positive values, and check the strongest colors at 100 %. 5 already multiplies by 1.2.

Nearly gray areas take odd colors after a change of Hue or Chroma
:   **Cause.** Their chroma is near 0, so their hue is set by noise.
:   **Fix.** Use smaller values of **Hue** and **Chroma**, or protect the neutral areas with a mask.

The preview or Apply reports "the LCh adjustment needs an image of red, green and blue"
:   **Cause.** The target, or a frame of the sequence, is not three-channel RGB: it is grayscale, or color with an
    alpha channel.
:   **Fix.** Use an image of red, green and blue only, or turn on the automatic debayer of a mosaic sequence. The
    target was not changed.

A few pixels are unchanged after Apply
:   **Cause.** They hold a value that is not a finite number, or the image marks them as invalid.
:   **Fix.** Repair or replace those pixels first if they must be adjusted.

## Related

- [Photometric Color Calibration](../color-calibration/photometric-cc.md) and
  [Spectrophotometric Color Calibration](../color-calibration/spectrophotometric-cc.md), which need the image linear
  and come before.
- [Adjust HSL](adjust-hsl.md), the same kind of change in the hue, saturation and lightness of the stored values.
- [Color Saturation](color-saturation.md), to raise the saturation of a range of hues only.
- [Histogram Transformation](../stretching/histogram-transformation.md) and
  [Exposure Correction](../stretching/exposure-correction.md), for tone, which CIE L* does not replace.
- [Image History](../../interface/image-history.md) and [Image Statistics](../../interface/image-statistics.md), to
  undo the step and to check the range of the samples.
- [Process Windows](../../interface/process-windows.md), for the preview, the histogram and what every process window
  shares.

## Credits

- The [ICC sRGB characterization](https://www.color.org/sRGB.pdf) defines the sRGB transfer, primaries and D65 white
  the conversion's constants come from.
- [CIE Standard Illuminant D65](https://www.cie.co.at/datatable/cie-standard-illuminant-d65) is the daylight reference
  behind the white of the tristimulus values.
