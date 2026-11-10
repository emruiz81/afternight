---
kind: process
stage: any
menu:
  - Process > Details > Gaussian Blur
  - Process > Filters > Gaussian Blur
summary: >-
  Blurs the image, or one channel or color component of it, with a Gaussian of the standard deviation you choose,
  softening every structure finer than that scale.
data_in: either
data_out: either
destructive: true
preview: real-time
sequences: true
masks: post-blend
external: none
glance:
  enabled: >-
    An image or a sequence view is active, no Gaussian Blur window is open on it yet, and no window that needs the
    image for itself is open on it.
  inputs: >-
    One grayscale or RGB image or sequence with finite samples, and an optional mask of the image's size on its view.
  outputs: >-
    The same view, replaced, with one history step. Its size, channels, pixel type, header and recorded processing
    state are kept. No file is written.
  state: >-
    Any image or mask whose fine structure should be softened, such as a mask, a support layer or one scale of the
    image. On a science master, linear data, with stars and edges protected by a mask when their profiles matter.
derived: false
rewritten: true
---
# Gaussian Blur

This process blurs the image with a Gaussian: each pixel becomes a weighted mean of its neighbours, the weights
falling off with distance as **Sigma** sets. Noise, stars, edges and fine texture soften alike, since nothing in the
blur tells an edge from noise. Use it on purpose: to soften a [mask](../../reference/glossary.md#mask), to make a
support layer, to separate one scale of the image, or to smooth a little. For noise reduction that keeps the detail,
a denoiser does better.

**Apply** replaces the image or [sequence](../../reference/glossary.md#sequence) with the blurred one, keeps its size
and [pixel type](../../reference/glossary.md#pixel-type), and adds one step to its
[history](../../reference/glossary.md#history).

```figure
image: ../../assets/images/processes/noise-reduction/gaussian-blur.webp
alt: >-
  The Gaussian Blur window. On the left, the Sigma box at 2.00 with its slider, and lower down Target Image, Channel at
  RGB and the Preview check box. On the right, a toolbar of zoom buttons, a channel box, a display mode box and a
  settings button over the preview of a red nebula, softened, with the word PREVIEW in its corner. Reset, Close and
  Apply run along the bottom.
caption: >-
  Gaussian Blur on the stretched master with Sigma at 2.00. The preview shows what Apply writes.
pins:
  - {n: 2, x: 2.0, y: 85.4}
  - {n: 3, x: 2.0, y: 15.8}
  - {n: 4, x: 46.9, y: 23.2}
  - {n: 5, x: 89.0, y: 96.0}
```

## Before you start

- Decide whether you want plain smoothing. When edges or impulses must be kept, compare Bilateral Filter, Median
  Filter or a denoiser first.
- **Apply** replaces the image. Duplicate it first, or use its history, which keeps the step before.
- Replace any sample that is not a finite number. The blur does not repair it, and spreads it into its neighbours.
- [Debayer](../../reference/glossary.md#debayer) a [CFA](../../reference/glossary.md#cfa) mosaic first, unless you
  mean to blur the mosaic itself. A raw mosaic is blurred as one grayscale plane, which mixes the colors of its filter.
  The frames of a sequence are read with the automatic debayer setting of its view.
- A mask on the view blends the result with the original afterwards, and the window has no **Amount**. When you blur
  a mask image, check that its own view carries no mask you did not mean.
- On a sequence, every frame gets the same **Sigma**, **Channel** and mask.

## Steps

1. **Open the window.** Make the image the [active image](../../reference/glossary.md#active-image) and choose
   **Process > Filters > Gaussian Blur**, or the same command under **Process > Details**. **Apply** stays disabled
   while **Sigma** is 0.
2. **Choose the channel.** Leave **Channel** at **RGB** to blur the three channels alike. Choose
   **CIE L* (Lightness)** to blur the brightness and keep the color components as they were.
3. **Set Sigma.** Raise **Sigma** to the scale you want to soften, near 1 for the finest structure. The preview
   follows while you drag, and shows the exact blur when you let go.
4. **Check at 100 %.** Zoom the preview to 100 % and hold ++space++ to compare the background, the star cores, the
   narrow structures and the edges of the frame with the original.
5. **Apply.** Choose **Apply**. The view shows the blurred image, and its history lists **Gaussian Blur** with the
   sigma.

## Controls

**Target Image**, the preview pane, **Close** and the progress window work as in every process window: see
[Process Windows](../../interface/process-windows.md). What is particular to this window is below.

```controls
window: Gaussian Blur
controls:
  - name: Sigma
    kind: spinbox
    default: "0.00"
    widget: {min: 0, max: 100, step: 0.5, decimals: 2}
    effect: Sets the standard deviation of the Gaussian, in pixels, the same along rows and columns. Higher blurs broader structure away. At 0 nothing changes.
    range: The arrows step by one power of ten below the value, and by 0.01 at 0. The slider moves the value in 1,000 equal steps of 0.1. A typed value is used as it is.
    enabled: Disabled while Apply runs. At 0, **Apply** is disabled, and every value above 0 blurs.
    tradeoff: The kernel reaches four times **Sigma** on each side, so the cost of Apply grows with it. Above 32, the blur runs as one wide kernel, or as several exact passes on an image too small for it, which costs more. At 40, one kernel of radius 160 runs on a frame at least 322 pixels wide and high, and two passes of 28.28 on a smaller one. At 100, one kernel of radius 400 needs a frame of 802 pixels each way.
    scope: The preview and Apply, on what **Channel** selects, before the mask is blended, for images and sequences.
    persistence: Until the window closes. Each run takes the value as it is when it starts. **Reset** and a finished Apply return it to 0.
    note: While you drag the slider, the preview blurs the image reduced to the pane's size by **Sigma** divided by the reduction, or the part shown at 100 % and closer, with the blur's reach around it. Over a reduction of 4, a **Sigma** of 2 blurs by 0.5, with 5 taps, so a **Sigma** below about half the reduction shows little change until you let go. Above 32 in the dragged preview, three box passes stand in for the Gaussian, within one per cent of it, at a cost that does not grow with **Sigma**. When you let go, the preview is the exact blur of the whole image, as Apply writes it.
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
    effect: Chooses what is blurred. **RGB** blurs each of the three channels on its own, with the same kernel. **Red**, **Green** or **Blue** blurs that channel and keeps the other two. Every other choice blurs one component of another color model and converts the image back.
    enabled: For an RGB image. A grayscale image shows a disabled **Grayscale**. Disabled while Apply runs.
    tradeoff: With **RGB**, color fringes around stars widen with the blur. A component costs two conversions and a working copy, and the conversion back can change colors.
    scope: The preview and Apply, before the mask is blended. Process Windows describes each component.
    persistence: Until the window closes. **Reset**, a finished Apply and a new target return it to **RGB**.
  - name: Preview
    kind: check
    default: "On"
    effect: Shows the blurred image beside the controls, with the mask blended in.
    range: The pane opens without a **Preview area**. At the window's default size, **Preview area** and **Take snapshot** are in the menu at the end of the pane's toolbar.
    tradeoff: Each settled preview blurs the whole image again, as Apply does. A **Preview area** shortens it, and does not limit Apply.
    scope: "The window only. On a sequence it shows the frame the view displays. Look at the blur at **Zoom 100%**: a fitted view resamples the image and hides how much stars and fine structure widened. **View Channel** shows the blur of each channel. **Display Mode** and **Autostretch Settings** change only how the preview is shown, and a strong display stretch can show leftover noise or hide broad gradients."
    persistence: Until the window closes. **Reset** leaves it as it is.
    note: A snapshot runs the exact blur that Apply runs and opens as `<target> - Gaussian Blur - SNAPSHOT`, without the display stretch.
  - name: Reset
    kind: button
    default: ""
    effect: Returns **Sigma** to 0 and, for an RGB image, **Channel** to **RGB**. The preview then shows the image unchanged, and **Apply** is disabled.
    enabled: Disabled while Apply runs.
  - name: Apply
    kind: button
    default: ""
    effect: Blurs the whole image, or every frame of the sequence, with **Sigma** and **Channel** as they are when you press it, and then returns them to their defaults.
    enabled: When **Sigma** is above 0, so it is disabled when the window opens. While a run works it is disabled, with **Sigma**, **Target Image**, **Channel**, **Reset** and **Close**.
    tradeoff: A single image shows no progress window, and its run cannot be cancelled once it starts. On a sequence, **Cancel** is noticed between two rows, and the progress window also reports each pass of a blur made of several. An integer image and a blur of several passes each take a working copy, and a color component, a mask and a sequence add work and memory.
    scope: The whole image or sequence, with the mask and its inversion as they are when you press it, blended afterwards.
    persistence: "The history step is **Gaussian Blur** and reads, for example, `Adjust sigma to 2 in channel: RGB`. For a grayscale image it ends after the value. The header cards, the other metadata and the recorded processing state are kept."
```

## How it works

1. Each channel that **Channel** selects is blurred on its own, one after another, with a kernel sampled from the
   Gaussian of **Sigma** out to four times **Sigma** on each side. The taps are divided by their sum, so a flat area
   stays flat.
2. The kernel runs along the rows and then down the columns, which gives the two-dimensional Gaussian. Beyond the
   edge of the image, the blur reads the image mirrored without its edge sample.
3. Above a **Sigma** of 32, the blur runs as one wide kernel when the image is more than twice its radius wide and
   high and the processor offers [AVX2](../../reference/glossary.md#avx2). Otherwise it runs as several exact passes whose variances add up to the square
   of **Sigma**.
4. The result is written back in the image's own pixel type. A component of another
   [color model](../../reference/glossary.md#color-model) is converted back to RGB, and a mask on the view then blends
   the result with the original.
5. The image replaces the target and its history gets one step. Its header and its recorded
   [processing state](../../reference/glossary.md#processing-state) are kept.

### Model

With \(\sigma\) the value of **Sigma** and \(x\) the distance to a neighbour in pixels, the Gaussian weighs that
neighbour by

\[ G(x) = \frac{1}{\sqrt{2\pi}\,\sigma}\,\exp\Bigl(-\frac{x^2}{2\sigma^2}\Bigr) \tag{1} \]

sampled at whole pixels out to \(r = \lceil 4\sigma \rceil\), which is \(2r + 1\) taps along each axis. Two blurs in a
row, \(\sigma_1\) and then \(\sigma_2\), make one blur of

\[ \sigma = \sqrt{\sigma_1^2 + \sigma_2^2} \tag{2} \]

apart from the mirrored edges and the rounding of an integer image between the two runs. A star's profile grows the
same way: a star of deviation \(s\) becomes one of deviation \(\sqrt{s^2 + \sigma^2}\), and its peak falls by the
square of their ratio. For noise that changes from one pixel to the next and a \(\sigma\) of 1 or more, the blur
divides the deviation of the noise by about

\[ 2\sqrt{\pi}\,\sigma \tag{3} \]

### Boundaries

- The mirror at the edges avoids a black border, but a wide blur draws mirrored structure into the border. A preview
  of the part shown reads the blur's reach around it, so its edges are those of the whole image.
- A sample that is not a finite number is not repaired. The blur spreads it into its neighbours, as far as four times
  **Sigma**.
- A 32-bit floating-point image is blurred in single precision, and a 64-bit one with every tap summed in double
  precision. An integer image is blurred on a single-precision copy of its
  [fractions of full scale](../../reference/glossary.md#fraction-of-full-scale), which holds each code of up to
  16 bits exactly, and rounded once to the nearest code on the way back. Above a **Sigma** of 32, every type is
  blurred on that copy.
- Each exact pass leaves out the tail of the Gaussian beyond four times its own sigma, a share of 0.00006.
- The blur has no edge test, no range weight and no adaptive scale, and does not tie the channels together.

### Worked example

The image is the linear [master](../../reference/glossary.md#master) after Histogram Transformation with **Shadows**
and **Midtones** at 0.02, which leaves the background near 0.28. With **Channel** at **RGB** and **Sigma** at 2.00,
the kernel reaches 8 pixels on each side, 17 taps along the rows and 17 down the columns. The background stays near
0.28, since a blur keeps the level of a flat area. By equation (3), its grain from one pixel to the next falls to
about \(1/7\) of what it was. By equation (2), a star of deviation 1.5 pixels, a
[FWHM](../../reference/glossary.md#fwhm) of 3.5, grows to 2.5 pixels, a FWHM of 5.9, and its peak falls to
\((1.5/2.5)^2 = 0.36\) of what it was. The edges of the dust lanes soften, and the faintest stars fade into the
background.

```figure
images:
  - image: ../../assets/images/processes/noise-reduction/gaussian-blur-before.webp
    alt: A crop of the stretched nebula at 100 percent before the blur, bright clouds beside dark dust lanes and many small sharp stars on a grainy gray background.
    label: Before
  - image: ../../assets/images/processes/noise-reduction/gaussian-blur-after.webp
    alt: The same crop after the blur, the grain smoothed away, the edges of the dust lanes soft, the stars wider and dimmer, and the faintest ones gone.
    label: After
caption: >-
  The middle of the stretched Carina Nebula master at 100 % before and after Apply with Sigma at 2.00. Both are shown
  in the Linear display mode, without a display stretch.
```

## Starting points

| Situation | Start | Then adjust |
| --- | --- | --- |
| Smoothing at the scale of a pixel | **Sigma** near 1 | Raise it in small steps, and compare at 100 % with ++space++. |
| A mask or a support layer | **Sigma** at the scale the next step needs | Check its edges at 100 %. For a very broad layer, a binned copy blurs faster. |
| A science master whose star profiles matter | Linear data, and a mask that protects the stars and edges | Check the mask's inversion, then the stars at 100 %. |

## When it goes wrong

!!! warning "The blur does not keep edges"
    Stars, halos, dust lanes, the edges of a mask and the noise widen together. A blurred layer added back strongly
    shows soft halos. Use a mask, or a denoiser that keeps edges.

**Apply** is disabled
:   **Cause.** **Sigma** is 0, which changes nothing.
:   **Fix.** Set a value above 0.

A small **Sigma** changes little while you drag
:   **Cause.** The dragged preview blurs the reduced image by **Sigma** divided by the reduction.
:   **Fix.** Let go of the slider, zoom the preview to 100 %, or take a snapshot.

The preview changes when you let go of the slider
:   **Cause.** The dragged preview blurs the reduced image or the part shown, and the settled one blurs the whole
    image.
:   **Fix.** Judge the settled preview. It is what Apply writes.

Edges and stars soften too much
:   **Cause.** The Gaussian treats an edge as it treats noise.
:   **Fix.** Lower **Sigma**, protect the structure with a mask, or use Bilateral Filter, Total Variation Denoise or
    Wavelet Denoise.

The border of the image differs from the middle
:   **Cause.** The image is mirrored at its edges, so the border averages mirrored structure.
:   **Fix.** Lower **Sigma**, or crop the border after the blur.

**Channel** is back at **RGB** after Apply or Reset
:   **Cause.** Both return it to **RGB** for an RGB image.
:   **Fix.** Choose the component again before the next run.

The applied image differs from the preview
:   **Cause.** A [preview area](../../reference/glossary.md#preview-area), the mask, the target, the channel or the
    display mode differed from what Apply used.
:   **Fix.** Untick **Preview area**, match the mask, target and channel, and compare under one display mode.

Apply is slow at a large **Sigma**
:   **Cause.** The kernel widens with **Sigma**, and above 32 a small image takes several passes.
:   **Fix.** Lower **Sigma**, or blur a binned copy for a very broad layer.

## Related

- [Bilateral Filter](bilateral-filter.md) and [Median Filter](median-filter.md), which keep strong edges and remove
  impulses.
- [Wavelet Denoise](wavelet-denoise.md), which separates the scales instead of smoothing them all at one.
- [Unsharp Mask](../sharpening/unsharp-mask.md) and [High Pass Filter](../sharpening/high-pass-filter.md), which
  subtract a Gaussian blur to bring detail out.
- [Masks](../../interface/masks.md), to blur one part of the image, or to blur a mask itself.
- [Image History](../../interface/image-history.md), to undo the step.
- [Process Windows](../../interface/process-windows.md), for the preview, the preview area and what every process
  window shares.

## Credits

- [Peter Kovesi, "Fast Almost-Gaussian Filtering", DICTA 2010](https://www.peterkovesi.com/papers/FastGaussianSmoothing.pdf),
  for the widths of the repeated boxes that stand in for the Gaussian in a dragged preview.
- [Basile Fraboni, FastGaussianBlur](https://github.com/bfraboni/FastGaussianBlur), under the MIT licence, whose three
  passes and mirror at the edges the box approximation keeps.
