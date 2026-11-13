---
kind: process
stage: noise
menu: Process > Noise Reduction > Wavelet Denoise Filter
summary: >-
  Reduces noise with G'MIC: shrinks the Haar wavelet coefficients of the image toward zero by a threshold set from an
  estimate of its noise, and averages several shifted reconstructions.
data_in: either
data_out: either
destructive: true
preview: real-time
sequences: true
masks: post-blend
external: The G'MIC library, bundled with AfterNight
glance:
  enabled: >-
    An image or a sequence view is active, no Wavelet Denoise window is open on it yet, and no window that needs the
    image for itself is open on it. Preview and Apply also need a build that carries G'MIC.
  inputs: >-
    One grayscale or RGB image or sequence whose samples are all finite numbers, best between 0 and 1, and an
    optional mask of the image's size on its view.
  outputs: >-
    The same view, replaced, with one history step. Its size, channels, pixel type, header and recorded processing
    state are kept, and what G'MIC returns is held between 0 and 1. No file is written.
  state: >-
    A calibrated and stacked master, linear or early in the stretch, after gradient removal and color calibration and
    before a strong stretch raises its noise.
derived: true
---
# Wavelet Denoise

Wavelet Denoise reduces noise scale by scale. It splits the image into Haar wavelet coefficients, which hold the
detail of square blocks of growing size, shrinks every coefficient toward zero by a threshold set from an estimate of
the image's noise, and builds the image again. It does this on several shifted copies and averages them, which hides
the block pattern of the Haar transform. The work is done by the `denoise_haar` command of G'MIC, which AfterNight
carries.

**Apply** replaces the image or [sequence](../../reference/glossary.md#sequence) with the denoised one, keeps its size
and [pixel type](../../reference/glossary.md#pixel-type), and adds one step to its
[history](../../reference/glossary.md#history).

```figure
image: ../../assets/images/processes/noise-reduction/wavelet-denoise.webp
alt: >-
  The Wavelet Denoise window. On the left, the Threshold, Scales, Iterations and Amount boxes with their sliders, and
  lower down Target Image, Channel at RGB and the Preview check box. On the right, a toolbar of zoom buttons, a channel
  box and a display mode box reading Autostretch over the preview of a red nebula, with the word PREVIEW in its corner
  and a small framed area in its middle. Reset, Close and Apply run along the bottom.
caption: >-
  Wavelet Denoise on the linear master with its defaults, Threshold at 1.40, Scales at 6, Iterations at 10 and Amount
  at 1.00. The framed area shows what Apply writes there, over the original, in the Autostretch display mode.
pins:
  - {n: 2, x: 2.0, y: 82.7}
  - {n: 3, x: 63.0, y: 55.2}
  - {n: 4, x: 2.0, y: 20.1}
  - {n: 5, x: 2.0, y: 24.6}
  - {n: 6, x: 2.0, y: 29.1}
  - {n: 7, x: 2.0, y: 33.6}
  - {n: 9, x: 89.2, y: 96.0}
```

## Before you start

- Finish calibration, stacking, gradient removal and color calibration first, and denoise before a strong
  [stretch](../../reference/glossary.md#stretch) raises the noise. The threshold cannot tell faint signal from noise of
  the same size.
- **Apply** replaces the image. Duplicate it first, or use its history, which keeps the step before.
- Every sample must be a finite number, and what G'MIC returns is held between 0 and 1.
- The process needs G'MIC, which AfterNight includes: nothing is installed or downloaded. A build without it keeps
  **Preview** and **Apply** disabled, and their tooltip says why.
- [Debayer](../../reference/glossary.md#debayer) a [CFA](../../reference/glossary.md#cfa) mosaic first. A raw mosaic is
  denoised as one grayscale plane. The frames of a sequence are read with the automatic debayer setting of its view.
- A [mask](../../reference/glossary.md#mask) on the view blends the result with the original after **Amount**. On a
  sequence, every frame gets the same four values, **Channel** and mask.

## Steps

1. **Open the window.** Make the [master](../../reference/glossary.md#master) the
   [active image](../../reference/glossary.md#active-image) and choose
   **Process > Noise Reduction > Wavelet Denoise Filter**. The preview shows the denoised image inside a framed
   [preview area](../../reference/glossary.md#preview-area) in the middle of the image.
2. **Choose what to denoise.** Leave **Channel** at **RGB** to denoise the three channels with one threshold. Choose
   **CIE L* (Lightness)** to smooth the brightness alone, or a chroma component for color noise.
3. **Place the area.** Move the frame over smooth background, faint nebula and a few small stars. The noise estimate
   of the preview is measured inside it.
4. **Set the threshold.** Raise **Threshold** until the grain of the background fades, and lower it when small stars
   and faint filaments turn soft or blocky.
5. **Set the scales.** Lower **Scales** when broad structure or the level of the background moves. Raise it only when
   blotches larger than a few pixels remain.
6. **Leave the iterations.** Keep **Iterations** at 10, and lower it only to try values faster.
7. **Set the strength.** Lower **Amount** to bring back part of the original when the result looks waxy.
8. **Check at 100 %.** Zoom into the area and hold ++space++ to compare with the original. Then untick **Preview area**
   to see the whole image.
9. **Apply.** Choose **Apply**. A progress window with a busy bar follows the run, the view shows the denoised image,
   and its history lists **Wavelet Denoise** with the four values.

## Controls

**Target Image**, the preview pane, **Close** and the progress window work as in every process window: see
[Process Windows](../../interface/process-windows.md). What is particular to this window is below.

```controls
window: Wavelet Denoise
controls:
  - name: Threshold
    kind: spinbox
    default: "1.40"
    widget: {min: 0, max: 10, step: 0.01, decimals: 2}
    effect: Sets the threshold as a multiple of the image's noise estimate. Every coefficient smaller than the threshold in size becomes 0, and every larger one moves that much closer to 0. Higher removes more noise and more faint detail. At 0 nothing is removed.
    range: The slider moves the value in 1,000 equal steps of 0.01.
    enabled: Disabled while Apply runs. **Apply** stays enabled at 0.
    tradeoff: One threshold acts on every scale and on the coarsest layer, so a high value also lowers broad levels and can leave blocks around stars and sharp edges. It costs no more time.
    scope: The preview and Apply, on what **Channel** selects, before **Amount** and the mask.
    persistence: Until the window closes. **Reset** and a finished Apply return it to its default.
  - name: Scales
    kind: spinbox
    default: "6"
    widget: {min: 2, max: 10, step: 1}
    effect: Sets how many times the transform halves the image. The details of the last scale span blocks of 2 to the power of **Scales** pixels, 64 at the default. Higher reaches broader blotches of noise, and broader structure too.
    range: The command uses at most one less than the base-2 logarithm of the image's shorter side, rounded down. That is 7 over the 500-pixel preview area and 11 over a frame whose shorter side is 5,429 pixels. There is no automatic choice.
    enabled: Disabled while Apply runs.
    tradeoff: The image is padded with zeros to a multiple of 2 to the power of **Scales** plus one, 128 pixels at 6 scales and 2,048 at 10, which pads a 7246 x 5429 frame to 8192 x 6144. More scales add work and memory, and expose broad structure and the edges of the frame to the threshold.
    scope: The preview and Apply, on what **Channel** selects.
    persistence: Until the window closes. **Reset** and a finished Apply return it to its default.
  - name: Iterations
    kind: spinbox
    default: "10"
    widget: {min: 1, max: 32, step: 1}
    effect: Sets how many shifted copies are denoised and averaged. More copies even out the block pattern of the Haar transform. It is no count toward convergence and does not raise the threshold.
    range: Each copy is shifted by a random amount of up to four times the number of scales, in pixels, along each axis, and the image wraps around. The shifts are drawn the same way on every run, so one image and one set of values always give the same result.
    enabled: Disabled while Apply runs.
    tradeoff: The time grows in proportion to it. Its tooltip says that more iterations increase smoothing. They average more copies of one denoise, which evens out its blocks rather than raising its strength.
    scope: The preview and Apply, on what **Channel** selects.
    persistence: Until the window closes. **Reset** and a finished Apply return it to its default. The shifts are not recorded.
  - name: Amount
    kind: spinbox
    default: "1.00"
    widget: {min: 0, max: 1, step: 0.01, decimals: 2}
    effect: Mixes the denoised image with the original. At 1 the result is the denoised image, at 0.5 halfway, and at 0 the original.
    range: The slider moves the value in 1,000 equal steps.
    enabled: Disabled while Apply runs. **Apply** stays enabled at 0.
    tradeoff: It brings back noise and detail together. The command runs in full at any value, and below 1 the mix adds one pass over the image.
    scope: The preview and Apply, on the whole image once **Channel** has been converted back, before the mask.
    persistence: Until the window closes. **Reset** and a finished Apply return it to its default.
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
    effect: Chooses what is denoised. **RGB** hands the command the three channels as one color image, with one noise estimate and one threshold for all three, each channel transformed on its own. **Red**, **Green** or **Blue** denoises that channel, with its own estimate, and keeps the other two. Every other choice denoises one component of another color model and converts the image back.
    enabled: For an RGB image. A grayscale image shows a disabled **Grayscale**. Disabled while Apply runs.
    tradeoff: With **RGB**, a channel noisier than the others keeps more of its noise, a cleaner one loses more detail, and colors can shift. A component costs two conversions and a working copy, and the conversion back can change colors. A hue is denoised as a line, not as a circle.
    scope: The preview and Apply, before **Amount** and the mask. Process Windows describes each component.
    persistence: Until the window closes. **Reset** leaves it as it is. A finished Apply and a new target return it to **RGB**.
  - name: Preview
    kind: check
    default: "On"
    effect: Shows the denoised image beside the controls, with **Amount** and the mask applied.
    range: >-
      The pane opens with a **Preview area** of 500 by 500 pixels in the middle of the image, labelled "Preview: Wavelet
      Denoise Filter", or the whole image when it is smaller. At the window's default size, **Preview area** and **Take
      snapshot** are in the menu at the end of the pane's toolbar.
    enabled: >-
      In a build without G'MIC it stays off and disabled, and its tooltip reads "This build of AfterNight carries no
      G'MIC library, so this process cannot run."
    tradeoff: The area is denoised as an image of its own, with its own noise estimate, its own padding and edges, and at most 7 scales. Every change runs the whole command again. **Apply**, or closing the window, stops a preview on its way inside the command.
    scope: The window only. On a sequence it shows the frame the view displays. **Display Mode** and **Autostretch Settings** change only how the preview is shown.
    persistence: Until the window closes. **Reset** leaves it as it is.
  - name: Reset
    kind: button
    default: ""
    effect: Returns the four values to their defaults, and one preview of them follows. It leaves **Channel** as it is.
    enabled: Disabled while Apply runs.
  - name: Apply
    kind: button
    default: ""
    effect: Denoises the whole image, or every frame of the sequence, with the four values and **Channel** as they are when you press it, and then returns the four values to their defaults.
    enabled: From the moment the window opens, at any values. In a build without G'MIC it is disabled, with the tooltip **Preview** shows.
    tradeoff: A single image shows a progress window too, with a busy bar, because the command reports no share done, and its **Cancel** stops the run. The command runs on one thread and holds several copies of the padded image in memory, beside the second copy every run needs.
    persistence: "The history step is **Wavelet Denoise** and reads, for example, `Threshold: 1.40, Scales: 6, Iterations: 10, Amount: 1.00 in channel: RGB`. For a grayscale image it ends after the amount. The noise estimate, the scales the command used, the shifts, the mask and the G'MIC version are not recorded. The header cards, the plate solution and the recorded processing state are kept."
```

## How it works

1. The channels **Channel** selects are checked: an image that holds a sample that is not a finite number is refused.
   Otherwise they go to G'MIC, in one session for the run, as single-precision
   [fractions of full scale](../../reference/glossary.md#fraction-of-full-scale).
2. The command estimates the noise of the image once, from its Laplacian, and pads the image with zeros at its right
   and bottom to a size the transform can halve **Scales** times.
3. For each of **Iterations** copies, it shifts the image with wraparound, takes the Haar transform, shrinks every
   coefficient by **Threshold** times the noise estimate, transforms back and undoes the shift. The copies are
   averaged and cut back to the image's size.
4. The result is held to 0 to 1 and written back in the image's own pixel type. **Amount** mixes it with the
   original, and a mask on the view then blends the two.
5. The image replaces the target and its history gets one step. Its header, its
   [plate solution](../../reference/glossary.md#plate-solution) and its recorded
   [processing state](../../reference/glossary.md#processing-state) are kept.

### Model

With \(L\) the Laplacian of the image, each sample's four neighbours less four times the sample, taken over every
channel handed over, the noise estimate is

\[ \hat\sigma = \frac{1.4826\ \operatorname{median}\bigl|L - \operatorname{median}(L)\bigr|}{\sqrt{20}} \tag{1} \]

For noise that changes from one pixel to the next, it is the noise's standard deviation. The transform keeps that
deviation in every detail coefficient, so one threshold suits every scale. With \(k\) the value of **Threshold**, every
coefficient \(c\), the coarsest layer's included, becomes

\[ S(c) = \operatorname{sign}(c)\,\max\bigl(|c| - k\hat\sigma,\ 0\bigr) \tag{2} \]

The coarsest layer holds each block of \(2^s\) by \(2^s\) pixels, with \(s\) the number of scales, as \(2^s\) times
its mean. Where that is above the threshold, equation (2) lowers the level of every pixel of the block by

\[ \Delta = \frac{k\hat\sigma}{2^s} \tag{3} \]

With \(I\) the original sample, \(D\) the average of the copies held to 0 to 1, and \(a\) the value of **Amount**,
the result before the mask is

\[ M = (1 - a)\,I + a\,D \tag{4} \]

### Boundaries

- At a **Threshold** of 0 the command gives the image back, apart from rounding and the hold to 0 to 1. At an
  **Amount** of 0 the result is the original. The command runs in full in every case.
- What G'MIC returns is held to 0 to 1, also in a floating-point image. An integer image is handed over as fractions
  and rounded back once to the nearest code, and a 64-bit floating-point image is narrowed to single precision.
- The padding is zeros, without interpolation, and the shifts wrap around the padded image. Near the edges of the
  frame, the blocks mix the image with these zeros or, through the wraparound, with its opposite edge.
- An image that holds a sample that is not a finite number is refused with "the image holds a sample that is not a
  finite number, which G'MIC cannot process", and left unchanged. When G'MIC itself fails, on memory for example, the
  run reports its message and writes nothing. An image whose shorter side is below 8 pixels gets fewer than 2 scales,
  and a very small one can make the command fail.
- **Cancel**, or closing the window, asks G'MIC to stop, and it stops at its next check inside the command. A stopped
  run writes nothing.

### Worked example

The image is the linear master. Its noise from one pixel to the next is about 0.0003 in red, 0.001 in green and
0.0007 in blue. The window keeps its defaults: **Threshold** at 1.40, **Scales** at 6, **Iterations** at 10,
**Amount** at 1.00 and **Channel** at **RGB**.

With **RGB**, equation (1) takes one median over the three channels, which comes to about 0.0006. The threshold is
then \(1.40 \times 0.0006 = 0.00084\): nearly three times the red noise, and less than the green. By equation (2) a
coefficient of 0.0005 becomes 0 and one of 0.0030 becomes 0.0022: the grain goes, and stronger structure keeps most
of its contrast. By equation (3) the background's level falls by \(0.00084 / 64\), about 0.00001. At 2 scales it
would fall by 0.0002, two thirds of the red noise. The few star cores above 1 are cut to 1. With **Amount** at 0.50
instead, a sample of 0.0300 that the command brings to 0.0290 is written as 0.0295 by equation (4).

For the pair, the middle of the master is cropped first and denoised alone, so the noise estimate is the crop's
own. Both pictures are then stretched by [Histogram Transformation](../stretching/histogram-transformation.md) with
**Shadows** and **Midtones** at 0.02.

```figure
images:
  - image: ../../assets/images/processes/noise-reduction/noise-crop-before.webp
    alt: >-
      A crop at 100 % of the middle of the master, stretched: pink clouds of nebula beside a dark dust lane, many small
      stars, and a fine colored grain over the background.
    label: Before
  - image: ../../assets/images/processes/noise-reduction/wavelet-denoise-after.webp
    alt: >-
      The same crop denoised before the same stretch: the grain of the background and of the faint nebula weaker, its
      colored speckle most of all, the stars and the edges of the dust lane kept.
    label: After
caption: >-
  The middle of the Carina Nebula master at 100 %, without and with Wavelet Denoise at the window's defaults, both then
  stretched by Histogram Transformation with Shadows and Midtones at 0.02. Both are shown in the Linear display mode.
```

## Starting points

| Situation | Start | Then adjust |
| --- | --- | --- |
| A linear master, first try | The defaults, the area over background and faint nebula | **Threshold** from 1.0 to 1.4, **Amount** at 1. Then **Amount** toward 0.5 to 0.8 if it looks waxy. |
| Noise at broader scales | **Scales** from 4 to 6, chosen first | **Threshold** up slowly. Check broad structure and the edges at 100 %. |
| Channels of unequal noise, or color noise | **Red**, **Green** and **Blue** in turn, or a component on a duplicate | **Threshold** for each. Check the colors. |
| Quick tries on a large image | Fewer **Iterations**, in the preview area | Back to 10, with **Preview area** unticked, for the last check. |

## When it goes wrong

!!! danger "Samples outside 0 to 1 are cut"
    What G'MIC returns is held to 0 to 1, also in a floating-point image. On a linear master, star cores above 1 become
    1 and samples below 0 become 0, at any **Threshold**. Bring the image between 0 and 1 first, or protect the stars
    with a mask.

!!! warning "Iterations does not make the denoise stronger"
    **Iterations** is the number of shifted copies averaged, not a count toward convergence, whatever its tooltip
    says. Set the strength with **Threshold**, **Scales** and **Amount**, and leave **Iterations** for the blocks.

Stars and faint filaments turn soft or waxy
:   **Cause.** The threshold removes the coefficients of faint detail along with the noise.
:   **Fix.** Lower **Threshold** or **Amount**, or protect the structure with a mask.

Blocks or squares show around stars and sharp edges
:   **Cause.** The Haar transform works in blocks, and a high **Threshold**, many **Scales** or few copies leave them.
:   **Fix.** Lower **Threshold** or **Scales**, raise **Iterations**, or use a feathered mask.

Broad structure or the background's level moves
:   **Cause.** The threshold reaches the broad scales and the coarsest layer too.
:   **Fix.** Lower **Scales**, then **Threshold**, and bring back the original with **Amount**.

Colors shift, or one channel stays noisier
:   **Cause.** With **RGB** the three channels share one noise estimate and one threshold.
:   **Fix.** Denoise **Red**, **Green** and **Blue** one at a time, each with its own estimate, or compare a
    component on a duplicate.

The border of the frame looks different from its middle
:   **Cause.** Near the edges the blocks meet the zero padding and the other side of the frame.
:   **Fix.** Lower **Scales**, or crop the border after the denoise.

The preview area differs from the result of Apply
:   **Cause.** The area is denoised alone, with its own noise estimate, its own edges and at most 7 scales.
:   **Fix.** Untick **Preview area** for the last check before **Apply**.

"Error executing the process", with "the image holds a sample that is not a finite number, which G'MIC cannot process"
:   **Cause.** The image holds a sample that is not a number or is infinite, and nothing was written.
:   **Fix.** Replace or crop away those samples, and apply again.

**Preview** and **Apply** are disabled, and their tooltip names G'MIC
:   **Cause.** This build of AfterNight was made without G'MIC.
:   **Fix.** Use a build that includes G'MIC, or another denoiser.

## Related

- [Total Variation Denoise](total-variation-denoise.md), [NL-Bayes](nl-bayes.md),
  [Patch PCA Denoise](patch-pca-denoise.md) and [DeepSNR](deepsnr.md), the other ways to reduce noise.
- [Background Extraction](../background-extraction/background-extraction.md) and
  [Photometric Color Calibration](../color-calibration/photometric-cc.md), which need linear data and come before.
- [Histogram Transformation](../stretching/histogram-transformation.md), the stretch that follows.
- [Masks](../../interface/masks.md), to denoise the background and protect the stars.
- [Image History](../../interface/image-history.md) and [Image Statistics](../../interface/image-statistics.md), to
  undo the step and to compare the noise before and after.
- [Process Windows](../../interface/process-windows.md), for the preview, the preview area and what every process
  window shares.

## Credits

- [G'MIC](https://gmic.eu) 3.5.4 and its CImg library, by David Tschumperlé and the G'MIC contributors at GREYC,
  under the CeCILL 2.1 or the CeCILL-C licence, at your choice. AfterNight runs its `denoise_haar` command, written by
  David Tschumperlé, unchanged, with its random generator seeded with a fixed value for the run.
- [G'MIC `denoise_haar`](https://gmic.eu/reference/denoise_haar.html), for the command's purpose, its arguments and its
  defaults.
- [G'MIC `std_noise`](https://gmic.eu/reference/std_noise.html), for the noise estimate.
- [Wavelet Analysis and Synthesis in G'MIC](https://gmic.eu/oldtutorial/wavelet-analysis-and-synthesis-in-gmic.html),
  for the Haar transform and its inverse.
