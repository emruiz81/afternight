---
kind: process
stage: noise
menu: Process > Noise Reduction > Patch PCA Denoise Filter
summary: >-
  Reduces noise with G'MIC's patch PCA filter: around evenly spaced positions it models how nearby patches differ,
  drops the components weaker than a multiple of the measured noise, and rebuilds the patches from the rest.
data_in: linear
data_out: linear
destructive: true
preview: real-time
sequences: true
masks: post-blend
external: The G'MIC library, bundled with AfterNight
glance:
  enabled: >-
    An image or a sequence view is active, no Patch PCA Denoise window is open on it yet, and no window that needs the
    image for itself is open on it. Preview and Apply also need a build that carries G'MIC.
  inputs: >-
    One grayscale or RGB image or sequence whose samples are all finite numbers, best between 0 and 1, and an optional
    mask of the image's size on its view.
  outputs: >-
    The same view, replaced, with one history step. Its size, channels, pixel type, header and recorded processing
    state are kept, and its samples are held between 0 and 1. No file is written.
  state: >-
    A calibrated and stacked linear master after gradient removal and color calibration, before the stretch.
derived: true
rewritten: true
---
# Patch PCA Denoise

Patch PCA Denoise reduces noise with the `denoise_patchpca` command of G'MIC, which AfterNight carries. At reference
positions spread evenly over the image, the command compares the patch there with every patch of a small window around
it, finds the main directions in which they differ, and keeps only the directions stronger than a multiple of the
noise it measures. Every patch of the window is rebuilt from those, and the overlapping patches are averaged. Unlike
NL-Bayes, it does not choose the patches that look alike: every patch of the window counts.

**Apply** replaces the image or [sequence](../../reference/glossary.md#sequence) with the denoised one, keeps its size
and [pixel type](../../reference/glossary.md#pixel-type), and adds one step to its
[history](../../reference/glossary.md#history).

```figure
image: ../../assets/images/processes/noise-reduction/patch-pca-denoise.webp
alt: >-
  The Patch PCA Denoise window. On the left, the Strength box at 4.00, Patch Size at 5, Lookup Size at 7 and Spatial
  Sampling at 7, each with its slider, and lower down Target Image, Channel at RGB and the Preview check box. On the
  right, a toolbar of zoom buttons, a channel box, the display mode box at Autostretch, a settings button, a switch and
  a menu button over the whole nebula, with the word PREVIEW in its corner and a small framed area labelled Preview:
  Patch PCA Denoise Filter in its middle. Reset, Close and Apply run along the bottom.
caption: >-
  Patch PCA Denoise on the linear Carina master with Strength at 4.00, Patch Size at 5, Lookup Size at 7 and Spatial
  Sampling at 7, the preview shown in the Autostretch display mode: the framed preview area is denoised, over the
  original.
pins:
  - {n: 2, x: 2.0, y: 82.7}
  - {n: 3, x: 64.5, y: 55.5}
  - {n: 4, x: 2.0, y: 20.1}
  - {n: 5, x: 2.0, y: 24.5}
  - {n: 6, x: 2.0, y: 33.5}
```

## Before you start

- Finish calibration, registration, stacking, gradient removal and color calibration first, and denoise the
  [master](../../reference/glossary.md#master) while it is linear, before the
  [stretch](../../reference/glossary.md#stretch). Every patch of the window enters the model, so walking noise,
  registration defects, gradients, [hot pixels](../../reference/glossary.md#hot-pixel) and compression texture are
  modelled as structure.
- Every sample must be a finite number, and is best between 0 and 1, where the result is held.
- The process needs G'MIC, which AfterNight includes: nothing is installed or downloaded. A build without it keeps
  **Preview** and **Apply** disabled.
- **Apply** replaces the image. Duplicate it first, or use its history, which keeps the step before.
- [Debayer](../../reference/glossary.md#debayer) a [CFA](../../reference/glossary.md#cfa) mosaic first. A raw mosaic
  is denoised as one grayscale plane. The frames of a sequence are read with the automatic debayer setting of its view.
- A [mask](../../reference/glossary.md#mask) on the view blends the result with the original afterwards.

## Steps

1. **Open the window.** Make the linear master the [active image](../../reference/glossary.md#active-image) and choose
   **Process > Noise Reduction > Patch PCA Denoise Filter**. The preview shows a framed
   [preview area](../../reference/glossary.md#preview-area) in the middle of the image, denoised once its run ends,
   and **Apply** is enabled at once.
2. **Choose what to denoise.** Leave **Channel** at **RGB**, or choose **CIE L* (Lightness)** when colors shift around
   weak detail.
3. **Place the preview area.** Move it over background, faint stars and an edge, and keep it small.
4. **Set the strength.** Raise **Strength** while the grain falls. Lower it when faint stars vanish or the background
   turns blotchy.
5. **Choose the patches.** Change **Patch Size** or **Lookup Size** one at a time. Smaller values run much faster.
6. **Keep the sampling dense enough.** Keep **Spatial Sampling** at or below **Patch Size** plus **Lookup Size**
   minus 1.
7. **Check at 100 %.** Zoom the preview to 100 %, hold ++space++ to compare, and look at each channel with
   **View Channel**.
8. **Apply.** Choose **Apply**. A progress window with a busy bar follows the run, the view shows the denoised image,
   and its history lists **Patch PCA Denoise** with the four values.

## Controls

**Target Image**, the preview pane, **Close** and the progress window work as in every process window: see
[Process Windows](../../interface/process-windows.md). What is particular to this window is below.

```controls
window: Patch PCA Denoise
controls:
  - name: Strength
    kind: spinbox
    default: "4.00"
    widget: {min: 0, max: 16, step: 0.01, decimals: 2}
    effect: Sets the cutoff as a multiple of the noise the command measures. In each local model, the components weaker than **Strength** times the noise are dropped. Higher drops more and smooths more. At 0 every component is kept.
    range: The slider moves it in 1,000 steps of 0.016. G'MIC's own default for the command is 1.8, and the window always sends its own value.
    enabled: Disabled while Apply runs.
    tradeoff: The cutoff is hard, so a high value removes faint stars, thin filaments and texture of low contrast at once, and leaves flat or blotchy areas. It costs no more time.
    scope: The preview and Apply, on what **Channel** selects, before the mask. The noise is measured once for the whole image or preview area, over all its channels, and is shown nowhere. Real edges and small, sharp stars can raise it.
    persistence: Until the window closes. **Reset** and a finished Apply return it to 4.00.
  - name: Patch Size
    kind: spinbox
    default: "7"
    widget: {min: 2, max: 21, step: 1}
    effect: Sets the side, in pixels, of the square patches the model is built on. Larger patches model broader structure, and smaller ones stay closer to the pixel.
    range: An even size reaches one pixel further on one side than on the other. The size is not checked against the size of the image.
    enabled: Disabled while Apply runs.
    tradeoff: A patch of side N is a vector of N² samples, so its model is a matrix of N⁴ values whose eigenvectors cost about N⁶ operations. From 7 to 14 the matrix grows 16 times and that work about 64 times. Large patches also mix unrelated detail.
    scope: The preview and Apply, on what **Channel** selects.
    persistence: Until the window closes. **Reset** and a finished Apply return it to 7.
  - name: Lookup Size
    kind: spinbox
    default: "11"
    widget: {min: 2, max: 21, step: 1}
    effect: Sets the side, in patch positions, of the window around each reference position. Every position of the window enters the model and is rebuilt from it. Larger takes a wider surrounding into each model.
    range: Every position counts, the reference's own included, alike or not. Its tooltip speaks of similar patches, but the command ranks and rejects none, and has no distance threshold and no group size.
    enabled: Disabled while Apply runs.
    tradeoff: The work grows with the square of the value, and so does the number of rebuilt patches added into the image. A large window mixes unrelated structure into one model.
    scope: The preview and Apply, on what **Channel** selects.
    persistence: Until the window closes. **Reset** and a finished Apply return it to 11.
  - name: Spatial Sampling
    kind: spinbox
    default: "7"
    widget: {min: 1, max: 16, step: 1}
    effect: Sets the step, in pixels along both axes, between the reference positions. A model is built only where both coordinates are multiples of it. Higher skips more positions and runs faster. Lower makes the estimates denser.
    range: G'MIC's own default is 5, and the window always sends its own value. Nothing holds it at or below **Patch Size** plus **Lookup Size** minus 1.
    enabled: Disabled while Apply runs.
    tradeoff: The number of positions falls with the square of the value, so from 7 to 1 it grows about 49 times. Above **Patch Size** plus **Lookup Size** minus 1, pixels between positions are rebuilt by no patch and come out black, and a sparse grid can leave uneven texture. The right and bottom edges can be left uncovered sooner.
    scope: The preview and Apply, on what **Channel** selects.
    persistence: Until the window closes. **Reset** and a finished Apply return it to 7.
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
    effect: Chooses what is denoised. **RGB** hands the command the three channels, and each is modelled on its own, with one noise level for all three. **Red**, **Green** or **Blue** denoises that channel and keeps the other two. Every other choice denoises one component and converts the image back.
    enabled: For an RGB image. A grayscale image shows a disabled **Grayscale**. Disabled while Apply runs.
    tradeoff: With **RGB** there is no joint color model, and each channel can keep a different number of components around weak detail, which can shift its color. A component costs two conversions and a working copy, and colors the conversion back puts out of range are cut in an integer image and kept in a floating-point one.
    scope: The preview and Apply, before the mask. Process Windows describes each component.
    persistence: Until the window closes. **Reset** leaves it as it is. A finished Apply and a new target return it to **RGB**.
  - name: Preview
    kind: check
    default: "On"
    effect: Shows the denoised image beside the controls, with the mask blended in.
    range: >-
      The pane opens with a **Preview area** of 350 by 350 pixels in the middle of the image, labelled "Preview: Patch
      PCA Denoise Filter", or the whole image when it is smaller. At the window's default size, **Preview area** and
      **Take snapshot** are in the menu at the end of the pane's toolbar.
    enabled: >-
      In a build without G'MIC it stays off and disabled, and its tooltip reads "This build of AfterNight carries no
      G'MIC library, so this process cannot run."
    tradeoff: Every change runs the whole command again, on one processor thread, so without the area each change on a large image can take a very long time. An Autostretch display can exaggerate leftover noise.
    scope: The window only. The area is denoised as an image of its own, with its own noise level, and near its edges the command repeats the area's edge pixels. Judge at **Zoom 100%**, since a fitted view hides black gaps, grid texture and lost stars. The pane offers **Display Mode**.
    persistence: Until the window closes. **Reset** leaves it as it is.
  - name: Reset
    kind: button
    default: ""
    effect: Returns the four values to their defaults.
    range: It leaves **Channel**, the target, **Preview** and the preview area as they are.
    enabled: Disabled while Apply runs.
  - name: Apply
    kind: button
    default: ""
    effect: Denoises the whole image, or every frame of the sequence, with the four values and **Channel** as they are when you press it, then returns the four values to their defaults.
    enabled: From the moment the window opens, at any values. In a build without G'MIC it is disabled, with the tooltip **Preview** shows.
    tradeoff: A single image shows a progress window too, with a busy bar, because the command reports no share done, and **Cancel**. A sequence's frames run one after another. A tightly packed floating-point image goes to G'MIC as it is, any other as a single-precision working copy, and the command adds an image the rebuilt patches are added into and an image of their weights. A component, a mask and a sequence add work and memory.
    persistence: >-
      The history step is **Patch PCA Denoise** and reads, for example, `Strength: 4.00, Patch Size: 5, Lookup Size: 7,
      Spatial Sampling: 7 in channel: RGB`. For a grayscale image it ends after the sampling. The header and the
      recorded processing state are kept.
```

## How it works

1. The samples **Channel** selects are checked, and an image that holds a sample that is not a finite number is
   refused. The others go to G'MIC in one session for the run, as single-precision
   [fractions of full scale](../../reference/glossary.md#fraction-of-full-scale), with the command `denoise_patchpca`
   and the four values.
2. The command measures one noise level for the whole image from its second differences, over every channel at once.
3. At each reference position and in each channel, it compares the patch there with every patch of the window, finds
   the principal components of their differences, keeps the leading ones above the cutoff, and rebuilds every patch
   of the window from them.
4. The rebuilt patches are added up with Gaussian weights and divided by the sum of the weights. The result is held
   between 0 and 1 and written back in the image's own pixel type, rounded to the nearest value.
5. A mask on the view blends the result with the original. The image replaces the target, its history gets one step,
   and its header and recorded [processing state](../../reference/glossary.md#processing-state) are kept.

### Model

With \(\Delta I\) the Laplacian of the image, the sum of its second differences along the two axes, the noise level
over every sample of every channel is

\[ \hat\sigma = \frac{1.4826}{\sqrt{20}}\,\operatorname{median}\bigl|\Delta I - \operatorname{median}\Delta I\bigr| \tag{1} \]

With \(N\) the value of **Patch Size**, \(L\) that of **Lookup Size**, \(X\) the patch at a reference position as a
vector of \(N^2\) samples, and \(Y_i\) the \(L^2\) patches of its window, the model is the matrix

\[ M = \frac{1}{L^2}\sum_{i=1}^{L^2} (Y_i - X)(Y_i - X)^{\mathsf T} \tag{2} \]

It is taken around \(X\), not around the mean of the patches, and no patch is left out. With \(\mu_j\) its
eigenvalues, from the largest, and \(t\) the value of **Strength**, the command keeps the leading components while

\[ \sqrt{|\mu_j|} \ge t\,\hat\sigma \tag{3} \]

With \(Q\) the eigenvectors and \(K\) the diagonal matrix of ones for the kept components and zeros for the others,
each patch of the window is rebuilt as

\[ \hat Y_i = X + Q K Q^{\mathsf T}(Y_i - X) \tag{4} \]

With \(w\) the Gaussian weight of a pixel inside its patch, whose standard deviation is half of **Patch Size**,
rounded down, a pixel's value over every rebuilt patch that covers it is

\[ \hat I = \frac{\sum w\,\hat Y}{\max\bigl(\sum w,\ 0.01\bigr)} \tag{5} \]

With \(S\) the value of **Spatial Sampling**, \(W\) and \(H\) the image's width and height, there are about
\(WH/S^2\) reference positions in each channel, and the work grows as

\[ \frac{WH}{S^2}\bigl(L^2 N^4 + N^6\bigr) \tag{6} \]

the first term for building and using the model, the second for its eigenvectors. One position reaches
\(N + L - 1\) pixels along each axis, so a larger \(S\) leaves pixels that no patch covers, which equation (5) makes 0.

### Boundaries

- An image that holds a sample that is not a finite number is refused with "the image holds a sample that is not a
  finite number, which G'MIC cannot process", and left unchanged.
- The result is held between 0 and 1, also in a floating-point image and at a **Strength** of 0. A 64-bit image is
  narrowed to single precision, and an integer image is rounded once to the nearest code. At a **Strength** of 0 the
  averaging, the hold and the rounding still change some samples.
- Beyond the edge, a patch repeats the edge pixel, and what a rebuilt patch puts outside the image is dropped. A patch
  or a window larger than the image is accepted and rests on repeated edge pixels.
- The command runs on one processor thread. **Cancel**, or closing the window, is noticed at G'MIC's next check,
  which the matrix work of a large patch delays, and on a sequence between frames. A stopped run writes nothing.
- The Windows and Linux builds can differ slightly: the command weighs its patches with each system's mathematics
  library, and a last-bit difference can change how many components a patch keeps. Each build repeats its own result.

### Worked example

The pair below is made on a crop of 1,100 by 820 pixels from the middle of the linear master, whose noise from one
pixel to the next is about 0.0003 in red, 0.001 in green and 0.0007 in blue. **Channel** is at **RGB**, **Strength**
at 4.00, **Patch Size** at 5, **Lookup Size** at 7 and **Spatial Sampling** at 7. If equation (1), which pools the
channels, measures 0.0007, equation (3) sets the cutoff at \(4 \times 0.0007 = 0.0028\). Of three components whose
spreads are 0.010, 0.004 and 0.002, the first two are kept and the third is set to zero, and a **Strength** of 2 would
keep all three.

Each patch is 25 samples, and its model a matrix of 625 values built from 49 patches. By equation (6), a position
costs \(49 \times 625 + 15{,}625 = 46{,}250\), against 408,170 at the defaults: about a ninth. Since
\(5 + 7 - 1 = 11\) is above 7, every pixel is covered. In the result, the brighter stars sit in small square halos
with colored rims. The crop and the result are then stretched by
[Histogram Transformation](../stretching/histogram-transformation.md) with **Shadows** and **Midtones** at 0.02, and
shown in Linear.

```figure
images:
  - image: ../../assets/images/processes/noise-reduction/noise-crop-before.webp
    alt: >-
      A crop at 100 % of the middle of the master, stretched: pink clouds of nebula beside a dark dust lane, many small
      stars, and a fine colored grain over the background.
    label: Before
  - image: ../../assets/images/processes/noise-reduction/patch-pca-denoise-after.webp
    alt: >-
      The same crop denoised before the same stretch: the grain of the background smoothed, and the brighter stars each
      inside a small square halo with colored rims.
    label: After
caption: >-
  The middle of the master at 100 %, without and with Patch PCA Denoise at Strength 4.00, Patch Size 5, Lookup Size 7
  and Spatial Sampling 7, both then stretched by Histogram Transformation with Shadows and Midtones at 0.02 and shown
  in the Linear display mode.
```

## Starting points

| Situation | Start | Then adjust |
| --- | --- | --- |
| A linear master, first look | The defaults, on the preview area | **Strength** first, then **Patch Size** or **Lookup Size**, one at a time. |
| A large master that must finish in reasonable time | **Patch Size** at 5, **Lookup Size** at 7, **Spatial Sampling** at 7 | Compare with the defaults on the preview area before you apply. |
| Faint stars or filaments vanish | A lower **Strength** | Compare with ++space++ under one display mode. |
| Colors shift around weak detail | **CIE L* (Lightness)** in **Channel**, on a duplicate | Compare the channels with **View Channel**. |

## When it goes wrong

!!! danger "Samples outside 0 to 1 are cut"
    The command's result is held between 0 and 1, also in a floating-point image and at a **Strength** of 0.
    Negative samples and star cores above 1 are lost. Bring the image between 0 and 1 before you denoise it.

!!! warning "A whole-image run can take very long"
    The command works on one processor thread, and its cost grows with the square of **Lookup Size** and faster
    still with **Patch Size**: a small change of **Patch Size** can multiply the time. Tune on the preview area, use
    smaller patches on a large master, and do not raise several sizes at once.

Black dots, black lines or a grid appear
:   **Cause.** **Spatial Sampling** leaves pixels that no rebuilt patch covers, between positions or at the right and
    bottom edges.
:   **Fix.** Lower **Spatial Sampling** to **Patch Size** plus **Lookup Size** minus 1 or less, and check the whole
    image at 100 %.

Faint stars or thin filaments disappear abruptly
:   **Cause.** Their components fell below the hard cutoff **Strength** sets.
:   **Fix.** Lower **Strength**, and compare under one display mode.

A larger **Lookup Size** changes the structure instead of matching it better
:   **Cause.** Every position of the window counts, alike or not.
:   **Fix.** Lower **Lookup Size** where unrelated structures meet.

Colors shift
:   **Cause.** Each channel keeps its own components, or a component was converted back.
:   **Fix.** Choose **CIE L* (Lightness)** on a duplicate, and compare the channels with **View Channel**.

"the image holds a sample that is not a finite number, which G'MIC cannot process"
:   **Cause.** The image holds a sample that is not a number or is infinite, such as the empty border of a registered
    frame. Nothing was written.
:   **Fix.** Repair or crop away those samples, and apply again.

**Preview** and **Apply** are disabled, and their tooltip names G'MIC
:   **Cause.** This build of AfterNight was made without G'MIC.
:   **Fix.** Use a build that includes G'MIC, or another denoiser of this group.

The applied image differs from the preview
:   **Cause.** The preview area is denoised alone, with its own noise level and its own edges, or the mask, the target
    or the channel changed.
:   **Fix.** Judge the area away from its edges, keep the mask, target and channel, and check the result at 100 %
    after Apply.

## Related

- [NL-Bayes](nl-bayes.md), which groups only the patches that look alike and estimates them in two passes.
- [Wavelet Denoise](wavelet-denoise.md) and [Total Variation Denoise](total-variation-denoise.md), which work scale by
  scale or by smoothing gradients.
- [Histogram Transformation](../stretching/histogram-transformation.md), the stretch that follows.
- [Image Viewer](../../interface/image-viewer.md), for the zoom, **View Channel** and the
  [Autostretch](../../reference/glossary.md#autostretch) display.
- [Masks](../../interface/masks.md) and [Image History](../../interface/image-history.md), to protect a region and to
  undo the step.
- [Process Windows](../../interface/process-windows.md), for the preview and what every process window shares.

## Credits

- [G'MIC](https://gmic.eu) 3.5.4 and its CImg library, by David Tschumperlé and the G'MIC contributors at GREYC,
  under the CeCILL 2.1 or the CeCILL-C licence, at your choice. AfterNight runs its `denoise_patchpca` command
  unchanged, and G'MIC computes the result. G'MIC's own filter that runs the command names David Tschumperlé and
  Jérôme Boulanger as its authors.
- [G'MIC `denoise_patchpca`](https://gmic.eu/reference/denoise_patchpca.html), for the command's four arguments and
  its own defaults of 1.8, 7, 11 and 5. AfterNight passes all four values itself.
- [G'MIC `std_noise`](https://gmic.eu/reference/std_noise.html), for the noise level of equation (1).
- [G'MIC mathematical expressions](https://gmic.eu/reference/mathematical_expressions.html), for the image median and
  the eigen decomposition the command uses.
- [G'MIC `pca`](https://gmic.eu/reference/pca.html), for the projection on eigenvectors and the rebuilding from them.
  The command does its own local projection with a hard cutoff, and does not call `pca`.
- [G'MIC on GitHub](https://github.com/GreycLab/gmic), for the project, its authors and the publication it asks to be
  cited.
