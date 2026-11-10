---
kind: process
stage: finish
menu: Process > Adjustments > Exposure
summary: >-
  Multiplies the image, or one of its color components, by one factor and holds the result between black and white,
  for a small overall brightening or darkening.
data_in: either
data_out: nonlinear
destructive: true
preview: real-time
sequences: true
masks: post-blend
external: none
glance:
  enabled: >-
    An image or a sequence view is active, no Exposure Correction window is open on it yet, and no window that needs
    the image for itself is open on it.
  inputs: >-
    One grayscale or RGB image or sequence with samples between 0 and 1, and an optional mask of the same size on
    its view.
  outputs: >-
    The same view, replaced, with one history step. Its size and pixel type are kept. No file, preset or report is
    written.
  state: >-
    A stretched image that needs a small overall brightening or darkening. On linear data the factor is a change of
    exposure, but the result is recorded as nonlinear, so every step that needs linear data comes first.
derived: false
rewritten: true
---
# Exposure Correction

Exposure Correction multiplies every sample of the image by one factor and holds the result between black and
white. A factor above 1 brightens the image as a longer exposure would, and one below 1 darkens it. On an RGB image
the factor can scale one channel or one color component instead.

**Exposure** sets the factor, between 0.02 and 2. It is not a number of
[exposure stops](../../reference/glossary.md): a value of 1 multiplies by 1.2. No value makes a black
[linear image](../../reference/glossary.md) visible, which is the work of a stretch.

**Apply** replaces the image or sequence with the scaled one, keeps its size and pixel type, and adds one step to
its history. The result is recorded as nonlinear.

```figure
image: ../../assets/images/processes/stretching/exposure-correction.webp
alt: >-
  The Exposure Correction window. On the left, a histogram with one broad peak near its left third and the lines
  Clipping Low and Clipping High in its corner. Under it a toolbar, Exposure at 1.50 with its slider right of the
  middle, Target Image, Channel at RGB and Preview checked. On the right, the preview shows a red nebula on a gray
  background. Reset, Close and Apply run along the bottom.
caption: >-
  Exposure Correction on a stretched master with Exposure at 1.50, a factor of 1.3. The histogram is counted from the
  brightened preview, and the preview shows what Apply writes.
pins:
  - {n: 2, x: 8.7, y: 84.3}
  - {n: 3, x: 2.1, y: 54.0}
  - {n: 4, x: 2.6, y: 18.9}
  - {n: 5, x: 45.3, y: 40.0}
  - {n: 6, x: 90.6, y: 96.0}
```

## Before you start

- Exposure Correction does not stretch. Its largest factor is 2, which leaves a linear master black in the view's
  **Linear** display mode.
- On linear data the factor is a true change of exposure until a sample clips, and on stretched data it only
  scales the displayed values. Either way **Apply** records the result as nonlinear: run every step that needs
  linear data first.
- **Apply** replaces the image or sequence. Its history keeps the step before, and what a factor above 1 pushes
  past white is lost.
- Use a grayscale or an RGB image with finite samples between 0 and 1. A raw color-filter mosaic is scaled as a
  grayscale image and is not debayered.
- A [mask](../../reference/glossary.md) on the view blends the scaled image with the original afterwards: white
  takes the result, black keeps the original, gray mixes them, and **Invert Mask** swaps the two.
- On a sequence, one value and one **Channel** choice are used for every frame. Check them, the mask and the
  automatic debayer of the sequence before you Apply.

## Steps

1. **Open the window.** Make the image the active one and choose **Process > Adjustments > Exposure**. The window
   opens as **Exposure Correction**, its preview shows the image as its view displays it, and **Apply** is
   disabled.
2. **Choose the channel.** Leave **Channel** at **RGB** to scale the three channels by one factor. Choose a single
   channel or component only to change color on purpose.
3. **Set the exposure.** Raise **Exposure** above 0 to brighten, or lower it below -0.10 to darken. The preview
   follows, and the peak of the histogram moves right or left.
4. **Watch the highlights.** Read **Clipping High** in the corner of the histogram while you brighten. Lower the
   value again when it starts to rise: what it counts has become white.
5. **Check the preview at 100 %.** Zoom the preview into the brightest stars and the brightest structure, and press
   ++space++ over it to compare with the original. Star cores that turn flat or change color have clipped in a
   channel.
6. **Apply.** Choose **Apply**. The image in its view is the scaled one, its history lists **Exposure Correction**
   with the value you set, and the controls return to their defaults.

## Controls

The toolbar under the histogram - zoom, grid, clipping lines, scales, areas, the histogram itself and **Plot
resolution** - changes only how the histogram is drawn, and is the same in every window that has one: see
[Process Windows](../../interface/process-windows.md).

```controls
window: The histogram
controls:
  - name: Histogram
    kind: readout
    default: ""
    effect: Shows how the samples of the image are spread between 0 and 1. With **Preview** on it is counted from the scaled preview, mask included. With **Preview** off it is counted from the image as it is.
    range: One line for a grayscale image, three for RGB, in the bins of the toolbar's **Plot resolution**. A finer resolution changes the drawing only, not the factor or the precision of the result.
    enabled: Shown while the toolbar's histogram button is on. Its lines follow the channel the preview displays, and it shows the original while the preview does.
    tradeoff: It is counted again after every preview, which adds to the time each change takes. While you drag the slider it may be counted from a reduced copy of the image, and the whole image follows when you release it.
    scope: The window only. It never changes a sample.
    persistence: Counted again when the target, the preview or **Plot resolution** changes.
    note: Holding the left button shows the coordinates under the pointer until you release it. Dragging with the middle button pans, and ++ctrl++ with the wheel zooms about the pointer. **Reset** leaves the zoom as it is.
  - name: Clipping Low
    kind: readout
    default: ""
    effect: Gives the share of the samples the histogram counts that are at or below 0. In a floating-point image no factor sends a sample above 0 to black, so this share comes from samples that were already there. In an integer image a strong darkening also rounds the lowest values to 0.
    range: A percentage with three decimals, over all the channels together. It is counted from the samples, not from the bins, so it does not change with **Plot resolution**.
    enabled: Shown while the toolbar's clipping button is on, as it is when the window opens.
    scope: The window only.
  - name: Clipping High
    kind: readout
    default: ""
    effect: Gives the share of the samples the histogram counts that are at or above 1. In the scaled preview those are the samples at or above 1 divided by the factor.
    range: Counted as **Clipping Low** is.
    enabled: Shown while the toolbar's clipping button is on.
    tradeoff: It counts every sample at white, also those that were white before the factor and those a mask left alone. Compare it with **Preview** off to see what the factor adds.
    scope: The window only.
```

```controls
window: Exposure Correction
controls:
  - name: Exposure
    kind: spinbox
    default: "0.00"
    widget: {min: -5, max: 5, step: 0.03, decimals: 2}
    effect: Sets the factor every sample is multiplied by. Above 0 it brightens, up to twice at the top of the range. Below -0.10 it darkens, down to a fiftieth at the bottom.
    range: The slider beside the box moves the same value in steps of 0.01. At 0 the factor is 1 and nothing changes. At -0.10 the factor is 1 again, and between -0.10 and 0 the image is slightly brightened.
    enabled: Disabled while Apply runs. Any value but 0 enables **Apply**.
    tradeoff: A factor above 1 sends every sample above 1 divided by the factor to white, which no later step brings back. The value is not a number of stops, and the label does not say which factor it stands for.
    scope: The preview and Apply, on the channel or component **Channel** selects, on images and sequences. A mask is blended afterwards.
    persistence: Until the window closes. Each preview and each Apply takes the value as it is when it starts. **Reset** and a finished Apply return it to its default.
    note: The tooltip of the box and of the slider states the two factors.
  - name: Target Image
    kind: combo
    default: The active image
    options: [The active image, Every other open image or sequence the window can work on]
    effect: Chooses the image Apply replaces, with the mask on its view. The title of the window, the choices of **Channel**, the histogram and the preview follow it.
    enabled: Disabled while Apply runs.
    tradeoff: Apply replaces the image chosen here, so check it before you Apply. A sequence is written again in a temporary place and replaced only when every frame is done.
    persistence: Until the window closes. **Reset** leaves it as it is.
  - name: Channel
    kind: combo
    default: RGB
    options: [RGB, Red, Green, Blue, CIE L* (Lightness), CIE a*, CIE b*, CIE c*, CIE h*, CIE X, CIE Y (Luminance), CIE Z, HSL Hue, HSL Saturation, HSL Lightness, HSV Hue, HSV Saturation, HSV Value, YCbCr Luma, YCbCr Blue Chroma, YCbCr Red Chroma]
    effect: Chooses what the factor multiplies. With **RGB** it is the three stored channels. With **Red**, **Green** or **Blue** it is that channel, and the other two stay as they are. Every other choice converts the image, multiplies that one component and converts back to red, green and blue.
    range: The lightness, luminance, value and luma choices scale brightness from black. The saturation choices and the CIE chroma scale the amount of color from gray. The CIE a and b choices and the two YCbCr chroma choices are neutral at the middle of their scale, so a factor moves gray itself and tints the whole image. The three hue choices hold a hue as a fraction of a turn, which is multiplied and cut as a plain number, not turned as an angle. **CIE X** and **CIE Z** shift the balance between the colors.
    enabled: For an RGB image. A grayscale image shows a disabled **Grayscale** instead, and its one channel is multiplied. Disabled while Apply runs.
    tradeoff: Only **RGB** keeps the ratios between the colors, and only until a channel clips. Every other choice changes color on purpose and is not a change of exposure. A component choice adds two conversions to every preview and Apply.
    scope: The preview and Apply, on images and sequences. Only the chosen channel or component is held between 0 and 1. After a component choice the rebuilt colors of a floating-point image can leave that range, and those of an integer image stop at the limits of its type.
    persistence: Until the window closes. Each preview and each Apply takes the choice as it is when it starts. **Reset** and a finished Apply return an RGB image to **RGB**.
    note: A component is worked in single precision, or in double precision for a 64-bit floating-point image, and the conversion back is the exact inverse of the conversion there. An image described as linear is converted as linear RGB, any other as sRGB-encoded. The header, the plate solution and the acquisition data are kept with every choice.
  - name: Preview
    kind: check
    default: "On"
    effect: Shows the scaled image beside the controls, with the mask blended in. Off removes the preview, and the histogram returns to the image as it is.
    enabled: It stays enabled while Apply runs.
    tradeoff: Each change scales the image again, or the **Preview area** alone, and counts the histogram again. On a large image, turn it off or use a **Preview area** while you try values, and check the whole image before Apply.
    scope: The window only, on the image or on the frame of the sequence the view shows. It runs the same computation as Apply. A **Preview area** shows the result inside the area over the original, and does not limit Apply.
    persistence: Until the window closes. **Reset** leaves it as it is.
    note: The preview opens in the display mode of the image's view, **Linear** or **Autostretch**. In **Autostretch** it is stretched by the statistics of the image as it is, so a brighter exposure looks brighter. That display stretch can show faint structure and noise that the factor did not make, and it is not written by Apply. An image made with the preview's snapshot button is named `<target> - Exposure Correction - SNAPSHOT` and is not recorded as nonlinear.
  - name: Reset
    kind: button
    default: ""
    effect: Returns **Exposure** to 0 and, on an RGB image, **Channel** to **RGB**.
    range: It leaves the target, **Preview**, the toolbar's choices and the zoom of the histogram as they are.
    enabled: Disabled while Apply runs.
    scope: The window only. The image is not touched, and Apply is disabled afterwards.
  - name: Apply
    kind: button
    default: ""
    effect: Scales the image, or every frame of the sequence, with the value and the channel as they are when you press it, and then returns the controls to their defaults.
    enabled: When **Exposure** is not 0. That includes -0.10, where the factor is 1. It is disabled while the window opens and while a run works. While something else is working on the image, it only shows a warning.
    tradeoff: The run writes the result beside the image, which needs the memory of a second image, in one pass over the processor threads it is given. A mask and a component choice add passes. When the memory limit cannot hold the result, nothing is written and a message says so.
    scope: The whole image or sequence, with the mask and its inversion as they are when you press it, blended afterwards.
    persistence: >-
      The image and its history keep the result, and Undo brings the image back. The history step is
      **Exposure Correction** and reads, for example, `Adjust exposure to 1.5 in channel: RGB`, without the channel
      for a grayscale image. It records the value, not the factor or its stops, and not the toolbar's choices or
      the mask.
    note: Closing the window instead discards the value and a preview on its way. The window has no presets, and nothing of it is saved between sessions.
```

```controls
window: Sequence progress
controls:
  - name: Progress
    kind: readout
    default: ""
    effect: Shows the preparation and the frame being scaled, from 0 to 100 percent.
    enabled: Shown only while Apply works on a sequence. A single image has no progress window.
    tradeoff: Every frame is read and written again in a temporary place, which needs the time of both and the disk space of a second copy until the run ends.
    scope: Every frame, scaled as a single image of it is, with the value, the channel and the mask taken when you pressed Apply. The frames are read with the automatic debayer setting of the view, which turns the frames of a mosaic sequence into RGB.
    persistence: A finished run replaces the sequence, adds one step to its history and returns the controls to their defaults.
  - name: Cancel
    kind: button
    default: ""
    effect: Asks the run to stop. The temporary sequence is removed and the original stays as it was.
    range: The request is noticed before each frame and inside the scaling of one.
    note: The sequence is replaced only when every frame has been scaled. A frame that fails ends the run as Cancel does. After a cancelled run, Apply again and let it finish.
```

## How it works

1. Every sample is read as a fraction of full scale: an unsigned integer divided by its largest value, a signed
   one counted from its smallest value, a floating-point sample as it is.
2. **Exposure** gives one factor. With **RGB**, or on a grayscale image, every stored sample is multiplied by it
   and then held between 0 and 1. With **Red**, **Green** or **Blue**, only that channel is.
3. With a component choice, the image is converted, that component is multiplied and held between 0 and 1, and the
   image is converted back to red, green and blue.
4. The result is written back in the image's own pixel type, an integer rounded to the nearest value with a tie
   going to the even one. A mask on the view then blends it with the original.
5. The image replaces the target and its history gets one step. Its header, its acquisition data and its plate
   solution are kept. A saved file records the new state in its header: `ANLIN` is `nonlinear`, `ANASRC` is
   `process`, `ANOP` is `exposure-correction`, and `ANBASIS` is `monochrome` or `display-rgb`. A linear encoding
   the image stated is no longer stated, and its lineage gains an entry with the value and its factor.

### Model

With \(e\) the value of **Exposure**, the factor \(m\) is

\[
m =
\begin{cases}
1 + e/5, & e > 0 \\
1, & e = 0 \\
1.02 + e/5, & e < 0
\end{cases}
\tag{1}
\]

A sample \(x\) of the chosen channel or component then becomes

\[ y = \min\bigl(1,\ \max(0,\ m\,x)\bigr) \tag{2} \]

Equation (2) turns every sample at or above \(1/m\) into white. Below zero, equation (1) starts at 1.02 and not at
1, so the factor jumps as the value crosses zero.

A change of \(s\) stops multiplies linear samples by \(2^s\). The value that gives that factor follows from
equation (1):

\[ e = 5\,(2^s - 1) \ \text{ for } s > 0, \qquad e = 5\,(2^s - 1.02) \ \text{ for } s < 0 \tag{3} \]

One stop up is the top of the range, 5. One stop down is -2.60, and the bottom of the range is about 5.6 stops
down. Read the other way, a value of 1 is about a quarter of a stop.

Under a mask whose value at a pixel is \(k\), from 0 for black to 1 for white, the pixel becomes
\((1 - k)\,x + k\,y\). The factor and the cut to white come before this blend.

### Boundaries

- A floating-point sample below 0 becomes 0 and one above 1 becomes 1, at every value but 0, also at -0.10. Between
  0 and 1 only an exact zero is black afterwards, because the factor is never below 0.02. An integer image also
  loses the values that round to 0.
- A sample that is not a number stays as it is, and a pixel the image marks as invalid is left unchanged.
- The factor is computed in single precision. Floating-point samples are worked in their own precision, 8-bit and
  16-bit integers in single precision and 32-bit integers in double precision.
- A stopped run writes nothing: the image and its history stay as they were. A run stops on **Cancel** in the
  progress window of a sequence, or when the image closes while Apply works.
- A single image has no progress window and cannot be cancelled once Apply starts.

### Worked example

The picture above shows the Carina Nebula master after Histogram Transformation with **Shadows** and **Midtones**
at 0.02, which leaves its background near 0.28. With **Channel** at **RGB** and **Exposure** at 1.50, equation (1)
gives a factor of 1.3. Equation (2) moves the background to 0.36 and turns every sample at or above 0.77 into
white: the whole picture is brighter, and the brightest clouds and the star cores go flat.

```figure
images:
  - image: ../../assets/images/processes/stretching/exposure-correction-before.webp
    alt: The whole frame before the change, the red and white clouds of the nebula across a field of stars on a dark gray background.
    label: Before
  - image: ../../assets/images/processes/stretching/exposure-correction-after.webp
    alt: The whole frame after the change, the same nebula on a lighter gray background, with its brightest clouds and its star cores a flat white.
    label: After
caption: >-
  The stretched Carina Nebula master before and after Apply with Exposure at 1.50. Both are the whole frame as the
  view shows it in the Linear display mode, without a display stretch.
```

A value of 1.00 gives a factor of 1.2: samples of 0.25, 0.50 and 0.90 become 0.30, 0.60 and 1, the last one cut
from 1.08. A negative value lowers white with everything else. At -1.00 the factor is 0.82: a sample of 0.50
becomes 0.41, the background moves from 0.28 to 0.23, and nothing in the result is brighter than 0.82. At -0.05
the factor is 1.01 and 0.50 becomes 0.505, which is brighter, not darker.

On the linear master itself the median is near 0.028. The largest value, 5, doubles it to 0.055, which is still
black in the **Linear** display mode.

## Starting points

| Situation | Start | Then adjust |
| --- | --- | --- |
| A stretched image a little too dark | **RGB**, and **Exposure** between 0.10 and 0.30 | Raise it in small steps. Stop when **Clipping High** rises or the star cores go flat. |
| An image too bright | **RGB**, and **Exposure** at -1.00 | Lower it further, and check that the whites have not turned gray. |
| A change counted in stops, on linear data | The value from equation (3) | Check each channel's white end, then stretch. The image is recorded as nonlinear from here on. |
| A change through a mask | The value first, with the mask off | Enable a mask with soft edges, then check its edges and the clipping inside it at 100 %. |

## When it goes wrong

!!! danger "Exposure is not a number of stops"
    The value is not an exponent of 2. A value of 1 multiplies by 1.2 and -1 by 0.82, and only 5 doubles the
    samples. Use equation (3) when you need a number of stops, and equation (1) to repeat the change elsewhere.

!!! danger "A value between -0.10 and 0 brightens"
    Below zero the factor starts at 1.02. At -0.10 it is 1, and between -0.10 and 0 the image gets brighter. Type a
    value below -0.10 to darken.

!!! warning "Apply records the image as nonlinear"
    A scaling of linear data that clips nothing is recorded as nonlinear too, and a later process treats it so.
    Use the image's history to go back to the linear image.

Apply is disabled
:   **Cause.** **Exposure** is 0, which changes nothing.
:   **Fix.** Move it to any other value.

Apply at -0.10 changed nothing you can see
:   **Cause.** The factor at -0.10 is 1, yet the run still cuts floating-point samples outside 0 to 1, adds a
    history step and records the image as nonlinear.
:   **Fix.** Undo the step. To leave an image as it is, close the window without Apply.

Highlights turn flat white, or bright stars change color
:   **Cause.** Samples above 1 divided by the factor were cut to white, and the three channels reach white at
    different levels.
:   **Fix.** Undo the step, then lower **Exposure** until **Clipping High** stops rising.

The image looks the same after Apply
:   **Cause.** The view is in **Autostretch**, which measures the scaled image again and shows its background at
    the level it had before.
:   **Fix.** Switch the view to **Linear**, or compare the numbers in Image Statistics.

A floating-point image has samples outside 0 to 1 after a component choice
:   **Cause.** Only the component is held between 0 and 1, and the colors rebuilt from it are not.
:   **Fix.** Use a smaller change, or **RGB** or a single stored channel.

"Error executing the process", with a message that names the memory limit
:   **Cause.** The memory limit of the session cannot hold the result beside the image, so nothing was written.
:   **Fix.** Close other images or work on a smaller one. To raise the limit, change **Memory Limit** on the
    Processing page of the settings and restart AfterNight.

## Related

- [Histogram Transformation](histogram-transformation.md) and [Statistical Stretch](statistical-stretch.md), to
  make a linear master visible, which this window cannot do.
- [Gamma Correction](gamma-correction.md), to brighten or darken the midtones and keep black and white where they
  are.
- [Adjust Brightness/Contrast](../color-adjustment/adjust-brightness-contrast.md), to shift the brightness and
  change the contrast around the middle gray.
- [Image Statistics](../../interface/image-statistics.md), to measure the room above the highlights before you
  brighten.
- [Image History](../../interface/image-history.md), to read the recorded value and to undo the step.
- [Process Windows](../../interface/process-windows.md), for the preview, the histogram toolbar and what every
  process window shares.
