---
kind: process
stage: noise
menu: Process > Filters > Median Filter
summary: >-
  Replaces each sample with the median of the square window around it, mixed with the original by Modulation, to
  remove hot pixels and other specks of one or two pixels.
data_in: either
data_out: either
destructive: true
preview: real-time
sequences: true
masks: post-blend
external: none
glance:
  enabled: >-
    An image or a sequence view is active, no Median Filter window is open on it yet, and no window that needs the image
    for itself is open on it.
  inputs: >-
    One grayscale or RGB image or sequence of any pixel type, and an optional mask of the image's size on its view. In
    a floating-point image, samples that are not finite numbers are left as they are.
  outputs: >-
    The same view, replaced, with one history step. Its size, channels, pixel type, header and recorded processing
    state are kept. No file is written.
  state: >-
    An image, linear or stretched, with hot or cold pixels or other specks that calibration and Cosmetic Correction
    left, or a mask to clean of specks.
derived: true
rewritten: true
---
# Median Filter

Median Filter replaces each sample with the median of the square window around it: the middle value once the
window's samples are put in order. A [hot pixel](../../reference/glossary.md#hot-pixel), a cosmic ray or another speck
that fills less than half the window is outvoted by its neighbours and disappears, while a step between two broad
areas stays in place. Small stars and thin lines are outvoted in the same way. **Modulation** mixes each pass with what
it started from, and **Iterations** repeats the pass.

**Apply** replaces the image or [sequence](../../reference/glossary.md#sequence) with the filtered one, keeps its size
and [pixel type](../../reference/glossary.md#pixel-type), and adds one step to its
[history](../../reference/glossary.md#history).

```figure
image: ../../assets/images/processes/noise-reduction/median-filter.webp
alt: >-
  The Median Filter window. On the left, the Kernel Size box at 3x3, the Iterations and Modulation boxes with their
  sliders, and lower down Target Image, Channel at RGB and the Preview check box. On the right, a toolbar of zoom
  buttons, a channel box and a display mode box reading Autostretch over the preview of a red nebula, with the word
  PREVIEW in its corner and a small framed area in its middle. Reset, Close and Apply run along the bottom.
caption: >-
  Median Filter on the linear master with its defaults, Kernel Size at 3x3, Iterations at 1 and Modulation at 1.00.
  The framed area shows what Apply writes there, over the original, in the Autostretch display mode.
pins:
  - {n: 2, x: 34.8, y: 85.4}
  - {n: 3, x: 71.2, y: 54.8}
  - {n: 4, x: 19.5, y: 15.6}
  - {n: 5, x: 5.0, y: 30.0}
  - {n: 6, x: 34.8, y: 20.6}
  - {n: 8, x: 89.2, y: 96.0}
```

## Before you start

- Remove hot and [cold pixels](../../reference/glossary.md#cold-pixel) in calibration or with Cosmetic Correction where
  you can. Those replace the defects alone, while Median Filter changes every sample.
- The filter is made for specks, not for the fine grain of a stack: it leaves grain blotchy and erases faint stars with
  it. Reduce grain with a denoiser.
- **Apply** replaces the image. Duplicate it first, or use its history, which keeps the step before.
- In a floating-point image, a sample that is not a finite number, as in the empty border of a registered frame, is
  left out of every window and left where it is. It stays in the result.
- [Debayer](../../reference/glossary.md#debayer) a [CFA](../../reference/glossary.md#cfa) mosaic first, unless you mean
  to mix the colors of its filter in one window. The frames of a sequence are read with the automatic debayer setting
  of its view.
- A [mask](../../reference/glossary.md#mask) on the view blends the result with the original afterwards. On a sequence,
  every frame gets the same values, **Channel** and mask.

## Steps

1. **Open the window.** Make the image the [active image](../../reference/glossary.md#active-image) and choose
   **Process > Filters > Median Filter**. The preview shows the filtered image inside a framed
   [preview area](../../reference/glossary.md#preview-area) in the middle of the image.
2. **Choose the channel.** Leave **Channel** at **RGB** to filter the three channels, each on its own. Choose
   **CIE L* (Lightness)** to filter the brightness and leave the colors as they are.
3. **Place the area.** Move the frame over a few hot pixels, small stars and smooth background, and zoom the preview
   to 100 %.
4. **Start with the smallest window.** Keep **Kernel Size** at **3x3**, which removes specks up to two pixels across.
   Choose a larger window only for wider defects.
5. **Set the strength.** Lower **Modulation** when small stars and fine structure fade with the defects.
6. **Repeat only when needed.** Raise **Iterations** one pass at a time, and check each result. Every pass works on
   the one before it.
7. **Check at 100 %.** Hold ++space++ over the preview to compare with the original, and look at the faint stars.
   Then untick **Preview area** and look at the whole image and its edges.
8. **Apply.** Choose **Apply**. A progress window follows the run, the view shows the filtered image, and its history
   lists **Median Filter** with the values.

## Controls

**Target Image**, the preview pane, **Close** and the progress window work as in every process window: see
[Process Windows](../../interface/process-windows.md). What is particular to this window is below.

```controls
window: Median Filter
controls:
  - name: Kernel Size
    kind: combo
    default: 3x3
    options: [3x3, 5x5, 7x7, 9x9, 11x11, 13x13, 15x15]
    effect: Chooses the square window whose median replaces each sample, from 3 by 3 to 15 by 15 pixels. Larger outvotes wider structures. A structure disappears where it fills less than half the window around a pixel.
    range: The sizes are odd, so every window has a centre. At the edges of the image the window is cut to the samples inside it, not mirrored, so a 3x3 window holds 4 samples at a corner and 6 along an edge.
    enabled: Disabled while Apply runs.
    tradeoff: The window holds from 9 to 225 samples, and the work of ranking them grows with them. Larger windows erase more stars and thin lines, and round off the corners of structure.
    scope: The preview and Apply, on what **Channel** selects, before the mask.
    persistence: Until the window closes. **Reset** and a finished Apply leave it as it is.
  - name: Iterations
    kind: spinbox
    default: "1"
    widget: {min: 1, max: 10, step: 1}
    effect: Sets how many times the filter runs, each pass on the result of the pass before. More passes grow flat areas and remove more thin detail. Repeated passes are not one larger window, and they move the image toward one that a further pass no longer changes.
    range: The slider moves one pass at a time.
    enabled: Disabled while Apply runs.
    tradeoff: The time grows in proportion to it. More than one pass takes one more working plane the size of a channel, two for a floating-point image.
    scope: The preview and Apply, on what **Channel** selects, with **Modulation** applied in every pass, before the mask.
    persistence: Until the window closes. **Reset** and a finished Apply return it to its default.
  - name: Modulation
    kind: spinbox
    default: "1.00"
    widget: {min: 0, max: 1, step: 0.05, decimals: 2}
    effect: Mixes each pass's median with the sample the pass started from. At 1 a pass writes the median, at 0.5 halfway, and at 0 the sample as it was.
    range: The box keeps two decimals, also when you drag the slider.
    enabled: Disabled while Apply runs. **Apply** stays enabled at 0.
    tradeoff: It lowers the change, not the work, since every window is ranked at any value. With several passes the mix is made again in each, so two passes at 0.25 are not one at 0.5.
    scope: The preview and Apply, inside every pass, before the mask.
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
    effect: Chooses what is filtered. **RGB** takes the median of each of the three channels on its own, so the three values written at one pixel can come from different neighbours. **Red**, **Green** or **Blue** filters that channel and keeps the other two. Every other choice filters one component of another color model and converts the image back.
    enabled: For an RGB image. A grayscale image shows a disabled **Grayscale**. Disabled while Apply runs.
    tradeoff: With **RGB**, small colored structures can change hue. A component costs two conversions and a working copy, and the conversion back can change colors.
    scope: The preview and Apply, before the mask. Process Windows describes each component.
    persistence: Until the window closes. **Reset** leaves it as it is. A finished Apply and a new target return it to **RGB**.
  - name: Preview
    kind: check
    default: "On"
    effect: Shows the filtered image beside the controls, with the mask blended in.
    range: >-
      The pane opens with a **Preview area** of 600 by 600 pixels in the middle of the image, labelled "Preview: Median
      Filter", or the whole image when it is smaller. At the window's default size, **Preview area** and **Take
      snapshot** are in the menu at the end of the pane's toolbar.
    tradeoff: Every change filters the area, or the whole image, again, with every pass. The area is filtered as an image of its own, with its own edges. While you drag a slider with no area drawn, the preview filters the part of the image the pane shows, at full scale, exactly as Apply writes it there.
    scope: The window only. On a sequence it shows the frame the view displays. **Display Mode** and **Autostretch Settings** change only how the preview is shown.
    persistence: Until the window closes. **Reset** leaves it as it is.
  - name: Reset
    kind: button
    default: ""
    effect: Returns **Iterations** and **Modulation** to their defaults, and one preview of them follows. It leaves **Kernel Size** and **Channel** as they are, although its tooltip says that it resets all parameters.
    enabled: Disabled while Apply runs.
  - name: Apply
    kind: button
    default: ""
    effect: Filters the whole image, or every frame of the sequence, with the values and **Channel** as they are when you press it, and then returns **Iterations** and **Modulation** to their defaults. **Kernel Size** stays as it is.
    enabled: From the moment the window opens, at any values, a **Modulation** of 0 included.
    tradeoff: A single image shows a progress window too, and its **Cancel** is noticed between groups of rows. The filter works on single-precision planes, with one window buffer for each group of rows, on the threads the run is granted, beside the second copy every run needs.
    persistence: "The history step is **Median Filter** and reads, for example, `Kernel size 3x3 iterations 1 modulation 1 in channel: RGB`. For a grayscale image it ends after the modulation. The mask is not recorded. The header cards, the plate solution and the recorded processing state are kept."
```

## How it works

1. Each channel that **Channel** selects is read into a single-precision plane, in the image's own units, and
   filtered on its own.
2. For each pixel, the samples of the window around it are put in order and the middle one is taken. For an integer
   image that marks no pixel invalid, the windows from 3x3 to 9x9 away from the edges are ranked by fixed selection
   networks, and every other window by a general selection. Both take the same sample, so the choice changes only the
   speed.
3. **Modulation** mixes the median with the sample the pass started from. The next pass, when **Iterations** asks for
   one, reads that result.
4. The last pass is written back in the image's own pixel type, rounded to the nearest code for an integer image, and
   a mask on the view then blends it with the original.
5. The image replaces the target and its history gets one step. Its header, its
   [plate solution](../../reference/glossary.md#plate-solution) and its recorded
   [processing state](../../reference/glossary.md#processing-state) are kept.

### Model

With \(x_{(1)} \le x_{(2)} \le \dots \le x_{(n)}\) the \(n\) samples of a window in order, the median is

\[ m = \begin{cases} x_{((n+1)/2)} & n \text{ odd} \\ \tfrac{1}{2}\bigl(x_{(n/2)} + x_{(n/2+1)}\bigr) & n \text{ even} \end{cases} \tag{1} \]

A whole window holds an odd count, so only a window cut at an edge, or one that leaves out a sample, takes the mean of
two. The median is no weighted mean of the window, so no blur reproduces it. With \(p\) a sample before a pass and \(a\) the value of **Modulation**, the pass writes

\[ p' = p + a\,(m - p) \tag{2} \]

With \(p_0\) the sample before the first pass and \(p_k\) the sample after \(k\) passes over a window whose median
stays at \(m\), what is left of its distance from the median is

\[ p_k - m = (1 - a)^k\,(p_0 - m) \tag{3} \]

### Boundaries

- The window is cut at the edges of the image, not padded or mirrored, so the border is filtered with smaller,
  lopsided windows. At a corner, a 5x5 window holds 9 samples, not 25, and a 3x3 window of 0, 0.2, 0.8 and 1 gives 0.5.
- In a floating-point image, a sample that is not a finite number is left out of every window and left where it is,
  and a window beside it ranks fewer samples, as at an edge. An image that marks its own invalid pixels is read under
  its own rule.
- Every pixel type keeps its type and is ranked in single precision. A 32-bit integer or a 64-bit floating-point image
  is rounded to it on the way in, so nearly equal samples can come back at the nearest value single precision holds.
- At a **Modulation** of 0 every window is still ranked, and the image comes back as it was.
- **Cancel**, or closing the window, stops the run between groups of rows, and nothing is written.

### Worked example

The image is the linear [master](../../reference/glossary.md#master). Its channels have their median near 0.028, and
their grain from one pixel to the next is about 0.0003 in red, 0.001 in green and 0.0007 in blue. The window keeps its
defaults: **Kernel Size** at **3x3**, **Iterations** at 1 and **Modulation** at 1.00.

A hot pixel of 0.5 among background samples near 0.028 is the largest of its nine, and by equation (1) it is replaced
by a background value. With **Modulation** at 0.25 instead, equation (2) writes 0.382, and a second pass leaves 0.5625
of its excess by equation (3), about 0.29. A star with a [FWHM](../../reference/glossary.md#fwhm) of 3 pixels has its
four nearest neighbours at 0.73 of its peak above the background and its four corners at 0.54. The fifth of the nine in
order is 0.73, so its peak drops by about a quarter, and a star with a FWHM of 2 pixels loses half its peak. For grain
that changes from one pixel to the next, the median of nine samples varies 0.41 times as much as one sample. The grain
falls to about 0.0004 in green, 0.0003 in blue and 0.0001 in red, and the background's level stays near 0.028. For
the pair, a crop of the middle of the master is filtered with these settings, and the crop and its result are then
both stretched by Histogram Transformation with **Shadows** and **Midtones** at 0.02 and shown in the Linear display
mode.

```figure
images:
  - image: ../../assets/images/processes/noise-reduction/noise-crop-before.webp
    alt: >-
      A crop at 100 % of the middle of the master, stretched: pink clouds of nebula beside a dark dust lane, many small
      stars, and a fine colored grain over the background.
    label: Before
  - image: ../../assets/images/processes/noise-reduction/median-filter-after.webp
    alt: >-
      The same crop filtered before the same stretch, the grain finer and a little blotchy, the faintest stars gone and
      the small ones dimmer, the edges of the dust lanes in place.
    label: After
caption: >-
  The middle of the Carina Nebula master at 100 %, without and with Median Filter at its defaults, both then stretched
  by Histogram Transformation with Shadows and Midtones at 0.02.
```

## Starting points

| Situation | Start | Then adjust |
| --- | --- | --- |
| Hot or cold pixels left after calibration | **3x3**, one pass, **Modulation** at 1 | Lower **Modulation** when faint stars fade. |
| Specks three pixels across | **5x5** | Check small stars at 100 %, and protect them with a mask. |
| Fine structure that must stay | **Modulation** near 0.5 | Raise it until the defects are gone. |
| A mask with specks in it | **3x3** or **5x5**, two or three passes | Check the mask's edges at 100 %. |

## When it goes wrong

!!! warning "Small stars go with the defects"
    A star, a close pair of stars or a thin line that fills less than half the window is outvoted as a hot pixel is. Start at **3x3** and one
    pass, check the faint stars at 100 %, and protect them with a mask, or lower **Modulation**, when they fade.

Larger specks or clusters of hot pixels stay
:   **Cause.** They fill half the window or more around their pixels.
:   **Fix.** Choose **5x5**, or remove them with Cosmetic Correction.

The background turns patchy, with flat areas
:   **Cause.** Repeated passes and large windows grow flat areas, and the median leaves fine grain blotchy.
:   **Fix.** Lower **Iterations** first, then **Kernel Size** or **Modulation**. Reduce grain with a denoiser.

The border of the image differs from its middle
:   **Cause.** The windows are cut at the edges and hold fewer samples there.
:   **Fix.** Check the whole frame, and crop the border if it shows.

Colors shift around small features
:   **Cause.** With **RGB**, each channel takes its median on its own, from different neighbours.
:   **Fix.** Choose **CIE L* (Lightness)** in **Channel**, and compare it with **RGB** on a duplicate.

Samples that are not numbers, or infinite ones, are still there
:   **Cause.** They are left out of every window and left in place.
:   **Fix.** Replace them, or crop them away, before the filter.

**Kernel Size** stays after **Reset** or **Apply**
:   **Cause.** **Reset** and **Apply** return only **Iterations** and **Modulation** to their defaults.
:   **Fix.** Choose **3x3** again.

The filter is slow, even at a **Modulation** of 0
:   **Cause.** The work grows with the samples of the window and the passes, and every window is ranked at any
    **Modulation**.
:   **Fix.** Choose a smaller window or fewer passes, tune in the preview area, and cancel a run that takes too long.

The preview area differs from the result of Apply
:   **Cause.** The area is filtered as an image of its own, with its own edges.
:   **Fix.** Untick **Preview area** for the last check before **Apply**, and compare under one display mode.

## Related

- [Cosmetic Correction](../calibration/cosmetic-correction.md), which replaces the defective pixels alone.
- [Bilateral Filter](bilateral-filter.md) and [Gaussian Blur](gaussian-blur.md), which average the neighbours instead of
  ranking them.
- [Morphological Transformation](../masking/morphological-transformation.md), which takes any rank of a shaped window.
- [Wavelet Denoise](wavelet-denoise.md) and [Total Variation Denoise](total-variation-denoise.md), for the fine grain
  the median leaves blotchy.
- [Masks](../../interface/masks.md) and [Image History](../../interface/image-history.md), to protect the stars and to
  undo the step.
- [Process Windows](../../interface/process-windows.md), for the preview, the preview area and what every process
  window shares.

## Credits

- T. S. Huang, G. J. Yang and G. Y. Tang,
  [A Fast Two-Dimensional Median Filtering Algorithm](https://doi.org/10.1109/TASSP.1979.1163188), IEEE Transactions on
  Acoustics, Speech, and Signal Processing 27 (1979), for the two-dimensional median filter.
- The selection networks of the 3x3 to 9x9 windows are [RawTherapee](https://github.com/Beep6581/RawTherapee)'s, by
  Flössie and Ingo Weyrich, from a file that states "Copyright (c) 2004-2010 Gabor Horvath", as
  [librtprocess](https://github.com/CarVac/librtprocess) carries them, under the GNU General Public License, version 3
  or any later version. AfterNight copies them unchanged apart from their names, and its copy is a derived work
  distributed under that licence. The general selection that serves every other window, and the choice between the
  two, are AfterNight's. **About > About AfterNight** credits them.
