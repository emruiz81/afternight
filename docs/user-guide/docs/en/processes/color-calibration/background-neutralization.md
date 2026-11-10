---
kind: process
stage: color
menu: Process > Color Calibration > Background Neutralization
summary: >-
  Brings the background of the three channels of an RGB image to one shared level, measured in a reference, by adding
  one constant to each channel.
data_in: linear
data_out: linear
destructive: true
preview: none
sequences: true
masks: post-blend
external: none
glance:
  enabled: >-
    An RGB image or sequence view is active, and no other process window is open on it: Background Neutralization
    needs the image to itself.
  inputs: >-
    One RGB image or sequence, a background reference of three channels - the image itself or another open RGB image
    of any size - and an optional mask on the view.
  outputs: >-
    The same view, replaced, with one history step. Its size, pixel type and header are kept. No file, preset or
    report is written.
  state: >-
    A linear RGB master after gradient removal, before the color calibration that sets the channels' gains.
derived: false
rewritten: true
---
# Background Neutralization

Background Neutralization makes the background of an RGB image neutral. It measures each channel's background level in
a reference - the image itself, another open RGB image, or a rectangle of either - as the median of the samples
between two limits. Then it adds to each channel the constant that brings its level to the mean of the three.

**OK** replaces the image, or every frame of a [sequence](../../reference/glossary.md#sequence), adds one step to its
[history](../../reference/glossary.md#history) and closes the window. The size, the
[pixel type](../../reference/glossary.md#pixel-type) and the header are kept. The window has no preview: what you set
reaches the image only when you press **OK**.

```figure
image: ../../assets/images/processes/color-calibration/background-neutralization.webp
alt: >-
  The Background Neutralization window. Under the title, Reference image reads TARGET IMAGE between arrows. Lower Limit
  reads 0.000000 and Upper Limit 0.200000, each with a slider. The Region of interest group is unticked, with its
  Left, Top, Width and Height boxes dimmed at 0 and its From Selection button. Target Image names the composed image,
  and Reset, OK and Cancel run along the bottom.
caption: >-
  Background Neutralization on an RGB image composed from the red, green and blue masters, with Upper Limit raised to
  0.2 so that every channel's background lies between the limits.
pins:
  - {n: 3, x: 2.2, y: 22.0}
  - {n: 4, x: 2.2, y: 37.1}
  - {n: 5, x: 28.5, y: 43.6}
  - {n: 6, x: 65.0, y: 94.3}
```

## Before you start

- Work on a [linear image](../../reference/glossary.md#linear-image), before the
  [stretch](../../reference/glossary.md#stretch): an offset per channel means what it should only on linear data. The
  window does not check.
- Remove gradients first, with Background Extraction or Auto Gradient Removal. One constant per channel corrects no
  gradient, no vignetting and no difference of gain between the channels.
- **OK** replaces the image, and there is no preview. The image's history keeps the step before. When unsure, try
  the settings on a duplicate.
- The window needs the image to itself: close the other process windows open on it first.
- A [mask](../../reference/glossary.md#mask) on the view blends the result with the original afterwards. The
  measurement leaves it out.
- On a sequence, the reference is measured once and every frame gets the same offsets.

## Steps

1. **Open the window.** Make the RGB image the [active image](../../reference/glossary.md#active-image) and choose
   **Process > Color Calibration > Background Neutralization**.
2. **Read the backgrounds.** Choose **Image > Information > Statistics** and note each channel's median. The limits
   must hold the background of every channel.
3. **Choose the reference.** Leave **Reference image** at `<--TARGET IMAGE-->` to measure the image itself. Choose
   another open RGB image only when its background stands for this one's.
4. **Set the limits.** Keep **Lower Limit** below the darkest channel's background and above any black border. Set
   **Upper Limit** above the brightest channel's background and below the stars and the bright nebula.
5. **Choose the area, if the frame is crowded.** Tick **Region of interest** and type the rectangle, or press **From
   Selection** and drag a rectangle of clean sky on the reference's view.
6. **Neutralize.** Choose **OK**. The window closes, the three channels share one background level, and the history
   lists **Background Neutralization** with the reference and its rectangle.

## Controls

What every process window shares is on [Process Windows](../../interface/process-windows.md).

```controls
window: Background Neutralization
controls:
  - name: Reference image
    kind: combo
    default: "<--TARGET IMAGE-->"
    options: ["<--TARGET IMAGE-->"]
    effect: Chooses the image whose channels' background levels set the three offsets. `<--TARGET IMAGE-->` is the image OK changes.
    range: It also lists every other open RGB image or sequence, of any size and pixel type, and follows them as they open, close and are renamed. The target is offered once, as `<--TARGET IMAGE-->`. Each item's tooltip describes the image.
    enabled: Closing the view chosen here returns it to `<--TARGET IMAGE-->`.
    tradeoff: A reference whose background does not stand for the target's passes its offsets on unchanged, and gives the target a new cast.
    scope: Measured once, when you press OK. Every frame of a sequence gets the same offsets. With `<--TARGET IMAGE-->`, a sequence is measured on the frame its view shows.
    persistence: Until the window closes. **Reset** and another target return it to `<--TARGET IMAGE-->`.
    note: The reference is never changed.
  - name: Lower Limit
    kind: spinbox
    default: "0.000000"
    widget: {min: 0, max: 1, step: 0.05, decimals: 6}
    effect: Sets the lowest sample that counts as background, as a fraction of full scale. Higher leaves darker samples out, such as black borders. A sample equal to the limit is kept.
    range: The slider moves in thousandths. Nothing keeps it below **Upper Limit**, and above it no sample is kept and the run is refused. A signed integer image reads its zero code near 0.5, counted from its smallest value, so the default limits keep nothing in it.
    tradeoff: Set too high, it leaves a channel without a sample, and the run is refused.
    scope: The measurement only, in all three channels. It does not choose which samples change.
    persistence: Until the window closes. **Reset** and another target return it to its default.
  - name: Upper Limit
    kind: spinbox
    default: "0.100000"
    widget: {min: 0, max: 1, step: 0.05, decimals: 6}
    effect: Sets the highest sample that counts as background. Lower leaves stars and nebula out. Higher keeps more of a background that sits high. A sample equal to the limit is kept.
    range: The slider moves in thousandths. It must lie above the background of every channel, or that channel is measured on its darkest samples only.
    tradeoff: Too high lets stars and bright structure into the median. Too low cuts a channel's background, or leaves it without a sample.
    scope: The measurement only, in all three channels.
    persistence: Until the window closes. **Reset** and another target return it to its default.
  - name: Region of interest
    kind: check
    default: "Off"
    effect: On, measures the rectangle that **Left**, **Top**, **Width** and **Height** give, in pixels of the reference. Off, measures the whole reference.
    range: Ticking it sets the rectangle to the whole reference. Unticking it returns the four boxes to 0 and stops **From Selection**. Choosing another reference while it is ticked sets the rectangle to the whole new reference.
    tradeoff: A small rectangle of clean sky keeps stars and nebula out, and holds fewer samples.
    scope: The reference only. OK changes the whole target whatever the rectangle.
    persistence: Until the window closes. **Reset** and another target untick it.
  - name: Left
    kind: spinbox
    default: "0"
    widget: {min: 0, max: 99, step: 1}
    effect: Sets the rectangle's left edge. Higher moves it right.
    range: The 99 the box opens with becomes the reference's width less 1 when you tick **Region of interest**. The maximum of **Width** follows, as the reference's width less **Left**.
    enabled: With **Region of interest** ticked.
    scope: The measurement. While **From Selection** is on, a change moves the rectangle on the reference's view.
    persistence: Until the window closes. **Reset** and unticking **Region of interest** return it to 0.
  - name: Top
    kind: spinbox
    default: "0"
    widget: {min: 0, max: 99, step: 1}
    effect: Sets the rectangle's top edge. Higher moves it down.
    range: The 99 the box opens with becomes the reference's height less 1 when you tick **Region of interest**. The maximum of **Height** follows, as the reference's height less **Top**.
    enabled: With **Region of interest** ticked.
    scope: The measurement. While **From Selection** is on, a change moves the rectangle on the reference's view.
    persistence: Until the window closes. **Reset** and unticking **Region of interest** return it to 0.
  - name: Width
    kind: spinbox
    default: "0"
    widget: {min: 0, max: 99, step: 1}
    effect: Sets the rectangle's width. A narrow rectangle holds fewer samples.
    range: Ticking **Region of interest** sets it, and its maximum, to the reference's width. OK refuses 0 and a rectangle that leaves the reference.
    enabled: With **Region of interest** ticked.
    persistence: Until the window closes. **Reset** and unticking **Region of interest** return it to 0.
  - name: Height
    kind: spinbox
    default: "0"
    widget: {min: 0, max: 99, step: 1}
    effect: Sets the rectangle's height. A short rectangle holds fewer samples.
    range: Ticking **Region of interest** sets it, and its maximum, to the reference's height. OK refuses 0 and a rectangle that leaves the reference.
    enabled: With **Region of interest** ticked.
    persistence: Until the window closes. **Reset** and unticking **Region of interest** return it to 0.
  - name: From Selection
    kind: button
    default: "Off"
    effect: On, lets you draw the rectangle on the reference's view. Drag a rectangle there, or move the one drawn, and the four boxes follow. Off stops drawing and keeps the rectangle.
    range: Turning it on clears the four boxes until you drag. Turning it off while **Width** or **Height** is 0 returns the rectangle to the whole reference.
    enabled: With **Region of interest** ticked. Choosing another reference stops it.
    tradeoff: It places the rectangle and changes no sample.
    scope: The reference's view. The target's view is drawn on only when it is the reference.
    persistence: "**Reset**, unticking **Region of interest** and closing the window stop it."
  - name: Target Image
    kind: combo
    default: The active image
    options: [The active image, Every other open RGB image or sequence]
    effect: Chooses the image or sequence OK replaces. Choosing another one returns every other control to its default.
    tradeoff: The image chosen here is the one replaced.
    persistence: Until the window closes. **Reset** leaves it as it is.
  - name: OK
    kind: button
    default: ""
    effect: Measures the reference and changes the whole image, or every frame of the sequence. The window closes once the view shows the result.
    enabled: At once, whatever the values. While the run works, OK, **Cancel** and **Reset** are disabled. The other controls stay editable, and a change does not reach the run.
    tradeoff: The run holds floating-point working copies of the reference and of the image. A single image shows no progress window, and its run cannot be stopped once it starts. A sequence needs the disk space of a second copy.
    scope: Every channel, through the mask on the view. Every frame of a sequence, whatever frames its player has selected, debayered first, by the method the sequence states, when the view debayers them.
    persistence: "The image and its history keep the result, as the step **Background Neutralization** with the text `Reference image '<name>' (ROI X:<x>, Y:<y>, W:<width>, H:<height>)`. The name is the target's own when it is its own reference, and the rectangle is the whole reference while **Region of interest** is off. The limits are not recorded."
    note: "A refused or failed run leaves the window open, and the image and its history as they were. A failure shows \"Error running background neutralization\" with \"An error occurred when executing the background neutralization process:\" and the reason, and writes them to the log. While another run holds a sequence, the busy message comes under the title Background Neutralization and nothing starts. When the history cannot keep the image, it says so once in a message titled \"Image History\"."
  - name: Cancel
    kind: button
    default: ""
    effect: Closes the window without changing the image.
    enabled: Disabled while a run works.
    note: Closing the window another way while a run works hides it, and it closes when the run has ended. The run goes on, on a sequence until the progress window's **Cancel** stops it.
  - name: Reset
    kind: button
    default: ""
    effect: Returns **Reference image** to `<--TARGET IMAGE-->` and both limits to their defaults, unticks **Region of interest**, returns its boxes to 0 and stops **From Selection**.
    range: It leaves **Target Image** as it is.
    enabled: Disabled while a run works.
```

```controls
window: Sequence progress
controls:
  - name: Progress
    kind: readout
    default: ""
    effect: "Shows the share of the frames done, under the title **Background Neutralization** and the text `Executing background neutralization to sequence <name>`."
    enabled: Only while OK works on a sequence. A single image has no progress window.
    tradeoff: The new sequence is written to a temporary file, which needs its disk space until the run ends.
  - name: Cancel
    kind: button
    default: ""
    effect: Stops the run, before the next frame at the latest, without a message. The sequence stays as it was, with no history step, and the temporary file is removed.
```

## How it works

1. **OK** reads the reference, the limits, the rectangle and the view's mask. A change made afterwards does not reach
   the run.
2. In each channel of the reference, inside the rectangle, the samples that are finite, valid under the image's
   [validity rule](../../reference/glossary.md#validity-rule) and between the two limits, both included, are kept.
   Samples are read as a [fraction of full scale](../../reference/glossary.md#fraction-of-full-scale). The exact median
   of the kept samples is the channel's background level.
3. The mean of the three levels is the shared level. Each channel's offset is the shared level less its own.
4. Each valid sample of the target gets its channel's offset, is held between 0 and 1, and is written back in the
   image's pixel type, rounded to the nearest value. The work is done in single precision, or double for a 64-bit
   floating-point or 32-bit integer image. A mask on the view then blends the result with the original.
5. The result replaces the target with one history step. Its header, acquisition data,
   [plate solution](../../reference/glossary.md#plate-solution) and
   [processing state](../../reference/glossary.md#processing-state) are kept, and its
   [lineage](../../reference/glossary.md#lineage) gains an entry with the detail
   `background neutralization: gains 1.000000, 1.000000, 1.000000; offsets <red>, <green>, <blue>`.

### Model

A channel's background level is the median of its kept samples: the middle one, or the mean of the two middle ones
for an even count. With \(m_R\), \(m_G\) and \(m_B\) the three levels, the shared level is their mean:

\[ L = \frac{m_R + m_G + m_B}{3} \tag{1} \]

A sample \(x_c\) of channel \(c\), between 0 and 1, then becomes

\[ x'_c = \min\bigl(1,\ \max(0,\ x_c + L - m_c)\bigr) \tag{2} \]

When the image is its own reference and nothing is held at 0 or 1, each channel's level lands exactly on \(L\). With a
separate reference, its offsets are applied to the target unchanged, whatever the target's own levels.

### Boundaries

- A channel left without a kept sample is refused before anything changes, with "No green sample of the background
  reference lies between the limits 0 and 0.1; widen the limits or choose another reference", naming the channel and
  the limits.
- A ticked **Region of interest** that is empty or leaves the reference is refused with "The background ROI must be
  fully inside the reference image and have a non-zero size.", and nothing starts. A reference view that holds no
  image when you press **OK** is refused with "The selected background reference image is no longer available."
- Every result is held between 0 and 1: a negative offset can clip the darkest background at 0 and a positive one
  star cores at 1. A floating-point sample above 1 is cut to 1 even where the offset is 0.
- A pixel that the image's validity rule rejects is left as it was.
- A single image's run has no progress window and cannot be stopped once it starts.

### Worked example

An RGB image composed with Channel Composition from the night's red, green and blue
[masters](../../reference/glossary.md#master), with its **Linear Match** off, keeps each filter's own sky. Image
Statistics gives its channels medians of 0.0277, 0.0800 and 0.1031, so its background is blue-green. With the default
limits, blue is measured on the 3 % of its samples below 0.1: its level reads 0.0997, and after **OK** blue stays
0.0034 above the other two.

Set **Upper Limit** to 0.2 and leave the rest. The levels are then 0.0277, 0.0800 and 0.1031, equation (1) gives a
shared level of 0.0702, and equation (2) adds 0.0426 to red, -0.0097 to green and -0.0328 to blue. After **OK** every
channel's median is 0.0702, and the history step reads `Reference image '<name>' (ROI X:0, Y:0, W:7246, H:5429)`. On
the master the other pages use, whose channels were already matched, the offsets are below 0.00001 and nothing
visible changes.

```figure
images:
  - image: ../../assets/images/processes/color-calibration/background-neutralization-before.webp
    alt: The whole composed frame before Background Neutralization, the sky a strong blue-green under the nebula's clouds.
    label: Before
  - image: ../../assets/images/processes/color-calibration/background-neutralization-after.webp
    alt: The whole frame after Background Neutralization, the sky a neutral dark gray under the same clouds.
    label: After
caption: >-
  The composed image before and after OK with Upper Limit at 0.2. Both are the whole frame stretched afterwards by
  Histogram Transformation with Shadows 0.06 and Midtones 0.03, and shown in the Linear display mode.
```

## Starting points

| Situation | Start | Then adjust |
| --- | --- | --- |
| An image composed from mono masters | `<--TARGET IMAGE-->`, **Upper Limit** above the brightest channel's median | Check each channel's median in Image Statistics after **OK**. |
| Nebula fills most of the frame | **Region of interest** over the darkest clean sky | Lower **Upper Limit** below the faint nebula inside the rectangle. |
| Black borders after registration | **Lower Limit** just above the borders' level | Crop the borders first when you can. |
| A correction in one part of the image | The settings above, with the mask off | Enable a soft mask, check its edges, then press **OK**. |

## When it goes wrong

!!! warning "OK changes the image without a preview"
    The window shows no result before **OK**, and the step replaces the image. Read the channels' medians first, and
    use the image's history to return to the image before the step.

"Error running background neutralization", with "No red sample of the background reference lies between the limits"
:   **Cause.** That channel has no finite, valid sample between the limits inside the rectangle, or **Lower Limit** is
    above **Upper Limit**.
:   **Fix.** Widen the limits around the channel's median, move the rectangle, or choose another reference.

One channel still sits above the other two after OK
:   **Cause.** Its background lies above **Upper Limit**, so it was measured on its darkest samples only.
:   **Fix.** Undo the step, raise **Upper Limit** above that channel's median, and press **OK** again.

"The background ROI must be fully inside the reference image and have a non-zero size."
:   **Cause.** **Region of interest** is ticked, and its rectangle is empty or reaches past the reference.
:   **Fix.** Untick it to measure the whole reference, or draw the rectangle again with **From Selection**.

"The selected background reference image is no longer available."
:   **Cause.** The view chosen in **Reference image** held no image when you pressed **OK**.
:   **Fix.** Choose an open RGB image, or `<--TARGET IMAGE-->`, and press **OK** again.

The background is neutral, but the nebula's colors are wrong
:   **Cause.** The window adds an offset to each channel and changes no gain.
:   **Fix.** Balance the gains next, with Color Calibration, Photometric Color Calibration or Spectrophotometric Color
    Calibration.

A cast remains in a corner or along one side
:   **Cause.** The cast is a gradient, and one constant per channel cannot follow it.
:   **Fix.** Undo the step, remove the gradient with Background Extraction or Auto Gradient Removal, then neutralize.

Star cores or the darkest sky are clipped after OK
:   **Cause.** The offsets pushed samples past 0 or 1, or the reference does not match the target.
:   **Fix.** Undo the step, measure the image itself or a clean rectangle of it, and check for samples above 1 first.

A signed integer image is refused with the default limits
:   **Cause.** Its zero code reads near 0.5, above **Upper Limit**.
:   **Fix.** Read its medians in Image Statistics and set both limits around them.

## Related

- [Background Extraction](../background-extraction/background-extraction.md) and
  [Auto Gradient Removal](../background-extraction/auto-gradient-removal.md), which remove the gradients first.
- [Color Calibration](color-calibration.md), [Photometric Color Calibration](photometric-cc.md) and
  [Spectrophotometric Color Calibration](spectrophotometric-cc.md), which balance the channels' gains after.
- [Linear Match](linear-match.md), which matches each channel's level and spread to another image's.
- [Channel Composition](../color-composition/channel-composition.md), which composes an RGB image from mono masters.
- [Image Statistics](../../interface/image-statistics.md), to read each channel's median before and after.
- [Process Windows](../../interface/process-windows.md), for what every process window shares.
