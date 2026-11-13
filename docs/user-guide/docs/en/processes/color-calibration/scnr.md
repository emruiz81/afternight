---
kind: process
stage: color
menu: Process > Color Calibration > SCNR Filter
summary: >-
  Suppresses green in an RGB image with one of four per-pixel protection methods, to remove a green cast or green
  noise, and records the result as nonlinear.
data_in: either
data_out: nonlinear
destructive: true
preview: none
sequences: true
masks: post-blend
external: none
glance:
  enabled: An RGB image or sequence view is active. SCNR does not need the image to itself.
  inputs: One RGB image or sequence of any pixel type, and an optional mask on its view.
  outputs: >-
    The same view, replaced, with one history step, and recorded as nonlinear. Its size, pixel type and header are
    kept. The window stays open. No file, preset or report is written.
  state: >-
    A color-calibrated RGB image with a green cast or green noise left, after every step that needs linear data.
derived: false
---
# SCNR

SCNR, subtractive chromatic noise reduction, removes green from an RGB image. Two methods hold each pixel's green
under a ceiling taken from its red and blue. Two others scale green by a factor taken from red and blue, as much as
**Amount** asks. **Preserve Lightness** keeps each pixel's lightness while it takes the suppressed color.

**Apply** replaces the image, or every frame of a [sequence](../../reference/glossary.md#sequence), adds one step to
its [history](../../reference/glossary.md#history) and leaves the window open. The size, the
[pixel type](../../reference/glossary.md#pixel-type) and the header are kept, and the result is recorded as a
[nonlinear image](../../reference/glossary.md#nonlinear-image). The window has no preview.

```figure
image: ../../assets/images/processes/color-calibration/scnr.webp
alt: >-
  The SCNR window. Protection Type reads Average Neutral. Amount reads 1.00 with its slider at the right end, both
  dimmed. Target Image names the master, Preserve Lightness is ticked, and Close and Apply sit at the bottom.
caption: >-
  SCNR on the stretched Carina master with its defaults, Average Neutral and Preserve Lightness, ready to apply.
pins:
  - {n: 2, x: 3.0, y: 39.1}
  - {n: 3, x: 3.0, y: 50.4}
  - {n: 4, x: 42.0, y: 71.9}
```

## Before you start

- Calibrate the color first, with Background Neutralization and Color Calibration, Photometric Color Calibration or
  Spectrophotometric Color Calibration. SCNR records its result nonlinear, and the last two refuse such an image.
- Decide whether the green is unwanted. Oxygen emission, reflection nebulae and some stars are green, and
  **Maximum Mask** and **Additive Mask** reduce green even where it is not the strongest channel.
- **Apply** replaces the image, with no preview and no **Reset**. A second Apply suppresses again on top of the first,
  so undo in the history before you try other settings.
- **Preserve Lightness** reads the image as linear only when the image is described as linear RGB. A
  [FITS](../../reference/glossary.md#fits) file states no color space and is read as sRGB-encoded, which suits an
  image after its [stretch](../../reference/glossary.md#stretch).
- A [mask](../../reference/glossary.md#mask) on the view blends the result with the original, and is read when you
  press **Apply**.
- On a sequence, every frame is processed with the same settings.

## Steps

1. **Open the window.** Make the RGB image the [active image](../../reference/glossary.md#active-image) and choose
   **Process > Color Calibration > SCNR Filter**.
2. **Choose how much green goes.** Leave **Protection Type** at **Average Neutral** to hold green at the mean of red
   and blue. Choose **Maximum Neutral** to touch only green that rises above both.
3. **Or grade it.** Choose **Maximum Mask** or **Additive Mask**, and set **Amount** below 1 to remove part of the
   green.
4. **Keep the lightness.** Leave **Preserve Lightness** ticked so that each pixel keeps its brightness. Untick it to
   change green alone.
5. **Apply.** Choose **Apply**. The view shows the image with its green held down, the history lists **SCNR** with the
   method, and the window stays open.
6. **Compare.** Look at green star cores, green areas of the nebula and the dark background at 100 %. The green that
   stood above the method's ceiling is gone.

## Controls

What every process window shares is on [Process Windows](../../interface/process-windows.md).

```controls
window: SCNR
controls:
  - name: Protection Type
    kind: combo
    default: Average Neutral
    options: [Average Neutral, Maximum Neutral, Maximum Mask, Additive Mask]
    effect: Chooses how green is held down. **Average Neutral** holds it at the mean of red and blue, and removes the most. **Maximum Neutral** holds it at the larger of red and blue, and touches only green that rises above both. **Maximum Mask** and **Additive Mask** scale green by a mask taken from red and blue, as much as **Amount** asks.
    enabled: A mask method enables **Amount**, and a neutral method disables it.
    tradeoff: The neutral methods are full-strength ceilings. The mask methods reduce green everywhere, also where it is not the strongest channel. **Additive Mask** keeps at least as much green as **Maximum Mask** at the same pixel and Amount.
    scope: Apply, on green, and on all three channels with **Preserve Lightness**.
    persistence: Until the window closes. A finished Apply keeps it. The history records it.
  - name: Amount
    kind: spinbox
    default: "1.00"
    widget: {min: 0, max: 1, step: 0.05, decimals: 2}
    effect: Sets how much the mask methods suppress green. Higher removes more. At 0 green stays as it was, and at 1 the mask is applied outright.
    range: The slider spans the range in a thousand steps.
    enabled: With **Maximum Mask** or **Additive Mask**. The neutral methods do not read it, and it keeps its value while they are chosen.
    tradeoff: High values can take the color out of real green structure, most in the dark areas.
    scope: Apply, on green.
    persistence: Until the window closes. The history does not record it.
  - name: Target Image
    kind: combo
    default: The active image
    options: [The active image, Every other open RGB image or sequence]
    effect: Chooses the image or sequence Apply changes. The other controls keep their values.
    tradeoff: The image chosen here is the one replaced.
    persistence: Until the window closes.
  - name: Preserve Lightness
    kind: check
    default: "On"
    effect: On, each pixel keeps the lightness it had and takes its color from the suppressed pixel, so red and blue move too. Off, only green changes.
    range: The lightness is CIE L*, with sRGB primaries and a D65 white. The image is read as linear RGB when it is described so, and as sRGB-encoded otherwise.
    tradeoff: It costs three color conversions a pixel. A rebuilt color outside 0 to 1 is held there, and then its lightness is not kept exactly.
    scope: Apply, on all three channels while it is on.
    persistence: Until the window closes. The history records it.
  - name: Close
    kind: button
    default: ""
    effect: Closes the window without applying.
    enabled: Disabled while a run works.
    note: Closing the window another way while a run works hides it, and it closes when the run has ended. The run goes on, on a sequence until the progress window's **Cancel** stops it.
  - name: Apply
    kind: button
    default: ""
    effect: Suppresses green over the whole image, or every frame of the sequence, with the values as they are when you press it. The window stays open for another run.
    enabled: At once. While the run works, Apply and **Close** are disabled. The other controls stay editable, and a change does not reach the run.
    tradeoff: The run holds two floating-point working copies of the image beside the result. A single image shows no progress window, and its run cannot be stopped once it starts. A sequence needs the disk space of a second copy.
    scope: Every pixel, through the mask on the view. Every frame of a sequence, whatever frames its player has selected, debayered first, by the method the sequence states, when the view debayers them.
    persistence: "The image and its history keep the result, as the step **SCNR** with the text `Protection Method Average Neutral preserve lightness true`, or `false` with **Preserve Lightness** off. Amount and the mask are not recorded."
    note: "A failed run leaves the image and its history as they were, shows \"Error running SCNR\" with \"An error occurred when executing the SCNR process:\" and the reason, and writes them to the log. While another run holds a sequence, the busy message comes under the title SCNR and nothing starts. When the history cannot keep the image, it says so once in a message titled \"Image History\"."
```

```controls
window: Sequence progress
controls:
  - name: Progress
    kind: readout
    default: ""
    effect: "Shows the share of the frames done, under the title **SCNR** and the text `Executing SCNR to sequence <name>`."
    enabled: Only while Apply works on a sequence. A single image has no progress window.
    tradeoff: The new sequence is written to a temporary file, which needs its disk space until the run ends.
  - name: Cancel
    kind: button
    default: ""
    effect: Stops the run, before the next frame at the latest, without a message. The sequence stays as it was, with no history step, and the temporary file is removed.
```

## How it works

1. **Apply** reads the method, **Amount**, **Preserve Lightness** and the view's mask. A change made afterwards does
   not reach the run.
2. Each sample is read as a [fraction of full scale](../../reference/glossary.md#fraction-of-full-scale), in single
   precision, or double for a 64-bit floating-point or 32-bit integer image.
3. For each pixel, green is held down by the method. With **Preserve Lightness**, the pixel is rebuilt from its
   original lightness and the suppressed color. All three channels are then held between 0 and 1.
4. The result is written back in the image's pixel type, rounded to the nearest value, and a mask on the view blends
   it with the original.
5. The result replaces the target with one history step. Its header, acquisition data and
   [plate solution](../../reference/glossary.md#plate-solution) are kept. Its
   [processing state](../../reference/glossary.md#processing-state) records it as nonlinear display RGB under the
   operation `scnr`, and its [lineage](../../reference/glossary.md#lineage) gains an entry with the detail
   `SCNR: AverageNeutral, amount 1.000000, lightness preserved`: the method without spaces, the amount, and
   `not preserved` with **Preserve Lightness** off.

### Model

For a pixel with red \(r\), green \(g\) and blue \(b\), between 0 and 1, **Average Neutral** writes the green

\[ g' = \min\Bigl(g,\ \frac{r + b}{2}\Bigr) \tag{1} \]

and **Maximum Neutral** writes

\[ g' = \min\bigl(g,\ \max(r, b)\bigr) \tag{2} \]

Since \((r + b)/2\) is never above \(\max(r, b)\), equation (1) removes at least as much green as equation (2). With
\(A\) the value of **Amount**, and the method's protection \(m\) of \(\max(r, b)\) for **Maximum Mask** or
\(\min(1,\ r + b)\) for **Additive Mask**, the mask methods write

\[ g' = g\,\bigl[1 - A\,(1 - m)\bigr] \tag{3} \]

so \(g' = g\) at \(A = 0\) and \(g' = m\,g\) at \(A = 1\). Equation (3) does not ask whether green is the strongest
channel.

Without **Preserve Lightness** the pixel becomes \((r, g', b)\). With it, the original pixel and \((r, g', b)\) are both
converted to [CIE Lab](../../reference/glossary.md#cie-lab), and the pixel is rebuilt from the original's L* and the
suppressed pixel's a* and b*.

### Boundaries

- Only green is suppressed. There is no choice of channel, no detection of a cast and no threshold.
- Every channel of the result is held between 0 and 1, so a floating-point sample outside that range is cut to it,
  even where green is not changed.
- A pixel that the image's [validity rule](../../reference/glossary.md#validity-rule) rejects is left as it was in all
  three channels.
- A frame of a sequence that is not three-channel RGB stops the run with "SCNR needs an RGB image".
- A single image's run has no progress window and cannot be stopped once it starts.

### Worked example

The Carina [master](../../reference/glossary.md#master), stretched by Histogram Transformation with **Shadows** at 0.02
and **Midtones** at 0.02, has its background near 0.28 in each channel, and green holds the most pixel-to-pixel
noise. Keep the defaults, **Average Neutral** with **Preserve Lightness**, and choose **Apply**.

A background pixel of (0.28, 0.31, 0.27) has a green speck: equation (1) holds its green at 0.275. **Preserve
Lightness** then gives the pixel back its lightness, and it becomes about (0.31, 0.30, 0.30). Over the whole frame,
green changes in about four pixels of ten. The background's green median falls to 0.27 while red and blue rise to
0.29, and the faint green tint of the right part of the frame goes. Without **Preserve Lightness**, red and blue would
stay at 0.28 and green would fall to 0.26.

One pixel of (0.20, 0.60, 0.40), with **Preserve Lightness** off, shows the four methods side by side. **Average
Neutral** writes a green of 0.30 and **Maximum Neutral** 0.40. **Maximum Mask** writes 0.24 at an **Amount** of 1 and
0.42 at 0.5, and **Additive Mask** 0.36 at 1. Red and blue stay as they were, and with **Maximum Neutral** the history
step reads `Protection Method Maximum Neutral preserve lightness false`.

```figure
images:
  - image: ../../assets/images/processes/color-calibration/scnr-before.webp
    alt: >-
      A crop at 100 % of the middle of the stretched master before SCNR: pink clouds of nebula beside a dark dust lane
      and many stars, the background's fine grain speckled with green.
    label: Before
  - image: ../../assets/images/processes/color-calibration/scnr-after.webp
    alt: The same crop after SCNR, the green speckle gone and the grain a touch warmer, the clouds and stars as before.
    label: After
caption: >-
  The middle of the stretched master at 100 % before and after Apply with Average Neutral and Preserve Lightness, in
  the Linear display mode. In the crop, one pixel in nine stood more than five display levels greener than the mean
  of its red and blue before, and none after.
```

## Starting points

| Situation | Start | Then adjust |
| --- | --- | --- |
| A faint green cast after color calibration | **Maximum Neutral** | Undo, then **Average Neutral** if green remains. |
| Green noise in a dark background | **Average Neutral**, with a mask that protects the bright nebula | Check the background and the nebula's edges at 100 %. |
| Green signal to keep, such as oxygen emission | **Additive Mask** with **Amount** at 0.5 | Raise **Amount** in small steps, undoing between tries. |
| A cast in one region only | The method above, through a soft mask over the region | Check the mask's edges at 100 %. |

## When it goes wrong

!!! warning "SCNR records the image as nonlinear"
    Photometric Color Calibration and Spectrophotometric Color Calibration refuse the image afterwards. Calibrate the
    color first, or undo the SCNR step in the image's history.

"Photometric color calibration requires linear data; this image is marked nonlinear."
:   **Cause.** SCNR ran before the color calibration and recorded the image as nonlinear.
:   **Fix.** Undo the SCNR step, calibrate the color, then apply SCNR again.

The menu item is disabled, or Target Image does not offer the image
:   **Cause.** The image is not a three-channel RGB image.
:   **Fix.** Make an RGB image active, or convert the image with **Convert to RGB** first.

Amount is disabled
:   **Cause.** **Average Neutral** or **Maximum Neutral** is chosen, and neither reads it.
:   **Fix.** Choose **Maximum Mask** or **Additive Mask**.

Real green is gone, in oxygen clouds or green stars
:   **Cause.** A ceiling or a mask method removed green that was signal.
:   **Fix.** Undo the step, then try **Maximum Neutral**, or **Additive Mask** with a lower **Amount**, or a mask that
    protects those areas.

The background turned magenta
:   **Cause.** The background had no green cast, and the method pulled green below red and blue.
:   **Fix.** Undo the step, and use **Maximum Neutral** or leave SCNR out.

Red and blue changed too, or Amount at 0 still changes something
:   **Cause.** **Preserve Lightness** rebuilds the pixel from its lightness, and every channel is held between 0 and 1.
:   **Fix.** Undo the step and untick **Preserve Lightness** to change green alone.

"Error running SCNR"
:   **Cause.** The run was refused for the reason the message gives: memory, a mask of another size, or a frame that
    is not RGB.
:   **Fix.** Free memory, give the mask the image's size, or check the sequence's frames, and apply again.

The history does not tell how strong the step was
:   **Cause.** The history records the method and **Preserve Lightness**, not **Amount** or the mask.
:   **Fix.** Note **Amount** and the mask before you apply.

## Related

- [Background Neutralization](background-neutralization.md) and [Color Calibration](color-calibration.md), which come
  before.
- [Photometric Color Calibration](photometric-cc.md) and
  [Spectrophotometric Color Calibration](spectrophotometric-cc.md), which refuse an image SCNR has changed.
- [Histogram Transformation](../stretching/histogram-transformation.md), the stretch the worked example starts from.
- [Color Saturation](../color-adjustment/color-saturation.md), for changes of color that are not about green.
- [Masks](../../interface/masks.md), to limit SCNR to part of the image.
- [Process Windows](../../interface/process-windows.md), for what every process window shares.

## Credits

- The International Color Consortium's [How to interpret the sRGB color space](https://www.color.org/sRGB.pdf)
  describes the sRGB transfer and the D65 white that **Preserve Lightness** reads the image with.
- The CIE's [CIE 1976 L*a*b* colour space](https://cie.co.at/publications/colorimetry-part-4-cie-1976-lab-colour-space-1)
  defines the L*, a* and b* that **Preserve Lightness** rebuilds the pixel from. The four protection methods are
  AfterNight's own formulas.
