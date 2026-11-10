---
kind: process
stage: finish
menu: Process > Intensity Transformations > Color Saturation
summary: >-
  Strengthens or mutes the colors of an RGB image, for every hue or for one hue range, and leaves the pixels at or
  below a background cutoff as they were.
data_in: nonlinear
data_out: nonlinear
destructive: true
preview: real-time
sequences: true
masks: post-blend
external: none
glance:
  enabled: >-
    A color image, with or without an alpha channel, or a sequence that shows one is active, no Color Saturation window
    is open on it yet, and no window that needs the image for itself is open on it.
  inputs: >-
    One RGB image, or a sequence of RGB frames, without an alpha channel, and an optional mask of the image's size on
    its view.
  outputs: >-
    The same view, replaced, with one history step. Its size, its channels and its pixel type are kept. No file,
    preset or report is written.
  state: >-
    A stretched RGB image after color calibration, whose colors need a last adjustment. Every Apply records the result
    as nonlinear, at an Amount of 0 too.
derived: false
rewritten: true
---
# Color Saturation

Color Saturation changes how strong the colors of an RGB image are. It converts each pixel to the
[HSL](../../reference/glossary.md#hsl) model and multiplies its saturation by one factor, for every hue or for one
range of hues. A pixel whose lightness is at or below a cutoff, which the green channel's statistics and
**Background factor** set, is left exactly as it was, so the dark sky keeps its colors. It is a finishing tool for
display, after color calibration and the main [stretch](../../reference/glossary.md#stretch).

**Apply** replaces the image or [sequence](../../reference/glossary.md#sequence) with the adjusted one, keeps its size
and [pixel type](../../reference/glossary.md#pixel-type), and adds one step to its
[history](../../reference/glossary.md#history). The result is recorded as nonlinear.

```figure
image: ../../assets/images/processes/color-adjustment/color-saturation.webp
alt: >-
  The Color Saturation window. On the left, a histogram with one broad peak near its left third and the lines
  Clipping Low and Clipping High in its corner. Under it a toolbar, Hue at Global, Amount at 0.50 and Background factor at
  1.00 with their sliders, Target Image and Preview checked. On the right, the preview shows a red nebula in strong
  colors on a gray background. Reset, Close and Apply run along the bottom.
caption: >-
  Color Saturation on a stretched master with Hue at Global, Amount at 0.50 and Background factor at 1.00. The
  preview shows the stronger colors Apply writes, and the sky below the cutoff as it was.
pins:
  - {n: 2, x: 2.0, y: 53.8}
  - {n: 3, x: 2.0, y: 58.9}
  - {n: 4, x: 2.0, y: 63.9}
  - {n: 5, x: 73.0, y: 26.0}
  - {n: 6, x: 91.0, y: 96.0}
```

## Before you start

- Calibrate the colors and stretch the image first. The window reads the stored red, green and blue as they are, with
  no color profile or transfer curve, so it calibrates nothing.
- The image is RGB without an alpha channel. A color image with alpha is offered but refused when the preview or
  Apply runs.
- **Apply** replaces the image or sequence. Duplicate it first, or use its history, which keeps the step before.
- A [mask](../../reference/glossary.md#mask) on the view blends the result with the original afterwards. Check that it
  is enabled, and whether it is inverted, before you Apply.
- On a sequence, each frame gets the cutoff of its own green channel, with the same three values.

## Steps

1. **Open the window.** Make the stretched RGB image the active one and choose **Process > Intensity
   Transformations > Color Saturation**. **Target Image** names it, the preview shows it unchanged, and **Apply** is
   already enabled.
2. **Choose the hues.** Leave **Hue** at **Global** to change every color. Choose a range only to change one family
   of colors and leave the others alone.
3. **Set the amount.** Raise **Amount** in small steps to strengthen the colors, or lower it below 0 to mute them. The
   preview follows: the colors of the brighter parts change, and the dark sky stays as it was.
4. **Protect the background.** Leave **Background factor** where it is to leave the sky alone. Raise it if the sky
   gains color, or lower it if a faint colored structure does not change.
5. **Check the preview at 100 %.** Zoom the preview into the background, the faint colored structure, the borders
   between colors and the brightest stars. Press ++space++ over it to compare with the original, and look for colored
   noise, halos and hard color edges.
6. **Apply.** Choose **Apply**. The image in its view is the adjusted one, its history lists **Color Saturation**
   with the amount, the factor and the hue range, and the controls return to their defaults.

## Controls

The histogram and its toolbar, the preview pane, **Close** and the progress window of a sequence work as in every
process window: see [Process Windows](../../interface/process-windows.md). This window has no **Channel**: it changes
the three channels together. Its preview pane has no **Display Mode**.

```controls
window: Color Saturation
controls:
  - name: Hue
    kind: combo
    default: Global
    options: [Global, Pink-Red / Red-Orange, Orange-Brown / Yellow, Yellow-Green / Green-Cyan, Cyan, Cyan-Blue / Blue-Magenta, Magenta / Pink]
    effect: Chooses the hues whose saturation changes. **Global** takes every hue. Each other choice takes one range of hue angles, and a pixel outside it keeps its saturation.
    range: "In degrees, with red at 0, green at 120 and blue at 240: **Global** 0 to 360, **Pink-Red / Red-Orange** 346 to 360 and 0 to 20, **Orange-Brown / Yellow** 21 to 60, **Yellow-Green / Green-Cyan** 61 to 200, **Cyan** 170 to 200, **Cyan-Blue / Blue-Magenta** 201 to 280 and **Magenta / Pink** 281 to 345. Both ends are included."
    enabled: Disabled while Apply runs.
    tradeoff: The edges are hard, with no feathering, so a smooth change of color can show a step where it crosses one. A hue between two ranges, such as 20.5 degrees, is in neither. **Cyan** lies inside **Yellow-Green / Green-Cyan**. The names label the ranges and are not calibrated bands of the spectrum.
    scope: The preview and Apply, on the pixels above the cutoff, all three channels together, for images and sequences. A mask is blended afterwards.
    persistence: Until the window closes. **Reset** and a finished Apply return it to **Global**.
  - name: Amount
    kind: spinbox
    default: "0.00"
    widget: {min: -1, max: 1, step: 0.01, decimals: 2}
    effect: Multiplies the saturation of the chosen pixels by 1 plus the amount. Above 0 the colors grow stronger, below 0 they fade, and at -1 they turn gray. At 1 the saturation is doubled, up to its limit of 1.
    range: The slider beside the box moves the same value.
    enabled: Disabled while Apply runs. **Apply** is enabled at 0 too.
    tradeoff: A positive value strengthens colored noise and color halos as much as the signal, and colors that are already strong reach the limit and flatten. A negative value mutes the chosen colors.
    scope: The preview and Apply, on the pixels above the cutoff whose hue is chosen. A mask is blended afterwards.
    persistence: Until the window closes. Each preview and each Apply takes the value as it is when it starts. **Reset** and a finished Apply return it to 0.
  - name: Background factor
    kind: spinbox
    default: "1.00"
    widget: {min: 0, max: 2, step: 0.01, decimals: 2}
    effect: Sets the cutoff at or below which pixels are left as they were, as a multiple of the green channel's median plus its standard deviation. Higher leaves more of the dark pixels alone. Lower reaches fainter ones, and at 0 only pure black is left alone.
    range: The cutoff is compared with each pixel's HSL lightness and held between 0 and 1. A factor that takes it to the lightness of every pixel, as a high factor on a bright image does, changes nothing.
    enabled: Disabled while Apply runs.
    tradeoff: It is a hard gate on lightness, with no soft edge, so a faint colored structure below the cutoff is not changed at all. Its tooltip speaks of low-saturation areas, but the gate reads lightness alone. A dim, strongly colored pixel is protected, and a bright, pale one is not. A value above 0 measures the median and the deviation of the green channel, which adds to the work of a preview and of Apply.
    scope: The preview and Apply. The median and the deviation are those of the whole image, also for a preview area, a snapshot of one and the previews while you drag, so the area shows what Apply writes there. On a sequence each frame gets the cutoff of its own green channel.
    persistence: Until the window closes. Each preview and each Apply takes the value as it is when it starts. **Reset** and a finished Apply return it to 1.
    note: The deviation is that of the whole population of green samples. A green sample that is not a number is left out of both measurements, and so is a pixel the image marks as invalid.
  - name: Target Image
    kind: combo
    default: The active image
    options: [The active image, Every other open color image or sequence]
    effect: Chooses the image or sequence Apply replaces, with the mask on its view. The cutoff, the histogram and the preview follow it.
    range: It lists the open color images and sequences, with or without an alpha channel.
    persistence: Until the window closes. **Reset** leaves it. An edit that turns the target into a grayscale image closes the window.
  - name: Preview
    kind: check
    default: "On"
    effect: Shows the adjusted image beside the controls, with the mask blended in. Off removes the preview, and the histogram returns to the image as it is.
    enabled: It stays enabled while Apply runs.
    tradeoff: With **Background factor** above 0, each preview of the whole image measures the green channel again. The previews of a preview area and of a drag reuse one measurement of the image.
    scope: The window only. The preview runs the computation Apply runs, so it shows the values Apply writes. While you drag a slider it is computed on what the pane shows, the image reduced to the pane's size or the part in view at 100 % and closer, and on the whole image when you stop. The pane shows the samples as they are, in the **Linear** mode, whatever the target's view shows.
    persistence: Until the window closes. **Reset** leaves it.
    note: A snapshot of the preview is an RGB image of the target's pixel type, named `<target> - Color Saturation - SNAPSHOT`, and is not recorded as nonlinear.
  - name: Reset
    kind: button
    default: ""
    effect: Returns **Hue** to **Global**, **Amount** to 0 and **Background factor** to 1.
    range: It leaves the target, **Preview**, the histogram and its toolbar as they are.
  - name: Apply
    kind: button
    default: ""
    effect: Changes the saturation of the image, or of every frame of the sequence, with the three values as they are when you press it, and then returns them to their defaults.
    enabled: From the moment the window opens, at an **Amount** of 0 too. While a run works it is disabled, with **Hue**, **Amount**, **Background factor**, **Target Image**, **Reset** and **Close**, with **Preview** on or off.
    tradeoff: Every pixel above the cutoff is converted to HSL and back. An integer image is worked in a floating-point copy, and a positive factor measures the green channel first.
    scope: The whole image or sequence, never limited by a preview area, with the mask blended afterwards.
    persistence: "The history step is **Color Saturation** and reads, for example, `Double coeff 0.5, background factor 1, hue min 0, hue max 360`. It records the hue range in degrees and not its name, the mask or the cutoff it computed. Undoing the step restores the image before."
```

## How it works

1. Every sample is read as a [fraction of full scale](../../reference/glossary.md#fraction-of-full-scale). Floating-point
   samples are worked where they are. Integer ones are converted into a floating-point copy, in double precision for
   32-bit integers, and each pixel is computed in double precision.
2. With a **Background factor** above 0, the median and the standard deviation of the green channel give the cutoff,
   by equation (2). At 0 the cutoff is 0.
3. Each pixel's hue, saturation and lightness are computed. A pixel whose lightness is at or below the cutoff, or
   which holds not-a-number or an infinity, is left as it was.
4. In every other pixel, the saturation is changed by equation (3) when the hue lies in the range of **Hue**. The
   pixel is converted back to RGB and each channel held between 0 and 1. A mask on the view then blends the result
   with the original.
5. The image replaces the target and its history gets one step. Its header, its
   [plate solution](../../reference/glossary.md#plate-solution) and its acquisition data are kept. A saved file
   records `ANLIN` as `nonlinear`, `ANASRC` as `process`, `ANOP` as `saturation-enhancement` and `ANBASIS` as
   `display-rgb`, unless the source recorded `monochrome`. A linear encoding the image stated is no longer stated, and
   its [lineage](../../reference/glossary.md#lineage) gains an entry with the amount, the cutoff and the hue range.

### Model

With \(v\) the largest and \(m\) the smallest of a pixel's red, green and blue, as fractions of full scale, its HSL
lightness is

\[ l = \frac{v + m}{2} \tag{1} \]

With \(f\) the value of **Background factor**, \(\tilde{G}\) the median of the green channel and \(\sigma_G\) its
standard deviation, the cutoff is

\[ b = \min\Bigl(1,\ \max\bigl(0,\ f\,(\tilde{G} + \sigma_G)\bigr)\Bigr) \tag{2} \]

A pixel changes only when \(l > b\). Its saturation \(s\) is \((v - m)/(v + m)\) when \(l \le 0.5\) and
\((v - m)/(2 - v - m)\) above. With \(a\) the value of **Amount**, a pixel whose hue is chosen takes the saturation

\[ s' = \min\bigl(1,\ s\,(1 + a)\bigr) \tag{3} \]

Hue and lightness stay as they were. Each channel \(x\) then becomes

\[ y = l + \frac{s'}{s}\,(x - l) \tag{4} \]

so each channel's distance from the lightness is multiplied by \(1 + a\) until the saturation reaches 1. A gray pixel,
whose saturation is 0, does not change.

### Boundaries

- An image with an alpha channel, or a frame of a sequence that is not RGB, stops the preview and Apply with "the
  colour saturation needs an image of red, green and blue". Nothing is written.
- A pixel holding not-a-number or an infinity is left as it was, and so is a pixel the image marks as invalid.
- Floating-point samples outside 0 to 1 are read as they are. A pixel above the cutoff is rebuilt with its saturation
  held at 1 when its hue is chosen, and its channels are held between 0 and 1. An integer result is rounded to the
  nearest value, a tie to the even one.
- A run that is stopped writes nothing, and the image and its history stay as they were. A run stops when its target
  closes, or with **Cancel** for a sequence.
- A single image has no progress window and cannot be cancelled once Apply starts.

### Worked example

The picture above shows the window on the linear Carina [master](../../reference/glossary.md#master) after Histogram
Transformation with **Shadows** and **Midtones** at 0.02, which leaves the background near 0.28. With **Hue** at
**Global**, **Amount** at 0.50 and **Background factor** at 1.00, equation (2) puts the cutoff one standard deviation
of the green channel above its median of about 0.28. Most of the sky lies below it and keeps its colors. A red pixel
at (0.60, 0.40, 0.40) has a lightness of 0.50 and a saturation of 0.20. Above the cutoff, equation (3) raises the
saturation to 0.30, and by equation (4) the pixel becomes (0.65, 0.35, 0.35): redder, at the same lightness.

```figure
images:
  - image: ../../assets/images/processes/color-adjustment/color-saturation-before.webp
    alt: The whole frame before the change, the red and white clouds of the nebula across a field of stars on a dark gray background.
    label: Before
  - image: ../../assets/images/processes/color-adjustment/color-saturation-after.webp
    alt: The whole frame after the change, the nebula in stronger, deeper colors at the same brightness, on the same gray background.
    label: After
caption: >-
  The stretched Carina Nebula master before and after Apply with Hue at Global, Amount at 0.50 and Background factor
  at 1.00. Both are the whole frame as the view shows it in the Linear display mode, without a display stretch.
```

A pure red, (1, 0, 0), has a lightness of 0.5 and a saturation of 1. With **Amount** at -0.50 and a cutoff below
0.5, it becomes (0.75, 0.25, 0.25). With **Amount** at 1, equation (3) holds its saturation at 1 and it stays pure
red. With a cutoff of 0.5 or more it is not changed at all, whatever **Hue** and **Amount** say.

## Starting points

| Situation | Start | Then adjust |
| --- | --- | --- |
| One family of colors is too weak | The narrowest range that holds it, and **Amount** at 0.05 | Raise **Amount** slowly. Stop when the borders of the range, colored noise or halos show. |
| The sky gains color, or faint color does not change | **Background factor** at 1.00, compared with 0.00 | Set **Amount** first, then raise the factor only as far as the sky needs. |
| A color near the edge of a range | **Global**, then the two neighboring ranges | Compare them. **Cyan** is narrow, and **Yellow-Green / Green-Cyan** covers it too. |
| A change through a mask | The mask checked first, with its inversion | Then set **Hue**, **Amount** and **Background factor**, and check where the mask's edge meets a hue border. |

## When it goes wrong

!!! warning "Background factor is a hard lightness gate"
    It does not weigh the change softly or find a gray background. Every pixel whose lightness is at or below the
    cutoff is left exactly as it was, and a cutoff that reaches 1 leaves every pixel so. Protect a dim colored
    structure you want to keep with a mask instead.

!!! warning "Apply changes the image at an Amount of 0"
    **Apply** is enabled at the defaults. At an **Amount** of 0 it still rebuilds the pixels above the cutoff, holds
    floating-point values outside 0 to 1, adds a history step and records the image as nonlinear. Close the window
    to leave the image as it is.

"the colour saturation needs an image of red, green and blue"
:   **Cause.** The target has an alpha channel, or a frame of the sequence is not RGB.
:   **Fix.** Remove the alpha channel, or make every frame RGB, then apply again. The target was not changed.

Nothing changes
:   **Cause.** **Amount** is 0, the hue range leaves the pixels out, or the cutoff is at or above their lightness.
:   **Fix.** Set an **Amount**, try **Global** with **Background factor** at 0, then return to your values one at a
    time.

A faint colored structure does not change
:   **Cause.** Its lightness is at or below the cutoff.
:   **Fix.** Lower **Background factor**, and protect the sky with a mask instead.

Yellow-Green / Green-Cyan changes the cyan too
:   **Cause.** **Cyan**, from 170 to 200 degrees, lies inside **Yellow-Green / Green-Cyan**, from 61 to 200.
:   **Fix.** Choose **Cyan** for cyan alone, and compare with **Global**.

A color near the border of two ranges does not change
:   **Cause.** Its hue lies in the one-degree gap between the two ranges, which neither takes.
:   **Fix.** Use **Global**, which takes every hue, at a modest **Amount**.

Saturated colors flatten, or halos and colored noise grow
:   **Cause.** The saturation reached its limit of 1, or **Amount** strengthened the noise with the signal.
:   **Fix.** Lower **Amount**, or apply it through a better mask.

Apply changed more of the image than the preview showed
:   **Cause.** A [preview area](../../reference/glossary.md#preview-area) was drawn, and outside it the preview shows
    the image as it was.
:   **Fix.** Untick **Preview area** to see the whole result before you Apply.

The preview looks darker than the image's view
:   **Cause.** The view is in [Autostretch](../../reference/glossary.md#autostretch), and the preview pane shows the
    samples as they are.
:   **Fix.** Switch the view to **Linear** to compare, or judge the result in the view after Apply.

## Related

- [Photometric Color Calibration](../color-calibration/photometric-cc.md) and
  [Spectrophotometric Color Calibration](../color-calibration/spectrophotometric-cc.md), which balance the colors
  and come before.
- [Adjust HSL](adjust-hsl.md), to turn the hue and change saturation and lightness across the whole image.
- [Adjust LCh](adjust-lch.md), to change lightness, chroma and hue in a model closer to the eye.
- [Curves Transformation](../stretching/curves-transformation.md), when a curve on a channel or a component is the
  change you want.
- [Image History](../../interface/image-history.md) and [Image Statistics](../../interface/image-statistics.md), to
  undo the step and to check the result.
- [Process Windows](../../interface/process-windows.md), for the preview, the histogram toolbar and what every
  process window shares.

## Credits

- The W3C [CSS Color Module Level 4](https://www.w3.org/TR/css-color-4/#the-hsl-notation) describes the HSL model,
  its conversion to and from RGB, and its limits as a model of what the eye sees.
