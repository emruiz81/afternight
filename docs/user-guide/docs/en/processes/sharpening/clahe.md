---
kind: process
stage: finish
menu: Process > Intensity Transformations > CLA Histogram Equalization
summary: >-
  Raises local contrast with contrast-limited adaptive histogram equalization: the image is cut into a grid of tiles,
  each tile's histogram is clipped and equalized, and every pixel is mapped through the tiles around it.
data_in: nonlinear
data_out: nonlinear
destructive: true
preview: real-time
sequences: true
masks: post-blend
external: none
glance:
  enabled: >-
    An image or a sequence view is active, no CLAHE window is open on it yet, and no window that needs the image for
    itself is open on it.
  inputs: One grayscale or RGB image or sequence, and an optional mask on its view.
  outputs: >-
    The same view, replaced, with one history step, its processing state recorded as nonlinear. Its size, channels and
    pixel type are kept. No file is written.
  state: >-
    A stretched image, after noise reduction, whose structure needs more local contrast than one curve for the whole
    image can give.
derived: false
rewritten: true
---
# CLAHE

CLAHE is contrast-limited adaptive histogram equalization. It cuts the image into a square grid of tiles, clips the
histogram of each tile so that no tone range is stretched too far, equalizes it, and maps every pixel through the
mappings of the tiles around it, mixed by distance. On a color image it equalizes the lightness alone. AfterNight's
own engine computes it, and the window opens from **Process > Intensity Transformations > CLA Histogram
Equalization**.

**Apply** replaces the image or [sequence](../../reference/glossary.md#sequence) with the equalized one, keeps its
size and [pixel type](../../reference/glossary.md#pixel-type), adds one step to its
[history](../../reference/glossary.md#history), and records the result as nonlinear.

```figure
image: ../../assets/images/processes/sharpening/clahe.webp
alt: >-
  The CLAHE window. On the left, a histogram with three colored peaks and the lines Clipping Low and Clipping High in
  its corner, a toolbar, then the Grid Size, Contrast Limit and Amount boxes with their sliders, Target Image, Channel
  at RGB and Preview. On the right, the preview toolbar with Preview area unticked, over the whole equalized nebula.
  Reset, Close and Apply run along the bottom.
caption: >-
  CLAHE on a stretched master with Grid Size at 8 and Contrast Limit at 1.50: the preview shows what Apply writes over
  the whole image.
pins:
  - {n: 2, x: 2.0, y: 82.8}
  - {n: 3, x: 2.0, y: 61.1}
  - {n: 4, x: 2.0, y: 56.6}
  - {n: 5, x: 47.5, y: 50.0}
  - {n: 6, x: 2.0, y: 65.5}
  - {n: 7, x: 91.0, y: 96.1}
```

## Before you start

- [Stretch](../../reference/glossary.md#stretch) the image and reduce its noise first. The equalization enlarges
  noise and leftover calibration defects along with the structure, and on linear data it remaps the background and
  its noise hard.
- **Apply** replaces the image. Its history keeps the step before: duplicate the image when you want to compare.
- A raw color-filter mosaic is equalized as one grayscale plane, which can strengthen its pattern:
  [debayer](../../reference/glossary.md#debayer) it first. The frames of a sequence follow its view's automatic
  debayer setting.
- A [mask](../../reference/glossary.md#mask) on the view blends the result with the original after **Amount**.

## Steps

1. **Open the window.** Make the stretched image the active one and choose **Process > Intensity Transformations >
   CLA Histogram Equalization**. The window is titled **CLAHE**, its preview shows the whole image already
   equalized, and **Apply** is enabled at once.
2. **Choose what to equalize.** Leave **Channel** at **RGB** to equalize the lightness and keep the colors.
3. **Set the limit.** At 100 % in the preview, lower **Contrast Limit** until the grain of the background and the
   rims of the stars calm down.
4. **Set the tiles.** Raise **Grid Size** for smaller tiles and a more local result, or lower it for larger tiles.
5. **Check the preview.** Look for grain, mottling the size of a tile, seams between tiles, halos and clipping, at
   100 % and over the whole image. Press ++space++ over the preview to compare with the original.
6. **Soften the result.** Lower **Amount** last, to fade the whole effect.
7. **Apply.** Choose **Apply**. The image in its view is the equalized one, its history lists **CLAHE** with the
   three values, and the controls return to their defaults.

## Controls

The histogram above the controls, its toolbar and the preview pane are the same in every window: see
[Process Windows](../../interface/process-windows.md). The histogram cannot show seams or halos, and its grid, its
clipping lines and **Plot resolution** have nothing to do with the tiles, the clip limit or the levels of the
equalization.

```controls
window: CLAHE
controls:
  - name: Grid Size
    kind: spinbox
    default: "8"
    widget: {min: 1, max: 256, step: 1}
    effect: Sets how many tiles divide each side of the image, so the image holds this number squared. Higher makes smaller tiles and a more local equalization. Lower makes larger tiles. At 1 one clipped equalization covers the whole image.
    range: It counts tiles, not pixels. The tiles are as equal as whole pixels allow, and the image is not padded. A grid with more tiles than the image has pixels along a side is refused with "a CLAHE grid cannot have more tiles than the image has pixels on an axis".
    tradeoff: Many small tiles show mottling and seams the size of a tile, and their histograms are sparse. Few large tiles give broad halos and miss small structure. Each tile holds a table of 8 bytes a level. With 65,536 levels, 8 needs about 34 MB, 32 about 537 MB, 64 about 2.1 GB and 256 about 34 GB, refused by the memory limit. The tables are built again for each channel, not kept for all.
    scope: The preview and Apply, on what **Channel** selects, before **Amount** and the mask. A preview area is divided into its own grid.
    persistence: Until the window closes. **Reset** and a finished Apply return it to its default.
  - name: Contrast Limit
    kind: spinbox
    default: "2.00"
    widget: {min: 1, max: 64, step: 0.5, decimals: 2}
    effect: Caps every level of a tile's histogram at this many times the count it would hold if the tile's samples were spread evenly. Higher allows steeper mappings and stronger contrast. Lower holds the equalization back.
    range: The slider moves it in tenths. The cap is a whole count of at least 1, so on small tiles with 65,536 levels several values give the same cap, and the limit acts lower than it reads.
    tradeoff: Higher values enlarge grain, halos and clipping. Lower values restrain the equalization, but the counts cut off are spread over every level and still change the mapping. It does not change the time a run takes.
    scope: The preview and Apply, on what **Channel** selects, before **Amount** and the mask.
    persistence: Until the window closes. **Reset** and a finished Apply return it to its default.
  - name: Amount
    kind: spinbox
    default: "1.00"
    widget: {min: 0, max: 1, step: 0.05, decimals: 2}
    effect: Mixes the result with the original. At 1 the image takes the whole result, at 0 it stays the original, and at 0.5 it takes half of each.
    range: The slider moves it in thousandths.
    tradeoff: It fades everything the equalization did, seams and grain included, and saves no time, because the equalization runs in full at any value.
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
    effect: Chooses what is equalized. **RGB** converts red, green and blue to CIE Lab and equalizes the lightness L* alone, between 0 and 100. a*, b* and an alpha channel keep their values. **Red**, **Green** or **Blue** equalizes that channel over its pixel type's full range. Every other choice equalizes one component between 0 and 1 and converts it back to RGB.
    enabled: For an RGB image. A grayscale image shows a disabled **Grayscale**, and its one channel is equalized over its pixel type's full range.
    tradeoff: Equalizing L* keeps the colors far better than three channels equalized apart, but the way back to RGB can push saturated colors out of range. A hue, saturation or chroma component equalizes color itself and rarely suits a photograph, and a hue jumps where its ends meet.
    scope: The preview and Apply, before **Amount** and the mask.
    persistence: Until the window closes. **Reset** leaves it as it is. A finished Apply returns it to **RGB**.
    note: With **RGB**, the red, green and blue are read as linear when the image is described as linear RGB, and as sRGB-encoded otherwise, in a 32-bit floating-point working copy, and converted back the same way.
  - name: Preview
    kind: check
    default: "On"
    effect: Shows the result beside the controls, with **Amount** and the mask applied. When the window opens it shows the whole image, with **Preview area** off. Ticked, the area frames 600 by 600 pixels in the middle of the image.
    tradeoff: Every change equalizes the whole image again, with its color conversions and tile tables.
    scope: The window only. A preview area is equalized as an image of its own, cut into its own grid, so its tiles are smaller than those of Apply.
    persistence: Until the window closes. **Reset** leaves it as it is.
  - name: Reset
    kind: button
    default: ""
    effect: Returns **Grid Size**, **Contrast Limit** and **Amount** to their defaults.
    range: It leaves **Channel**, the target, **Preview** and the preview area as they are.
  - name: Apply
    kind: button
    default: ""
    effect: Equalizes the image, or every frame of the sequence, with the values as they are when you press it, then returns them to their defaults.
    enabled: From the moment the window opens, at any values.
    tradeoff: A color image needs its 32-bit working copy and four working planes, beside the second copy every run needs and the tile tables. The tables are built, and the rows mapped, on the threads the run is given.
    persistence: >-
      The history step is **CLAHE** and reads, for example, `Grid Size: 8, Contrast Limit: 1.50, Amount: 1.00 in
      channel: RGB`. For a grayscale image it ends after the amount. The mask is not recorded.
```

## How it works

1. With **RGB** on a color image, red, green and blue are converted to
   [CIE Lab](../../reference/glossary.md#cie-lab) with the D65 white of sRGB, and L* is equalized between
   0 and 100. A grayscale image, a channel or a component is equalized in its own units over its full range. The
   histograms have 256 levels for an 8-bit image and 65,536 for every other pixel type.
2. The image is cut into **Grid Size** by **Grid Size** tiles, and each tile counts a histogram of the pixels the image
   marks valid.
3. Each histogram is clipped at the cap **Contrast Limit** sets, and the counts cut off are spread evenly over every
   level. Its cumulative count, the [CDF](../../reference/glossary.md#cdf) of the tile, becomes the tile's mapping
   onto the full range.
4. Every pixel is mapped through the four tiles around it, mixed by its distance from their centers, and written back
   in the image's pixel type, rounded for an integer image. **Amount** and a mask on the view then mix the result with
   the original.
5. The image replaces the target and its history gets one step. Its header, its
   [plate solution](../../reference/glossary.md#plate-solution) and its acquisition data are kept. Its
   [processing state](../../reference/glossary.md#processing-state) is recorded as nonlinear, and a saved file states
   in its [header cards](../../reference/glossary.md#header-card) that `ANLIN` is `nonlinear` and `ANOP` is `clahe`.
   Its [lineage](../../reference/glossary.md#lineage) names the process. A preview records nothing.

### Model

For a tile of \(P\) valid pixels, a histogram of \(B\) levels and \(C\) the value of **Contrast Limit**, every level
holds at most

\[ c = \max\bigl(1,\ \lfloor C\,P / B \rfloor\bigr) \tag{1} \]

samples. \(P / B\) is the count a level would hold if the tile were spread evenly. The counts above \(c\) are taken
off and spread over all \(B\) levels: each gets the same whole share, and what remains goes one each to levels spaced
evenly from the lowest. With \(h_j\) the count of level \(j\) after that, and \(\ell_0\) and \(\ell_1\) the ends of the
range, a sample in level \(k\) maps to

\[ T(k) = \ell_0 + (\ell_1 - \ell_0)\,\frac{1}{P}\sum_{j \le k} h_j \tag{2} \]

Where the mapping climbs faster than an even spread would, contrast grows. The cap keeps that climb to about
\(C + 1\) times the even rate, however crowded a tone range is, unless the cap sits at its minimum of 1. A pixel at
column \(x\) of an image \(W\) pixels wide, with \(N\) the value of **Grid Size**, lies at

\[ g = \frac{(x + 0.5)\,N}{W} - 0.5 \tag{3} \]

in tile units, so tile centers sit on the centers of their pixels. The result mixes the mappings of the two tiles
either side of \(g\), and of the two above and below in the same way. With \(a\) the value of **Amount**, \(m\) the
mask's weight at the pixel, 1 where there is no mask, \(x\) the original sample and \(y\) the equalized one, the
sample written is

\[ z = x + a\,m\,(y - x) \tag{4} \]

### Boundaries

- A pixel beyond the outermost tile centers takes the mapping of its border tile alone.
- Pixels the image marks invalid are left as they were. In a color image their lightness is kept, so they come back
  as their own color within the rounding of the round trip. A tile without a valid pixel maps every value to itself.
- The result stays within the range: a floating-point sample of a grayscale image, or of one channel, outside 0 to 1
  is cut to it. A sample that is not a number counts in the lowest level of its tile.
- Every pixel type is equalized in 32-bit precision, so a 64-bit floating-point or 32-bit integer image loses
  precision.
- A single image shows no progress window and cannot be cancelled once Apply starts. On a sequence, **Cancel** also
  stops the frame being equalized.

### Worked example

The image in the picture above is the linear [master](../../reference/glossary.md#master) after Histogram
Transformation with **Shadows** and **Midtones** at 0.02, which leaves the background near 0.28. It is a 32-bit RGB
image of 7246 by 5429 pixels. **Channel** is at **RGB**, **Grid Size** at 8, **Contrast Limit** at 1.50 and
**Amount** at 1.

The tiles are 905 or 906 pixels wide and 678 or 679 tall, so a tile holds about 613,600 pixels over 65,536 levels of
lightness: about 9.4 a level if spread evenly. By equation (1) every level is capped at 14 samples, where the default
limit of 2 gives 18. The crowded levels of the background are cut hardest, yet their contrast still grows by up to
about 2.5 times, and the grain with it. At **Grid Size** 16 a tile holds about
153,200 pixels, and the same limit gives a cap of 3: the whole count makes the limit act like 1.28.

On a 1024 by 1024 8-bit grayscale image, **Grid Size** 8 makes tiles of 128 by 128 pixels. At the default limit the
cap is 128, and 32 at **Grid Size** 16. A 16-bit image of that size has 65,536 levels, and its cap falls to the
minimum of 1. **Amount** at 0.5 averages the original and the result, pixel by pixel, before the mask.

```figure
images:
  - image: ../../assets/images/processes/sharpening/clahe-before.webp
    alt: >-
      A crop at 100 % of the middle of the stretched master, pink clouds of nebula beside a dark dust lane,
      and many stars on a grainy background.
    label: Before
  - image: ../../assets/images/processes/sharpening/clahe-after.webp
    alt: >-
      The same crop after Apply, the nebula's structure separated more strongly, the background lifted and grainier,
      and the colors kept.
    label: After
caption: >-
  The middle of the stretched Carina Nebula master at 100 %, before and after Apply with Grid Size at 8 and Contrast
  Limit at 1.50. Both are shown in the Linear display mode, without a display stretch.
```

## Starting points

These are starting ranges, not presets: the cap a limit gives changes with the size of the tiles and the number of
levels.

| Situation | Start | Then adjust |
| --- | --- | --- |
| A careful first pass | **Grid Size** 8, **Contrast Limit** 1 to 2, **Amount** 0.25 to 0.6 | Watch the grain of the background, faint gradients and the rims of small stars at 100 %. |
| Broader structures | **Grid Size** 2 to 6, **Contrast Limit** 1 to 3, **Amount** 0.25 to 0.7 | Watch for wide halos and an uneven background. |
| Smaller structures | **Grid Size** 12 to 24, **Contrast Limit** 1 to 2, **Amount** 0.15 to 0.5 | Watch for mottling the size of a tile, seams, noise and star cores. |
| Finding where clipping starts | Your grid, **Contrast Limit** at 1, then 2, then higher, **Amount** at 1 | Watch the ends of the histogram and the halos, then lower **Amount**. |

## When it goes wrong

!!! warning "A preview area does not show what Apply writes"
    A [preview area](../../reference/glossary.md#preview-area) is cut into its own grid, so its tiles are much smaller
    than those of the whole image. Leave **Preview area** off, as the window opens, to judge the result.

!!! warning "The image is recorded as nonlinear"
    Apply records the result as nonlinear, and a later process that needs a
    [linear image](../../reference/glossary.md#linear-image) treats it so. Finish every linear step first, or undo
    the step in the image's history.

The background turns grainy
:   **Cause.** **Contrast Limit**, **Grid Size** or **Amount** is too high for the noise of the image.
:   **Fix.** Lower **Contrast Limit** first, then **Amount**, use fewer tiles, or reduce the noise before.

Blocks, seams or mottling appear
:   **Cause.** The mappings of neighboring tiles differ strongly.
:   **Fix.** Lower **Grid Size** or **Contrast Limit**, and compare with the original over several tile boundaries.

Stars gain rings or broad halos
:   **Cause.** The tile's mapping stretches the bright transitions around them.
:   **Fix.** Lower **Amount** or **Contrast Limit**, try another **Grid Size**, or protect the stars with a mask.

Colors shift or jump
:   **Cause.** A hue, saturation or chroma component was equalized, or saturated colors left the range on the way
    back to RGB.
:   **Fix.** Use **RGB** or **CIE L* (Lightness)**, lower **Amount**, or protect saturated areas with a mask.

Changing Contrast Limit seems to do little
:   **Cause.** The cap rounds to a whole count, which on small tiles of a deep image moves in large steps, or
    **Amount** or the mask fade the result.
:   **Fix.** Change **Grid Size**, raise the limit in larger steps, and check **Amount** and the mask.

"a CLAHE grid cannot have more tiles than the image has pixels on an axis"
:   **Cause.** **Grid Size** is larger than the width or the height of the image, or of the preview area.
:   **Fix.** Lower **Grid Size**.

A high Grid Size is refused for memory
:   **Cause.** The tile tables take 8 bytes a level in every tile, which runs into gigabytes past a grid of 64.
:   **Fix.** Lower **Grid Size**.

## Related

- [Local Histogram Equalization](local-histogram-equalization.md), which ranks every pixel in its own neighborhood,
  without tiles or a clip limit.
- [Local Contrast Enhancement](local-contrast-enhancement.md), which raises contrast by the difference from a wide
  blur.
- [Wavelet Denoise](../noise-reduction/wavelet-denoise.md), to reduce the noise before.
- [Masks](../../interface/masks.md), to protect the background, the stars or saturated colors.
- [Image History](../../interface/image-history.md), to compare with the step before or undo it.
- [Process Windows](../../interface/process-windows.md), for the preview, the histogram and what every process window
  shares.

## Credits

- The [OpenCV 4.12.0 reference of its CLAHE class](https://docs.opencv.org/4.12.0/d6/db6/classcv_1_1CLAHE.html)
  describes the clip limit and the tile grid this explanation uses.
- The OpenCV
  [CLAHE source](https://github.com/opencv/opencv/blob/4.12.0/modules/imgproc/src/clahe.cpp) shows the cap, the
  redistribution of the clipped counts in equal steps and the bilinear mapping this explanation describes.
