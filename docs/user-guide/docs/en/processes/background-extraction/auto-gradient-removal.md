---
kind: process
stage: color
menu: Process > Background Modelization > Auto Gradient Removal
summary: >-
  Estimates the large-scale background of a still image without samples, keeping bright structure out of the fit,
  and subtracts it from the image or divides the image by it, to remove a gradient.
data_in: linear
data_out: linear
destructive: true
preview: static
sequences: false
masks: post-blend
external: none
glance:
  enabled: >-
    A still image view is active and no other process window is open on it. Auto Gradient Removal needs the image to
    itself, so no other window opens on it while it is open. A sequence is refused.
  inputs: >-
    One grayscale or RGB still image, with or without alpha, of any pixel type, and an optional mask on its view. No
    sample and no reference image.
  outputs: >-
    The same view, replaced, with one history step. Its size, channels and pixel type are kept. On request, the
    gradient model and the protection mask open as new images. No file is written.
  state: >-
    A calibrated, stacked and cropped linear master, before color calibration and the stretch.
derived: true
rewritten: true
---
# Auto Gradient Removal

Auto Gradient Removal finds the background of an image by itself and removes it. It averages blocks of the image
into a working image, fits a smooth model to it, leaves out the pixels that stand above the model, and fits again
until the pixels it keeps stop changing. Bright extended structure can be kept out of the fit on purpose. The model
is then enlarged back to full size and subtracted from the image, or the image is divided by it.

**Apply** replaces the image with the corrected one, keeps its size, channels and
[pixel type](../../reference/glossary.md#pixel-type), and adds one step to its
[history](../../reference/glossary.md#history). It can also open the model and the protected areas as images of
their own.

```figure
image: ../../assets/images/processes/background-extraction/auto-gradient-removal.webp
alt: >-
  The Auto Gradient Removal window. On the left, the Background model group with Scale at 5.0, Smoothness at 1.00,
  Downsample at 4, the unticked Simplified polynomial model box and a grayed Polynomial degree slider. Under it, the
  Structure protection group with Protect bright structures ticked, Threshold at 0.050 and Amount at 0.50, the
  Correction group with Mode at Subtraction, Preview display at Gradient model and a grayed Preview button, and the
  Outputs group with two unticked boxes. Then the ticked Enable preview box and Target image. On the right, the
  preview shows a smooth blurred surface, reddish at the left and blue-green at the right. Reset, Close and Apply run
  along the bottom.
caption: >-
  Auto Gradient Removal on the linear Carina master at its defaults, with the preview computed and Preview display at
  Gradient model: the background the estimate found.
pins:
  - {n: 1, x: 36.6, y: 90.6}
  - {n: 2, x: 16.0, y: 66.3}
  - {n: 3, x: 2.6, y: 62.6}
  - {n: 5, x: 2.6, y: 18.2}
  - {n: 6, x: 91.5, y: 92.7}
```

## Before you start

- Calibrate, register, stack and [crop](../geometry/crop.md) first. A stacking border can be taken for part of the
  background.
- Keep the image linear: remove the gradient before color calibration and the
  [stretch](../../reference/glossary.md#stretch). The result is not recorded as nonlinear.
- **Apply** replaces the image. Its history keeps the step before.
- The window works on a still image and needs it to itself. Close the other process windows open on it first.
- A [mask](../../reference/glossary.md#mask) on the view keeps no pixel out of the estimate. It blends every preview
  display and the result with the original, so turn it off while you judge the model, and on again before **Apply**.
- On an image with broad real signal, plan to look at **Gradient model** and **Protection mask**. The protection
  finds what stands above the background, not what an object is.

## Steps

1. **Open the window.** Make the linear [master](../../reference/glossary.md#master) the
   [active image](../../reference/glossary.md#active-image) and choose **Process > Background Modelization > Auto
   Gradient Removal**. The preview shows the image as it is, and **Apply** is already enabled.
2. **Preview.** Choose **Preview**. When the estimate is done, the preview shows the corrected image, and the button
   stays disabled until a setting changes.
3. **Check the model.** Set **Preview display** to **Gradient model**. It shows at once: a smooth surface with the
   broad glow, without stars or the outline of the nebula.
4. **Check the protection.** Set **Preview display** to **Protection mask**. White marks what was kept out of the
   fit. Return to **Corrected image**.
5. **Adjust one group at a time.** Raise **Scale** if the model holds the nebula, lower it if a gradient remains. Lower
   **Threshold** to protect more. Choose **Preview** again after each change.
6. **Apply.** Tick **Create gradient model image** or **Create protection mask image** to keep them, then choose
   **Apply**. The view shows the corrected image, its history lists **Auto Gradient Removal**, and the controls return
   to their defaults.

## Controls

The window has no histogram and no **Channel**: every channel is estimated and corrected on its own. What every
process window shares is on [Process Windows](../../interface/process-windows.md).

```controls
window: Auto Gradient Removal
controls:
  - name: Scale
    kind: spinbox
    default: "5.0"
    widget: {min: 1, max: 10, step: 0.5, decimals: 1}
    effect: Sets the reach of the model, as a percentage of the working image's shorter side. Higher gives a broader model that follows only the widest changes, lower follows finer ones. It also sets how far the protection grows and, with **Smoothness**, the final smoothing.
    range: >-
      The radius is a whole number of working pixels, at least 1. A radius above 1,024 is refused with "the model
      radius this scale implies is larger than the working picture can usefully carry; raise the reduction factor or
      lower the scale", which only a very large frame at **Downsample** 1 or 2 reaches.
    enabled: With **Simplified polynomial model** on too, where it still sets the protection and the final smoothing.
    tradeoff: Too low lets the model take in real detail of the nebula. Too high can miss a compact gradient. Its cost does not grow with it, because each box pass is a running sum, kept in double precision.
    scope: Every channel, on its own, in the preview and Apply.
    persistence: Until the window closes. **Reset** and a finished Apply return it to its default.
  - name: Smoothness
    kind: spinbox
    default: "1.00"
    widget: {min: 0, max: 3, step: 0.1, decimals: 2}
    effect: Smooths the finished model with three more box passes, of a radius this many times the model's. Higher removes more texture from the model. At 0 the model is not smoothed.
    tradeoff: Higher can miss a faster change of the background.
    scope: Both models, after the fit, in every channel.
    persistence: Until the window closes. **Reset** and a finished Apply return it to its default.
  - name: Downsample
    kind: combo
    default: "4"
    options: ["1", "2", "4", "8"]
    effect: Sets how many pixels on a side one working pixel averages. Higher estimates on fewer pixels, and 1 works at full resolution. The corrected image and the outputs keep the full size.
    range: >-
      A partial block at the right or the bottom averages the pixels it covers. A frame shorter than the factor on a
      side is worked at the largest smaller factor that fits, and Apply warns "The image is too small to be
      downsampled by <n>; it was downsampled by <m>."
    tradeoff: >-
      Higher is faster and needs less memory, and can miss a smaller change of the background. 1 costs the most.
      Compare at 4 or 8 first, and go to 2 or 1 only when the model misses a change it should follow. The cost grows
      with the working pixels, the channels, the rounds, the protection and the polynomial's terms.
    scope: The estimate, in every channel.
    persistence: Until the window closes. **Reset** and a finished Apply return it to its default.
  - name: Simplified polynomial model
    kind: check
    default: "Off"
    effect: On, fits a polynomial surface to the kept pixels by least squares. Off, the multiscale model fills the pixels left out from their surroundings and smooths.
    enabled: It enables **Polynomial degree**.
    tradeoff: A polynomial suits a smooth plane or curve across the frame, and cannot follow a local glow. At a high degree it can follow real structure, or be poorly determined.
    scope: Every channel, in the preview and Apply.
    persistence: >-
      Until the window closes. **Reset** and a finished Apply turn it off. The history records `Model: Simplified
      polynomial, Degree: 2`, or `Model: Multiscale`.
  - name: Polynomial degree
    kind: spinbox
    default: "2"
    widget: {min: 1, max: 6, step: 1}
    effect: Sets the polynomial's total degree. 1 is a tilted plane, and each degree above it allows more curvature.
    range: Degree d has (d + 1)(d + 2)/2 terms, 28 at 6. A term the kept pixels do not determine is dropped and set to zero.
    enabled: With **Simplified polynomial model** on.
    tradeoff: More terms can follow the nebula, or bend where few pixels are kept.
    persistence: Until the window closes. **Reset** and a finished Apply return it to its default.
  - name: Protect bright structures
    kind: check
    default: "On"
    effect: Keeps bright structure out of every fit after the first. A pixel standing more than **Threshold** above the residual's center seeds an area, **Amount** grows it, and the area stays out of the fit, also when too few pixels are kept and the kept set is rebuilt.
    enabled: It enables **Threshold**, **Amount**, **Protection mask** in **Preview display** and **Create protection mask image**. Turned off, it clears **Create protection mask image** and moves **Preview display** off **Protection mask**.
    tradeoff: It keeps a bright extended object out of the model. Too much protection leaves too little background, and a fit left with none is refused.
    scope: The estimate only, in each channel on its own.
    persistence: >-
      Until the window closes. **Reset** and a finished Apply turn it on. The history records `Structure protection:
      enabled (threshold: 0.050, amount: 0.50)`, or `Structure protection: disabled`.
  - name: Threshold
    kind: spinbox
    default: "0.050"
    widget: {min: 0, max: 1, step: 0.005, decimals: 3}
    effect: Sets how far above the residual's center a pixel must stand to seed a protected area, in fractions of full scale. Lower protects more, higher less.
    enabled: With **Protect bright structures** on.
    tradeoff: Too low keeps ordinary background out of the fit. Too high lets bright nebula or a halo into the model. A linear master's nebula often stands only a few hundredths above its background, so check **Protection mask**. A stretched image is accepted, and the threshold is not adapted to it.
    scope: The protection, in each channel.
    persistence: Until the window closes. **Reset** and a finished Apply return it to its default.
  - name: Amount
    kind: spinbox
    default: "0.50"
    widget: {min: 0, max: 1, step: 0.05, decimals: 2}
    effect: Sets how far a protected area grows. Higher widens the growth and lowers the level the grown area must reach, from the bright cores toward the whole object and its halo.
    enabled: With **Protect bright structures** on.
    tradeoff: Too little leaves bright outskirts in the fit. Too much leaves too little background.
    scope: The protection, in each channel.
    persistence: Until the window closes. **Reset** and a finished Apply return it to its default.
  - name: Mode
    kind: combo
    default: Subtraction
    options: [Subtraction, Division]
    effect: >-
      **Subtraction** removes an added glow and puts back the model's median. **Division** divides by the model and
      multiplies by its median, for a dimming such as a vignette.
    tradeoff: "**Division** raises the noise where the model is low, and divides by 0.000001 where it is lower still. Both hold the result between 0 and 1."
    scope: The corrected image, every channel, in the preview and Apply. Each channel has its own model, so the background's color balance changes too, and is worth a check afterwards.
    persistence: Until the window closes. **Reset** and a finished Apply return it to **Subtraction**.
  - name: Preview display
    kind: combo
    default: Corrected image
    options: [Corrected image, Gradient model, Protection mask]
    effect: Chooses what a computed preview shows. One preview makes all three, so a switch shows the other at once, without computing again. It changes nothing Apply writes.
    enabled: >-
      With **Enable preview** on. **Protection mask** only with **Protect bright structures** on. Until **Preview** is
      pressed, and after a setting changes, a switch shows the image as it is.
    tradeoff: >-
      The computed preview holds its three images, full size beside the target, until a setting, the target or its
      mask changes or the preview is turned off. An enabled mask blends every display with the image.
    scope: >-
      The preview, and **Take snapshot**, which keeps the display shown. In the Autostretch display the corrected
      image is stretched by the target's statistics, so it compares with the original under one stretch, and the
      model and the protection mask by their own, which shows their broad pattern rather than a flat gray.
    persistence: Until the window closes. **Reset** and a finished Apply return it to **Corrected image**.
  - name: Preview
    kind: button
    default: ""
    effect: Runs the estimate and the correction over the whole image once, makes the corrected image, the model and, with protection, the protection mask, and shows the one **Preview display** names.
    enabled: With **Enable preview** on and no run working, while no preview stands for the settings. A change of a setting or of the target enables it again.
    tradeoff: It is the whole estimate of Apply. There is no preview area. For a cheap trial, work on a cropped copy, which cannot stand for the model of the whole frame.
    persistence: After a setting changes, the pane keeps the last result until you press **Preview** again, and a switch of **Preview display** before that shows the image as it is. **Reset**, another target and turning **Enable preview** off return the pane to the image as it is.
  - name: Create gradient model image
    kind: check
    default: "Off"
    effect: On, Apply also opens the model as a new image beside the target, named `<target> - AGR Gradient`.
    tradeoff: It is a full-size image of the target's channels and pixel type, drawn from the estimate the correction used.
    scope: Apply only. The image is not blended through the mask, and its values are held between 0 and 1.
    persistence: Until the window closes. **Reset** and a finished Apply turn it off.
    note: >-
      Its history starts with `AutoGradientRemoval: gradient model created from '<target>'`. It keeps the target's
      acquisition record, plate solution and lineage and none of its header cards, and its processing state cards
      record it nonlinear, made by `auto-gradient-removal-gradient`.
  - name: Create protection mask image
    kind: check
    default: "Off"
    effect: On, Apply also opens the protection as a new image, named `<target> - AGR Protection Mask`. It is 1 over the whole block of every working pixel kept out of the fit by the protection in any round, and 0 elsewhere.
    enabled: With **Protect bright structures** on. Turning the protection off clears it.
    tradeoff: It is a full-size image of the target's channels and pixel type. A color image gets one channel of protection for each of its channels, each found on its own.
    scope: Apply only, not blended through the mask.
    persistence: Until the window closes. **Reset** and a finished Apply turn it off.
    note: >-
      Its history starts with `AutoGradientRemoval: protection mask created from '<target>'`, and its processing
      state cards name `auto-gradient-removal-protection-mask`. It keeps what the gradient model image keeps.
  - name: Enable preview
    kind: check
    default: "On"
    effect: Shows the preview pane. Off closes it, drops the computed preview and disables **Preview display** and **Preview**. Apply still works.
    tradeoff: Off, it frees the images a computed preview holds.
    persistence: Until the window closes. **Reset** leaves it as it is.
  - name: Target image
    kind: combo
    default: The active image
    options: [The active image, Every other open still image the window can work on]
    effect: Chooses the image Apply corrects. A sequence is not offered. Choosing another returns the preview to that image as it is.
    tradeoff: A larger image takes more time and memory.
    persistence: Until the window closes.
  - name: Reset
    kind: button
    default: ""
    effect: Returns every control above to its default, except **Enable preview** and the target, and returns the preview to the image as it is.
  - name: Apply
    kind: button
    default: ""
    effect: Estimates the background of the whole image once, corrects the image and replaces it, and opens the outputs asked for. The controls then return to their defaults.
    enabled: Whenever no run works, at the defaults too.
    tradeoff: >-
      A progress window with **Cancel** follows the run, round by round. The run works on a full-size copy of the
      image, and the estimate on a few planes of the working size, one channel at a time. Only an output asked for adds
      a full-size image.
    scope: The whole image, every channel, with the mask blended afterwards.
    persistence: >-
      The history records every setting, as `Scale: 5.00, Smoothness: 1.00, Downsample: 4, Correction: Subtraction,
      Model: Multiscale, Structure protection: enabled (threshold: 0.050, amount: 0.50)`. No preset is saved.
```

## How it works

1. Each channel is read in [fractions of full scale](../../reference/glossary.md#fraction-of-full-scale) and
   averaged over blocks of **Downsample** pixels on a side into a working image of 32-bit floating-point samples. An
   integer image is worked on as a floating-point copy, 64-bit for 32-bit integers.
2. The model is fitted to every working pixel, then fitted again to the pixels equation (2) keeps and the protection
   leaves, round after round. The rounds stop when fewer than 1 in 10,000 working pixels change sides, from the
   second round on, or after 20 rounds.
3. **Smoothness** smooths the finished model. The correction reads the model at every pixel by bilinear
   interpolation between the blocks' centers, holding the edge value beyond the outermost ones, and applies equation
   (5) or (6) to each channel.
4. The result is held between 0 and 1, and an integer image is written back to the nearest code, ties to even, within
   its type's range. The mask then blends the result with the original.
5. The image replaces the target with one history step. Its [header cards](../../reference/glossary.md#header-card),
   acquisition record, [plate solution](../../reference/glossary.md#plate-solution) and
   [processing state](../../reference/glossary.md#processing-state) are kept, so it is not recorded as nonlinear. Its
   [lineage](../../reference/glossary.md#lineage) gains an entry with the step's text.

### Model

With \(W\) and \(H\) the working image's width and height, the image's own divided by **Downsample** and rounded up,
and \(s\) the value of **Scale**, the model's radius in working pixels is

\[ r = \max\bigl(1,\ \operatorname{round}(\tfrac{s}{100}\,\min(W, H))\bigr) \tag{1} \]

with halves rounded away from zero. In each round, let \(R = I - G\) be the residual of a working channel \(I\)
against the current model \(G\), \(m\) the median of the residuals of the pixels the last round kept, every pixel in
the first round, and \(\sigma\) their [median absolute deviation](../../reference/glossary.md#median-absolute-deviation)
from \(m\) times 1.4826, plus \(10^{-12}\). A pixel is kept when

\[ m - 4\sigma \le R \le m + 2\sigma \tag{2} \]

The tighter bright side is what leaves stars and bright structure out. When fewer pixels stay than the larger of 16
and 2 % of the working image, the kept pixels become those whose residual is at or below the percentile that count
marks, less the protected ones.

With \(T\) the value of **Threshold** and \(a\) of **Amount**, the pixels where \(R > m + T\) seed the protection.
Two box passes of radius

\[ r_p = \max\bigl(1,\ \operatorname{round}(r\,(0.5 + a))\bigr) \tag{3} \]

spread the seeds, and a pixel whose spread value is above \((1 - a)/2 + 0.001\) stays out of every later fit.

The multiscale model fills the pixels left out with the mean of the kept ones. Ten times it smooths with three box
passes of radius \(r\) along each axis and puts the kept pixels back, and one more smoothing gives the model. Box
passes repeat the edge pixel beyond the border. The polynomial, with \(x\) and \(y\) running from -1 to 1 across the
working image and \(d\) the value of **Polynomial degree**, is

\[ G(x, y) = \sum_{i + j \le d} c_{ij}\, x^i y^j \tag{4} \]

fitted by least squares to the kept pixels in double precision. **Smoothness** \(k\) then adds three box passes of
radius \(\max(1, \operatorname{round}(r\,k))\). With \(L\) the median of the working model of the channel,
**Subtraction** and **Division** give

\[ O = \min\bigl(1, \max(0,\ I - G + L)\bigr) \tag{5} \]

\[ O = \min\Bigl(1, \max\Bigl(0,\ \frac{I}{\max(G, 10^{-6})}\,L\Bigr)\Bigr) \tag{6} \]

### Boundaries

- A pixel that holds no number takes no part in the estimate and is left as it was. A block without a usable pixel
  takes the channel's mean. Floating-point samples outside 0 to 1 take part as they are, and the result is held
  between 0 and 1.
- A polynomial the kept pixels determine no surface for is refused with "the accepted samples determine no
  background surface at all", and a protection that leaves no working pixel with "structure protection leaves no
  working sample to fit". Nothing is written.
- A run the memory budget cannot hold is refused with a message that names the budget, and writes nothing.
- A [sequence](../../reference/glossary.md#sequence) is refused: "Auto Gradient Removal works on a still image, not a
  sequence". So is an image of more than four channels.
- Cancellation is checked between rounds, passes and blocks of rows. A stopped Apply changes nothing, records no
  history step and opens no output.

### Worked example

The master in the picture above is the linear Carina master, 7,246 by 5,429 pixels, whose background turns from
pink at the left to green at the right. At the defaults, **Downsample** at 4 gives a working image of 1,812 by 1,358
pixels, and equation (1) a radius of 68 working pixels, 272 pixels of the image. Equation (2) leaves the stars and
the bright nebula out, and **Gradient model** shows the color shift the model follows. With **Subtraction**,
equation (5) brings every background pixel on the model to \(L\), its median, near 0.027 in each channel.

```figure
images:
  - image: ../../assets/images/processes/background-extraction/auto-gradient-removal-before.webp
    alt: >-
      The whole frame before Apply in the view's Autostretch display: the red and white nebula on a background that
      turns from pink at the left to green-gray at the right.
    label: Before
  - image: ../../assets/images/processes/background-extraction/auto-gradient-removal-after.webp
    alt: The whole frame after Apply in the same display, the same nebula on an even dark gray background.
    label: After
caption: >-
  The Carina master before and after Apply at the defaults. Both are the whole frame in the view's Autostretch
  display, which stretches each image by its own statistics.
```

## Starting points

| Situation | Start | Then adjust |
| --- | --- | --- |
| A linear master with a gradient and a nebula smaller than the frame | The defaults | **Scale** up if the model holds the nebula, down if a gradient remains, then **Smoothness**. |
| A halo or extended nebula shows in the model | **Protection mask** in **Preview display** | Lower **Threshold** first, then raise **Amount** to grow what it found. |
| A smooth plane or curve across the frame | **Simplified polynomial model**, **Polynomial degree** at 1 or 2 | Raise the degree only when the model shows that a lower one cannot follow the background. |
| Darker corners the flats did not correct | **Division** | Compare with **Subtraction** at the same settings, and keep the one whose model matches the pattern. |

## When it goes wrong

!!! warning "The estimate cannot tell a gradient from a faint glow"
    What does not stand clearly above the background is taken for background, a faint nebula wider than the model's
    reach too, and is removed with the gradient. Look at **Gradient model** before **Apply**, above all around an
    extended nebula, a galaxy's halo or in a mosaic.

!!! danger "Samples above 1 are cut to 1"
    The corrected image is held between 0 and 1. A floating-point master whose brightest stars are above 1 loses
    them, and a sample below 0 becomes 0. When those values matter, bring the image into 0 to 1 first, with
    [Pixel Math](../pixel-math/pixel-math.md) for example.

"This process supports still images only, not sequences."
:   **Cause.** The active view is a sequence.
:   **Fix.** Extract the frame you need into an image of its own, or open the still master.

The preview shows the image unchanged
:   **Cause.** No preview stands for the settings: **Preview** was not pressed since the last change, or the display
    was switched after a change.
:   **Fix.** Keep **Enable preview** on, choose **Preview** and wait for the result.

The model or the protection display shows the image through it
:   **Cause.** An enabled mask on the view blends every preview display with the image.
:   **Fix.** Turn the mask off, choose **Preview** again, and turn the mask on only to check the corrected image.

**Protection mask** cannot be chosen, or **Create protection mask image** turned itself off
:   **Cause.** **Protect bright structures** is off.
:   **Fix.** Turn it on, then choose **Preview**.

The model holds stars, the nebula or a galaxy's halo
:   **Cause.** The scale or the protection let real structure into the fit.
:   **Fix.** Look at **Protection mask**, lower **Threshold** or raise **Amount** a little, raise **Scale**, or
    compare with the other model.

Clipped shadows or highlights, or noisy dark areas
:   **Cause.** The result is held between 0 and 1, and **Division** strongly scales the pixels where the model is
    weak.
:   **Fix.** Use **Subtraction** for an added glow, and check the levels in Image Statistics.

"the accepted samples determine no background surface at all", or "structure protection leaves no working sample to fit"
:   **Cause.** The kept pixels left the polynomial nothing to fit, or the protection covered every working pixel.
    Nothing was changed.
:   **Fix.** Protect less by raising **Threshold** or lowering **Amount**, lower **Polynomial degree**, or use the
    multiscale model.

"the model radius this scale implies is larger than the working picture can usefully carry"
:   **Cause.** **Scale** at a low **Downsample** makes a radius above 1,024 working pixels on a very large frame.
:   **Fix.** Raise **Downsample** or lower **Scale**.

## Related

- [Background Extraction](background-extraction.md), to fit the background through samples you place yourself.
- [Crop](../geometry/crop.md), to remove stacking borders first.
- [Background Neutralization](../color-calibration/background-neutralization.md),
  [Photometric Color Calibration](../color-calibration/photometric-cc.md) and
  [Spectrophotometric Color Calibration](../color-calibration/spectrophotometric-cc.md), which come next, once the
  background is flat.
- [Image Statistics](../../interface/image-statistics.md), to compare the background's levels before and after.
- [Masks](../../interface/masks.md), for the mask the result is blended through.
- [Process Windows](../../interface/process-windows.md), for the preview pane, Apply and what every process window
  shares.

## Credits

- The automatic estimate is a derived work of
  [AutoGradientRemoval 1.0.0](https://gitlab.com/free-astro/siril-scripts/-/blob/ca21c1425b14eeaff2f94e315e683240e28b86ce/processing/AutoGradientRemoval.py),
  a script of the Siril project, Copyright (C) 2026 Cyril Richard, which its file declares GPL-3.0-or-later, and is
  distributed under that licence. The reduction, the two models, the robust interval, the protection and the
  bilinear return to full size are the script's. The handling of invalid pixels, the correction fused into the
  write, the threshold in the image's own units and the protection kept across rounds are AfterNight's.
  **About > About AfterNight** credits it.
