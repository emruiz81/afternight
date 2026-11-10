---
kind: process
stage: finish
menu: Process > Intensity Transformations > Local Histogram Equalization
summary: >-
  Ranks each pixel's lightness among its neighbors and blends that rank back over the image, brightening what is
  bright for its surroundings and darkening what is dark, computed by G'MIC.
data_in: nonlinear
data_out: nonlinear
destructive: true
preview: real-time
sequences: true
masks: post-blend
external: The G'MIC library bundled with AfterNight
glance:
  enabled: >-
    An image or a sequence view is active, no Local Histogram Equalization window is open on it yet, and no window
    that needs the image for itself is open on it.
  inputs: >-
    One grayscale or RGB image or sequence whose samples are all finite numbers, and an optional mask on its view.
  outputs: >-
    The same view, replaced, with one history step, its processing state recorded as nonlinear. Its size, channels and
    pixel type are kept, and its samples are held between 0 and 1. No file is written.
  state: >-
    A stretched image, after noise reduction, whose structure needs more separation between neighboring tones.
derived: true
rewritten: true
---
# Local Histogram Equalization

Local Histogram Equalization raises local contrast by ranking. For every pixel it builds a histogram of the lightness
in a small square around it, finds where the pixel stands in it, and blends that standing back over the image: a pixel
brighter than its neighbors is brightened, a darker one is darkened. Each pixel gets its own mapping, with no tiles
and no clip limit.

**Apply** replaces the image or [sequence](../../reference/glossary.md#sequence) with the equalized one, keeps its
size and [pixel type](../../reference/glossary.md#pixel-type), adds one step to its
[history](../../reference/glossary.md#history), and records the result as nonlinear. The bundled G'MIC library
computes the samples.

```figure
image: ../../assets/images/processes/sharpening/local-histogram-equalization.webp
alt: >-
  The Local Histogram Equalization window. On the left, a histogram with three colored peaks and the lines Clipping
  Low and Clipping High in its corner, a toolbar, then the Strength, Radius, Sigma, Regularization and Amount boxes
  with their sliders, the Reduce Halos check box ticked, Target Image, Channel at RGB and Preview. On the right, the
  preview of the stretched nebula, with a framed area in its middle that shows the equalized result. Reset, Close and
  Apply run along the bottom.
caption: >-
  Local Histogram Equalization on a stretched master with its default values: the framed preview area shows what
  Apply writes there.
pins:
  - {n: 2, x: 2.0, y: 85.5}
  - {n: 3, x: 2.0, y: 52.1}
  - {n: 4, x: 2.0, y: 56.3}
  - {n: 5, x: 2.0, y: 60.6}
  - {n: 6, x: 2.0, y: 47.9}
  - {n: 7, x: 73.4, y: 16.6}
  - {n: 8, x: 2.0, y: 64.8}
  - {n: 9, x: 91.0, y: 96.7}
```

## Before you start

- [Stretch](../../reference/glossary.md#stretch) the image and reduce its noise first. The ranking enlarges every
  small difference between neighbors, noise and defects included.
- **Apply** replaces the image. Its history keeps the step before: duplicate the image when you want to compare.
- Every sample must be a finite number, and what G'MIC returns is held between 0 and 1. A floating-point sample
  outside that range is cut to it, and a 64-bit image is computed in 32-bit precision.
- The process needs G'MIC, which AfterNight includes: nothing is installed or downloaded. A build without G'MIC keeps
  **Preview** and **Apply** disabled, and their tooltip reads "This build of AfterNight carries no G'MIC library, so
  this process cannot run."
- A raw color-filter mosaic is equalized as one grayscale plane, which strengthens its pattern:
  [debayer](../../reference/glossary.md#debayer) it first. The frames of a sequence follow its view's automatic
  debayer setting.
- A [mask](../../reference/glossary.md#mask) on the view blends the result with the original after **Amount**.

## Steps

1. **Open the window.** Make the stretched image the active one and choose **Process > Intensity Transformations >
   Local Histogram Equalization**. The preview shows a framed area in the middle of the image already equalized, and
   **Apply** is enabled at once.
2. **Choose what to equalize.** Leave **Channel** at **RGB** to equalize the lightness and keep the colors.
3. **Set the neighborhood.** Raise **Radius** for broader structures. Every step makes the run much longer.
4. **Set the tone range.** Keep **Reduce Halos** ticked and lower **Sigma** to rank each pixel among neighbors of
   similar brightness only.
5. **Calm the map.** Raise **Regularization** when the background turns mottled, and lower it when fine structure
   flattens.
6. **Set the strength.** Lower **Strength (%)** to weaken the whole effect. Keep it above 0.
7. **Check the whole image at 100 %.** Untick **Preview area**, zoom into the background, a bright star and the edge
   of the frame, and press ++space++ over the preview to compare with the original.
8. **Soften the result.** Lower **Amount** last, to fade the whole effect.
9. **Apply.** Choose **Apply**. The image in its view is the equalized one, its history lists **Local Histogram
   Equalization** with the six values, and the controls return to their defaults.

## Controls

The histogram above the controls, its toolbar and the preview pane are the same in every window: see
[Process Windows](../../interface/process-windows.md). A histogram cannot show a halo: judge halos on the preview.

```controls
window: Local Histogram Equalization
controls:
  - name: Strength (%)
    kind: spinbox
    default: "75.00"
    widget: {min: 0, max: 100, step: 1, decimals: 2}
    effect: Sets how much of the equalized lightness is blended in. A pixel at the top of its neighborhood is brightened by up to this share of its value, and one at the bottom darkened by up to it. Lower weakens both.
    range: The slider moves it in tenths. At 0 the preview and Apply are refused, because G'MIC then returns two images instead of one. Use **Amount** to fade the result to nothing.
    tradeoff: High values look crunchy and flat, enlarge noise and clip the brightest pixels. The equalization costs as much at any value.
    scope: The preview and Apply, on what **Channel** selects, before **Amount** and the mask.
    persistence: Until the window closes. **Reset** and a finished Apply return it to its default.
  - name: Radius
    kind: spinbox
    default: "4"
    widget: {min: 1, max: 16, step: 1}
    effect: Sets the size of the square neighborhood every pixel is ranked in, a side of twice the radius plus one pixel. Higher ranks a pixel among broader structures, from 3 by 3 pixels at 1 to 33 by 33 at 16.
    tradeoff: The work for every pixel is the square's area plus a scan of 512 levels, so 9 samples at 1 and 1,089 at 16. Larger squares spread halos further.
    scope: The preview and Apply, on what **Channel** selects, before **Amount** and the mask.
    persistence: Until the window closes. **Reset** and a finished Apply return it to its default.
  - name: Sigma
    kind: spinbox
    default: "100.00"
    widget: {min: 0, max: 256, step: 0.05, decimals: 2}
    effect: Sets how far apart in lightness a neighbor may be and still count, in levels of 512 from the darkest pixel to the brightest. With **Reduce Halos** ticked, higher lets more different neighbors count, and lower ranks a pixel only among neighbors of similar lightness.
    range: The slider moves it in tenths. It measures a difference in lightness, not a distance in pixels, which **Radius** sets alone.
    enabled: Its effect turns round when **Reduce Halos** is unticked.
    tradeoff: Very low values leave almost no neighbor to rank against. Very high values count every neighbor almost alike.
    scope: The preview and Apply, on what **Channel** selects, before **Amount** and the mask.
    persistence: Until the window closes. **Reset** and a finished Apply return it to its default.
  - name: Regularization
    kind: spinbox
    default: "8.00"
    widget: {min: 0, max: 32, step: 0.05, decimals: 2}
    effect: Smooths the map of ranks over about this many pixels before it is blended in. Higher calms a mottled background and broad halos. At 0 the map is not smoothed, and every pixel is ranked on its own.
    range: The slider moves it in tenths.
    tradeoff: Higher values also soften the fine separation the process is for, and cost one more pass. At 0 the grain of the background is ranked pixel by pixel and grows strongly.
    scope: The preview and Apply, on what **Channel** selects, before the blend, **Amount** and the mask.
    persistence: Until the window closes. **Reset** and a finished Apply return it to its default.
    note: It is G'MIC's bilateral filter, guided by the lightness, with a range of 2 more than this value. On the fractions of full scale G'MIC is given, the lightness spans far less than that range, so the smoothing does not stop at edges.
  - name: Amount
    kind: spinbox
    default: "1.00"
    widget: {min: 0, max: 1, step: 0.05, decimals: 2}
    effect: Mixes the result with the original. At 1 the image takes the whole result, at 0 it stays the original, and at 0.5 it takes half of each.
    range: The slider moves it in thousandths.
    tradeoff: It fades everything the equalization did and saves no time, because the equalization runs in full at any value. It cannot bring back what was clipped.
    scope: The preview and Apply, on the whole image once **Channel** has been converted back, before the mask.
    persistence: Until the window closes. **Reset** and a finished Apply return it to its default.
  - name: Reduce Halos
    kind: check
    default: "On"
    effect: On, a neighbor counts more the closer its lightness is to the pixel's, and one of the same lightness counts fully. Off, the weights are turned round. A neighbor of the same lightness counts nothing, and the more different ones count most.
    tradeoff: Off gives aggressive mappings that change a lot from pixel to pixel. On is the gentler choice, but it does not remove every halo.
    scope: The preview and Apply, with **Sigma**, before **Amount** and the mask.
    persistence: Until the window closes. **Reset** and a finished Apply tick it again.
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
    effect: Chooses what is equalized. **RGB** ranks the lightness and multiplies the three channels of a pixel by the same factor, which keeps their ratios. Every other choice gives G'MIC one plane, which it equalizes as a grayscale image, and the plane is then converted back to RGB.
    enabled: For an RGB image. A grayscale image shows a disabled **Grayscale**.
    tradeoff: A chroma or hue component can come back as colors outside the range or as jumps of hue. A component costs two conversions of the image.
    scope: The preview and Apply, before **Amount** and the mask.
    persistence: Until the window closes. **Reset** leaves it as it is. A finished Apply returns it to **RGB**.
  - name: Preview
    kind: check
    default: "On"
    effect: Shows the result beside the controls, with **Amount** and the mask applied. When the window opens it shows a framed preview area of 600 by 600 pixels in the middle of the image.
    tradeoff: Every change runs the whole equalization again, in a new G'MIC session. On the whole of a large image it takes far longer than on the area.
    scope: The window only. A preview area is equalized as an image of its own, between its own darkest and brightest pixels, so it can rank differently from the whole image.
    persistence: Until the window closes. **Reset** leaves it as it is.
    note: In a build without G'MIC it is off and disabled, with the tooltip "This build of AfterNight carries no G'MIC library, so this process cannot run."
  - name: Reset
    kind: button
    default: ""
    effect: Returns **Strength (%)**, **Radius**, **Sigma**, **Regularization** and **Amount** to their defaults and ticks **Reduce Halos**.
    range: It leaves **Channel**, the target, **Preview** and the preview area as they are.
  - name: Apply
    kind: button
    default: ""
    effect: Equalizes the image, or every frame of the sequence, with the values as they are when you press it, then returns them to their defaults.
    enabled: From the moment the window opens, at any values. In a build without G'MIC it is disabled, with the tooltip **Preview** shows.
    tradeoff: The image goes to G'MIC as a 32-bit floating-point working copy, beside the second copy every run needs. A single image shows a progress window with a busy bar, because the command reports no share done, and its **Cancel** stops the run.
    persistence: >-
      The history step is **Local Histogram Equalization** and reads, for example, `Strength: 75.00, Radius: 4, Sigma:
      100.00, Regularization: 8.00, Amount: 1.00, Reduce Halos: true in channel: RGB`. For a grayscale image it ends
      after **Reduce Halos**. The mask and the G'MIC version are not recorded.
```

## How it works

1. The channels **Channel** selects go to the bundled G'MIC library as 32-bit floating-point samples, each a
   [fraction of full scale](../../reference/glossary.md#fraction-of-full-scale), in one G'MIC session for the run. An
   image with a sample that is not a finite number is refused before G'MIC sees it.
2. G'MIC converts the image to [CIE Lab](../../reference/glossary.md#cie-lab) and works on its lightness, in which
   green counts most. A single plane is worked as a grayscale image. The lightness is spread over 512 levels, from the
   darkest pixel of what G'MIC is given to the brightest.
3. For every pixel, a weighted histogram of its square neighborhood gives the share of neighbors at or below its
   level: its [CDF](../../reference/glossary.md#cdf) there. Over the image, that map is stretched to run from 0 to 1
   and smoothed by **Regularization**.
4. The map is blended over the image in G'MIC's soft-light mode, at an opacity of **Strength (%)**. The result is
   held between 0 and 1 and written back in the image's own pixel type, rounded to the nearest value. **Amount** and
   a mask on the view then mix it with the original.
5. The image replaces the target and its history gets one step. Its header, its
   [plate solution](../../reference/glossary.md#plate-solution) and its acquisition data are kept. Its
   [processing state](../../reference/glossary.md#processing-state) is recorded as nonlinear, and a saved file states
   in its [header cards](../../reference/glossary.md#header-card) that `ANLIN` is `nonlinear` and `ANOP` is
   `local-histogram-equalization`. Its [lineage](../../reference/glossary.md#lineage) names the process. A preview
   records nothing.

### Model

With \(k\) the lightness level of a pixel, \(k_u\) that of a neighbor \(u\) in its square of side \(2r + 1\), \(r\)
the value of **Radius** and \(\sigma\) that of **Sigma**, the neighbor weighs

\[ w_u = e^{-\left(|k_u - k| / (\sigma + 0.1)\right)^2} \tag{1} \]

with **Reduce Halos** ticked, and \(1 - w_u\) without it. The pixel's standing is the weighted share of the
neighbors at or below its level, the pixel itself included:

\[ C = \frac{\sum_{k_u \le k} w_u}{\sum_{u} w_u} \tag{2} \]

The map of \(C\) is stretched over the image, smoothed, and turned back into a lightness. That lightness, as a
fraction \(F\), lies within 0.04 of the smoothed map. With \(s\) the value of **Strength (%)** divided by 100, the
soft-light blend of \(F\) over a sample \(x\), given as a fraction of full scale, gives

\[ y = \min\bigl(1,\ x\,(1 - s + 2sF)\bigr) \tag{3} \]

A pixel at the top of its neighborhood is multiplied by up to \(1 + s\), one at the bottom by down to \(1 - s\), and
the three channels of a pixel by the same factor. With \(a\) the value of **Amount** and \(m\) the mask's weight at
the pixel, 1 where there is no mask, the sample written is

\[ z = x + a\,m\,(y - x) \tag{4} \]

Equation (3) clips before equation (4) mixes, so **Amount** and the mask cannot bring back a clipped highlight.

### Boundaries

- A sample that is not a finite number stops the preview and the run with "the image holds a sample that is not a
  finite number, which G'MIC cannot process". Nothing is written.
- At a **Strength (%)** of 0 the run is refused: G'MIC returns two images, and the reason says so.
- Beyond the frame the neighborhood repeats the image's edge pixels. A
  [preview area](../../reference/glossary.md#preview-area) is a frame of its own, with its own darkest and brightest
  pixels.
- **Cancel**, or closing the window, asks G'MIC to stop. It stops at its next check, and a stopped run writes nothing.
- When G'MIC itself fails, on memory for example, the run reports the library's reason and writes nothing. Try again
  on a smaller image or with fewer images open.

### Worked example

The image in the picture above is the linear [master](../../reference/glossary.md#master) after Histogram
Transformation with **Shadows** and **Midtones** at 0.02, which leaves the background near 0.28. The window keeps its
defaults: **Strength (%)** at 75, **Radius** at 4, **Sigma** at 100, **Regularization** at 8, **Reduce Halos** ticked,
**Amount** at 1 and **Channel** at **RGB**. **Preview area** is unticked.

Each pixel is ranked in a square of 9 by 9 pixels. A neighbor 100 levels away from it weighs \(e^{-1} = 0.37\) by
equation (1). A neighborhood whose weights add to 10, with 7 of them at or below the pixel's level, gives
\(C = 0.7\) by equation (2). By equation (3), a pixel where the smoothed map stands at 1 is multiplied by 1.75, and one
where it stands at 0 by 0.25. In the smooth background the ranks of the grain even out to about one half, which gives
\(F\) near 0.46 and a factor near 0.94: the background drops from 0.28 to about 0.26. Stars and the bright edges of
the nebula rank high and are brightened, and the pixels just around them rank low and are darkened.

```figure
images:
  - image: ../../assets/images/processes/sharpening/local-histogram-equalization-before.webp
    alt: >-
      A crop at 100 % of the middle of the stretched master, pink clouds of nebula beside a dark dust lane,
      and many stars on a grainy background.
    label: Before
  - image: ../../assets/images/processes/sharpening/local-histogram-equalization-after.webp
    alt: >-
      The same crop after Apply, the edges of the nebula and the stars brighter against darker surroundings, the
      background a little darker and the colors kept.
    label: After
caption: >-
  The middle of the stretched Carina Nebula master at 100 %, before and after Apply with the window's defaults. Both
  are shown in the Linear display mode, without a display stretch.
```

## Starting points

| Situation | Start | Then adjust |
| --- | --- | --- |
| A stretched nebula, first try | The defaults, with **Preview area** on | **Radius** first, then **Sigma** with **Reduce Halos** ticked, then **Regularization**. **Strength (%)** before **Amount**. |
| A mottled background | **Regularization** higher | Compare with 0 only on a duplicate. Then **Strength (%)** lower. |
| Halos around bright stars | **Strength (%)** or **Radius** lower | A mask that protects the stars. |
| A color component in mind | **RGB** first | Choose the component before the other values, since every value acts on it. |

## When it goes wrong

!!! warning "The image is recorded as nonlinear"
    Apply records the result as nonlinear, and a later process that needs a
    [linear image](../../reference/glossary.md#linear-image) treats it so. Finish every linear step first, or undo
    the step in the image's history.

!!! warning "Amount and the mask cannot undo clipping"
    Bright pixels multiplied by more than 1 are clipped before **Amount** and the mask mix the result with the
    original. Lower **Strength (%)** when star cores turn flat white.

The smooth background turns mottled or grainy
:   **Cause.** Every small difference between neighbors, noise included, is ranked and enlarged.
:   **Fix.** Reduce the noise first, raise **Regularization**, or lower **Strength (%)** or **Radius**.

Halos around stars and edges with Reduce Halos ticked
:   **Cause.** The pixels around a bright star rank low in a square that holds the star, and are darkened.
:   **Fix.** Lower **Strength (%)** or **Radius**, check the whole image, or protect the stars with a mask.

The result changes a lot, or Sigma works the other way, after unticking Reduce Halos
:   **Cause.** The weights are turned round: a neighbor of the same lightness counts nothing.
:   **Fix.** Tick **Reduce Halos** again, unless you mean that ranking.

The preview area looks different from the result of Apply
:   **Cause.** The area is equalized alone, between its own darkest and brightest pixels.
:   **Fix.** Untick **Preview area** for the last check before **Apply**.

The preview fails at a Strength of 0, and the reason says the command left 2 images
:   **Cause.** At 0 G'MIC skips the blend and returns both images.
:   **Fix.** Set **Strength (%)** above 0, and fade the result with **Amount** instead.

Colors shift after the equalization
:   **Cause.** A bright channel was clipped while the others were multiplied, or a component other than **RGB** was
    equalized.
:   **Fix.** Return **Channel** to **RGB**, and lower **Strength (%)** or **Amount**.

"the image holds a sample that is not a finite number, which G'MIC cannot process"
:   **Cause.** The image holds a sample that is not a number or is infinite, and the image was left unchanged.
:   **Fix.** Repair or crop away those samples, then apply again.

Preview and Apply are disabled, and the tooltip names G'MIC
:   **Cause.** This build of AfterNight was made without G'MIC.
:   **Fix.** Use a build that includes G'MIC, or use [CLAHE](clahe.md), which does not need it.

## Related

- [CLAHE](clahe.md), which equalizes tiles of the image with a clip limit instead of every pixel's neighborhood.
- [Local Contrast Enhancement](local-contrast-enhancement.md), which raises contrast by the difference from a wide
  blur.
- [Wavelet Denoise](../noise-reduction/wavelet-denoise.md), to reduce the noise before.
- [Masks](../../interface/masks.md), to protect the background or the stars.
- [Image History](../../interface/image-history.md), to compare with the step before or undo it.
- [Process Windows](../../interface/process-windows.md), for the preview, the histogram and what every process window
  shares.

## Credits

- [G'MIC](https://gmic.eu) 3.5.4, by David Tschumperlé and the G'MIC team, under CeCILL-2.1 or CeCILL-C at the
  user's choice, computes the equalization with its `fx_equalize_local_histograms` filter, by David Tschumperlé.
  AfterNight bundles it.
- [David Tschumperlé's note of 2018](https://discuss.pixls.us/t/feature-request-contrast-limited-adaptive-histogram-equalization-clahe/6573/5)
  explains that this filter is not CLAHE, and lists its controls.
- The G'MIC [`blend` reference](https://gmic.eu/reference/blend) describes the soft-light mode and its opacity.
- The G'MIC [`bilateral` reference](https://gmic.eu/reference/bilateral.html) describes the smoothing
  **Regularization** sets.
- The G'MIC [`apply_channels` reference](https://gmic.eu/reference/apply_channels.html) describes how the filter
  reaches the lightness of the image.
