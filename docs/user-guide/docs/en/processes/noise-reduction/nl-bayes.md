---
kind: process
stage: noise
menu: Process > Noise Reduction > Non-Local Bayes Filter
summary: >-
  Reduces noise by gathering the patches of the image that look alike, keeping what each group has in common, and
  refining that estimate in a second pass, with an optional strengthening loop and a Fourier finishing stage.
data_in: linear
data_out: linear
destructive: true
preview: real-time
sequences: true
masks: post-blend
external: none
glance:
  enabled: >-
    An image or a sequence view is active, no Non-Local Bayes window is open on it yet, and no window that needs the
    image for itself is open on it.
  inputs: >-
    One grayscale or RGB image or sequence, best with every sample between 0 and full scale, and an optional mask of
    the image's size on its view.
  outputs: >-
    The same view, replaced, with one history step. Its size, channels, pixel type, header and recorded processing
    state are kept, and its samples are held between 0 and full scale. No file is written.
  state: >-
    A calibrated and stacked linear master after gradient removal and color calibration, before the stretch, whose
    stars, edges and texture repeat across the frame.
derived: true
---
# NL-Bayes

NL-Bayes reduces noise by comparing each small patch of the image with the patches around it. The patches that look
alike form a group: what the group has in common is kept as signal, and the rest is treated as noise. A first pass
makes a basic estimate, and a second pass searches it again to estimate the original patches. A strengthening loop and
a Fourier finishing stage can follow, and **Amount** mixes the result with the original. The menu names the process
**Non-Local Bayes Filter**, and its window is titled **Non-Local Bayes**.

**Apply** replaces the image or [sequence](../../reference/glossary.md#sequence) with the denoised one, keeps its size
and [pixel type](../../reference/glossary.md#pixel-type), and adds one step to its
[history](../../reference/glossary.md#history).

```figure
image: ../../assets/images/processes/noise-reduction/nl-bayes.webp
alt: >-
  The Non-Local Bayes window. On the left, the Sigma box at 5.0000, grayed, with its slider, then five check boxes,
  of which Automatic Sigma Estimation and 1° Step Homogeneous Area are ticked, the SOS Iterations box at 1, SOS Rho at
  0.80 and Amount at 1.00 with their sliders, and lower down Target Image, Channel at RGB and the Preview check box. On
  the right, a toolbar of zoom buttons, a channel box, the display mode box at Autostretch, a settings button, a switch
  and a menu button over the whole nebula, with the word PREVIEW in its corner and a small framed area labelled
  Preview: Non-Local Bayes Filter in its middle. Reset, Close and Apply run along the bottom.
caption: >-
  NL-Bayes on the linear Carina master with the window's defaults, the preview shown in the Autostretch display mode:
  the framed preview area in the middle of the image is denoised, over the original.
pins:
  - {n: 2, x: 3.0, y: 82.7}
  - {n: 3, x: 3.0, y: 24.9}
  - {n: 4, x: 3.0, y: 29.4}
  - {n: 5, x: 60.5, y: 55.0}
  - {n: 6, x: 3.0, y: 47.7}
  - {n: 7, x: 3.0, y: 57.8}
```

## Before you start

- Finish calibration, registration, stacking, gradient removal and color calibration first, and denoise the
  [master](../../reference/glossary.md#master) while it is linear, before the
  [stretch](../../reference/glossary.md#stretch). The patch search keeps, and can repeat, what the frame repeats:
  walking noise, registration defects, gradients and compression texture.
- Bring the samples between 0 and full scale, where the result is held.
- **Apply** replaces the image. Duplicate it first, or use its history, which keeps the step before.
- [Debayer](../../reference/glossary.md#debayer) a [CFA](../../reference/glossary.md#cfa) mosaic first. A raw mosaic
  is denoised as one grayscale plane. The frames of a sequence are read with the automatic debayer setting of its view.
- A [mask](../../reference/glossary.md#mask) on the view blends the result with the original after **Amount**.
- On a sequence, every frame gets the same values, **Channel** and mask, and the automatic noise level is measured on
  each frame.

## Steps

1. **Open the window.** Make the linear master the [active image](../../reference/glossary.md#active-image) and choose
   **Process > Noise Reduction > Non-Local Bayes Filter**. The preview shows a framed
   [preview area](../../reference/glossary.md#preview-area) in the middle of the image, denoised with the defaults,
   and **Apply** is enabled at once.
2. **Choose what to denoise.** Leave **Channel** at **RGB** to denoise the three channels together, unless a
   comparison on a duplicate favours a component.
3. **Set the noise level.** Leave **Automatic Sigma Estimation** ticked. Untick it and type **Sigma** only when you
   know the noise, as 255 times its standard deviation.
4. **Leave the shortcuts.** Keep **1° Step Homogeneous Area** ticked and **2° Step Homogeneous Area** unticked at
   first, and change one at a time.
5. **Check at 100 %.** Move the preview area over background and fine stars, zoom the preview to 100 % and hold
   ++space++ to compare. Look for blotches, repeated texture, softened stars and color shifts.
6. **Strengthen only if needed.** When grain remains after one stable pass, set **SOS Iterations** to 2. Tick
   **Data Adaptive Dual Domain Denoise (DA3D)** only as a last refinement, with **Sigma** set by hand.
7. **Set the strength.** Lower **Amount** until the result no longer looks smoothed.
8. **Apply.** Choose **Apply**. A progress window follows the passes, the view shows the denoised image, and its
   history lists **Non-Local Bayes** with the values.

## Controls

**Target Image**, the preview pane, **Close** and the progress window work as in every process window: see
[Process Windows](../../interface/process-windows.md). What is particular to this window is below.

```controls
window: Non-Local Bayes
controls:
  - name: Sigma
    kind: spinbox
    default: "5.0000"
    widget: {min: 0.001, max: 50, step: 0.1, decimals: 4}
    effect: Sets the standard deviation of the noise by hand, on a scale where full scale is 255. Higher treats more of the variation as noise and smooths more. Lower keeps more detail and more noise.
    range: The slider moves it in 1,000 equal steps of about 0.05. Full scale is 1 for a floating-point image and the type's largest value for an integer one, so a value means the same in every pixel type. The level also chooses, from the method's tables, the patches, the groups and the search window. For an RGB image the first pass compares patches of 3 by 3 pixels below 20, 5 by 5 below 50 and 7 by 7 above, and the second pass 3 by 3 below 50, 5 by 5 below 70 and 7 by 7 above. A group holds three times as many patches as a patch has pixels, in a search window of half that, made odd, so 27 patches in 13 pixels for 3 by 3. For one channel the patches are 5 by 5, or 7 by 7 in the first pass from 30, in groups of 15 to 100. The step between reference patches and the weight of the estimate follow from the same choice.
    enabled: Disabled while **Automatic Sigma Estimation** is ticked, and while Apply runs.
    tradeoff: A value far from the image's noise erases detail or leaves noise. Crossing 20 or 50, or 30 for one channel, changes the patches, the groups and the search window at once, so a small change can change the texture of the result. Larger values use larger patches and windows, which cost more time. In the second pass of an RGB image the covariance has three times a patch's area on each side, so a larger patch costs much more there.
    scope: The preview and Apply, on what **Channel** selects. **Data Adaptive Dual Domain Denoise (DA3D)** runs at this value even while the box is disabled. With **Anscombe Variance-Stabilizing Transform** ticked, the estimator ignores it.
    persistence: Until the window closes. **Reset** and a finished Apply return it to 5.
  - name: Automatic Sigma Estimation
    kind: check
    default: "On"
    effect: Measures the noise and uses it in place of **Sigma**, from the differences between neighbouring samples along each row. It measures each input of the SOS loop again, and each frame of a sequence.
    range: The differences of each row are clipped three times at five standard deviations, the median of the rows' deviations divided by the square root of 2 is a channel's level, and the channels' levels are averaged. It is not a median absolute deviation, and it stands a few outliers in a row. A difference that involves a sample that is not a finite number, or one the image marks as invalid, is skipped.
    enabled: Disabled while Apply runs.
    tradeoff: It is no model of the camera. Horizontal structure, a small image, noise that changes across the frame and channels of unequal noise can bias it, and the frames of a sequence can get different patch sizes. The level it measures is shown nowhere.
    scope: The preview and Apply, on what **Channel** selects. It does not reach **Data Adaptive Dual Domain Denoise (DA3D)**, which runs at **Sigma**.
    persistence: Until the window closes. **Reset** and a finished Apply tick it again.
  - name: 1° Step Homogeneous Area
    kind: check
    default: "On"
    effect: In the first pass, replaces a group whose spread is within the noise by its mean in each channel, without the covariance estimate.
    range: A group counts as featureless when its variance, averaged over its channels, is below 1.05 times the square of the noise level.
    enabled: Disabled while Apply runs.
    tradeoff: Smooth areas become flatter, and the pass is quicker there. Faint structure of low contrast can be taken for a featureless area and flattened.
    scope: The preview and Apply, on what **Channel** selects. It does not depend on the second-pass box.
    persistence: Until the window closes. **Reset** and a finished Apply leave it as it is. It is ticked when the window opens.
  - name: 2° Step Homogeneous Area
    kind: check
    default: "Off"
    effect: Takes the same shortcut in the second pass. The variance of the noisy group is tested, and the group of the basic estimate is replaced by its mean in each channel.
    enabled: Disabled while Apply runs.
    tradeoff: More smooth areas turn into flat patches, and faint signal can be lost. It saves covariance work.
    scope: The preview and Apply, on what **Channel** selects. It does not depend on the first-pass box.
    persistence: Until the window closes. **Reset** and a finished Apply leave it as it is. It is unticked when the window opens.
  - name: Data Adaptive Dual Domain Denoise (DA3D)
    kind: check
    default: "Off"
    effect: Runs a finishing stage after the patch estimate. Around the pixels least covered so far, it takes large patches of the original image and shrinks each of their frequencies by how much power the estimate has there against the noise. The estimate guides it, and the original is what it filters.
    range: Its patches are transformed at 64 by 64 pixels, and its other settings are fixed. The image, or the preview area, must be at least 64 pixels each way.
    enabled: Disabled, and not run, while **Anscombe Variance-Stabilizing Transform** is ticked. Disabled while Apply runs.
    tradeoff: It can bring back detail and soften steps the estimate left, and can add Fourier or patch artifacts of its own. It adds large buffers and time.
    scope: The preview and Apply, on what **Channel** selects, before **Amount** and the mask. It runs at the value of **Sigma**, 5 when the window opens, even while **Automatic Sigma Estimation** is ticked.
    persistence: Until the window closes. **Reset** and a finished Apply leave it as it is.
  - name: Anscombe Variance-Stabilizing Transform
    kind: check
    default: "Off"
    effect: Transforms the samples so that noise which grows with the signal, as shot noise does, has one strength everywhere, runs one pass of the estimator, and transforms back with a closed-form inverse made for low counts.
    range: The transform assumes pure shot noise at a gain of 1, with no read noise and no offset. The camera's gain, offset and read noise are not read from the image. The noise is measured on the transformed image, whether **Automatic Sigma Estimation** is ticked or not.
    enabled: Disabled while Apply runs. Ticked, it disables **Data Adaptive Dual Domain Denoise (DA3D)**, **SOS Iterations** and **SOS Rho**, and the run uses one pass without the finishing stage.
    tradeoff: On data whose noise does not follow the model, levels, faint signal and detail can change. The way back is biased at very low counts, and the result is held between 0 and full scale.
    scope: The preview and Apply, on what **Channel** selects.
    persistence: Until the window closes. **Reset** and a finished Apply leave it as it is.
  - name: SOS Iterations
    kind: spinbox
    default: "1"
    widget: {min: 1, max: 8, step: 1}
    effect: Sets how many passes the strengthen-operate-subtract loop runs. At 1 the estimator runs once. Above 1, each pass denoises a mix of the last answer and the original, and the mix is subtracted back out before the next.
    range: Eight is the most the engine runs. The loop stops early, and keeps the answer of the pass before, when the measured noise of a new input is more than 1 % above the last one's.
    enabled: Disabled while **Anscombe Variance-Stabilizing Transform** is ticked, and while Apply runs.
    tradeoff: Each pass costs as much as a whole run at 1, and above 1 the engine holds two more copies of the image. Later passes can amplify artifacts or diverge, which the 1 % rule stops.
    scope: The preview and Apply, on what **Channel** selects. With **Automatic Sigma Estimation**, each pass runs at the noise measured on its own input.
    persistence: Until the window closes. **Reset** and a finished Apply return it to 1.
  - name: SOS Rho
    kind: spinbox
    default: "0.80"
    widget: {min: 0.01, max: 0.99, step: 0.05, decimals: 2}
    effect: Sets the share of the last answer mixed into each new input of the loop. Higher moves faster from one pass to the next and strengthens the subtraction. Lower stays closer to the original.
    range: The slider moves it in 1,000 steps, which the box shows to 0.01.
    enabled: Disabled while **Anscombe Variance-Stabilizing Transform** is ticked, and while Apply runs. It changes nothing while **SOS Iterations** is 1, and the history records it all the same.
    tradeoff: Near 0.99 the subtraction divides by a small number, and the loop can diverge or leave halos.
    persistence: Until the window closes. **Reset** and a finished Apply return it to 0.80.
  - name: Amount
    kind: spinbox
    default: "1.00"
    widget: {min: 0, max: 1, step: 0.05, decimals: 2}
    effect: Mixes the denoised image with the original. At 1 the result is the denoised image, at 0.5 halfway, and at 0 the original.
    range: The slider moves it in 1,000 steps, which the box shows to 0.01.
    enabled: Disabled while Apply runs.
    tradeoff: Lower brings back signal and noise together. At 0 the whole estimate still runs and costs as much, and any value below 1 adds a blend of the whole image.
    scope: The preview and Apply, after **Channel** has been converted back and before the mask.
    persistence: Until the window closes. **Reset** and a finished Apply return it to 1.
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
    effect: Chooses what is denoised. **RGB** hands the estimator the three channels together, with the tables for color. **Red**, **Green** or **Blue** denoises that channel and keeps the other two. Every other choice denoises one component, with the tables for one channel, and converts the image back.
    enabled: For an RGB image. A grayscale image shows a disabled **Grayscale**. Disabled while Apply runs.
    tradeoff: With **RGB** the channels share one measured noise level and, in the second pass, one covariance. Shared structure is kept, and color can move where the channels' noise or structure differ. A component costs two conversions and a working copy, hue and opponent components are filtered as plain values, and colors the conversion back puts out of range are cut in an integer image and kept in a floating-point one.
    scope: The preview and Apply, before **Amount** and the mask. Process Windows describes each component.
    persistence: Until the window closes. **Reset** leaves it as it is. A finished Apply and a new target return it to **RGB**.
  - name: Preview
    kind: check
    default: "On"
    effect: Shows the denoised image beside the controls, with **Amount** and the mask applied.
    range: >-
      The pane opens with a **Preview area** of 500 by 500 pixels in the middle of the image, labelled "Preview:
      Non-Local Bayes Filter", or the whole image when it is smaller. At the window's default size, **Preview area**
      and **Take snapshot** are in the menu at the end of the pane's toolbar.
    tradeoff: Every change runs the whole estimate again, patch search included, so without the area each change on a large image takes long. An Autostretch display can exaggerate leftover noise and artifacts.
    scope: The window only. The area is denoised as an image of its own, so near its edges the search sees nothing of the image around it. Judge at **Zoom 100%**, since a fitted view hides patch artifacts and lost faint stars. The pane offers **Display Mode**.
    persistence: Until the window closes. **Reset** leaves it as it is.
  - name: Reset
    kind: button
    default: ""
    effect: Returns **Sigma**, **Automatic Sigma Estimation**, **SOS Iterations**, **SOS Rho** and **Amount** to their defaults.
    range: It leaves the two homogeneous-area boxes, **Data Adaptive Dual Domain Denoise (DA3D)**, **Anscombe Variance-Stabilizing Transform** and **Channel** as they are. Close the window and open it again to have them as it opens.
    enabled: Disabled while Apply runs.
  - name: Apply
    kind: button
    default: ""
    effect: Denoises the whole image, or every frame of the sequence, with the values and **Channel** as they are when you press it, then returns the values **Reset** returns to their defaults.
    enabled: From the moment the window opens, at any values. Disabled while a run works.
    tradeoff: >-
      A single image shows a progress window too, with **Cancel**. Its bar moves as each pass over the image ends: two
      for each pass of the SOS loop and one for DA3D. The estimate holds three single-precision copies of the image,
      five with **SOS Iterations** above 1, and takes all its working memory before the first patch, so a run the
      memory limit cannot hold is refused before it starts.
    persistence: >-
      The history step is **Non-Local Bayes** and reads, for example, `Sigma: auto, Step 1 Homogeneous Area: true,
      Step 2 Homogeneous Area: false, DA3D: false, Anscombe: false, SOS: 1 (rho=0.80), Amount: 1.00 in channel: RGB`.
      With **Anscombe Variance-Stabilizing Transform** ticked it records DA3D as false and SOS as 1, as the run used
      them. For a grayscale image it ends after the amount. The measured noise, the patch sizes, an early stop of the
      loop, DA3D's fixed settings, the mask, the preview area, the display mode and the engine's version are not
      recorded. The header and the recorded processing state are kept.
```

## How it works

1. The samples **Channel** selects are read in single precision and scaled so that full scale is 255, the scale of
   the method's tables. The noise level is measured there, or taken from **Sigma**.
2. For RGB, the first pass works on a luminance and two opponent channels. For each reference patch it keeps a fixed
   number of the closest patches by luminance, estimates each channel's group from its own covariance, and averages
   the overlapping patches into a basic estimate.
3. The second pass converts that estimate back to RGB, searches it with all three channels, keeps every patch closer
   than a threshold, and estimates the original patches under the joint covariance of the basic ones. A pixel no
   group covers keeps the value its pass started from.
4. The SOS loop, the Anscombe transform and DA3D wrap these passes as their boxes ask. The result is held between 0
   and 255, scaled back and written in the image's own pixel type, rounded to the nearest value.
5. **Amount** and then a mask on the view mix the result with the original. The image replaces the target, its
   history gets one step, and its header and recorded
   [processing state](../../reference/glossary.md#processing-state) are kept.

### Model

With \(s\) the standard deviation of the noise as a
[fraction of full scale](../../reference/glossary.md#fraction-of-full-scale), the value **Sigma** stands for is

\[ \sigma = 255\,s \tag{1} \]

so a noise of 0.02 is a **Sigma** of 5.1. The level chooses the patches, the groups and the search window, which
**Sigma** lists.

With **Automatic Sigma Estimation**, \(s_{c,r}\) is the clipped standard deviation of the differences between
neighbours along row \(r\) of channel \(c\), and for \(K\) channels the level is

\[ \hat\sigma = \frac{1}{K\sqrt{2}} \sum_{c=1}^{K} \operatorname{median}_r\, s_{c,r} \tag{2} \]

With \(\sigma\) the level in use, \(\beta\) a weight near 1 from the same tables and \(I\) the identity, a group of
noisy patches \(y_i\) with mean \(m\) and covariance \(C_y\) has each patch estimated in the first pass as

\[ \hat x_i = m + \bigl(I - \beta\sigma^2 C_y^{-1}\bigr)(y_i - m) \tag{3} \]

and the second, with \(C_b\) the covariance of the same patches in the basic estimate, as

\[ \hat x_i = m + \Bigl[I - \beta\sigma^2\bigl(C_b + \beta\sigma^2 I\bigr)^{-1}\Bigr](y_i - m) \tag{4} \]

Where the noise is large against the group's spread, the patches move toward their mean, and strong structure stays. A
group whose variance, averaged over its channels, is below \(1.05\,\sigma^2\) is replaced by its mean when the box of
its pass is ticked. A covariance that cannot be inverted leaves its group as it was.

With \(f\) the original, \(q_k\) the loop's state, which starts at \(f\), \(\rho\) the value of **SOS Rho** and
\(d_k\) what pass \(k\) returns, each pass denoises \(u_k\), and the next state is

\[ u_k = \rho\,q_k + (1 - \rho)\,f, \qquad q_{k+1} = \frac{d_k - \rho\,u_k}{1 - \rho} \tag{5} \]

The last pass's \(d_k\) is the answer. The Anscombe transform lifts the samples to a 16-bit range and maps them through
\(2\sqrt{x + 3/8}\) before its one pass, and back afterwards. With \(a\) the value of **Amount** and \(d\) the denoised
sample, the sample written before the mask is

\[ b = (1 - a)\,f + a\,d \tag{6} \]

### Boundaries

- Every pixel type is computed in single precision, so a 64-bit image loses its extra precision.
- An image, or a preview area, narrower than one search window is refused with "this noise level needs a frame at
  least one search window across", or "the measured noise level" in its place. The window is 13 to 73 pixels, as the
  level chooses. DA3D needs 64: "the finishing stage needs a frame at least one transform across".
- A sample that is not a finite number is neither refused nor repaired: the noise measure skips it, and it enters the
  patches around it. A measured level that is not a finite number stops the run. Pixels the image marks as invalid
  are read as 0 and left as they were.
- The image is cut into tiles by its own size, each at least four margins across, with mirrored margins that are
  discarded. The result does not depend on the number of processor threads. The window is made for images of one or
  three channels.
- **Cancel**, or closing the window, is noticed between tiles, between passes and inside DA3D. A cancelled or failed
  run writes nothing.

### Worked example

The pair below is made on a crop of 1,100 by 820 pixels from the middle of the linear master, whose noise from one
pixel to the next is about 0.0003 in red, 0.001 in green and 0.0007 in blue. The window keeps its defaults, with
**Channel** at **RGB**. If equation (2) measures those levels, it averages them to about 0.00067, which is 0.17 by
equation (1). Any level below 20, a noise of 0.078, gives this RGB image 3 by 3 patches in both passes, groups of 27
and a search window of 13 pixels. The few star cores above 1 come back at 1. The crop and the result are then
stretched by [Histogram Transformation](../stretching/histogram-transformation.md) with **Shadows** and **Midtones**
at 0.02, and shown in Linear.

```figure
images:
  - image: ../../assets/images/processes/noise-reduction/noise-crop-before.webp
    alt: >-
      A crop at 100 % of the middle of the master, stretched: pink clouds of nebula beside a dark dust lane, many small
      stars, and a fine colored grain over the background.
    label: Before
  - image: ../../assets/images/processes/noise-reduction/nl-bayes-after.webp
    alt: >-
      The same crop denoised with the window's defaults before the same stretch: the grain of the background and of
      the nebula smoothed, the stars and the edges of the dust lanes kept.
    label: After
caption: >-
  The middle of the master at 100 %, without and with NL-Bayes at the window's defaults, both then stretched by
  Histogram Transformation with Shadows and Midtones at 0.02 and shown in the Linear display mode.
```

With **SOS Iterations** at 2, a sample of 0.0300 that the first pass returns as 0.0290 gives
\(q_1 = (0.0290 - 0.8 \times 0.0300)/0.2 = 0.0250\) by equation (5), and the second pass denoises
\(u_1 = 0.0260\). With **Amount** at 0.50, a sample of 0.0300 denoised to 0.0290 is written as 0.0295 by equation (6).

## Starting points

| Situation | Start | Then adjust |
| --- | --- | --- |
| A linear master, first try | The defaults, on the preview area | Check at 100 %, then set the strength with **Amount**. |
| The automatic level smooths too much or too little | **Sigma** by hand, near 255 times the noise | Lower it when detail softens. Check again after crossing 20, 30 or 50. |
| Grain remains after one stable pass | **SOS Iterations** at 2, **SOS Rho** between 0.5 and 0.8 | Add one pass at a time, and lower **SOS Rho** when halos or growing noise appear. |
| One channel stays noisy, or colors shift | **RGB** and **CIE L* (Lightness)**, compared on a duplicate | Then a feathered mask, and **Amount**. |

## When it goes wrong

!!! danger "Samples outside 0 to full scale are cut"
    A negative background left by a subtraction comes back as 0, and star cores above 1 come back as 1, at an
    **Amount** of 1. Offset or rescale the image into range before you denoise it.

!!! warning "The automatic level reaches neither DA3D nor the Anscombe pass"
    DA3D runs at the value of **Sigma**, 5 when the window opens, even while **Sigma** is disabled: set **Sigma** to
    the noise before you tick it, and compare with DA3D off. The Anscombe pass measures its own level, so **Sigma**
    changes nothing there: set the strength with **Amount**. Note which boxes you ticked to repeat a result.

**Sigma** is grayed out
:   **Cause.** **Automatic Sigma Estimation** is ticked.
:   **Fix.** Untick it to type a level.

**Data Adaptive Dual Domain Denoise (DA3D)**, **SOS Iterations** and **SOS Rho** are grayed out
:   **Cause.** **Anscombe Variance-Stabilizing Transform** is ticked, which runs one pass without DA3D.
:   **Fix.** Untick it to use them.

**Reset** leaves boxes ticked
:   **Cause.** **Reset** returns only **Sigma**, the automatic level, the loop and **Amount**.
:   **Fix.** Untick the homogeneous-area boxes, DA3D or the transform yourself, or close the window and open it again.

The background turns flat, waxy or patchy
:   **Cause.** The featureless shortcut, a high level, several passes or DA3D removed real variation.
:   **Fix.** Untick **2° Step Homogeneous Area**, then the first one. Lower **Sigma** or **SOS Iterations**, untick
    DA3D, or lower **Amount**.

Colors shift, or fine color detail disappears
:   **Cause.** With **RGB** the channels share one level and one covariance, or a component was converted back.
:   **Fix.** Compare **RGB** and **CIE L* (Lightness)** on a duplicate, and lower **Sigma** or **Amount**.

"The preview failed", with "needs a frame at least one search window across"
:   **Cause.** The preview area, or the image, is narrower than the search window the level chooses.
:   **Fix.** Enlarge the preview area, or lower **Sigma**. Apply works on the whole image.

## Related

- [Image Stacking](../stacking-integration/image-stacking.md),
  [Background Extraction](../background-extraction/background-extraction.md) and
  [Photometric Color Calibration](../color-calibration/photometric-cc.md), which come before.
- [Wavelet Denoise](wavelet-denoise.md) and [Total Variation Denoise](total-variation-denoise.md), other linear
  denoisers.
- [Patch PCA Denoise](patch-pca-denoise.md), which models every patch of a window, and [DeepSNR](deepsnr.md), a
  learned model.
- [Histogram Transformation](../stretching/histogram-transformation.md), the stretch that follows.
- [Masks](../../interface/masks.md) and [Image History](../../interface/image-history.md), to protect a region and to
  undo the step.
- [Process Windows](../../interface/process-windows.md), for the preview and what every process window shares.

## Credits

- Marc Lebrun, Antoni Buades and Jean-Michel Morel,
  ["Implementation of the Non-Local Bayes (NL-Bayes) Image Denoising Algorithm"](https://www.ipol.im/pub/art/2013/16/),
  Image Processing On Line, 2013: the method, its parameter tables and its two estimates.
- The engine's patch estimator is a derived work of Marc Lebrun's implementation published with that article,
  Copyright (c) 2013, Marc Lebrun, under GPL-3.0-or-later, and is distributed under that licence. Its tables, color
  decorrelation, patch search, featureless shortcut, covariance estimate and aggregation follow that implementation.
  The tiling, the memory, the noise measure, the strengthening loop and the transform are AfterNight's.
  **About > About AfterNight** credits it.
- Yaniv Romano and Michael Elad,
  ["Boosting of Image Denoising Algorithms"](https://epubs.siam.org/doi/10.1137/140990978), SIAM Journal on Imaging
  Sciences, 2015, for the strengthen-operate-subtract loop.
- Nicola Pierazzo and Gabriele Facciolo,
  ["DA3D: Data Adaptive Dual Domain Denoising"](https://www.ipol.im/pub/art/2017/203/), Image Processing On Line, 2017.
  The finishing stage is written from the published method, and carries none of its code.
- Markku Mäkitalo and Alessandro Foi,
  ["A Closed-Form Approximation of the Exact Unbiased Inverse of the Anscombe Variance-Stabilizing Transformation"](https://doi.org/10.1109/TIP.2011.2121085),
  IEEE Transactions on Image Processing, 2011, for the way back from the transform.
