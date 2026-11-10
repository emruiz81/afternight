---
kind: process
stage: finish
menu: Process > Details > Local Contrast Enhancement
summary: >-
  Raises contrast at a broad scale by enlarging each sample's difference from a wide Gaussian blur, with separate
  gains for the bright and the dark side, computed by G'MIC.
data_in: nonlinear
data_out: nonlinear
destructive: true
preview: real-time
sequences: true
masks: post-blend
external: The G'MIC library bundled with AfterNight
glance:
  enabled: >-
    An image or a sequence view is active, no Local Contrast Enhancement window is open on it yet, and no window that
    needs the image for itself is open on it.
  inputs: >-
    One grayscale or RGB image or sequence whose samples are all finite numbers, and an optional mask on its view.
  outputs: >-
    The same view, replaced, with one history step. Its size, channels and pixel type are kept, its samples are held
    between 0 and 1, and its recorded processing state is left as it was. No file is written.
  state: >-
    A stretched image, after noise reduction, whose broad structures - nebula and dust lanes - need more contrast.
derived: true
rewritten: true
---
# Local Contrast Enhancement

Local Contrast Enhancement raises contrast at a broad scale. It blurs each channel with a wide Gaussian, enlarges the
difference between every sample and that blur, and adds it back, with one gain for what is brighter than its
surroundings and another for what is darker. It is unsharp masking with a large radius: it lifts nebula against dust
lanes, and it is neither [deconvolution](../../reference/glossary.md#deconvolution) nor histogram equalization.

**Apply** replaces the image or [sequence](../../reference/glossary.md#sequence) with the enhanced one, keeps its size
and [pixel type](../../reference/glossary.md#pixel-type), and adds one step to its
[history](../../reference/glossary.md#history). The bundled G'MIC library computes the samples.

```figure
image: ../../assets/images/processes/sharpening/local-contrast-enhancement.webp
alt: >-
  The Local Contrast Enhancement window. On the left, a histogram with three colored peaks and the lines Clipping Low
  and Clipping High in its corner, a toolbar, then the Spatial Radius, Weight, Darkness Level, Lightness Level and
  Amount boxes with their sliders, Target Image, Channel at RGB and Preview. On the right, the preview toolbar with
  Preview area unticked, over the whole stretched nebula with its dark lanes. Reset, Close and Apply run along the
  bottom.
caption: >-
  Local Contrast Enhancement on a stretched master with its default values and the preview area off: the preview shows
  what Apply writes over the whole image.
pins:
  - {n: 2, x: 2.0, y: 84.6}
  - {n: 3, x: 73.4, y: 17.6}
  - {n: 4, x: 2.0, y: 50.7}
  - {n: 5, x: 2.0, y: 55.2}
  - {n: 6, x: 2.0, y: 59.7}
  - {n: 7, x: 47.5, y: 50.0}
  - {n: 8, x: 2.0, y: 68.6}
  - {n: 9, x: 91.0, y: 96.5}
```

## Before you start

- [Stretch](../../reference/glossary.md#stretch) the image and reduce its noise first. The process enlarges noise and
  leftover defects along with the structure.
- **Apply** replaces the image. Its history keeps the step before: duplicate the image when you want to compare.
- Every sample must be a finite number, and what G'MIC returns is held between 0 and 1. A floating-point sample
  outside that range is cut to it, and a 64-bit image is computed in 32-bit precision.
- The process needs G'MIC, which AfterNight includes with the filter's definitions: nothing is installed, read from a
  script file or downloaded. A build without G'MIC keeps **Preview** and **Apply** disabled, and their tooltip reads
  "This build of AfterNight carries no G'MIC library, so this process cannot run."
- A raw color-filter mosaic is enhanced as one grayscale plane, which strengthens its pattern:
  [debayer](../../reference/glossary.md#debayer) it first. The frames of a sequence follow its view's automatic
  debayer setting.
- A [mask](../../reference/glossary.md#mask) on the view blends the result with the original after **Amount**.

## Steps

1. **Open the window.** Make the stretched image the active one and choose **Process > Details > Local Contrast
   Enhancement**. The preview shows a framed area in the middle of the image already enhanced, and **Apply** is
   enabled at once.
2. **Choose what to enhance.** Leave **Channel** at **RGB** to enhance every channel. Choose **CIE L* (Lightness)**
   to enhance the lightness alone when colored rims appear.
3. **Choose what the preview covers.** Keep **Preview area** ticked while you try values, and untick it to see the
   whole image: a wide blur near the area's edges sees nothing of the image around it.
4. **Set the scale.** Set **Spatial Radius** near the size, in pixels, of the structures you want to bring out.
5. **Set the strength.** Raise **Weight** until the structures separate, and lower it when halos or grain appear.
6. **Balance the two sides.** Lower **Lightness Level** or **Darkness Level** only when one side goes too far: bright
   rims and clipped stars, or dark rings and black lanes.
7. **Check the preview at 100 %.** Zoom into the background, a bright star and an edge, and press ++space++ over the
   preview to compare with the original.
8. **Soften the result.** Lower **Amount** last, to fade the whole effect.
9. **Apply.** Choose **Apply**. The image in its view is the enhanced one, its history lists **Local Contrast
   Enhancement** with the five values, and the controls return to their defaults.

## Controls

The histogram above the controls, its toolbar and the preview pane are the same in every window: see
[Process Windows](../../interface/process-windows.md). A histogram cannot show a halo: judge halos on the preview.

```controls
window: Local Contrast Enhancement
controls:
  - name: Spatial Radius
    kind: spinbox
    default: "80.00"
    widget: {min: 30, max: 200, step: 1, decimals: 2}
    effect: Sets the standard deviation, in pixels, of the Gaussian blur every sample is compared with. Higher compares each sample with a wider surrounding and enhances broader structures. Lower brings the effect closer to the detail.
    range: The slider moves it in tenths of a pixel.
    tradeoff: A wide radius spreads the bright and dark halos further around stars and bright edges, and makes the blur longer to compute.
    scope: The preview and Apply, on what **Channel** selects, before **Amount** and the mask.
    persistence: Until the window closes. **Reset** and a finished Apply return it to its default.
  - name: Weight
    kind: spinbox
    default: "0.50"
    widget: {min: 0, max: 5, step: 1, decimals: 2}
    effect: Multiplies the difference between every sample and its blur. Higher enlarges it, so structure and noise grow together. At 0 the samples are only held between 0 and 1.
    range: The slider moves it in hundredths. The box's arrows step by a whole 1, so type a value or use the slider.
    tradeoff: High values bring halos and clip the brightest and darkest samples. The background's grain grows by the same factor as the structure. The filter runs in full at 0 too.
    scope: The preview and Apply, on what **Channel** selects, before the two levels, **Amount** and the mask.
    persistence: Until the window closes. **Reset** and a finished Apply return it to its default.
  - name: Darkness Level
    kind: spinbox
    default: "1.00"
    widget: {min: 0, max: 4, step: 0.05, decimals: 2}
    effect: Multiplies the dark side, where a sample is darker than its blur. Higher deepens dust lanes and the dark rings around bright stars. At 0 nothing is darkened.
    range: The slider moves it in hundredths.
    enabled: It changes nothing while **Weight** is 0.
    tradeoff: High values clip shadows to black. It does not change the time a run takes.
    scope: The preview and Apply, on what **Channel** selects, before **Amount** and the mask.
    persistence: Until the window closes. **Reset** and a finished Apply return it to its default.
  - name: Lightness Level
    kind: spinbox
    default: "1.00"
    widget: {min: 0, max: 4, step: 0.05, decimals: 2}
    effect: Multiplies the bright side, where a sample is brighter than its blur. Higher brightens nebula and star cores. At 0 nothing is brightened.
    range: The slider moves it in hundredths.
    enabled: It changes nothing while **Weight** is 0.
    tradeoff: High values clip highlights to white and make bright rims around edges. It does not change the time a run takes.
    scope: The preview and Apply, on what **Channel** selects, before **Amount** and the mask.
    persistence: Until the window closes. **Reset** and a finished Apply return it to its default.
  - name: Amount
    kind: spinbox
    default: "1.00"
    widget: {min: 0, max: 1, step: 0.05, decimals: 2}
    effect: Mixes the result with the original. At 1 the image takes the whole result, at 0 it stays the original, and at 0.5 it takes half of each.
    range: The slider moves it in thousandths.
    tradeoff: It fades everything the filter did, halos and grain included, and saves no time, because the filter runs in full at any value. It cannot bring back what the filter clipped.
    scope: The preview and Apply, on the whole image once **Channel** has been converted back, before the mask.
    persistence: Until the window closes. **Reset** and a finished Apply return it to its default.
  - name: Target Image
    kind: combo
    default: The active image
    options: [The active image, Every other open image or sequence the window can work on]
    effect: Chooses the image or sequence Apply replaces, with the mask on its view. The histogram, the choices of **Channel** and the preview follow it.
    persistence: Until the window closes. **Reset** leaves it as it is.
  - name: Channel
    kind: combo
    default: RGB
    options: [RGB, Red, Green, Blue, CIE L* (Lightness), CIE a*, CIE b*, CIE c*, CIE h*, CIE X, CIE Y (Luminance), CIE Z, HSL Hue, HSL Saturation, HSL Lightness, HSV Hue, HSV Saturation, HSV Value, YCbCr Luma, YCbCr Blue Chroma, YCbCr Red Chroma]
    effect: Chooses what is enhanced. **RGB** gives each of the three channels its own blur and difference. **Red**, **Green** or **Blue** enhances that channel and keeps the other two. Every other choice enhances one component, which is converted back to RGB afterwards.
    enabled: For an RGB image. A grayscale image shows a disabled **Grayscale**.
    tradeoff: With **RGB** the three channels are enhanced apart, which can leave colored rims around stars and edges. **CIE L* (Lightness)** keeps a* and b* as they were. A component costs two conversions of the image.
    scope: The preview and Apply, before **Amount** and the mask.
    persistence: Until the window closes. **Reset** leaves it as it is. A finished Apply returns it to **RGB**.
  - name: Preview
    kind: check
    default: "On"
    effect: Shows the result beside the controls, with **Amount** and the mask applied. When the window opens it shows a framed preview area of 600 by 600 pixels in the middle of the image.
    tradeoff: Every change runs the whole G'MIC command again. Each run opens a G'MIC session, reads the filter's definitions and computes the blur. The preview area keeps that short.
    scope: The window only. A preview area is enhanced as an image of its own, so near its edges the blur sees nothing of the image around it.
    persistence: Until the window closes. **Reset** leaves it as it is.
    note: In a build without G'MIC it is off and disabled, with the tooltip "This build of AfterNight carries no G'MIC library, so this process cannot run."
  - name: Reset
    kind: button
    default: ""
    effect: Returns **Spatial Radius**, **Weight**, **Darkness Level**, **Lightness Level** and **Amount** to their defaults.
    range: It leaves **Channel**, the target, **Preview** and the preview area as they are.
  - name: Apply
    kind: button
    default: ""
    effect: Enhances the image, or every frame of the sequence, with the values as they are when you press it, then returns them to their defaults.
    enabled: From the moment the window opens, at any values. In a build without G'MIC it is disabled, with the tooltip **Preview** shows.
    tradeoff: The image goes to G'MIC as a 32-bit floating-point working copy, beside the second copy every run needs. A single image shows a progress window with a busy bar, because the command reports no share done, and its **Cancel** stops the run.
    persistence: >-
      The history step is **Local Contrast Enhancement** and reads, for example, `Spatial Radius: 80.00, Weight: 0.50,
      Darkness Level: 1.00, Lightness Level: 1.00, Amount: 1.00 in channel: RGB`. For a grayscale image it ends after
      the amount. The mask and the G'MIC version are not recorded.
```

## How it works

1. The channels **Channel** selects go to the bundled G'MIC library as 32-bit floating-point
   [fractions of full scale](../../reference/glossary.md#fraction-of-full-scale), in one G'MIC session for the run. An
   image with a sample that is not a finite number is refused before G'MIC sees it.
2. G'MIC blurs each channel with a Gaussian whose standard deviation is **Spatial Radius**, and multiplies the
   difference between every sample and its blur by **Weight**.
3. The part of that difference above zero is multiplied by **Lightness Level**, the part below zero by **Darkness
   Level**, and both are added to the sample. The result is held between 0 and 1 and written back in the image's own
   pixel type, rounded to the nearest value.
4. **Amount** mixes the result with the original, and a mask on the view then blends the two once more.
5. The image replaces the target and its history gets one step. Its header, its
   [plate solution](../../reference/glossary.md#plate-solution) and its acquisition data are kept. Its
   [processing state](../../reference/glossary.md#processing-state) is left as it was: the process does not record the
   result as nonlinear.

### Model

For a sample \(x\) between 0 and 1, with \(B\) its Gaussian blur at **Spatial Radius** and \(w\) the value of
**Weight**, the difference is

\[ D = w\,(x - B) \tag{1} \]

The filter's own threshold is fixed at 0, so every difference counts. With \(l\) the value of **Lightness Level** and
\(d\) that of **Darkness Level**, the filtered sample is

\[ y = \min\Bigl(1,\ \max\bigl(0,\ x + l\,\max(D, 0) + d\,\min(D, 0)\bigr)\Bigr) \tag{2} \]

With \(a\) the value of **Amount** and \(m\) the mask's weight at the pixel, 1 where there is no mask, the sample
written is

\[ z = x + a\,m\,(y - x) \tag{3} \]

Equation (2) clips before equation (3) mixes. A lower **Amount** or a mask fades a clipped sample, but cannot bring
back what it lost. **Weight** at 0, or both levels at 0, gives \(y = x\), and **Amount** at 0 gives \(z = x\).

### Boundaries

- A sample that is not a finite number stops the preview and the run with "the image holds a sample that is not a
  finite number, which G'MIC cannot process". Nothing is written.
- Every pixel type is held between 0 and 1, also with **Weight** at 0. Integer results are rounded to the nearest
  value, a tie to the even one.
- Beyond the frame the blur repeats the image's edge pixels, so a sample near the edge is compared with a blur of what
  lies inside. A [preview area](../../reference/glossary.md#preview-area) is treated as a frame of its own.
- **Cancel**, or closing the window, asks G'MIC to stop. It stops at its next check, which a wide blur can delay. A
  stopped run writes nothing.
- When G'MIC itself fails, on memory for example, the run reports the library's reason and writes nothing. Try again
  on a smaller image or with fewer images open.

### Worked example

The image in the picture above is the linear [master](../../reference/glossary.md#master) after Histogram
Transformation with **Shadows** and **Midtones** at 0.02, which leaves the background near 0.28. The window keeps its
defaults: **Spatial Radius** at 80, **Weight** at 0.50, both levels and **Amount** at 1.00, **Channel** at **RGB**.
**Preview area** is unticked.

A nebula sample of 0.60 whose blur is 0.40 gives \(D = 0.10\) by equation (1) and becomes 0.70 by equation (2). A
dust-lane sample of 0.30 under the same blur gives \(D = -0.05\) and becomes 0.25. The step between them grows from
0.30 to 0.45. A background sample 0.07 above a blur of 0.28 ends 0.105 above it: the grain grows by half, as the
structure does. With **Lightness Level** at 0.50 instead, the nebula sample would stop at 0.65 and the dust lane would
still reach 0.25. With **Amount** at 0.25, a sample of 0.40 that the filter takes to 0.60 is written as 0.45 by
equation (3), and as 0.425 where a mask is half gray.

```figure
images:
  - image: ../../assets/images/processes/sharpening/local-contrast-enhancement-before.webp
    alt: >-
      A crop at 100 % of the middle of the stretched master, pink clouds of nebula beside a dark dust lane,
      and many stars on a grainy background.
    label: Before
  - image: ../../assets/images/processes/sharpening/local-contrast-enhancement-after.webp
    alt: >-
      The same crop after Apply, the bright nebula brighter and the dust lanes darker, the stars with brighter cores and
      darker surroundings, and the grain of the background stronger.
    label: After
caption: >-
  The middle of the stretched Carina Nebula master at 100 %, before and after Apply with the window's defaults. Both
  are shown in the Linear display mode, without a display stretch.
```

## Starting points

| Situation | Start | Then adjust |
| --- | --- | --- |
| A stretched nebula, first try | The defaults, with **Preview area** on | **Spatial Radius** to the size of the structures, then **Weight**. Keep **Amount** at 1 until both are right. |
| Colored rims around stars or edges | **CIE L* (Lightness)** in **Channel** | The same **Weight**. Compare with **RGB** before you Apply. |
| Dark rings around bright stars | **Darkness Level** below 1 | Or protect the stars with a mask and keep the level. |
| A smooth, grainy background | The values first, without the mask | Then a mask that protects the background, with feathered edges. A hard edge shows as a step in contrast. |

## When it goes wrong

!!! warning "Amount and the mask cannot undo clipping"
    The filter clips before **Amount** and the mask mix the result with the original. When the histogram shows
    clipping, lower **Weight** or the level of the side that clips, not **Amount**.

!!! warning "The image keeps the state it had"
    The result is not recorded as nonlinear. Applied to a [linear image](../../reference/glossary.md#linear-image),
    it is still recorded as linear although its samples are not. Use the process after the stretch.

Stars have bright or dark halos
:   **Cause.** The difference from a wide blur is large around every bright star, on both sides.
:   **Fix.** Lower **Weight** first, then **Spatial Radius**. Lower only the level of the side that shows.

Colored rims appear around stars and edges
:   **Cause.** With **RGB** every channel is enhanced apart.
:   **Fix.** Choose **CIE L* (Lightness)** in **Channel**, or lower **Weight** or **Amount**.

The background turns coarse
:   **Cause.** The noise is enlarged by the same factor as the structure.
:   **Fix.** Reduce the noise first, lower **Weight**, or protect the background with a feathered mask.

The preview differs from the result of Apply
:   **Cause.** A preview area is enhanced alone, so near its edges the blur misses the image around it.
:   **Fix.** Untick **Preview area** for the last check before **Apply**.

The result looks unchanged
:   **Cause.** **Weight**, **Amount** or both levels are at 0, a mask protects the region, or **Spatial Radius** is
    far from the size of the structures.
:   **Fix.** Check those values and the mask, then set **Spatial Radius** to the structures you see.

The edges of the frame look different from its middle
:   **Cause.** Beyond the frame the blur repeats the edge pixels.
:   **Fix.** Judge the result away from the edges, or crop the frame before the enhancement.

"the image holds a sample that is not a finite number, which G'MIC cannot process"
:   **Cause.** The image holds a sample that is not a number or is infinite, and the image was left unchanged.
:   **Fix.** Repair or crop away those samples, then apply again.

Preview and Apply are disabled, and the tooltip names G'MIC
:   **Cause.** This build of AfterNight was made without G'MIC.
:   **Fix.** Use a build that includes G'MIC, or raise contrast with another process of this group.

## Related

- [Unsharp Mask](unsharp-mask.md), the same difference from a blur at the small radius of detail.
- [Local Histogram Equalization](local-histogram-equalization.md) and [CLAHE](clahe.md), which raise local contrast
  by equalizing histograms.
- [Wavelet Denoise](../noise-reduction/wavelet-denoise.md), to reduce the noise before.
- [Masks](../../interface/masks.md), to protect the background or the stars.
- [Image History](../../interface/image-history.md), to undo the step.
- [Process Windows](../../interface/process-windows.md), for the preview, the histogram and what every process window
  shares.

## Credits

- [G'MIC](https://gmic.eu) 3.5.4, by David Tschumperlé and the G'MIC team, under CeCILL-2.1 or CeCILL-C at the
  user's choice, computes the blur and the result. AfterNight bundles it.
- The process carries the `fx_LCE` filter of the
  [G'MIC community filters](https://github.com/GreycLab/gmic-community/blob/master/include/arto_huotari.gmic), by Arto
  Huotari and PhotoComiX, with unsharp-mask code by David Tschumperlé, distributed under the CeCILL licence of G'MIC.
  It is derived from that filter, with its threshold fixed at 0.
- The G'MIC [`blur` reference](https://gmic.eu/reference/blur.html) describes the standard deviation and the
  boundary conditions of the Gaussian this explanation uses.
- The G'MIC [`apply_channels` reference](https://gmic.eu/reference/apply_channels.html) describes how the filter is
  applied to the channels it is given.
