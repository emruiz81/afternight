---
kind: process
stage: noise
menu: Process > Multiscale > Multiscale Linear Transform
summary: >-
  Splits each channel into detail layers of growing scale and a residual, lets you weight, denoise and dering each
  layer, and adds them back: noise reduction and detail work scale by scale.
data_in: linear
data_out: linear
destructive: true
preview: real-time
sequences: true
masks: post-blend
external: none
glance:
  enabled: >-
    An image or a sequence view is active, no Multiscale Linear Transform window is open on it yet, and no window that
    needs the image for itself is open on it.
  inputs: >-
    One grayscale or RGB image or sequence of any pixel type, and an optional mask of the image's size on its view.
  outputs: >-
    The same view, replaced, with one history step. Its size, channels, pixel type, header and recorded processing
    state are kept, and every sample is held between 0 and 1. An Apply to a single image can also open two deringing
    maps.
  state: >-
    A calibrated and stacked linear master, after gradient removal and color calibration, for noise reduction by scale.
    Detail work by scale suits a stretched image too.
derived: false
---
# Multiscale Linear Transform

Multiscale Linear Transform splits each channel of the image into detail layers, each holding the structure of one
range of sizes, and a residual that holds what is larger. You weight each layer, leave it out, reduce its noise or
dering its edges, and the window adds the layers back. It offers two transforms, neither of which shrinks a layer: the
multiscale linear transform, which blurs the original with ever wider Gaussians, and the
[starlet transform](../../reference/glossary.md#starlet-transform), which blurs it again and again with a small kernel.

**Apply** replaces the image or [sequence](../../reference/glossary.md#sequence) with the result, keeps its size and
[pixel type](../../reference/glossary.md#pixel-type), and adds one step to its
[history](../../reference/glossary.md#history).

```figure
image: ../../assets/images/processes/multiscale/multiscale-linear.webp
alt: >-
  The Multiscale Linear Transform window. On the left, the Transform Settings box with Algorithm, Detail Layers at 6,
  Layer Spacing at Dyadic, a disabled Scaling Function and Amount at 1.00; a table of Layer 1 to Layer 6 and Residual,
  each ticked with a bias of 0.00, over the Enable All, Disable All and Reset Biases buttons; and a scrolling Global
  Settings box whose k-Sigma Thresholding group is ticked, with Threshold (k) at 3.00 and Amount at 0.70. On the right,
  a toolbar over the preview of a red nebula with the word PREVIEW in its corner and a framed area near its right edge, the
  Selected Layer box for Layer 1, Target Image, Channel at RGB, Preview with Processed Image beside it, and Reset,
  Close and Apply along the bottom.
caption: >-
  Multiscale Linear Transform on the linear master with k-Sigma Thresholding ticked and its Amount at 0.70. The framed
  area shows the result there, over the original, in the Autostretch display mode.
pins:
  - {n: 2, x: 2.2, y: 13.7}
  - {n: 3, x: 2.2, y: 38.9}
  - {n: 4, x: 66.4, y: 69.4}
  - {n: 5, x: 76.8, y: 72.0}
  - {n: 6, x: 22.5, y: 67.2}
  - {n: 8, x: 95.6, y: 93.3}
  - {n: 9, x: 89.2, y: 97.4}
```

## Before you start

- Reduce noise on the linear [master](../../reference/glossary.md#master), after calibration, stacking, gradient
  removal and color calibration, and before a [stretch](../../reference/glossary.md#stretch) raises the noise.
- Leave **Large-Scale Transfer Function** at **None** and **Dynamic Range Extension** at 0 on linear data. The first
  makes the image nonlinear and the second offsets it, and the image stays recorded as it was.
- **Apply** replaces the image. Duplicate it first, or use its history, which keeps the step before but not each
  layer's settings.
- A [mask](../../reference/glossary.md#mask) on the view blends the whole result with the original. **Linear Mask** is
  a mask made inside the window that weighs only the noise reduction.
- [Debayer](../../reference/glossary.md#debayer) a [CFA](../../reference/glossary.md#cfa) mosaic first: a raw mosaic is
  split as one grayscale plane. On a sequence, every frame gets the same values.
- Grain and fine detail lie in the first layers, broad structure in the last ones, and the background and large glows
  in the residual.

## Steps

1. **Open the window.** Make the master the [active image](../../reference/glossary.md#active-image) and choose
   **Process > Multiscale > Multiscale Linear Transform**. The preview shows the result inside a framed
   [preview area](../../reference/glossary.md#preview-area) in the middle of the image.
2. **Choose the transform.** Keep **Algorithm**, **Detail Layers** and **Layer Spacing** for a first pass. Choose
   **Starlet Transform** to find the grain of single pixels in Layer 1, or to choose a **Scaling Function**.
3. **Find the scale.** Click a row of the layer table and untick its box: the preview loses the structure of that
   size. Tick it again once you know which layer to change.
4. **Weight the layer.** Raise **Bias** a little to strengthen the layer's detail, or lower it below 0 to weaken it. A
   positive bias acts six times over or more: start at 0.05.
5. **Reduce the noise of one layer.** Tick **Noise Reduction** and raise **Threshold (σ)** until the grain of that size
   fades. Lower its **Amount** before you add **Iterations**.
6. **Or threshold every layer.** Tick **k-Sigma Thresholding** and set **Threshold (k)** and its **Amount**. Each
   detail layer loses what lies below k times its own noise.
7. **Protect and dering.** Tick **Linear Mask** to aim the noise reduction at the faint areas, and **Deringing**
   against [ringing](../../reference/glossary.md#ringing) at stars and edges.
8. **Check at 100 %.** Keep **Processed Image** beside **Preview**, zoom the preview to 100 % and hold ++space++ to
   compare with the original. Then untick **Preview area** and look at the whole image.
9. **Apply.** Choose **Apply**. A progress window follows the run, the view shows the result, and its history lists
   **Multiscale Linear Transform**.

## Controls

**Target Image**, the preview pane, **Close** and the progress window work as in every process window: see
[Process Windows](../../interface/process-windows.md). The blocks below follow the window's groups.

```controls
window: Transform Settings
controls:
  - name: Algorithm
    kind: combo
    default: Multiscale Linear Transform
    options: [Starlet Transform, Multiscale Linear Transform]
    effect: >-
      Chooses how the layers are made. **Multiscale Linear Transform** blurs the original with a Gaussian at each scale
      and keeps the difference between two blurs. **Starlet Transform** blurs the image again and again with the
      **Scaling Function**, its taps spread as far apart as the layer's scale, and keeps the difference between two
      blurs.
    range: >-
      Neither transform shrinks a layer: every layer has the image's size, and the layers add back to the channel to
      single-precision rounding. With **Dyadic** spacing the multiscale linear transform builds at most 7 detail layers
      and the starlet 10.
    tradeoff: >-
      The two split the sizes differently. Layer 1 of the multiscale linear transform keeps about a twentieth of a noise
      that changes from pixel to pixel, and its Layer 2 about two thirds. Layer 1 of the starlet keeps nine tenths of
      it. The Gaussians of the multiscale linear transform widen with the scale, so its deep layers cost more, while the
      starlet's cost the same at every scale.
    scope: The preview and Apply, on what **Channel** selects. A change builds the layers and their noise estimates again.
    persistence: Until the window closes. **Reset** and a finished Apply return it to its default.
  - name: Detail Layers
    kind: spinbox
    default: "6"
    widget: {min: 1, max: 11, step: 1}
    effect: >-
      Sets how many detail layers the image is split into, finest first. The residual comes after them and is not
      counted. More layers reach larger structure, and the residual then holds only what is larger still.
    range: >-
      The deepest layer must fit the transform: a Gaussian radius of at most 128 pixels for the multiscale linear
      transform, taps at most 512 pixels apart for the starlet. With **Dyadic** spacing that allows 7 layers for the
      multiscale linear transform and 10 for the starlet.
      With **Linear** spacing the starlet takes all 11 at any **Linear Scale**, and the multiscale linear transform
      takes as many as keep (layers - 1) × **Linear Scale** at 95 or less. A deeper request is refused, never narrowed.
    tradeoff: >-
      Each layer adds work and holds a full copy of every channel in memory, also while its row is unticked.
    scope: >-
      The preview and Apply. The table gets a row for each layer. The rows keep their settings by position: raising the
      value turns the residual's row into the new last detail layer, and every row added starts ticked, with a bias of
      0, its noise reduction off and an **Amount** of 0.50. Lowering it turns the last detail layer kept into the
      residual.
    persistence: Until the window closes. **Reset** and a finished Apply return it to its default.
  - name: Layer Spacing
    kind: combo
    default: Dyadic
    options: [Dyadic, Linear]
    effect: >-
      Sets the scale of each layer, in pixels. **Dyadic** doubles it from one layer to the next: 1, 2, 4, 8 and so on.
      **Linear** adds **Linear Scale** each time: 1, 1 + q, 1 + 2q, with q its value.
    enabled: "**Linear Scale** shows only with **Linear**."
    tradeoff: >-
      Linear scales split the sizes more finely, and their layers overlap more. More of them fit below the limits of
      **Detail Layers**.
    scope: The preview and Apply.
    persistence: Until the window closes. **Reset** and a finished Apply return it to its default.
  - name: Linear Scale
    kind: spinbox
    default: "1"
    widget: {min: 1, max: 16, step: 1}
    effect: >-
      Sets the step between the scales of two layers under **Linear** spacing. Higher separates the scales faster and
      reaches large structure with fewer layers.
    enabled: Hidden unless **Layer Spacing** is **Linear**.
    tradeoff: >-
      A higher step reaches the depth limit of the multiscale linear transform with fewer layers. It does not change
      how many taps the starlet's kernel has, only how far apart they are.
    scope: The preview and Apply.
    persistence: Until the window closes. **Reset** and a finished Apply return it to its default.
  - name: Scaling Function
    kind: combo
    default: B3 Spline (5)
    options: [Linear Interpolation (3), B3 Spline (5), Gaussian (5), Gaussian (7), Gaussian (9), Gaussian (11), Small Scale 3 (3), Small Scale 4 (3), Small Scale 5 (3), Small Scale 6 (3), Small Scale 8 (3), Small Scale 12 (3), Small Scale 16 (3), Small Scale 24 (3), Small Scale 32 (3)]
    effect: >-
      Chooses the kernel the starlet blurs with. Each sums to 1, so a flat area gives layers of 0. **Linear Interpolation
      (3)** weighs 1, 2, 1 over 4, and **B3 Spline (5)** 1, 4, 6, 4, 1 over 16. **Gaussian (5)** to **Gaussian (11)**
      are Gaussians of that many taps, with a deviation of a third of their radius. **Small Scale n (3)** weighs 1, n, 1
      over n + 2: the higher n, the less each blur takes from the neighbours.
    enabled: >-
      With **Algorithm** at **Starlet Transform** only. The multiscale linear transform builds its Gaussians from the
      scales and ignores it.
    tradeoff: A wider kernel blurs more at each layer and costs more taps.
    scope: The preview and Apply of the starlet transform.
    persistence: Until the window closes. **Reset** and a finished Apply return it to its default.
  - name: Amount
    object: amountSpinBox
    kind: spinbox
    default: "1.00"
    widget: {min: 0, max: 1, step: 0.05, decimals: 2}
    effect: >-
      Mixes the result with the original. At 1 the result is the transform's, at 0.5 halfway, and at 0 the original.
    range: The slider moves the value in 1,000 equal steps.
    tradeoff: >-
      It weakens every change together: bias, noise reduction, deringing and the residual's curve. The transform runs in
      full at any value.
    scope: >-
      The preview and Apply, on the whole image once **Channel** has been converted back, before the mask on the view.
      In the preview it also mixes the original into a layer view and into **Preview Mask**.
    persistence: Until the window closes. **Reset** and a finished Apply return it to its default.
```

```controls
window: The layer table
controls:
  - name: En
    kind: check
    default: "On"
    effect: >-
      Keeps the row's layer in the result while ticked, and leaves it out while not. It is the **Enabled** box of
      **Selected Layer** for that row.
    tradeoff: >-
      Leaving a detail layer out removes every structure of that size, real or noise. Leaving the residual out removes
      the image's base, and what is left is the detail around 0.
    scope: The preview and Apply. A layer view of the preview ignores it.
    persistence: Until the window closes. **Reset** and a finished Apply tick every row again.
    note: Its header's tooltip reads "Enable/Disable layer".
  - name: Layer
    kind: radio
    default: Layer 1
    options: [Layer 1, Layer 2, Layer 3, Layer 4, Layer 5, Layer 6, Residual]
    effect: >-
      Click a row to choose the layer that **Selected Layer** edits and that the layer views of the preview show. One
      row is chosen at a time, and a new preview follows the choice.
    range: One row for each detail layer, finest first, and **Residual** last.
    persistence: >-
      Until the window closes. **Reset** and a finished Apply choose Layer 1 again, and so does a change of **Detail
      Layers** that removes the chosen row.
  - name: Bias
    kind: readout
    default: "0.00"
    effect: Shows the row's **Bias**, which you set under **Selected Layer**, not in the table.
  - name: NR
    kind: readout
    default: ""
    effect: Shows a dot while the row's **Noise Reduction** is ticked, and a dash for the residual, which has none.
    note: Its header's tooltip reads "Noise Reduction enabled".
```

```controls
window: Layers
controls:
  - name: Enable All
    kind: button
    default: ""
    effect: Ticks every row of the table, the residual included.
    range: The biases and the noise reduction of every row stay as they are.
    enabled: Disabled while Apply runs, as is every control of the window's groups.
    note: Its tooltip reads "Enable all layers".
  - name: Disable All
    kind: button
    default: ""
    effect: Unticks every row, the residual included.
    tradeoff: >-
      The layers then add up to 0, before **Deringing** and **Dynamic Range Extension** act, and **Amount** and the mask
      mix the original back. It is of use only to tick layers back one at a time and see them alone.
    note: Its tooltip reads "Disable all layers".
  - name: Reset Biases
    kind: button
    default: ""
    effect: Sets the bias of every row to 0.
    range: It leaves the ticks and the noise reduction of every row as they are.
    note: Its tooltip reads "Reset all layer biases to 0".
```

```controls
window: Global Settings
controls:
  - name: Linear Mask
    kind: check
    default: "Off"
    effect: >-
      Weighs the noise reduction pixel by pixel with a mask made from the channel itself: the channel times
      **Amplification**, held between 0 and 1, turned round while **Inverted** is ticked, blurred by **Smoothness** and
      held between 0 and 1 again. Where the mask is white the thresholds act in full, and where it is black not at all.
    enabled: >-
      It acts only while the **Noise Reduction** of a layer or **k-Sigma Thresholding** is on. Otherwise it is not built
      and changes nothing, though **Preview Mask** still shows it.
    tradeoff: It adds the making and the blur of a mask for each channel.
    scope: >-
      The **Amount** of every layer's noise reduction and of **k-Sigma Thresholding**, multiplied by the mask. It does
      not weigh **Bias**, **Deringing**, the **Large-Scale Transfer Function** or the **Dynamic Range Extension**, and it
      is not the mask on the target's view, which blends the whole result afterwards.
    persistence: Until the window closes. **Reset** and a finished Apply untick it.
    note: >-
      With **Inverted** ticked and **Amplification** at 100, every sample at or above 0.01 gets no noise reduction,
      which is nearly the whole of a linear master.
  - name: Amplification
    kind: spinbox
    default: "100"
    widget: {min: 1, max: 300, step: 1}
    effect: >-
      Multiplies the channel before the mask is held to 1. Higher sends more samples to full weight: every sample at 1
      divided by this value or above. Lower keeps the mask following the brightness over a wider range.
    range: >-
      The slider moves the value one unit at a time. At 10, a background at 0.028 weighs 0.28, or 0.72 once inverted.
    tradeoff: Too high a value makes most of the mask white, or black once inverted, and the mask no longer tells tones apart.
    scope: The mask of **Linear Mask** and **Preview Mask**.
    persistence: Until the window closes. **Reset** and a finished Apply return it to its default.
  - name: Smoothness
    kind: spinbox
    default: "1.00"
    widget: {min: 0, max: 10, step: 0.1, decimals: 2}
    effect: >-
      Blurs the mask with a Gaussian of this deviation, in pixels. Higher softens its transitions. At 0 it is not
      blurred.
    tradeoff: A larger value costs a wider blur.
    scope: The mask of **Linear Mask** and **Preview Mask**.
    persistence: Until the window closes. **Reset** and a finished Apply return it to its default.
  - name: Preview Mask
    kind: check
    default: "Off"
    effect: >-
      Shows the mask of each channel in the preview in place of the result, white where the thresholds act in full.
    enabled: While **Linear Mask** is ticked. It comes before the layer views of the list beside **Preview**.
    tradeoff: >-
      **Amount** and the mask on the target's view act on it afterwards, as on any preview, so below an **Amount** of 1,
      or under a view mask, the original shows through. Set **Amount** to 1, disable the view's mask, and choose
      **Linear** in the pane's **Display Mode** to read it.
    scope: The preview and **Take snapshot** only. **Apply** writes the result, not the mask.
    persistence: Until the window closes. **Reset** and a finished Apply untick it.
  - name: Inverted
    kind: check
    default: "On"
    effect: >-
      Turns the mask round before it is blurred: 1 minus the amplified channel. Ticked, the faint parts get the full
      thresholds and the bright ones are spared. Unticked, the bright parts get them.
    scope: The mask of **Linear Mask** and **Preview Mask**.
    persistence: Until the window closes. **Reset** and a finished Apply tick it again.
  - name: k-Sigma Thresholding
    kind: check
    default: "Off"
    effect: >-
      Shrinks the coefficients of every detail layer by one rule: those smaller than **Threshold (k)** times the
      layer's own noise become 0, and with **Soft Threshold** the others move that much closer to 0. The residual is
      left alone.
    range: It runs after each layer's own **Noise Reduction**, so the two add up on a layer that has both.
    enabled: It does nothing while **Threshold (k)** or its **Amount** is 0.
    tradeoff: Faint real detail below the threshold goes with the noise.
    scope: The preview and Apply, on every detail layer, weighed by **Linear Mask** while that is ticked.
    persistence: Until the window closes. **Reset** and a finished Apply untick it.
  - name: Threshold (k)
    kind: spinbox
    default: "3.00"
    widget: {min: 0, max: 10, step: 0.1, decimals: 2}
    effect: >-
      Sets the threshold in deviations of each layer's own noise. Higher removes stronger coefficients too, noise and
      detail alike. Lower keeps more of both.
    range: >-
      With **Use MRS** it also bounds the coefficients the noise is measured on. When the median of Layer 1 is 0, each
      layer's noise is measured on its own coefficients.
    scope: Every detail layer, in the preview and Apply.
    persistence: Until the window closes. **Reset** and a finished Apply return it to its default.
  - name: Amount
    object: ksigmaAmountSpinBox
    kind: spinbox
    default: "1.00"
    widget: {min: 0, max: 1, step: 0.05, decimals: 2}
    effect: >-
      Mixes the thresholded coefficients with the ones before. At 1 the threshold acts in full and at 0 not at all. In
      between, a coefficient below the threshold keeps 1 minus this value of itself.
    tradeoff: Lower keeps more of every coefficient, grain and detail together.
    scope: Every detail layer, multiplied by the mask of **Linear Mask** while that is ticked.
    persistence: Until the window closes. **Reset** and a finished Apply return it to its default.
  - name: Soft Threshold
    kind: check
    default: "On"
    effect: >-
      Ticked, a coefficient below the threshold becomes 0 and a larger one moves closer to 0 by the threshold. Unticked,
      a coefficient below the threshold becomes 0 and the others are kept whole, one exactly at the threshold included.
    tradeoff: Soft is smoother and weakens all the structure it keeps a little. Hard keeps strong structure whole and can look harsher.
    scope: "**k-Sigma Thresholding** only. The **Noise Reduction** of a layer is always soft."
    persistence: Until the window closes. **Reset** and a finished Apply tick it again.
  - name: Use MRS
    kind: check
    default: "Off"
    effect: >-
      Measures the noise of Layer 1 again, three times, each time on the coefficients no larger than **Threshold (k)**
      times the last estimate, so that structure counts less in it.
    tradeoff: It adds passes over Layer 1. It changes only the noise estimate and keeps no map of where structure lies.
    scope: >-
      The noise of every layer, for the layers' own **Noise Reduction** too, while **k-Sigma Thresholding** is ticked.
    persistence: Until the window closes. **Reset** and a finished Apply untick it.
    note: Its tooltip reads "Use Multiresolution Support for sigma estimation".
  - name: Deringing
    kind: check
    default: "Off"
    effect: >-
      Reduces the dark and bright rings a strong change leaves at sharp edges. Near an edge of the original, each detail
      coefficient is pulled toward 0, and after the layers are added back the result is pulled toward the original.
    range: >-
      The edges are the larger of the horizontal and the vertical difference across each pixel, scaled so that the
      strongest tenth of them count in full. In an image whose strongest edge is below 0.001 it changes next to nothing.
    enabled: It does nothing while **Dark** and **Bright** are both 0.
    tradeoff: It can soften real high-contrast detail.
    scope: The preview and Apply, on each channel by its own edges. **Linear Mask** does not weigh it.
    persistence: Until the window closes. **Reset** and a finished Apply untick it.
  - name: Dark
    kind: spinbox
    default: "0.10"
    widget: {min: 0, max: 1, step: 0.05, decimals: 2}
    effect: >-
      Pulls the negative coefficients near edges toward 0, and lifts a result that fell below the original there:
      the dark rings. Higher removes more of them. At 1, a coefficient on the strongest edges loses its whole negative
      part.
    tradeoff: Too high a value flattens dark structure along edges.
    scope: The preview and Apply, while **Deringing** is ticked.
    persistence: Until the window closes. **Reset** and a finished Apply return it to its default.
  - name: Bright
    kind: spinbox
    default: "0.00"
    widget: {min: 0, max: 1, step: 0.05, decimals: 2}
    effect: >-
      Pulls the positive coefficients near edges toward 0, and lowers a result that rose above the original there:
      the bright rings. Higher removes more of them.
    tradeoff: Too high a value flattens highlights and star cores along edges.
    scope: The preview and Apply, while **Deringing** is ticked.
    persistence: Until the window closes. **Reset** and a finished Apply return it to its default.
  - name: Output Deringing Maps
    kind: check
    default: "Off"
    effect: >-
      Opens two images beside the target after an **Apply** to a single image: what the deringing took from the
      negative coefficients, and what it took from the positive ones, added over every detail layer.
    range: >-
      They are named `<target> - Deringing Dark Map` and `<target> - Deringing Bright Map`, with the channel in
      parentheses when **Channel** is not **RGB**, as `M42 - Deringing Dark Map (Red)`. Each is a 32-bit floating-point
      image with one channel for each channel or component worked on.
    enabled: >-
      While **Deringing** is ticked. Both maps are made even when **Dark** or **Bright** is 0, and that map is then
      black.
    tradeoff: >-
      They hold the change to the layers, not the pull on the result that follows, so they are no exact difference
      between the image before and after.
    scope: >-
      An **Apply** to a single image only: not the preview, a snapshot or a sequence. A run that is stopped or fails
      opens none.
    persistence: >-
      The maps open once the result is shown. Their history starts with `Multiscale Linear Transform - Deringing Dark
      Map` or `Multiscale Linear Transform - Deringing Bright Map`. They keep the target's acquisition data, plate
      solution and lineage and no other header card, and their processing state is recorded as nonlinear, made by
      `multiscale-deringing-dark-map` or `multiscale-deringing-bright-map`.
  - name: Mode
    kind: combo
    default: None
    options: [None, Hyperbolic, Log10, Natural Log (ln)]
    effect: >-
      Sends the residual through a curve before the layers are added back. **Hyperbolic** maps a value v to v / (v + b),
      with b the **Breakpoint**. **Log10** maps it to log10(1 + 9v) and **Natural Log (ln)** to ln(1 + (e - 1)v): both
      keep 0 and 1 and brighten the tones between.
    range: >-
      The residual is first multiplied by its weight and held between 0 and 1, and the curve's result is added back
      with a weight of 1.
    enabled: It does nothing while the residual's row is unticked.
    tradeoff: >-
      Every choice but **None** makes the result nonlinear, and the image is still recorded as it was. **Hyperbolic**
      brightens only the tones below 1 - b and sends 1 to 1 / (1 + b), so white turns gray.
    scope: The residual only, in the preview and Apply. Its group is **Large-Scale Transfer Function**.
    persistence: Until the window closes. **Reset** and a finished Apply return it to **None**.
  - name: Breakpoint
    kind: spinbox
    default: "0.75"
    widget: {min: 0.1, max: 1, step: 0.05, decimals: 2}
    effect: >-
      Sets b of **Hyperbolic**. Lower brightens the residual more and brings white closer to 1: white lands at 0.57 at
      the default and at 0.91 at 0.1.
    enabled: Shown only while **Mode** is **Hyperbolic**.
    scope: The residual only, in the preview and Apply.
    persistence: Until the window closes. **Reset** and a finished Apply return it to its default.
  - name: Low Range
    kind: spinbox
    default: "0.00"
    widget: {min: 0, max: 1, step: 0.05, decimals: 2}
    effect: >-
      Lifts the result and compresses its range: every sample x becomes (x + L) / (1 + L + H), with L this value and H
      **High Range**. Higher raises the black and brings back samples that fell below 0.
    tradeoff: >-
      The result is held between 0 and 1 after the mapping, and a linear master keeps the offset. Set the two values
      last: they map everything the other controls made.
    scope: >-
      The whole result, after the layers are added back and deringed. Its group is **Dynamic Range Extension**, and it
      acts whether the transform changed anything or not.
    persistence: Until the window closes. **Reset** and a finished Apply return it to its default.
  - name: High Range
    kind: spinbox
    default: "0.00"
    widget: {min: 0, max: 1, step: 0.05, decimals: 2}
    effect: >-
      Compresses the range from the top: it enlarges the divisor of **Low Range**'s mapping without adding to the
      sample. Higher brings back samples that rose above 1, and darkens the result.
    tradeoff: The result is held between 0 and 1 after the mapping.
    scope: The whole result, after the layers are added back and deringed.
    persistence: Until the window closes. **Reset** and a finished Apply return it to its default.
```

```controls
window: Selected Layer
controls:
  - name: Enabled
    kind: check
    default: "On"
    effect: Keeps the chosen layer in the result while ticked. It is the **En** box of the layer's row.
    range: It applies to the residual too.
    scope: The preview and Apply.
    persistence: For each layer until the window closes. **Reset** and a finished Apply tick it on every layer.
  - name: Layer 1
    kind: readout
    default: Layer 1
    effect: >-
      Names the layer the controls below edit: Layer 1 and up, or Residual Layer. With no row chosen it reads "No layer
      selected" and the whole box is disabled.
  - name: Copy to All
    kind: button
    default: ""
    effect: >-
      Copies the chosen layer's tick and **Bias** to every other row, and its **Noise Reduction** values to every other
      detail layer.
    range: >-
      The residual gets no noise reduction, and copying from the residual leaves the detail layers' noise reduction as
      it was.
    tradeoff: >-
      It overwrites the differences you set between scales, and the same **Threshold (σ)** is not the same strength on
      every layer.
    note: Its tooltip reads "Copy this layer's settings to all other layers".
  - name: Reset Layer
    kind: button
    default: ""
    effect: >-
      Returns the chosen layer to ticked, a bias of 0, noise reduction off, **Threshold (σ)** at 3.00, **Amount** at
      0.50 and **Iterations** at 1.
    tradeoff: Its **Amount** of 0.50 is not the 1.00 the window opens with and **Reset** gives.
  - name: Bias
    kind: spinbox
    default: "0.00"
    widget: {min: -1, max: 15, step: 0.01, decimals: 2}
    effect: >-
      Weighs the chosen layer when the layers are added back. At 0 the layer is kept as it is, below 0 it is weakened,
      and at -1 it is removed. Above 0 it is strengthened much more than the number says: a bias b gives a weight of
      1 + 6b, and 1 + 8b on Layer 1 of the multiscale linear transform.
    range: >-
      The slider moves the value in 1,000 equal steps from -1 to 15. The residual of the multiscale linear transform
      takes 1 + 8b when it follows a single detail layer, and 1 + 6b otherwise.
    tradeoff: >-
      A bias of 0.5 already makes a layer four times as strong, five times on Layer 1 of the multiscale linear
      transform, and ringing and clipping grow fast. Its tooltip says the layer is multiplied by 1 plus the bias, which
      holds only at 0 and below.
    scope: >-
      The preview and Apply, after the noise reduction and the deringing of the layer. The layer views of the preview
      scale the layer by five times the bias instead.
    persistence: For each layer until the window closes. **Reset** and a finished Apply return it to 0 on every layer.
  - name: Noise Reduction
    kind: check
    default: "Off"
    effect: >-
      Shrinks the chosen detail layer's coefficients toward 0: those smaller than the threshold become 0 and the larger
      ones move closer to 0 by the threshold, once for each of the **Iterations**.
    enabled: Disabled for the residual. Ticking it shows a dot in the layer's row.
    tradeoff: Weak real structure of that size goes with the noise.
    scope: >-
      The chosen layer, in the preview and Apply, before **k-Sigma Thresholding** and **Deringing**, weighed by **Linear
      Mask** while that is ticked.
    persistence: For each layer until the window closes. **Reset** and a finished Apply untick it on every layer.
  - name: Threshold (σ)
    kind: spinbox
    default: "3.00"
    widget: {min: 0, max: 40, step: 0.1, decimals: 2}
    effect: >-
      Sets the threshold in twentieths of a noise deviation: 20 is one deviation, and 3 is 0.15 of one. Higher removes
      more.
    range: >-
      The deviation is the larger of the layer's own noise and the noise of Layer 1. With the multiscale linear
      transform that is mostly the layer's own noise. With the starlet and **B3 Spline (5)** it is the noise of Layer 1
      on every layer, from 4 to nearly 90 times the noise of Layers 2 to 6.
    tradeoff: The same value is therefore much stronger on the deep layers of the starlet than on its Layer 1.
    scope: The chosen layer, weighed by **Linear Mask** while that is ticked.
    persistence: For each layer until the window closes. **Reset** and a finished Apply return it to its default.
  - name: Amount
    object: nrAmountSpinBox
    kind: spinbox
    default: "1.00"
    widget: {min: 0, max: 1, step: 0.05, decimals: 2}
    effect: >-
      Mixes each pass of the shrinkage with the coefficients before it. At 1 a pass acts in full, at 0 not at all.
    range: >-
      The window opens with 1.00 on every layer and **Reset** gives 1.00, while **Reset Layer** and a layer added by
      raising **Detail Layers** start at 0.50.
    tradeoff: Lower it before you add **Iterations**. Lower keeps more of every coefficient.
    scope: The chosen layer, multiplied by the mask of **Linear Mask** while that is ticked.
    persistence: For each layer until the window closes.
  - name: Iterations
    kind: spinbox
    default: "1"
    widget: {min: 1, max: 10, step: 1}
    effect: >-
      Repeats the shrinkage on the coefficients the last pass left. Each pass removes another threshold's worth, so 3
      passes at an **Amount** of 1 act as a threshold three times as high.
    tradeoff: Each pass adds work. The image is not split again between passes.
    scope: The chosen layer.
    persistence: For each layer until the window closes. **Reset** and a finished Apply return it to 1.
```

```controls
window: Multiscale Linear Transform
controls:
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
    effect: >-
      Chooses what is split into layers. **RGB** splits each of the three channels on its own, each with its own noise
      estimate. **Red**, **Green** or **Blue** works on that channel and keeps the other two. Every other choice works on
      one component of another color model and converts the image back.
    enabled: For an RGB image. A grayscale image shows a disabled **Grayscale**. Disabled while Apply runs.
    tradeoff: >-
      A component costs two conversions and a working copy, and the conversion back can change colors and clip those
      outside the range.
    scope: >-
      The preview, Apply and the mask of **Linear Mask**, which is made from the channel or component worked on. Process
      Windows describes each component.
    persistence: >-
      Until the window closes. **Reset** leaves it as it is. A finished Apply and a new target return it to **RGB**.
  - name: Preview
    object: preview
    kind: check
    default: "On"
    effect: Shows the result beside the controls, with **Amount** and the mask applied, or the view the list beside it chooses.
    range: >-
      The pane opens with a **Preview area** of 1200 by 1200 pixels in the middle of the image, labelled "Preview:
      Multiscale Linear Transform", or the whole image when it is smaller.
    tradeoff: >-
      The area is split into layers as an image of its own: its noise is measured inside it, and its deep layers see
      its border, not the rest of the image. Every change runs the whole transform again. A display in Autostretch can
      make what a threshold leaves look stronger than it is.
    scope: The window only. **Display Mode** and **Autostretch Settings** change only how the preview is shown.
    persistence: Until the window closes. **Reset** leaves it as it is.
  - name: Preview
    object: previewModeComboBox
    kind: combo
    default: Processed Image
    options: [Processed Image, Layer (All Changes), Layer (Increasing Pixels), Layer (Decreasing Pixels)]
    effect: >-
      Chooses what the preview shows. **Processed Image** is what Apply writes. The three layer views show the
      coefficients w of the row chosen in the table, the residual too, scaled by k, five times its **Bias**: **Layer
      (All Changes)** as 0.5 + k × w, gray where the layer is 0, **Layer (Increasing Pixels)** its positive part on
      black and **Layer (Decreasing Pixels)** its negative part on black.
    range: >-
      A layer view splits the image as it is and ignores the tick, the noise reduction, the thresholds, **Linear Mask**,
      the deringing and the residual's curve. At a bias of 0 it is uniform gray or black. A negative bias turns **Layer
      (All Changes)** round and leaves the other two black.
    enabled: >-
      While **Preview** is ticked, also while Apply runs. **Preview Mask** comes before it, and with no row chosen the
      layer views show the processed image. Its tooltip reads "Select what to show in the preview window".
    tradeoff: >-
      A layer view is no result: **Amount** below 1 and the mask on the view mix the original into it. Read it with
      **Amount** at 1, the view's mask disabled and the pane's **Display Mode** at **Linear**.
    scope: The preview and **Take snapshot**, which takes the view shown. **Apply** always writes the processed image.
    persistence: Until the window closes. **Reset** and a finished Apply return it to **Processed Image**.
  - name: Reset
    kind: button
    default: ""
    effect: >-
      Returns every value of the window to its default: the transform, six rows ticked with a bias of 0 and no noise
      reduction, Layer 1 chosen, every group of **Global Settings** off and **Processed Image**. One preview follows.
    range: It leaves **Target Image**, **Channel** and **Preview** as they are.
  - name: Apply
    kind: button
    default: ""
    effect: >-
      Works on the whole image, or every frame of the sequence, with the values as they are when you press it, replaces
      it, and returns the values to their defaults.
    enabled: >-
      From the moment the window opens, at any values. While it runs, the window's groups, **Reset** and **Close** are
      disabled.
    tradeoff: >-
      A single image shows a progress window too, and its **Cancel** stops the run at the next check between or inside
      the transform's passes, so a long pass may finish first. The work grows with the pixels, the channels, the layers,
      the width of the blurs, the iterations and each group that is on, and a run with a noise reduction also splits one
      bright pixel on a square of 512 to 1,024 pixels. The noise shares come from that split, not from random noise,
      so they are the same on every run and every computer. The run works on the threads it is granted. The layers are held at once: width × height ×
      channels × (layers
      + 1) × 4 bytes, beside a 32-bit copy of the channels, three working planes of one channel, a 32-bit result, a
      floating-point copy of an integer image, 64-bit for a 32-bit integer image, and the second copy every run needs. A run the memory limit cannot hold
      is refused with a reason that names the memory budget: fewer layers, or one channel or component, need less.
    persistence: >-
      The history step is **Multiscale Linear Transform** and reads, for example, `Algorithm: Multiscale Linear
      Transform, Layers: 6, Spacing: Dyadic, k-sigma: k=3.00, amt=0.70, soft in channel: RGB`. It names the algorithm,
      the layers, the spacing, as `Linear (scale 2)` for linear spacing, the starlet's scaling function, the values of
      each group that is on and the channel of a color image. It leaves out the rows' ticks, biases and noise reduction
      and **Amount**: note those yourself. The header cards, the acquisition data, the plate solution and the recorded
      processing state are kept, and the image's lineage gains an entry with the step's description.
```

## How it works

1. The channels **Channel** selects are read as
   [fractions of full scale](../../reference/glossary.md#fraction-of-full-scale) into a 32-bit floating-point copy and
   split into detail layers and a residual. Beyond the frame, the blurs repeat the edge sample.
2. While a noise reduction is on, the noise of Layer 1 is measured and the noise of every other layer derived from it.
   Each layer's **Noise Reduction** runs, then **k-Sigma Thresholding**, then the pull of **Deringing** at edges.
3. The layers are added back with their weights, the residual first through the **Large-Scale Transfer Function**.
   **Deringing** pulls the sum toward the original at edges, **Dynamic Range Extension** maps it, and it is held
   between 0 and 1.
4. The result is written back in the image's pixel type, rounded to the nearest code for an integer image. **Amount**
   mixes it with the original, and a mask on the view then blends the two.
5. The image replaces the target and its history gets one step. Its header,
   [plate solution](../../reference/glossary.md#plate-solution) and
   [processing state](../../reference/glossary.md#processing-state) are kept, even when a curve or a range mapping made
   it nonlinear.

### Model

Number the detail layers \(l = 1, \ldots, n\), with \(n\) the value of **Detail Layers**. Their scales are
\(s_l = 2^{l-1}\) under **Dyadic** spacing, or \(s_l = 1 + (l - 1)\,q\) under **Linear**, with \(q\) the **Linear
Scale**. With \(c_0\) the channel, the starlet blurs \(c_{l-1}\) with the **Scaling Function**, its taps \(s_l\) pixels
apart, to make \(c_l\). The multiscale linear transform blurs \(c_0\) itself with a Gaussian of deviation \(s_l / 3\),
cut at four deviations. Layer \(l\) holds \(w_l = c_{l-1} - c_l\), the residual is \(c_n\), and

\[ c_0 = \sum_{l=1}^{n} w_l + c_n \tag{1} \]

so the layers give the channel back when nothing is changed. A ticked layer of **Bias** \(b\) is added back with the
weight

\[ \beta = \begin{cases} 1 + b & b \le 0 \\ 1 + g\,b & b > 0 \end{cases} \tag{2} \]

where \(g\) is 8 on Layer 1 of the multiscale linear transform and 6 elsewhere. An unticked layer weighs 0.

The noise of Layer 1 is \(\sigma_1 = \operatorname{median}|w_1| / 0.6745\). The window splits a single bright pixel with
the same transform, and \(\rho_l\) is the root of the sum of squares of layer \(l\)'s response. Then

\[ \sigma_l = \sigma_1\,\frac{\rho_l}{\rho_1} \tag{3} \]

At the defaults, \(\rho_1\) to \(\rho_6\) are 0.05, 0.64, 0.30, 0.14, 0.07 and 0.04 for the multiscale linear transform,
and 0.89, 0.20, 0.09, 0.04, 0.02 and 0.01 for the starlet with **B3 Spline (5)**.

With \(T\) a threshold, \(a\) an **Amount** and \(m\) the weight of **Linear Mask**, 1 without it, a coefficient \(w\)
becomes

\[ w + a\,m\,\bigl(\operatorname{sign}(w)\max(|w| - T,\ 0) - w\bigr) \tag{4} \]

**Noise Reduction** uses \(T = 0.05\,t\,\max(\sigma_1, \sigma_l)\), with \(t\) its **Threshold (σ)**, once per
iteration. **k-Sigma Thresholding** uses \(T = k\,\sigma_l\), with \(k\) its **Threshold (k)**. With \(y\) the deringed
sum of the weighted layers, and \(L\) and \(H\) the values of **Low Range** and **High Range**, the result is

\[ \min\Bigl(1,\ \max\Bigl(0,\ \frac{y + L}{1 + L + H}\Bigr)\Bigr) \tag{5} \]

### Boundaries

- A layer deeper than the transform builds is refused in the preview and in Apply with "the deepest
  multiscale-linear layer needs a Gaussian radius above the stated maximum; ask for fewer layers or a smaller linear
  step", or with "the deepest starlet layer needs a tap spacing above the stated maximum" and the same advice.
- The transform works in single precision on every pixel type, so a 64-bit floating-point image gets a result of
  single precision.
- A sample that is no finite number, or that the image marks as invalid, does not stop the run. It counts as 0 for its
  neighbours, and the result keeps it as it was.
- **Cancel**, or closing the window, stops the run at its next check, and nothing is written and no map opens.

### Worked example

The image is a 1,100 by 820 crop of the middle of the linear master. The window keeps its defaults, with **k-Sigma
Thresholding** ticked and its **Amount** at 0.70. Each channel is thresholded by its own noise, which in this master
is about 0.0003 in red, 0.001 in green and 0.0007 in blue from one pixel to the next.

Suppose the window measures the green channel's noise at 0.001. Equation (3) then gives its six layers a noise of
0.00005, 0.00064, 0.00030, 0.00014, 0.00007 and 0.00004, and **Threshold (k)** at 3 makes thresholds three times
those. In Layer 2, a grain coefficient of 0.0010 lies below the threshold of 0.0019 and keeps 0.3 of itself, 0.0003,
by equation (4). A star's coefficient of 0.1000 lies above it and keeps 0.0987. Nearly every coefficient of the grain
lies below its threshold, so the grain keeps about 0.3 of its strength, and structure far above the thresholds keeps
nearly all of its contrast. Both crops are then stretched alike with Histogram Transformation, **Shadows** and
**Midtones** at 0.02, and shown in the Linear display mode.

```figure
images:
  - image: ../../assets/images/processes/noise-reduction/noise-crop-before.webp
    alt: >-
      A crop at 100 % of the middle of the master, stretched: pink clouds of nebula beside a dark dust lane, many small
      stars, and a fine colored grain over the background.
    label: Before
  - image: ../../assets/images/processes/multiscale/multiscale-linear-after.webp
    alt: >-
      The same crop after Apply and the same stretch: the grain of the background and of the faint nebula about half as
      strong, the stars and the edges of the dust lane kept.
    label: After
caption: >-
  A 1,100 by 820 crop of the middle of the linear Carina master, before and after Apply with k-Sigma Thresholding
  ticked and its Amount at 0.70. Both are stretched alike with Histogram Transformation, Shadows and Midtones at 0.02,
  and shown at 100 % in the Linear display mode.
```

## Starting points

| Situation | Start | Then adjust |
| --- | --- | --- |
| A grainy linear master | **k-Sigma Thresholding** at 3, its **Amount** from 0.5 to 0.7 | **Amount** up while the faint nebula keeps its texture. |
| Grain of one size | **Noise Reduction** on Layer 2, or on Layer 1 of the starlet, **Threshold (σ)** from 20 to 40 | The next layer at a lower value, and **Iterations** last. |
| Noise in the faint areas only | **Linear Mask**, **Inverted**, **Amplification** about 10 on a linear master | Check **Preview Mask** at an **Amount** of 1 in the Linear display mode. |
| Small detail on a stretched image | **Bias** from 0.05 to 0.2 on Layers 2 to 4 | **Deringing** with **Dark** at 0.10 if dark rings show around stars. |

## When it goes wrong

!!! danger "Samples outside 0 to 1 are cut"
    The result is held between 0 and 1, also in a floating-point image. On a linear master, star cores above 1 become 1
    at any setting. Bring the image between 0 and 1 first, or protect the stars with a mask.

"the deepest multiscale-linear layer needs a Gaussian radius above the stated maximum; ask for fewer layers or a smaller linear step"
:   **Cause.** **Detail Layers** asks for a layer deeper than the transform builds.
:   **Fix.** Lower **Detail Layers** to 7, or 10 for the starlet, or choose **Linear** spacing with a small **Linear
    Scale**.

Noise Reduction on Layer 1 changes almost nothing
:   **Cause.** Layer 1 of the multiscale linear transform keeps about a twentieth of the grain, and Layer 2 most of it.
:   **Fix.** Reduce the noise of Layers 2 and 3, or choose **Starlet Transform**, whose Layer 1 holds most of the grain.

Linear Mask changes nothing, or takes the noise reduction away
:   **Cause.** It weighs only the thresholds, and with **Inverted** ticked at an **Amplification** of 100 its weight is
    0 on every sample above 0.01.
:   **Fix.** Turn on a noise reduction, then lower **Amplification** until **Preview Mask** shows the faint areas light.

The noise reduction is far stronger than you set
:   **Cause.** A layer's own **Noise Reduction** and **k-Sigma Thresholding** add up, **Iterations** repeat it, or the
    deep layers of the starlet take the noise of Layer 1 as their unit.
:   **Fix.** Turn one of the two off, set **Iterations** to 1, and lower **Threshold (σ)** on the deep layers.

A layer view is uniform gray or black, or nearly white
:   **Cause.** The view scales the layer by five times its **Bias**, which is 0, and
    [Autostretch](../../reference/glossary.md#autostretch) turns its gray almost white.
:   **Fix.** Set a small **Bias** to look, and choose **Linear** in the pane's **Display Mode**.

White turns gray after a Hyperbolic curve
:   **Cause.** **Hyperbolic** sends 1 to 1 / (1 + b), with b the **Breakpoint**.
:   **Fix.** Lower **Breakpoint**, or choose **Log10** or **Natural Log (ln)**, which keep white at 1.

## Related

- [Wavelet Denoise](../noise-reduction/wavelet-denoise.md), another way to reduce noise scale by scale.
- [Sharpen Alpha](../sharpening/sharpen-alpha.md), to strengthen detail by scale with fewer controls.
- [Histogram Transformation](../stretching/histogram-transformation.md), the stretch that follows the linear work.
- [Masks](../../interface/masks.md), for the mask on the view that blends the whole result.
- [Image History](../../interface/image-history.md), to undo the step and to read its summary.
- [Process Windows](../../interface/process-windows.md), for the preview, the preview area and what every process
  window shares.
