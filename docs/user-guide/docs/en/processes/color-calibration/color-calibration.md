---
kind: process
stage: color
menu: Process > Color Calibration > Color Calibration
summary: >-
  Balances the three channels of an RGB image or sequence with gains measured from a white and a background reference,
  or typed by hand, so that what should be white comes out white.
data_in: linear
data_out: linear
destructive: true
preview: none
sequences: true
masks: post-blend
external: none
glance:
  enabled: >-
    An RGB image or sequence view is active and no other process window is open on it. Color Calibration needs the
    image to itself, so no other window opens on it while Color Calibration is open.
  inputs: >-
    One RGB image or sequence of any pixel type. A background reference and, unless the balance is typed, a white
    reference: each the target or another open RGB image, whole or a rectangle of it. An optional mask on the target's
    view.
  outputs: >-
    The same view, replaced, with one history step, and the window closes. On request, a white and a background mask
    image that show which pixels each reference kept. No file is written.
  state: >-
    A stacked and cropped linear RGB master after gradient removal and before the stretch, with a field of stars or a
    galaxy to take as white.
derived: false
rewritten: true
---
# Color Calibration

Color Calibration balances the three channels of an RGB image, or of every frame of an RGB
[sequence](../../reference/glossary.md#sequence). It measures each channel's level in a patch of sky and in something
that should be white, such as a field of stars, and gives each channel the gain that lifts its white as far above its
sky as the strongest channel's. You can type the three gains instead. Each channel is scaled about its own background,
which stays where it was.

**Apply** replaces the image, keeps its size and [pixel type](../../reference/glossary.md#pixel-type), adds one step
to its [history](../../reference/glossary.md#history) and closes the window. The balance is relative: no catalog,
filter or camera response enters it.

```figure
image: ../../assets/images/processes/color-calibration/color-calibration.webp
alt: >-
  The Color Calibration window, titled ColorCalibration with the image's name. Under the White Reference heading, the
  Reference image box at <--TARGET IMAGE-->, Lower Limit at 0 and Upper Limit at 0.9 with their sliders, a ticked
  Region of interest group with Left 6000, Top 200, Width 1000, Height 1000 and a From Selection button, an unticked
  Manual White Balance group with Red, Green and Blue at 1, and the Output White Reference Mask check box. Under the
  Background Reference heading, the same rows with the limits at 0 and 0.1 and the same rectangle, and the Output
  Background Reference Mask check box. Target Image names the master, and Reset, Close and Apply run along the bottom.
caption: >-
  Color Calibration on the linear Carina master with the star field of its top right corner, 1000 by 1000 pixels at
  6000, 200, as both the white and the background reference.
pins:
  - {n: 2, x: 2.5, y: 27.7}
  - {n: 4, x: 2.6, y: 74.0}
  - {n: 5, x: 2.5, y: 40.0}
  - {n: 7, x: 88.0, y: 93.6}
```

## Before you start

- Remove gradients and crop the stacking borders first: both bias the references. Use a
  [linear image](../../reference/glossary.md#linear-image). The window does not check, and on stretched data the
  means and medians it compares lose their meaning.
- Color Calibration keeps each channel's background level. To bring the three together, run Background
  Neutralization first.
- A [mask](../../reference/glossary.md#mask) on the target's view limits where the result lands, not what the
  references measure. Settle the references with the mask off: a hard-edged mask leaves color steps at its edges.
- On a sequence, every frame gets the same gains, whatever frames its player has selected. A reference that is the
  target is measured on the frame the view shows when you press **Apply**.

## Steps

1. **Open the window.** Make the linear RGB [master](../../reference/glossary.md#master) the
   [active image](../../reference/glossary.md#active-image) and choose **Process > Color Calibration > Color
   Calibration**. Both **Reference image** boxes read `<--TARGET IMAGE-->`: the whole image is both references.
2. **Choose what should be white.** Under **White Reference**, tick **Region of interest** and type **Left**, **Top**,
   **Width** and **Height** of a field of stars without nebula. Or choose **From Selection** and drag it on the image.
3. **Leave out saturated stars.** Keep **Upper Limit** below the level of saturated star cores, which have lost their
   color. Leave **Lower Limit** at 0, so that the stars' faint light counts too.
4. **Choose the sky under it.** Under **Background Reference**, tick **Region of interest** and type the same
   rectangle, or a patch of star-poor sky of the same brightness beside it. The white is measured above this sky.
5. **Or type the balance.** Tick **Manual White Balance** and lower **Red**, **Green** or **Blue** for a channel that
   is too strong. The background reference is still measured.
6. **Ask for the masks.** Tick **Output White Reference Mask** and **Output Background Reference Mask** to see which
   pixels each reference kept.
7. **Apply.** Choose **Apply**. The window closes, the view shows the balanced image, its history lists
   **ColorCalibration**, and the masks open beside it.
8. **Check the balance.** Open the view's [Autostretch](../../reference/glossary.md#autostretch) Settings and turn
   on **Linked channels**, without which each channel is shown on its own and most of a calibration hides. The stars
   should now look white.

## Controls

A rectangle is counted in its reference's own pixels from the top-left one, at 0, 0. What every process window
shares is on [Process Windows](../../interface/process-windows.md).

```controls
window: White Reference
controls:
  - name: Reference image
    kind: combo
    default: "<--TARGET IMAGE-->"
    options: ["<--TARGET IMAGE-->"]
    effect: Chooses the image whose channels' means between the limits are taken as white. `<--TARGET IMAGE-->` is the image the window calibrates.
    range: >-
      It lists `<--TARGET IMAGE-->` and every other open RGB image or sequence by its view's name, and follows them as
      they open, close and are renamed. A reference may differ from the target in size and pixel type.
    enabled: Disabled while **Manual White Balance** is on.
    tradeoff: A reference whose channels barely stand above its sky gives large, unstable gains.
    scope: Apply only. On a sequence, `<--TARGET IMAGE-->` is the frame the view shows when you press **Apply**.
    persistence: >-
      Until the window closes. **Reset** and another target return it to `<--TARGET IMAGE-->`, and so does closing the
      view it names. Choosing another image while **Region of interest** is on fills the four boxes with that image's
      size.
    note: >-
      A view that holds no image when you press **Apply** is refused with "The selected white reference image is no
      longer available.", and one that no longer holds an RGB image with "the white reference is not a three-channel
      colour image".
  - name: Lower Limit
    kind: spinbox
    default: "0.000000"
    widget: {min: 0, max: 1, step: 0.05, decimals: 6}
    effect: Leaves out of each channel's mean the samples below it. Higher keeps only the brighter samples.
    range: >-
      A sample equal to a limit is kept. Each channel's samples are chosen on their own, so a pixel can count in one
      channel and not in another. It is not held below **Upper Limit**.
    enabled: Disabled while **Manual White Balance** is on.
    tradeoff: >-
      Raised, it keeps another set of pixels in each channel, so the three means no longer measure the same light.
      Above every sample of a channel, or above **Upper Limit**, the run is refused.
    scope: The white mean and the white mask.
    persistence: Until the window closes. **Reset** and another target return it to 0.
  - name: Upper Limit
    kind: spinbox
    default: "0.900000"
    widget: {min: 0, max: 1, step: 0.05, decimals: 6}
    effect: Leaves out of each channel's mean the samples above it. Lower leaves out bright and saturated star cores.
    range: A sample equal to a limit is kept. At 1, samples at full scale count. It is not held above **Lower Limit**.
    enabled: Disabled while **Manual White Balance** is on.
    tradeoff: Too low leaves too little of what is white. Saturated cores, equal in every channel, pull the gains toward 1.
    scope: The white mean and the white mask.
    persistence: Until the window closes. **Reset** and another target return it to 0.9.
  - name: Region of interest
    kind: check
    default: "Off"
    effect: On, the white is measured over the rectangle of **Left**, **Top**, **Width** and **Height** only. Off, over the whole reference.
    range: Turning it on fills the four boxes with the whole reference. Turning it off returns them to 0 and ends a drawing with **From Selection**.
    enabled: Disabled while **Manual White Balance** is on.
    tradeoff: A small clean rectangle keeps nebula and gradients out, and measures fewer samples.
    scope: The white mean and the white mask.
    persistence: Until the window closes. **Reset** and another target turn it off.
    note: >-
      A rectangle that holds no pixel, or reaches past the reference's edge, is refused when you press **Apply** with
      "The white reference ROI must be fully inside the reference image and have a non-zero size."
  - name: Left
    kind: spinbox
    default: "0"
    widget: {min: 0, max: 99, step: 1}
    effect: Places the rectangle's left edge, in pixels from the reference's left edge.
    range: >-
      Up to the reference's width less 1 once **Region of interest** has been turned on. Until then the box stops at
      99. **Width** cannot reach past the right edge from it.
    enabled: With **Region of interest** on and **Manual White Balance** off.
    scope: The white rectangle. While **From Selection** is on, the frame on the view follows it.
    persistence: Until the window closes.
  - name: Top
    kind: spinbox
    default: "0"
    widget: {min: 0, max: 99, step: 1}
    effect: Places the rectangle's top edge, in pixels from the reference's top edge.
    range: >-
      Up to the reference's height less 1 once **Region of interest** has been turned on. Until then the box stops at
      99. **Height** cannot reach past the bottom edge from it.
    enabled: With **Region of interest** on and **Manual White Balance** off.
    scope: The white rectangle. While **From Selection** is on, the frame on the view follows it.
    persistence: Until the window closes.
  - name: From Selection
    kind: button
    default: "Off"
    effect: >-
      Lets you draw the rectangle on the reference's view. Choose it, then drag on the image. The four boxes read 0
      until you press the mouse button, and then follow the frame.
    range: Choosing it again ends the drawing and keeps the rectangle, or returns to the whole reference when **Width** or **Height** is 0.
    enabled: >-
      With **Region of interest** on and **Manual White Balance** off. Choosing the other **From Selection**, turning
      **Manual White Balance** on, turning **Region of interest** off or choosing another **Reference image** ends it.
    tradeoff: It places the rectangle and shows nothing of the result.
    scope: The frame on the reference's view and the four boxes. On a sequence you draw on the frame the view shows.
    persistence: Until you choose it again or the window closes. **Reset** ends it.
  - name: Width
    kind: spinbox
    default: "0"
    widget: {min: 0, max: 99, step: 1}
    effect: Sets the rectangle's width, from the left edge **Left** places.
    range: Up to the reference's width less **Left**. **Apply** refuses 0.
    enabled: With **Region of interest** on and **Manual White Balance** off.
    tradeoff: Fewer samples make the mean noisier.
    scope: The white rectangle.
    persistence: Until the window closes.
  - name: Height
    kind: spinbox
    default: "0"
    widget: {min: 0, max: 99, step: 1}
    effect: Sets the rectangle's height, from the top edge **Top** places.
    range: Up to the reference's height less **Top**. **Apply** refuses 0.
    enabled: With **Region of interest** on and **Manual White Balance** off.
    tradeoff: Fewer samples make the mean noisier.
    scope: The white rectangle.
    persistence: Until the window closes.
  - name: Manual White Balance
    kind: check
    default: "Off"
    effect: On, **Red**, **Green** and **Blue** are the gains, and no white is measured.
    enabled: >-
      On, it disables the **Reference image**, the limits, **Region of interest**, **From Selection** and **Output
      White Reference Mask** of this group, and ends a white drawing.
    tradeoff: Simple and repeatable. No value exceeds 1, so it can lower a channel toward its background but never lift one.
    scope: Apply, every pixel. The background reference is still measured, because its medians are the levels the gains scale about.
    persistence: Until the window closes. **Reset** and another target turn it off.
  - name: Red
    kind: spinbox
    default: "1.000000"
    widget: {min: 1e-06, max: 1, step: 0.05, decimals: 6}
    effect: Sets the red channel's gain about its background. Lower presses red toward its background level, and 1 leaves it as it is.
    range: The slider's left end is 0.000001, because a channel cannot be multiplied by zero.
    enabled: With **Manual White Balance** on.
    tradeoff: A very small value leaves red almost at its background level everywhere.
    scope: The red channel, through the mask.
    persistence: Until the window closes. **Reset** and another target return it to 1.
  - name: Green
    kind: spinbox
    default: "1.000000"
    widget: {min: 1e-06, max: 1, step: 0.05, decimals: 6}
    effect: Sets the green channel's gain about its background, as **Red** does for red.
    enabled: With **Manual White Balance** on.
    scope: The green channel, through the mask.
    persistence: Until the window closes. **Reset** and another target return it to 1.
  - name: Blue
    kind: spinbox
    default: "1.000000"
    widget: {min: 1e-06, max: 1, step: 0.05, decimals: 6}
    effect: Sets the blue channel's gain about its background, as **Red** does for red.
    enabled: With **Manual White Balance** on.
    scope: The blue channel, through the mask.
    persistence: Until the window closes. **Reset** and another target return it to 1.
  - name: Output White Reference Mask
    kind: check
    default: "Off"
    effect: >-
      After **Apply**, opens `<target> - WHITE MASK`: a grayscale image of the whole white reference, white where every
      channel of a pixel inside the rectangle is valid and between the limits, black everywhere else.
    range: >-
      White is the top of the reference's pixel type, 1 or its largest code. Black is its bottom, 0 or its smallest
      code, which is the most negative one of a signed integer type.
    enabled: Disabled, and ignored, while **Manual White Balance** is on.
    tradeoff: Adds one image of the reference's size and pixel type.
    scope: An output only. It never limits the calibration.
    persistence: >-
      It opens once the result is shown, while the target is open, even when the window has closed. Its history begins
      with `ColorCalibration: White reference mask image created from '<target>'`. It keeps the reference's
      acquisition data and plate solution, and no header cards.
```

```controls
window: Background Reference
controls:
  - name: Reference image
    kind: combo
    default: "<--TARGET IMAGE-->"
    options: ["<--TARGET IMAGE-->"]
    effect: Chooses the image whose channels' medians are the background levels the gains scale about, with **Manual White Balance** on or off.
    range: The same list as the white **Reference image**.
    tradeoff: >-
      Stars, nebula or a gradient in it move the levels. Its tooltip speaks of background neutralization, but Color
      Calibration keeps each channel's background where it is.
    scope: Apply only. On a sequence, `<--TARGET IMAGE-->` is the frame the view shows when you press **Apply**.
    persistence: As the white **Reference image**.
    note: >-
      A view that holds no image when you press **Apply** is refused with "The selected background reference image is
      no longer available." A channel without a finite, valid sample stops the run with "No green sample of the
      background reference means something; choose another reference", naming the channel.
  - name: Lower Limit
    kind: spinbox
    default: "0.000000"
    widget: {min: 0, max: 1, step: 0.05, decimals: 6}
    effect: Leaves the darker pixels out of the background mask.
    range: A sample equal to a limit is selected. It is not held below **Upper Limit**.
    tradeoff: It changes nothing of the result, because the levels are the medians of every sample of the rectangle.
    scope: The background mask only.
    persistence: Until the window closes. **Reset** and another target return it to 0.
  - name: Upper Limit
    kind: spinbox
    default: "0.100000"
    widget: {min: 0, max: 1, step: 0.05, decimals: 6}
    effect: Leaves the brighter pixels out of the background mask.
    range: A sample equal to a limit is selected. It is not held above **Lower Limit**.
    tradeoff: It changes nothing of the result, as **Lower Limit**.
    scope: The background mask only.
    persistence: Until the window closes. **Reset** and another target return it to 0.1.
  - name: Region of interest
    kind: check
    default: "Off"
    effect: On, the medians are taken, and the mask made, over the rectangle of **Left**, **Top**, **Width** and **Height** only. Off, over the whole reference.
    range: As the white **Region of interest**, with **Manual White Balance** on or off.
    tradeoff: A clean rectangle of sky keeps stars and nebula out of the levels.
    scope: The background levels and the background mask.
    persistence: Until the window closes. **Reset** and another target turn it off.
    note: >-
      A rectangle that holds no pixel, or reaches past the reference's edge, is refused when you press **Apply** with
      "The background reference ROI must be fully inside the reference image and have a non-zero size."
  - name: Left
    kind: spinbox
    default: "0"
    widget: {min: 0, max: 99, step: 1}
    effect: Places the rectangle's left edge, as under **White Reference**.
    range: Up to the reference's width less 1 once **Region of interest** has been turned on, and 99 until then.
    enabled: With **Region of interest** on.
    scope: The background rectangle.
  - name: Top
    kind: spinbox
    default: "0"
    widget: {min: 0, max: 99, step: 1}
    effect: Places the rectangle's top edge, as under **White Reference**.
    range: Up to the reference's height less 1 once **Region of interest** has been turned on, and 99 until then.
    enabled: With **Region of interest** on.
    scope: The background rectangle.
  - name: From Selection
    kind: button
    default: "Off"
    effect: Lets you draw the background rectangle on its reference's view, as the white **From Selection** does.
    enabled: >-
      With **Region of interest** on. Choosing the white **From Selection**, turning **Region of interest** off or
      choosing another **Reference image** ends it.
    persistence: Until you choose it again or the window closes. **Reset** ends it.
  - name: Width
    kind: spinbox
    default: "0"
    widget: {min: 0, max: 99, step: 1}
    effect: Sets the rectangle's width, from the left edge **Left** places.
    range: Up to the reference's width less **Left**. **Apply** refuses 0.
    enabled: With **Region of interest** on.
    tradeoff: A narrow rectangle may not hold the sky's level.
  - name: Height
    kind: spinbox
    default: "0"
    widget: {min: 0, max: 99, step: 1}
    effect: Sets the rectangle's height, from the top edge **Top** places.
    range: Up to the reference's height less **Top**. **Apply** refuses 0.
    enabled: With **Region of interest** on.
    tradeoff: A short rectangle may not hold the sky's level.
  - name: Output Background Reference Mask
    kind: check
    default: "Off"
    effect: >-
      After **Apply**, opens `<target> - BACKGROUND MASK`, made as the white mask is from the background rectangle and
      the background limits.
    enabled: With **Manual White Balance** on or off.
    tradeoff: Adds one image of the reference's size and pixel type.
    scope: An output only. It never limits the calibration.
    persistence: >-
      It opens as the white mask does, and its history begins with `ColorCalibration: Background reference mask image
      created from '<target>'`.
```

```controls
window: ColorCalibration
controls:
  - name: Target Image
    kind: combo
    default: The active image
    options: [The active image, Every other open RGB image or sequence]
    effect: Chooses the RGB image or sequence **Apply** calibrates.
    tradeoff: The image chosen here is the one replaced.
    persistence: Until the window closes. Choosing another target returns every other control of the window to its default, as **Reset** does.
  - name: Close
    kind: button
    default: ""
    effect: Closes the window without applying, and the frame of **From Selection** leaves the view.
    enabled: Disabled while a run works.
    note: >-
      Closing the window another way while a run works hides it, and it closes when the run has ended. The run is not
      stopped, on a sequence either: the progress window's **Cancel** stops it. The masks asked for still open.
  - name: Reset
    kind: button
    default: ""
    effect: >-
      Returns both **Reference image** boxes to `<--TARGET IMAGE-->`, the four limits, **Manual White Balance** and its
      three values, both **Region of interest** groups and their boxes, and both mask choices to their defaults, and
      ends a drawing.
    range: It leaves **Target Image** as it is, and lists the open RGB images again in both **Reference image** boxes.
    enabled: Disabled while a run works.
  - name: Apply
    kind: button
    default: ""
    effect: >-
      Reads the window, the references and the mask on the target's view, calibrates the image or every frame of the
      sequence, and closes the window once the view shows the result.
    enabled: >-
      Whenever no run works, from the moment the window opens. While a run works, **Apply**, **Close** and **Reset**
      are disabled and the other controls stay enabled, but a change made then does not reach the run.
    tradeoff: >-
      The run makes a floating-point copy of each reference, with a bit for each of its samples, and of the target, and
      the history keeps the image beside the result. A mask adds a copy for the blend, and each reference mask an
      image. A sequence costs one frame's copies at a time and the disk space of the new sequence.
    scope: >-
      The whole target, every channel, through the mask on its view. On a sequence, every frame, whatever frames its
      player has selected, read debayered when the view debayers them, with the references measured once. The frames
      are written into a new temporary sequence of the same container, which replaces the view's sequence, with one
      history step, only when every frame is done.
    persistence: >-
      The image and its history keep the result, as the step **ColorCalibration** with the text `White reference image
      '<name>' (ROI X:<x>, Y:<y>, W:<w>, H:<h>) (MIN:<lower>, MAX:<upper>) - Background reference image '<name>' (ROI
      X:<x>, Y:<y>, W:<w>, H:<h>) (MIN:<lower>, MAX:<upper>)`, which begins `Using manual white balance values R:<r>,
      G:<g>, B:<b>` in manual mode. The limits and the manual values have six decimals. A reference that is the target
      is named by the target's view name, and a rectangle that is off reads as the whole reference.
    note: >-
      Before anything runs, a reference view that holds no image, or a rectangle that does not fit its reference, is
      refused in a message titled **Color Calibration**, and nothing starts. A single image has no progress window, and
      its run cannot be stopped. A sequence shows a progress window titled
      **Color Calibration** with `Executing color calibration to sequence <name>`, whose **Cancel** stops the run, before
      the next frame at the latest. A run that fails says why under "Error running color calibration", after "An error occurred when
      executing the color calibration process:", writes it to the log, and leaves the image, its history and the window
      as they were. While another run holds a sequence, **Apply** shows the busy warning under the title **Color
      Calibration** and starts nothing.
```

```controls
window: The frame on the reference view
controls:
  - name: Selection frame
    kind: readout
    default: ""
    effect: Shows the rectangle as a thin dashed frame on the reference's view while a **From Selection** is on, and follows the four boxes.
    scope: The view only. It changes no sample.
  - name: Drag on the image
    kind: action
    default: ""
    effect: Right after you choose **From Selection**, draws the rectangle from where you press the button to where you release it.
  - name: Drag inside the frame
    kind: action
    default: ""
    effect: Moves the frame without changing its size. **Left** and **Top** follow.
  - name: Drag an edge or a corner
    kind: action
    default: ""
    effect: Moves that edge, or the two edges of that corner. The pointer changes shape over an edge.
  - name: Click outside the frame
    kind: action
    default: ""
    effect: Centers the frame on the point you click, as far as the image allows.
    note: While a **From Selection** is on, holding the left button does not open the view's lens.
```

## How it works

1. **Apply** reads the window, both references as their views show them and the mask on the target's view.
2. Every sample is read as a [fraction of full scale](../../reference/glossary.md#fraction-of-full-scale), in a 32-bit
   floating-point copy, or a 64-bit one for a 64-bit floating-point or 32-bit integer image.
3. Each channel's background level is the median of every valid, finite sample of the background rectangle. Its white
   level is the mean of the valid, finite samples of the white rectangle between the white limits.
4. Every valid sample of the target is scaled about its channel's background by the channel's gain, held between 0
   and 1, and written back in the image's pixel type, rounded to the nearest value. The mask on the view then blends
   the result with the image, as on every process window.
5. The result replaces the target with one history step, and the masks asked for open beside it. The header, the
   acquisition data, the [plate solution](../../reference/glossary.md#plate-solution), which samples are valid and
   what the image states it is are kept. The [lineage](../../reference/glossary.md#lineage) gains an entry with the detail
   `color calibration: gains <r>, <g>, <b>; offsets <r>, <g>, <b>`, and the image is not marked as color calibrated.

### Model

For each channel \(c\), let \(B_c\) be the median of the background rectangle, and \(W_c\) the mean of the samples of
the white rectangle that lie between the white **Lower Limit** and **Upper Limit**. The channel's gap is how far its
white stands above its background:

\[ D_c = \lvert W_c - B_c \rvert \tag{1} \]

With \(r\) the channel of the largest gap, the gain \(K_c\) of channel \(c\) is

\[ K_c = \frac{D_r}{D_c} \tag{2} \]

so the strongest channel keeps a gain of 1 and the weaker ones are lifted to its gap. With **Manual White Balance**
on, \(K_c\) is the value of **Red**, **Green** or **Blue**. In both modes a sample \(I_c\) becomes

\[ P_c = \min\Bigl(1,\ \max\bigl(0,\ (I_c - B_c)\,K_c + B_c\bigr)\Bigr) \tag{3} \]

A sample at \(B_c\) stays where it was, so each channel keeps its background level, and the three levels are not
brought together. The lineage records the offset \(B_c\,(1 - K_c)\) that equation (3) adds to \(I_c\,K_c\). With
**Red** at 0.9 and a red background of 0.125, for example, the red offset is 0.0125.

### Boundaries

- Samples the image's [validity rule](../../reference/glossary.md#validity-rule) rejects are not measured and are
  copied unchanged. Samples that are not finite are never measured.
- The result is held between 0 and 1 in every channel, the strongest one too, so a floating-point image loses its
  negative samples and those above 1.
- When the history cannot keep the image the calibration replaces, it says so once in a message titled "Image
  History". The image stays as it was and no mask opens.
- When the target changes while a single image's run works, the result is dropped without a message.
- A single image's run cannot be stopped. A sequence's run stopped with **Cancel** changes nothing, records nothing
  and opens no mask. In these cases the window stays open.

### Worked example

The linear Carina master is an RGB image of 7246 by 5429 pixels whose three channels have a median of 0.02766. Its top
right corner is a field of stars without nebula. Under **White Reference** and under **Background Reference**, tick
**Region of interest** and type **Left** 6000, **Top** 200, **Width** 1000 and **Height** 1000, and leave the limits as
they open. In that rectangle the channels' medians are 0.02629, 0.02826 and 0.02791, and their means between 0 and 0.9
are 0.02792, 0.03024 and 0.03102. Equation (1) gives gaps of 0.00163, 0.00198 and 0.00312: the stars' light is
strongest in blue. Equation (2) gives gains of 1.909, 1.575 and 1.

On average, the stars of the rectangle now stand as high above their sky in red and green as in blue. By equation
(3), the whole frame's median moves to 0.02890, 0.02731 and 0.02766, and a sample of 0.25 becomes 0.453 in red and
0.377 in green. Red samples above 0.536 now reach 1, about 17,000 of them against 2,600 before.

```figure
images:
  - image: ../../assets/images/processes/color-calibration/color-calibration-before.webp
    alt: The whole frame before the calibration, a pink nebula and blue-white stars on a gray background.
    label: Before
  - image: ../../assets/images/processes/color-calibration/color-calibration-after.webp
    alt: The whole frame after the calibration, a deeper red nebula and whiter stars on a slightly warmer background.
    label: After
caption: >-
  The Carina master before and after Apply with the star field of its top right corner as both references. Both are
  the whole frame after the same Histogram Transformation, with Shadows and Midtones at 0.02, in the view's Linear
  display.
```

With the defaults, the whole frame is both references, and the nebula's light counts as white. The gaps become
0.00248, 0.00217 and 0.00348, and the gains 1.400, 1.603 and 1: red is lifted far less than the stars ask for.

## Starting points

| Situation | Start | Then adjust |
| --- | --- | --- |
| A linear RGB master with a field of stars | Both **Region of interest** groups on the same star field without nebula, the limits as they open | Check the masks, and move the rectangle when a gain looks extreme. |
| A galaxy in the frame | The white rectangle around the galaxy, the background rectangle on sky beside it | Lower **Upper Limit** when the galaxy's core is saturated. |
| One channel is plainly too strong | **Manual White Balance**, that channel lowered, the others at 1 | Lower it in small steps, and compare the channels' statistics. |
| The channels' backgrounds differ | Background Neutralization first | Then Color Calibration once. Repeated passes of offsets and gains clip shadows or highlights. |

## When it goes wrong

!!! warning "The defaults count the whole image as white"
    Both references start as the whole target, so each gap is only a channel's mean less its median, and a nebula
    counts as white. Choose a field of stars and its sky before you apply.

!!! warning "There is no preview"
    **Apply** replaces the image and the window closes. Try the references on a duplicate or on one frame of a long
    sequence, and use the image's history to go back.

"The selected white reference image is no longer available."
:   **Cause.** The view the **Reference image** box names held no image when you pressed **Apply**. The background
    box has a message of its own.
:   **Fix.** Choose an open RGB image, or `<--TARGET IMAGE-->`, and apply again.

"The white reference ROI must be fully inside the reference image and have a non-zero size."
:   **Cause.** A **Region of interest** that is on holds no pixel, or reaches past its reference's edge. The
    background rectangle has the same message.
:   **Fix.** Correct **Left**, **Top**, **Width** and **Height**, draw the rectangle with **From Selection**, or turn
    **Region of interest** off.

"Error running color calibration" with "No red sample of the white reference lies between the limits"
:   **Cause.** No sample of that channel in the white rectangle lies between the limits, or **Lower Limit** is above
    **Upper Limit**.
:   **Fix.** Widen the limits, or choose another rectangle or **Reference image**.

"Error running color calibration" with "sits on its background level, so no gain balances it"
:   **Cause.** A channel's white mean equals its background median, as on a flat reference.
:   **Fix.** Choose a white that stands above its sky, or other limits.

The result has a strong cast, or one channel is lifted many times
:   **Cause.** The white barely stands above its background, holds a colored object, or sits on other sky.
:   **Fix.** Choose a field of stars and its own sky, and check the masks.

Nothing seems to change after **Apply**
:   **Cause.** The view's Autostretch treats each channel on its own, which undoes most of a calibration.
:   **Fix.** Turn on **Linked channels** in the view's Autostretch Settings, or compare the channels' statistics.

Changing the background limits does not change the result
:   **Cause.** The levels are the medians of every sample of the rectangle, and the limits shape only the mask.
:   **Fix.** Move or resize the background rectangle, or use Background Neutralization to align a range of levels.

A reference mask is empty or sparse, or the white mask does not open
:   **Cause.** A pixel is white only when all three channels lie between the limits. **Manual White Balance** makes
    no white mask.
:   **Fix.** Widen the limits and check the rectangle, or turn **Manual White Balance** off.

## Related

- [Background Extraction](../background-extraction/background-extraction.md) and
  [Auto Gradient Removal](../background-extraction/auto-gradient-removal.md), to remove gradients first.
- [Background Neutralization](background-neutralization.md), to bring the backgrounds to one level.
- [Photometric Color Calibration](photometric-cc.md) and
  [Spectrophotometric Color Calibration](spectrophotometric-cc.md), to calibrate against a star catalog.
- [Histogram Transformation](../stretching/histogram-transformation.md), once the linear color work is done.
- [Image Viewer](../../interface/image-viewer.md), for **Linked channels**.
- [Process Windows](../../interface/process-windows.md), for the mask, the progress window and the rest.
