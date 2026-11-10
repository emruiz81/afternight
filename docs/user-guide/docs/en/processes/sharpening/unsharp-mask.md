---
kind: process
stage: finish
menu: Process > Details > Unsharp Mask
summary: >-
  Sharpens the image by adding back, as many times as you set, its difference from a Gaussian blur of the radius you
  set.
data_in: nonlinear
data_out: nonlinear
destructive: true
preview: real-time
sequences: true
masks: post-blend
external: none
glance:
  enabled: >-
    An image or a sequence view is active, no Unsharp Mask window is open on it yet, and no window that needs the
    image for itself is open on it.
  inputs: >-
    One grayscale or RGB image or sequence with finite samples between 0 and 1, and an optional mask of the image's
    size on its view.
  outputs: >-
    The same view, replaced, with one history step. Its size, channels, pixel type, header and recorded processing
    state are kept. No file, preset or report is written.
  state: >-
    A stretched image after noise reduction, whose fine structure needs more contrast. A linear image works too when
    its noise is low and the settings are tried on it first.
derived: false
---
# Unsharp Mask

This process sharpens by contrast. It blurs a copy of the image with a Gaussian of **Radius**, takes the difference
between the image and the blur, and adds that difference back **Amount** times. Edges and small structures about as
wide as the radius gain contrast, while smooth areas stay as they are. It is no
[deconvolution](../../reference/glossary.md#deconvolution): it recovers nothing that the seeing, the optics or the
sampling lost.

**Apply** replaces the image or [sequence](../../reference/glossary.md#sequence) with the sharpened one, keeps its size
and [pixel type](../../reference/glossary.md#pixel-type), and adds one step to its
[history](../../reference/glossary.md#history).

```figure
image: ../../assets/images/processes/sharpening/unsharp-mask.webp
alt: >-
  The Unsharp Mask window. On the left, the Radius and Amount boxes with their sliders, and lower down Target Image,
  Channel at RGB and the Preview check box. On the right, a toolbar of zoom buttons, a channel box, a display mode box
  and a settings button over the preview of a red nebula, with the word PREVIEW in its corner and a framed area in its
  middle labelled Preview: Unsharp Mask. Reset, Close and Apply run along the bottom.
caption: >-
  Unsharp Mask on the stretched master with Radius at 2.00 and Amount at 1.00. The framed area in the preview shows
  what Apply writes there, over the original.
pins:
  - {n: 2, x: 2.0, y: 82.7}
  - {n: 3, x: 2.0, y: 20.1}
  - {n: 4, x: 2.0, y: 25.1}
  - {n: 5, x: 56.8, y: 54.6}
  - {n: 6, x: 89.1, y: 96.0}
```

## Before you start

- Finish calibration, stacking, gradient removal and noise reduction first. The filter raises every transition, noise,
  [ringing](../../reference/glossary.md#ringing) and leftover calibration defects included.
- **Apply** replaces the image. Duplicate it first, or use its history, which keeps the step before.
- [Debayer](../../reference/glossary.md#debayer) a [CFA](../../reference/glossary.md#cfa) mosaic first. A raw mosaic is
  sharpened as one grayscale plane, which sharpens the sensor pattern with it. The frames of a sequence are read with
  the automatic debayer setting of its view, which can turn mosaic frames into RGB.
- Bring the samples between 0 and 1, and replace any sample that is not a finite number.
- A [mask](../../reference/glossary.md#mask) on the view blends the result with the original afterwards, so you can
  sharpen the nebula and leave the background alone.
- On a sequence, every frame gets the same **Radius**, **Amount**, **Channel** and mask.

## Steps

1. **Open the window.** Make the stretched image the active one and choose **Process > Details > Unsharp Mask**. The
   preview shows the sharpened image inside a framed [preview area](../../reference/glossary.md#preview-area) in the
   middle of the image, over the original.
2. **Choose what to sharpen.** Leave **Channel** at **RGB** to sharpen the three channels, each on its own. Choose
   **CIE L* (Lightness)** or **CIE Y (Luminance)** to sharpen the brightness and leave the colors alone.
3. **Set the scale.** Set **Radius** to about the width of the structure you want to bring out. Small values sharpen
   single pixels and the noise, large ones give broad bright and dark rims.
4. **Set the strength.** Raise **Amount** until the edges in the area gain contrast. Lower it again when rims appear
   around the stars.
5. **Check at 100 %.** Zoom the preview to 100 % where small stars, a bright edge and a smooth background meet, and
   hold ++space++ to compare with the original. Move the area, or untick **Preview area** in the menu at the end of
   the preview's toolbar, to check the rest of the image.
6. **Apply.** Choose **Apply**. A progress window follows the run, the view shows the sharpened image, and its history
   lists **Unsharp Mask** with the radius and the amount.

## Controls

**Target Image**, **Preview**, the preview pane, **Close** and the progress window work as in every process window:
see [Process Windows](../../interface/process-windows.md). What is particular to this window is below.

```controls
window: Unsharp Mask
controls:
  - name: Radius
    kind: spinbox
    default: "1.00"
    widget: {min: 0, max: 10, step: 0.1, decimals: 2}
    effect: Sets the standard deviation of the Gaussian blur, in pixels, which is the scale of what is sharpened. Higher brings out broader structure, with broader bright and dark rims beside the edges. Lower sharpens finer detail and the noise from one pixel to the next. At 0 nothing changes.
    range: The arrows step by one power of ten below the value, 0.1 from 1 up and 0.01 below 1. The slider moves the value in 1,000 equal steps.
    enabled: Disabled while Apply runs.
    tradeoff: The blur reaches four times the radius on each side, so its cost grows with the radius. The useful radius depends on the sampling, the seeing, the noise reduction done before and the size the image is shown at.
    scope: The preview and Apply, on what **Channel** selects, before the mask is blended.
    persistence: Until the window closes. Each run takes the value as it is when it starts. **Reset** and a finished Apply return it to its default.
    note: While you drag a slider, the preview works on the image reduced to the pane's size, with the radius reduced by the same factor, or on the part shown at 100 % and closer, at full scale. The reduced result comes close to a reduction of what Apply writes but is not one, because the result is clipped after the blur. When you let go, the preview works on the whole image, as Apply does.
  - name: Amount
    kind: spinbox
    default: "1.00"
    widget: {min: -10, max: 10, step: 0.1, decimals: 2}
    effect: Sets how many times the difference from the blur is added back. Higher sharpens more. At 0 the image stays as it is, at -1 the result is the Gaussian blur itself, and below -1 the detail is turned the other way, more strongly the lower it goes.
    range: The arrows step as they do for **Radius**. The slider moves the value in 1,000 equal steps of 0.02.
    enabled: Disabled while Apply runs.
    tradeoff: A high value raises noise, compression defects and the rims around stars and bright edges as much as real detail. Between 0 and -1 the image is softened, and below -1 the rims are reversed. At 0 the filter still runs and costs as much, and every sample is still held to full scale.
    scope: The preview and Apply, on what **Channel** selects, before the mask is blended.
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
    effect: Chooses what is sharpened. **RGB** sharpens each of the three channels on its own. **Red**, **Green** or **Blue** sharpens that channel and keeps the other two. Every other choice sharpens one component of another color model and converts the image back.
    enabled: For an RGB image. A grayscale image shows a disabled **Grayscale**. Disabled while Apply runs.
    tradeoff: With **RGB** the channels gain different rims where their edges differ, which shows as colored fringes around stars. A lightness or luminance component sharpens the brightness alone, at the cost of two conversions and a working copy. Colors the conversion back puts out of range are cut in an integer image and kept in a floating-point one.
    scope: The preview and Apply, before the mask is blended. Process Windows describes each component.
    persistence: Until the window closes. **Reset**, a finished Apply and a new target return it to **RGB**.
  - name: Preview
    kind: check
    default: "On"
    effect: Shows the sharpened image beside the controls, with the mask blended in.
    range: >-
      The pane opens with a **Preview area** of 1,200 by 1,200 pixels in the middle of the image, labelled "Preview:
      Unsharp Mask", or the whole image when it is smaller. At the window's default size, **Preview area** is in the
      menu at the end of the pane's toolbar.
    tradeoff: The area shortens each preview but can hide rims wider than the part it shows. Check the whole image before Apply.
    scope: The window only. The pane offers **Display Mode** and **Autostretch Settings**, which change only how the preview is shown.
    persistence: Until the window closes. **Reset** leaves it as it is.
  - name: Reset
    kind: button
    default: ""
    effect: Returns **Radius** and **Amount** to 1 and, for an RGB image, **Channel** to **RGB**.
    enabled: Disabled while Apply runs.
  - name: Apply
    kind: button
    default: ""
    effect: Sharpens the whole image, or every frame of the sequence, with **Radius**, **Amount** and **Channel** as they are when you press it, and then returns them to their defaults.
    enabled: Whatever the values, also when they change nothing. It stays enabled while a run works, and a second press is ignored.
    tradeoff: A single image shows a progress window too, with **Cancel**, which the filter notices between two rows. Runs of other windows on other images go on at the same time.
    persistence: "The history step is **Unsharp Mask** and reads, for example, `Radius: 2, amount: 1 in channel: RGB`. For a grayscale image it ends after the amount. The header cards, the other metadata and the recorded processing state of the image are kept."
```

## How it works

1. Each channel that **Channel** selects is blurred, one after another, with a Gaussian whose standard deviation is
   **Radius**. Beyond the edge of the image the blur reads the image mirrored with the edge sample repeated, as
   SciPy's `reflect` mode does, and its kernel ends at four times the radius.
2. The blur is subtracted from the image, and the difference is multiplied by **Amount** and added back.
3. The result is held to full scale: 0 to 1 for a floating-point image, the range of its type for an integer one,
   which is rounded once to the nearest code. The work is done in single precision, with the blur summed in double
   precision.
4. A component of another [color model](../../reference/glossary.md#color-model) is converted back to RGB, and a mask
   on the view then blends the result with the original.
5. The image replaces the target and its history gets one step. Its header and its recorded
   [processing state](../../reference/glossary.md#processing-state) are kept.

### Model

With \(I\) a sample, \(G_r * I\) the Gaussian blur of radius \(r\) at that sample and \(a\) the value of **Amount**,
the result before it is held to full scale is

\[ U = I + a\,(I - G_r * I) = (1 + a)\,I - a\,G_r * I \tag{1} \]

The term in brackets is the detail: zero on a smooth area, positive on the bright side of an edge and negative on its
dark side. With \(a = 0\), equation (1) gives the image itself. With \(a = 1\) it gives \(2I - G_r * I\), and with
\(a = -1\) the blur \(G_r * I\). AfterNight's own engine computes the unsharp mask as scikit-image's `unsharp_mask`
defines it, with **Radius** as its sigma and **Amount** as its amount.

With a positive \(a\), noise that changes from one pixel to the next is multiplied by nearly \(1 + a\): at an
**Amount** of 1 it almost doubles, 1.86 times at a **Radius** of 1 and 1.96 times at 2. With **RGB**, equation (1) runs on red, green and blue
separately, with no shared brightness and no protection of the hue.

### Boundaries

- At a **Radius** of 0 nothing runs and the image is unchanged. At an **Amount** of 0 the filter runs and only holds
  the samples to full scale.
- Overshoot beyond full scale is lost before the mask blends: a star core pushed above 1 stays at 1, a dark rim below
  0 stays at 0.
- A sample that is not a finite number is not refused. The blur spreads it into its neighbours, as far as four times
  the radius.
- A 64-bit floating-point image is computed in single precision and written back in its own type. A negative
  **Amount** is computed on a single-precision copy of the image's
  [fractions of full scale](../../reference/glossary.md#fraction-of-full-scale).
- The filter has no threshold that spares low-contrast noise and no control that suppresses rims.

### Worked example

The image is the linear [master](../../reference/glossary.md#master) after Histogram Transformation with **Shadows**
and **Midtones** at 0.02, which leaves the background near 0.28. With **Channel** at **RGB**, **Radius** at 2.00 and **Amount** at 1.00, equation (1) adds
the detail once. The smooth background stays near 0.28, with its grain almost doubled. A sample of 0.50 at the edge
of a dust lane, whose blurred neighbourhood is 0.40, becomes 0.60. One of 0.30 on the dark side of that edge, under
the same blur, becomes 0.20. A star core of 0.95 whose blur is 0.60 would reach 1.30 and is held at 1.

```figure
images:
  - image: ../../assets/images/processes/sharpening/unsharp-mask-before.webp
    alt: A crop of the stretched nebula at 100 percent before sharpening, bright clouds beside dark dust lanes and many small stars on a gray background.
    label: Before
  - image: ../../assets/images/processes/sharpening/unsharp-mask-after.webp
    alt: The same crop after sharpening, the edges of the dust lanes and the small stars crisper, thin dark rims around the brighter stars and a grainier background.
    label: After
caption: >-
  The middle of the stretched Carina Nebula master at 100 % before and after Apply with Radius at 2.00 and Amount at
  1.00. Both are shown in the Linear display mode, without a display stretch.
```

## Starting points

| Situation | Start | Then adjust |
| --- | --- | --- |
| A stretched image after noise reduction | **Radius** from 1 to 2, **Amount** at 1 | Lower **Amount** when rims show around the stars, then **Radius**. |
| Colored fringes around stars | **Channel** at **CIE L* (Lightness)** or **CIE Y (Luminance)** | Compare the channels with **View Channel** in the preview's toolbar, then set **Radius** and **Amount** again. |
| The nebula needs it and the background does not | A mask that is white on the nebula | Set the values without the mask, then enable it, check its inversion and its edges at 100 %, and check the whole image. |
| A gentle blur on purpose | **Amount** at -1 | Choose **Radius**, then move **Amount** toward 0 if the result is too soft. |

## When it goes wrong

!!! danger "Samples outside 0 to 1 are cut"
    The result is held to full scale, also at an **Amount** of 0. A floating-point background below 0 becomes 0, and
    a star above 1 becomes 1, for good. Bring the image between 0 and 1 before you sharpen it.

Colored rims appear around stars
:   **Cause.** With **RGB** each channel is sharpened on its own, and their edges differ.
:   **Fix.** Choose **CIE L* (Lightness)** or **CIE Y (Luminance)** in **Channel**, and lower **Amount**.

Bright and dark halos surround the stars and edges
:   **Cause.** **Radius** sets the width of the rims and a positive **Amount** adds them.
:   **Fix.** Lower **Amount** first, then **Radius**, and check the whole image, not only the preview area.

The background turns grainy
:   **Cause.** The noise is raised with the detail, almost twofold at an **Amount** of 1.
:   **Fix.** Reduce the noise first, lower **Amount** or **Radius**, or protect the background with a mask.

Nothing seems to change
:   **Cause.** **Radius** or **Amount** is 0, a mask protects the area, or the pane shows the original.
:   **Fix.** Check the two values, the mask and its inversion, and the word PREVIEW in the corner of the pane.

The image is blurred instead of sharpened
:   **Cause.** **Amount** is negative. At -1 the result is the Gaussian blur itself.
:   **Fix.** Set a positive **Amount**.

The applied image looks different from the preview
:   **Cause.** The preview area showed only part of the image, or the pane and the view use different display modes.
:   **Fix.** Untick **Preview area** for the last check, and compare both under one display mode.

Apply reports "An error occurred when running the process:" and names the memory budget
:   **Cause.** The memory limit cannot hold the copies of the image the run needs, so nothing was written.
:   **Fix.** Close other images or work on a smaller one, or raise the memory limit on the Processing page of the
    settings, which takes effect at the next start.

## Related

- [Wavelet Denoise](../noise-reduction/wavelet-denoise.md) and the other noise reduction processes, which belong
  before sharpening.
- [Sharpen Alpha](sharpen-alpha.md), to sharpen several scales at once.
- [High Pass Filter](high-pass-filter.md), to make the detail itself into an image, around middle gray.
- [Masks](../../interface/masks.md), to sharpen one part of the image and protect the rest.
- [Image History](../../interface/image-history.md) and [Saving and Exporting](../../interface/saving-and-exporting.md),
  to undo the step and to save the result once it is checked at its final size.
- [Process Windows](../../interface/process-windows.md), for the preview, the preview area and what every process
  window shares.

## Credits

- [scikit-image: `unsharp_mask`](https://scikit-image.org/docs/stable/api/skimage.filters.html#skimage.filters.unsharp_mask),
  for the equation, the radius as the Gaussian's sigma, the amount and the warning about sharpening color channels
  separately that this page follows.
- [scikit-image: image data types and what they mean](https://scikit-image.org/docs/stable/user_guide/data_types.html),
  for the ranges of floating-point and integer samples.
