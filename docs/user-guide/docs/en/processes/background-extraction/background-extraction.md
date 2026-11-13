---
kind: process
stage: color
menu: Process > Background Modelization > Background Extraction
summary: >-
  Fits a smooth surface through background samples you place, by a grid or by clicking the preview, and subtracts it
  from the image or divides the image by it, to remove a gradient.
data_in: linear
data_out: linear
destructive: true
preview: static
sequences: true
masks: post-blend
external: none
glance:
  enabled: >-
    An image or a sequence view is active and no other process window is open on it. Background Extraction needs the
    image to itself, so no other window opens on it while it is open.
  inputs: >-
    One grayscale or RGB image, with or without alpha, of any pixel type, or a sequence, and an optional mask on its
    view. Preview and Apply need at least one sample.
  outputs: >-
    The same view, replaced, with one history step. Its size, channels and pixel type are kept. Apply to a still image
    can also open the fitted surface as a new image. No file is written.
  state: >-
    A calibrated, stacked and cropped linear master, before color calibration and the stretch.
derived: true
---
# Background Extraction

Background Extraction removes a smooth gradient, such as light pollution, moonlight or a vignette the flats left,
from samples of the background that you choose. You place square sample boxes with a grid or by clicking the preview.
The window measures the median of each box and fits a smooth surface through those medians, one for each channel.
It then subtracts the surface from the image, or divides the image by it.

**Apply** replaces the image or [sequence](../../reference/glossary.md#sequence) with the corrected one, keeps its
size, channels and [pixel type](../../reference/glossary.md#pixel-type), and adds one step to its
[history](../../reference/glossary.md#history). It can also open the fitted surface as an image of its own.

```figure
image: ../../assets/images/processes/background-extraction/background-extraction.webp
alt: >-
  The Background Extraction window. On the left, the Sample generation group with the Samples per Line and Grid
  Tolerance sliders at 20 and 2.00 and the Generate Samples and Clear Samples buttons. Under it, the Background
  Extraction group with Interpolation Method at Polynomial, Correction at Subtraction, a grayed Smoothing slider,
  Degree Order at 1, two unticked check boxes, Preview Display at Corrected image and the Preview button. Then the
  ticked Enable preview box and Target Image. On the right, the preview of a red nebula, with small yellow squares
  spread over the field. Reset, Close and Apply run along the bottom.
caption: >-
  Background Extraction on the linear Carina master after Generate Samples, with a plane fitted through the samples
  and the corrected image previewed.
pins:
  - {n: 1, x: 2.2, y: 88.8}
  - {n: 2, x: 3.0, y: 30.7}
  - {n: 3, x: 44.0, y: 55.0}
  - {n: 4, x: 3.0, y: 42.9}
  - {n: 5, x: 3.0, y: 69.7}
  - {n: 6, x: 3.0, y: 64.6}
  - {n: 7, x: 90.5, y: 91.6}
```

## Before you start

- Calibrate, register, stack and [crop](../geometry/crop.md) first. A black stacking border, a star, a galaxy halo or
  a dust cloud under a sample is fitted as background.
- Keep the image linear: remove the gradient before color calibration and the
  [stretch](../../reference/glossary.md#stretch). The result is not recorded as nonlinear.
- **Apply** replaces the image. Its history keeps the step before.
- The window needs the image to itself. Close the other process windows open on it first.
- A [mask](../../reference/glossary.md#mask) on the view keeps no pixel out of the fit. It only blends the result with
  the original afterwards, so turn it off while you place samples and judge the model.
- On a sequence, one set of sample positions serves every frame, and each frame is measured and fitted on its own.
  Place them on the frame the view shows, where every frame has background.

## Steps

1. **Open the window.** Make the linear [master](../../reference/glossary.md#master) the
   [active image](../../reference/glossary.md#active-image) and choose **Process > Background Modelization >
   Background Extraction**. The preview shows the image as it is, and **Preview** and **Apply** stay disabled until
   there is a sample.
2. **Place a grid.** Choose **Generate Samples**. Small yellow squares appear over the preview, except where a box is
   brighter than **Grid Tolerance** allows. If squares sit on the nebula, lower **Grid Tolerance** and generate again.
3. **Edit the samples.** Click the preview to add a sample where the background has none. Right-click a square on a
   star, the nebula or a border to remove it. Edit last: a new grid replaces every sample.
4. **Choose the surface.** For a smooth tilt, set **Interpolation Method** to **Polynomial**, with a low
   **Degree Order**. For a gradient that bends, keep **RBF** and its **Smoothing**. Keep **Correction** at
   **Subtraction** for sky glow.
5. **Preview.** Choose **Preview**. When the fit is done, the preview shows the corrected image.
6. **Check the model.** Set **Preview Display** to **Gradient model**. The surface should be smooth, without stars or
   the outline of the nebula. Return to **Corrected image**.
7. **Apply.** Tick **Output Background Model** to keep the surface, then choose **Apply**. The view shows the corrected
   image, its history lists **Background Extraction**, and the samples are cleared.

## Controls

The window has no histogram and no **Channel**: every channel is fitted and corrected. What every process window
shares is on [Process Windows](../../interface/process-windows.md).

```controls
window: Background Extraction
controls:
  - name: Samples per Line
    kind: spinbox
    default: "20"
    widget: {min: 5, max: 100, step: 1}
    effect: Sets how many boxes one row of the grid holds. The rows take the same spacing, as many as fit the height. Higher covers the frame more densely, lower leaves wider gaps.
    range: The row must fit the image, so this number times 25 pixels may not exceed the width less 2 pixels. One grid holds at most 4,096 boxes.
    tradeoff: More boxes follow the background more closely, cost more fitting time and land on more stars and nebula. **RBF** accepts at most 1,024 samples, and a dense spline grid on a large frame takes most of the run.
    scope: "**Generate Samples** only. It moves no sample already placed."
    persistence: Until the window closes. **Reset** and **Apply** return it to its default. The history records it, also when every sample was placed by hand.
  - name: Grid Tolerance
    kind: spinbox
    default: "2.00"
    widget: {min: 0.01, max: 6, step: 0.1, decimals: 2}
    effect: Sets how much brighter than the typical box a box may be and stay in the grid, in deviations of the boxes' medians. Higher keeps brighter boxes, lower drops more of them.
    range: A box at or below zero is always dropped. No limit drops a box for being dark.
    tradeoff: Too low leaves too few samples. Too high keeps boxes on stars and extended signal.
    scope: "**Generate Samples** only. A sample you click is kept whatever its level."
    persistence: Until the window closes. **Reset** and **Apply** return it to its default. The history records it.
  - name: Generate Samples
    kind: button
    default: ""
    effect: Places a centered grid of boxes of 25 by 25 pixels and keeps the boxes the tolerance allows. When it is done, they replace every sample the window holds, the ones you clicked meanwhile too.
    range: >-
      A grid that does not fit is refused with a message, and the samples stay as they were. A row wider than the
      image says "this many boxes of this size do not fit across the picture", more than 4,096 boxes "this grid holds
      more sites than a background fit accepts", and an image under 27 pixels on a side "the picture cannot hold one
      complete sample box with a margin".
    enabled: While no grid is on its way and no run works.
    tradeoff: It reads a full-size copy of the image, or of a color image's lightness, which takes memory and a moment on a large frame.
    scope: The image the view shows, or the frame it shows of a sequence. **Clear Samples**, **Reset** and another target drop a grid still on its way.
  - name: Clear Samples
    kind: button
    default: ""
    effect: Removes every sample and its square, and drops a grid still on its way.
    enabled: While at least one sample is placed and no run works.
    tradeoff: "**Preview** and **Apply** are disabled until a sample is placed again."
  - name: Interpolation Method
    kind: combo
    default: RBF
    options: [Polynomial, RBF]
    effect: >-
      Chooses the surface. **Polynomial** fits a surface of the degree **Degree Order** gives, by least squares, and
      passes through no sample. **RBF** fits a thin-plate spline that comes close to every sample, relaxed by
      **Smoothing**.
    enabled: "**Polynomial** enables **Degree Order**, and **RBF** enables **Smoothing**."
    tradeoff: >-
      **RBF** follows a gradient that bends, needs at least three samples that do not lie on one line and accepts at
      most 1,024. Its fit costs the cube of the number of samples, and its surface one term for each sample at every
      pixel of every channel. **Polynomial** has at most 15 terms, so it is cheaper, can miss a local change, and at a
      high degree can bend through the nebula.
    scope: Every channel, fitted on its own, in the preview and Apply.
    persistence: Until the window closes. **Reset** and **Apply** return it to **RBF**. A change does not run the preview again.
    note: Both surfaces measure the image from -1 to 1 along each side, so on an image that is not square a pixel along the longer side counts for less of a spline's distance than one along the shorter side.
  - name: Correction
    kind: combo
    default: Subtraction
    options: [Subtraction, Division]
    effect: >-
      **Subtraction** takes the surface away and adds back one level shared by every channel, the mean of all the
      samples' medians. **Division** divides by the surface and multiplies each channel by its own mean.
    tradeoff: >-
      **Subtraction** suits an added glow, **Division** a dimming such as a vignette. **Division** raises the noise
      where the surface is low, and divides by 0.000001 where the surface is lower still.
    scope: Every channel, in the preview and Apply, before the mask is blended. Both hold the result between 0 and 1.
    persistence: Until the window closes. **Reset** and **Apply** return it to **Subtraction**. A change does not run the preview again.
  - name: Smoothing
    kind: spinbox
    default: "0.50"
    widget: {min: 0, max: 1, step: 0.01, decimals: 2}
    effect: Sets how far the spline may leave the samples. Higher gives a smoother surface that relaxes toward a plane, lower follows each sample more closely.
    range: The relaxation grows tenfold for every third of the range, so 1 relaxes a thousand times more than 0.
    enabled: With **Interpolation Method** at **RBF**.
    tradeoff: Higher resists noisy samples and can miss a real bend of the background. Lower follows every sample, one on a star too.
    scope: The spline, in every channel.
    persistence: Until the window closes. **Reset** and **Apply** return it to its default. A change does not run the preview again.
  - name: Degree Order
    kind: spinbox
    default: "4"
    widget: {min: 1, max: 4, step: 1}
    effect: Sets the polynomial's degree. 1 is a tilted plane, 2 adds a curve, 3 and 4 add more bends.
    range: The samples must determine all of its 3, 6, 10 or 15 terms. A degree they do not determine is refused by name, and the image is left as it was.
    enabled: With **Interpolation Method** at **Polynomial**.
    tradeoff: A higher degree needs more samples spread over the frame, and can bend through real structure.
    scope: The polynomial, in every channel.
    persistence: >-
      **Reset** and **Apply** leave it as it is, and a new window opens at 4. After a change, switching **Preview
      Display** computes the preview again with the new degree.
  - name: Output Background Model
    kind: check
    default: "Off"
    effect: On, Apply also opens the fitted surface as a new image beside the target, named `<target> - GRADIENT`.
    range: For a still image only. A preview and a sequence open no model.
    tradeoff: The new image has the target's size, channels and pixel type, and the run holds the surface beside it, a 32-bit floating-point plane for each channel.
    scope: The new image is not blended through the mask, and its values are held between 0 and 1.
    persistence: Until the window closes. **Reset** and **Apply** turn it off.
    note: >-
      Its history starts with the step `BackgroundExtractionProcess: Image gradient created from <target>`. It keeps
      the target's acquisition record, plate solution and lineage, and none of its header cards. Its own processing
      state cards record it nonlinear, made by `background-extraction-gradient`.
  - name: Add dither
    kind: check
    default: "Off"
    effect: Adds a tiny fixed noise to every sample just before the correction, which breaks up the bands an integer image can show afterwards. It changes neither the samples' medians nor the surface.
    range: The noise spans 0.0001 around zero, so no sample moves by more than 0.00005. It depends only on the pixel's position, so every run gives the same result.
    tradeoff: Leave it off unless banding shows. It changes every corrected sample a little.
    scope: The corrected image, in every channel, in the preview and Apply.
    persistence: Until the window closes. **Reset** and **Apply** turn it off. After a change, switching **Preview Display** computes the preview again with it.
  - name: Preview Display
    kind: combo
    default: Corrected image
    options: [Corrected image, Gradient model]
    effect: Chooses what a computed preview shows, the corrected image or the fitted surface. Switching it after **Preview** fits the samples again and shows the other.
    enabled: >-
      With **Enable preview** on. Until **Preview** is pressed, the pane shows the image as it is, whichever is chosen.
      After a change of the samples, **Interpolation Method**, **Smoothing** or **Correction**, switching it changes
      nothing until **Preview** is pressed again.
    tradeoff: The model shows a surface that follows stars or nebula without opening an image. Showing it holds a 32-bit floating-point plane for each channel. An enabled mask blends the model with the image too.
    scope: The preview only, and **Take snapshot**, which keeps what the preview shows. Apply writes the corrected image.
    persistence: Until the window closes. **Reset** and **Apply** return it to **Corrected image**.
  - name: Preview
    kind: button
    default: ""
    effect: Measures the samples, fits the surface and shows the corrected image or the model of the whole image, as Apply computes it.
    enabled: With at least one sample, **Enable preview** on and no run working.
    tradeoff: Each press is the whole fit and correction of Apply. No waiting indicator turns while it works.
    scope: The preview only. There is no preview area.
    persistence: >-
      A change of a setting or of the samples leaves the last preview on the pane until you press it again. **Reset**,
      another target, a finished Apply and turning **Enable preview** off return the pane to the image as it is.
  - name: Enable preview
    kind: check
    default: "On"
    effect: Shows the preview pane, where the samples are drawn and clicked. Off hides it with the squares, and drops a computed preview.
    tradeoff: Off, you cannot click samples. **Generate Samples** and **Apply** still work.
    persistence: Until the window closes. **Reset** leaves it as it is.
    note: Once it has been turned off, **Preview** does nothing, and after you turn it on again **Preview** and **Preview Display** stay disabled. Open the window again to preview.
  - name: Target Image
    kind: combo
    default: The active image
    options: [The active image, Every other open image or sequence the window can work on]
    effect: Chooses the image Apply corrects. Choosing another clears the samples, and the preview shows the new image as it is.
    tradeoff: On a sequence, one set of positions serves every frame.
    persistence: Until the window closes.
  - name: Reset
    kind: button
    default: ""
    effect: Clears the samples, drops a grid on its way and returns the controls to their defaults. The preview shows the image as it is.
    range: It leaves **Degree Order**, **Enable preview** and the target as they are.
  - name: Apply
    kind: button
    default: ""
    effect: Measures the samples on the image, or on each frame of a sequence, fits, corrects and replaces the image. The samples are then cleared and the controls return to their defaults, except **Degree Order**.
    enabled: With at least one sample, whether or not the preview was computed.
    tradeoff: A progress window with **Cancel** follows the run, on a single image too. The run works on a copy of the image, and an integer image on a floating-point copy as well.
    scope: The whole image or sequence, every channel, with the mask blended afterwards.
    persistence: >-
      The history records the grid, the surface and the correction, as `Samples per Line: 20, Grid Tolerance: 2,
      Correction: Subtraction, Interpolation: RBF, Smoothing: 0.5`, with `Degree Order: 1` in place of the smoothing
      for a polynomial. No preset and no sample list is saved.
```

```controls
window: The samples on the preview
controls:
  - name: Sample squares
    kind: readout
    default: No square
    effect: Show each sample as a small yellow square at its place on the preview.
    enabled: While **Enable preview** is on.
    note: A sample is measured over a box of 25 by 25 pixels around its center, in every channel, at each preview and Apply. The window keeps its position only.
    persistence: Until **Clear Samples**, **Reset**, another target, a finished Apply or a new grid. Samples are never saved, and closing the window loses them.
  - name: Click
    kind: action
    default: ""
    effect: Adds a sample centered on the pixel you click.
    range: A sample whose box would reach past the image's edge is not kept, and its square disappears.
    enabled: While **Enable preview** is on.
    tradeoff: Samples placed by hand avoid objects, and can leave parts of the frame without any.
  - name: Right-click a square
    kind: action
    default: ""
    effect: Removes the sample of the square you click inside. A right-click elsewhere does nothing.
    tradeoff: A gap left by removed samples lets the surface bend into it, the more so at a high degree or a low **Smoothing**.
```

## How it works

1. **Generate Samples** reads the image in [fractions of full scale](../../reference/glossary.md#fraction-of-full-scale):
   a color image's CIE lightness L\*, scaled to 0 to 1, or a grayscale image as it is. It places a centered grid of
   boxes, takes each box's median and keeps the boxes equation (1) allows.
2. At **Preview** or **Apply**, each sample's median is measured again in every channel of the image, or of each frame
   of a sequence. A pixel that holds no number takes no part.
3. Each channel gets its own surface, fitted in double precision through its medians by equation (2) or (3), so the
   background's color balance changes too: check it afterwards.
4. The surface is evaluated at every pixel and removed by equation (5) or (6), and the result is held between 0 and 1.
   An integer image is written back to the nearest code, ties to even, and a pixel that holds no number is left as it
   was. The mask then blends the result with the original.
5. The image replaces the target with one history step. Its [header cards](../../reference/glossary.md#header-card),
   acquisition record, [plate solution](../../reference/glossary.md#plate-solution) and
   [processing state](../../reference/glossary.md#processing-state) are kept, so it is not recorded as nonlinear. Its
   [lineage](../../reference/glossary.md#lineage) gains an entry with the step's text.

### Model

Let \(\ell_k\) be the median lightness of grid box \(k\), \(L\) the median of all the \(\ell_k\), \(D\) the median of
\(|\ell_k - L|\), which is their [median absolute deviation](../../reference/glossary.md#median-absolute-deviation)
without the factor 1.4826, and \(t\) the value of **Grid Tolerance**. A box stays in the grid when

\[ 0 < \ell_k \le L + t\,D \tag{1} \]

Both surfaces read the pixel at column \(x\) and row \(y\) of an image \(W\) pixels wide and \(H\) high at
\(u = 2x/(W-1) - 1\) and \(v = 2y/(H-1) - 1\). With \(d\) the value of **Degree Order**, the polynomial of channel
\(c\) is

\[ G_c(u, v) = \sum_{i + j \le d} a_{ij}\, u^i v^j \tag{2} \]

whose coefficients \(a_{ij}\) are fitted by least squares, every sample with equal weight. The
[thin-plate spline](../../reference/glossary.md#rbf) of **RBF**, with \(r_k\) the distance from sample \(k\) in the
same coordinates, is

\[ G_c(u, v) = a_0 + a_1 u + a_2 v + \sum_k w_k\, r_k^2 \ln r_k \tag{3} \]

where the weights \(w_k\) sum to zero, also when each is multiplied by its sample's \(u\) or \(v\). With \(s\) the
value of **Smoothing** and \(\bar K\) the mean row sum of the matrix of \(r^2 \ln r\) between the samples, the fit adds

\[ \lambda = 10^{-4}\,\bar K\,10^{3(s - 0.5)} \tag{4} \]

to the diagonal of its system, which is solved by elimination with partial pivoting.

Let \(I_c\) be channel \(c\), \(\mu_B\) the mean of every sample's median over every channel, and \(\mu_c\) the mean
of channel \(c\) over the pixels that hold a number. **Subtraction** and **Division** give

\[ O_c = \min\bigl(1, \max(0,\ I_c - G_c + \mu_B)\bigr) \tag{5} \]

\[ O_c = \min\Bigl(1, \max\Bigl(0,\ \frac{I_c}{\max(G_c, 10^{-6})}\,\mu_c\Bigr)\Bigr) \tag{6} \]

So **Subtraction** puts back one level shared by every channel, and **Division** each channel's own mean. With
**Add dither**, \(I_c\) first becomes \(I_c + 0.0001\,\delta\), where \(\delta\) lies between -0.5 and 0.5 and depends
only on the pixel's position.

### Boundaries

- A sample whose box holds no number in some channel stops the run with "the sample box held no value the validity
  rule accepts". An image of more than four channels is refused.
- Every result is held between 0 and 1. Where the surface is at or below 0.000001, **Division** divides by
  0.000001, which sends those pixels to white.
- A stopped Apply writes nothing, adds no history step, opens no model and shows no error. A run the memory budget
  cannot hold is refused with a message that names the budget.

### Worked example

The master in the picture above is the linear Carina master, 7,246 by 5,429 pixels, whose background turns from
pink at the left to green at the right. **Generate Samples** at its defaults spreads 20 boxes across the width, 380
pixels apart, and 15 rows fit the height: 300 boxes. Equation (1) drops those on the bright heart of the nebula.
**Polynomial** with **Degree Order** at 1 fits one tilted plane to each channel, which leaves the nebula's structure
alone. With **Subtraction**, equation (5) brings every background pixel on its plane to \(\mu_B\), near 0.027, a
little under the image's median of 0.0277, and the color shift across the frame is gone.

```figure
images:
  - image: ../../assets/images/processes/background-extraction/background-extraction-before.webp
    alt: >-
      The whole frame before Apply in the view's Autostretch display: the red and white nebula on a background that
      turns from pink at the left to green-gray at the right.
    label: Before
  - image: ../../assets/images/processes/background-extraction/background-extraction-after.webp
    alt: The whole frame after Apply in the same display, the same nebula on an even gray background.
    label: After
caption: >-
  The Carina master before and after Apply with a plane fitted through the grid's samples. Both are the whole frame
  in the view's Autostretch display, which stretches each image by its own statistics.
```

## Starting points

| Situation | Start | Then adjust |
| --- | --- | --- |
| A gradient across a frame full of faint nebula | **Polynomial**, **Degree Order** at 1, the grid at its defaults | **Degree Order** at 2 if a curve remains. A spline through many samples follows the faint nebula too. |
| A light dome or a corner glow over a sparse field | **RBF** with **Smoothing** at 0.50, the grid at its defaults | Remove the samples on objects, then raise **Smoothing** if the model follows single samples. |
| Darker corners the flats did not correct | **Division** | Compare with **Subtraction** on the same samples, and keep the one whose model matches the pattern. |
| The grid lands on stars or nebula | A lower **Grid Tolerance**, or fewer **Samples per Line** | Right-click what remains, then preview again. |

## When it goes wrong

!!! warning "The surface cannot tell a gradient from faint nebula"
    Every sample on real signal is fitted as background and taken away with the gradient. Look at **Gradient model**
    before **Apply**, and remove the samples on the nebula, a galaxy's halo or dust.

!!! danger "Samples above 1 are cut to 1"
    The corrected image is held between 0 and 1. A floating-point master whose brightest stars are above 1 loses
    them, and a sample below 0 becomes 0. When those values matter, bring the image into 0 to 1 first, with
    [Pixel Math](../pixel-math/pixel-math.md) for example.

**Preview** or **Apply** stays disabled
:   **Cause.** The window holds no sample. **Preview** also stays disabled once **Enable preview** has been turned
    off in this window.
:   **Fix.** Choose **Generate Samples** or click the preview's background. To preview again, close the window and
    open it again.

"this many boxes of this size do not fit across the picture", or "this grid holds more sites than a background fit accepts"
:   **Cause.** **Samples per Line** times 25 pixels is wider than the image less 2 pixels, or the grid would hold more
    than 4,096 boxes. Nothing was placed.
:   **Fix.** Lower **Samples per Line**, at most to the image's width less 2, divided by 25.

**Generate Samples** places few samples or none
:   **Cause.** The boxes above the tolerance or at zero were dropped: the frame is mostly nebula, or its background
    sits at zero.
:   **Fix.** Raise **Grid Tolerance** a little, or click samples by hand. Bring a background at zero above it first.

"a polynomial of degree <n> needs samples that determine its <terms> terms"
:   **Cause.** The samples do not determine every term of **Degree Order**: too few of them, or too close to one
    line. The image is unchanged.
:   **Fix.** Add samples across the frame, lower **Degree Order**, or choose **RBF**.

"the thin-plate system these sites describe is singular", or "a thin-plate spline solves a system of one row per site and accepts fewer"
:   **Cause.** **RBF** has fewer than three samples, samples all on one line, two at one place, or more than 1,024.
:   **Fix.** Spread at least three samples over the frame and remove doubles, lower **Samples per Line**, or choose
    **Polynomial**.

The gradient model shows stars or the nebula
:   **Cause.** Samples sit on real signal: the tolerance let them in, or you clicked them there.
:   **Fix.** Lower **Grid Tolerance** or **Samples per Line**, right-click those samples, raise **Smoothing**, or try
    **Polynomial** at a low degree. Then press **Preview** again.

The preview does not change after a change of a setting
:   **Cause.** The preview is computed only when you press **Preview**. Until then it keeps the last result, or shows
    the image as it is.
:   **Fix.** Press **Preview** after each change.

**Division** leaves black, white or noisy areas
:   **Cause.** The surface is near zero there, or the gradient is an added glow rather than a dimming.
:   **Fix.** Return to **Subtraction**, or place samples so that the surface stays above zero everywhere.

## Related

- [Auto Gradient Removal](auto-gradient-removal.md), which finds the background without samples when placing clean
  ones is impractical.
- [Crop](../geometry/crop.md), to remove stacking borders before you sample.
- [Background Neutralization](../color-calibration/background-neutralization.md),
  [Photometric Color Calibration](../color-calibration/photometric-cc.md) and
  [Spectrophotometric Color Calibration](../color-calibration/spectrophotometric-cc.md), which come next, once the
  background is flat.
- [Image Statistics](../../interface/image-statistics.md), to compare the background's levels before and after.
- [Masks](../../interface/masks.md), for the mask the result is blended through.
- [Process Windows](../../interface/process-windows.md), for the preview pane, Apply and what every process window
  shares.

## Credits

- The sample box and its acceptance rule, and the thin-plate spline's kernel and smoothing, are derived from the
  background extraction of [Siril](https://gitlab.com/free-astro/siril/-/blob/07d942814b1fc1052765cf18fa9c524b4e8a269c/src/algos/background_extraction.c),
  Copyright (C) 2005-2011 Francois Meyer and Copyright (C) 2012-2026 team free-astro, under GPL-3.0-or-later, and are
  distributed under that licence. The polynomial, the solvers and the correction are AfterNight's.
  **About > About AfterNight** credits it.
