---
kind: process
stage: any
menu: Process > Mask Generation > Range Selection
summary: >-
  Makes a mask from the samples of the image that lie between two limits, with hard or soft edges, an optional blur
  and inversion, and opens it as a new image.
data_in: either
data_out: nonlinear
destructive: false
preview: real-time
sequences: true
masks: none
external: none
glance:
  enabled: >-
    An image or a sequence view is active, no Range Selection window is open on it yet, and no window that needs the
    image for itself is open on it.
  inputs: >-
    One grayscale or color image or sequence, of any pixel type. A mask on its view is not used.
  outputs: >-
    A new image or sequence named after the target with " - MASK", of the target's size, beside it. It has one channel
    of floating point with Lightness on a color image, and the target's channels and pixel type otherwise. The target
    is not changed.
  state: >-
    Any image whose structures of interest sit at known values, most often a stretched image, from which a mask for
    later work is wanted.
derived: false
---
# Range Selection

Range Selection makes a [range mask](../../reference/glossary.md#range-mask): a new image that is white where the
samples of the target lie between **Lower limit** and **Upper limit**, and black elsewhere. **Fuzziness** turns the
two edges of the range into ramps, **Smoothness** blurs the mask, **Screening** keeps the brightness of the samples
selected, and **Invert** swaps black and white. On a color image, **Lightness** makes one grayscale mask from the
lightness of each pixel.

**OK** opens the mask as a new image named `<target> - MASK` beside the target, and closes the window. The target
stays as it was. The mask is recorded as nonlinear.

```figure
image: ../../assets/images/processes/analysis/range-selection.webp
alt: >-
  The Range Selection window. On the left, Lower limit at 0.450000 and Upper limit at 1.000000 with their sliders,
  the Link Range Limits check box, Fuzziness at 0.00 and Smoothness at 4.00, the Screening, Lightness and Invert check
  boxes with Lightness ticked, then Target Image and Preview. On the right, the preview shows a grayscale mask: a
  black background with the bright clouds of the nebula in soft-edged white. Reset, OK and Cancel run along the
  bottom.
caption: >-
  Range Selection on the stretched Carina master with Lower limit at 0.45 and Smoothness at 4, on the lightness. The
  preview, in the Linear display mode, shows the mask OK opens.
pins:
  - {n: 2, x: 13.9, y: 44.5}
  - {n: 3, x: 2.0, y: 20.1}
  - {n: 4, x: 2.0, y: 29.9}
  - {n: 5, x: 2.0, y: 37.2}
  - {n: 6, x: 25.7, y: 44.5}
  - {n: 7, x: 91.1, y: 19.9}
  - {n: 8, x: 79.3, y: 96.0}
```

## Before you start

- Make the mask from an image whose structures stand apart in brightness. On a stretched image the limits are easy
  to set. On a [linear image](../../reference/glossary.md#linear-image) everything but the stars lies close to 0.
- The limits are [fractions of full scale](../../reference/glossary.md#fraction-of-full-scale), not of the image's
  own range. Nothing measures the background for you: read the values you need with Image Statistics or the lens.
- Decide whether the mask should follow the lightness or each channel. **Lightness** gives one grayscale mask from a
  color image. Without it, each channel is selected on its own and the mask is in color.
- A [mask](../../reference/glossary.md#mask) on the target's view is not used, and need not be disabled. The result
  is the range mask alone.
- The target is never changed. On a [sequence](../../reference/glossary.md#sequence), each frame gets the mask of its
  own samples.

## Steps

1. **Open the window.** Make the image the [active image](../../reference/glossary.md#active-image) and choose
   **Process > Mask Generation > Range Selection**. The preview is white wherever a sample lies between 0 and 1.
2. **Choose what is selected.** Leave **Lightness** ticked for one grayscale mask of a color image. Untick it to
   select each channel on its own.
3. **Set the range.** Raise **Lower limit** until the background turns black, and lower **Upper limit** to leave out
   what is too bright. Samples at either limit are selected.
4. **Slide the range, if you want.** Once a limit has moved, tick **Link Range Limits** to move both limits together
   at the width they have.
5. **Soften the edges.** Raise **Fuzziness** to turn the hard edges into ramps, then **Smoothness** to blur the mask.
6. **Choose the kind of mask.** Tick **Screening** to keep the brightness of the samples selected, and **Invert** to
   protect the range instead of selecting it.
7. **Check the whole mask.** Choose **Linear** in **Display Mode**, look at the edges at 100 %, and untick
   **Preview area** if you used one.
8. **Choose OK.** The mask opens beside the image as `<target> - MASK`, and the window closes.

## Controls

The preview's toolbar is the one every process window shares: see
[Process Windows](../../interface/process-windows.md). For a mask of the lightness it shows **Grayscale** in **View
Channel**. The window has no histogram.

```controls
window: Range Selection
controls:
  - name: Lower limit
    kind: spinbox
    default: "0.000000"
    widget: {min: 0, max: 1, step: 0.05, decimals: 6}
    effect: Sets the lowest sample selected. Higher leaves out more of the dark samples, and moves the lower ramp of **Fuzziness** up with it.
    range: The limit itself is selected. Raising it to **Upper limit** or above moves **Upper limit** up to it.
    enabled: Disabled while OK runs.
    tradeoff: Equal to **Upper limit**, only the samples of exactly that value are selected.
    scope: The preview, a snapshot and OK, on every channel or on the lightness.
    persistence: Until the window closes. **Reset** returns it to 0, and a new window starts at 0.
    note: The slider beside the box moves the same value in 1,000 steps.
  - name: Upper limit
    kind: spinbox
    default: "1.000000"
    widget: {min: 0, max: 1, step: 0.05, decimals: 6}
    effect: Sets the highest sample selected. Lower leaves out more of the bright samples, and moves the upper ramp of **Fuzziness** down with it.
    range: The limit itself is selected. Lowering it to **Lower limit** or below moves **Lower limit** down to it.
    enabled: Disabled while OK runs.
    scope: The preview, a snapshot and OK, on every channel or on the lightness.
    persistence: Until the window closes. **Reset** returns it to 1, and a new window starts at 1.
  - name: Link Range Limits
    kind: check
    default: "Off"
    effect: Keeps the distance between the two limits while you move either, so the range slides at a fixed width.
    range: The width is the one the limits have when you tick it. A limit that would pass 0 or 1 stops the range there.
    enabled: Disabled while the limits are exactly 0 and 1, so move one of them first. Disabled while OK runs.
    scope: The two limits only. The mask does not depend on it.
    persistence: Until the window closes. **Reset** unticks it.
  - name: Fuzziness
    kind: spinbox
    default: "0.00"
    widget: {min: 0, max: 1, step: 0.05, decimals: 2}
    effect: Turns the two edges of the range into ramps. At 0 the edges are hard. Higher widens both ramps, and at 1 they meet in the middle of the range, where only one value is fully white.
    range: Each ramp is a share of half its value of the range's width. With the two limits equal it has no effect.
    enabled: Disabled while OK runs.
    tradeoff: Wider ramps narrow the fully white part of the range. With **Upper limit** at 1, a sample of exactly 1, such as a saturated star core, gets no weight.
    scope: Every channel or the lightness, before **Smoothness**.
    persistence: Until the window closes. **Reset** returns it to 0.
  - name: Smoothness
    kind: spinbox
    default: "0.00"
    widget: {min: 0, max: 32, step: 1, decimals: 2}
    effect: Blurs the mask with a Gaussian of this standard deviation, in pixels. At 0 the mask stays sharp. Higher softens the edges and fades small selected spots.
    range: The blur reaches four deviations from each pixel, and reads the image mirrored beyond its edges.
    enabled: Disabled while OK runs.
    tradeoff: Its work grows with the value. Higher spreads the selection past its structures into their surroundings. In an integer mask the soft values are rounded to the codes of the type.
    scope: The whole mask, every channel, after **Fuzziness** and **Screening** and before **Invert**.
    persistence: Until the window closes. **Reset** returns it to 0.
    note: The slider moves the value in steps of 0.1. While you drag a slider over a preview reduced to the pane, the blur is reduced with it.
  - name: Screening
    kind: check
    default: "Off"
    effect: Off makes the samples selected white. On keeps their own brightness, times the weight **Fuzziness** gives them, so the mask follows the image inside the range.
    enabled: Disabled while OK runs.
    tradeoff: A dim region in the range makes a weak mask there. It multiplies the samples, and is not the Screen blend mode.
    scope: Every channel or the lightness, before **Smoothness** and **Invert**.
    persistence: Until the window closes. **Reset** unticks it.
  - name: Lightness
    kind: check
    default: "On"
    effect: For a color image, selects on the CIE lightness of each pixel and makes one grayscale mask. Off selects on each channel of the image on its own and makes a mask with the image's channels.
    enabled: A grayscale image is selected on its one channel either way. Disabled while OK runs.
    tradeoff: The lightness is computed into a plane of its own, in 32-bit floating point, or 64-bit for a 64-bit floating-point or 32-bit integer image, whatever the image's pixel type.
    scope: The preview, a snapshot and OK, and each frame of a sequence.
    persistence: Until the window closes. **Reset** ticks it again.
    note: The lightness runs from 0 to 1. The red, green and blue are read as linear when the image is described as linear RGB, and as sRGB-encoded otherwise. A preview area on a color image shows the grayscale mask of the area over the image in gray.
  - name: Invert
    kind: check
    default: "Off"
    effect: Swaps black and white at the end, so the range is protected instead of selected.
    enabled: Disabled while OK runs.
    tradeoff: A sample that held no number is not selected, so after **Invert** it is white, or nearly so after **Smoothness**.
    scope: The whole mask, after **Smoothness**.
    persistence: Until the window closes. **Reset** unticks it.
  - name: Target Image
    kind: combo
    default: The active image
    options: [The active image, Every other open image or sequence the window can work on]
    effect: Chooses the image the mask is made from and named after.
    enabled: Disabled while OK runs.
    persistence: Until the window closes. **Reset** leaves it as it is.
  - name: Preview
    kind: check
    default: "On"
    effect: Shows the mask beside the controls, without the target's mask.
    tradeoff: While you drag a slider, the preview is computed on what the pane shows, the image reduced to the pane or the part shown at 100 % and closer with the blur's reach around it. When you let go, it is computed on the whole image, as OK computes it.
    persistence: Until the window closes. **Reset** leaves it as it is.
    note: Over a reduced image the limits and ramps are close but not exact, so a thin structure can be selected more or less than at full scale. A lightness mask of a color image in **Autostretch** is computed on the whole image at every step, unless the pane shows the image reduced to half its size or less. Ticked, **Preview area** is 600 by 600 pixels in the middle of the image, or the image's size when that is smaller. A snapshot is the mask the preview shows, with the area, and is not recorded as nonlinear or as a range selection.
  - name: OK
    kind: button
    default: ""
    effect: Makes the mask of the whole image, or of every frame of a sequence, opens it beside the target as `<target> - MASK` and closes the window.
    enabled: Disabled while it runs, with **Cancel**, **Reset** and every control of the window.
    tradeoff: A single image shows no progress window. Closing the window or the target stops the run, which then makes nothing and shows no message. A sequence is written to a new temporary sequence, opened only when every frame is done.
    scope: The whole image. A preview area does not limit it, and the mask on the target's view is not used.
    persistence: The new image's history starts with a **Range Selection** step without a description. The target and its history are not changed.
  - name: Reset
    kind: button
    default: ""
    effect: Returns the limits to 0 and 1 and **Fuzziness** and **Smoothness** to 0, ticks **Lightness**, and unticks **Screening**, **Invert** and **Link Range Limits**.
    range: It leaves **Target Image**, **Preview** and the preview area as they are.
```

## How it works

1. The image is read as fractions of full scale. With **Lightness** on, a color image becomes its CIE lightness, one
   plane from 0 to 1. Otherwise every channel is worked on its own, an integer one in a floating-point copy.
2. Every sample gets a weight: 1 inside the range, 0 outside it, and a ramp across each edge when **Fuzziness** is
   above 0. **Screening** multiplies the weight by the sample. A sample that holds no number, or that the image marks
   as invalid, gets 0.
3. **Smoothness** blurs the weights with a Gaussian, and **Invert** then takes each from 1.
4. Without **Lightness**, an integer mask is rounded back to the image's own type, to the nearest code, ties to the
   even one.
5. The mask opens as a new image. Its [processing state](../../reference/glossary.md#processing-state) is recorded as
   nonlinear with the operation `range-selection`, every sample is stated valid, and its
   [lineage](../../reference/glossary.md#lineage) gains an entry with the values, such as
   `0.45 to 1, fuzziness 0, smoothness 4, on the CIE lightness`. It keeps the target's acquisition record and
   [plate solution](../../reference/glossary.md#plate-solution). A lightness mask carries none of the target's other
   [header cards](../../reference/glossary.md#header-card), while a mask of the channels keeps them.

### Model

With \(x\) a sample, \(L\) and \(U\) the values of **Lower limit** and **Upper limit**, and \(f\) the value of
**Fuzziness**, each ramp is \(z\) wide:

\[ z = \frac{(U - L)\,f}{2} \tag{1} \]

and the weight of the sample is

\[
w(x) =
\begin{cases}
0, & x < L \ \text{or}\ x > U \\[0.5ex]
(x - L)/z, & L \le x < L + z \\[0.5ex]
1, & L + z \le x \le U - z \\[0.5ex]
(U - x)/z, & U - z < x \le U
\end{cases}
\tag{2}
\]

With \(f = 0\), every sample from \(L\) to \(U\) weighs 1. **Screening** writes \(x\,w\) in place of \(w\). With
\(v\) that value and \(G_s\) the Gaussian blur whose standard deviation \(s\) is **Smoothness**, the mask is

\[ m = G_s * v, \quad \text{or} \quad m = 1 - G_s * v \ \text{with **Invert**} \tag{3} \]

A single selected pixel in a black surround keeps about \(1 / (2\pi s^2)\) of its white at its center: 0.04 at
\(s = 2\) and 0.01 at \(s = 4\). That is why **Smoothness** clears isolated specks of noise while broad regions stay
white.

### Boundaries

- The limits are inclusive. With the two limits equal, only samples of exactly that value are selected, and
  **Fuzziness** has no effect.
- A sample outside 0 to 1 gets 0. On a linear [master](../../reference/glossary.md#master) the brightest stars,
  above 1, are black in the mask. A sample that holds no number gets 0 too, and **Smoothness** can spread that into
  its neighbours.
- A [preview area](../../reference/glossary.md#preview-area) is computed as an image of its own, so near its edges
  **Smoothness** reads the area mirrored, where OK reads the pixels beyond it.
- A [CFA](../../reference/glossary.md#cfa) mosaic is selected as one grayscale plane and is not debayered.
- A run the memory limit cannot hold fails with "Error executing the process" naming the memory budget, and makes
  nothing.

### Worked example

The image is the Carina master after Histogram Transformation with **Shadows** and **Midtones** at 0.02. Its
background, near 0.28 in each channel, has a lightness near 0.30. With **Lightness** on, **Lower limit** at 0.45,
**Upper limit** at 1 and **Smoothness** at 4, the background is black and the bright
clouds of the nebula are white, with soft edges. A speck of noise above 0.45 in the background keeps about 0.01 by
equation (3), and the small stars fade with it. **OK** opens the mask as a new grayscale image in 32-bit floating
point.

```figure
images:
  - image: ../../assets/images/processes/analysis/range-selection-before.webp
    alt: The whole stretched frame, the red and white clouds of the nebula across a field of stars on a dark gray background.
    label: Before
  - image: ../../assets/images/processes/analysis/range-selection-after.webp
    alt: The whole mask, black where the background was and white with soft edges where the bright clouds of the nebula were.
    label: After
caption: >-
  The stretched Carina Nebula master and the mask OK made of it with Lower limit at 0.45 and Smoothness at 4. Both are
  the whole frame in the Linear display mode, without a display stretch.
```

**Fuzziness** works on the width of the range. With the limits at 0.20 and 0.60 and **Fuzziness** at 0.20, equation
(1) gives ramps 0.04 wide. Samples of 0.20, 0.22, 0.24, 0.40, 0.56 and 0.60 then weigh 0, 0.5, 1, 1, 1 and 0.

## Starting points

| Situation | Start | Then adjust |
| --- | --- | --- |
| A mask of the bright nebula | **Lightness**, **Lower limit** just above the background, **Upper limit** at 1, **Smoothness** from 2 to 5 | Raise **Lower limit** until the background stays black at 100 %. |
| A soft interval of brightness | The hard interval first, then **Fuzziness** at 0.10 | **Smoothness** last, and only as much as the edges need. |
| A mask that follows the image's brightness | The interval with **Screening** off, then on, to compare | Keep **Screening** if the dimmer parts should be weaker in the mask. |
| A mask that protects the range | The mask without **Invert**, finished | Tick **Invert** once, at the end, and check which side is white. |

## When it goes wrong

!!! warning "Check the mask's polarity before you use it"
    White is where a later process acts. A mask the wrong way round applies the work to the part you meant to
    protect. Look at the mask in **Linear**, and tick **Invert** only when you mean to.

Link Range Limits is dimmed
:   **Cause.** The limits are exactly 0 and 1.
:   **Fix.** Move one limit inward first, then tick it.

The mask is white almost everywhere
:   **Cause.** **Lower limit** is below the background, or the limits are still 0 and 1.
:   **Fix.** Raise **Lower limit** until the background turns black.

Star cores are black or dark inside white stars
:   **Cause.** Their samples are above **Upper limit**, as the brightest stars of a linear image are above 1, or
    **Fuzziness** gives no weight at the upper limit.
:   **Fix.** Make the mask from a stretched image, and soften it with **Smoothness** rather than **Fuzziness**.

Fuzziness changes nothing
:   **Cause.** The two limits are equal, so the ramps have no width.
:   **Fix.** Widen the range before you raise **Fuzziness**.

The mask is dim inside the range
:   **Cause.** **Screening** is on, which keeps the brightness of the samples.
:   **Fix.** Untick **Screening** for a white mask.

A single white pixel in an inverted mask
:   **Cause.** That sample of the image held no number, so it was not selected, and **Invert** made it white.
:   **Fix.** Repair the image's sample first if that pixel matters.

The image around a preview area is gray
:   **Cause.** With **Lightness** on a color image, the area shows the grayscale mask over the image in gray.
:   **Fix.** Nothing to fix: untick **Preview area** to see the whole mask.

## Related

- [Masks](../../interface/masks.md), to attach the new mask to an image, enable it and invert it.
- [Image Statistics](../../interface/image-statistics.md), to read the values the limits should take.
- [Binarize](../masking/binarize.md), for a hard threshold of each channel without ramps.
- [Morphological Transformation](../masking/morphological-transformation.md), to grow, shrink or soften the mask
  further.
- [Pixel Math](../pixel-math/pixel-math.md), for a selection of your own or to combine this mask with another.
- [Process Windows](../../interface/process-windows.md), for the preview, its toolbar and what every process window
  shares.
