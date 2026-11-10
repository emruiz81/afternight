---
kind: process
stage: finish
menu: Process > Details > Multiscale Alpha Sharpen
summary: >-
  Sharpens several scales at once with G'MIC: splits the image into detail layers of growing size, makes the finer
  layers more opaque and lays them back over the base.
data_in: nonlinear
data_out: nonlinear
destructive: true
preview: real-time
sequences: true
masks: post-blend
external: The G'MIC library, bundled with AfterNight
glance:
  enabled: >-
    An image or a sequence view is active, no window of this process is open on it yet, and no window that needs the
    image for itself is open on it. Preview and Apply also need a build that carries G'MIC.
  inputs: >-
    One grayscale or RGB image or sequence whose samples are all finite numbers, best between 0 and 1, and an
    optional mask of the image's size on its view.
  outputs: >-
    The same view, replaced, with one history step. Its size, channels, pixel type, header and recorded processing
    state are kept. No file, preset or report is written.
  state: >-
    A stretched image after noise reduction, whose detail at several scales, from single pixels to broad structure,
    needs more contrast.
derived: true
rewritten: true
---
# Sharpen Alpha

This process sharpens several scales at once. It splits the image into a smooth base and detail layers of growing
size, makes the finer layers more opaque, and lays them back over the base, so the finest detail gains the most and
broad structure the least. **Amount** then mixes the result with the original. The work is done by the `sharpen_alpha`
command of G'MIC, which AfterNight carries. The menu names the process **Multiscale Alpha Sharpen**, and its window is
titled **Multiscale Sharpen Alpha**.

**Apply** replaces the image or [sequence](../../reference/glossary.md#sequence) with the sharpened one, keeps its size
and [pixel type](../../reference/glossary.md#pixel-type), and adds one step to its
[history](../../reference/glossary.md#history).

```figure
image: ../../assets/images/processes/sharpening/sharpen-alpha.webp
alt: >-
  The Multiscale Sharpen Alpha window. On the left, the Amplitude, Number of Scales, Anisotropy and Amount boxes with
  their sliders, and lower down Target Image, Channel at RGB and the Preview check box. On the right, a toolbar of
  zoom buttons, a channel box, a switch, the Preview area check box and a camera button over the preview of a red
  nebula, with the word PREVIEW in its corner and a small framed area in its middle. Reset, Close and Apply run along
  the bottom.
caption: >-
  Multiscale Sharpen Alpha on the stretched master with Amplitude at 1.00, Number of Scales at 5, Anisotropy at 0.00
  and Amount at 0.70. The framed area shows what Apply writes there, over the original.
pins:
  - {n: 2, x: 2.0, y: 82.7}
  - {n: 3, x: 2.0, y: 25.1}
  - {n: 4, x: 2.0, y: 30.2}
  - {n: 5, x: 2.0, y: 20.1}
  - {n: 6, x: 2.0, y: 35.2}
  - {n: 7, x: 59.5, y: 54.6}
  - {n: 8, x: 89.3, y: 96.0}
```

## Before you start

- Finish calibration, stacking, gradient removal and noise reduction first. The detail layers hold the noise, the
  [hot pixels](../../reference/glossary.md#hot-pixel) left over, [ringing](../../reference/glossary.md#ringing) and
  compression edges as well as real structure.
- **Apply** replaces the image. Duplicate it first, or use its history, which keeps the step before.
- [Debayer](../../reference/glossary.md#debayer) a [CFA](../../reference/glossary.md#cfa) mosaic first. A raw mosaic is
  sharpened as one grayscale plane, sensor pattern and all. The frames of a sequence are read with the automatic
  debayer setting of its view, which can turn mosaic frames into RGB.
- Bring the samples between 0 and 1. An image that holds a sample that is not a finite number is refused.
- A [mask](../../reference/glossary.md#mask) on the view blends the result with the original afterwards, so you can
  sharpen the nebula and leave the background alone.
- On a sequence, every frame gets the same four values, **Channel** and mask.

## Steps

1. **Open the window.** Make the stretched image the active one and choose **Process > Details > Multiscale Alpha
   Sharpen**. The preview shows the sharpened image inside a framed
   [preview area](../../reference/glossary.md#preview-area) in the middle of the image, over the original.
2. **Choose what to sharpen.** Leave **Channel** at **RGB** to sharpen the color image as a whole. Choose **CIE L*
   (Lightness)** or **CIE Y (Luminance)** to sharpen the brightness and leave the colors alone.
3. **Choose the scales.** Set **Number of Scales**: each scale adds a detail layer twice as broad as the last. Stop at
   the size of the largest structure you want to sharpen.
4. **Choose how the scales are made.** Set **Anisotropy** to 0 for plain Gaussian blurs, the case the model below
   describes. Above 0, each scale gets a bilateral blur and a small extra blur.
5. **Set the boost.** Raise **Amplitude** until the fine detail in the area gains contrast, and lower it when it turns
   crunchy.
6. **Set the strength.** Lower **Amount** to mix the result back toward the original, until the rims and the grain
   are no longer distracting.
7. **Check at 100 %.** Zoom into the area and hold ++space++ to compare with the original. Then untick **Preview area**
   and look at the whole image for broad rims around bright stars.
8. **Apply.** Choose **Apply**. A progress window with a busy bar follows the run, the view shows the sharpened image,
   and its history lists **Multiscale Sharpen Alpha** with the four values.

## Controls

**Target Image**, **Preview**, the preview pane, **Close** and the progress window work as in every process window:
see [Process Windows](../../interface/process-windows.md). What is particular to this window is below.

```controls
window: Multiscale Sharpen Alpha
controls:
  - name: Amplitude
    kind: spinbox
    default: "1.00"
    widget: {min: 0, max: 4, step: 0.1, decimals: 2}
    effect: Sets how much more opaque the detail layers are made. The finest layer's opacity is multiplied by 1 + **Amplitude**, the broadest layer's by 1, and those between by steps in between. Higher sharpens more, the finest scales most. At 0 nothing is raised.
    range: It is the command's own factor, which G'MIC's filter shows as a percentage, 1 for 100 % and 4 for 400 %. The slider moves the value in 1,000 equal steps.
    enabled: Disabled while Apply runs.
    tradeoff: A high value raises the noise and the rims around stars with the detail, and pushes more samples to black or white. It costs no more time.
    scope: The preview and Apply, on what **Channel** selects, before **Amount** and the mask.
    persistence: Until the window closes. **Reset** and a finished Apply return it to its default.
  - name: Number of Scales
    kind: spinbox
    default: "4"
    widget: {min: 1, max: 10, step: 1}
    effect: Sets how many layers the image is split into, the base included. Each scale blurs about twice as wide as the one before, so a higher value takes broader structure into the sharpening. At 1 the image is not split and nothing changes.
    range: The command uses at most as many scales as the base-2 logarithm of the image's shorter side, rounded down. That is 9 over a preview area of 600 pixels and 12 over an image whose shorter side is 5,000 pixels.
    enabled: Disabled while Apply runs.
    tradeoff: More scales add broader rims around stars and large edges. Each one adds a wider blur, a layer in memory and one more step of laying the layers back, so the work grows with the count.
    scope: The preview and Apply, on what **Channel** selects.
    persistence: Until the window closes. **Reset** and a finished Apply return it to its default.
  - name: Anisotropy
    kind: spinbox
    default: "0.50"
    widget: {min: 0, max: 1, step: 0.05, decimals: 2}
    effect: Chooses how each scale is blurred. At 0 with a Gaussian. Above 0 with a bilateral blur, which smooths less across an edge where the samples differ enough, followed by a Gaussian of 0.5 + **Anisotropy** pixels.
    range: >-
      The command sets the bilateral blur's tolerance to (1 - **Anisotropy**) x 100, for samples from 0 to 255, and
      AfterNight hands it fractions from 0 to 1. Up to 0.95 the tolerance is so wide that the bilateral blur smooths
      across edges almost as a Gaussian does, and the extra blur makes most of the difference. At 1 the tolerance is
      0, the bilateral blur hardly smooths, and each scale is little more than the extra blur.
    enabled: Disabled while Apply runs.
    tradeoff: Every value above 0 changes how the scales are made, not only how much, so the result does not move smoothly away from the one at 0. It adds a second blur to every scale. It is no control of halos, and settings taken from G'MIC's own filter do not give the same result here.
    scope: The preview and Apply, on what **Channel** selects.
    persistence: Until the window closes. **Reset** and a finished Apply return it to its default.
    note: The arrows step by one power of ten below the value, which is 0.01 over this range. The slider moves it in 1,000 equal steps. Its tooltip calls it a directional factor, though no direction is chosen.
  - name: Amount
    kind: spinbox
    default: "1.00"
    widget: {min: 0, max: 1, step: 0.05, decimals: 2}
    effect: Mixes the sharpened image with the original. At 1 the result is the sharpened image, at 0.5 halfway, and at 0 the original.
    range: The arrows step by 0.01, as for **Anisotropy**. The slider moves the value in 1,000 equal steps.
    enabled: Disabled while Apply runs.
    tradeoff: It weakens every scale together, which makes it the safest last control of the strength. At 0 the command still runs in full and costs as much.
    scope: The preview and Apply, after the command and before the mask.
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
    effect: Chooses what is sharpened. **RGB** hands the command the three channels as one color image. **Red**, **Green** or **Blue** sharpens that channel and keeps the other two. Every other choice sharpens one component of another color model and converts the image back.
    enabled: For an RGB image. A grayscale image shows a disabled **Grayscale**. Disabled while Apply runs.
    tradeoff: With **RGB** the three channels share the opacity of each detail layer, so a strong edge in one channel limits or shapes the sharpening of the other two there, and colors can change. A component costs two conversions and a working copy. Colors the conversion back puts out of range are cut in an integer image and kept in a floating-point one.
    scope: The preview and Apply, before **Amount** and the mask. Process Windows describes each component.
    persistence: Until the window closes. **Reset** leaves it as it is. A finished Apply and a new target return it to **RGB**.
  - name: Preview
    kind: check
    default: "On"
    effect: Shows the sharpened image beside the controls, with **Amount** and the mask applied.
    range: >-
      The pane opens with a **Preview area** of 600 by 600 pixels in the middle of the image, labelled "Preview:
      Multiscale Sharpen Alpha", or the whole image when it is smaller.
    enabled: >-
      In a build without G'MIC it stays off and disabled, and its tooltip reads "This build of AfterNight carries no
      G'MIC library, so this process cannot run."
    tradeoff: Without the area, every change runs the command over the whole image, also while you drag a slider. The area is sharpened as an image of its own, so near its edges the broad scales differ from what Apply writes there.
    scope: The window only. The pane has no **Display Mode** and shows the samples as they are.
    persistence: Until the window closes. **Reset** leaves it as it is.
  - name: Reset
    kind: button
    default: ""
    effect: Returns the four values to their defaults. It leaves **Channel** as it is.
    enabled: Disabled while Apply runs.
  - name: Apply
    kind: button
    default: ""
    effect: Sharpens the whole image, or every frame of the sequence, with the four values and **Channel** as they are when you press it, and then returns the four values to their defaults.
    enabled: Whatever the values, also when they change nothing. It stays enabled while a run works, and a second press is ignored. In a build without G'MIC it is disabled, with the tooltip **Preview** shows.
    tradeoff: A single image shows a progress window too, with a busy bar and **Cancel**. G'MIC notices **Cancel** at its next check inside the command, so the run can go on for a moment, and a result that arrives after it is dropped.
    persistence: "The history step is **Multiscale Sharpen Alpha** and reads, for example, `Amplitude: 1.00, Number of Scales: 5, Anisotropy: 0.00, Amount: 0.70 in channel: RGB`. For a grayscale image it ends after the amount. The header cards, the other metadata and the recorded processing state of the image are kept."
```

## How it works

1. The samples that **Channel** selects are checked: an image that holds a sample that is not a finite number is
   refused. Otherwise they are handed to G'MIC, in one session for the run, as single-precision
   [fractions of full scale](../../reference/glossary.md#fraction-of-full-scale), with the command
   `sharpen_alpha` and the first three values. The command's fourth value, which keeps each layer's opacity as low
   as it can be, stays at its default of 1.
2. The command sets aside an opacity channel, if the image has one, and splits the rest into a base and detail
   layers. Each layer is a color and an opacity which, laid over the next smoother image, gives back the one before.
3. It multiplies the opacity of each detail layer by a factor that rises from the broadest layer to the finest, holds
   it at full opacity, and lays the layers back over the base, from the broadest to the finest.
4. The result is held to 0 to 1 and written back in the image's own pixel type. **Amount** mixes it with the
   original, and a mask on the view then blends the two.
5. The image replaces the target and its history gets one step. Its header and its recorded
   [processing state](../../reference/glossary.md#processing-state) are kept.

### Model

With \(N\) the value of **Number of Scales**, the command makes \(N - 1\) blurred images, each from the one before,
by a blur of

\[ \sigma_k = 0.25 + 2^k \ \text{pixels}, \qquad k = 0, 1, \ldots, N - 2 \tag{1} \]

That is 1.25, 2.25, 4.25 and 8.25 pixels for the first four. Because each blur works on the one before, the images
are blurred from the original by 1.25, 2.57, 4.97 and 9.63 pixels. The last of them is the base, and each detail
layer holds what one image has and the next does not.

With \(A\) the value of **Amplitude**, the detail layers numbered \(j = 0\) for the broadest to \(j = N - 2\) for the
finest have their opacity multiplied by

\[ f_j = 1 + A\,\frac{j}{N - 2} \tag{2} \]

and the one layer of \(N = 2\) by \(1 + A\). Where a detail is small against the level it sits on, it comes back
multiplied by \(f_j\). A darkening detail can at most reach black, because the opacity is held at full.

With \(I\) the original sample, \(S\) the command's result held to 0 to 1, and \(m\) the value of **Amount**, the
result before the mask is

\[ M = (1 - m)\,I + m\,S \tag{3} \]

### Boundaries

- At an **Amplitude** of 0 or a **Number of Scales** of 1 the command gives the image back, apart from rounding and
  the hold to 0 to 1. At an **Amount** of 0 the result is the original. The command runs in full in every case.
- The command's result is held to 0 to 1, also in a floating-point image. At an **Amount** of 1 samples above 1 and
  below 0 are lost. A lower **Amount** keeps the share 1 - **Amount** of the original sample.
- An image that holds a sample that is not a finite number is refused with "the image holds a sample that is not a
  finite number, which G'MIC cannot process", and left unchanged.
- Beyond the edge of the image the blurs repeat the edge sample, without mirroring the image.
- An integer image is handed over as fractions and rounded back once to the nearest code. A 64-bit floating-point
  image is narrowed to single precision on the way.

### Worked example

The image is the linear [master](../../reference/glossary.md#master) after Histogram Transformation with **Shadows**
and **Midtones** at 0.02, which leaves the background near 0.28. With **Channel** at **RGB**, **Amplitude** at 1.00,
**Number of Scales** at 5, **Anisotropy** at 0.00 and **Amount** at 0.70, equation (1) blurs by 1.25, 2.25, 4.25 and
8.25 pixels. Equation (2) multiplies the four detail layers, from the broadest, by 1, 1.33, 1.67 and 2: the finest
detail doubles and broad structure keeps its contrast. By equation (3), a sample of 0.40 that the command raises to
0.50 becomes 0.47. The smooth background stays near 0.28, its grain raised with the finest layer.

```figure
images:
  - image: ../../assets/images/processes/sharpening/sharpen-alpha-before.webp
    alt: A crop of the stretched nebula at 100 percent before sharpening, bright clouds beside dark dust lanes and many small stars on a gray background.
    label: Before
  - image: ../../assets/images/processes/sharpening/sharpen-alpha-after.webp
    alt: The same crop after sharpening, the fine filaments and small stars crisper, the edges of the dust lanes firmer and the background grainier.
    label: After
caption: >-
  The middle of the stretched Carina Nebula master at 100 % before and after Apply with Amplitude at 1.00, Number of
  Scales at 5, Anisotropy at 0.00 and Amount at 0.70. Both are shown in the Linear display mode, without a display
  stretch.
```

## Starting points

| Situation | Start | Then adjust |
| --- | --- | --- |
| A stretched image after noise reduction | **Number of Scales** at 4, **Amplitude** at 1, **Anisotropy** at 0 | Try 5 scales on purpose, then set the strength with **Amount**. |
| Rims around stars or crunchy contrast | **Amplitude** near 1 and **Amount** below 1 | Lower **Number of Scales** first when the rims are broad. |
| Colors change at edges | **Channel** at **CIE L* (Lightness)** or **CIE Y (Luminance)** | Compare the channels with **View Channel** in the preview's toolbar, then set the strength again. |
| Choosing **Anisotropy** | 0, then 0.5, at a modest **Number of Scales** | Keep the one you prefer, then set **Amplitude** again. |

## When it goes wrong

!!! danger "Samples outside 0 to 1 are cut"
    The command's result is held to 0 to 1, also in a floating-point image. Highlights above 1 and samples below 0 are
    lost at an **Amount** of 1. Bring the image between 0 and 1 before you sharpen it.

Broad halos surround the stars
:   **Cause.** Many scales and a high **Amplitude** raise broad detail layers too.
:   **Fix.** Lower **Number of Scales** first, then **Amplitude** or **Amount**, or protect the stars with a mask.

The background turns grainy or crunchy
:   **Cause.** The finest layers hold the noise with the detail, and they are raised the most.
:   **Fix.** Reduce the noise first, lower **Amplitude** or **Amount**, or protect the background with a mask.

Colors shift, or the rims are colored
:   **Cause.** With **RGB** the channels share the opacity of each layer, so one channel's edge shapes the others.
:   **Fix.** Choose **CIE L* (Lightness)** or **CIE Y (Luminance)** in **Channel**, check the channels with **View
    Channel**, and lower the strength.

Nothing changes, yet every change is slow
:   **Cause.** **Amplitude** or **Amount** is 0, or **Number of Scales** is 1, and the command still runs in full.
:   **Fix.** Set a value that sharpens, or turn **Preview** off while you need no sharpening.

The preview looks darker than the image in its view
:   **Cause.** The pane shows the samples as they are, and the view may show the image in
    [Autostretch](../../reference/glossary.md#autostretch).
:   **Fix.** Compare with the view in its Linear display mode, or take a snapshot of the preview.

"Error executing the process", with "the image holds a sample that is not a finite number, which G'MIC cannot process"
:   **Cause.** The image holds a sample that is not a number or is infinite, and nothing was written.
:   **Fix.** Replace those samples, or crop them away, and apply again.

**Preview** and **Apply** are disabled, and their tooltip says the build carries no G'MIC library
:   **Cause.** This build of AfterNight was made without G'MIC.
:   **Fix.** Use a build that includes G'MIC, or another sharpening process.

Another error names G'MIC or the memory budget
:   **Cause.** The command failed, with G'MIC's own message, or the memory limit cannot hold the layers.
:   **Fix.** Check that the samples are finite and between 0 and 1, close other images or use a smaller one, and
    apply again.

## Related

- [Wavelet Denoise](../noise-reduction/wavelet-denoise.md) and the other noise reduction processes, which belong
  before sharpening.
- [Unsharp Mask](unsharp-mask.md), to sharpen one scale whose radius you set.
- [High Pass Filter](high-pass-filter.md), to make the detail itself into an image, around middle gray.
- [Masks](../../interface/masks.md), to sharpen one part of the image and protect the rest.
- [Image History](../../interface/image-history.md) and [Saving and Exporting](../../interface/saving-and-exporting.md),
  to undo the step and to save the result once it is checked at its final size.
- [Process Windows](../../interface/process-windows.md), for the preview, the preview area and what every process
  window shares.

## Credits

- [G'MIC](https://gmic.eu) 3.5.4 and its CImg library, by David Tschumperlé and the G'MIC contributors at GREYC,
  under the CeCILL 2.1 or the CeCILL-C licence, at your choice. AfterNight runs its `sharpen_alpha` command, written
  by David Tschumperlé, unchanged.
- [G'MIC `sharpen_alpha`](https://gmic.eu/reference/sharpen_alpha.html), for the command's purpose, its arguments and
  its defaults.
- [G'MIC `split_alpha`](https://gmic.eu/reference/split_alpha.html), for the split into alpha detail layers and the
  rebuild from them.
- [G'MIC `sub_alpha`](https://gmic.eu/reference/sub_alpha.html), for the least-opacity difference each layer holds.
- [G'MIC `blur`](https://gmic.eu/reference/blur.html), for the Gaussian blur and the edge rule the command uses.
