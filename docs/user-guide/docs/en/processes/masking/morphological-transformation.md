---
kind: process
stage: any
menu: Process > Morphology > Morphological Transformation
summary: >-
  Grows, shrinks, opens or closes the bright and dark structures of an image or a mask by replacing each sample with
  the minimum, the maximum or another rank of the samples a structuring element covers.
data_in: either
data_out: either
destructive: true
preview: real-time
sequences: true
masks: post-blend
external: none
glance:
  enabled: >-
    An image or a sequence view is active, no Morphological Transformation window is open on it yet, and no window
    that needs the image for itself is open on it.
  inputs: >-
    One grayscale or RGB image or sequence, often a mask, and an optional mask of the image's size on its view.
  outputs: >-
    The same view, replaced, with one history step. Its size, channels, pixel type, header cards and recorded
    processing state are kept. No file is written, and no mask is made or attached.
  state: >-
    A mask, or a duplicate of an image, whose bright or dark structures must grow, shrink or be cleaned, linear or
    stretched.
derived: false
---
# Morphological Transformation

Morphological Transformation applies [morphology](../../reference/glossary.md#morphology) to an image: it reshapes its
bright and dark structures. Each sample is replaced by the smallest, the largest or another rank of the samples a
[structuring element](../../reference/glossary.md#structuring-element) covers around it. An
[erosion](../../reference/glossary.md#erosion) shrinks bright structures, a
[dilation](../../reference/glossary.md#dilation) grows them, an opening removes bright details smaller than the
element and a closing fills dark gaps smaller than it. You draw the element on a grid of up to 25 by 25 cells.

**Apply** replaces the image or [sequence](../../reference/glossary.md#sequence) with the result, keeps its size,
channels and [pixel type](../../reference/glossary.md#pixel-type), and adds one step to its
[history](../../reference/glossary.md#history). The recorded state of the image, linear or nonlinear, is kept as it
was. No [mask](../../reference/glossary.md#mask) is made or attached.

```figure
image: ../../assets/images/processes/masking/morphological-transformation.webp
alt: >-
  The Morphological Transformation window. On the left, the Morphological Filter group with Operator at Opening,
  Iterations at 1, Amount at 1.00 with its slider and a grayed Selection box, then the Structuring Element group with
  a seven by seven grid whose white cells form a disc, the Size box at 7 and seven small icon buttons, then Target
  Image, Channel at RGB and the Preview box, ticked. On the right, a toolbar over the preview of a red nebula with
  fewer stars than the original. Reset, Close and Apply run along the bottom.
caption: >-
  Morphological Transformation on the stretched Carina master with Opening and a circular element of size 7. The
  preview shows what Apply writes: the small stars are gone and the clouds keep their shape.
pins:
  - {n: 2, x: 2.0, y: 20.5}
  - {n: 3, x: 2.0, y: 56.0}
  - {n: 4, x: 2.0, y: 25.0}
  - {n: 5, x: 2.0, y: 29.5}
  - {n: 6, x: 8.6, y: 85.4}
  - {n: 8, x: 89.2, y: 96.0}
```

## Before you start

- Work on a duplicate. **Apply** replaces the image, and its history keeps the step before.
- To change a mask, make the mask image itself the target. The window makes no mask and attaches none.
- Turn off a mask on the target's view when the whole image must change. That mask blends the original back in, and
  leaves gray values where it is not white.
- Look at the grid itself, not only at **Size**. A larger size keeps the old cells in the middle and adds cells that
  are off around them.
- Keep the center cell on for the usual centered operation.

## Steps

1. **Open the window.** Make the image the [active image](../../reference/glossary.md#active-image) and choose
   **Process > Morphology > Morphological Transformation**. The preview shows the image eroded by a filled square of
   3 by 3 cells.
2. **Choose the operator.** In **Operator**, choose what the element does: shrink, grow, open, close, or take the
   rank **Selection** sets.
3. **Shape the element.** Choose **Size**, then a shape with one of the buttons beside the grid, or click cells to
   turn them on and off. The cells that are on show white.
4. **Set the reach.** Leave **Iterations** at 1. Raise it only to repeat the same element farther.
5. **Soften the result.** Lower **Amount** to mix the original back in when the full result is too abrupt.
6. **Choose the channel.** Leave **Channel** at **RGB** to process each color channel on its own, or choose one
   component to process it alone.
7. **Check at 100 %.** Zoom the preview to 100 % on edges, on structures near the size of the element and on the
   borders of the image. Hold ++space++ to compare with the original.
8. **Apply.** Choose **Apply**. The image in its view is the reshaped one, its history lists **Morphological
   Transformation** with the operator, the iterations and the amount, and the controls return to their defaults.

## Controls

**Target Image**, the preview pane, **Close** and the progress window of a sequence work as in every process window,
and so does **Channel**: see
[Process Windows](../../interface/process-windows.md). The window has no histogram.

```controls
window: Morphological Transformation
controls:
  - name: Operator
    kind: combo
    default: Erosion (Minimum)
    options: [Erosion (Minimum), Dilation (Maximum), Opening (Dilation + Erosion), Closing (Erosion + Dilation), Morphological Selection]
    effect: "**Erosion (Minimum)** takes the smallest sample the element covers, so bright structures shrink and dark ones grow. **Dilation (Maximum)** takes the largest, so bright structures grow. **Opening** erodes and then dilates, which removes bright details the element does not fit into and gives the rest back its size. **Closing** dilates and then erodes, which fills dark gaps and holes smaller than the element. **Morphological Selection** takes the rank **Selection** sets."
    range: The parentheses of the two labels name the steps in reverse order. An opening erodes first, and a closing dilates first.
    enabled: Disabled while Apply runs.
    tradeoff: An opening and a closing run both operations, which doubles the work. **Morphological Selection** ranks every covered sample, which costs more than a minimum or a maximum.
    scope: The preview and Apply, on what **Channel** selects, before **Amount** and the mask.
    persistence: Until the window closes. **Reset** and a finished Apply return it to **Erosion (Minimum)**. The history step names it.
  - name: Iterations
    kind: spinbox
    default: "1"
    widget: {min: 1, max: 20, step: 1}
    effect: Repeats the operation with the same element, which reaches farther. An opening and a closing repeat each of their two steps, so an opening at 2 erodes twice and then dilates twice. That is not two openings in a row.
    range: Every repetition is applied.
    enabled: Disabled while Apply runs.
    tradeoff: The work and the effect grow with the count. A larger element changes which neighbours count and can carry a shape or a direction, while more iterations repeat the same footprint.
    scope: The preview and Apply.
    persistence: Until the window closes. **Reset** and a finished Apply return it to 1.
  - name: Amount
    kind: spinbox
    default: "1.00"
    widget: {min: 0, max: 1, step: 0.05, decimals: 2}
    effect: Mixes the result with the original. At 1 the image is the full result, at 0 the original.
    range: The slider moves in 1,000 steps of 0.001.
    enabled: Disabled while Apply runs. It does not disable **Apply**.
    tradeoff: The whole operation runs at every **Amount**, 0 included, and the mix adds a pass. Below 1, a binary mask gains gray values.
    scope: After the channel or component is put back, and before the mask on the view.
    persistence: Until the window closes. **Reset** and a finished Apply return it to 1.
  - name: Selection
    kind: spinbox
    default: "0.50"
    widget: {min: 0, max: 1, step: 0.05, decimals: 2}
    effect: Sets the rank **Morphological Selection** takes among the samples the element covers. At 0 it is the smallest, as an erosion, at 1 the largest, as a dilation, and at 0.5 the median.
    range: Between two ranks the value is interpolated in a straight line, so the result need not be one of the covered samples. At 0.5 over a filled square it is the median of that size, as Median Filter gives. The slider moves in steps of 0.001.
    enabled: Only with **Morphological Selection**. Disabled while Apply runs.
    tradeoff: The ranking grows with the number of cells that are on. A binary mask can gain values between its two levels.
    persistence: Until the window closes. **Reset** and a finished Apply return it to 0.5. The history step records it for **Morphological Selection** only.
  - name: Size
    kind: combo
    default: 3 (9 elements)
    options: [3 (9 elements), 5 (25 elements), 7 (49 elements), 9 (81 elements), 11 (121 elements), 13 (169 elements), 15 (225 elements), 17 (289 elements), 19 (361 elements), 21 (441 elements), 23 (529 elements), 25 (625 elements)]
    effect: Sets the side of the grid, an odd number of cells, with the number of its cells in parentheses.
    range: A larger size keeps the cells in the middle and adds cells that are off around them. A smaller size keeps the middle of the grid. For a larger shape, press a shape button or **Fill grid** afterwards.
    enabled: Disabled while Apply runs.
    tradeoff: A larger footprint reaches farther and costs more, in proportion to the cells that are on.
    persistence: Until the window closes. **Reset** and a finished Apply return it to 3, with every cell on.
  - name: Clear grid
    kind: button
    default: ""
    effect: Turns every cell off. Its icon is a bin.
    enabled: Disabled while Apply runs.
    tradeoff: An element with no cell on does nothing. **Apply** is then disabled, and the preview shows the image unchanged until a cell is on.
  - name: Fill grid
    kind: button
    default: ""
    effect: Turns on every cell of the square. Its icon is a paint bucket.
    enabled: Disabled while Apply runs.
    tradeoff: The square footprint grows and shrinks the most for its size, and has the most cells to read.
  - name: Invert
    kind: button
    default: ""
    effect: Turns every cell over. Its icon is a half-filled circle.
    enabled: Disabled while Apply runs.
    tradeoff: It can turn the center off, or leave a footprint in pieces.
  - name: Circular structure
    kind: button
    default: ""
    effect: Replaces the grid with a filled disc. Its icon is a circle.
    range: A cell is on when its squared distance from the center is at most r² + r, with r half of one less than the side. Size 7 gives 37 cells of 49.
    enabled: Disabled while Apply runs.
    tradeoff: The growth and the shrinking are nearly the same in every direction.
  - name: Diamond structure
    kind: button
    default: ""
    effect: Replaces the grid with a filled diamond, the cells whose row and column distances from the center add up to at most half of one less than the side. Its icon is a diamond.
    enabled: Disabled while Apply runs.
    tradeoff: It reaches farther along the rows and columns than along the diagonals.
  - name: Cross structure
    kind: button
    default: ""
    effect: Turns on the middle row and the middle column. Its icon is a plus sign.
    enabled: Disabled while Apply runs.
    tradeoff: It joins and grows structures along the rows and columns only, which can leave jagged edges.
  - name: Diagonal structure
    kind: button
    default: ""
    effect: Turns on the two diagonals, an X with the center on. Its icon is an X.
    enabled: Disabled while Apply runs.
    tradeoff: It joins and grows structures along the diagonals only.
  - name: Target Image
    kind: combo
    default: The active image
    options: [The active image, Every other open image or sequence the window can work on]
    effect: Chooses the image or sequence Apply replaces, with the mask on its view. The choices of **Channel** and the preview follow it.
    tradeoff: Apply replaces the image chosen here. Check it before you apply.
    persistence: Until the window closes. **Reset** leaves it as it is.
  - name: Channel
    kind: combo
    default: RGB
    options: [RGB, Red, Green, Blue, CIE L* (Lightness), CIE a*, CIE b*, CIE c*, CIE h*, CIE X, CIE Y (Luminance), CIE Z, HSL Hue, HSL Saturation, HSL Lightness, HSV Hue, HSV Saturation, HSV Value, YCbCr Luma, YCbCr Blue Chroma, YCbCr Red Chroma]
    effect: Chooses what is processed. **RGB** processes each color channel on its own. **Red**, **Green** or **Blue** processes that channel and copies the other two. Any other choice processes one component and converts back to RGB.
    enabled: For an RGB image. A grayscale image shows a disabled **Grayscale**. Disabled while Apply runs.
    tradeoff: With **RGB** the channels' edges move differently, which can color them. A hue is a circle, but its minimum and maximum are taken as numbers, so colors near the wrap can jump.
    scope: The preview and Apply, on images and sequences. The history step names it for a color image.
    persistence: Until the window closes. **Reset** leaves it. A finished Apply or another target returns it to **RGB**.
  - name: Preview
    kind: check
    default: "On"
    effect: Shows the result beside the controls, with **Amount** and the mask applied.
    enabled: It stays enabled while Apply runs.
    tradeoff: Each change of a value or of the grid processes the image again. Turn it off while you edit the grid at length, or draw a **Preview area**, then check the whole image before Apply.
    scope: "The window only. The pane's toolbar offers **Display Mode**, whose Autostretch can exaggerate faint changes. While you drag a slider, the preview processes the part of the image the pane shows, at full scale. **Preview area** is off when the window opens, and ticked it starts at 600 by 600 pixels in the middle of the image."
    persistence: Until the window closes. **Reset** leaves it as it is.
  - name: Close
    kind: button
    default: ""
    effect: Closes the window without applying.
    enabled: Disabled while Apply runs.
  - name: Reset
    kind: button
    default: ""
    effect: Returns **Operator** to **Erosion (Minimum)**, **Iterations** to 1, **Amount** to 1, **Selection** to 0.5 and the element to a filled square of 3 by 3 cells, and runs a preview.
    range: It leaves **Target Image**, **Channel** and **Preview** as they are.
    enabled: Disabled while Apply runs.
  - name: Apply
    kind: button
    default: ""
    effect: Processes the whole image, or every frame of the sequence, with the values, the element and the channel as they are when you press it, then returns the controls to their defaults.
    enabled: While the element has at least one cell on, as it has when the window opens. While the run works, every control of the window but **Preview** is disabled.
    tradeoff: The work grows with the pixels, the channels, the cells that are on and the iterations. A 25 by 25 element at 20 iterations costs far more than the default, and a color component adds its conversions.
    scope: The whole image or sequence, never limited by a preview area, with the mask blended afterwards. Each frame of a sequence keeps its own metadata, and **Cancel** stops the work inside the frame being processed. The progress window counts frames, not the work inside one, so try a short duplicate sequence first.
    persistence: "The history step is **Morphological Transformation** and reads, for example, `Operator: Opening (Dilation + Erosion), Iterations: 1, Amount: 1.00 in channel: RGB`, with `, Selection: 0.50` after the amount for **Morphological Selection**. For a grayscale image it ends before ` in channel`. It does not record the size or the cells of the element. The image keeps its header cards and its processing state, and the step records it neither as linear nor as nonlinear."
```

```controls
window: The structuring element
controls:
  - name: Grid
    kind: action
    default: ""
    effect: Shows the element, its cells that are on in white. A click with the left button turns one cell on or off, and a preview follows.
    range: The center cell is the anchor, which stays fixed. The element need not have its center on.
    enabled: Disabled while Apply runs.
    tradeoff: An element that is not symmetric shifts the result toward one side of each structure.
    scope: Every channel or component processed.
    persistence: Each run takes a copy of the element as it is when it starts. **Reset** and a finished Apply return it to a filled square of 3 by 3 cells.
```

## How it works

1. **Channel** decides what is processed: each color channel, one channel, or one component of the image converted to
   a [color model](../../reference/glossary.md#color-model).
2. Around each sample, the element is centered on its middle cell, and the samples its cells cover are read. An
   erosion takes the smallest, a dilation the largest and a selection the rank you set. An opening and a closing
   chain the two, each step repeated **Iterations** times.
3. The work is done in single precision, in the image's stored units, and written back in its pixel type.
4. A component is converted back to RGB, **Amount** mixes the result with the original, and the mask on the view
   blends the two.
5. The image replaces the target and its history gets one step. It keeps its size, its pixel type, its
   [header cards](../../reference/glossary.md#header-card) and its
   [processing state](../../reference/glossary.md#processing-state).

### Model

With \(S\) the cells that are on, as offsets from the center, and \(X(p)\) a channel at pixel \(p\), an erosion
\(E\) and a dilation \(D\) give

\[ E(p) = \min_{q \in S} X(p + q), \qquad D(p) = \max_{q \in S} X(p + q) \tag{1} \]

With \(n\) the value of **Iterations**, an opening \(O\) and a closing \(C\) are

\[ O = D^{n}\bigl(E^{n}(X)\bigr), \qquad C = E^{n}\bigl(D^{n}(X)\bigr) \tag{2} \]

so an opening never brightens a sample and a closing never darkens one. With \(s\) the value of **Selection** and
\(Q_s\) the value at rank \(s\) of a set of samples, interpolated between two ranks, a morphological selection gives

\[ R(p) = Q_s\bigl\{X(p + q) : q \in S\bigr\} \tag{3} \]

At \(s = 0\) it equals \(E\), at \(s = 1\) it equals \(D\). With \(a\) the value of **Amount** and \(M(X)\) the
result, the image becomes

\[ Y = (1 - a)\,X + a\,M(X) \tag{4} \]

and a mask on the view then blends \(Y\) with \(X\) in the same way, by its own value at each pixel.

### Boundaries

- A neighbour outside the image is left out. An erosion draws no dark rim and a dilation no bright one along the
  borders, and a selection ranks only the cells inside the image.
- A sample that is not a number, or is infinite, in an image that states every sample valid, is treated as missing.
  It is left out of every neighbourhood and left where it is. An image with a
  [validity rule](../../reference/glossary.md#validity-rule) of its own is read under that rule, and a pixel the rule
  rejects is left as it was.
- A 32-bit integer or 64-bit floating-point image is rounded to single precision for the work.
- An element with no cell on changes nothing.
- A run the memory budget cannot hold is refused with the reason, and the image stays as it was. Close other images,
  or use a smaller element.

### Worked example

The picture above shows the window on the linear Carina [master](../../reference/glossary.md#master) after Histogram
Transformation with **Shadows** and **Midtones** at 0.02, which leaves the background near 0.28. **Operator** is at
**Opening (Dilation + Erosion)**, **Size** at 7, the element made with **Circular structure**, **Iterations** at 1
and **Amount** at 1.00. The disc has 37 cells and reaches 3 pixels from its center. A star narrower than the disc
holds no place where the whole disc fits inside it, so equation (1) erodes it to the darker sky around it, and the
dilation of equation (2) cannot bring it back. A cloud wider than the disc keeps its shape and loses only its
narrower peaks, and the grain of the background is flattened and darkened a little.

```figure
images:
  - image: ../../assets/images/processes/masking/morphological-transformation-before.webp
    alt: A crop of the nebula at full resolution before Apply, red and white clouds with many small stars on a gray background.
    label: Before
  - image: ../../assets/images/processes/masking/morphological-transformation-after.webp
    alt: The same crop after Apply, the clouds in the same places, the small stars gone, the large ones smaller and rounder, and the background a little darker and smoother.
    label: After
caption: >-
  A crop at 100 % of the middle of the stretched Carina master before and after Apply with Opening and a circular
  element of size 7. Both are shown in the Linear display mode, without a display stretch.
```

On a black mask with the default filled square of 3 by 3 cells, a single white pixel vanishes under an erosion or an
opening, and becomes a white block of 3 by 3 pixels under a dilation. At **Amount** 0.50, equation (4) makes the
eight new pixels of that block 0.5. A single black pixel in a white field is filled by a closing.

## Starting points

| Situation | Start | Then adjust |
| --- | --- | --- |
| A white mask to shrink or grow | **Erosion (Minimum)** or **Dilation (Maximum)**, **Circular structure** at size 3 | **Size** first, then **Iterations**, then **Amount**. Watch thin parts vanish or nearby regions merge. |
| Isolated bright specks to remove | **Opening (Dilation + Erosion)**, **Circular structure** or **Cross structure** at size 3 | A larger element removes larger specks, and small details you want too. |
| Small dark holes or gaps to fill | **Closing (Erosion + Dilation)**, **Circular structure** at size 3 | Check that separate bright regions are not bridged. |
| A median through a shape | **Morphological Selection**, **Circular structure** at size 5, **Selection** at 0.5 | Shape and size first, then **Selection** toward 0 or 1. Detail smaller than half the element disappears. |

## When it goes wrong

!!! warning "The history does not record the element"
    The history step names the operator, the iterations, the amount and the channel, not the size or the cells of
    the element. Note the element, or keep a picture of the window, when you must repeat the step.

Opening and Closing seem to run in the order opposite to their labels
:   **Cause.** The parentheses of the labels name the steps in reverse order.
:   **Fix.** Go by the name: an opening erodes first, a closing dilates first.

A larger Size barely changes the result
:   **Cause.** The new cells around the old element are off.
:   **Fix.** After you change **Size**, press a shape button or **Fill grid**.

Apply is disabled
:   **Cause.** No cell of the grid is on.
:   **Fix.** Click the center cell, or press a shape button.

Selection is grayed out
:   **Cause.** **Operator** is not **Morphological Selection**.
:   **Fix.** Choose **Morphological Selection** to use a rank.

The result is gray instead of black and white
:   **Cause.** **Amount** is below 1, or a mask on the view blended the original back in.
:   **Fix.** Set **Amount** to 1 and turn the mask off, then apply again to the original.

Colored edges appear in an RGB image
:   **Cause.** Each channel moved its edges on its own, or a component was converted back to RGB.
:   **Fix.** Look at each channel with the preview's **View Channel**. Try a lightness or luminance component instead.

Structures along the borders change unexpectedly
:   **Cause.** Part of the element lies outside the image there, and only the samples inside count.
:   **Fix.** Check the borders at 100 %, and crop them afterwards if they matter.

Samples that held no number are still there
:   **Cause.** They are treated as missing and left where they are.
:   **Fix.** Repair them before this step when the result must hold only numbers.

## Related

- [Binarize](binarize.md), to cut a soft mask to black and white before or after reshaping it.
- [Mask Drawing](mask-drawing.md) and [Star Mask](../star-operations/star-mask.md), to draw a mask by hand or make
  one from the stars.
- [Tighten Edges](../color-adjustment/tighten-edges.md), which darkens edges with a fixed filter of 3 by 3 pixels.
- [Median Filter](../noise-reduction/median-filter.md), for the median of a whole square.
- [Masks](../../interface/masks.md) and [Image History](../../interface/image-history.md), to attach the result to
  another image as its mask, and to undo the step.
- [Process Windows](../../interface/process-windows.md), for the preview, **Channel** and what every process window
  shares.

## Credits

- The [OpenCV image-filtering reference](https://docs.opencv.org/4.x/d4/d86/group__imgproc__filter.html) describes
  the erosion, dilation, opening and closing, the anchor at the element's center, the iterations and the default
  border that leaves outside samples out, which the engine's morphology follows.
