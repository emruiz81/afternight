---
kind: process
stage: noise
menu: Process > Filters > Bilateral Filter
summary: >-
  Smooths the image with a weighted mean of a small disc of neighbours, each weighed by its distance and by its
  difference in value, so that grain is averaged away while stars and strong edges stay.
data_in: linear
data_out: linear
destructive: true
preview: real-time
sequences: true
masks: post-blend
external: none
glance:
  enabled: >-
    An image or a sequence view is active, no Bilateral Filter window is open on it yet, and no window that needs the
    image for itself is open on it.
  inputs: >-
    One grayscale or RGB image or sequence of any pixel type, with finite samples, and an optional mask of the image's
    size on its view.
  outputs: >-
    The same view, replaced, with one history step. Its size, channels, pixel type, header and recorded processing
    state are kept. No file is written.
  state: >-
    A calibrated and stacked linear master, after gradient removal and color calibration, with its hot pixels already
    removed.
derived: false
---
# Bilateral Filter

Bilateral Filter replaces each pixel with a weighted mean of the pixels in a small disc around it. A neighbour's
weight falls with its distance, as **Sigma Space** sets, and with how far its value lies from the centre's, as
**Sigma Color** sets. Grain, which differs little from its neighbours, is averaged away. A star or the edge of a dust
lane, which differs a lot, keeps its own value. AfterNight's engine computes the filter in one pass.

**Apply** replaces the image or [sequence](../../reference/glossary.md#sequence) with the filtered one, keeps its size
and [pixel type](../../reference/glossary.md#pixel-type), and adds one step to its
[history](../../reference/glossary.md#history).

```figure
image: ../../assets/images/processes/noise-reduction/bilateral-filter.webp
alt: >-
  The Bilateral Filter window. On the left, the Diameter, Sigma Space and Sigma Color boxes with their sliders, the
  Link Sigma controls check box and the Amount box with its slider, and lower down Target Image, Channel at RGB and the
  Preview check box. On the right, a toolbar of zoom buttons, a channel box and a display mode box reading Autostretch
  over the preview of a red nebula, with the word PREVIEW in its corner and a small framed area in its middle. Reset,
  Close and Apply run along the bottom.
caption: >-
  Bilateral Filter on the linear master with Sigma Color at 0.01 and the other values at their defaults. The framed
  area shows what Apply writes there, over the original, in the Autostretch display mode.
pins:
  - {n: 2, x: 34.8, y: 85.4}
  - {n: 3, x: 72.5, y: 54.6}
  - {n: 4, x: 34.8, y: 24.7}
  - {n: 5, x: 34.8, y: 15.7}
  - {n: 6, x: 5.0, y: 39.2}
  - {n: 8, x: 89.2, y: 96.0}
```

## Before you start

- Filter the linear [master](../../reference/glossary.md#master), after calibration, stacking, gradient removal and
  color calibration. **Sigma Color** is set from the noise of the image, and a
  [stretch](../../reference/glossary.md#stretch) changes that noise.
- Remove [hot pixels](../../reference/glossary.md#hot-pixel) first, in calibration or with Cosmetic Correction. A hot
  pixel differs from all its neighbours, so the filter keeps it as it keeps a star.
- **Apply** replaces the image. Duplicate it first, or use its history, which keeps the step before.
- Replace any sample that is not a finite number: the filter spreads it into its neighbours.
- [Debayer](../../reference/glossary.md#debayer) a [CFA](../../reference/glossary.md#cfa) mosaic first. A raw mosaic
  is filtered as one grayscale plane. The frames of a sequence are read with the automatic debayer setting of its view.
- A [mask](../../reference/glossary.md#mask) on the view blends the result with the original after **Amount**. On a
  sequence, every frame gets the same values, **Channel** and mask.

## Steps

1. **Open the window.** Make the master the [active image](../../reference/glossary.md#active-image) and choose
   **Process > Filters > Bilateral Filter**. The preview shows the filtered image inside a framed
   [preview area](../../reference/glossary.md#preview-area) in the middle of the image.
2. **Choose what to smooth.** Leave **Channel** at **RGB** to filter the three channels, each on its own. Choose
   **CIE L* (Lightness)** to smooth the brightness and leave the colors as they are.
3. **Place the area.** Move the frame over smooth background, faint nebula, a few stars and the edge of a dust lane,
   and zoom the preview to 100 %.
4. **Lower Sigma Color.** Type a few times the grain of the background, or 0.01, the smallest value the box takes, on
   a [linear image](../../reference/glossary.md#linear-image), whose grain is often below it. The grain fades while the
   stars and the edges keep their values.
5. **Set the reach.** Keep **Diameter** at 5 for grain of a pixel or two, and raise it to 7 or 9 for coarser blotches.
   Leave **Sigma Space** high, so that every pixel of the disc counts alike.
6. **Set the strength.** Lower **Amount** to bring back part of the original when the result looks flat or waxy.
7. **Check at 100 %.** Hold ++space++ over the preview to compare with the original. Then untick **Preview area** and
   look at the whole image, its stars and its edges.
8. **Apply.** Choose **Apply**. A progress window follows the run, the view shows the filtered image, and its history
   lists **Bilateral Filter** with the values.

## Controls

**Target Image**, the preview pane, **Close** and the progress window work as in every process window: see
[Process Windows](../../interface/process-windows.md). What is particular to this window is below.

```controls
window: Bilateral Filter
controls:
  - name: Diameter
    kind: spinbox
    default: "5.00"
    widget: {min: 0, max: 20, step: 0.5, decimals: 2}
    effect: Sets the disc of neighbours each pixel is averaged with. The value is cut to a whole number, and the disc's radius is half of it, rounded down, so 5 gives a radius of 2 and a disc of 13 pixels. Below 1, the radius follows **Sigma Space** instead.
    range: The radius is at least 1, so 1, 2 and 3 all give a disc 3 pixels across, and 4, 5 and 5.50 one 5 pixels across. Below 1, the radius is 1.5 times **Sigma Space**, rounded, and at most 32. The slider moves the value in 1,000 equal steps of 0.02.
    enabled: Disabled while Apply runs.
    tradeoff: The work grows with the pixels of the disc, 13 at 5, 81 at 10 or 11 and 317 at 20, and up to 3,209 when the radius follows **Sigma Space**. The weights depend on the centre's value, so the filter cannot be split into two cheaper passes along the rows and the columns.
    scope: The preview and Apply, on what **Channel** selects, before **Amount** and the mask.
    persistence: Until the window closes. **Reset** and a finished Apply return it to its default.
  - name: Sigma Space
    kind: spinbox
    default: "30.00"
    widget: {min: 0, max: 200, step: 0.5, decimals: 2}
    effect: Sets how fast a neighbour's weight falls with its distance, in pixels. Higher lets the far pixels of the disc count as much as the near ones. Lower favours the nearest. At 0 the image is left as it is.
    range: A value much larger than the disc's radius gives every pixel of the disc almost the same weight. At the default, the farthest pixel of a disc of radius 2 keeps 0.998 of it. With **Diameter** below 1 it also sets the disc, whose radius reaches its limit of 32 at 21, so the default, which would ask for 45, gets 32. The slider moves the value in 1,000 equal steps of 0.2.
    enabled: Disabled while Apply runs. With **Link Sigma controls** ticked, a change sets **Sigma Color** to the same number.
    tradeoff: With **Diameter** below 1, a large value makes a wide disc and a slow preview. Above 21 it changes only the weights inside that disc.
    scope: The preview and Apply, on what **Channel** selects, before **Amount** and the mask.
    persistence: Until the window closes. **Reset** and a finished Apply return it to its default.
  - name: Sigma Color
    kind: spinbox
    default: "30.00"
    widget: {min: 0, max: 200, step: 0.5, decimals: 2}
    effect: Sets how fast a neighbour's weight falls with its difference in value from the centre, as a fraction of full scale. Lower keeps more edges. A neighbour that differs by three times **Sigma Color** keeps about 1 % of its weight. Higher lets different values mix, and blurs stars and edges. At 0 the image is left as it is.
    range: The box keeps two decimals, so its smallest value above 0 is 0.01. On an integer image the value is restated in the image's own codes, so 0.01 of a 16-bit image is 655.35 codes. The slider moves the value in 1,000 equal steps of 0.2.
    enabled: Disabled while Apply runs. With **Link Sigma controls** ticked, a change sets **Sigma Space** to the same number.
    tradeoff: The default is thirty times full scale. Even a jump from black to white then keeps 0.9994 of its weight, and the disc is averaged as a plain mean that blurs every edge. Too low a value leaves the grain, or smooths some of it and keeps the rest as flat patches with steps between them. The grain of a linear master is often well below 0.01, so even 0.01 also smooths faint texture. Lower **Amount** to keep some of it.
    scope: The preview and Apply, before **Amount** and the mask. Each channel or component is judged by its own differences.
    persistence: Until the window closes. **Reset** and a finished Apply return it to its default.
  - name: Link Sigma controls
    kind: check
    default: "Off"
    effect: Ticking it sets **Sigma Color** to the value of **Sigma Space**. While it is ticked, changing either sigma changes the other to the same number.
    enabled: Disabled while Apply runs.
    tradeoff: The two sigmas are on different scales, pixels and fractions of full scale. A **Sigma Space** of a few pixels is a **Sigma Color** that blurs every edge. A **Sigma Color** of 0.01 to 0.05, useful on a linear image, leaves the neighbours one pixel away no weight, and the image stays as it was.
    scope: The two sigmas of the preview and Apply.
    persistence: Until the window closes. **Reset** and a finished Apply untick it. The history does not record it.
  - name: Amount
    kind: spinbox
    default: "1.00"
    widget: {min: 0, max: 1, step: 0.05, decimals: 2}
    effect: Mixes the filtered image with the original. At 1 the result is the filtered image, at 0.5 halfway, and at 0 the original.
    range: The box keeps two decimals, also when you drag the slider.
    enabled: Disabled while Apply runs. **Apply** stays enabled at 0.
    tradeoff: It brings back grain and detail together. The filter runs in full at any value.
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
    effect: Chooses what is filtered. **RGB** filters each of the three channels on its own, each weighed by its own differences, not by a distance between colors. **Red**, **Green** or **Blue** filters that channel and keeps the other two. Every other choice filters one component of another color model and converts the image back.
    enabled: For an RGB image. A grayscale image shows a disabled **Grayscale**. Disabled while Apply runs.
    tradeoff: With **RGB**, an edge strong in one channel and weak in another is kept in the first and smoothed in the second, which can shift colors at faint colored edges. A component costs two conversions and a working copy, and the conversion back can change colors.
    scope: The preview and Apply, before **Amount** and the mask. **Sigma Color** is read in the units of the component, which Process Windows gives.
    persistence: Until the window closes. **Reset** leaves it as it is. A finished Apply and a new target return it to **RGB**.
  - name: Preview
    kind: check
    default: "On"
    effect: Shows the filtered image beside the controls, with **Amount** and the mask applied.
    range: >-
      The pane opens with a **Preview area** of 800 by 800 pixels in the middle of the image, labelled "Preview:
      Bilateral Filter", or the whole image when it is smaller. At the window's default size, **Preview area** and
      **Take snapshot** are in the menu at the end of the pane's toolbar.
    tradeoff: Every change filters the area, or the whole image, again, and a large disc makes each preview slow. The area is filtered as an image of its own, mirrored at its own edges. While you drag a slider with no area drawn, the preview filters the part of the image the pane shows, at full scale, exactly as Apply writes it there.
    scope: The window only. On a sequence it shows the frame the view displays. **Display Mode** and **Autostretch Settings** change only how the preview is shown.
    persistence: Until the window closes. **Reset** leaves it as it is.
  - name: Reset
    kind: button
    default: ""
    effect: Returns **Diameter**, **Sigma Space**, **Sigma Color** and **Amount** to their defaults and unticks **Link Sigma controls**, and one preview of them follows. It leaves **Channel** as it is.
    enabled: Disabled while Apply runs.
  - name: Apply
    kind: button
    default: ""
    effect: Filters the whole image, or every frame of the sequence, with the values and **Channel** as they are when you press it, and then returns the values to their defaults.
    enabled: From the moment the window opens, at any values, a sigma of 0 included.
    tradeoff: A single image shows a progress window too, and its **Cancel** is noticed between two rows. The filter takes a single-precision copy of one channel, bordered by the disc's radius and used for each channel in turn, beside the second copy every run needs. It works on the threads the run is granted.
    persistence: "The history step is **Bilateral Filter** and reads, for example, `Diameter: 5.00, Sigma Space: 30, Sigma Color: 0.01, Amount: 1.00 in channel: RGB`. For a grayscale image it ends after the amount. **Link Sigma controls** and the mask are not recorded. The header cards, the plate solution and the recorded processing state are kept."
```

## How it works

1. Each channel that **Channel** selects is filtered on its own, in single precision with sums in double precision.
   **Sigma Color** is a [fraction of full scale](../../reference/glossary.md#fraction-of-full-scale), restated in the
   image's own codes for an integer image, so a 16-bit image is filtered as the same picture in floating point is.
2. For each pixel, every neighbour inside the disc gets a weight: one factor falls with its distance, the other with
   its difference in value from the centre. The pixel becomes the weighted mean of the disc, its own value included.
3. Beyond the edge of the image, the filter reads the image mirrored without its edge sample.
4. The result is written back in the image's own pixel type, rounded to the nearest code for an integer image.
   **Amount** mixes it with the original, and a mask on the view then blends the two.
5. The image replaces the target and its history gets one step. Its header, its
   [plate solution](../../reference/glossary.md#plate-solution) and its recorded
   [processing state](../../reference/glossary.md#processing-state) are kept.

### Model

With \(p\) a pixel, \(q\) a neighbour in the disc \(\Omega_p\) around it, \(I\) the samples of one channel,
\(\sigma_s\) the value of **Sigma Space** and \(\sigma_c\) the value of **Sigma Color**, each neighbour weighs

\[ w(p,q) = \exp\Bigl(-\frac{\lVert p - q \rVert^2}{2\sigma_s^2}\Bigr)\,\exp\Bigl(-\frac{\bigl(I(q) - I(p)\bigr)^2}{2\sigma_c^2}\Bigr) \tag{1} \]

and the filtered sample is

\[ F(p) = \frac{\sum_{q \in \Omega_p} w(p,q)\,I(q)}{\sum_{q \in \Omega_p} w(p,q)} \tag{2} \]

With \(d\) the value of **Diameter** cut to a whole number, the disc holds every pixel no farther than \(r\) from the
centre, where

\[ r = \begin{cases} \max\bigl(1, \lfloor d/2 \rfloor\bigr) & d \ge 1 \\ \min\bigl(32, \max(1, \operatorname{round}(1.5\,\sigma_s))\bigr) & d = 0 \end{cases} \tag{3} \]

With \(a\) the value of **Amount**, the result before the mask is

\[ B = (1 - a)\,I + a\,F \tag{4} \]

### Boundaries

- At a **Sigma Space** or **Sigma Color** of 0 the image is left as it is, whatever the other values. At an
  **Amount** of 0 the result is the original, and the filter still runs in full.
- A sample that is not a finite number is not repaired. In an image that marks every sample valid, it spreads into
  every pixel whose disc holds it. A pixel the image marks as invalid is left as it was, and enters its neighbours'
  discs as 0.
- Every pixel type is filtered in single precision. A 64-bit floating-point image is narrowed to it, and an integer
  result is rounded to the nearest code.
- **Cancel**, or closing the window, stops the run between two rows, and nothing is written.

### Worked example

The image is the linear master. Its channels have their median near 0.028, and their grain from one pixel to the next
is about 0.0003 in red, 0.001 in green and 0.0007 in blue. **Sigma Color** is 0.01, the smallest value the box takes,
and the other values keep their defaults. By equation (3), **Diameter** at 5 gives a disc of radius 2, which holds 13
pixels, and at a **Sigma Space** of 30 the farthest of them keeps 0.998 of its weight.

By equation (1), a green neighbour two grain deviations from the centre keeps 0.98 of its weight. The 13 pixels are
averaged almost equally, so for grain that changes from one pixel to the next the deviation falls to about a quarter:
0.0003 in green, 0.0002 in blue and 0.0001 in red. The faint texture of the nebula goes with it: a step of 0.01 keeps
0.61 of its weight and softens. A step of 0.03 across the edge of a dust lane keeps 0.01 and stays sharp. A star whose
core stands 0.27 above the background keeps its value, since the background around it gets a weight below
\(10^{-150}\). At the default **Sigma Color** of 30, every weight of equation (1) is above 0.999, and the star would be
spread over the disc. With **Amount** at 0.50, a sample of 0.0300 that the filter brings to 0.0280 is written as
0.0290 by equation (4). For the pair, a crop of the middle of the master is filtered with these settings, and the crop
and its result are then both stretched by Histogram Transformation with **Shadows** and **Midtones** at 0.02 and
shown in the Linear display mode.

```figure
images:
  - image: ../../assets/images/processes/noise-reduction/noise-crop-before.webp
    alt: >-
      A crop at 100 % of the middle of the master, stretched: pink clouds of nebula beside a dark dust lane, many small
      stars, and a fine colored grain over the background.
    label: Before
  - image: ../../assets/images/processes/noise-reduction/bilateral-filter-after.webp
    alt: >-
      The same crop filtered before the same stretch, the grain of the background and of the faint nebula smoothed and
      the faintest texture of the nebula flattened, the stars and the edges of the dust lanes as sharp as before.
    label: After
caption: >-
  The middle of the Carina Nebula master at 100 %, without and with Bilateral Filter at Sigma Color 0.01 and the other
  values at their defaults, both then stretched by Histogram Transformation with Shadows and Midtones at 0.02.
```

## Starting points

| Situation | Start | Then adjust |
| --- | --- | --- |
| A linear master | **Sigma Color** at 0.01, the smallest value the box takes, the rest at their defaults | **Amount** down when faint texture turns flat, **Sigma Color** up by 0.01 while grain remains. |
| A stretched image | **Sigma Color** at three to four times the grain of the background after the stretch | Check star halos and faint gradients at 100 %. |
| Coarser blotches | **Diameter** at 7 or 9 | Check small stars, and expect each preview to take longer. |
| Stars or edges that must not change | A mask over them, or a lower **Amount** | Check the mask's edges at 100 %. |

## When it goes wrong

!!! warning "The default Sigma Color blurs every edge"
    At 30, **Sigma Color** is thirty times full scale, and every neighbour in the disc counts almost fully. The filter
    then acts as a plain mean, and stars and edges blur. Type a few times the grain of the image, or 0.01 on a linear
    one.

!!! warning "Link Sigma controls ties two different scales"
    Ticking it copies **Sigma Space**, in pixels, into **Sigma Color**, a fraction of full scale. The default 30 then
    blurs every edge, and a value useful on a linear image leaves the neighbours no weight. Leave it off.

Stars and edges blur almost as with a plain blur
:   **Cause.** **Sigma Color** is far above the differences in the image, as it is at its default.
:   **Fix.** Lower **Sigma Color** to 0.01 on a linear image, or to a few times the grain on a stretched one.

The image does not change
:   **Cause.** A sigma or **Amount** is 0, or **Link Sigma controls** copied a small **Sigma Color** into **Sigma Space**.
:   **Fix.** Untick **Link Sigma controls**, set **Sigma Space** back to 30, and keep both sigmas and **Amount** above 0.

Faint stars turn flat, or the background shows smooth patches with steps between them
:   **Cause.** **Sigma Color** is close to the contrast of the grain and of faint stars, so some of them are smoothed
    and some kept.
:   **Fix.** Change **Sigma Color** by 0.01 and compare at 100 %, or lower **Amount** or **Diameter**.

Changing **Diameter** from 5 to 5.50, or to 4, changes nothing
:   **Cause.** **Diameter** is cut to a whole number and halved, so all three give a radius of 2.
:   **Fix.** Use the odd whole values 3, 5, 7 and up.

The preview is slow
:   **Cause.** The disc is large: **Diameter** is high, or below 1, where the disc follows **Sigma Space** up to a
    radius of 32.
:   **Fix.** Set **Diameter** to an odd value from 3 to 9, and tune in the preview area.

Colors shift at faint colored edges
:   **Cause.** With **RGB**, each channel is judged by its own differences, so an edge can be kept in one channel and
    smoothed in another.
:   **Fix.** Choose **CIE L* (Lightness)** in **Channel**, and compare it with **RGB** on a duplicate.

The preview area differs from the result of Apply
:   **Cause.** The area is filtered alone, mirrored at its own edges.
:   **Fix.** Untick **Preview area** for the last check before **Apply**, and compare under one display mode.

## Related

- [Median Filter](median-filter.md) and [Cosmetic Correction](../calibration/cosmetic-correction.md), which remove the
  hot pixels and other impulses this filter keeps.
- [Gaussian Blur](gaussian-blur.md), the same kind of mean without the weight on differences in value.
- [Total Variation Denoise](total-variation-denoise.md) and [Wavelet Denoise](wavelet-denoise.md), other ways to
  reduce noise while keeping edges.
- [Image History](../../interface/image-history.md), to undo the step.
- [Masks](../../interface/masks.md), to filter the background and protect the stars.
- [Process Windows](../../interface/process-windows.md), for the preview, the preview area and what every process
  window shares.

## Credits

- Carlo Tomasi and Roberto Manduchi,
  [Bilateral Filtering for Gray and Color Images](https://www.cs.jhu.edu/~misha/ReadingSeminar/Papers/Tomasi98.pdf),
  ICCV 1998, for the spatial and range weights. AfterNight filters each channel on its own rather than with a distance
  between colors.
- The [OpenCV bilateralFilter documentation](https://docs.opencv.org/master/d4/d86/group__imgproc__filter.html), for
  the circular window, the rule that derives its radius from the diameter or the spatial sigma, and the border, which
  the filter reproduces. Its range weight is computed for each neighbour rather than read from a table.
- The [OpenCV border types](https://docs.opencv.org/master/d2/de8/group__core__array.html), for the reflection that
  leaves out the edge sample.
