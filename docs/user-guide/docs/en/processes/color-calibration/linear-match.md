---
kind: process
stage: color
menu: Process > Color Calibration > Linear Match
summary: >-
  Scales and offsets each channel of an image so that its level and spread match a reference image's, to bring the
  masters of different filters or sessions to one level before they are combined.
data_in: linear
data_out: linear
destructive: true
preview: none
sequences: true
masks: none
external: none
glance:
  enabled: >-
    An image or a sequence view is active. OK waits until Reference Image names another open image with the same
    number of channels.
  inputs: >-
    One image or sequence of one to four channels, and a reference image of the same number of channels, of any size
    and pixel type.
  outputs: >-
    The same view, replaced, with one history step. Its size, pixel type and header are kept. The scales and offsets are
    written in its lineage only. No file, preset or report is written.
  state: >-
    Linear masters of one field, registered and cropped alike, before they are combined or stacked together.
derived: false
---
# Linear Match

Linear Match makes an image's level and spread match those of a reference image. For each channel it takes the ratio
of the two images' [median absolute deviations](../../reference/glossary.md#median-absolute-deviation) as a scale, and
the offset that moves the image's median onto the reference's. Images of one size are measured over the positions both
keep, and images of two sizes each on its own.

**OK** replaces the image, or every frame of a [sequence](../../reference/glossary.md#sequence), adds one step to its
[history](../../reference/glossary.md#history) and closes the window. The size, the
[pixel type](../../reference/glossary.md#pixel-type) and the header are kept, and the result is not clamped. The window
has no preview.

```figure
image: ../../assets/images/processes/color-calibration/linear-match.webp
alt: >-
  The Linear Match window. Reference Image names the red master. Reject low reads 0.000 with its slider at the left,
  and Reject high 0.920 with its slider near the right. Target Image names the green master, and OK and Close sit at
  the bottom.
caption: >-
  Linear Match on the night's green master, with the red master as its reference and the default limits.
pins:
  - {n: 2, x: 3.0, y: 33.8}
  - {n: 3, x: 3.0, y: 59.2}
  - {n: 4, x: 50.0, y: 91.3}
```

## Before you start

- Decide which image sets the level. Linear Match changes **Target Image** to resemble **Reference Image**, never the
  reverse, and never changes the reference.
- Use [linear images](../../reference/glossary.md#linear-image). The window checks neither the images' linearity nor
  their [processing state](../../reference/glossary.md#processing-state).
- Images of one width and height are paired by position: register and crop them alike. Images of two sizes are matched
  by each channel's median and spread alone, and one pixel of difference switches between the two measurements.
- A sample of exactly zero is never measured, in either image. Black borders therefore need no limit, but samples just
  above zero do.
- Both images need the same number of channels, up to four. Their sizes and pixel types may differ: each sample is
  measured as a [fraction of full scale](../../reference/glossary.md#fraction-of-full-scale) of its own pixel type.
- **OK** replaces the target, and its history keeps the step before. Save a copy to keep the original outside the
  session. A [mask](../../reference/glossary.md#mask) on the view is not used.

## Steps

1. **Open both images.** Open the target and the reference, make the target the
   [active image](../../reference/glossary.md#active-image) and choose **Process > Color Calibration > Linear Match**.
2. **Choose the reference.** In **Reference Image**, choose the image whose level and spread the target should take.
   Until you do, it reads `<--NO IMAGE-->` and **OK** is disabled.
3. **Set the limits.** Keep **Reject high** below the brightest stars and clipped cores, so that they stay out of the
   measurement. Raise **Reject low** only to leave dark artifacts above zero out.
4. **Match.** Choose **OK**. The window closes, and the history lists **Linear Match** with both images and the limits.
5. **Check.** Choose **Image > Information > Statistics** and compare each channel's median with the reference's. They
   now agree closely.

## Controls

What every process window shares is on [Process Windows](../../interface/process-windows.md).

```controls
window: Linear Match
controls:
  - name: Reference Image
    kind: combo
    default: "<--NO IMAGE-->"
    options: ["<--NO IMAGE-->"]
    effect: Chooses the image whose level and spread the target takes. The image its view shows when you press OK is the one measured.
    range: It also lists every other open image or sequence with the target's number of channels, of any size and pixel type. The target is never listed. A sequence's view offers the frame it shows. Each item's tooltip describes the image.
    enabled: The list follows the views as they open, change, are renamed or close. Closing the chosen view returns it to `<--NO IMAGE-->`, and **OK** is disabled while it reads so.
    tradeoff: A reference of another field or another state passes its level and spread on. Images of one size that are not registered alike pair unrelated samples, and give a meaningless scale. Each channel of a color image is matched on its own, so a color cast of the reference passes on too. An encoded hue or chroma component has no level or spread to match.
    scope: OK, on every channel. Every frame of a sequence is matched to the same reference.
    persistence: Until the window closes. Choosing another target returns it to `<--NO IMAGE-->`.
    note: The reference is never changed.
  - name: Reject low
    kind: spinbox
    default: "0.000"
    widget: {min: 0, max: 1, step: 0.01, decimals: 3}
    effect: Sets the lowest sample measured, as a fraction of full scale. Higher leaves darker samples out of the measurement. A sample equal to the limit is kept, and a sample of exactly zero is left out whatever the limit.
    range: Its three decimals hold every position of the slider, which moves in thousandths. Nothing keeps it below **Reject high**, and limits that keep no sample are refused when the run measures.
    tradeoff: Too high leaves too few samples. It never chooses which samples change.
    scope: The measurement only, in every channel. For images of one size it holds the reference's samples only, and a position is left out of both images where the reference's sample is. For images of two sizes it holds each image's samples.
    persistence: Until the window closes. The window has no **Reset**.
  - name: Reject high
    kind: spinbox
    default: "0.920"
    widget: {min: 0, max: 1, step: 0.01, decimals: 3}
    effect: Sets the highest sample measured. Lower leaves bright stars and clipped cores out of the measurement. At 1, samples at full scale are kept too. A sample equal to the limit is kept.
    range: Its three decimals hold every position of the slider, which moves in thousandths.
    tradeoff: Lower keeps stars and clipping out. Too low leaves too few samples, or a nearly flat set that no longer stands for the spread.
    scope: The measurement only, in every channel, held to the same images as **Reject low**.
    persistence: Until the window closes.
  - name: Target Image
    kind: combo
    default: The active image
    options: [The active image, Every other open image or sequence]
    effect: Chooses the image or sequence OK replaces. Choosing another one lists the references of its number of channels, and returns **Reference Image** to `<--NO IMAGE-->`.
    tradeoff: The image chosen here is the one replaced.
    persistence: Until the window closes.
  - name: OK
    kind: button
    default: ""
    effect: Measures the target against the reference and changes the whole image, or every frame of the sequence. The window closes once the view shows the result.
    enabled: While **Reference Image** names an image. While the run works, OK and **Close** are disabled. The other controls stay editable, and a change does not reach the run.
    tradeoff: The run holds floating-point working copies of both images, and a bit a sample for those it measures. A single image shows no progress window, and its run cannot be stopped once it starts. A sequence measures every frame against the reference again, and needs the disk space of a second copy.
    scope: Every channel, without the mask on the view. Every frame of a sequence, whatever frames its player has selected, debayered first, by the method the sequence states, when the view debayers them.
    persistence: "The image and its history keep the result, as the step **Linear Match** with the text `Target image '<target>', Reference image '<reference>', Reject Low: 0, Reject High: 0.92` for the default limits. Over a sequence it begins `Target sequence`."
    note: "A reference view that holds no image when you press OK is refused with \"The selected reference image is no longer available.\", under the title Linear Match, and nothing runs. A failure shows \"Error running linear match\" with \"An error occurred when executing the linear match process:\" and the reason, writes them to the log, and leaves the image, its history and the window as they were. While another run holds a sequence, the busy message comes under the title Linear Match and nothing starts. When the history cannot keep the image, it says so once in a message titled \"Image History\"."
  - name: Close
    kind: button
    default: ""
    effect: Closes the window without applying.
    enabled: Disabled while a run works.
    note: Closing the window another way while a run works hides it, and it closes when the run has ended. The run goes on, on a sequence until the progress window's **Cancel** stops it.
```

```controls
window: Sequence progress
controls:
  - name: Progress
    kind: readout
    default: ""
    effect: "Shows the share of the frames done, under the title **Linear Match** and the text `Executing linear match to sequence <name>`."
    enabled: Only while OK works on a sequence. A single image has no progress window.
    tradeoff: The new sequence is written to a temporary file, which needs its disk space until the run ends.
  - name: Cancel
    kind: button
    default: ""
    effect: Stops the run, before the next frame at the latest, without a message. The sequence stays as it was, with no history step, and the temporary file is removed. The Linear Match window stays open.
```

## How it works

1. **OK** reads both limits and the image the reference's view shows. A change made afterwards does not reach the run.
2. Both images are read as fractions of full scale, in single precision, or double for a 64-bit floating-point or
   32-bit integer image. A sample is measured when it is finite, valid under its image's
   [validity rule](../../reference/glossary.md#validity-rule) and not exactly zero.
3. Images of one width and height are paired: a position counts in a channel when both images keep it there and the
   reference's sample lies between the limits. Images of two sizes are measured each on its own, both held to the
   limits.
4. Each channel's median and median absolute deviation give a scale and an offset. Every sample the target's validity
   rule admits is then written as the scale times the sample plus the offset, whatever the limits measured, with no
   clamp. It is brought back to the image's pixel type, rounded to the nearest value, and an integer stops at the ends
   of its range.
5. The result replaces the target with one history step. Its header, acquisition data,
   [plate solution](../../reference/glossary.md#plate-solution) and processing state are kept, and its
   [lineage](../../reference/glossary.md#lineage) gains an entry with the detail `linear match:` and each channel's
   scale and offset, named `red`, `green` and `blue` in a color image and `channel 1` and so on otherwise.

### Model

For channel \(c\), let \(T_c\) be the target's kept samples and \(R_c\) the reference's, over the shared positions for
images of one size. With \(\operatorname{med}\) the median and
\(\operatorname{MAD}(X) = \operatorname{med}\bigl(|X - \operatorname{med}(X)|\bigr)\), the scale \(a_c\) and the
offset \(b_c\) are

\[ a_c = \frac{\operatorname{MAD}(R_c)}{\operatorname{MAD}(T_c)}, \qquad b_c = \operatorname{med}(R_c) - a_c\,\operatorname{med}(T_c) \tag{1} \]

and every admitted sample \(x_c\) of the target becomes

\[ x'_c = a_c\,x_c + b_c \tag{2} \]

So the target's median lands on the reference's, and its spread takes the reference's. A target sample of 0, which is
never measured, still becomes \(b_c\). When either deviation is zero, because more than half the kept samples share
one value, the scale stays 1 and only the medians are matched, without a message.

On samples that already lie on one straight line, this match and a least-squares line agree exactly. On real data, a
bright star or a gradient moves a least-squares line but barely moves a median or a median absolute deviation. The
match carries one level and one spread per channel: not a histogram's shape, a gradient or any spatial structure.

### Boundaries

- Limits that keep no sample in some channel are refused, and nothing changes. For images of one size the reason is
  "A channel of the image and its reference shares no position whose reference sample lies between the limits 0 and
  0.92 and where neither sample is zero; widen the limits or choose another reference". For two sizes it is "A channel
  of the image or of its reference holds no sample that lies between the limits 0 and 0.92 and is not zero; widen the
  limits or choose another reference". Neither names the channel.
- A reference of another number of channels is refused with "Reference and target images must have the same number of
  channels". **Reference Image** offers none, so only a sequence whose frames differ from its view's image meets it.
  An image of more than four channels is refused too.
- A floating-point result keeps samples below 0 and above 1, and an integer result stops at the ends of its range.
- A pixel that the target's validity rule rejects is left as it was.
- A single image's run has no progress window and cannot be stopped once it starts.

### Worked example

The night's green and red [masters](../../reference/glossary.md#master) are linear, 7246 by 5429 pixels and registered
alike, so their samples are paired. In Image Statistics the green background sits at 0.0800 with a median absolute
deviation of 0.00108, and the red one at 0.0277 with 0.00175. Open both, make the green master active, choose the red
master in **Reference Image**, keep the limits and choose **OK**.

Equation (1) gives a scale of 1.6242 and an offset of -0.1022, and equation (2) brings the green background to 0.0277
and its deviation to the red's 0.00175. A star core at 1.0 becomes 1.52 and stays there, unclamped: 7309 samples end
above 1 and 12 below 0. The result has the median, the spread, the minimum and the maximum of the green channel of the
RGB master the other pages use, and the history reads
`Target image '<green>', Reference image '<red>', Reject Low: 0, Reject High: 0.92`.

```figure
images:
  - image: ../../assets/images/processes/color-calibration/linear-match-before.webp
    alt: The whole green master before Linear Match, a pale gray frame with the nebula faint and washed out.
    label: Before
  - image: ../../assets/images/processes/color-calibration/linear-match-after.webp
    alt: The whole green master after Linear Match, a dark gray background with the nebula's structure spread above it.
    label: After
caption: >-
  The green master before and after OK with the red master as its reference. Both are the whole frame stretched
  afterwards by Histogram Transformation with Shadows and Midtones at 0.02, and shown in the Linear display mode.
```

## Starting points

| Situation | Start | Then adjust |
| --- | --- | --- |
| Masters of one field from different filters, before Channel Composition | One master as the reference for the others, default limits | Compare the medians in Image Statistics, and calibrate the color after composing: the match sets each channel's spread, not the color balance. |
| Masters of two sessions of one target | The better session as the reference, both registered and cropped alike | Lower **Reject high** if bright stars dominate. |
| Bright stars or clipped cores fill the field | **Reject high** at 0.5 | Lower it in small steps, watching that enough samples stay. |
| Images of two sizes | Comparable linear images with broad histograms inside the limits | Remove gradients and crop defects first, then change one limit at a time. |

## When it goes wrong

!!! warning "OK changes the target, and the result can leave 0 to 1"
    The match is not clamped, so a floating-point image can hold samples below 0 and above 1 afterwards. Use the
    image's history to return to the image before the step.

OK is disabled
:   **Cause.** **Reference Image** reads `<--NO IMAGE-->`: none is chosen, the chosen view closed, or the target changed.
:   **Fix.** Choose a reference.

The image you want is not in Reference Image
:   **Cause.** It has another number of channels than the target, or it is the target.
:   **Fix.** Choose another target or reference. The size and the pixel type need not match.

"A channel of the image and its reference shares no position whose reference sample lies between the limits"
:   **Cause.** The limits keep no sample of some channel, or **Reject low** is above **Reject high**.
:   **Fix.** Widen the limits, keep **Reject low** below **Reject high**, or choose another reference.

"The selected reference image is no longer available."
:   **Cause.** The reference's view held no image when you pressed **OK**.
:   **Fix.** Choose another reference and press **OK** again.

"Error running linear match"
:   **Cause.** The run was refused for the reason the message gives, such as a sequence frame of another number of
    channels, or memory.
:   **Fix.** Read the reason, free memory or choose a matching reference, and press **OK** again.

The result changed after one image was cropped or resized
:   **Cause.** Images of one size are paired by position, and images of two sizes are measured each on its own.
:   **Fix.** Give both images one size when they show one field, registered alike.

Samples below 0 or above 1 after OK
:   **Cause.** The match is not clamped, and a scale above 1 carries star cores past full scale.
:   **Fix.** Keep them for the next linear step, or choose a closer reference or other limits.

The mask on the view had no effect
:   **Cause.** Linear Match does not use a mask.
:   **Fix.** Match the whole image, or use a process that blends through a mask.

## Related

- [Channel Composition](../color-composition/channel-composition.md), which composes the matched masters and has a
  linear match of its own: use one of the two, not both.
- [Image Registration](../registration-alignment/image-registration.md) and
  [Background Extraction](../background-extraction/background-extraction.md), before a match of images of one size.
- [Background Neutralization](background-neutralization.md), which moves only the levels, measured in one reference.
- [Color Calibration](color-calibration.md), [Photometric Color Calibration](photometric-cc.md) and
  [Spectrophotometric Color Calibration](spectrophotometric-cc.md), which balance color rather than match a reference.
- [Image Statistics](../../interface/image-statistics.md), to compare the medians and deviations before and after.
- [Process Windows](../../interface/process-windows.md), for what every process window shares.
