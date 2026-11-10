---
kind: process
stage: finish
menu: Process > Morphology > Tighten Edges
summary: >-
  Darkens each sample by the strength of the edges around it, measured in four directions over three by three
  pixels, which contracts and hardens the edges of bright structures.
data_in: either
data_out: either
destructive: true
preview: real-time
sequences: true
masks: post-blend
external: none
glance:
  enabled: >-
    An image or a sequence view is active, no Tighten Edges window is open on it yet, and no window that needs the
    image for itself is open on it.
  inputs: >-
    One grayscale or RGB image or sequence with samples between 0 and 1, and an optional mask of the image's size on
    its view.
  outputs: >-
    The same view, replaced, with one history step. Its size, channels, pixel type and recorded processing state are
    kept. No file, preset or report is written.
  state: >-
    A linear or a stretched image, a star layer or a mask whose bright edges need tightening, after noise reduction.
derived: false
---
# Tighten Edges

Tighten Edges measures how strongly the image changes around each sample and darkens the sample by that much. On a
bright structure against a dark field, such as a star, the edge contracts and hardens. The measure is one fixed
neighbourhood of three by three pixels, so the window has no radius and no scale. It is no
[deconvolution](../../reference/glossary.md#deconvolution), no sharpening as in
[Unsharp Mask](../sharpening/unsharp-mask.md) and no [morphology](../../reference/glossary.md#morphology) with a shape
you choose.

**Apply** replaces the image or [sequence](../../reference/glossary.md#sequence) with the filtered one, keeps its size
and [pixel type](../../reference/glossary.md#pixel-type), and adds one step to its
[history](../../reference/glossary.md#history). The recorded state of the image, linear or nonlinear, is kept as it
was.

```figure
image: ../../assets/images/processes/color-adjustment/tighten-edges.webp
alt: >-
  The Tighten Edges window. On the left, the Value box at 0.50 and the Amount box at 1.00 with their sliders, then
  Target Image, Channel at RGB and the Preview box, ticked. On the right, a toolbar over the preview of a red nebula
  with many stars. Reset, Close and Apply run along the bottom.
caption: >-
  Tighten Edges on a stretched master with Value at 0.50 and Amount at 1.00. The preview shows what Apply writes.
pins:
  - {n: 2, x: 2.0, y: 82.7}
  - {n: 3, x: 2.0, y: 20.1}
  - {n: 4, x: 2.0, y: 25.1}
  - {n: 5, x: 33.0, y: 40.0}
  - {n: 6, x: 90.6, y: 96.0}
```

## Before you start

- Reduce the noise first. The filter darkens every local change: grain,
  [ringing](../../reference/glossary.md#ringing), [hot pixels](../../reference/glossary.md#hot-pixel), the steps of a
  [mask](../../reference/glossary.md#mask) and compression artifacts as well as edges.
- Work on a duplicate, a star layer or a mask whose edges need tightening. **Apply** replaces the image, and its
  history keeps the step before.
- A [linear image](../../reference/glossary.md#linear-image) is accepted, but the edges are easier to judge after a
  [stretch](../../reference/glossary.md#stretch).
- A mask on the view blends the result with the original afterwards, after **Amount**.

## Steps

1. **Open the window.** Make the image active and choose **Process > Morphology > Tighten Edges**. **Apply** stays
   disabled while **Value** is 0.
2. **Choose the channel.** Leave **Channel** at **RGB**, or compare it with **CIE Y (Luminance)** or
   **CIE L* (Lightness)** when colored rims appear.
3. **Set the strength.** Raise **Value** a little at a time. The preview follows, and the edges of the stars darken.
4. **Blend the result.** Lower **Amount** to mix the original back in when the effect is too strong.
5. **Check at 100 %.** Zoom the preview into small stars, both sides of a bright edge, the noise of the background and
   the four borders of the image. Press ++space++ to compare with the original.
6. **Apply.** Choose **Apply**. The edges in the view are tighter, and the history lists **Tighten Edges** with the
   value, the amount and, for a color image, the channel.

## Controls

**Target Image**, **Channel**, **Preview**, the preview pane, **Close** and the progress window of a sequence work as
in every process window: see [Process Windows](../../interface/process-windows.md). The entries below hold what is
particular to this window, which has no histogram.

```controls
window: Tighten Edges
controls:
  - name: Value
    kind: spinbox
    default: "0.00"
    widget: {min: 0, max: 2, step: 0.5, decimals: 2}
    effect: Sets how much of the edge map is taken from each sample. Higher darkens the edges more. At 0 nothing changes.
    range: The arrows change it by 0.01 below 1 and by 0.1 from 1. The slider moves in 1,000 steps of 0.002.
    enabled: At exactly 0, **Apply** is disabled. Disabled while Apply runs.
    tradeoff: Larger values darken and clip more edge samples, and strengthen the rims at the image's borders, ringing, noise and colored fringes. The work is the same at every value.
    scope: The preview and Apply, on what **Channel** selects, before **Amount** and the mask. Images and sequences.
    persistence: Until the window closes. **Reset** and a finished Apply return it to its default.
  - name: Amount
    kind: spinbox
    default: "1.00"
    widget: {min: 0, max: 1, step: 0.5, decimals: 2}
    effect: Mixes the filtered image with the original. At 1 the result is the filtered image, at 0 the original.
    range: The arrows change it as those of **Value** do. The slider moves in 1,000 steps of 0.001.
    enabled: It does not enable **Apply**. With **Value** above 0, Apply works at **Amount** 0 too, and records a step that changes nothing.
    tradeoff: It weakens the visible effect without changing the work. It cannot bring back what the filter clipped to black, only mix the original back in.
    scope: After the channel or component is put back, and before the mask on the view.
    persistence: Until the window closes. **Reset** and a finished Apply return it to its default.
  - name: Target Image
    kind: combo
    default: The active image
    options: [The active image, Every other open image or sequence the window can work on]
    effect: Chooses the image or sequence Apply replaces, with the mask on its view. The choices of **Channel** and the preview follow it.
    persistence: Until the window closes. **Reset** leaves it.
  - name: Channel
    kind: combo
    default: RGB
    options: [RGB, Red, Green, Blue, CIE L* (Lightness), CIE a*, CIE b*, CIE c*, CIE h*, CIE X, CIE Y (Luminance), CIE Z, HSL Hue, HSL Saturation, HSL Lightness, HSV Hue, HSV Saturation, HSV Value, YCbCr Luma, YCbCr Blue Chroma, YCbCr Red Chroma]
    effect: Chooses what is filtered. **RGB** filters the three channels, each with an edge map of its own. **Red**, **Green** or **Blue** filters that channel alone. Any other choice filters one component and converts back to RGB.
    enabled: For an RGB image. A grayscale image shows a disabled **Grayscale**. Disabled while Apply runs, which takes the choice it finds when you press **Apply**.
    tradeoff: With **RGB** the channels' edges differ, which can leave colored rims. A lightness or luminance component avoids them, but the conversion back can change colors, cuts an integer image at the ends of its type and leaves a floating-point image outside 0 to 1. A hue component is treated as a line, not as a circle.
    scope: The preview and Apply, on images and sequences. The history step names the choice.
    persistence: Until the window closes. **Reset** leaves it. A finished Apply or another target returns it to **RGB**.
  - name: Preview
    kind: check
    default: "On"
    effect: Shows the filtered image beside the controls, with **Amount** and the mask applied.
    enabled: It stays enabled while Apply runs.
    tradeoff: Each change filters the image again. A color component adds its two conversions, and the Autostretch display its stretch.
    scope: "The window only. The pane's toolbar offers **Display Mode**. **Preview area** is off when the window opens, and ticked it starts at 600 by 600 pixels in the middle of the image, or the whole image when that is smaller. While you drag a slider, the preview filters the part of the image the pane shows, at full scale."
    persistence: Until the window closes. **Reset** leaves it.
    note: The outermost pixels of a preview area are filtered against neighbours of 0, as the image's own border is, so they do not show what Apply writes there. A snapshot, named `<target> - Tighten Edges - SNAPSHOT`, keeps them as the preview shows them.
  - name: Reset
    kind: button
    default: ""
    effect: Returns **Value** to 0 and **Amount** to 1, and one preview follows.
    range: It leaves **Target Image**, **Channel**, **Preview**, the mask and the preview's toolbar as they are.
  - name: Apply
    kind: button
    default: ""
    effect: Filters the whole image, or every frame of the sequence, with the values it finds when you press it, and then returns **Value** and **Amount** to their defaults.
    enabled: When **Value** is not 0, whatever **Amount** is. While the run works, **Value**, **Amount**, **Target Image**, **Channel**, **Reset** and **Close** are disabled. **Apply** itself still looks enabled, but a second press does nothing.
    tradeoff: One pass over each plane filtered, then the blend of **Amount**, the mask and the conversions of a component.
    scope: The whole image, never limited by a preview area.
    persistence: "The history step is **Tighten Edges** and reads, for example, `Adjust value to 0.5, amount 1 in channel: RGB`. For a grayscale image it ends after the amount. The image keeps its header cards and its processing state, and the step records it neither as linear nor as nonlinear."
```

## How it works

1. The channel or component **Channel** selects is read in its own units and computed in single precision. A 64-bit
   floating-point image is narrowed for the computation.
2. Four kernels of three by three pixels measure the change across each sample: from top to bottom, from left to
   right and along the two diagonals. Their absolute responses are added and scaled by 0.05. That is the edge map.
3. **Value** times the edge map is taken from the sample, and the result is held to the full scale of the pixel type,
   0 to 1 for floating point. It is written back in the image's pixel type.
4. A component is converted back to RGB, **Amount** mixes the result with the original, and the mask on the view
   blends the two.
5. The image keeps its size, its pixel type, its [header cards](../../reference/glossary.md#header-card) and its
   [processing state](../../reference/glossary.md#processing-state).

### Model

For a plane \(S\) in [fractions of full scale](../../reference/glossary.md#fraction-of-full-scale), with \(K_1\) to
\(K_4\) the four kernels, each weighting its middle row, column or diagonal twice, the edge map is

\[ E = 0.05 \sum_{j=1}^{4} \bigl\lvert K_j \ast S \bigr\rvert \tag{1} \]

With \(v\) the value of **Value** and \(a\) the value of **Amount**, the filtered plane \(T\) and the result \(O\) are

\[ T = \min\bigl(1,\ \max(0,\ S - v\,E)\bigr), \qquad O = (1 - a)\,S + a\,T \tag{2} \]

Where \(T\) is not clipped, the sample loses \(a\,v\,E\). Where \(T\) has reached 0, a lower **Amount** mixes the
original back in but does not undo the clip. On a straight step between two flat levels, along the rows or the
columns, equation (1) gives both sides of the step half the height of the step. The filter only takes away: it adds
no detail and models no star profile. A bright structure contracts at its edge, and the dark side of the edge falls
toward black rather than being sharpened.

### Boundaries

- Every neighbour outside the image counts as 0, on all four sides, so strong settings draw a dark or colored rim
  along the image's borders. A [preview area](../../reference/glossary.md#preview-area) makes borders of its own.
- The neighbourhood is fixed. Structures broader than a few pixels change little, while fine noise and one-pixel
  stars are eroded.
- A sample that holds no number is not refused or cleaned. It stays in the result and can turn its neighbours'
  results into not-a-number. A pixel the image marks as invalid is left as it was and counts as 0 for its neighbours.
- At **Value** 0 a sample between 0 and 1 keeps its value. An integer result is rounded to the nearest value.
- A run that is cancelled stops between rows, so a frame stops promptly, and writes nothing.

### Worked example

The picture above shows the window on the linear Carina [master](../../reference/glossary.md#master) after Histogram
Transformation with **Shadows** and **Midtones** at 0.02, which leaves the background near 0.28, with **Value** at
0.50 and **Amount** at 1.00. At a sharp edge between that background and a star core at 1.00, equation (1) gives
\(E = 0.36\) on both sides. Equation (2) takes 0.18 from each: the star's edge falls to 0.82 and the background beside
it to 0.10, a dark rim. With **Amount** at 0.25, each side loses 0.045. A sample of 0.80 with \(E = 0.20\) becomes
0.70, or 0.775 at **Amount** 0.25. Where \(v\,E\) is larger than the sample, \(T\) is 0, and **Amount** 0.25 leaves
three quarters of the sample.

```figure
images:
  - image: ../../assets/images/processes/color-adjustment/tighten-edges-before.webp
    alt: A crop of the nebula at full resolution before the process, red and white clouds and many small stars on a gray background.
    label: Before
  - image: ../../assets/images/processes/color-adjustment/tighten-edges-after.webp
    alt: The same crop after the process, the stars smaller and harder with dark rims, the edges of the clouds crisper and the background a little darker and grainier.
    label: After
caption: >-
  A crop at 100 % of the middle of the stretched Carina master before and after Apply with Value at 0.50 and Amount at
  1.00. Both are shown in the Linear display mode, without a display stretch.
```

## Starting points

| Situation | Start | Then adjust |
| --- | --- | --- |
| A first try | **Value** from 0.10 to 0.25, **Amount** from 0.25 to 0.50 | If the edges barely change, raise **Value**. If dark rims, clipped detail or crunchy noise appear, lower it, then blend with **Amount**. |
| An RGB image | **RGB** beside **CIE Y (Luminance)** or **CIE L* (Lightness)** | A component can leave colored edges, and **RGB** unequal rims. Choose **Channel** first, then the two values. |
| Detail at its final scale | Judge at 100 %, at the scale you will deliver | Broad soft structures do not change. For broader scales, use another tool. |
| Through a mask | A soft mask and a modest **Amount** | Find the strength without the mask, then turn it on and check its boundary. |

## When it goes wrong

!!! warning "Check every border of the image"
    Neighbours outside the image count as 0, so a strong **Value** draws a dark or colored rim along each border.
    Lower **Value** or **Amount**, or crop the border after processing.

Apply is disabled
:   **Cause.** **Value** is exactly 0.
:   **Fix.** Set a **Value** above 0. **Amount** alone does not enable **Apply**.

Apply ran but nothing changed
:   **Cause.** **Amount** is 0, or the channel selected has hardly any edges.
:   **Fix.** Raise **Amount**, or choose the channel you meant.

The edge of a preview area looks different after Apply
:   **Cause.** The area's outermost pixels were filtered against neighbours of 0.
:   **Fix.** Judge the inside of the area, then untick **Preview area** and check the whole image.

Colored fringes in an RGB image
:   **Cause.** The red, green and blue each have an edge map of their own.
:   **Fix.** Try **CIE Y (Luminance)** or **CIE L* (Lightness)** on a duplicate, or lower **Value** and **Amount**.

Dark halos and grainy background
:   **Cause.** **Value** is too high for the noise of the image, and the dark side of every edge falls toward black.
:   **Fix.** Lower **Value**, blend with **Amount**, or reduce the noise first.

## Related

- [Unsharp Mask](../sharpening/unsharp-mask.md), to sharpen at a Gaussian scale you choose, by adding detail on both
  sides of an edge.
- [Morphological Transformation](../masking/morphological-transformation.md), to erode, dilate, open or close with a
  [structuring element](../../reference/glossary.md#structuring-element) you choose.
- [Profiled Starmask Recombination](../star-operations/profiled-starmask-recomb.md), which applies the same filter to
  a star layer before it adds the layer back.
- [Image History](../../interface/image-history.md) and [Image Statistics](../../interface/image-statistics.md), to
  undo the step and to check the range of the result.
- [Process Windows](../../interface/process-windows.md), for the preview, its toolbar and what every process window
  shares.
