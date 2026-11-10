---
kind: process
stage: stretch
menu: Process > Intensity Transformations > Histogram Transformation
summary: >-
  Maps the image through a shadows, midtones and highlights transfer, the controlled step from a linear master to a
  visible picture.
data_in: linear
data_out: nonlinear
destructive: true
preview: real-time
sequences: true
masks: post-blend
external: none
glance:
  enabled: >-
    An image or a sequence view is active. The window opens beside other process windows, and Apply waits for
    nothing else to be working on the target.
  inputs: One grayscale or RGB image or sequence, and an optional mask on its view.
  outputs: >-
    The same view, replaced, with one history step. Its size and pixel type are kept. No file, preset or report is
    written.
  state: >-
    A calibrated linear master, mono or RGB, after gradient removal, color calibration and every measurement,
    deconvolution or noise reduction that needs linear data.
derived: false
---
# Histogram Transformation

Histogram Transformation [stretches](../../reference/glossary.md#stretch) a
[linear image](../../reference/glossary.md#linear-image) into a visible one. You set a
[black point](../../reference/glossary.md#black-point), a [white point](../../reference/glossary.md#white-point) and a
[midtones](../../reference/glossary.md#midtones) balance, and every sample is mapped through the
[midtones transfer function](../../reference/glossary.md#midtones-transfer-function) they define. Two toolbar buttons
derive the same kind of curve from the image's statistics instead.

**Apply** replaces the image or [sequence](../../reference/glossary.md#sequence) with the stretched one, keeps its size
and [pixel type](../../reference/glossary.md#pixel-type), and adds one step to its
[history](../../reference/glossary.md#history). The result is recorded as nonlinear.

```figure
image: ../../assets/images/processes/stretching/histogram-transformation.webp
alt: >-
  The Histogram Transformation window. On the left, two histograms: the upper one spread into several peaks, the
  lower one a single spike at the left edge under a steep white curve, with three handles below it. Under them a
  toolbar, the Channel box with two buttons, the Shadows, Highlights and Midtones boxes with their Clipped fields,
  Target Image and Preview. On the right, the preview shows a red nebula. Reset, Close and Apply run along the bottom.
caption: >-
  Histogram Transformation on a linear master with Shadows and Midtones at 0.02. The lower histogram is the image
  with the curve over it, the upper one is the result, and the preview shows what Apply writes.
pins:
  - {n: 2, x: 10.4, y: 65.4}
  - {n: 3, x: 10.4, y: 70.8}
  - {n: 4, x: 10.4, y: 78.5}
  - {n: 6, x: 70.4, y: 54.0}
  - {n: 7, x: 90.4, y: 97.0}
```

## Before you start

- Finish the work that needs linear data first: gradient removal, color calibration, measurement,
  [deconvolution](../../reference/glossary.md#deconvolution) and linear noise reduction. After **Apply** the image is nonlinear.
- **Apply** replaces the image. Its history keeps the step before, and this window has no inverse. Stretch once:
  every repeated stretch adds rounding and clipping.
- The image is grayscale or RGB. A raw color-filter mosaic is stretched as a grayscale image, which breaks the
  relation between its sensor values: [debayer](../../reference/glossary.md#debayer) it first.
- A [mask](../../reference/glossary.md#mask) on the view blends the stretched image with the original after the stretch.
  The histograms and the **Clipped** fields are counted without it.
- On a sequence, your own values give every frame the same curve. An Auto-stretch button gives each frame the curve
  of its own statistics.

## Steps

1. **Open the window.** Make the linear [master](../../reference/glossary.md#master) the
   [active image](../../reference/glossary.md#active-image) and choose **Process > Intensity Transformations >
   Histogram Transformation**. The lower histogram shows the image as a narrow peak at the left, and the preview is
   nearly black.
2. **Choose the channel.** Leave **Channel** at **RGB** to stretch the three channels with one curve. Choose
   **Red**, **Green** or **Blue** only to correct one channel on purpose.
3. **Set the black point.** Raise **Shadows** until its handle reaches the foot of the peak. Stop while the
   **Clipped** field beside it is still near zero: what it counts is lost.
4. **Brighten the midtones.** Lower **Midtones** until the faint signal shows in the preview. The upper histogram
   shows where the peak lands.
5. **Leave the white point.** Keep **Highlights** where it is unless the top of the lower histogram is empty.
   Lowering it sends star cores to white.
6. **Check the preview at 100 %.** Zoom the preview into the faint areas and the brightest stars, and press
   ++space++ over it to compare with the original.
7. **Apply.** Choose **Apply**. The image in its view is the stretched one, its history lists **Histogram
   Transformation** with the values you set, and the controls return to their defaults.

## Controls

The toolbar under the histograms - zoom, grid, curve, scales, areas and **Plot resolution** - changes only how the
histograms are drawn, and is the same in every window that has one: see
[Process Windows](../../interface/process-windows.md).

```controls
window: The histograms
controls:
  - name: Upper histogram
    kind: readout
    default: ""
    effect: Shows the result, which is the image's counts sent through the curve your values or the Auto-stretch button define.
    range: One line for a grayscale image, three for RGB.
    enabled: Always shown. The lines it draws follow the channel the preview displays, not **Channel**.
    tradeoff: It is a prediction from the counted bins. It leaves the mask out and is never counted again from the stretched image.
    scope: The window only. It is redrawn after every change of a value, of **Channel**, of the Auto-stretch button, of the target and of **Plot resolution**.
  - name: Lower histogram
    kind: readout
    default: ""
    effect: Shows the image as it is, with the three handles under it and a guide line that follows the pointer.
    enabled: Always shown. The toolbar's curve button draws the white transfer curve over this histogram only, from the three values. An Auto-stretch does not redraw it.
    tradeoff: The narrow peak of a linear image may need the toolbar's zoom or a finer **Plot resolution** to be seen. Neither changes the image.
    note: A click away from a handle shows the coordinates under the pointer. Dragging with the middle button pans, the wheel with ++ctrl++ held zooms and a right click restores the zoom, in both histograms.
  - name: Shadows, Midtones and Highlights handles
    kind: slider
    default: At 0, 0.5 and 1
    widget: {min: 0, max: 1}
    effect: Dragging a handle left or right moves its value. The preview follows when you release it.
    range: The Shadows and Highlights handles cannot cross while you drag them. The Midtones handle keeps its place between the two as they move.
    tradeoff: Handles that lie on top of each other are hard to pick up. Type an exact value in its box instead.
    scope: The same values as the three boxes, on the channels **Channel** selects.
    persistence: Until **Reset**, **Apply** or the window closes.
```

```controls
window: Histogram Transformation
controls:
  - name: Channel
    kind: combo
    default: RGB
    options: [RGB, Red, Green, Blue]
    effect: Chooses what the three values stretch. **RGB** gives the three channels one curve. A single color stretches that channel and leaves the other two as they are.
    enabled: A grayscale image shows a disabled **Grayscale** instead.
    tradeoff: Stretching one channel shifts the color balance, and can clip stars and background in that color.
    scope: The preview, Apply and the count of the **Clipped** fields. It does not choose the lines the histograms draw.
    persistence: Until the window closes. **Reset** and **Apply** return it to **RGB**.
    note: The window keeps one set of values, not one for each channel. Choosing a channel clears an Auto-stretch.
  - name: Auto-stretch linked channels
    kind: toolbutton
    default: ""
    effect: Returns the three values to their defaults and stretches every channel with one curve derived from the image's statistics.
    range: The curve puts the black point 2.8 deviations below the median and the background at 0.25. Neither number can be changed.
    enabled: With **Preview** on, for a grayscale or an RGB image.
    tradeoff: The curve it derives is shown nowhere. The three boxes keep their defaults, and the history records those.
    scope: The preview and Apply. A **Preview area** shows the curve of the whole image, so it shows what Apply writes there. On a sequence, each frame gets the curve of its own statistics.
    persistence: Until you change a value or **Channel**, press **Reset**, or Apply succeeds.
  - name: Auto-stretch channels independently
    kind: toolbutton
    default: ""
    effect: Returns the three values to their defaults and stretches each channel with a curve derived from its own statistics.
    range: The same two numbers as the linked button.
    enabled: With **Preview** on, for an RGB image.
    tradeoff: It brings the channels' backgrounds together, which changes the ratios between the colors.
    scope: The preview and Apply, with the mask blended afterwards. On a sequence, each frame gets the curves of its own statistics.
    persistence: Until you change a value or **Channel**, press **Reset**, or Apply succeeds.
  - name: Shadows
    kind: spinbox
    default: "0.000000"
    widget: {min: 0, max: 1, step: 0.01, decimals: 6}
    effect: Sets the black point. Higher sends more of the low samples to black and spreads the rest over the range. Lower keeps more of the shadows.
    range: The box does not keep it below **Highlights**.
    tradeoff: Set too high, it erases faint signal and background for good. Set too low, the image stays dim and the signal stays pressed against the left.
    scope: The preview and Apply, on the channels **Channel** selects, before the mask is blended.
    persistence: Until the window closes. Apply returns it to its default.
  - name: Clipped
    object: shadowsInfo
    kind: readout
    default: "0 - 0.000000%"
    effect: Counts the samples below the black point, and gives them as a share of the image.
    range: A count of whole bins, at the toolbar's **Plot resolution**. The bin the black point falls in is not counted.
    tradeoff: With one color in **Channel** the count is that channel's and the share is of all three channels, so it reads a third of that channel's share.
    scope: The window only, without the mask. It is counted again after every change of a value, of **Channel**, of the Auto-stretch button, of the target and of **Plot resolution**.
    note: For exact counts, use Image Statistics after Apply.
  - name: Highlights
    kind: spinbox
    default: "1.000000"
    widget: {min: 0, max: 1, step: 0.01, decimals: 6}
    effect: Sets the white point. Lower sends more of the high samples to white and spreads the rest over the range. Higher keeps more of the highlights.
    range: The box does not keep it above **Shadows**.
    tradeoff: Lowering it flattens star cores and bright structure, which cannot be brought back.
    scope: The preview and Apply, on the channels **Channel** selects, before the mask is blended.
    persistence: Until the window closes. Apply returns it to its default.
  - name: Clipped
    object: highlightsInfo
    kind: readout
    default: "0 - 0.000000%"
    effect: Counts the samples above the white point, and gives them as a share of the image.
    range: A count of whole bins, at the toolbar's **Plot resolution**. The bin the white point falls in is not counted.
    tradeoff: With one color in **Channel** the share is of all three channels, as for the black point.
    scope: The window only, without the mask, counted again with the black point's field.
  - name: Midtones
    kind: spinbox
    default: "0.500000"
    widget: {min: 0, max: 1, step: 0.01, decimals: 6}
    effect: Sets the balance between the two points. Below the default it brightens what lies between them, above it darkens. It is the place between the two points that ends at half brightness.
    tradeoff: Near either end of its range almost everything collapses to black or white and the steps between tones show. Small moves change the contrast a lot.
    scope: The preview and Apply, on the channels **Channel** selects, before the mask is blended.
    persistence: Until the window closes. Apply returns it to its default.
  - name: Target Image
    kind: combo
    default: The active image
    options: [The active image, Every other open image or sequence the window can work on]
    effect: Chooses the image Apply replaces. The histograms, the channels and the preview follow it.
    tradeoff: A larger image or a sequence takes more time, memory and disk.
    persistence: Until the window closes.
  - name: Preview
    kind: check
    default: "On"
    effect: Shows the stretched image beside the controls, with the mask blended in. Off removes it and disables both Auto-stretch buttons. The histograms keep working.
    tradeoff: Each change stretches the whole image again. On a large image, turn it off while you adjust and on again for the last check.
    scope: The window only.
    persistence: Until the window closes. **Reset** leaves it as it is.
    note: The channel the preview displays is the one whose lines both histograms draw.
  - name: Reset
    kind: button
    default: ""
    effect: Returns the three values and **Channel** to their defaults, clears an Auto-stretch and restores the zoom of both histograms.
    range: It leaves the toolbar's choices, the target and **Preview** as they are.
    scope: The window only. Apply is disabled afterwards.
  - name: Apply
    kind: button
    default: ""
    effect: Stretches the image, or every frame of the sequence, with the three values or the Auto-stretch in force, and then returns the controls to their defaults.
    enabled: When a value is off its default or an Auto-stretch is in force. It is disabled while the window opens.
    scope: The whole image or sequence, on the channels **Channel** selects, with the mask blended afterwards.
    persistence: The image and its history keep the result. The history records **Channel** and the three values, and not the toolbar's choices, the mask, the **Clipped** counts or the curve of an Auto-stretch.
    note: Closing the window instead discards the values. Nothing of the window is saved between sessions.
```

```controls
window: Sequence progress
controls:
  - name: Progress
    kind: readout
    default: ""
    effect: Shows the preparation and the frame being stretched, from 0 to 100 percent.
    enabled: Shown only while Apply works on a sequence. A single image has no progress window.
    tradeoff: The sequence is written again in a temporary place, which needs the disk space of a second copy.
  - name: Cancel
    kind: button
    default: ""
    effect: Asks the run to stop. The temporary sequence is removed and the original stays as it was.
    range: The request is noticed before each frame and at intervals inside one, so the frame being read or written finishes first.
    note: The window's own **Close** button is disabled while the run lasts.
```

## How it works

1. The window counts a histogram of each channel, at the toolbar's **Plot resolution**, and draws it below.
2. Your three values, or the Auto-stretch button, define one transfer curve for the channels in **Channel**. An
   Auto-stretch measures the median and the deviation of every valid sample of the whole image, once for each state
   of the image.
3. Every selected sample is brought to the range 0 to 1, sent through the curve and written back in the image's own
   pixel type, rounded to the nearest value. The other channels are kept exactly as they were.
4. A mask on the view then blends the result with the original.
5. The image replaces the target and its history gets one step. Its header, its acquisition data and its
   [plate solution](../../reference/glossary.md#plate-solution) are kept, and its
   [processing state](../../reference/glossary.md#processing-state) is recorded as nonlinear with the operation
   `histogram-mtf`.

### Model

A sample \(x\) between 0 and 1 is first placed between the black point \(S\) of **Shadows** and the white point
\(H\) of **Highlights**:

\[ t = \frac{x - S}{H - S} \tag{1} \]

A sample at or below \(S\) becomes 0 and one at or above \(H\) becomes 1. Between them, with \(m\) the value of
**Midtones**, the result is

\[ f(t) = \frac{(m - 1)\,t}{(2m - 1)\,t - m} \tag{2} \]

With \(m = 0.5\), equation (2) is a straight line between the two points. The sample at \(t = m\) always lands at
0.5, which is where the middle handle sits: at \(S + (H - S)\,m\).

An Auto-stretch takes, for each channel \(j\), the median \(v_j\) and the deviation \(q_j\), which is 1.4826 times
the [median absolute deviation](../../reference/glossary.md#median-absolute-deviation), or 0.001 when that is zero.
The linked button derives

\[ S = \max\Bigl(0,\ \overline{v_j - 2.8\,q_j}\Bigr), \qquad H = 1, \qquad m = f_{0.25}\bigl(\overline{v_j} - S\bigr) \tag{3} \]

where the bar is the mean over the channels and \(f_{0.25}\) is equation (2) with \(m = 0.25\). The other button
applies equation (3) to each channel alone. When every median is above 0.5 the roles turn round: \(S = 0\), \(H\)
is placed 2.8 deviations above the median, and \(m\) is the complement.

### Boundaries

- **Shadows** above **Highlights** is refused: the preview and Apply report "midtones parameters must be finite
  unit values with ordered endpoints" and the upper histogram empties. With the two equal, the result is a black
  and white threshold.
- A sample that is not a finite number, in a channel being stretched, stops the run with "a selected stretch sample
  is not finite". An Auto-stretch also refuses an image without a valid sample, and a floating-point image whose
  median lies outside 0 to 1. Pixels the image marks as invalid are left unchanged.
- A 64-bit floating-point or 32-bit integer image is computed in double precision, every other type in single
  precision. In the channels left alone, and in an alpha channel, floating-point samples outside 0 to 1 are kept.
- The window is made for images of one or three channels.
- A single image has no progress window and cannot be cancelled once Apply starts.

### Worked example

The master in the picture above is an RGB image whose three channels have their median near 0.028. With **Channel**
at **RGB**, **Shadows** at 0.02 and **Midtones** at 0.02, equation (1) places the median at \(t = 0.0078\) and
equation (2) lifts it to 0.28: the background becomes a dark gray and the nebula spreads over the range above it.
The **Clipped** field of the black point counts 46 samples of 118 million.

```figure
images:
  - image: ../../assets/images/processes/stretching/histogram-transformation-before.webp
    alt: The whole frame before the stretch, black except for a few of the brightest stars.
    label: Before
  - image: ../../assets/images/processes/stretching/histogram-transformation-after.webp
    alt: The whole frame after the stretch, the red and white clouds of the nebula across a field of stars on a dark gray background.
    label: After
caption: >-
  The Carina Nebula master before and after Apply with Shadows and Midtones at 0.02. Both are the whole frame as the
  view shows it in the Linear display mode, without a display stretch.
```

One channel alone works the same way. With **Channel** at **Red**, **Shadows** at 0.10, **Midtones** at 0.40 and
**Highlights** at 0.90, a red sample of 0.50 gives \(t = 0.5\) and becomes 0.60, while green and blue keep their
values. A red sample of 0.10 becomes black, one of 0.90 white, and one of 0.42 lands at 0.5.

## Starting points

| Situation | Start | Then adjust |
| --- | --- | --- |
| A linear RGB master | **RGB**, and **Shadows** at the foot of the peak | **Midtones** down until the faint signal shows. **Highlights** last, if at all. |
| A first look, or a quick result | **Auto-stretch linked channels** | Check the preview at 100 %. Type your own values when the stretch must be repeatable. |
| One channel is off after color calibration | That color in **Channel**, with small moves | Return to **RGB** for the stretch itself. |
| A stretch through a mask | The curve first, with the mask off | Enable the mask, check its edges at 100 %, then Apply. |

## When it goes wrong

!!! warning "Apply cannot be taken back from this window"
    The stretch replaces the image and records it as nonlinear. Finish color calibration and every other linear
    step first, and use the image's history to return to the linear master.

!!! warning "An Auto-stretch leaves no numbers"
    The curve an Auto-stretch button derives is written nowhere: not in the boxes, not in the history. Note which
    button you pressed, or type your own values when you must be able to repeat the stretch.

The preview is stretched but the three boxes still show their defaults
:   **Cause.** An Auto-stretch is in force, and its curve is not copied into the boxes.
:   **Fix.** Check the preview and Apply, or type your own values to see the numbers you apply.

Apply is disabled again after an Auto-stretch
:   **Cause.** A change of a value or of **Channel**, **Reset** or a finished Apply cleared the Auto-stretch.
:   **Fix.** Press the Auto-stretch button again and check the preview before you Apply.

The image is nearly black and white only, or the preview reports "midtones parameters must be finite unit values with ordered endpoints"
:   **Cause.** **Shadows** and **Highlights** are equal or crossed, or **Midtones** is at an end of its range.
:   **Fix.** Press **Reset**, then keep **Shadows** below **Highlights** and **Midtones** away from both ends.

The Clipped share looks three times too small for one channel
:   **Cause.** The count is of the channel in **Channel**, and the share is of the samples of all three channels.
:   **Fix.** Multiply by three for a rough reading, or use Image Statistics after Apply for an exact one.

A Clipped count changed when Plot resolution changed
:   **Cause.** The fields count whole bins, and another resolution has other bins.
:   **Fix.** Compare counts at one resolution only.

The histograms still draw three lines while Channel is Red
:   **Cause.** The lines follow the channel the preview displays, not **Channel**.
:   **Fix.** Choose the channel to display in the preview's own toolbar, and leave **Channel** at what you stretch.

The colors shift after the stretch
:   **Cause.** One channel was stretched alone, or the channels were Auto-stretched independently.
:   **Fix.** Undo the step, then stretch with **RGB** or with the linked button. Balance colors before the stretch.

The preview is slow
:   **Cause.** Every change stretches the whole image again and recounts the histograms, and a fine **Plot
    resolution** and a mask add to it.
:   **Fix.** Turn **Preview** off while you adjust, return **Plot resolution** to its default, and turn
    **Preview** on for the last check.

## Related

- [Background Neutralization](../color-calibration/background-neutralization.md) and
  [Photometric Color Calibration](../color-calibration/photometric-cc.md), which need the image linear and come
  before.
- [Generalized Hyperbolic Transformation](gh-transformation.md), a stretch with more control over where the
  contrast goes.
- [Statistical Stretch](statistical-stretch.md), a stretch that places the median where you ask.
- [Curves Transformation](curves-transformation.md), to shape tone and color after the stretch.
- [Image Statistics](../../interface/image-statistics.md), for exact counts of black and white samples after Apply.
- [Process Windows](../../interface/process-windows.md), for the preview, the histogram toolbar and what every
  process window shares.

## Credits

- The [XISF](../../reference/glossary.md#xisf)
  [1.0 specification](https://pixinsight.com/doc/docs/XISF-1.0-spec/XISF-1.0-spec.html) describes display functions and the midtones transfer function this explanation uses.
- A [PixInsight forum explanation of the midtones transfer function](https://pixinsight.com/forum/index.php?threads/after-doing-luminance-levels-transform-the-image-is-monochrome.741/)
  describes the two end points, the control point at 0.5 and the straight line at a midtones balance of 0.5.
