---
kind: process
stage: any
menu: Process > Utilities > Noise Generator
summary: >-
  Adds random noise of a type and strength you choose to an image or a sequence, to make test data for denoisers,
  masks, rejection and other processing that depends on noise.
data_in: either
data_out: either
destructive: true
preview: real-time
sequences: true
masks: post-blend
external: none
glance:
  enabled: >-
    An image or a sequence view is active, no Noise Generator window is open on it yet, and no window that needs the
    image for itself is open on it.
  inputs: >-
    One grayscale or RGB image or sequence, best a duplicate or a synthetic test frame with finite samples between 0
    and 1, and an optional mask of the image's size on its view.
  outputs: >-
    The same view, replaced, with one history step. Its size, channels, pixel type, header and recorded processing
    state are kept. No file, seed or report is written.
  state: >-
    A duplicate of any image, linear or stretched, or a synthetic frame, made into test data. Noise on linear data is
    the easiest to read.
derived: false
---
# Noise Generator

Noise Generator adds random noise to an image, so that you can test a denoiser, a
[mask](../../reference/glossary.md#mask), a stacking [rejection](../../reference/glossary.md#rejection) rule or any
other step that depends on noise. You choose the type of noise and its strength. It makes test data and is no model of
a camera: it reads no gain, exposure, temperature, read noise, dark current or quantization from the image.

**Apply** replaces the image or [sequence](../../reference/glossary.md#sequence) with the noisy one, keeps its size and
[pixel type](../../reference/glossary.md#pixel-type), and adds one step to its
[history](../../reference/glossary.md#history). The window draws a random seed when it opens and keeps it, so the
preview shows the noise Apply adds.

```figure
image: ../../assets/images/processes/utility/noise-generator.webp
alt: >-
  The Noise Generator window. On the left, Noise Type at Gaussian, the Mean, Var, Amount and Salt vs Pepper boxes with
  their sliders, the last two grayed, then Target Image, Channel at RGB and, at the bottom, the Preview check box. On
  the right, a toolbar of zoom buttons, a channel box, a display mode box and a settings button over the preview of a
  red nebula, with the word PREVIEW in its corner and a framed area in its middle labelled Preview: Noise Generator,
  grainier than the image around it. Reset, Close and Apply run along the bottom.
caption: >-
  Noise Generator on the stretched master with Gaussian noise at a Var of 0.0100. The framed area in the preview shows
  the noise over the original.
pins:
  - {n: 2, x: 2.0, y: 16.7}
  - {n: 3, x: 2.0, y: 25.7}
  - {n: 4, x: 2.0, y: 43.4}
  - {n: 5, x: 61.8, y: 55.3}
  - {n: 6, x: 88.7, y: 96.0}
```

## Before you start

- Work on a duplicate. The seed is not recorded, so the noise can be neither taken out nor drawn again later. The
  image's history keeps the step before.
- Use finite samples between 0 and 1. For Gaussian, **Mean** is a
  [fraction of full scale](../../reference/glossary.md#fraction-of-full-scale) and **Var** its square, and every result
  is held to full scale: 0 to 1 for a floating-point image, the range of its type for an integer one.
- With **RGB**, each channel gets its own draws, so an impulse need not hit all three samples of a pixel. Choose a
  component in **Channel** when the noise must be one plane.
- A mask on the view blends the noisy image with the original afterwards.
- On a sequence, every frame gets the same type, values, **Channel** and mask, and noise of its own. The frames are
  read with the view's automatic [debayer](../../reference/glossary.md#debayer) setting, which can turn the frames of a
  [CFA](../../reference/glossary.md#cfa) sequence into RGB first.

## Steps

1. **Open the window.** Make the duplicate the [active image](../../reference/glossary.md#active-image) and choose
   **Process > Utilities > Noise Generator**. The preview shows Gaussian noise inside a framed
   [preview area](../../reference/glossary.md#preview-area) in the middle of the image, over the original.
2. **Choose the noise.** Choose a **Noise Type**. Use only the boxes that type reads, which the controls below list.
3. **Set its strength.** For Gaussian and Speckle raise **Var**, and set **Mean** only for a shift on purpose. For the
   impulse types set **Amount**, and for Salt & Pepper also **Salt vs Pepper**.
4. **Choose the channel.** Leave **Channel** at **RGB** to give each channel its own noise. Choose
   **CIE L* (Lightness)** or **CIE Y (Luminance)** for noise in the brightness alone.
5. **Check at 100 %.** Zoom the preview to 100 % and hold ++space++ to compare with the original. Untick **Preview
   area** in the menu at the end of the preview's toolbar to see the noise Apply adds to the whole image.
6. **Apply.** Choose **Apply**. A progress window follows the run, the view shows the noisy image, and its history
   lists **Noise Generator** with the type.

## Controls

**Target Image**, the preview pane, **Close** and the progress window work as in every process window: see
[Process Windows](../../interface/process-windows.md). What is particular to this window is below.

```controls
window: Noise Generator
controls:
  - name: Noise Type
    kind: combo
    default: Gaussian
    options: [Gaussian, LocalVar, Poisson, Salt, Pepper, "Salt & Pepper", Speckle]
    effect: Chooses the distribution the noise is drawn from, and the boxes below that it reads. **Gaussian** adds noise of **Mean** and **Var**. **LocalVar** adds Gaussian noise of a variance of 0.01 everywhere. **Poisson** redraws each sample about its own value. **Salt**, **Pepper** and **Salt & Pepper** replace a share of the samples by the top or the bottom of the range. **Speckle** adds noise in proportion to the sample.
    range: Gaussian and Speckle read **Mean** and **Var**. Salt, Pepper and Salt & Pepper read **Amount**, and Salt & Pepper also **Salt vs Pepper**. LocalVar and Poisson read no box.
    enabled: Disabled while Apply runs. A type enables the boxes it reads, but does not always disable the others. Salt, Pepper and Salt & Pepper leave **Mean** and **Var** as the type before left them, and Speckle leaves **Salt vs Pepper** so. A box left enabled that way is ignored.
    tradeoff: Each change computes the preview again, from the same seed.
    scope: The preview and Apply, on what **Channel** selects, before the mask is blended, for images and sequences.
    persistence: Until the window closes. Each run takes it as it is when it starts. **Reset** and a finished Apply return it to **Gaussian**.
  - name: Mean
    kind: spinbox
    default: "0.0000"
    widget: {min: 0, max: 0.5, step: 0.005, decimals: 4}
    effect: Sets the mean of the normal draw. For Gaussian it shifts every sample by that much, so a higher value brightens the image. For Speckle it is a share of the sample, so it brightens bright samples more than dark ones.
    range: A fraction of full scale for Gaussian, a share of the sample for Speckle. The arrows step by one power of ten below the value, and by 0.0001 at 0. The slider moves the value in 1,000 equal steps.
    enabled: For Gaussian and Speckle. Every other type ignores it, also when it stays enabled after a change of type.
    tradeoff: A positive mean pushes more samples against the top of the range, where they are held.
    scope: The preview and Apply, on what **Channel** selects, before the mask is blended.
    persistence: Until the window closes. **Reset** and a finished Apply return it to its default.
  - name: Var
    kind: spinbox
    default: "0.0020"
    widget: {min: 0, max: 0.5, step: 0.001, decimals: 4}
    effect: Sets the variance of the normal draw. Higher spreads the samples further from where they were. The standard deviation is its square root, about 0.045 at the default.
    range: A variance, not a deviation. In squared fractions of full scale for Gaussian, and in squared shares of the sample for Speckle. The arrows and the slider move it as they move **Mean**.
    enabled: For Gaussian and Speckle. LocalVar and the other types ignore it, also when it stays enabled.
    tradeoff: A large variance holds more samples at the ends of the range and hides the structure of the image. At 0 nothing random is added, and **Mean** still shifts the samples.
    scope: The preview and Apply, on what **Channel** selects, before the mask is blended.
    persistence: Until the window closes. **Reset** and a finished Apply return it to its default.
  - name: Amount
    kind: spinbox
    default: "0.0200"
    widget: {min: 0, max: 0.5, step: 0.001, decimals: 4}
    effect: Sets the share of the samples that Salt, Pepper and Salt & Pepper replace. Higher replaces more.
    range: The chance that each sample of each channel is replaced, drawn for each on its own. The arrows and the slider move it as they move **Mean**.
    enabled: For Salt, Pepper and Salt & Pepper. Every other type ignores it, and it is disabled when the window opens.
    tradeoff: A replaced sample is lost. With **RGB** the three samples of a pixel are drawn separately, so the impulses can be colored.
    scope: The preview and Apply, on what **Channel** selects, before the mask is blended.
    persistence: Until the window closes. **Reset** and a finished Apply return it to its default.
  - name: Salt vs Pepper
    kind: spinbox
    default: "0.50"
    widget: {min: 0, max: 1, step: 0.01, decimals: 2}
    effect: Sets the share of the replaced samples that take the top of the range, the others taking its bottom. Higher gives more bright impulses, lower more dark ones. At 0 every replaced sample is black, at 1 every one is white.
    range: It does not change how many samples are replaced, which is **Amount**. The arrows and the slider move it as they move **Mean**.
    enabled: For Salt & Pepper, and disabled when the window opens. It stays enabled when you switch from Salt & Pepper to Speckle, which ignores it.
    tradeoff: Its two ends make a one-sided test with Salt & Pepper.
    scope: The preview and Apply, on what **Channel** selects, before the mask is blended.
    persistence: Until the window closes. **Reset** and a finished Apply return it to its default.
  - name: Target Image
    kind: combo
    default: The active image
    options: [The active image, Every other open image or sequence the window can work on]
    effect: Chooses the image or sequence Apply replaces, with the mask on its view. The choices of **Channel** and the preview follow it.
    tradeoff: Apply replaces the image chosen here once it succeeds, and the noise cannot be taken out again.
    persistence: Until the window closes. **Reset** leaves it as it is.
  - name: Channel
    kind: combo
    default: RGB
    options: [RGB, Red, Green, Blue, CIE L* (Lightness), CIE a*, CIE b*, CIE c*, CIE h*, CIE X, CIE Y (Luminance), CIE Z, HSL Hue, HSL Saturation, HSL Lightness, HSV Hue, HSV Saturation, HSV Value, YCbCr Luma, YCbCr Blue Chroma, YCbCr Red Chroma]
    effect: Chooses where the noise goes. **RGB** draws noise for each sample of the three channels on its own. **Red**, **Green** or **Blue** adds it to that channel and keeps the other two. Every other choice adds it to one component of another color model, as one plane, and converts the image back.
    enabled: For an RGB image. A grayscale image shows a disabled **Grayscale**. Disabled while Apply runs.
    tradeoff: A component costs two conversions and a working copy, and the conversion back can round the image again and put colors out of range.
    scope: The preview and Apply, before the mask is blended. Process Windows describes each component.
    persistence: Until the window closes. Each run takes it as it is when it starts. **Reset**, a finished Apply and a new target return it to **RGB**.
  - name: Preview
    kind: check
    default: "On"
    effect: Shows the noisy image beside the controls, with the mask blended in. Every preview draws from the window's seed, so the same values show the same noise.
    range: >-
      The pane opens with a **Preview area** of 1,200 by 1,200 pixels in the middle of the image, labelled "Preview:
      Noise Generator", or the whole image when it is smaller. At the window's default size, **Preview area** and
      **Take snapshot** are in the menu at the end of the pane's toolbar.
    tradeoff: The area is drawn as an image of its own, so its noise is not the noise Apply puts on those pixels. Judge the kind and the strength of the noise there, and untick **Preview area** to see the samples Apply writes.
    scope: "The window only. On a sequence it shows the frame the view displays, with the noise of the first frame. Look at the noise at **Zoom 100%**: a fitted view resamples it and hides single impulses. **View Channel** shows each channel's own draws. **Display Mode** and **Autostretch Settings** change only how the preview is shown, and an Autostretch makes faint noise look stronger."
    persistence: Until the window closes. **Reset** leaves it as it is.
    note: A snapshot of the preview draws from the same seed as the preview of the same part, opens as `<target> - Noise Generator - SNAPSHOT`, and draws no new seed.
  - name: Reset
    kind: button
    default: ""
    effect: Returns **Noise Type** and the four boxes to their defaults and, for an RGB image, **Channel** to **RGB**.
    range: It keeps the seed, so the preview that follows draws the same noise as before for the same values.
    enabled: Disabled while Apply runs.
  - name: Apply
    kind: button
    default: ""
    effect: Adds the noise to the whole image, or to every frame of the sequence, with the type, the four values, **Channel** and the seed as they are when you press it. Then it returns the controls to their defaults and draws a new seed, so the next Apply adds new noise.
    enabled: Always, also at the defaults, which add Gaussian noise. While a run works it is disabled, with **Noise Type**, the four boxes, **Reset** and **Close**.
    tradeoff: A single image shows a progress window too, with **Cancel**, which the generator notices between two rows. The work grows with the number of samples, so an RGB image costs three times a grayscale one. Poisson costs more than the other types, and a color component, a mask and a sequence add work and memory.
    scope: The whole image or sequence, with the mask and its inversion as they are when you press it, blended afterwards. On a single image, Apply adds exactly the noise of a preview without an area. Each frame of a sequence draws its own noise, from the seed shifted by the frame's index.
    persistence: "The history step is **Noise Generator** and reads, for example, `Noise Type Gaussian -  in channel: RGB`. For a grayscale image it ends after the type. It records neither the four values nor the seed. The header cards, the other metadata and the recorded processing state are kept."
    note: Closing the window instead discards the values and the seed. Nothing of the window is saved between sessions, and nothing brings back the seed of an earlier Apply. For another draw with the same values, apply to a fresh duplicate after an Apply, or open the window again.
```

## How it works

1. **Channel** decides what gets the noise: every channel, one channel, or one component of the image converted to
   another [color model](../../reference/glossary.md#color-model). An integer image's values are restated in its own
   codes: for Gaussian, **Mean** times the span of its type and **Var** times the square of that span.
2. Each sample gets its own draw, keyed on its position, its channel and the seed rather than taken from a running
   series. The same seed gives the same noise at any number of threads, and each frame of a sequence uses the seed
   shifted by a fixed multiple of its index.
3. The result is held to full scale and written back in the image's own pixel type, an integer rounded to the
   nearest code. A salt sample takes the top of that range and a pepper sample its bottom.
4. A component is converted back to RGB, and a mask on the view then blends the result with the original.
5. The image replaces the target and its history gets one step. Its header, its metadata and its recorded
   [processing state](../../reference/glossary.md#processing-state) are kept, and nothing records that the noise was
   added.

### Model

With \(I\) a sample as a fraction of full scale, \(m\) the value of **Mean**, \(v\) the value of **Var** and \(z\) a
draw from the standard normal distribution, Gaussian gives

\[ O = I + m + \sqrt{v}\,z \tag{1} \]

LocalVar is equation (1) with \(m = 0\) and \(v = 0.01\). Speckle gives

\[ O = I + I\,\bigl(m + \sqrt{v}\,z\bigr) \tag{2} \]

so its noise grows with the sample. With \(a\) the value of **Amount** and \(q\) that of **Salt vs Pepper**, Salt
replaces a sample by the top of the range with the probability \(a\), and Pepper by the bottom. Salt & Pepper replaces
it with the probability \(a\), by the top with the probability \(q\) and otherwise by the bottom. With \(s\) equal to
256 for an 8-bit integer image and to 65,536 for every other type, and \(P(\lambda)\) a draw from the Poisson
distribution of mean \(\lambda\), Poisson gives

\[ O = \frac{P(I\,s)}{s} \tag{3} \]

Its deviation is \(\sqrt{I/s}\): about 0.002 at a sample of 0.28 in a floating-point image, which is faint. A sample at
or below 0 becomes 0. Every draw is for one sample of one channel: in an RGB image, \(I\) is one channel of a pixel.

### Boundaries

- The seed comes from the system's source of randomness and the clock when the window opens, and is kept until an
  Apply finishes. It is not stored with the image, so a later session cannot draw the same noise.
- **Var** is a variance, not a standard deviation. The deviation is its square root.
- The result is held to full scale before the mask blends it and before an integer image is rounded. What the clip
  cut is lost.
- A sample the image marks as invalid keeps its value. A sample that is not a finite number is not refused: under
  Gaussian and Speckle one that is not a number stays so, and Poisson makes it 0.
- Each channel is worked as a single-precision plane, so a 64-bit floating-point image comes back at single
  precision. A run that fails or is cancelled writes nothing, and no lock makes other processes wait for it.

### Worked example

The image is the linear [master](../../reference/glossary.md#master) after Histogram Transformation with **Shadows**
and **Midtones** at 0.02, which leaves the background near 0.28. With **Noise Type** at **Gaussian**, **Mean** at 0,
**Var** at 0.0100 and **Channel** at **RGB**, the deviation is \(\sqrt{0.01} = 0.1\). By equation (1), about two
background samples in three land between 0.18 and 0.38, and about one in 400 falls below 0 and is held there. A star
core at 1 keeps 1 in about half its samples, and the other half drop below it. The three channels draw apart, so the
grain is colored.

```figure
images:
  - image: ../../assets/images/processes/utility/noise-generator-before.webp
    alt: A crop of the stretched nebula at 100 percent before the noise, bright clouds beside dark dust lanes and many small stars on a gray background.
    label: Before
  - image: ../../assets/images/processes/utility/noise-generator-after.webp
    alt: The same crop after the noise, every part covered by a coarse colored grain that blurs the edges of the dust lanes and hides the faintest stars.
    label: After
caption: >-
  The middle of the stretched Carina Nebula master at 100 % before and after Apply with Gaussian noise at a Var of
  0.0100. Both are shown in the Linear display mode, without a display stretch.
```

## Starting points

| Situation | Start | Then adjust |
| --- | --- | --- |
| A denoiser test on noise of one strength | **Gaussian**, **Mean** at 0, **Var** at 0.002 | Raise **Var** until the noise shows at the scale you test. Add **Mean** only for a shift on purpose. |
| Noise that grows with the signal | **Speckle**, **Mean** at 0, a low **Var** | Raise **Var** while you compare bright and dark regions. Too much breaks up and clips the bright structure. |
| [Hot pixels](../../reference/glossary.md#hot-pixel) and [cold pixels](../../reference/glossary.md#cold-pixel), or a test of rejection | **Salt & Pepper**, **Amount** at 0.02, **Salt vs Pepper** at 0.5 | Set **Amount** first, then the split. Too much hides the image under the impulses. |
| Noise in the brightness alone | **Channel** at **CIE L* (Lightness)** | Compare with **RGB**, whose separate draws make colored grain and colored impulses. |

## When it goes wrong

!!! danger "The noise cannot be taken out"
    Apply replaces the image, and records neither the seed nor the values. Work on a duplicate, and use the image's
    history to return to the image before.

!!! warning "A box can stay enabled for a type that ignores it"
    After a change of **Noise Type**, **Mean** and **Var** can stay enabled for an impulse type, and **Salt vs Pepper**
    for Speckle. Only the boxes the type reads change the noise. Press **Reset** before you change the type for a
    clean start.

The noise Apply added is not the noise of the preview area
:   **Cause.** The area is drawn as an image of its own.
:   **Fix.** Untick **Preview area**, and compare the preview of the whole image with Apply.

A second Apply adds different noise
:   **Cause.** The window draws a new seed after each finished Apply.
:   **Fix.** Duplicate the noisy result when you need the same noise on another copy.

LocalVar looks like plain Gaussian noise
:   **Cause.** It is Gaussian noise of one variance, 0.01, over the whole image.
:   **Fix.** Use Gaussian for a variance you choose. Noise that varies over the image needs another tool.

Salt and pepper make colored pixels
:   **Cause.** Each sample of each channel is drawn on its own.
:   **Fix.** Choose **CIE L* (Lightness)** or **CIE Y (Luminance)** in **Channel**.

Highlights and shadows go flat
:   **Cause.** Noise beyond full scale is held at the ends of the range.
:   **Fix.** Lower **Mean**, **Var** or **Amount**.

Nothing seems to change
:   **Cause.** The values add nothing, a box the type ignores was changed, a mask protects the area, or Poisson adds
    very little to a floating-point image.
:   **Fix.** Check the values the type reads, the mask and its inversion, and look at the preview at 100 %.

Apply reports "An error occurred when running the process:" and a reason
:   **Cause.** The run was refused, often because the memory limit cannot hold the copies it needs, and nothing was
    written.
:   **Fix.** Close other images or work on a smaller one, or raise the memory limit on the Processing page of the
    settings, which takes effect at the next start.

## Related

- [Wavelet Denoise](../noise-reduction/wavelet-denoise.md) and
  [Total Variation Denoise](../noise-reduction/total-variation-denoise.md), to test on the noisy image.
- [Image Integration](../stacking-integration/image-integration.md), whose rejection can be tested on frames with
  impulses.
- [Pixel Math](../pixel-math/pixel-math.md), for noise from a formula with a seed you state.
- [Image History](../../interface/image-history.md) and [Saving and Exporting](../../interface/saving-and-exporting.md),
  to undo the step and to keep a test frame, since its seed is not kept.
- [Masks](../../interface/masks.md), to add noise to one part of the image.
- [Process Windows](../../interface/process-windows.md), for the preview, the preview area and what every process
  window shares.

## Credits

- [scikit-image: `random_noise`](https://scikit-image.org/docs/stable/api/skimage.util.html#skimage.util.random_noise),
  whose noise types and parameters AfterNight's own generator follows. The process does not use scikit-image.
