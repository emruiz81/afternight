---
kind: process
stage: stretch
menu: Process > Intensity Transformations > Generalized Hyperbolic Transformation
summary: >-
  Stretches a linear image with a generalized hyperbolic curve placed around a symmetry point, or with the histogram,
  arcsinh, linear, inversion and statistics-derived curves the same window offers.
data_in: linear
data_out: nonlinear
destructive: true
preview: real-time
sequences: true
masks: post-blend
external: none
glance:
  enabled: >-
    An image or a sequence view is active. The window opens beside other process windows, but a second one for the
    same image is refused. Apply waits for nothing else to be working on the target.
  inputs: One grayscale or RGB image or sequence, and an optional mask on its view.
  outputs: >-
    The same view, replaced, with one history step. Its size and pixel type are kept. No file, lookup table, preset
    or report is written.
  state: >-
    A calibrated linear master, mono or RGB, after gradient removal, color calibration and every measurement,
    deconvolution or noise reduction that needs linear data.
derived: false
controls_ignore:
  - {label: Human weighted luminance, reason: The window never shows it and no result reads it.}
---
# Generalized Hyperbolic Transformation

Generalized Hyperbolic Transformation [stretches](../../reference/glossary.md#stretch) a
[linear image](../../reference/glossary.md#linear-image) with a curve you place. A stretch factor sets its strength and
a local intensity its shape. A [symmetry point (SP)](../../reference/glossary.md#symmetry-point) names the tones that
get the most contrast, and two [protection points (LP/HP)](../../reference/glossary.md#protection-point) limit how far
the curve bends on either side. The same window offers a histogram, an arcsinh and a linear curve, the negative of the
image, and a curve derived from the image's statistics.

**Apply** replaces the image or [sequence](../../reference/glossary.md#sequence) with the stretched one, keeps its size
and [pixel type](../../reference/glossary.md#pixel-type), and adds one step to its
[history](../../reference/glossary.md#history). The result is recorded as nonlinear, whichever curve you chose.

```figure
image: ../../assets/images/processes/stretching/gh-transformation.webp
alt: >-
  The Generalized Hyperbolic Transformation window. On the left, two histograms, each under a white curve. In the
  upper one the peak stands about a quarter of the way along. In the lower one it is a spike at the left edge, under
  a curve that rises steeply and then bends over, with three handles below it. Under them a toolbar, the Channel and
  Transformation type boxes with the Invert box, five values with sliders, from Stretch factor to Protect highlights,
  three SP picker buttons, Target Image and Preview. On the right, the preview shows a pale nebula on a gray background. Reset, Close and
  Apply run along the bottom.
caption: >-
  Generalized Hyperbolic Transformation on a linear master, with the symmetry point at 0.025, a stretch factor of 2.5
  and a local intensity of 5. The lower histogram is the image, the upper one the result, and the preview shows what
  Apply writes.
pins:
  - {n: 2, x: 3.0, y: 61.1}
  - {n: 3, x: 3.0, y: 70.2}
  - {n: 4, x: 3.0, y: 64.1}
  - {n: 5, x: 3.0, y: 67.2}
  - {n: 6, x: 3.0, y: 73.2}
  - {n: 7, x: 41.0, y: 29.0}
  - {n: 8, x: 92.6, y: 97.6}
```

## Before you start

- Finish the work that needs linear data first: gradient removal, color calibration, measurement,
  [deconvolution](../../reference/glossary.md#deconvolution) and linear noise reduction. After **Apply** the image is
  nonlinear.
- **Apply** replaces the image, and its history keeps the step before. Stretch in few passes: every pass adds
  rounding, and clipping at both ends.
- The image is grayscale or RGB. A raw color-filter mosaic is stretched as a grayscale image:
  [debayer](../../reference/glossary.md#debayer) it first.
- Samples below 0 or above 1 are cut to that range before the curve.
- A [mask](../../reference/glossary.md#mask) on the view blends the stretched image with the original after the stretch.
  The curve, the histograms and the clipping figures leave it out.
- On a sequence, your values give every frame the same curve. Only **STF Transformation** gives each frame the curve
  of its own statistics.

## Steps

1. **Open the window.** Make the linear [master](../../reference/glossary.md#master) the
   [active image](../../reference/glossary.md#active-image) and choose **Process > Intensity Transformations >
   Generalized Hyperbolic Transformation**. The lower histogram shows the image as a narrow peak at the left, and the
   preview is nearly black.
2. **Keep the channel and the type.** Leave **Channel** at **RGB** and **Transformation type** at **Generalized
   Hyperbolic**, the curve made for a linear master. Both white curves stay straight lines until you raise the
   stretch factor.
3. **Place the symmetry point.** Set **Symmetry point (SP)** at the peak of the lower histogram, or just below it.
   Type it, drag the middle handle, or press an **SP picker** button, click the background in the preview and press
   the button again. The middle handle now sits under the peak.
4. **Raise the stretch.** Raise **Stretch factor(In(D+1))** until the faint signal shows in the preview. The peak of
   the upper histogram moves to the right.
5. **Shape the curve.** Raise **Local intensity (b)** to lift the tones next to the symmetry point more and to keep
   the bright ones away from white. Then set the stretch factor again.
6. **Protect the ends.** Leave the two protection points alone at first. Lower **Protect highlights (HP)** when star
   cores flatten, and raise **Protect shadows (LP)** when the darkest tones lose their contrast.
7. **Check the preview at 100 %.** Zoom the preview into the faint areas and the brightest stars, and press
   ++space++ over it to compare with the original.
8. **Apply.** Choose **Apply**. The image in its view is the stretched one, its history lists **Generalized
   Hyperbolic Transformation** with the values you set, and the controls return to their defaults.

## Controls

The toolbar under the histograms - zoom, grid, curve, scales, areas and **Plot resolution** - changes only how the
histograms are drawn, and is the same in every window that has one: see
[Process Windows](../../interface/process-windows.md). Its **Show clipping info** button is described with the
figures it shows. Controls that **Channel** or **Transformation type** hides are listed where the window puts them.

```controls
window: The histograms
controls:
  - name: Upper histogram
    kind: readout
    default: ""
    effect: Shows the result, which is the image's counts sent through the curve, with the curve drawn over it.
    range: One line for a grayscale image and three for RGB. With **Red**, **Green** or **Blue** in **Channel** it draws that channel alone, and with **Lightness**, **Saturation** or **Colour** the component being stretched.
    enabled: Always shown, and its curve cannot be hidden. Choosing one channel to display in the preview's own toolbar changes the lines of both histograms only until the next change of a value.
    tradeoff: For **RGB** and the single channels it is a prediction, each count moved to the bin nearest to where its value goes. For **Lightness**, **Saturation** and **Colour** the whole image is stretched and counted again, which takes longer. It leaves the mask out.
    scope: The window only. It is redrawn after every change of a value, of the type, of **Channel**, of the target and of **Plot resolution**.
    note: With **Pick from histogram point** on, a click here sets the symmetry point.
  - name: Lower histogram
    kind: readout
    default: ""
    effect: Shows the image as it is, with the three handles under it and a guide line that follows the pointer.
    enabled: Always shown. The toolbar's **Transfer curve** button draws or hides the white curve over this histogram only.
    tradeoff: The narrow peak of a linear image may need the toolbar's zoom or a finer **Plot resolution** to be seen. Neither changes the image.
    scope: The window only, without the mask. While you set up a sequence, both histograms and the preview show one frame of it.
    note: A click away from a handle shows the coordinates under the pointer. Dragging with the middle button pans, the wheel with ++ctrl++ held zooms and a right click restores the zoom, in both histograms. The curve is white in the light theme too.
  - name: Clipping Low and Clipping High
    kind: readout
    default: ""
    effect: Gives, in the corner of each histogram, the share of its samples at the black end and at the white end.
    range: On the lower histogram, and on an upper histogram counted again from a stretched image, these are the samples at or below 0 and at or above 1. On a predicted upper histogram they are the counts that land in its first and its last bin, a forecast that changes with **Plot resolution**.
    enabled: With the toolbar's **Show clipping info** button on, as it is when the window opens.
    tradeoff: Each share is of all the channels the histogram holds, the stretched component included, whichever lines are drawn. Take it as orientation, and use Image Statistics after Apply for exact counts.
    scope: The window only, without the mask.
    persistence: The button keeps its state through **Reset**, until the window closes.
  - name: LP, SP and HP handles
    kind: slider
    default: At 0, 0 and 1
    widget: {min: 0, max: 1}
    effect: Dragging a handle left or right moves its value, and the preview follows as you drag.
    range: The outer handles stop at the middle one. The middle handle pushes an outer handle along when it reaches it.
    enabled: With **Linear Stretch** the outer handles are **Black point (BP)** and **White point (WP)**, and the middle handle, placed halfway between them, changes nothing. With **Image Inversion** and **STF Transformation** the handles move values those types do not use.
    tradeoff: Where handles lie on top of each other, as LP and SP do when the window opens, the middle one is picked up first. Type an exact value in its box instead. With **Linear Stretch** an outer handle cannot pass the middle one, so type that point in its box.
    scope: The same values as the boxes of the same names, on what **Channel** selects.
    persistence: Until **Reset**, **Apply** or the window closes.
    note: Dragging the middle handle turns off an SP picker.
```

```controls
window: Generalized Hyperbolic Transformation
controls:
  - name: Channel
    kind: combo
    default: RGB
    options: [RGB, Red, Green, Blue, Lightness, Saturation, Colour]
    effect: Chooses what the curve stretches. **RGB** gives the three channels the same curve, each on its own. **Red**, **Green** or **Blue** stretches that channel and keeps the other two exactly. **Lightness** stretches the CIE Lab lightness and keeps the two color coordinates. **Saturation** stretches the HSV saturation and then restores the lightness the pixel had. **Colour** stretches a weighted luminance and multiplies the three channels by one factor.
    enabled: A grayscale image shows a disabled **Grayscale** instead.
    tradeoff: One channel alone shifts the color balance. **Lightness**, **Saturation** and **Colour** convert every pixel, which takes longer and more memory, and colors at the edge of the range can round or clip.
    scope: The preview, Apply and the lines both histograms draw. The mask is blended afterwards.
    persistence: Until the window closes. **Reset** and Apply return it to **RGB**.
    note: With **Linear Stretch** the choices **Lightness**, **Saturation** and **Colour** stretch the three channels, each on its own, as **RGB** does. With **STF Transformation** and **Linked STF** off, they stretch their component with the curve derived from the red channel.
  - name: Colour clip
    kind: combo
    default: Clip
    options: [Clip, Rescale]
    effect: Decides what happens where the common factor would send a channel above 1. **Clip** cuts each channel at 1 on its own. **Rescale** lowers the factor of that pixel until its brightest channel ends at 1.
    enabled: Shown only with **Channel** at **Colour**.
    tradeoff: With **Clip** the ratios between the colors change in bright pixels. **Rescale** keeps them and brightens those pixels less.
    scope: The preview and Apply.
    persistence: Until the window closes. **Reset** and Apply return it to **Clip**. The history does not record it.
  - name: Luminance coeffs
    kind: combo
    default: Default
    options: [Default, Image, Manual]
    effect: Chooses the weights of red, green and blue in the luminance that **Colour** stretches. **Default** and **Image** both weigh the three equally. **Manual** takes the weights from the three **R/G/B** boxes.
    enabled: Shown only with **Channel** at **Colour**.
    tradeoff: With **Image** no weights are read from the image or from a color profile, so it gives what **Default** gives.
    scope: The preview, Apply and the luminance both histograms show.
    persistence: Until the window closes. **Reset** and Apply return it to **Default**. The history does not record it.
  - name: R/G/B
    object: lumCoeffRValue
    kind: spinbox
    default: "1.0000"
    widget: {min: 0, max: 10, step: 0.1, decimals: 4}
    effect: Sets the weight of red, the first of the three boxes. Higher gives red a larger share of the luminance, lower a smaller one.
    range: The three weights are divided by their sum, so only their proportions count. With all three at zero the channels are weighed equally.
    enabled: Shown only with **Channel** at **Colour** and **Luminance coeffs** at **Manual**.
    tradeoff: Uneven weights change which colors drive the common factor, and so which pixels reach 1 first.
    scope: The luminance of **Colour** only, in the preview and Apply.
    persistence: Until the window closes. **Reset** and Apply return it to its default. The history does not record it.
  - name: R/G/B
    object: lumCoeffGValue
    kind: spinbox
    default: "1.0000"
    widget: {min: 0, max: 10, step: 0.1, decimals: 4}
    effect: Sets the weight of green, the second box. Higher gives green a larger share of the luminance, lower a smaller one.
    enabled: Shown and kept as the weight of red is.
  - name: R/G/B
    object: lumCoeffBValue
    kind: spinbox
    default: "1.0000"
    widget: {min: 0, max: 10, step: 0.1, decimals: 4}
    effect: Sets the weight of blue, the third box. Higher gives blue a larger share of the luminance, lower a smaller one.
    enabled: Shown and kept as the weight of red is.
  - name: Transformation type
    kind: combo
    default: Generalized Hyperbolic
    options: [Generalized Hyperbolic, Histogram Transformation, Arcsinh, Linear Stretch, Image Inversion, STF Transformation]
    effect: Chooses the curve. **Generalized Hyperbolic** is the stretch for a linear master, shaped by all five values. **Histogram Transformation** puts a midtones curve around the symmetry point, and **Arcsinh** an inverse hyperbolic sine. **Linear Stretch** rescales between a black and a white point. **Image Inversion** makes the negative. **STF Transformation** applies a curve derived from the image's statistics.
    enabled: Always. It decides which controls the window shows below it. The stretch factor, the symmetry point, the two protection points and the SP pickers show with **Generalized Hyperbolic**, **Histogram Transformation** and **Arcsinh**. **Local intensity (b)** shows with **Generalized Hyperbolic** only, the black and the white point with **Linear Stretch**, **Linked STF** with **STF Transformation**, and none of them with **Image Inversion**.
    tradeoff: The types differ in where they put the contrast and in what they do to color. **Histogram Transformation** here is set by the stretch factor and the symmetry point, not by the three values of the Histogram Transformation window. **STF Transformation** measures the image, which takes longer, and adapts to each frame of a sequence.
    scope: The preview and Apply, on what **Channel** selects.
    persistence: Until the window closes. **Reset** and Apply return it to **Generalized Hyperbolic**.
    note: Changing it turns off an SP picker. The values of the controls it hides are kept. Every type records the result as nonlinear.
  - name: Invert
    kind: check
    default: "Off"
    effect: Applies the inverse of the chosen curve, the one that undoes a stretch made with the same values.
    enabled: With **Generalized Hyperbolic**, **Histogram Transformation** and **Arcsinh**. Any other type clears and disables it.
    tradeoff: It does not give the earlier image back exactly, because clipping at 0 and 1, rounding to the pixel type and color conversion are not undone. The result is still recorded as nonlinear.
    scope: The preview and Apply, on what **Channel** selects.
    persistence: Until the window closes. **Reset** and Apply clear it. The history names the type with `(Inverse)` after it.
    note: It is not **Image Inversion**, which makes the negative of the image.
  - name: Linked STF
    kind: check
    default: "Off"
    effect: Chooses how **STF Transformation** derives its curve. On derives one curve from the three channels together and gives it to all of them. Off derives one curve for each channel from its own statistics.
    enabled: Shown only with **STF Transformation**. A grayscale image has one curve either way.
    tradeoff: Off brings the channels' backgrounds together, which changes the ratios between the colors. On keeps the color balance and leaves the backgrounds apart.
    scope: The preview and Apply. A **Preview area** is stretched with the statistics of the whole image, so it shows what Apply writes there. On a sequence, each frame gets the curve of its own statistics.
    persistence: Until the window closes. **Reset** and Apply clear it. The history does not record it.
    note: The derived values are shown nowhere. Both histograms draw the curve, for the channel in **Channel** when that is **Green** or **Blue** and for red otherwise.
  - name: Stretch factor(In(D+1))
    kind: spinbox
    default: "0.0000"
    widget: {min: 0, max: 20, step: 0.01, decimals: 4}
    effect: Sets the strength of the stretch. Higher steepens the curve at the symmetry point and spreads the tones around it further apart. Lower flattens it, and at 0 the curve changes nothing.
    range: The value is the natural logarithm of D + 1, where D is the strength the curve uses. The label writes the logarithm with a capital I.
    enabled: Shown with **Generalized Hyperbolic**, **Histogram Transformation** and **Arcsinh**.
    tradeoff: Too low, the image stays close to what it was. Too high, the bright tones crowd against white, and noise and the steps between tones show.
    scope: The preview and Apply, on what **Channel** selects, before the mask is blended.
    persistence: Until the window closes. **Reset** and Apply return it to its default.
  - name: Local intensity (b)
    kind: spinbox
    default: "0.0000"
    widget: {min: -5, max: 15, step: 0.01, decimals: 4}
    effect: Chooses the member of the hyperbolic family. Above 0, higher lifts the tones next to the symmetry point more and keeps the bright tones further from white. Below 0, lower makes the stretch weaker and broader, closer to a straight line.
    range: At 0 the curve is an exponential, at -1 a logarithm, and a power law at every other value.
    enabled: Shown only with **Generalized Hyperbolic**.
    tradeoff: Too low, the contrast stays spread over a wide range. Too high, it gathers in a narrow band at the symmetry point, and the passage out of that band looks abrupt although the curve has no break.
    scope: The preview and Apply, on what **Channel** selects, before the mask is blended.
    persistence: Until the window closes. **Reset** and Apply return it to its default.
  - name: Symmetry point (SP)
    kind: spinbox
    default: "0.0000"
    widget: {min: 0, max: 1, step: 0.01, decimals: 4}
    effect: Sets the tone where the curve is steepest. Higher moves the strongest contrast to brighter tones, lower to darker ones. The curve bends away from it on both sides.
    range: A value below **Protect shadows (LP)** or above **Protect highlights (HP)** takes that protection point with it.
    enabled: Shown with **Generalized Hyperbolic**, **Histogram Transformation** and **Arcsinh**.
    tradeoff: Placed in the background, it stretches the noise most. Placed far above the faint signal, it compresses the tones you want to show.
    scope: The preview and Apply, on what **Channel** selects, before the mask is blended.
    persistence: Until the window closes. **Reset** and Apply return it to its default.
    note: Typing a value, or moving its slider or the middle handle, turns off an SP picker.
  - name: Protect shadows (LP)
    kind: spinbox
    default: "0.0000"
    widget: {min: 0, max: 1, step: 0.01, decimals: 4}
    effect: Sets the tone below which the curve continues as a straight line. Higher keeps the contrast of more of the dark tones, and brightens the image. Lower lets the curve compress them.
    range: It cannot be set above the symmetry point, and a higher value returns to it. It clips nothing.
    enabled: Shown with **Generalized Hyperbolic**, **Histogram Transformation** and **Arcsinh**.
    tradeoff: The higher it is, the more of the output range goes to the tones below the symmetry point, and the less is left for the signal above it.
    scope: The preview and Apply, on what **Channel** selects, before the mask is blended.
    persistence: Until the window closes. **Reset** and Apply return it to its default.
    note: The reference page has a table of checkpoints for a curve with both protection points set.
  - name: Protect highlights (HP)
    kind: spinbox
    default: "1.0000"
    widget: {min: 0, max: 1, step: 0.01, decimals: 4}
    effect: Sets the tone above which the curve continues as a straight line. Lower keeps the contrast of more of the bright tones, such as star cores, and darkens the rest a little. Higher lets the curve compress them.
    range: It cannot be set below the symmetry point, and a lower value returns to it. It clips nothing.
    enabled: Shown with **Generalized Hyperbolic**, **Histogram Transformation** and **Arcsinh**.
    tradeoff: The lower it is, the more of the output range goes to the bright tones, and the less is left for the faint signal.
    scope: The preview and Apply, on what **Channel** selects, before the mask is blended.
    persistence: Until the window closes. **Reset** and Apply return it to its default.
  - name: Black point (BP)
    kind: spinbox
    default: "0.0000"
    widget: {min: 0, max: 1, step: 0.01, decimals: 4}
    effect: Sets the black point of **Linear Stretch**. Higher sends more of the low samples to black and spreads the rest over the range. Lower keeps more of the shadows.
    range: It stays at least 0.0001 below **White point (WP)**, and a higher value returns to that.
    enabled: Shown only with **Linear Stretch**. The left handle follows it.
    tradeoff: What it sends to black is lost.
    scope: The preview and Apply, on the single channel **Channel** names or else on all three channels.
    persistence: Until the window closes. **Reset** and Apply return it to its default.
  - name: White point (WP)
    kind: spinbox
    default: "1.0000"
    widget: {min: 0, max: 1, step: 0.01, decimals: 4}
    effect: Sets the white point of **Linear Stretch**. Lower sends more of the high samples to white and spreads the rest over the range. Higher keeps more of the highlights.
    range: It stays at least 0.0001 above **Black point (BP)**, and a lower value returns to that.
    enabled: Shown only with **Linear Stretch**. The right handle follows it.
    tradeoff: What it sends to white, star cores first, is lost.
    scope: The preview and Apply, on the single channel **Channel** names or else on all three channels.
    persistence: Until the window closes. **Reset** and Apply return it to its default.
  - name: Pick from image point
    kind: button
    default: "Off"
    effect: Sets the symmetry point from a pixel you click. The value is the mean of the pixel's channels, whatever **Channel** says.
    range: The first of the three **SP picker** buttons. Only one of the three is on at a time.
    enabled: Shown with **Generalized Hyperbolic**, **Histogram Transformation** and **Arcsinh**. Click in the preview, or in the image's own view when **Preview** is off.
    tradeoff: One pixel may be a star, a hot pixel or noise. Every further click sets the symmetry point again while the button is on.
    scope: The value is read from the image itself, not from the stretched preview, and without the mask.
    persistence: It stays on after a click. Pressing it again, another picker, a value typed in **Symmetry point (SP)**, a drag of the middle handle, a change of **Transformation type** or **Reset** turns it off.
    note: While it is on, the window shows `** PICK VALUE FROM IMAGE **` under the buttons, and the pointer shows its coordinates.
  - name: Pick from image area
    kind: button
    default: "Off"
    effect: Sets the symmetry point from a rectangle you draw. The value is the mean of the rectangle, averaged over the channels.
    range: The second **SP picker** button. The part of the rectangle outside the image is left out.
    enabled: Shown and used as the first picker is.
    tradeoff: An area is steadier than one pixel, but it may mix background and nebula. Every rectangle you draw or move sets the symmetry point again while the button is on.
    scope: The value is read from the image itself, without the mask.
    persistence: It stays on after a rectangle, and is turned off as the first picker is.
    note: While it is on, the window shows `** PICK VALUE FROM IMAGE AREA **` under the buttons.
  - name: Pick from histogram point
    kind: button
    default: "Off"
    effect: Sets the symmetry point to the place you click in the upper histogram, read along its horizontal axis from 0 to 1.
    range: The third **SP picker** button. A line marks the last click, and the pointer becomes a cross.
    enabled: Shown as the first picker is.
    tradeoff: The value is as fine as the histogram is wide on screen. Zoom the histogram for a finer one, or type the value. Every click sets the symmetry point again while the button is on.
    scope: The upper histogram only. A click there shows no coordinates while the button is on.
    persistence: It stays on after a click, and is turned off as the first picker is.
    note: While it is on, the window shows `** PICK VALUE FROM HISTOGRAM **` under the buttons.
  - name: Target Image
    kind: combo
    default: The active image
    options: [The active image, Every other open image or sequence the window can work on]
    effect: Chooses the image Apply replaces. The histograms, the channels, the pickers and the preview follow it.
    tradeoff: A larger image or a sequence takes more time, memory and disk.
    persistence: Until the window closes. **Reset** leaves it as it is.
  - name: Preview
    kind: check
    default: "On"
    effect: Shows the stretched image beside the controls, with the mask blended in. Off removes it. The histograms keep working.
    tradeoff: Each change stretches the whole image again, and a color choice of **Channel** adds conversions to it. On a large image, turn it off while you adjust and on again for a check.
    scope: The window only. The image is not changed, and a newer change replaces a preview still being computed.
    persistence: Until the window closes. **Reset** leaves it as it is.
  - name: Reset
    kind: button
    default: ""
    effect: Returns every value, **Channel**, **Transformation type**, **Invert**, **Linked STF**, **Colour clip**, **Luminance coeffs** and the three weights to their defaults, turns off an SP picker and restores the zoom of both histograms.
    range: It leaves the toolbar's choices, the target and **Preview** as they are.
    scope: The window only. The image is not changed, and Apply stays enabled.
  - name: Apply
    kind: button
    default: ""
    effect: Stretches the image, or every frame of the sequence, with the type and the values shown, and then returns the controls to their defaults.
    enabled: From the moment the window opens, with every value at its default too. An Apply at the defaults only cuts samples outside 0 to 1, and still records the image as nonlinear.
    range: While another process is working on the target, it only shows "Another process is executing for this image, please cancel that job first before running this process".
    scope: The whole image or sequence, on what **Channel** selects, with the mask blended afterwards.
    persistence: The image and its history keep the result. The history records the type, **Channel**, the stretch factor, the local intensity, the symmetry point, both protection points, and the black and the white point. It does not record the mask, **Colour clip**, the luminance weights, **Linked STF**, the curve an STF derived or the toolbar's choices. The image no longer states a transfer function.
    note: Closing the window instead discards the values. Nothing of the window is saved between sessions, and it has no presets.
```

```controls
window: Sequence progress
controls:
  - name: Progress
    kind: readout
    default: ""
    effect: Shows the preparation and the frame being stretched, from 0 to 100 percent.
    enabled: Shown only while Apply works on a sequence. A single image has no progress window.
    tradeoff: The stretched sequence is written to a new file, whose name starts with `SEQ_SWAP`, in the session's temporary folder, which needs the disk space of a second copy. The view shows it in place of the original only when every frame succeeded, and the original file is not changed.
    scope: One set of values, taken when Apply starts, for every frame. **STF Transformation** measures each frame and derives its curve again.
    note: A sequence whose view debayers its mosaic frames is stretched as RGB frames, and the sequence written is RGB. Turn that option of the view off first to keep the frames as mosaics.
  - name: Cancel
    kind: button
    default: ""
    effect: Asks the run to stop. The temporary sequence is removed and the original stays as it was.
    range: The request is noticed between frames and at intervals inside one, so the frame being read or written may finish first.
    note: Closing the Generalized Hyperbolic Transformation window while a sequence is being stretched asks the run to stop too. The window goes out of sight and closes when the run has ended.
```

## How it works

1. The window counts a histogram of each channel, and of the component a color choice of **Channel** stretches, at
   the toolbar's **Plot resolution**.
2. The type and its values define one curve from 0 to 1. **STF Transformation** first measures the median and the
   deviation of every valid sample of the whole image, once for each state of the image.
3. Every sample **Channel** selects is cut to the range 0 to 1 and sent through the curve: each channel on its
   own, or the lightness, the saturation or a luminance, from which the three channels are rebuilt.
4. The result is written back in the image's own pixel type, rounded to the nearest value. The channels left out and
   an alpha channel are kept exactly as they were. A mask on the view then blends the result with the original.
5. The image replaces the target and its history gets one step. Its header, its acquisition data and its
   [plate solution](../../reference/glossary.md#plate-solution) are kept, and its
   [processing state](../../reference/glossary.md#processing-state) is recorded as nonlinear with the operation
   `ght-stretch`.

### Model

The value \(s\) of **Stretch factor(In(D+1))** gives the strength \(D\):

\[ D = e^{s} - 1 \tag{1} \]

The first three types build their curve from a base curve \(g(t)\) of the distance \(t\) from the symmetry point.
With \(b\) the value of **Local intensity (b)**, the base curve of **Generalized Hyperbolic** is

\[
g(t) =
\begin{cases}
\dfrac{\ln(1 + D\,t)}{D}, & b = -1 \\[1.5ex]
\dfrac{1 - (1 - b\,D\,t)^{(b+1)/b}}{(b + 1)\,D}, & b < 0,\ b \ne -1 \\[1.5ex]
\dfrac{1 - e^{-D\,t}}{D}, & b = 0 \\[1.5ex]
\dfrac{1 - (1 + b\,D\,t)^{-1/b}}{D}, & b > 0
\end{cases}
\tag{2}
\]

**Histogram Transformation** and **Arcsinh** use these base curves instead:

\[ g(t) = \frac{(m - 1)\,t}{(2m - 1)\,t - m} \quad \text{with} \quad m = \frac{1}{2\,(D + 1)}, \qquad g(t) = \operatorname{asinh}(D\,t) \tag{3} \]

With \(S\) the symmetry point, \(L\) and \(H\) the two protection points and \(g'\) the slope of \(g\), a sample
\(x\) between 0 and 1 is given the value

\[
q(x) =
\begin{cases}
-g(S - L) - g'(S - L)\,(L - x), & x < L \\
-g(S - x), & L \le x < S \\
g(x - S), & S \le x \le H \\
g(H - S) + g'(H - S)\,(x - H), & x > H
\end{cases}
\tag{4}
\]

and the curve is

\[ F(x) = \frac{q(x) - q(0)}{q(1) - q(0)} \tag{5} \]

Equation (5) sends 0 to 0 and 1 to 1, and is steepest at \(S\). Below \(L\) and above \(H\) the curve goes on as a
straight line with the slope it has there, so a protection point makes no corner and clips nothing. With \(D = 0\)
the curve changes nothing. **Invert** sends a sample \(y\) to the \(x\) for which \(F(x) = y\), which the
[reference page](gh-transformation-reference.md) gives in closed form.

With \(B\) and \(W\) the black and the [white point](../../reference/glossary.md#white-point), **Linear Stretch**
and **Image Inversion** are

\[ F(x) = \min\Bigl(1,\ \max\Bigl(0,\ \frac{x - B}{W - B}\Bigr)\Bigr), \qquad F(x) = 1 - x \tag{6} \]

**STF Transformation** takes, for each channel \(j\), the median \(v_j\) and the deviation \(\sigma_j\), which is
1.4826 times the [median absolute deviation](../../reference/glossary.md#median-absolute-deviation), or 0.001 when
that is zero. With \(M_m\) the base curve of equation (3) for a balance \(m\), it derives a
[black point](../../reference/glossary.md#black-point) \(c\) and a balance, and stretches the range from \(c\) to 1:

\[ c = \max\bigl(0,\ \overline{v_j - 2.8\,\sigma_j}\bigr), \qquad m = M_{0.25}\bigl(\overline{v_j} - c\bigr), \qquad F(x) = M_m\Bigl(\frac{x - c}{1 - c}\Bigr) \tag{7} \]

The bar is the mean over the channels, with **Linked STF** on. With it off, equation (7) is applied to each channel
alone. When every median is above 0.5 the roles turn round, and a white point is derived instead of the black
point.

**Colour** forms the luminance \(Y = w_R R + w_G G + w_B B\), with weights that sum to 1, and multiplies the three
channels by one factor \(k\). **Rescale** uses the second factor:

\[ k = \frac{F(Y)}{Y}, \qquad k' = \frac{k}{\max\bigl(1,\ k \cdot \max(R, G, B)\bigr)} \tag{8} \]

### Boundaries

- A sample that is not a finite number, in a channel being stretched, stops the run with "a selected stretch sample
  is not finite". Pixels the image marks as invalid are left unchanged.
- **STF Transformation** refuses an image without a valid sample, and one whose median lies outside 0 to 1. The
  preview then reports "The preview failed" and Apply "Error executing the process", and nothing is written.
- A 64-bit floating-point or 32-bit integer image is computed in double precision, every other type in single
  precision. In the channels left alone, floating-point samples outside 0 to 1 are kept. After **Lightness**,
  **Saturation** or **Colour**, all three channels lie between 0 and 1.
- The window is made for images of one or three channels, and opens on others without a warning.
- A single image has no progress window and cannot be cancelled once Apply starts. A run that fails changes nothing.

### Worked example

The master in the picture above is an RGB image whose three channels have their median near 0.0277. Set **Symmetry
point (SP)** to 0.025, just below it, **Stretch factor(In(D+1))** to 2.5 and **Local intensity (b)** to 5, and leave
the rest. Equation (1) gives \(D = 11.18\), and equations (2), (4) and (5) send the symmetry point to 0.23 and the
median to 0.26: the background becomes a dark gray. A sample of 0.10 lands at 0.62 and one of 0.50 at 0.91, so the
nebula spreads over the range above the background and stays below white.

```figure
images:
  - image: ../../assets/images/processes/stretching/gh-transformation-before.webp
    alt: The whole frame before the stretch, black except for a few of the brightest stars.
    label: Before
  - image: ../../assets/images/processes/stretching/gh-transformation-after.webp
    alt: The whole frame after the stretch, the clouds of the nebula in pale red and white across a field of stars on a gray, grainy background.
    label: After
caption: >-
  The Carina Nebula master before and after Apply with the symmetry point at 0.025, a stretch factor of 2.5 and a
  local intensity of 5. Both are the whole frame as the view shows it in the Linear display mode, without a display
  stretch.
```

## Starting points

| Situation | Start | Then adjust |
| --- | --- | --- |
| A linear RGB master | **RGB** and **Generalized Hyperbolic**, the symmetry point at the histogram's peak | The stretch factor, then the local intensity, then the protection points, one at a time. |
| Bright colors wash out or shift | **Channel** at **Colour** with **Colour clip** at **Rescale** | Compare **Clip**, **Lightness** and **RGB** at the same values, on the brightest stars at 100 %. |
| A first look, or a quick result | **STF Transformation** with **Linked STF** on | Turn **Linked STF** off only to bring the channels' backgrounds together. Its curve cannot be tuned. |
| Only the range needs trimming | **Linear Stretch** with both points at their defaults | Move one point at a time, and watch the clipping figures. |

## When it goes wrong

!!! warning "Apply cannot be taken back from this window"
    Every type replaces the image and records it as nonlinear, **Linear Stretch**, **Image Inversion** and a stretch
    factor of 0 included. **Invert** does not give the linear master back. Use the image's history to return.

!!! warning "STF Transformation leaves no numbers"
    The curve it derives is drawn, but its values are written nowhere, and the history does not record **Linked
    STF**. Use your own values when you must be able to repeat the stretch.

The preview does not change when you move a value
:   **Cause.** **Stretch factor(In(D+1))** is at 0, where the three curves it drives change nothing.
:   **Fix.** Raise the stretch factor first.

The symmetry point changes when you click in the preview or in the upper histogram
:   **Cause.** An **SP picker** button is still on.
:   **Fix.** Press the button again to turn it off.

A protection point seems to clip the image
:   **Cause.** Black and white samples come from values outside 0 to 1 in the image, from the two points of **Linear
    Stretch** or from rounding, never from a protection point.
:   **Fix.** Read the clipping figures of the lower histogram, and check the result with Image Statistics.

The colors of bright stars shift with Colour
:   **Cause.** **Clip** cuts each channel at 1 on its own, or uneven weights let one color drive the factor.
:   **Fix.** Choose **Rescale**, check the three weights, and compare at 100 %.

The clipping figures read zero, or change with Plot resolution
:   **Cause.** On the upper histogram they count its first and last bin, and each share is of all the channels.
:   **Fix.** Compare figures at one resolution only, and use Image Statistics after Apply.

The frames of a sequence come out unevenly bright
:   **Cause.** **STF Transformation** derives a curve from each frame's own statistics.
:   **Fix.** Undo the step, then use another type, whose values give every frame one curve.

## Related

- [Background Extraction](../background-extraction/background-extraction.md),
  [Background Neutralization](../color-calibration/background-neutralization.md) and
  [Photometric Color Calibration](../color-calibration/photometric-cc.md), which need the image linear and come
  before.
- [Generalized Hyperbolic Transformation Reference](gh-transformation-reference.md), for the inverse curves, the
  derivation of the STF curve, checkpoints of a curve and the history text.
- [Histogram Transformation](histogram-transformation.md) and [ArcSinh Stretch](arcsinh-stretch.md), two other
  stretches for a linear master, each in its own window.
- [Curves Transformation](curves-transformation.md), to shape tone and color after the stretch.
- [Image History](../../interface/image-history.md) and [Image Statistics](../../interface/image-statistics.md), to
  return to the linear master and for exact counts after Apply.
- [Process Windows](../../interface/process-windows.md), for the preview, the histogram toolbar and what every
  process window shares.

## Credits

- Mike Cranfield's [Generalized Hyperbolic Stretch documentation](https://www.ghsastro.co.uk/doc/tools/GeneralizedHyperbolicStretch/GeneralizedHyperbolicStretch.html)
  describes the five-parameter stretch, its curve families, its inverse, its protection points and its color modes,
  which this explanation follows. It credits David Payne's proposal of 2021 and his work with Mike Cranfield.
- The [Generalized Hyperbolic Stretch information page](https://ghsastro.co.uk/information/) of
  [GHS](../../reference/glossary.md#ghs) Astro gives the
  authors and the publication history of the algorithm.
