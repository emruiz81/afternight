---
kind: process
stage: noise
menu: Process > Noise Reduction > Total Variation Denoise Filter
summary: >-
  Reduces noise while keeping strong edges, by finding a piecewise-smooth image close to the source with one of two
  solvers of the Rudin-Osher-Fatemi problem, split Bregman or Chambolle.
data_in: either
data_out: either
destructive: true
preview: real-time
sequences: true
masks: post-blend
external: none
glance:
  enabled: >-
    An image or a sequence view is active, no Total Variation Denoise window is open on it yet, and no window that
    needs the image for itself is open on it.
  inputs: >-
    One grayscale or RGB image or sequence of any pixel type, with finite samples, and an optional mask of the image's
    size on its view.
  outputs: >-
    The same view, replaced, with one history step. Its size, channels, pixel type, header and recorded processing
    state are kept, and a floating-point result is not held between 0 and 1. No file is written.
  state: >-
    A calibrated and stacked master, linear or early in the stretch, after gradient removal and color calibration and
    before a strong stretch raises its noise.
derived: false
rewritten: true
---
# Total Variation Denoise

Total Variation Denoise reduces noise while keeping strong edges. It looks for the image closest to yours whose total
variation, the sum of the differences between neighbouring pixels, is small. Small differences, such as noise, are
smoothed away, and a large step, such as the edge of a star or a dust lane, stays. Two solvers reach that image,
**Bregman** and **Chambolle**, and each reads **Weight** on a scale of its own. AfterNight's engine computes both.

**Apply** replaces the image or [sequence](../../reference/glossary.md#sequence) with the denoised one, keeps its size
and [pixel type](../../reference/glossary.md#pixel-type), and adds one step to its
[history](../../reference/glossary.md#history).

```figure
image: ../../assets/images/processes/noise-reduction/total-variation-denoise.webp
alt: >-
  The Total Variation Denoise window. On the left, the Algorithm box at Chambolle, the Weight, Epsilon and Max
  Iterations boxes with their sliders, a greyed Isotropic check box, and lower down Target Image, Channel at RGB and the
  Preview check box. On the right, a toolbar of zoom buttons, a channel box and a display mode box reading Autostretch
  over the preview of a red nebula, with the word PREVIEW in its corner and a framed area in its middle. Reset, Close
  and Apply run along the bottom.
caption: >-
  Total Variation Denoise on the linear master with Algorithm at Chambolle, which sets Epsilon to 0.0002 and Max
  Iterations to 200, and Weight at 0.0050. The framed area shows what Apply writes there, in the Autostretch display
  mode.
pins:
  - {n: 2, x: 2.0, y: 82.7}
  - {n: 3, x: 2.0, y: 19.9}
  - {n: 4, x: 2.0, y: 25.0}
  - {n: 5, x: 2.0, y: 39.9}
  - {n: 6, x: 2.0, y: 30.1}
  - {n: 7, x: 57.5, y: 55.2}
  - {n: 8, x: 88.9, y: 96.0}
```

## Before you start

- Finish calibration, stacking, gradient removal and color calibration first, and denoise before a strong
  [stretch](../../reference/glossary.md#stretch) raises the noise. The solver can take a weak real gradient for noise.
- **Apply** replaces the image. Duplicate it first, or use its history, which keeps the step before.
- Choose **Algorithm** before anything else. Choosing it sets **Epsilon** and **Max Iterations** again, and a
  **Weight** tuned for one solver means another strength in the other.
- Replace any sample that is not a finite number. Unless the image marks it as invalid, it enters the solve and
  spreads into its neighbours.
- [Debayer](../../reference/glossary.md#debayer) a [CFA](../../reference/glossary.md#cfa) mosaic first. A raw mosaic is
  denoised as one grayscale plane. The frames of a sequence are read with the automatic debayer setting of its view.
- A [mask](../../reference/glossary.md#mask) on the view blends the result with the original afterwards. On a
  sequence, every frame gets the same values, **Channel** and mask.

## Steps

1. **Open the window.** Make the [master](../../reference/glossary.md#master) the
   [active image](../../reference/glossary.md#active-image) and choose **Process > Noise Reduction > Total Variation
   Denoise Filter**. The preview shows the denoised image inside a framed
   [preview area](../../reference/glossary.md#preview-area) in the middle of the image.
2. **Choose the channel.** Leave **Channel** at **RGB** to denoise the three channels, each on its own. Choose
   **CIE L* (Lightness)** or a chroma component to work on the brightness or on color noise alone.
3. **Choose the solver.** On a [linear image](../../reference/glossary.md#linear-image), choose **Chambolle**: its low
   **Weight** values are gentler than any **Bregman** setting. **Epsilon** and **Max Iterations** change with it.
4. **Set the strength.** Raise **Weight** in small steps until the grain of the background is gone, and lower it when
   faint structure turns flat or terraced.
5. **Compare the gradient.** With **Bregman**, compare **Isotropic** on and off. Off can leave a texture of horizontal
   and vertical steps.
6. **Let the solver finish.** Lower **Epsilon** or raise **Max Iterations** only when the preview still changes as you
   do, which means the solver stopped too soon.
7. **Check at 100 %.** Zoom into the area, hold ++space++ to compare with the original, and look at faint gradients and
   small stars. Untick **Preview area** for the last check.
8. **Apply.** Choose **Apply**. A progress window counts the iterations, the view shows the denoised image, and its
   history lists **Total Variation Denoise** with the values.

## Controls

**Target Image**, the preview pane, **Close** and the progress window work as in every process window: see
[Process Windows](../../interface/process-windows.md). What is particular to this window is below.

```controls
window: Total Variation Denoise
controls:
  - name: Algorithm
    kind: combo
    default: Bregman
    options: [Bregman, Chambolle]
    effect: Chooses the solver. **Bregman** runs the split-Bregman method of Goldstein and Osher, **Chambolle** the projection on a dual field of Chambolle. Both seek the same kind of image and read **Weight** on scales of their own.
    range: Choosing **Bregman** sets **Epsilon** to 0.0010 and **Max Iterations** to 100, and ticks and enables **Isotropic**. Choosing **Chambolle** sets them to 0.0002 and 200, and disables **Isotropic** without clearing it. **Weight** keeps its value.
    enabled: Disabled while Apply runs.
    tradeoff: Switching discards the values you gave **Epsilon** and **Max Iterations**. The tooltip calls **Chambolle** "fast primal-dual style TV denoising", though it runs Chambolle's projection, not a primal-dual method.
    scope: The preview and Apply, on what **Channel** selects.
    persistence: Until the window closes. **Reset** and a finished Apply return it to **Bregman**. The history records its name.
  - name: Weight
    kind: spinbox
    default: "0.5000"
    widget: {min: 0, max: 1, step: 0.005, decimals: 4}
    effect: Sets the strength. Higher smooths more with both solvers, through a mapping of its own for each, which equation (2) gives.
    range: The arrows step by one power of ten below the value, and the slider in steps of 0.001. At exactly 1, **Bregman** solves as it would at 0.9917, so 1 smooths less than 0.995 and more than 0.99. At exactly 0, **Chambolle** solves as it would at 0.001.
    enabled: Disabled while Apply runs.
    tradeoff: One number is a different strength in the two solvers. **Bregman** at 0 equals **Chambolle** at 0.0833, so no **Bregman** setting is gentler than that. Strong settings flatten faint gradients into terraces and round or erase small stars.
    scope: The preview and Apply, on what **Channel** selects, before the mask.
    persistence: Until the window closes. Choosing **Algorithm** keeps it. **Reset** and a finished Apply return it to its default. The history records it as typed, not as the solver reads it.
  - name: Epsilon
    kind: spinbox
    default: "0.0010"
    widget: {min: 0, max: 1, step: 0.001, decimals: 4}
    effect: Sets when the solver stops before **Max Iterations**. Higher stops sooner, further from the image the solver seeks. Lower runs longer.
    range: >-
      **Bregman** stops when the root-mean-square change of the image in one iteration falls below it, in fractions of
      full scale for an integer image. **Chambolle** stops when the change of its energy in one iteration falls below
      it times the energy's first value. At 0 neither stops early. The arrows step by one power of ten below the value,
      and the slider in steps of 0.001, so a smaller value must be typed.
    enabled: Disabled while Apply runs.
    tradeoff: A large value can stop the solver before the noise is gone. A small one can cost every iteration up to **Max Iterations**.
    scope: The preview and Apply, for each channel.
    persistence: Until the window closes. Choosing **Algorithm** sets it again. **Reset** and a finished Apply return it to its default.
  - name: Max Iterations
    kind: spinbox
    default: "100"
    widget: {min: 1, max: 500, step: 1}
    effect: Sets the most iterations the solver runs on each channel. Higher lets it come closer to the image it seeks when **Epsilon** has not stopped it.
    range: The solver stops sooner when **Epsilon** is met.
    enabled: Disabled while Apply runs.
    tradeoff: Each iteration passes over the whole channel several times, so the time grows with the iterations that run. A higher limit changes nothing once **Epsilon** stops the solver.
    scope: The preview and Apply, for each channel.
    persistence: Until the window closes. Choosing **Algorithm** sets it again. **Reset** and a finished Apply return it to its default.
    note: The progress window counts the iterations of every channel against this limit, and jumps ahead when a channel stops early.
  - name: Isotropic
    kind: check
    default: "On"
    effect: For **Bregman**, measures each difference between pixels as one length, alike in every direction. Off measures its horizontal and vertical parts apart, which favours edges along the rows and columns.
    enabled: With **Bregman** only. **Chambolle** disables it and always measures the length. It is disabled while Apply runs, and stays disabled after a run while **Chambolle** is chosen.
    tradeoff: Off can leave a blocky texture of horizontal and vertical steps.
    scope: The preview and Apply, with **Bregman**.
    persistence: Until the window closes. Choosing **Bregman**, **Reset** and a finished Apply tick it. Choosing **Chambolle** leaves it as it was, and disabled. The history does not record it.
  - name: Target Image
    kind: combo
    default: The active image
    options: [The active image, Every other open image or sequence the window can work on]
    effect: Chooses the image or sequence Apply replaces, with the mask on its view. The choices of **Channel** and the preview follow it.
    persistence: Until the window closes. **Reset** leaves it as it is.
  - name: Channel
    kind: combo
    default: RGB
    options: [RGB, Red, Green, Blue, CIE L* (Lightness), CIE a*, CIE b*, CIE c*, CIE h*, CIE X, CIE Y (Luminance), CIE Z, HSL Hue, HSL Saturation, HSL Lightness, HSV Hue, HSV Saturation, HSV Value, YCbCr Luma, YCbCr Blue Chroma, YCbCr Red Chroma]
    effect: Chooses what is denoised. **RGB** solves each of the three channels on its own, one after another. **Red**, **Green** or **Blue** denoises that channel and keeps the other two. Every other choice denoises one component of another color model and converts the image back.
    enabled: For an RGB image. A grayscale image shows a disabled **Grayscale**. Disabled while Apply runs.
    tradeoff: The channels are not tied together, so strong smoothing can move a color edge or leave terraces that differ between channels. A component costs two conversions and a working copy, and the conversion back can change colors. A hue or a chroma is no measure of brightness.
    scope: The preview and Apply, before the mask. Process Windows describes each component.
    persistence: Until the window closes. **Reset**, a finished Apply and a new target return it to **RGB**. The history names it for a color image.
  - name: Preview
    kind: check
    default: "On"
    effect: Shows the denoised image beside the controls, with the mask blended in.
    range: >-
      The pane opens with a **Preview area** of 800 by 800 pixels in the middle of the image, labelled "Preview: Total
      Variation Denoise", or the whole image when it is smaller. At the window's default size, **Preview area** and
      **Take snapshot** are in the menu at the end of the pane's toolbar.
    tradeoff: The area is solved as an image of its own, with its own edges and its own stopping point. Every change solves it again. **Apply**, or closing the window, stops a preview on its way at the solver's next iteration.
    scope: The window only. On a sequence it shows the frame the view displays. **Display Mode** and **Autostretch Settings** change only how the preview is shown.
    persistence: Until the window closes. **Reset** leaves it as it is.
  - name: Reset
    kind: button
    default: ""
    effect: Returns **Algorithm** to **Bregman**, **Weight**, **Epsilon** and **Max Iterations** to their defaults and ticks **Isotropic**. For an RGB image it returns **Channel** to **RGB**. One preview of them follows.
    enabled: Disabled while Apply runs.
  - name: Apply
    kind: button
    default: ""
    effect: Denoises the whole image, or every frame of the sequence, with the values as they are when you press it, and then returns them to their defaults.
    enabled: From the moment the window opens, at any values.
    tradeoff: A single image shows a progress window too, which counts the iterations, and its **Cancel** is noticed at the solver's next iteration. The solver keeps six single-precision planes the size of one channel, about 0.94 GB for a 7246 x 5429 frame, beside the second copy every run needs. It works on the threads the run is granted, beside other processes, and gives the same answer at any thread count.
    persistence: "The history step is **Total Variation Denoise** and reads, for example, `Algorithm: Chambolle, weight: 0.02, eps: 0.0002, max iterations: 200 -  in channel: RGB`. For a grayscale image it ends after the dash. **Isotropic**, the strength the solver read and the mask are not recorded. The header cards, the plate solution and the recorded processing state are kept."
```

## How it works

1. Each channel that **Channel** selects is solved on its own, one after another, in single precision with sums in
   double precision. An integer image's **Weight** and **Epsilon** are restated in its own codes, so it is solved as
   its [fractions of full scale](../../reference/glossary.md#fraction-of-full-scale) would be.
2. The solver starts from the image and, iteration by iteration, moves toward the image that minimizes equation (1)
   below, until **Epsilon** stops it or **Max Iterations** is reached. **Bregman** relaxes the image and shrinks its
   differences by a threshold; **Chambolle** steps a dual field and projects it back.
3. The edge of the image is closed: nothing passes across it, so the last rows and columns are smoothed from inside
   alone.
4. The result is written back in the image's own pixel type, and a mask on the view then blends it with the
   original.
5. The image replaces the target and its history gets one step. Its header, its
   [plate solution](../../reference/glossary.md#plate-solution) and its recorded
   [processing state](../../reference/glossary.md#processing-state) are kept.

### Model

With \(f\) the image, \(u\) the result, \(|\nabla u|\) the size of the differences \(d_x\) and \(d_y\) of each pixel
of \(u\) from its right and lower neighbours, and \(\Lambda\) a weight that **Weight** sets, both solvers minimize

\[ E(u) = \sum |\nabla u| + \frac{\Lambda}{2} \sum (u - f)^2 \tag{1} \]

The first sum is the total variation, the second the distance from the image. A smaller \(\Lambda\) gives the
variation more weight and smooths more. With **Isotropic** on, the size is the length \(\sqrt{d_x^2 + d_y^2}\), and
off it is \(|d_x| + |d_y|\). With \(p\) the value of **Weight**, the solvers take

\[ \Lambda_{\text{Bregman}} = 120\,(1 - p), \qquad \Lambda_{\text{Chambolle}} = \frac{10}{p} \tag{2} \]

with 1 in place of 0 for **Bregman** at \(p = 1\), and 10,000 for **Chambolle** at \(p = 0\). **Bregman** at
\(p = 0.5\) is therefore **Chambolle** at \(p = 0.1667\). In the continuous form of equation (1), a flat round feature
of radius \(R\) pixels on a flat background keeps its size and loses

\[ \delta = \frac{2}{\Lambda R} \tag{3} \]

of its contrast, and disappears when its contrast is smaller. Grains and small stars lose the most, and the step
between two broad areas barely moves. A flat area keeps its level.

### Boundaries

- The closed edge treats the frame's border as the end of the image, and a
  [preview area](../../reference/glossary.md#preview-area) has edges of its own. Judge the result away from the border.
- A sample that is not a finite number is not repaired. In an image that marks every sample valid it enters the
  solve and spreads to its neighbours, iteration by iteration. A pixel the image marks as invalid enters the solve as
  zero, which can pull its neighbours, and is left as it was.
- At an **Epsilon** of 0 every iteration runs. A run that stops at **Max Iterations** has not reached the minimum, and
  its result depends on the solver's route.
- A 64-bit floating-point image is solved in single precision. A floating-point result is not held between 0 and 1,
  and an integer one is rounded to the nearest code and held to its type's range.
- **Cancel**, or closing the window, stops the solver at its next iteration, and nothing is written. Each solver
  reports its progress after every iteration.

### Worked example

The image is the linear master, whose background lies near 0.028. Its noise from one pixel to the next is about
0.0003 in red, 0.001 in green and 0.0007 in blue. With **Algorithm** at **Chambolle**, which sets **Epsilon** to
0.0002 and **Max Iterations** to 200, and **Weight** at 0.0050, equation (2) gives \(\Lambda = 2000\) for each
channel.

By equation (3), a grain of radius 1 pixel loses about 0.001, as much as the green noise and more than the red and
the blue, and is smoothed away. A small star of radius 2 pixels loses about 0.0005 of its peak, and a knot of nebula
of radius 10 pixels about 0.0001. The background keeps its level near 0.028. At its default, **Bregman** solves with
\(\Lambda = 60\): the same star would lose about 0.017, seventeen times the green noise.

For the pair, the middle of the master is cropped first and denoised alone. Both pictures are then stretched by
[Histogram Transformation](../stretching/histogram-transformation.md) with **Shadows** and **Midtones** at 0.02.

```figure
images:
  - image: ../../assets/images/processes/noise-reduction/noise-crop-before.webp
    alt: >-
      A crop at 100 % of the middle of the master, stretched: pink clouds of nebula beside a dark dust lane, many small
      stars, and a fine colored grain over the background.
    label: Before
  - image: ../../assets/images/processes/noise-reduction/total-variation-denoise-after.webp
    alt: >-
      The same crop denoised before the same stretch: the grain of the background weaker and its colored speckle mostly
      gone, the strong edges of the nebula and the stars kept.
    label: After
caption: >-
  The middle of the Carina Nebula master at 100 %, without and with Total Variation Denoise at Algorithm Chambolle and
  Weight 0.0050, both then stretched by Histogram Transformation with Shadows and Midtones at 0.02. Both are shown in
  the Linear display mode.
```

## Starting points

| Situation | Start | Then adjust |
| --- | --- | --- |
| A linear master | **Chambolle**, **Weight** near 0.005 | Up in small steps until the grain is gone, down at the first terrace. |
| A stretched image | The defaults, **Bregman** at 0.5000 | **Weight** up for more smoothing. Equation (2) gives the same strength in **Chambolle**. |
| Color noise | A chroma component in **Channel**, on a duplicate | Check colored edges and hues at 100 %. |
| Quick tries on a large image | The preview area, a higher **Epsilon** or fewer **Max Iterations** | Back to the solver's values, with **Preview area** unticked, for the last check. |

## When it goes wrong

!!! warning "The two solvers read Weight differently"
    The same **Weight** is a different strength in **Bregman** and in **Chambolle**, and choosing **Algorithm** sets
    **Epsilon** and **Max Iterations** again. Choose the solver first, then tune, and carry a strength across with
    equation (2).

Faint gradients turn into flat terraces, or small stars look rounded or vanish
:   **Cause.** **Weight** is too strong for the noise, and total variation favours flat areas joined by steps.
:   **Fix.** Lower **Weight**. On a linear image, use **Chambolle** with a small **Weight**.

**Bregman** smooths faint detail away even at a **Weight** of 0
:   **Cause.** At 0, **Bregman** solves with \(\Lambda = 120\), the strength of **Chambolle** at 0.0833.
:   **Fix.** Choose **Chambolle** and a **Weight** below 0.0833.

A texture of horizontal and vertical steps appears
:   **Cause.** **Isotropic** is off, which favours edges along the rows and columns.
:   **Fix.** Tick **Isotropic**.

**Isotropic** cannot be changed
:   **Cause.** **Chambolle** is chosen, which always measures the length of each difference.
:   **Fix.** Choose **Bregman** to compare the two measures.

A run takes long
:   **Cause.** Each iteration passes over every channel, and a small **Epsilon** runs up to **Max Iterations**.
:   **Fix.** Follow the progress window, which **Cancel** stops at the next iteration. Raise **Epsilon**, lower **Max
    Iterations**, or tune in the preview area.

The preview area differs from the result of Apply
:   **Cause.** The area is solved alone, with its own edges and stopping point.
:   **Fix.** Untick **Preview area** for the last check, and compare under one display mode.

Colors shift, or terraces differ between channels
:   **Cause.** The channels are solved apart, or a component was converted there and back.
:   **Fix.** Lower **Weight**, or compare **RGB** with a component on a duplicate.

Patches spread from a few bad pixels
:   **Cause.** The image holds samples that are not finite numbers, which spread through the solve.
:   **Fix.** Undo the step, replace those samples, and apply again.

## Related

- [Wavelet Denoise](wavelet-denoise.md), which thresholds the image scale by scale, and [NL-Bayes](nl-bayes.md),
  which compares patches that repeat across the image.
- [Background Extraction](../background-extraction/background-extraction.md) and
  [Photometric Color Calibration](../color-calibration/photometric-cc.md), which need linear data and come before.
- [Broadband Imaging](../../workflows/broadband-imaging.md), for where the denoise sits in a whole workflow.
- [Masks](../../interface/masks.md), to denoise the background and protect the stars.
- [Histogram Transformation](../stretching/histogram-transformation.md), the stretch that follows.
- [Process Windows](../../interface/process-windows.md), for the preview, the preview area and what every process
  window shares.

## Credits

- Leonid I. Rudin, Stanley Osher and Emad Fatemi,
  [Nonlinear total variation based noise removal algorithms](https://doi.org/10.1016/0167-2789(92)90242-F), Physica D
  60 (1992), for the problem both solvers minimize.
- Tom Goldstein and Stanley Osher,
  [The Split Bregman Method for L1-Regularized Problems](https://ww3.math.ucla.edu/camreport/cam08-29.pdf), for the
  **Bregman** solver, its shrinkage and its two measures of the gradient.
- Antonin Chambolle,
  [An Algorithm for Total Variation Minimization and Applications](https://www.math.ucla.edu/~lvese/285j.1.09f/Chambolle.pdf),
  for the **Chambolle** solver.
- David Strong and Tony Chan,
  [Edge-preserving and scale-dependent properties of total variation regularization](https://doi.org/10.1088/0266-5611/19/6/059),
  Inverse Problems 19 (2003), for the loss of contrast that shrinks with the size of a feature.
- The [scikit-image 0.26.0 restoration source](https://github.com/scikit-image/scikit-image/blob/v0.26.0/src/skimage/restoration/_denoise.py)
  and its [compiled split-Bregman source](https://github.com/scikit-image/scikit-image/blob/v0.26.0/src/skimage/restoration/_denoise_cy.pyx),
  for the discretization, the weights and the stopping rules. The **Chambolle** solver reproduces that library's
  answer, and the **Bregman** solver reaches the same minimum by a route of its own.
- The [scikit-image restoration API](https://scikit-image.org/docs/stable/api/skimage.restoration.html), for the
  meaning of the two solvers' weights.
