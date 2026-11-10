---
kind: process
stage: finish
menu: Process > Details > Dark Structure Enhance
summary: >-
  Darkens the places that are darker than their large-scale surroundings, such as dust lanes and dark nebulae, through
  a mask it builds from the image with a starlet transform.
data_in: nonlinear
data_out: nonlinear
destructive: true
preview: real-time
sequences: true
masks: post-blend
external: none
glance:
  enabled: >-
    An image or a sequence view is active, no Dark Structure Enhance window is open on it yet, and no window that
    needs the image for itself is open on it.
  inputs: One grayscale or RGB image or sequence, of any pixel type, and an optional mask of the image's size on its view.
  outputs: >-
    The same view, replaced, with one history step. Its size, channels and pixel type are kept. With Create mask image
    on, an Apply to a single image also opens the mask it used as a new image.
  state: A stretched image, mono or RGB, whose dust lanes or dark nebulae should stand out more.
derived: false
---
# Dark Structure Enhance

Dark Structure Enhance makes the dark structures of a picture darker: dust lanes, dark nebulae and the gaps between
bright clouds. It measures where the image is darker than its own smooth surroundings, with two smoothings of the
[starlet transform](../../reference/glossary.md#starlet-transform), and turns that measurement into a
[mask](../../reference/glossary.md#mask). Then it darkens the image through the mask with a
[midtones transfer function](../../reference/glossary.md#midtones-transfer-function). It sharpens no bright detail and
detects no stars.

**Apply** replaces the image or [sequence](../../reference/glossary.md#sequence) with the darkened one, keeps its size
and [pixel type](../../reference/glossary.md#pixel-type), and adds one step to its
[history](../../reference/glossary.md#history). The result is recorded as nonlinear. **Create mask image** also opens
the mask as an image of its own.

```figure
image: ../../assets/images/processes/star-operations/dark-structure-enhance.webp
alt: >-
  The DarkStructureEnhance window. On the left, the Layers to Remove, Amount and Iterations boxes with their sliders,
  the Scaling Function and Preview Mode lists, then Target Image and Channel at RGB. Near the bottom, the Preview box
  ticked and Create mask image unticked. On the right, the preview shows a red nebula crossed by dark lanes on a gray
  background, and a framed area in the middle shows the darkened result. Reset, Close and Apply run along the bottom.
caption: >-
  Dark Structure Enhance on a stretched master at its defaults, with its preview area in the middle.
pins:
  - {n: 2, x: 2.0, y: 47.1}
  - {n: 3, x: 2.0, y: 37.8}
  - {n: 4, x: 2.0, y: 20.1}
  - {n: 5, x: 2.0, y: 29.0}
  - {n: 6, x: 34.5, y: 40.0}
  - {n: 7, x: 90.6, y: 96.0}
```

## Before you start

- [Stretch](../../reference/glossary.md#stretch) the image first. The darkening is a curve made for a picture whose
  dark structures already show.
- **Apply** replaces the image. Its history keeps the step before, and this window has no inverse.
- Correct gradients first. A slope in the background can read as a broad dark structure.
- A mask on the view blends the result with the original afterwards, as a second mask.
- On a sequence, every frame gets the same settings and a mask built from its own samples, and the mask on the view
  is used for every frame. **Create mask image** opens nothing, and the run needs the disk space of a second copy.

## Steps

1. **Open the window.** Make the stretched image active and choose **Process > Details > Dark Structure Enhance**.
   The window is titled DarkStructureEnhance, and the preview shows the result inside a framed area in the middle.
2. **Choose what to measure.** Leave **Channel** at **RGB**, or choose **CIE L* (Lightness)** to darken the
   lightness alone and keep the colors closer.
3. **Look at the mask.** Set **Preview Mode** to **Generated mask**, with the view's mask off. The dust lanes should
   be brighter than the plain background, and the star cores dark.
4. **Set the scale.** Change **Layers to Remove** until the mask follows the dark lanes rather than background
   slopes, star surroundings or noise. Then try the other **Scaling Function**.
5. **Set the strength.** Return **Preview Mode** to **Processed image** and adjust **Amount**. Change **Iterations**
   only after it.
6. **Check the whole image.** Untick [**Preview area**](../../reference/glossary.md#preview-area) in the preview's
   toolbar, or in the menu at its end. Zoom to 100 % on the dark lanes and the stars beside them, and press
   ++space++ to compare.
7. **Apply.** Tick **Create mask image** to keep the mask, then choose **Apply**. The dust lanes are darker, the
   history lists **DarkStructureEnhance**, and the mask opens beside the image when you asked for it.

## Controls

**Target Image**, **Channel**, **Preview**, the preview pane, **Close** and the progress window work as in every
process window: see [Process Windows](../../interface/process-windows.md). The entries below hold what is particular to
this window, which has no histogram.

```controls
window: DarkStructureEnhance
controls:
  - name: Layers to Remove
    kind: spinbox
    default: "8"
    widget: {min: 7, max: 10, step: 1}
    effect: Sets how coarse the smooth surroundings are that the image is compared with. Higher compares with a smoother picture and selects broader structures. Lower selects narrower ones.
    range: Whole layers. 10 is the deepest smoothing the transform holds, with taps 512 pixels apart.
    enabled: Disabled while Apply runs.
    tradeoff: Each layer more costs two more smoothing passes over each channel, and no more memory. On a small preview area a high value reaches far past the area's edges, and the mask is ruled by them.
    scope: The mask only, in the preview and in Apply, from every channel the run reads. It does not change the curve.
    persistence: Until the window closes. **Reset** and a finished Apply return it to its default.
  - name: Scaling Function
    kind: combo
    default: 5x5 B3 Spline
    options: [3x3 Linear Interpolation, 5x5 B3 Spline]
    effect: Chooses the kernel of the two smoothings the mask is measured with. **5x5 B3 Spline** smooths more widely and softly. **3x3 Linear Interpolation** keeps harder edges.
    range: "The kernels are `[1, 2, 1] / 4` and `[1, 4, 6, 4, 1] / 16`, applied along the rows and then along the columns. The cleaning of the mask always uses the linear one."
    enabled: Disabled while Apply runs.
    tradeoff: The B3 spline reads five samples in each pass where the linear kernel reads three, so it costs more.
    scope: The mask only.
    persistence: Until the window closes. **Reset** and a finished Apply return it to **5x5 B3 Spline**.
  - name: Amount
    kind: spinbox
    default: "0.40"
    widget: {min: 0, max: 0.99, step: 0.01, decimals: 2}
    effect: Sets how strongly the masked places are darkened. Higher darkens their midtones more. At 0 the curve changes nothing.
    range: It sets the curve's midtones balance to 0.5 plus half the value, so the default gives 0.70 and the whole range darkens.
    enabled: Disabled while Apply runs.
    tradeoff: A high value crushes the detail inside the dark structures. At 0 the mask is still built, and its cost paid.
    scope: The darkening, on what **Channel** selects. It does not change the mask, although a **Generated mask** preview is computed again when it changes.
    persistence: Until the window closes. **Reset** and a finished Apply return it to its default.
  - name: Iterations
    kind: spinbox
    default: "1"
    widget: {min: 1, max: 10, step: 1}
    effect: Sets how many times the curve is applied through the mask. Each pass darkens the result of the one before again.
    range: The mask is built once, before the first pass, and is the same for every pass.
    enabled: Disabled while Apply runs.
    tradeoff: Each pass adds a curve and a blend over every channel. A few passes crush the shadows quickly and can show steps between tones.
    scope: The darkening, on what **Channel** selects. It does not change the mask.
    persistence: Until the window closes. **Reset** and a finished Apply return it to its default.
  - name: Preview Mode
    kind: combo
    default: Processed image
    options: [Processed image, Generated mask]
    effect: Chooses what the preview shows. **Processed image** is what Apply writes. **Generated mask** is the mask the darkening goes through, white where it darkens fully and black where it leaves the image alone.
    range: The mask is a 32-bit floating-point grayscale image, shown over the three channels of a color image. It is built without the passes of **Iterations**.
    enabled: While **Preview** is on. Disabled while Apply runs.
    tradeoff: A mask on the target's view blends the mask preview with the image, as it blends a result. Turn the view's mask off to read the mask alone.
    scope: The preview and **Take snapshot** only. Apply always darkens the image. In **Generated mask**, switching to the original compares the mask with the image, and a preview area shows the mask inside the area and the image around it.
    persistence: Until the window closes. **Reset** and a finished Apply return it to **Processed image**.
    note: A snapshot in **Generated mask** is that mask, recorded as nonlinear with the operation `dark-structure-mask`. A snapshot in **Processed image** keeps the target's record and is not recorded as nonlinear. Neither opens a mask image.
  - name: Target Image
    kind: combo
    default: The active image
    options: [The active image, Every other open image or sequence the window can work on]
    effect: Chooses the image or sequence Apply replaces, with the mask on its view. The choices of **Channel** and the preview follow it.
    persistence: Until the window closes. **Reset** leaves it.
  - name: Channel
    kind: combo
    default: RGB
    options: [RGB, Red, Green, Blue, CIE L* (Lightness), CIE a*, CIE b*, CIE c*, CIE h*, CIE X, CIE Y (Luminance), CIE Z, HSL Hue, HSL Saturation, HSL Lightness, HSV Hue, HSV Saturation, HSV Value, YCbCr Luma, YCbCr Blue Chroma, YCbCr Red Chroma]
    effect: Chooses what is measured and darkened. **RGB** builds one mask from the three channels, averaged with equal weights, and darkens each channel through it. Any other choice builds the mask from that channel or component alone and darkens it alone.
    enabled: For an RGB image. A grayscale image shows a disabled **Grayscale**. Disabled while Apply runs.
    tradeoff: A lightness or a luma component changes the colors least. With **RGB** the three channels share one mask, which can shift colors. A hue or chroma component can jump, or leave the range on its way back to RGB.
    scope: The mask, the preview and Apply, on images and sequences. The history step names the choice.
    persistence: Until the window closes. **Reset** leaves it. A finished Apply or another target returns it to **RGB**.
  - name: Preview
    kind: check
    default: "On"
    effect: Shows the preview beside the controls. Off removes it and disables **Preview Mode**.
    enabled: Disabled while Apply runs.
    tradeoff: Inside a preview area the mask is measured on the area alone. Its smoothings reach the area's reflected edges and its rescalings use the area's own extremes, so the result there can differ a lot from what Apply writes.
    scope: "The window only. The preview opens with a preview area of 800 by 800 pixels in the middle of the image, labelled \"Preview: DarkStructureEnhance\", and its toolbar offers **Display Mode**."
    persistence: Until the window closes. **Reset** leaves it.
  - name: Create mask image
    kind: check
    default: "Off"
    effect: On, a finished Apply also opens the mask it darkened through, as a new grayscale image named `<target> - DarkStructureEnhance Mask` beside the target.
    range: "The mask is 32-bit floating point, of the target's size. Its history starts with `DarkStructureEnhance: mask created from '<target>'`."
    enabled: Disabled while Apply runs. It counts only for an Apply to a single image. The preview, a snapshot and an Apply to a sequence open no mask image.
    tradeoff: One more full-size image in memory. The darkening is the same either way.
    scope: The new image keeps the target's acquisition data, plate solution and lineage. It keeps no header card but its processing state, which is nonlinear with the operation `dark-structure-mask`.
    persistence: Until the window closes. **Reset** and a finished Apply turn it off.
    note: When the mask cannot be opened, the window warns "Mask output was not available."
  - name: Reset
    kind: button
    default: ""
    effect: Returns **Layers to Remove**, **Scaling Function**, **Amount**, **Iterations**, **Preview Mode** and **Create mask image** to their defaults, and one preview follows.
    range: It leaves **Target Image**, **Channel**, **Preview**, the mask on the view and the preview area as they are.
  - name: Apply
    kind: button
    default: ""
    effect: Builds the mask of the whole image, or of each frame of the sequence, darkens through it, and then returns the window's values to their defaults.
    enabled: From the moment the window opens, since the defaults darken. While the run works it is disabled, with every control of the window, **Preview** and **Preview Mode** included, and the preview's snapshot and area.
    tradeoff: The run holds a copy of the target, a floating-point copy of an integer image, the working image the darkening writes, and the mask image when you asked for one. A progress window opens for a single image too, and its bar shows that the run works, not a percentage.
    scope: The whole image, never limited by a preview area, with the mask on the view blended afterwards.
    persistence: "The history step is **DarkStructureEnhance** and reads, for example, `Layers: 8, Scaling: 5x5 B3 Spline, Amount: 0.40 (midtones 0.70), Iterations: 1`. On a color image, a run on one channel or component adds ` in channel: ` and its name, as in `in channel: CIE L* (Lightness)`."
```

## How it works

1. Each sample is read as a [fraction of full scale](../../reference/glossary.md#fraction-of-full-scale), an integer
   image in a floating-point copy. **Channel** hands over the three channels, one of them or one component.
2. For each channel, two starlet smoothings make a smooth version of it, and the difference says where the channel
   lies below its surroundings. The channels' differences are averaged into one plane.
3. The plane is rescaled from 0 to 1 by its own extremes, cleaned of its finest noise and rescaled again: that is the
   mask.
4. A curve made from **Amount** is applied to each channel and blended with it through the mask, as many times as
   **Iterations** says. Each blend is held between 0 and 1.
5. The result is written back in the image's pixel type, rounded to the nearest value, and the view's mask blends it
   with the original. The header, the [plate solution](../../reference/glossary.md#plate-solution) and the
   acquisition data are kept. A saved file records `ANLIN` as `nonlinear`, `ANASRC` as `process` and `ANOP` as
   `dark-structure-enhance`, no linear encoding is stated any more, and the
   [lineage](../../reference/glossary.md#lineage) gains an entry with the layers, kernel, midtones and iterations.

### Model

Write \(F_c\) for channel \(c\), with the samples set aside read as 0, and \(S_k(X)\) for a plane \(X\) without its
\(k\) finest starlet layers: \(k\) smoothings in a row, whose taps lie 1, 2, 4 and up to \(2^{k-1}\) pixels apart.
With \(N\) the value of **Layers to Remove** and \(C\) the number of channels read, the measurement is

\[ Q = \frac{1}{C} \sum_{c=1}^{C} \Bigl( S_N\bigl(S_6(F_c)\bigr) - F_c \Bigr) \tag{1} \]

\(Q\) is positive where the image is darker than its surroundings. It is an average of the channels, not a perceptual
luminance. With \(Q_{\min}\) and \(Q_{\max}\) its lowest and highest finite values, the first mask is

\[ A_0 = \frac{Q - Q_{\min}}{Q_{\max} - Q_{\min}} \tag{2} \]

The place darkest against its surroundings gets 1, the brightest, usually a star core, gets 0, and a place equal to
its surroundings lands at \(-Q_{\min}/(Q_{\max} - Q_{\min})\). So a few bright stars can leave the plain background
high in the mask. The mask is then split into two layers and a residual with the linear kernel. The finest layer is
dropped. The next is shrunk toward 0, five times, by three times its noise: 1.4826 times its
[median absolute deviation](../../reference/glossary.md#median-absolute-deviation) about its median. The rest is
added back, rescaled by equation (2) and held between 0 and 1: that is the mask \(A\).

With \(a\) the value of **Amount**, the midtones balance \(m\) and the curve \(T\) of a sample \(x\) are

\[ m = 0.5 + 0.5\,a, \qquad T(x) = \frac{(m - 1)\,x}{(2m - 1)\,x - m} \tag{3} \]

\(T\) keeps 0 and 1, and with \(m\) above 0.5 lowers every value between. With \(W_k\) the channel after \(k\)
passes, \(W_0\) the channel itself, each pass gives

\[ W_{k+1} = \min\Bigl(1,\ \max\bigl(0,\ (1 - A)\,W_k + A\,T(W_k)\bigr)\Bigr) \tag{4} \]

### Boundaries

- A sample that holds no number, or that the image marks as invalid, is read as 0 for the mask and written back as
  it was. The extremes of equation (2) leave such values out.
- When the range of either rescaling is no wider than \(10^{-7}\), the mask is black and the image keeps its values,
  apart from the hold of equation (4). That hold applies where the mask is 0 too, so a floating-point sample outside
  0 to 1 moves to the nearer end, even with **Amount** at 0.
- The darkening is computed in single precision. 8-bit and 16-bit integer and 32-bit floating-point images lose
  nothing to it. 64-bit floating-point and 32-bit integer images keep only single precision, even where the mask is
  black. An integer result is rounded to the nearest value, a tie to the even one.
- Beyond the edge of the image, or of a preview area, the smoothings reflect the image about its edge pixel without
  repeating it. The transform holds at most twelve layers, with taps at most 512 pixels apart, so **Layers to
  Remove** stops at 10: 11 would need taps 1,024 pixels apart.

### Worked example

The picture above shows the window at its defaults, with **Preview area** off, on the linear Carina
[master](../../reference/glossary.md#master) after Histogram Transformation with **Shadows** and **Midtones** at
0.02, which leaves the background near 0.28. **Amount** at 0.40 gives \(m = 0.70\) by equation (3). One pass takes
0.20 to 0.097, 0.50 to 0.30, 0.80 to 0.63 and the background's 0.28 to 0.14. By equation (4) a sample of 0.50 stays
0.50 where the mask is 0, becomes 0.40 where it is 0.5 and 0.30 where it is 1. A second pass there takes 0.30 to
0.155. With **Amount** at 0 every sample inside 0 to 1 keeps its value, but the mask is still built.

```figure
images:
  - image: ../../assets/images/processes/star-operations/dark-structure-enhance-before.webp
    alt: >-
      A crop of the nebula at full resolution before the process: bright red and white clouds crossed by brown dust
      lanes, with many stars on a gray background.
    label: Before
  - image: ../../assets/images/processes/star-operations/dark-structure-enhance-after.webp
    alt: >-
      The same crop after the process: the dust lanes and the darker gaps between the clouds are deeper and stand out
      more, while the bright clouds and the stars keep their brightness.
    label: After
caption: >-
  A crop at 100 % of the middle of the stretched Carina master before and after Apply at the defaults. Both are shown
  in the Linear display mode, without a display stretch.
```

## Starting points

| Situation | Start | Then adjust |
| --- | --- | --- |
| Dust lanes in a color image | **CIE L* (Lightness)** and the defaults | **Layers to Remove** with the mask shown, then **Amount**, then **Iterations**. Lanes that barely separate want more **Amount**, black cores or a black channel less. |
| Narrow dark filaments | **Generated mask**, **Layers to Remove** at 7 | A filament missing from the mask: the other **Scaling Function**. Noise or star rims in it: a mask on the view. |
| A broad dark nebula | **Generated mask**, **Layers to Remove** at 9 or 10, **5x5 B3 Spline** | Only small knots selected: more layers. The background selected: fewer. Check the whole mask before **Amount**. |
| Stars or background to protect | The mask alone, the view's mask off | Turn the view's mask on and check its shape and inversion until only the right places darken, then **Amount**. |

## When it goes wrong

!!! warning "Read the mask with the view's mask off"
    A mask on the target's view blends the **Generated mask** preview with the image, which then shows outside the
    masked region. Turn the view's mask off to judge the mask, and on again before Apply if it should limit the
    result.

!!! warning "A preview area is not what Apply computes"
    The mask is measured on the area alone, with its own extremes and reflected edges. Untick **Preview area** for
    the last check.

Nothing changes
:   **Cause.** **Amount** is 0, the mask is black, the view's mask protects the place, or **Channel** names another
    channel.
:   **Fix.** Look at the mask with the view's mask off, press **Reset**, and check **Channel**.

The whole background darkens
:   **Cause.** The layers select a broad gradient, or bright stars set the mask's low end and leave the background
    high in it.
:   **Fix.** Lower **Layers to Remove**, remove the gradient first, or protect the background with a mask on the view.

The mask does not change when Amount changes
:   **Cause.** **Amount** and **Iterations** change the curve, not the mask.
:   **Fix.** Judge them with **Preview Mode** at **Processed image**.

The shadows turn black after a few iterations
:   **Cause.** Each pass applies the same curve again through the same mask.
:   **Fix.** Return to one pass and lower **Amount**.

The colors shift
:   **Cause.** With **RGB** the channels are darkened through one mask, or a component was converted back to RGB.
:   **Fix.** Choose **CIE L* (Lightness)** or **YCbCr Luma**, and check each channel with **View Channel**.

No mask image opens
:   **Cause.** It opens only after an Apply to a single image, not after a sequence or a snapshot.
:   **Fix.** Apply to one image of the set, or take a snapshot in **Generated mask**.

"An error occurred when running the process:" names the memory budget
:   **Cause.** The memory limit cannot hold the copies the run needs, and nothing was written.
:   **Fix.** Close images you no longer need, or work on a smaller image or on one component.

**Cancel** takes a moment
:   **Cause.** The run stops at its next stage, layer, pass or band of rows.
:   **Fix.** Wait. While you adjust, keep the preview area small and the layers few.

## Related

- [Histogram Transformation](../stretching/histogram-transformation.md), for the stretch that comes first.
- [Multiscale Linear Transform](../multiscale/multiscale-linear.md), to work on the starlet layers themselves, with
  controls for each layer.
- [Local Contrast Enhancement](../sharpening/local-contrast-enhancement.md) and
  [Local Histogram Equalization](../sharpening/local-histogram-equalization.md), which raise contrast in bright and
  dark places alike.
- [Range Selection](../analysis/range-selection.md), for a mask chosen by brightness rather than by structure.
- [Mask Drawing](../masking/mask-drawing.md) and [Masks](../../interface/masks.md), to draw and attach a mask that
  protects stars or background.
- [Process Windows](../../interface/process-windows.md), for the preview, its toolbar, the progress window and what
  every process window shares.
