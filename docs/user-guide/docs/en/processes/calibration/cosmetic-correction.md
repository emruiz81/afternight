---
kind: process
stage: calibrate
menu: Process > Filters > Cosmetic Correction Filter
summary: >-
  Replaces the hot and cold pixels of an image with the median of the pixels around them and leaves every other sample
  as it was, to repair the defects calibration leaves, or those of a frame without darks.
data_in: linear
data_out: linear
destructive: true
preview: real-time
sequences: true
masks: post-blend
external: none
glance:
  enabled: >-
    An image or a sequence view is active, no Cosmetic Correction window is open on it yet, and no window that needs
    the image to itself is open on it.
  inputs: >-
    One grayscale or RGB image or sequence of any pixel type, a raw color-filter mosaic included, and an optional mask
    of the image's size on its view. No dark frame is needed.
  outputs: >-
    The same view, replaced, with one history step. Its size, channels, pixel type, header and recorded processing
    state are kept. No defect map, count, report or file is written.
  state: >-
    A raw or calibrated light, or a master, with isolated hot and cold pixels, before the debayer and registration. A
    stretched image is accepted too.
derived: true
rewritten: true
---
# Cosmetic Correction

Cosmetic Correction repairs the [hot pixels](../../reference/glossary.md#hot-pixel) and
[cold pixels](../../reference/glossary.md#cold-pixel) of an image from the image itself. It compares each sample with
the median of the square window around it. A sample that stands too far above or below that median is replaced by it,
and every other sample stays exactly as it was. **Amount** mixes the result with the original. It is made for defects
of one or a few pixels, not for dust shadows, amplifier glow or trails.

**Apply** replaces the image or [sequence](../../reference/glossary.md#sequence) with the corrected one, keeps its size
and [pixel type](../../reference/glossary.md#pixel-type), and adds one step to its
[history](../../reference/glossary.md#history).

```figure
owed: true
image: ../../assets/images/processes/calibration/cosmetic-correction.webp
alt: >-
  The Cosmetic Correction window. On the left, the Hot Pixels and Cold Pixels check boxes, each over a Threshold box at
  0.50 with its slider, then General Settings with Kernel Size at 3x3 and Amount at 1.00 with its slider, and lower
  down Target Image, Channel reading Grayscale and the Preview check box. On the right, a toolbar of zoom buttons, a
  channel box and a display mode box reading Autostretch over the preview of a gray frame with a galaxy across its
  middle, the word PREVIEW in its corner and a small framed area in its middle. Reset, Close and Apply run along the
  bottom.
caption: >-
  Cosmetic Correction at its defaults on a raw light of NGC 253, in the Autostretch display mode: the framed area in
  the middle shows what Apply writes there.
pins:
  - {n: 2, x: 2.4, y: 15.0}
  - {n: 3, x: 72.8, y: 54.6}
  - {n: 4, x: 2.4, y: 20.6}
  - {n: 5, x: 2.4, y: 44.8}
  - {n: 6, x: 2.4, y: 49.3}
  - {n: 8, x: 90.0, y: 96.0}
```

## Before you start

- Where you have a [master](../../reference/glossary.md#master) [dark frame](../../reference/glossary.md#dark-frame),
  remove the defects in calibration: the cosmetic correction of [Image Calibration](image-calibration.md) finds them in
  the master dark, and repairs a mosaic from pixels of the same color. Use this window for what calibration leaves, or
  on lights without darks.
- Correct a raw [CFA](../../reference/glossary.md#cfa) mosaic before you [debayer](../../reference/glossary.md#debayer)
  it: a defect is one sample there, and the debayer spreads it into its neighbors. The window's median mixes the
  colors of the filter.
- Open a raw frame as a mosaic: answer **No** when AfterNight asks "Do you want to automatically debayer it before
  opening?". The frames of a sequence are read with the automatic debayer setting of its view.
- Look at the image at 100 % first. Small stars, cosmic rays and fine detail of high contrast can look like hot
  pixels.
- **Apply** replaces the image. Duplicate it first, or use its history, which keeps the step before.
- A [mask](../../reference/glossary.md#mask) on the view blends the result with the original afterwards. It does not
  change which samples are found, so turn it off while you set the thresholds, and on again for **Apply**.

## Steps

1. **Open the window.** Make the frame the [active image](../../reference/glossary.md#active-image) and choose
   **Process > Filters > Cosmetic Correction Filter**. The preview shows the corrected image inside a framed
   [preview area](../../reference/glossary.md#preview-area) in the middle of the image.
2. **Choose what to replace.** Keep **Hot Pixels** and **Cold Pixels** ticked, or untick the kind your frame does not
   need. With both unticked, **Apply** is disabled.
3. **Place the area.** Move the frame over hot pixels and a few stars, zoom the preview to 100 %, and hold ++space++
   over it to compare with the original.
4. **Set the thresholds.** When defects stay, lower the **Threshold** of **Hot Pixels** in steps of 0.05 until they
   are gone. When the cores of stars change, raise it. Set the **Threshold** of **Cold Pixels** the same way.
5. **Widen the window only for clusters.** Keep **Kernel Size** at **3x3**. Choose **5x5** when defects of several
   touching pixels stay, and check the stars again.
6. **Soften the result if needed.** Lower **Amount** when a full replacement looks abrupt.
7. **Check the whole image.** Untick **Preview area** and look at the whole frame and its edges.
8. **Apply.** Choose **Apply**. A progress window follows the run, the view shows the corrected image, and its history
   lists **Cosmetic Correction** with the values.

## Controls

**Target Image**, the preview pane, **Close** and the progress window work as in every process window: see
[Process Windows](../../interface/process-windows.md). What is particular to this window is below.

```controls
window: Cosmetic Correction
controls:
  - name: Hot Pixels
    kind: check
    default: "On"
    effect: Ticked, replaces every sample that stands above the median of its window by more than its **Threshold**. Unticked, no sample is replaced for standing high.
    enabled: It stays enabled while Apply runs, and a change then does not reach that run.
    tradeoff: Bright defects go, and so does any single pixel that stands out as far, such as the peak of a small star or one color's site in a star of a raw mosaic.
    scope: The preview and Apply, on what **Channel** selects, before **Amount** and the mask.
    persistence: Until the window closes. **Reset** and a finished Apply leave it as it is. A new window opens with it ticked.
  - name: Threshold
    object: hotPixelsThresholdValue
    kind: spinbox
    default: "0.50"
    widget: {min: 0, max: 1, step: 0.05, decimals: 2}
    effect: Sets how far above the median of its window a sample must stand, as a share of the sample itself, to be replaced. Lower replaces more samples, higher fewer. At 0.50 a sample is replaced when it is more than twice its median.
    range: At 1.00 nothing is replaced for standing high, as with **Hot Pixels** unticked. A sample of exactly 0 is never replaced as a hot pixel.
    enabled: Unticking **Hot Pixels** disables it, although **Reset** and the end of a run enable it again. Its value is used only while **Hot Pixels** is ticked. Disabled while Apply runs.
    tradeoff: Low values remove fainter defects, and then the grain and the cores of stars. High values leave faint defects.
    scope: The hot-pixel test only.
    persistence: Until the window closes. **Reset** and a finished Apply return it to its default.
  - name: Cold Pixels
    kind: check
    default: "On"
    effect: Ticked, replaces every sample that stands below the median of its window by more than its **Threshold**. Unticked, no sample is replaced for standing low.
    enabled: It stays enabled while Apply runs, and a change then does not reach that run.
    tradeoff: Dead pixels go, and so does dark fine structure that stands out as far.
    scope: The preview and Apply, on what **Channel** selects, before **Amount** and the mask.
    persistence: Until the window closes. **Reset** and a finished Apply leave it as it is. A new window opens with it ticked.
  - name: Threshold
    object: coldPixelsThresholdValue
    kind: spinbox
    default: "0.50"
    widget: {min: 0, max: 1, step: 0.05, decimals: 2}
    effect: Sets how far below the median of its window a sample must stand, as a share of the median, to be replaced. Lower replaces more samples, higher fewer. At 0.50 a sample is replaced when it is less than half its median.
    range: At 1.00 nothing is replaced for standing low. Where the median of a window is exactly 0, its sample is never replaced as a cold pixel.
    enabled: Unticking **Cold Pixels** disables it, although **Reset** and the end of a run enable it again. Its value is used only while **Cold Pixels** is ticked. Disabled while Apply runs.
    tradeoff: Low values replace dark grain and the dark detail of structure. High values leave faint dead pixels.
    scope: The cold-pixel test only.
    persistence: Until the window closes. **Reset** and a finished Apply return it to its default.
  - name: Kernel Size
    kind: combo
    default: 3x3
    options: [3x3, 5x5, 7x7, 9x9, 11x11, 13x13, 15x15]
    effect: Chooses the square window whose median each sample is compared with, and which replaces it. A larger window finds a cluster of touching defects, which the median of a 3x3 window follows.
    range: The sizes are odd, so every window has a middle sample. At the edges of the image the window is cut to the samples inside it, not wrapped or mirrored, and a cut window of an even count takes the mean of its two middle samples. On an image smaller than the window, every window is cut, and the tests find little.
    enabled: Disabled while Apply runs.
    tradeoff: A larger window costs more, takes its replacement from farther away, from structure unrelated to the pixel, and compares star cores with the darker sky around them, so it replaces more of them.
    scope: The median both tests use, on what **Channel** selects.
    persistence: Until the window closes. **Reset** and a finished Apply return it to **3x3**.
  - name: Amount
    kind: spinbox
    default: "1.00"
    widget: {min: 0, max: 1, step: 0.05, decimals: 2}
    effect: Mixes the corrected image with the original. At 1.00 a replaced sample takes the median, at 0.50 it goes halfway, and at 0 the image comes back as it was. A sample that neither test replaces keeps its value at any **Amount**.
    enabled: Disabled while Apply runs.
    tradeoff: Below 1 a replaced defect keeps part of its excess. It lowers the change, not the work.
    scope: The preview and Apply, after the tests and before the mask. It does not change which samples are found.
    persistence: Until the window closes. **Reset** and a finished Apply return it to its default.
  - name: Target Image
    kind: combo
    default: The active image
    options: [The active image, Every other open image or sequence the window can work on]
    effect: Chooses the image or sequence Apply replaces, with the mask on its view. **Channel** and the preview follow it.
    tradeoff: A larger image or a sequence takes more time, memory and disk.
    persistence: Until the window closes. **Reset** leaves it as it is.
  - name: Channel
    kind: combo
    default: RGB
    options: [RGB, Red, Green, Blue, CIE L* (Lightness), CIE a*, CIE b*, CIE c*, CIE h*, CIE X, CIE Y (Luminance), CIE Z, HSL Hue, HSL Saturation, HSL Lightness, HSV Hue, HSV Saturation, HSV Value, YCbCr Luma, YCbCr Blue Chroma, YCbCr Red Chroma]
    effect: Chooses what is corrected. **RGB** tests and corrects each of the three channels on its own, so a defect is replaced in the channels where it stands out. **Red**, **Green** or **Blue** corrects that channel and keeps the other two. Every other choice corrects one component of another color model and converts the image back.
    enabled: For an RGB image. A grayscale image, a raw mosaic included, shows a disabled **Grayscale**. Disabled while Apply runs.
    tradeoff: "**RGB** ranks three medians, one for each channel, and the three can replace different pixels. A component costs two conversions and a working copy, and the conversion back can change colors around a corrected pixel."
    scope: The preview and Apply, before **Amount** and the mask. Process Windows describes each component.
    persistence: Until the window closes. **Reset** leaves it as it is. A finished Apply and a new target return it to **RGB**.
  - name: Preview
    kind: check
    default: "On"
    effect: Shows the corrected image beside the controls, with the mask blended in.
    range: >-
      The pane opens with a **Preview area** of 600 by 600 pixels in the middle of the image, labelled "Preview:
      Cosmetic Correction Filter", or the whole image when it is smaller. At the window's default size, **Preview area**
      and **Take snapshot** are in the menu at the end of the pane's toolbar.
    tradeoff: Every change corrects the area, or the whole image, again, so turn it off while you try a large window on a large image. The area is corrected as an image of its own, with its own edges. While you drag a slider with no area drawn, the preview corrects the part of the image the pane shows, at full scale, exactly as Apply writes it there.
    scope: The window only. On a sequence it shows the frame the view displays. **Display Mode** and **Autostretch Settings** change only how the preview is shown.
    persistence: Until the window closes. **Reset** leaves it as it is. **Apply** stays enabled with it off.
  - name: Reset
    kind: button
    default: ""
    effect: Returns both thresholds, **Kernel Size** and **Amount** to their defaults, and one preview of them follows. It leaves **Hot Pixels**, **Cold Pixels** and **Channel** as they are, although its tooltip says that it resets all parameters.
    enabled: Disabled while Apply runs.
  - name: Apply
    kind: button
    default: ""
    effect: Corrects the whole image, or every frame of the sequence, with the values and **Channel** as they are when you press it, and then returns both thresholds, **Kernel Size** and **Amount** to their defaults. **Hot Pixels** and **Cold Pixels** stay as they are.
    enabled: While **Hot Pixels** or **Cold Pixels** is ticked, from the moment the window opens.
    tradeoff: A single image shows a progress window too, and its **Cancel** is noticed between groups of rows. Its bar does not move inside one image. The run holds the medians of the whole image, in its pixel type, beside the second copy every run needs.
    persistence: "The history step is **Cosmetic Correction** and reads, for a grayscale image at the defaults, `Hot Pixels Threshold 0.5, Cold Pixels Threshold 0.5, Kernel size 3x3, Amount 1`. An unticked group leaves its part out, and an RGB image adds ` in channel: RGB`. The mask is not recorded. The header cards, the plate solution and the recorded processing state are kept."
    note: No map, count or report of the defects is written.
```

## How it works

1. Each channel that **Channel** selects is read into a single-precision plane, in the image's own units.
2. The median of the square window around every sample is ranked once, the sample itself included. For an integer
   image that marks no pixel invalid, the windows from 3x3 to 9x9 away from the edges are ranked by fixed selection
   networks, and every other window by a general selection. Both take the same sample.
3. Each original sample is tested against its own median, as a hot pixel and as a cold one. A sample that either test
   names takes the median, and every other sample is copied exactly.
4. **Amount** mixes the result with the original, and a mask on the view then blends the two.
5. The result is written back in the image's own pixel type and replaces the target, with one history step. Its
   header, its [plate solution](../../reference/glossary.md#plate-solution) and its recorded
   [processing state](../../reference/glossary.md#processing-state) are kept.

### Model

With \(v\) a sample, \(m\) the median of the window around it, and \(t_h\) and \(t_c\) the values of the two
**Threshold** boxes, a sample is replaced as a hot pixel when

\[ v > m \quad\text{and}\quad \frac{v - m}{|v|} > t_h \tag{1} \]

and as a cold pixel when

\[ v < m \quad\text{and}\quad \frac{m - v}{|m|} > t_c \tag{2} \]

For a positive sample and median, the two tests are limits on the sample:

\[ v > \frac{m}{1 - t_h}, \qquad v < (1 - t_c)\,m \tag{3} \]

At 0.50 a sample is hot above twice its median and cold below half of it. At 0.30 it is hot above 1.43 times its
median. With \(a\) the value of **Amount**, and \(F\) the median for a replaced sample and \(v\) for any other, the
result is

\[ O = (1 - a)\,v + a\,F \tag{4} \]

### Boundaries

- The window is cut at the edges of the image, so the border is tested against smaller, lopsided windows. A cut
  window of an even count takes the mean of its two middle samples, which then replaces a defect there.
- In a floating-point image, a sample that is not a finite number is left out of every window and left where it is,
  so it stays in the result. An image that marks its own invalid pixels is read under its own
  [validity rule](../../reference/glossary.md#validity-rule), and those pixels are left as they are.
- Every pixel type keeps its type. The medians are ranked in single precision from the stored values, so the median of
  a 32-bit integer or a 64-bit floating-point image is rounded to it.
- With both thresholds at 1.00 nothing is replaced, and **Apply** still adds a history step.
- **Cancel**, or closing the window, stops the run between groups of rows, and nothing is written.

### Worked example

The frame is one raw light of NGC 253 from a QHY183C color camera: 5544 by 3684 pixels, 16-bit, RGGB, 180 seconds
with the sensor at 21 °C, so it holds many hot pixels. It is opened as a mosaic, with **No** to the question, and no
dark frame is used. Its median is 3712 codes, 0.057 of full scale, the red and blue sites near 0.050 and the green
near 0.062. The window keeps its defaults: both thresholds at 0.50, **Kernel Size** at **3x3** and **Amount** at 1.00.

By equation (3), a background sample is hot above about 0.11 and cold below about 0.028. The pixel at x 2712, y 1649
reads 65504 codes, nearly full scale, and the median of its window is 4000. Equation (1) gives 0.94, above 0.50, so it
becomes 4000. Over the whole frame, **Apply** replaces about 24,400 samples as hot pixels, 0.12 % of the frame, and
about 60 as cold ones. A fifth of the hot ones read 65504, the frame's largest value. A cluster of five touching bright
pixels at x 2796, y 1827 loses its four outer pixels and keeps the middle one: five of the nine samples of its window
are bright, so its median is too. **5x5** removes it. Where one color's site in a bright star stands more than twice above its neighbors,
it is replaced as well. At the defaults about one bright star in six loses a pixel near its peak, with **5x5** about
half of them, and with the hot **Threshold** at 0.30 most of them.

The pair shows the core of the galaxy, from x 2572 to 2971 and from y 1692 to 1991 of the frame, at 100 % in the
view's [Autostretch](../../reference/glossary.md#autostretch) display mode.

```figure
owed: true
images:
  - image: ../../assets/images/processes/calibration/cosmetic-correction-before.webp
    alt: >-
      A crop at 100 % of the raw frame, gray with a fine checkered texture, the core of NGC 253 running diagonally
      across it, a few round stars, and many single white pixels scattered over the sky and the galaxy alike.
    label: Before
  - image: ../../assets/images/processes/calibration/cosmetic-correction-after.webp
    alt: >-
      The same crop after the correction, the single white pixels gone, the stars and the galaxy as they were, and the
      cluster near the core of the galaxy reduced to one bright pixel.
    label: After
caption: >-
  The core of NGC 253 on a raw light at 100 %, before and after Apply at the defaults, both in the Autostretch display
  mode.
```

## Starting points

| Situation | Start | Then adjust |
| --- | --- | --- |
| A raw light without darks | The defaults, on the mosaic | Raise the hot **Threshold** when star cores change at 100 %. |
| Defects left after calibration | The defaults | Lower one **Threshold** in steps of 0.05 until the defects you see are gone. |
| Clusters of touching hot pixels | **5x5** | Check the star cores, and return to **3x3** when they change. |
| A correction that looks abrupt | **Amount** at 1.00 while you set the thresholds | Lower **Amount** once the right samples are found. |

## When it goes wrong

!!! warning "Star cores can be replaced"
    The window compares each sample with its neighbors, whatever they are. In a raw mosaic one color's site in a bright
    star can stand more than twice above the others, and so can the peak of a small star above the sky. Check the
    stars at 100 % before you apply, keep **3x3**, and raise the hot **Threshold** when they change. A mask can then
    protect the stars that still change.

**Apply** is disabled
:   **Cause.** **Hot Pixels** and **Cold Pixels** are both unticked.
:   **Fix.** Tick at least one of them.

A defect you can see stays
:   **Cause.** Its **Threshold** is too high, it belongs to a cluster that sets the median of its window, **Amount** is
    below 1, or **Channel** leaves it out.
:   **Fix.** Check that its group is ticked, lower its **Threshold**, choose **5x5** for a cluster, return **Amount** to
    1.00, and check **Channel**.

Thousands of ordinary pixels change
:   **Cause.** At low thresholds the grain of a raw light and the steps between its colors pass the tests. On the worked
    example, both thresholds at 0.20 replace about 377,000 samples, and at 0.10 over four million.
:   **Fix.** Start again from 0.50, and lower a **Threshold** only as far as the defects you see need.

Colors shift around corrected pixels
:   **Cause.** A component of another [color model](../../reference/glossary.md#color-model) was corrected and
    converted back, the three channels replaced different pixels, or a raw frame was corrected after the debayer.
:   **Fix.** Choose **RGB**, and correct a raw frame on its mosaic, before the debayer.

Samples that are not numbers, or infinite ones, are still there
:   **Cause.** They are left out of every window and left in place.
:   **Fix.** Replace them, or crop them away, before the correction.

The border of the image differs from its middle
:   **Cause.** The windows are cut at the edges and hold fewer samples there.
:   **Fix.** Check the whole frame, and crop the border if it shows.

## Related

- [Image Calibration](image-calibration.md), whose cosmetic correction finds the defects in the master dark and
  repairs a mosaic from pixels of the same color.
- [Debayer](debayer.md), the step after the correction of a raw mosaic.
- [Median Filter](../noise-reduction/median-filter.md), which replaces every sample with the median of its window.
- [Image Registration](../registration-alignment/image-registration.md) and
  [Image Stacking](../stacking-integration/image-stacking.md), the steps after calibration.
- [Masks](../../interface/masks.md) and [Image History](../../interface/image-history.md), to protect the stars and to
  undo the step.
- [Process Windows](../../interface/process-windows.md), for the preview, the preview area and what every process
  window shares.

## Credits

- The selection networks of the 3x3 to 9x9 windows are [RawTherapee](https://github.com/Beep6581/RawTherapee)'s, by
  Flössie and Ingo Weyrich, from a file that states "Copyright (c) 2004-2010 Gabor Horvath", as
  [librtprocess](https://github.com/CarVac/librtprocess) carries them, under the GNU General Public License, version 3
  or any later version. AfterNight copies them unchanged apart from their names, and its copy is a derived work
  distributed under that licence. The general selection that serves every other window, the tests and their
  thresholds are AfterNight's. **About > About AfterNight** credits them.
