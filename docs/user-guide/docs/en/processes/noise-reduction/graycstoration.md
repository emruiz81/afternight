---
kind: process
stage: noise
menu: Process > Noise Reduction > GRAYCstoration Filter
summary: >-
  Reduces noise with G'MIC's anisotropic smoothing: averages each pixel along short paths that follow the image's own
  structure, long where the image is flat and short at stars and edges.
data_in: linear
data_out: linear
destructive: true
preview: real-time
sequences: true
masks: post-blend
external: The G'MIC library, bundled with AfterNight
glance:
  enabled: >-
    An image or a sequence view is active, no GRAYCstoration window is open on it yet, and no window that needs the
    image for itself is open on it. Preview and Apply also need a build that carries G'MIC.
  inputs: >-
    One grayscale or RGB image or sequence whose samples are all finite numbers, best between 0 and 1, and an
    optional mask of the image's size on its view.
  outputs: >-
    The same view, replaced, with one history step. Its size, channels, pixel type, header and recorded processing
    state are kept, and what G'MIC returns is held between 0 and 1. No file is written.
  state: >-
    A calibrated and stacked linear master, after gradient removal and color calibration and before the stretch.
derived: true
---
# GRAYCstoration

GRAYCstoration smooths noise along the image's own structure. At every pixel it measures how strongly the image
changes and in which direction it changes least, then averages the pixel along short curved paths that follow that
direction. Where the image is flat the paths are long and run every way. At a star or a sharp edge they shrink, and
they cross an edge less than they follow it. The work is done by the `smooth` command of G'MIC, the anisotropic
smoothing behind GREYCstoration, which AfterNight carries.

**Apply** replaces the image or [sequence](../../reference/glossary.md#sequence) with the smoothed one, keeps its size
and [pixel type](../../reference/glossary.md#pixel-type), and adds one step to its
[history](../../reference/glossary.md#history).

```figure
image: ../../assets/images/processes/noise-reduction/graycstoration.webp
alt: >-
  The GRAYCstoration window. On the left, ten boxes with sliders from Iterations to Amount, the Interpolation box at
  Nearest Neighbor and the ticked Fast Approximation box, and lower down Target Image, Channel at RGB and the Preview
  check box. On the right, a toolbar of zoom buttons, a channel box and a display mode box reading Autostretch over
  the preview of a red nebula, with the word PREVIEW in its corner and a small framed area in its middle. Reset, Close
  and Apply run along the bottom.
caption: >-
  GRAYCstoration on the linear master with Amplitude at 5 and the other values at their defaults. The framed area
  shows what Apply writes there, in the Autostretch display mode.
pins:
  - {n: 1, x: 64.2, y: 54.4}
  - {n: 2, x: 2.8, y: 85.3}
  - {n: 3, x: 2.8, y: 21.3}
  - {n: 4, x: 2.8, y: 25.6}
  - {n: 5, x: 2.8, y: 34.1}
  - {n: 6, x: 2.8, y: 42.7}
  - {n: 7, x: 2.8, y: 55.6}
  - {n: 9, x: 89.4, y: 96.6}
```

## Before you start

- Finish calibration, stacking, gradient removal and color calibration first, and smooth before the
  [stretch](../../reference/glossary.md#stretch). The process does not estimate the noise: it smooths whatever changes
  little against the image's range, faint signal included.
- **Apply** replaces the image. Duplicate it first, or use its history, which keeps the step before.
- Every sample must be a finite number.
- The process needs G'MIC, which AfterNight includes: nothing is installed or downloaded. A build without it keeps
  **Preview** and **Apply** disabled, and their tooltip says why.
- [Debayer](../../reference/glossary.md#debayer) a [CFA](../../reference/glossary.md#cfa) mosaic first. A raw mosaic is
  smoothed as one grayscale plane. The frames of a sequence are read with the automatic debayer setting of its view.
- A [mask](../../reference/glossary.md#mask) on the view blends the result with the original after **Amount**. On a
  sequence, every frame gets the same values, **Channel** and mask.

## Steps

1. **Open the window.** Make the [master](../../reference/glossary.md#master) the
   [active image](../../reference/glossary.md#active-image) and choose **Process > Noise Reduction > GRAYCstoration
   Filter**. The preview shows the smoothed image inside a framed
   [preview area](../../reference/glossary.md#preview-area): move it over background, faint nebula and small stars.
2. **Choose what to smooth.** Leave **Channel** at **RGB** to smooth the three channels along shared paths. Choose
   **CIE L* (Lightness)** to smooth the brightness alone, or a chroma component for color noise.
3. **Set the reach.** On a [linear image](../../reference/glossary.md#linear-image), start with **Amplitude** near 5.
   Raise it while blotches of noise remain, and lower it when stars grow spikes or faint filaments smear.
4. **Keep the edges.** Raise **Sharpness** to shorten the paths at stars and bright structure, and **Anisotropy** to
   keep them from crossing edges.
5. **Keep the noise from steering.** Raise **Noise Scale**, then **Regularity**, when the background turns into
   streaks or worms.
6. **Leave the sampling.** Keep **Spatial Step Size**, **Angular Step Size**, **Precision**, **Interpolation**,
   **Fast Approximation** and **Iterations** at their defaults until the rest looks right.
7. **Set the strength.** Lower **Amount** to bring back part of the original when the result looks painted.
8. **Check at 100 %.** Zoom into the area and hold ++space++ to compare with the original. Then untick **Preview
   area** and look at the whole image, its stars and its edges.
9. **Apply.** Choose **Apply**. A progress window follows the run, the view shows the smoothed image, and its history
   lists **GRAYCstoration** with the values.

## Controls

**Target Image**, the preview pane, **Close** and the progress window work as in every process window: see
[Process Windows](../../interface/process-windows.md). What is particular to this window is below.

```controls
window: GRAYCstoration
controls:
  - name: Iterations
    kind: spinbox
    default: "1"
    widget: {min: 1, max: 10, step: 1}
    effect: Sets how many times the whole smoothing runs, each pass on the result of the one before. Every pass measures the structure again, so later passes follow what earlier ones left. Higher smooths more.
    enabled: Disabled while Apply runs.
    tradeoff: The time grows in proportion to it. Repeated passes flatten areas and can draw flowing, painted textures. They are not one pass at a higher **Amplitude**.
    scope: The preview and Apply, on what **Channel** selects, before **Amount** and the mask. Each pass holds its result within the lowest and highest samples it was given.
    persistence: Until the window closes. **Reset** and a finished Apply return it to its default.
  - name: Amplitude
    kind: spinbox
    default: "60"
    widget: {min: 0, max: 100, step: 1}
    effect: Sets how far the paths reach. Higher smooths more, over longer paths. At 0 every pass returns the image unchanged, apart from rounding and the hold to 0 to 1, though the command still runs in full.
    range: The scale of the smoothing, in pixels, is the square root of twice its value times the strength of the structure, 10.95 at the default where the image is flat. Equation (2) gives the length of a path.
    enabled: Disabled while Apply runs.
    tradeoff: The time grows with the length of the paths. Long paths carry the smoothing along faint filaments and across weak edges.
    scope: The preview and Apply, on what **Channel** selects.
    persistence: Until the window closes. **Reset** and a finished Apply return it to its default.
  - name: Sharpness
    kind: spinbox
    default: "0.70"
    widget: {min: 0, max: 2.5, step: 0.01, decimals: 2}
    effect: Sets how quickly the smoothing gives way where the image changes. Higher shortens the paths more at stars, edges and bright structure. Lower smooths them almost as much as the background.
    range: The slider moves it in 1,000 equal steps of its range, and the box keeps two decimals. At 0 it counts as 0.00001, and the strength then barely depends on the structure.
    enabled: Disabled while Apply runs.
    tradeoff: Set high, it counts the noise as structure too, and the background keeps its grain.
    scope: The preview and Apply, on what **Channel** selects.
    persistence: Until the window closes. **Reset** and a finished Apply return it to its default.
  - name: Anisotropy
    kind: spinbox
    default: "0.30"
    widget: {min: 0, max: 1, step: 0.05, decimals: 2}
    effect: Sets how much more the paths follow a structure than cross it. At 0 they run alike in every direction, shorter at structure. Toward 1 they cross edges less and less.
    range: The slider moves it in 1,000 equal steps. At 1 the paths only follow the direction of least change, even in the noise of the background.
    enabled: Disabled while Apply runs.
    tradeoff: High values extend thin features and draw streaks that follow the noise, the more so with a high **Sharpness**.
    scope: The preview and Apply, on what **Channel** selects.
    persistence: Until the window closes. **Reset** and a finished Apply return it to its default.
  - name: Noise Scale
    kind: spinbox
    default: "0.60"
    widget: {min: 0, max: 3, step: 0.05, decimals: 2}
    effect: Sets the blur, in pixels, of the copy whose changes are measured. Higher keeps fine noise from steering the paths. Lower lets small details steer them.
    range: The slider moves it in 1,000 equal steps of its range. The blurred copy is then rescaled from its lowest to its highest sample before it is measured.
    enabled: Disabled while Apply runs.
    tradeoff: Set high, small stars and fine structure no longer steer the paths either, and are smoothed like the noise.
    scope: The preview and Apply, on what **Channel** selects. It changes only how the paths run, not the samples they average.
    persistence: Until the window closes. **Reset** and a finished Apply return it to its default.
    note: The reference of G'MIC calls it `alpha`.
  - name: Regularity
    kind: spinbox
    default: "1.10"
    widget: {min: 0, max: 3, step: 0.05, decimals: 2}
    effect: Sets the blur, in pixels, of the measured structure. Higher makes the direction of the paths turn more smoothly from pixel to pixel. Lower lets it follow every turn.
    range: The slider moves it in 1,000 equal steps of its range.
    enabled: Disabled while Apply runs.
    tradeoff: Set high, the direction of one structure spreads over its neighbours and bends paths across them. With a high **Noise Scale** too, broad structure steers the paths around small stars and fine texture.
    scope: The preview and Apply, on what **Channel** selects.
    persistence: Until the window closes. **Reset** and a finished Apply return it to its default.
    note: The reference of G'MIC calls it `sigma`.
  - name: Spatial Step Size
    kind: spinbox
    default: "0.80"
    widget: {min: 0.1, max: 1, step: 0.01, decimals: 2}
    effect: Sets the distance, in pixels, between two samples along a path. Smaller samples the paths more finely.
    range: The slider moves it in 1,000 equal steps of its range. A path holds its length divided by the step, rounded up, as samples, 28 at the defaults where the image is flat.
    enabled: Disabled while Apply runs.
    tradeoff: The time grows in inverse proportion to it. Larger steps are coarser and can cut the turns of a curved path.
    scope: The preview and Apply, on what **Channel** selects.
    persistence: Until the window closes. **Reset** and a finished Apply return it to its default.
    note: The reference of G'MIC calls it `dl`.
  - name: Angular Step Size
    kind: spinbox
    default: "30"
    widget: {min: 1, max: 90, step: 1}
    effect: Sets the angle, in degrees, between the directions in which paths start from each pixel. Smaller uses more directions.
    range: The number of directions is 360 divided by it, rounded up, 12 at the default. Even at 1 the command smooths along paths, so the window never reaches the command's other mode, which iterates a diffusion with a time step instead.
    enabled: Disabled while Apply runs.
    tradeoff: The time grows with the number of directions. Large steps leave few directions, which can show as a bias toward some of them.
    scope: The preview and Apply, on what **Channel** selects.
    persistence: Until the window closes. **Reset** and a finished Apply return it to its default.
    note: The reference of G'MIC calls it `da`.
  - name: Precision
    kind: spinbox
    default: "2.00"
    widget: {min: 0.1, max: 10, step: 0.1, decimals: 2}
    effect: Sets how long a path runs, as a multiple of the scale **Amplitude** sets. Higher lengthens the paths.
    range: The slider moves it in 1,000 equal steps of its range.
    enabled: Disabled while Apply runs.
    tradeoff: Longer paths cost time. With **Fast Approximation** on, every added sample weighs as much as the first. Off, they weigh less and less, and past 3 they add almost nothing.
    scope: The preview and Apply, on what **Channel** selects.
    persistence: Until the window closes. **Reset** and a finished Apply return it to its default.
    note: The reference of G'MIC calls it `gauss_prec`.
  - name: Amount
    kind: spinbox
    default: "1.00"
    widget: {min: 0, max: 1, step: 0.01, decimals: 2}
    effect: Mixes the smoothed image with the original. At 1 the result is the smoothed image, at 0.5 halfway, and at 0 the original.
    range: The slider moves it in 1,000 equal steps.
    enabled: Disabled while Apply runs. **Apply** stays enabled at 0.
    tradeoff: It brings back noise and detail together. The command runs in full at any value, and below 1 the mix adds one pass over the image. At 0 even samples outside 0 to 1 come back, and between 0 and 1 they are mixed with the held result.
    scope: The preview and Apply, on the whole image once **Channel** has been converted back, before the mask. It mixes once, after the last iteration.
    persistence: Until the window closes. **Reset** and a finished Apply return it to its default.
  - name: Interpolation
    kind: combo
    default: Nearest Neighbor
    options: [Nearest Neighbor, Linear, Runge-Kutta]
    effect: Chooses how a path reads the image and the structure between pixels. **Nearest Neighbor** takes the nearest pixel. **Linear** reads both between pixels. **Runge-Kutta** also aims each step by the structure halfway along it, and reads the image as **Linear** does.
    enabled: Disabled while Apply runs.
    tradeoff: "**Linear** and **Runge-Kutta** follow curves without the small stair steps of **Nearest Neighbor**, and cost more time. **Nearest Neighbor** with a coarse **Spatial Step Size** is the fastest and the most likely to show steps."
    scope: The preview and Apply, on what **Channel** selects. The pixel type of the result does not change.
    persistence: Until the window closes. **Reset** and a finished Apply return it to its default.
  - name: Fast Approximation
    kind: check
    default: "On"
    effect: On, every sample of a path counts the same. Off, a sample weighs less the farther along the path it lies, as equation (3) gives.
    enabled: Disabled while Apply runs.
    tradeoff: It changes the result, not only the time. Off favours the samples near the pixel, so it smooths less at the same **Amplitude**, and it computes an exponential for every sample.
    scope: The preview and Apply, on what **Channel** selects.
    persistence: Until the window closes. **Reset** and a finished Apply tick it. The history does not record it.
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
    effect: Chooses what is smoothed. **RGB** hands the command the three channels as one color image, whose structure is measured from all three, and averages each channel along the same paths. **Red**, **Green** or **Blue** smooths that channel, along paths of its own, and keeps the other two. Every other choice smooths one component of another color model and converts the image back.
    enabled: For an RGB image. A grayscale image shows a disabled **Grayscale**. Disabled while Apply runs.
    tradeoff: With **RGB**, a strong edge or color noise in one channel steers the paths of all three, which can smear colors together. A component costs two conversions and a working copy, and the conversion back can change colors. A hue is smoothed as a line, not as a circle.
    scope: The preview and Apply, before **Amount** and the mask. Process Windows describes each component.
    persistence: Until the window closes. **Reset** leaves it as it is. A finished Apply and a new target return it to **RGB**.
  - name: Preview
    kind: check
    default: "On"
    effect: Shows the smoothed image beside the controls, with **Amount** and the mask applied.
    range: >-
      The pane opens with a **Preview area** of 500 by 500 pixels in the middle of the image, labelled "Preview:
      GRAYCstoration Filter", or the whole image when it is smaller. At the window's default size, **Preview area** and
      **Take snapshot** are in the menu at the end of the pane's toolbar.
    enabled: >-
      In a build without G'MIC it stays off and disabled, and its tooltip reads "This build of AfterNight carries no
      G'MIC library, so this process cannot run."
    tradeoff: The area is smoothed as an image of its own, with its own range and its own edges, where the paths stop. Every change runs the whole command again. **Apply**, or closing the window, stops a preview on its way inside the command.
    scope: The window only. On a sequence it shows the frame the view displays. **Display Mode** and **Autostretch Settings** change only how the preview is shown.
    persistence: Until the window closes. **Reset** leaves it as it is.
  - name: Reset
    kind: button
    default: ""
    effect: Returns the twelve controls from **Iterations** to **Fast Approximation** to their defaults, and one preview of them follows. It leaves **Channel** as it is.
    enabled: Disabled while Apply runs.
  - name: Apply
    kind: button
    default: ""
    effect: Smooths the whole image, or every frame of the sequence, with the values and **Channel** as they are when you press it, and then returns the twelve values to their defaults.
    enabled: From the moment the window opens, at any values. In a build without G'MIC it is disabled, with the tooltip **Preview** shows.
    tradeoff: A single image shows a progress window too, and its **Cancel** stops the run. With one iteration the bar is busy, because the command reports no share done. With more, it shows the share of the passes done. The time grows with the pixels and channels, the iterations, the directions and the samples on each path. The command runs on one thread and holds several working images of the image's size, beside the second copy every run needs. Samples already in 32-bit floating point and tightly packed are handed over as they are, any others as a working copy. G'MIC runs one command at a time in the application, so another G'MIC window's preview or run waits for this one.
    persistence: "The history step is **GRAYCstoration** and reads, for example, `Interpolation: Nearest Neighbor, Iterations: 1, Amplitude: 60, Sharpness: 0.70, Anisotropy: 0.30, Noise Scale: 0.60, Regularity: 1.10, Spatial Step Size: 0.80, Angular Step Size: 30, Precision: 2.00, Amount: 0.50 in channel: RGB`. For a grayscale image it ends after the amount. **Fast Approximation**, the mask and the G'MIC version are not recorded. The header cards, the plate solution and the recorded processing state are kept."
```

## How it works

1. The channels **Channel** selects are checked: an image that holds a sample that is not a finite number is refused.
   Otherwise they go to G'MIC, in one session for the run, as single-precision
   [fractions of full scale](../../reference/glossary.md#fraction-of-full-scale).
2. The command blurs a copy by **Noise Scale**, rescales it from its lowest to its highest sample onto 0 to 255, and
   measures at each pixel how strongly it changes and along which direction it changes least. It blurs that measure
   by **Regularity** and turns it into two strengths, along the structure and across it, set by **Sharpness** and
   **Anisotropy**.
3. From every pixel, in each direction **Angular Step Size** sets, it follows the structure in steps of **Spatial
   Step Size** for a length set by **Amplitude**, **Precision** and the strengths, and averages the samples it meets.
   The averages of all directions are averaged again, and **Iterations** repeats the whole on the result.
4. The result is held to 0 to 1 and written back in the image's own pixel type. **Amount** mixes it with the
   original, and a mask on the view then blends the two.
5. The image replaces the target and its history gets one step. Its header, its
   [plate solution](../../reference/glossary.md#plate-solution) and its recorded
   [processing state](../../reference/glossary.md#processing-state) are kept.

### Model

With \(\lambda\) the measure of change at a pixel, the sum of the squared differences of the rescaled copy over the
channels, blurred, \(p\) the value of **Sharpness** and \(q\) that of **Anisotropy**, the strengths along the
structure and across it are

\[ n_\parallel = (1 + \lambda)^{-p/2}, \qquad n_\perp = (1 + \lambda)^{-p/(2(1 - q))} \tag{1} \]

Both are 1 where the image is flat and fall toward 0 where it changes strongly, \(n_\perp\) faster. A path that starts
in one direction has a strength \(n\) between the two. With \(A\) the value of **Amplitude** and \(P\) that of
**Precision**, it runs for

\[ L = P\,n\,\sqrt{2A} \tag{2} \]

pixels. With **Fast Approximation** off, a sample at a distance \(l\) along the path weighs

\[ w(l) = \exp\!\left(-\frac{l^2}{2\,(n\sqrt{2A})^2}\right) \tag{3} \]

and with it on, every sample weighs 1. With \(I\) the original sample, \(D\) the smoothed one held to 0 to 1, and \(a\)
the value of **Amount**, the result before the mask is

\[ M = (1 - a)\,I + a\,D \tag{4} \]

### Boundaries

- What G'MIC returns is held to 0 to 1, also in a floating-point image. An integer image is handed over as fractions
  and rounded back once to the nearest code, and a 64-bit floating-point image is narrowed to single precision.
- The paths stop at the edge of the frame, so the pixels near it are smoothed less, and from one side.
- An image that holds a sample that is not a finite number is refused with "the image holds a sample that is not a
  finite number, which G'MIC cannot process", and left unchanged. When G'MIC itself fails, on memory for example, the
  run reports its message and writes nothing.
- **Cancel**, or closing the window, asks G'MIC to stop, and it stops at its next check inside the command. A stopped
  run writes nothing.

### Worked example

The pair is made on a crop of 1,100 by 820 pixels from the middle of the linear master, with **Amplitude** at 5 and
the other values at their defaults.

The crop's own blurred copy runs from about 0.03 to 1.66, so one of its 255 steps is about 0.006, and the pixel noise
of the background, at most 0.0011, is a sixth of a step or less. There \(\lambda\) stays far below 1, and by equation
(1) both strengths stay near 1. By equation (2) a path runs \(2\sqrt{10} = 6.3\) pixels, 8 samples at a step of 0.80,
in each of 12 directions: the grain is averaged away. Suppose the flank of a star gives \(\lambda = 1000\). Then
\(n_\parallel = 0.089\), and every path from there runs 0.6 pixels and takes the pixel alone: the star keeps its shape.
At the default **Amplitude** of 60 the background's paths would run 22 pixels, far enough to reach nearby stars.

The few star cores above 1 come back as 1. With **Amount** at 0.50 instead, a background sample of 0.0300 that the
paths bring to 0.0280 would be written as 0.0290 by equation (4), and a star core of 1.40 as 1.20. Both crops are then
stretched by [Histogram Transformation](../stretching/histogram-transformation.md) with **Shadows** and **Midtones** at
0.02.

```figure
images:
  - image: ../../assets/images/processes/noise-reduction/noise-crop-before.webp
    alt: >-
      A crop at 100 % of the middle of the master, stretched: pink clouds of nebula beside a dark dust lane, many small
      stars, and a fine colored grain over the background.
    label: Before
  - image: ../../assets/images/processes/noise-reduction/graycstoration-after.webp
    alt: >-
      The same crop smoothed before the same stretch: the fine grain and the texture of the nebula softened, the stars
      round and a little softer, the edge of the dust lane kept.
    label: After
caption: >-
  The middle of the linear Carina Nebula master at 100 %, without and with GRAYCstoration at Amplitude 5 and the
  other values at their defaults, both then stretched by Histogram Transformation with Shadows and Midtones at 0.02.
```

## Starting points

| Situation | Start | Then adjust |
| --- | --- | --- |
| A linear master, first try | **Amplitude** near 5, the rest at their defaults | **Amplitude** up while grain remains, down at the first spike or smeared filament. **Amount** below 1 to keep some grain. |
| Edges and filaments must stay sharp | **Anisotropy** at 0.6 to 0.8 and **Sharpness** at 1.0 to 1.5 | **Noise Scale** up if the background turns into streaks. |
| Color noise | A chroma component in **Channel**, on a duplicate | Check colored edges and hues at 100 %. |
| Quick tries on a large image | The preview area | **Preview area** unticked for the last check. |

## When it goes wrong

!!! danger "Samples outside 0 to 1 are cut"
    What G'MIC returns is held to 0 to 1, also in a floating-point image. On a linear master, star cores above 1 become
    1 and samples below 0 become 0, at any **Amplitude**. Bring the image between 0 and 1 first, or protect the stars
    with a mask.

!!! warning "The defaults are strong on a linear image"
    The noise of a linear image is small against its range, so the paths run nearly their full length over the
    background and the faint nebula, about 22 pixels at the default **Amplitude**. They smear the nebula and reach
    into the stars, which grow thin spikes. Start with **Amplitude** near 5.

Small stars or faint filaments vanish or smear
:   **Cause.** Their changes are small against the image's range, so the paths stay long there.
:   **Fix.** Lower **Amplitude** or **Iterations**, raise **Sharpness**, or protect the structure with a mask.

The background turns into streaks, worms or a painted texture
:   **Cause.** The noise steers the paths, through a high **Anisotropy**, a low **Noise Scale** or many **Iterations**.
:   **Fix.** Lower **Anisotropy**, raise **Noise Scale** and **Regularity**, and use one iteration.

A stretched image is smoothed much less than a linear one
:   **Cause.** After a stretch the noise changes strongly against the range, so it counts as structure.
:   **Fix.** Smooth before the stretch, or raise **Noise Scale** and lower **Sharpness**.

Thin spikes radiate from the stars, or small stair steps show
:   **Cause.** Long paths from the flat background run into a star in each direction and carry its light out, and
    coarse sampling leaves steps.
:   **Fix.** Lower **Amplitude** or **Precision**. Against steps, choose **Linear** or **Runge-Kutta** and lower
    **Spatial Step Size** or **Angular Step Size**.

The border of the frame looks different from its middle
:   **Cause.** The paths stop at the edge, so the border is smoothed less and from one side.
:   **Fix.** Crop the border, or protect it with a mask.

The preview area differs from the result of Apply
:   **Cause.** The area is smoothed alone, with its own range and its own edges.
:   **Fix.** Untick **Preview area** for the last check before **Apply**.

"Error executing the process", with "the image holds a sample that is not a finite number, which G'MIC cannot process"
:   **Cause.** The image holds a sample that is not a number or is infinite, and nothing was written.
:   **Fix.** Replace or crop away those samples, and apply again.

**Preview** and **Apply** are disabled, and their tooltip names G'MIC
:   **Cause.** This build of AfterNight was made without G'MIC.
:   **Fix.** Use a build that includes G'MIC, or another denoiser.

## Related

- [Wavelet Denoise](wavelet-denoise.md), [Total Variation Denoise](total-variation-denoise.md) and
  [NL-Bayes](nl-bayes.md), the other ways to reduce noise.
- [Bilateral Filter](bilateral-filter.md) and [Gaussian Blur](gaussian-blur.md), which smooth over a fixed
  neighbourhood without following the structure.
- [Histogram Transformation](../stretching/histogram-transformation.md), the stretch that follows.
- [Masks](../../interface/masks.md), to smooth the background and protect the stars.
- [Image History](../../interface/image-history.md) and [Image Statistics](../../interface/image-statistics.md), to
  undo the step and to compare the noise before and after.
- [Process Windows](../../interface/process-windows.md), for the preview, the preview area and what every process
  window shares.

## Credits

- [G'MIC](https://gmic.eu) 3.5.4 and its CImg library, by David Tschumperlé and the G'MIC contributors at GREYC,
  under the CeCILL 2.1 or the CeCILL-C licence, at your choice. AfterNight runs its `smooth` command unchanged.
- [G'MIC `smooth`](https://gmic.eu/reference/smooth.html), for the command's arguments, its interpolation choices and
  its defaults, which AfterNight passes explicitly.
- David Tschumperlé,
  [Fast Anisotropic Smoothing of Multi-Valued Images using Curvature-Preserving PDE's](https://tschumperle.users.greyc.fr/publications/tschumperle_ijcv06.pdf),
  International Journal of Computer Vision 68 (2006), for the smoothing along curved paths behind GREYCstoration and
  the command.
