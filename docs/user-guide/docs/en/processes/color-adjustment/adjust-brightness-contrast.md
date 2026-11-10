---
kind: process
stage: finish
menu: Process > Adjustments > Brightness/Contrast
summary: >-
  Adds one brightness offset to the image, or to one channel or color component of it, then spreads or presses the
  tones about middle gray, for a small correction after the stretch.
data_in: nonlinear
data_out: nonlinear
destructive: true
preview: real-time
sequences: true
masks: post-blend
external: none
glance:
  enabled: >-
    An image or a sequence view is active, no Adjust Brightness And Contrast window is open on it yet, and no window
    that needs the image for itself is open on it.
  inputs: >-
    One grayscale or RGB image or sequence of any pixel type, read as fractions of full scale, and an optional mask of
    the image's size on its view.
  outputs: >-
    The same view, replaced, with one history step. Its size, its channels and its pixel type are kept. No file,
    preset or report is written.
  state: >-
    A stretched image, mono or RGB, whose tones need a small last correction. A linear image is accepted too, and its
    result is recorded as nonlinear all the same.
derived: false
---
# Adjust Brightness/Contrast

This window adds one offset to every sample, **Brightness**, and holds the result between black and white. It then
spreads the samples away from middle gray, or presses them toward it, by **Contrast**, and holds them again.
**Channel** applies the two to the whole image, to one channel, or to one component of a
[color model](../../reference/glossary.md#color-model). It is made for a small correction after the main
[stretch](../../reference/glossary.md#stretch), not for a stretch or a change of exposure.

**Apply** replaces the image or [sequence](../../reference/glossary.md#sequence) with the adjusted one, keeps its size
and [pixel type](../../reference/glossary.md#pixel-type), and adds one step to its
[history](../../reference/glossary.md#history). The result is recorded as nonlinear.

```figure
image: ../../assets/images/processes/color-adjustment/adjust-brightness-contrast.webp
alt: >-
  The Adjust Brightness And Contrast window. On the left, a histogram with one broad peak about a fifth of the way
  across and the lines Clipping Low and Clipping High in its corner. Under it a toolbar, the Brightness box at -10 and the
  Contrast box at 20 with their sliders, Target Image, Channel at RGB and Preview checked. On the right, the preview
  shows a red nebula on a dark background. Reset, Close and Apply run along the bottom.
caption: >-
  Adjust Brightness And Contrast on a stretched master with Brightness at -10 and Contrast at 20. The histogram is
  counted from the darker, more contrasted preview, and the preview shows what Apply writes.
pins:
  - {n: 2, x: 2.0, y: 84.3}
  - {n: 3, x: 2.0, y: 54.1}
  - {n: 4, x: 2.0, y: 59.2}
  - {n: 5, x: 2.0, y: 19.0}
  - {n: 6, x: 73.0, y: 26.0}
  - {n: 7, x: 91.0, y: 96.0}
```

## Before you start

- Stretch the image first, and finish background correction and color calibration before that. The two values
  recover no clipped sample and balance no channel.
- **Apply** replaces the image or sequence. Duplicate it first, or use its history, which keeps the step before.
  What the two values send to black or white is lost for good once you save.
- A component of **Channel** gets the same straight curve as a channel. On a hue or a chroma it does not treat the hue
  as a circle and is not even to the eye. Prefer a process made for color.
- A [mask](../../reference/glossary.md#mask) on the view blends the result with the original afterwards. Check that it
  is enabled, and whether it is inverted, before you Apply.

## Steps

1. **Open the window.** Make the stretched image the active one and choose **Process > Adjustments >
   Brightness/Contrast**. The window opens as **Adjust Brightness And Contrast**, its preview shows the image
   unchanged, and **Apply** stays disabled until a value leaves 0.
2. **Choose what to adjust.** Leave **Channel** at **RGB** to give the three channels the same curve. Choose a single
   color, or a component such as **CIE Y (Luminance)**, only to change one of them on purpose.
3. **Set the brightness first.** Lower **Brightness** to darken the image, or raise it to brighten it. The preview
   follows, and the histogram moves left or right as a whole.
4. **Set the contrast.** Raise **Contrast** to spread the tones away from middle gray, or lower it to press them
   toward it. The histogram widens or narrows about its middle.
5. **Watch the ends.** Read **Clipping Low** and **Clipping High** in the corner of the histogram. Stop when they
   start to rise: what they count has become black or white.
6. **Check the preview at 100 %.** Zoom the preview into the background, the faint nebula and the brightest stars,
   and press ++space++ over it to compare with the original. Look at the edges of the mask too.
7. **Apply.** Choose **Apply**. The image in its view is the adjusted one, its history lists **Adjust Brightness And
   Contrast** with both values, and the controls return to their defaults.

## Controls

The histogram and its toolbar, **Target Image**, the preview pane, **Close** and the progress window of a sequence
work as in every process window, and the choices of **Channel** are described there: see
[Process Windows](../../interface/process-windows.md). The preview pane of this window has no **Display Mode**.

```controls
window: Adjust Brightness And Contrast
controls:
  - name: Clipping Low and Clipping High
    kind: readout
    default: ""
    effect: Give the shares of the samples at or below 0 and at or above 1, over all the channels together. With **Preview** on they count the preview, so they rise as the two values send samples to black and white.
    tradeoff: They count every sample at an end, also those that were there before and those a mask kept. Compare them with **Preview** off to see what the two values add.
    scope: The window only, with the mask blended in.
  - name: Brightness
    kind: spinbox
    default: "0"
    widget: {min: -150, max: 150, step: 1}
    effect: Adds the same amount to every value **Channel** selects. Higher brightens, lower darkens, and 0 adds nothing.
    range: Each unit is a 255th of full scale, one code of an 8-bit image, so the ends of the range add or take about 0.588. The slider beside the box moves the same value one unit at a time.
    enabled: Disabled while Apply runs. Any value but 0 enables **Apply**, as **Contrast** does.
    tradeoff: The result is held between 0 and 1 before **Contrast** works, so what it pushes past an end is lost, and no later **Contrast** brings it back. It also moves the values that sat at middle gray before the contrast pivots there.
    scope: The preview and Apply, on what **Channel** selects, for images and sequences. A mask is blended afterwards.
    persistence: Until the window closes. Each preview and each Apply takes the value as it is when it starts. **Reset** and a finished Apply return it to 0.
  - name: Contrast
    kind: spinbox
    default: "0"
    widget: {min: -100, max: 100, step: 1}
    effect: Spreads the values away from 0.5, or presses them toward it, after **Brightness**. Higher spreads them, lower flattens the image toward middle gray, and 0 leaves them as they are.
    range: Equation (2) turns the value into a factor, about 0.44 at the bottom of the range, 1 at 0 and about 2.27 at the top. The slider moves one unit at a time.
    enabled: Disabled while Apply runs. Any value but 0 enables **Apply**.
    tradeoff: A positive value sends the darkest and the brightest values to black and white, and the result is held between 0 and 1 again. A negative value pulls black and white toward gray, and the image looks washed out.
    scope: The preview and Apply, on what **Channel** selects, for images and sequences. A mask is blended afterwards.
    persistence: Until the window closes. Each preview and each Apply takes the value as it is when it starts. **Reset** and a finished Apply return it to 0.
  - name: Target Image
    kind: combo
    default: The active image
    options: [The active image, Every other open image or sequence the window can work on]
    effect: Chooses the image or sequence Apply replaces, with the mask on its view. The histogram, the choices of **Channel** and the preview follow it.
    range: It lists every open image and sequence.
    persistence: Until the window closes. **Reset** leaves it.
  - name: Channel
    kind: combo
    default: RGB
    options: [RGB, Red, Green, Blue, CIE L* (Lightness), CIE a*, CIE b*, CIE c*, CIE h*, CIE X, CIE Y (Luminance), CIE Z, HSL Hue, HSL Saturation, HSL Lightness, HSV Hue, HSV Saturation, HSV Value, YCbCr Luma, YCbCr Blue Chroma, YCbCr Red Chroma]
    effect: Chooses what the two values work on. **RGB** gives each of the three channels the same curve. **Red**, **Green** or **Blue** adjusts that channel and leaves the other two as they are. Every other choice adjusts one component, between 0 and 1, and converts the image back to red, green and blue.
    enabled: For an RGB image. A grayscale image shows a disabled **Grayscale**, and its one channel is adjusted. Disabled while Apply runs.
    tradeoff: The same curve on each channel changes the ratios between them, so **RGB** changes the colors a little and a single color shifts the balance. A lightness, luminance or value component changes the tone with less change of color, at the cost of two conversions. On a hue or a chroma the straight curve breaks the circle of hues and is not even to the eye. The conversion back can leave a floating-point color outside 0 to 1, and it cuts an integer one at the ends of its type.
    scope: The preview and Apply, for images and sequences. A mask is blended afterwards.
    persistence: Until the window closes. **Reset**, a finished Apply and a new target return it to **RGB**.
  - name: Preview
    kind: check
    default: "On"
    effect: Shows the adjusted image beside the controls, with the mask blended in. Off removes the preview, and the histogram returns to the image as it is.
    enabled: It stays enabled while Apply runs.
    scope: The window only. The preview runs the computation Apply runs, so it shows the values Apply writes, inside a preview area too. While you drag a slider it is computed on what the pane shows, the image reduced to the pane's size or the part in view at 100 % and closer, and on the whole image when you stop. The pane shows the samples as they are, in the **Linear** mode, whatever the target's view shows.
    persistence: Until the window closes. **Reset** leaves it.
    note: At 0 and 0 the pane shows the image itself. A snapshot of the preview is named `<target> - Adjust Brightness And Contrast - SNAPSHOT` and is not recorded as nonlinear.
  - name: Reset
    kind: button
    default: ""
    effect: Returns **Brightness** and **Contrast** to 0 and, for an RGB image, **Channel** to **RGB**.
    range: It leaves the target, **Preview** and the histogram as they are.
  - name: Apply
    kind: button
    default: ""
    effect: Adjusts the image, or every frame of the sequence, with the two values and **Channel** as they are when you press it, and then returns all three to their defaults.
    enabled: When **Brightness** or **Contrast** is not 0, so it is disabled when the window opens. While a run works it is disabled, with **Brightness**, **Contrast**, **Target Image**, **Channel**, **Reset** and **Close**, with **Preview** on or off.
    tradeoff: The values are held between 0 and 1 twice, and the conversion back from a component can cut an integer image again. An integer image is worked a band of rows at a time, without a working copy of the whole image.
    scope: The whole image or sequence, never limited by a preview area, with the mask blended afterwards.
    persistence: "The history step is **Adjust Brightness And Contrast** and reads, for example, `Adjust brightness to 40 and contrast to 25 in channel: RGB`. For a grayscale image it ends after the contrast. It does not record the mask, the clipping or the conversion of a component. Undoing the step restores the image before."
```

## How it works

1. Every sample is read as a [fraction of full scale](../../reference/glossary.md#fraction-of-full-scale). Floating-point
   samples are worked where they are, in their own precision. Integer ones are worked in single precision, or in
   double precision for 32-bit integers.
2. **Channel** decides what is adjusted: every channel, one channel, or one component of the image converted to a
   color model.
3. **Brightness** is added and the result held between 0 and 1. **Contrast** then scales each value's distance from
   0.5, and the result is held between 0 and 1 again. The four steps run as one pass, with the offset and the factor
   computed in single precision. At a **Contrast** of 0 the factor is exactly 1 and only the first two steps run.
4. Each value is written back in the image's own pixel type, an integer rounded to the nearest value with a tie to
   the even one. A component is converted back to RGB first. A mask on the view then blends the result with the
   original.
5. The image replaces the target and its history gets one step. Its header, its
   [plate solution](../../reference/glossary.md#plate-solution) and its acquisition data are kept with every choice of
   **Channel**. A saved file records `ANLIN` as `nonlinear`, `ANASRC` as `process` and `ANOP` as
   `brightness-contrast`. `ANBASIS` is `display-rgb` for a color image and `monochrome` for a grayscale one, but a
   recorded `monochrome` is kept and a recorded RGB basis becomes `display-rgb`. A linear encoding the image stated is
   no longer stated, and its [lineage](../../reference/glossary.md#lineage) gains an entry with both values.

### Model

With \(B\) the value of **Brightness**, the offset added to each value is

\[ b = \frac{B}{255} \tag{1} \]

With \(C\) the value of **Contrast**, the factor is

\[ k = \frac{259\,(C + 255)}{255\,(259 - C)} \tag{2} \]

A value \(x\), as a fraction of full scale, becomes \(u\) after the brightness and \(y\) after the contrast:

\[ u = \min\bigl(1,\ \max(0,\ x + b)\bigr), \qquad y = \min\bigl(1,\ \max(0,\ k\,(u - 0.5) + 0.5)\bigr) \tag{3} \]

With \(B = 0\), a value of 0.5 stays at 0.5 at any **Contrast**. With a positive **Contrast**, every value at or
below \(0.5 - 0.5/k - b\) becomes black and every one at or above \(0.5 + 0.5/k - b\) becomes white. With a negative
one, the result lies between \(0.5 - 0.5\,k\) and \(0.5 + 0.5\,k\).

### Boundaries

- A floating-point sample below 0 or above 1, an infinity among them, is brought to 0 or 1 by the first hold of
  equation (3). In a stored channel, a sample that is not a number passes through unchanged.
- A pixel the image marks as invalid is left as it was.
- At 0 and 0 nothing runs: the preview shows the image itself and **Apply** is disabled.
- A run that is stopped writes nothing, and the image and its history stay as they were. A run stops when its target
  closes, or with **Cancel** for a sequence.
- A single image has no progress window and cannot be cancelled once Apply starts.

### Worked example

The picture above shows the window on the linear Carina [master](../../reference/glossary.md#master) after Histogram
Transformation with **Shadows** and **Midtones** at 0.02, which leaves the background near 0.28. With **Channel** at
**RGB**, **Brightness** at -10 and **Contrast** at 20, equation (1) gives an offset of -0.039 and equation (2) a
factor of 1.169. By equation (3) the background drops to 0.20, a sample of 0.50 becomes 0.45 and one of 0.90 becomes
0.92. Every sample at or below 0.11 turns black and every one at or above 0.97 turns white. The sky is darker, the
nebula stands out from it more, and the brightest clouds and star cores reach white.

```figure
images:
  - image: ../../assets/images/processes/color-adjustment/adjust-brightness-contrast-before.webp
    alt: The whole frame before the adjustment, the red and white clouds of the nebula across a field of stars on a dark gray background.
    label: Before
  - image: ../../assets/images/processes/color-adjustment/adjust-brightness-contrast-after.webp
    alt: The whole frame after the adjustment, the same nebula with stronger contrast on a darker background, its brightest clouds and the star cores white.
    label: After
caption: >-
  The stretched Carina Nebula master before and after Apply with Brightness at -10 and Contrast at 20. Both are the
  whole frame as the view shows it in the Linear display mode, without a display stretch.
```

The order of the two steps matters. With **Brightness** at 25, equation (1) adds 0.098 and a sample of 0.25 becomes
0.348. **Contrast** at 50, a factor of 1.482, then takes it to 0.275. At the ends of their ranges, **Contrast** gives
factors of 0.4385 and 2.2677, and **Brightness** at 150 adds 0.588: every sample above 0.412 is held at 1 before
**Contrast** works.

## Starting points

| Situation | Start | Then adjust |
| --- | --- | --- |
| A stretched image whose tones need a small correction | **RGB**, **Brightness** between -10 and 10, then **Contrast** between -10 and 10 | Set **Brightness** first. Stop when **Clipping Low** or **Clipping High** rises. |
| Brighter, with more contrast | A small positive **Brightness**, and the highlights watched before you raise **Contrast** | When the highlights flatten, lower **Brightness** first, then **Contrast**. |
| A darker background with more contrast | A small negative **Brightness** and a positive **Contrast** | Watch the background: when the shadows collapse to black, lower either value. |
| A change through a mask | The values first, with the mask's edges checked | When the edge of the mask shows, soften the mask or lower the values. |

## When it goes wrong

!!! warning "Brightness clips before Contrast"
    What **Brightness** pushes past 0 or 1 is held there before **Contrast** works, and no later value brings it
    back. Set **Brightness** first and keep it small, and undo the step when highlights or shadows went flat.

!!! warning "Apply records the image as nonlinear"
    Every Apply marks the result nonlinear, with any choice of **Channel** and with or without a mask, and a later
    step treats it so. Use the image's history to return to the image before.

Apply is disabled
:   **Cause.** **Brightness** and **Contrast** are both 0, which changes nothing.
:   **Fix.** Set either to another value.

The highlights stay flat after you lower Contrast
:   **Cause.** **Brightness** held them at 1 before **Contrast** worked.
:   **Fix.** Lower **Brightness**, or press **Reset** and start again from the values you need.

Middle gray moves although only the contrast should change
:   **Cause.** **Brightness** moves every value before **Contrast** pivots about 0.5.
:   **Fix.** Set **Brightness** first, or leave it at 0.

Floating-point values above 1 or below 0 end at 1 or 0
:   **Cause.** The first hold of equation (3) brings every value into 0 to 1, at any values but 0 and 0.
:   **Fix.** Undo the step, and work on the image in another process when its values outside 0 to 1 must be kept.

Colors shift with a component of Channel
:   **Cause.** The curve changed one component, and the conversion back cut an integer image or left a floating-point
    color outside 0 to 1.
:   **Fix.** Choose **RGB**, or use a process made for color.

The preview and the result differ
:   **Cause.** The target, its mask or its inversion, **Channel** or the frame of a sequence changed after the last
    preview, or a [preview area](../../reference/glossary.md#preview-area) showed only part of the image.
:   **Fix.** Wait for the latest preview, and compare with **Preview area** off.

The preview looks darker than the image's view
:   **Cause.** The view is in [Autostretch](../../reference/glossary.md#autostretch), and the preview pane shows the
    samples as they are.
:   **Fix.** Switch the view to **Linear** to compare, or judge the result in the view after Apply.

## Related

- [Histogram Transformation](../stretching/histogram-transformation.md),
  [Generalized Hyperbolic Transformation](../stretching/gh-transformation.md) and
  [ArcSinh Stretch](../stretching/arcsinh-stretch.md), for the main stretch, which this window does not do.
- [Exposure Correction](../stretching/exposure-correction.md), which multiplies the samples by a factor instead of
  adding an offset.
- [Gamma Correction](../stretching/gamma-correction.md), to brighten or darken the
  [midtones](../../reference/glossary.md#midtones) and keep black and white where they are.
- [Adjust HSL](adjust-hsl.md) and [Adjust LCh](adjust-lch.md), to change hue, saturation or lightness by name.
- [Image History](../../interface/image-history.md) and [Image Statistics](../../interface/image-statistics.md), to
  undo the step and to check the range of the samples.
- [Process Windows](../../interface/process-windows.md), for the preview, the histogram toolbar and what every
  process window shares.
