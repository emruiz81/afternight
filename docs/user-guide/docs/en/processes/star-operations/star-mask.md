---
kind: process
stage: noise
menu: Process > Mask Generation > Star Mask
summary: >-
  Finds the stars of an image with a fitted star detector and paints them into a new grayscale mask, each star a soft
  disc or a solid ellipse, for later work on the stars or around them.
data_in: linear
data_out: nonlinear
destructive: false
preview: real-time
sequences: true
masks: none
external: none
glance:
  enabled: >-
    An image or a sequence view is active, no Star Mask window is open on it yet, and no window that needs the image
    for itself is open on it.
  inputs: >-
    One grayscale or RGB image or sequence, with or without alpha, of any pixel type, that is not a raw color-filter
    mosaic. A mask on its view is not used.
  outputs: >-
    A new grayscale image or sequence named after the target with " - STARMASK", of the target's size and pixel type.
    The target is not changed.
  state: >-
    A calibrated linear image, debayered and without strong gradients, whose stars are to be protected or treated
    apart from the rest.
derived: true
rewritten: true
---
# Star Mask

This window finds the stars of an image and paints them into a new grayscale
[mask](../../reference/glossary.md#mask). A detector fits a profile to each star, and each star it accepts becomes a
soft disc, whose size follows the star's width, or a solid ellipse. Attach the mask to an image through
[Masks](../../interface/masks.md), and a later process, such as a noise reduction or Star Reduction, works on the stars
alone or, inverted, around them.

**Apply** opens the mask as a new image named `<target> - STARMASK`, of the target's size and
[pixel type](../../reference/glossary.md#pixel-type), and closes the window. The target is not changed, and the mask is
recorded as nonlinear.

```figure
image: ../../assets/images/processes/star-operations/star-mask.webp
alt: >-
  The Star Mask window. On the left, the Detection group with Radius at 5 and Threshold at 1.00, the grayed Amplitude
  Range group, Roundness Threshold at 0.50, the grayed Roundness Range group, Convergence at 1, Relax PSF Checks,
  Profile Type at Gaussian, a grayed Minimum Beta and Use Lightness. Under it the ticked Shaped Areas group with
  Percent Increase at 1.00, Pixel Increase at 0, Min Size at 2, Max Size at 25 and Star Shape at Truncated Bell, then
  Target Image and Preview. On the right, the preview's toolbar with Display Mode at Linear over the whole mask, small
  white dots of stars on black. Reset, Close and Apply run along the bottom.
caption: >-
  Star Mask on the linear Carina master with Percent Increase at 1.00. With the preview area off and the Linear
  display mode, the preview shows the whole mask Apply makes.
pins:
  - {n: 2, x: 66.0, y: 16.0}
  - {n: 3, x: 2.7, y: 16.6}
  - {n: 4, x: 2.7, y: 34.5}
  - {n: 5, x: 2.7, y: 57.4}
  - {n: 6, x: 2.7, y: 61.4}
  - {n: 7, x: 91.0, y: 12.9}
```

## Before you start

- Use the calibrated [linear image](../../reference/glossary.md#linear-image). The detector reads every image as linear,
  with 1 as full scale, so a [stretch](../../reference/glossary.md#stretch) changes the widths it measures and the stars
  it takes as saturated.
- One sky level serves the whole image. Remove strong gradients first, or faint stars on the dark side are missed and
  noise on the bright side is taken for stars.
- [Hot pixels](../../reference/glossary.md#hot-pixel) and noise peaks can pass for stars. Calibrate the image, or
  repair them, before you make the mask.
- A raw color-filter mosaic is refused: [debayer](../../reference/glossary.md#debayer) it first. A
  [sequence](../../reference/glossary.md#sequence) of raw frames is searched after the view's automatic debayer, when
  it is set.
- A mask on the target's view is not used, and need not be disabled.

## Steps

1. **Open the window.** Make the image the [active image](../../reference/glossary.md#active-image) and choose
   **Process > Mask Generation > Star Mask**. The preview shows the mask of a square
   [preview area](../../reference/glossary.md#preview-area) in the middle, over the rest of the image in gray.
2. **See the mask as it is.** Choose **Linear** in the pane's **Display Mode**, and zoom in on the area. In
   **Autostretch** the mask's soft edges look brighter than they are.
3. **Find the stars.** Match **Radius** to the size of the stars. Lower **Threshold** to find fainter stars, or raise
   it to leave out noise.
4. **Leave out what is not a star.** Raise **Roundness Threshold** to refuse elongated fits, and tick **Amplitude
   Range** to keep only stars of some brightness.
5. **Choose the footprint.** Leave **Shaped Areas** ticked for a soft disc on each star, or untick it for solid
   ellipses.
6. **Size the discs.** Raise **Percent Increase** to cover each star, add **Pixel Increase** for its halo, and choose
   a **Star Shape**.
7. **Check the whole image.** Untick **Preview area** in the pane's toolbar, and hold ++space++ over the preview to
   compare with the original.
8. **Apply.** Choose **Apply**. A progress window shows while the run works, then the mask opens as
   `<target> - STARMASK` at the right of the workspace, and the window closes.

## Controls

**Target Image**, the preview pane, **Close** and the progress window work as in every process window: see
[Process Windows](../../interface/process-windows.md). The window has no histogram and no **Channel**. What is
particular to it is below.

```controls
window: Star Mask
controls:
  - name: Radius
    kind: spinbox
    default: "5"
    widget: {min: 3, max: 32, step: 1}
    effect: Sets half the width, in pixels, of the box a star must be the brightest point of. At 5 the box is 11 by 11 pixels. Higher looks for broader stars. Lower suits small, compact stars.
    range: The image, or the preview area, must be more than 4 × **Radius** + 8 pixels wide and tall, more than 28 at the default, or the run is refused. The search skips a border of **Radius** pixels.
    enabled: Disabled while Apply runs.
    tradeoff: Higher costs more work and loses a fainter star within **Radius** of a brighter one. Too low splits broad stars or cuts them short.
    scope: The detection, in the preview, a snapshot and Apply, on every frame of a sequence.
    persistence: Until the window closes. **Reset** returns it to its default.
  - name: Threshold
    kind: spinbox
    default: "1.00"
    widget: {min: 0.01, max: 15, step: 0.05, decimals: 2}
    effect: Sets how far above the sky a star's peak must stand, as a multiple of five noise deviations, five at 1 and ten at 2. Lower finds fainter stars and more noise. Higher keeps only the clearer stars.
    range: The slider crosses the range in 1,000 steps.
    enabled: Disabled while Apply runs.
    tradeoff: A low value turns noise peaks into candidates, and every candidate is fitted, which costs time. A high value misses faint stars.
    scope: The plane the stars are searched in, by equation (1).
    persistence: Until the window closes. **Reset** returns it to its default.
    note: A fit is also refused when its residual, times this value, is more than a fifth of its peak, unless the star is saturated. A higher value asks for a closer fit too.
  - name: Amplitude Range
    kind: check
    default: "Off"
    effect: Ticked, keeps only the stars whose fitted peak above the local sky lies between **Minimum** and **Maximum**, in fractions of full scale.
    enabled: Its two boxes are disabled while it is unticked. Disabled while Apply runs.
    tradeoff: It can leave out faint fits on noise, or saturated stars. A narrow window can leave no star. On a linear image whose brightest stars peak above 1, those stars are above any **Maximum**.
    scope: Every fit, after it is made.
    persistence: Until the window closes. **Reset** unticks it.
  - name: Minimum
    kind: spinbox
    default: "0.00"
    widget: {min: 0, max: 0.99, step: 0.05, decimals: 2}
    effect: Sets the smallest fitted peak a star may have. Higher leaves out the fainter stars, and the noise fitted as stars.
    enabled: With **Amplitude Range** ticked.
    persistence: Until the window closes. **Reset** returns it to its default.
  - name: Maximum
    object: amplitudeMaximumValue
    kind: spinbox
    default: "1.00"
    widget: {min: 0.01, max: 1, step: 0.05, decimals: 2}
    effect: Sets the largest fitted peak a star may have. Lower leaves out the brighter stars.
    range: Below **Minimum**, the run is refused with the reason.
    enabled: With **Amplitude Range** ticked.
    persistence: Until the window closes. **Reset** returns it to its default.
  - name: Roundness Threshold
    kind: spinbox
    default: "0.50"
    widget: {min: 0.01, max: 0.95, step: 0.05, decimals: 2}
    effect: Sets the smallest ratio a star may have of its narrower fitted width to its wider one. Higher keeps only rounder stars. Lower admits elongated stars, trails and pairs of stars.
    enabled: Disabled while Apply runs.
    tradeoff: On trailed or distorted stars, a high value leaves many real stars out.
    scope: Every fit, after it is made.
    persistence: Until the window closes. **Reset** returns it to its default.
  - name: Roundness Range
    kind: check
    default: "Off"
    effect: Ticked, sets an upper limit on the same ratio with its **Maximum**. Unticked, the limit is 1, a perfectly round star.
    enabled: Its box is disabled while it is unticked. Disabled while Apply runs.
    tradeoff: It keeps the elongated stars and leaves out the round ones, which is rarely wanted. A narrow band can leave no star.
    persistence: Until the window closes. **Reset** unticks it.
  - name: Maximum
    object: roundnessMaximumValue
    kind: spinbox
    default: "1.00"
    widget: {min: 0.01, max: 1, step: 0.05, decimals: 2}
    effect: Sets the largest ratio of the two widths a star may have. Lower leaves out the roundest stars.
    range: Below **Roundness Threshold**, the run is refused with the reason.
    enabled: With **Roundness Range** ticked.
    persistence: Until the window closes. **Reset** returns it to its default.
  - name: Convergence
    kind: spinbox
    default: "1"
    widget: {min: 1, max: 3, step: 1}
    effect: Allows each fit more iterations to settle, 20 for each unit with **Gaussian**, twice as many with **Moffat**, and three times as many again for a star with saturated pixels. A fit that does not settle is not accepted.
    enabled: Disabled while Apply runs.
    tradeoff: Higher can rescue difficult stars, at the cost of time.
    persistence: Until the window closes. **Reset** returns it to its default.
  - name: Relax PSF Checks
    kind: check
    default: "Off"
    effect: Skips four checks, the scan's tests of a candidate's symmetry and brightness, and the fit's tests of its width against the scan's estimate and of its residual.
    range: The roundness and amplitude windows, **Minimum Beta**, and the tests of a finite position and width and a positive brightness still apply.
    enabled: Disabled while Apply runs.
    tradeoff: It keeps more of the difficult stars, and also pairs of stars, edges and poor fits. More candidates reach the fit, which costs time.
    persistence: Until the window closes. **Reset** unticks it.
  - name: Profile Type
    kind: combo
    default: Gaussian
    options: [Gaussian, Moffat]
    effect: Chooses the profile each candidate is fitted with, an elliptical Gaussian or an elliptical Moffat profile, whose exponent beta is fitted with the rest and shapes its wings.
    enabled: "**Moffat** enables **Minimum Beta**. Disabled while Apply runs."
    tradeoff: "**Moffat** follows broad wings more closely, costs more time and can accept far fewer stars, when its fits fail or its beta falls below **Minimum Beta**. Start with **Gaussian**."
    persistence: Until the window closes. **Reset** returns it to **Gaussian**.
  - name: Minimum Beta
    kind: spinbox
    default: "1.50"
    widget: {min: 0.05, max: 10, step: 0.05, decimals: 2}
    effect: Sets the smallest Moffat exponent a star may have. A lower beta means heavier wings, so higher leaves out the stars with the broadest wings.
    range: Beta is fitted between 0 and 10.
    enabled: With **Profile Type** at **Moffat**.
    tradeoff: Too high refuses real stars with wide wings. Too low admits fits that are not stable.
    persistence: Until the window closes. **Reset** returns it to its default.
  - name: Use Lightness
    kind: check
    default: "Off"
    effect: Ticked, finds the stars of an RGB image in its CIE lightness, L*, which combines the three channels. Unticked, in its green channel. A grayscale image is searched in its one channel either way.
    enabled: Disabled while Apply runs.
    tradeoff: The lightness costs a conversion and a plane of its own. It is not proportional to the light, so on a linear image it compresses the cores of bright stars, which then measure wider. Green is often the sharpest channel of a one-shot-color camera, which samples it twice.
    scope: Only the plane the stars are searched in. The mask has one channel either way.
    persistence: Until the window closes. **Reset** unticks it.
    note: Searched in green, a star fitted 1 pixel wide or less on either axis is not accepted. Searched in the lightness, or in a grayscale image, it can be. In the lightness, a pixel the image marks as invalid reads as black instead of taking the sky's value. The red, green and blue are read as linear when the image is described as linear RGB, and as sRGB-encoded otherwise. On a grayscale image the box only adds `, on the lightness` to the history text.
  - name: Shaped Areas
    kind: check
    default: "On"
    effect: Ticked, paints each star as a disc whose value falls from its center by the **Star Shape**, with the group's size and growth. Unticked, paints each star as a solid ellipse at full value, from its fitted widths and angle.
    enabled: Unticked, the five controls of the group are disabled and have no effect. Disabled while Apply runs.
    tradeoff: A disc is round even for an elongated star, since its radius comes from the wider width. An ellipse keeps the star's shape and angle, but its edges are hard and no control here grows or softens it.
    scope: The painting only. The same stars are found either way.
    persistence: Until the window closes. **Reset** ticks it.
  - name: Percent Increase
    kind: spinbox
    default: "0.50"
    widget: {min: 0.01, max: 1, step: 0.05, decimals: 2}
    effect: Multiplies the star's wider fitted width to give the disc's radius, by equation (3). At 1.00 the radius is the full width at half maximum, and the default halves it. Higher makes every disc larger in proportion to its star.
    range: It is a factor, not a percentage added, whatever its label and its tooltip suggest. The box's step grows and shrinks with the value.
    enabled: With **Shaped Areas** ticked.
    tradeoff: Low values cover only the cores. High values on a dense field merge the discs.
    persistence: Until the window closes. **Reset** returns it to its default.
  - name: Pixel Increase
    kind: spinbox
    default: "0"
    widget: {min: 0, max: 99, step: 1}
    effect: Adds this many pixels to every disc's radius, after **Percent Increase**.
    enabled: With **Shaped Areas** ticked.
    tradeoff: It is the same for every star, so it matters most for the small ones, and it merges neighbouring discs sooner.
    persistence: Until the window closes. **Reset** returns it to its default.
  - name: Min Size
    kind: spinbox
    default: "2"
    widget: {min: 0, max: 99, step: 1}
    effect: Leaves out every star whose wider fitted width, doubled, is below this number of pixels. Higher leaves out small stars and hot pixels.
    range: It tests twice the width, not the radius its tooltip names. Above **Max Size**, the run is refused with the reason.
    enabled: With **Shaped Areas** ticked. Solid ellipses are painted whatever their size.
    persistence: Until the window closes. **Reset** returns it to its default.
  - name: Max Size
    kind: spinbox
    default: "25"
    widget: {min: 0, max: 99, step: 1}
    effect: Leaves out every star whose wider fitted width, doubled, is above this number of pixels. At the default, a star fitted more than 12.5 pixels wide is left out. Lower leaves out more of the large stars.
    range: It tests twice the width, not the radius its tooltip names.
    enabled: With **Shaped Areas** ticked.
    tradeoff: High values keep large, saturated stars, and any broad structure the detector took for a star.
    persistence: Until the window closes. **Reset** returns it to its default.
  - name: Star Shape
    kind: combo
    default: Truncated Bell
    options: [Bell, Truncated Bell, Linear, Truncated Linear, Cubic, Quadratic]
    effect: "Chooses how a disc's value falls from its center to its edge at three radii, by equation (4). **Bell** falls smoothly from the center. **Truncated Bell** is white out to one radius, then falls as the bell. **Linear** is a cone, and **Truncated Linear** a cone that is white over its inner third. **Cubic** and **Quadratic** stay near white longer and fall more steeply at the edge."
    range: "**Quadratic** uses a fourth power, despite its name."
    enabled: With **Shaped Areas** ticked.
    tradeoff: The two bells still hold a little value where the disc ends, which leaves a faint square step around a large star. The truncated shapes cover the star's core fully and leave a narrower soft edge.
    persistence: Until the window closes. **Reset** returns it to **Truncated Bell**.
  - name: Target Image
    kind: combo
    default: The active image
    options: [The active image, Every other open image or sequence the window can work on]
    effect: Chooses the image the stars are found in, which the mask is named after and takes its size and pixel type from.
    tradeoff: The wrong image gives a mask of the wrong stars, which may not even fit the image you mean to mask.
    persistence: Until the window closes. **Reset** leaves it as it is.
  - name: Preview
    kind: check
    default: "On"
    effect: Shows the mask beside the controls.
    range: >-
      The pane opens with a **Preview area** of 600 by 600 pixels in the middle of the image, labelled "Preview: Star
      Mask", or the image's size when it is smaller. When the toolbar has no room for them - at the window's default
      size with **Display Mode** at Autostretch - **Preview area** and **Take snapshot** are in the menu at its end.
    enabled: It stays enabled while Apply runs.
    tradeoff: Every change runs the whole detection again over the area, or over the whole image, never over a reduced copy, since the sky and the ranking of the stars are measured on all of it. A preview of the whole image does the work of an Apply, and holds a copy of the image, the mask and the detector's working planes while it runs.
    scope: The window only. The area is searched as an image of its own, with its own sky, noise and edges, so the stars near its edges, and the faintest ones, can differ from what Apply finds there.
    persistence: Until the window closes. **Reset** leaves it as it is.
    note: A color image shows the mask of the area over the rest of the image in gray, and the pane's **View Channel** shows a disabled **Grayscale**. In **Autostretch** the mask is stretched for the screen, which brightens its soft edges. A snapshot holds the mask's samples, area included, and gains no lineage entry.
  - name: Reset
    kind: button
    default: ""
    effect: Returns every control of the **Detection** and **Shaped Areas** groups to its default, unticks the two range groups and ticks **Shaped Areas**. One preview follows.
    range: It leaves **Target Image**, **Preview**, the preview area and the pane's toolbar as they are.
    enabled: Disabled while Apply runs.
  - name: Apply
    kind: button
    default: ""
    effect: Finds the stars of the whole image, or of each frame of a sequence, paints the mask, opens it as `<target> - STARMASK` at the right of the workspace and closes the window.
    enabled: From the moment the window opens, at any values. While the run works, **Reset**, **Close** and every control of the two groups are disabled.
    tradeoff: The run holds a copy of the image, a floating-point copy of an integer image, the lightness when asked for, and the mask with a floating-point copy for an integer target. Painting checks every star on every row, so many stars with large discs take longer. A sequence adds the disk space of a new temporary sequence.
    scope: The whole image. A preview area does not limit it, and the target's mask is not used. Each frame of a sequence gets the mask of its own stars.
    persistence: "The new image's history starts with **Star Mask** and the values, as `Created in shaped areas mode. Radius: 5, Threshold: 1.00, Profile: Gaussian. Percent Increase: 0.50, Pixel Increase: 0, Min Size: 2, Max Size: 25, Star Shape: Truncated Bell`. Without **Shaped Areas** it reads `binary areas mode`, and with **Use Lightness** `, on the lightness` follows the profile. The amplitude and roundness windows, **Convergence**, **Relax PSF Checks** and **Minimum Beta** are not recorded. The history does not list the target's steps as inherited lineage. The target and its history are not changed."
    note: A floating-point target gets the mask's values as they are. An integer target gets them as shares of its range, rounded to the nearest code, ties to the even one, from the smallest code of a signed type to its largest. Nothing is written to a file, so save the mask if you need it after the session.
```

```controls
window: Progress
controls:
  - name: Progress
    kind: readout
    default: ""
    effect: Shows a busy indicator, without a share done, while the image is searched and painted. On a sequence it adds the frame being processed and the share of frames done.
    enabled: For every Apply, on a single image too.
  - name: Cancel
    kind: button
    default: ""
    effect: Stops the run at its next stop point, between bands of rows of the search, between the stars it fits, between painted rows and between frames.
    persistence: A stopped run opens no image, leaves no part of a sequence and shows no error.
```

## How it works

1. The stars are searched in one plane: a grayscale image's channel, an RGB image's green or, with **Use Lightness**,
   its lightness. Samples are read as [fractions of full scale](../../reference/glossary.md#fraction-of-full-scale),
   as linear data whatever the image records. A pixel the image marks as invalid, or one that holds no finite number,
   takes the value of the sky, so it stops nothing and becomes no star.
2. The detector measures the sky and the noise of the plane and smooths it with a Gaussian of about 1.83 pixels.
   Candidates are the points of the smoothed plane above the threshold of equation (1) that are the brightest of their
   box, are backed by enough bright neighbours and are symmetric enough.
3. Each candidate is fitted with a rotated, elliptical Gaussian or Moffat
   [PSF](../../reference/glossary.md#psf), brightest first, until 20,000 stars are accepted. A fit outside the roundness
   or amplitude window, below **Minimum Beta**, wider than the scan's estimate, with too large a residual, or that
   does not settle is not accepted.
4. With **Shaped Areas**, each star that passes equation (2) is painted as a disc of the radius of equation (3) and
   the profile of equation (4). Without it, each star is a solid ellipse. A pixel keeps the largest value any star
   gives it.
5. The mask opens as a new image of one channel. Its own
   [processing state](../../reference/glossary.md#processing-state), not the target's, is recorded as nonlinear with
   the operation `star-mask`. Every sample is stated valid and no linear encoding is claimed. It keeps the target's
   acquisition data, [plate solution](../../reference/glossary.md#plate-solution) and
   [lineage](../../reference/glossary.md#lineage), which gains an entry for the shaped or elliptical painting with the
   [history](../../reference/glossary.md#history) text. It keeps none of the target's
   [header cards](../../reference/glossary.md#header-card): a saved file records only `ANLIN` as `nonlinear`, `ANASRC`
   as `process` and `ANOP` as `star-mask`.

### Model

With \(B\) the median of the plane, \(n\) its noise, measured from the differences between neighbouring pixels of each
row, and \(s\) the value of **Threshold**, a candidate's peak in the smoothed plane must exceed

\[ T = B + 5\,s\,n \tag{1} \]

With \(w_1 \ge w_2\) a star's fitted full widths at half maximum, its roundness \(w_2 / w_1\) must lie between
**Roundness Threshold** and the **Maximum** of **Roundness Range**, which is 1 while that group is unticked. With
\(m_1\) and \(m_2\) the values of **Min Size** and **Max Size**, a disc is painted only when

\[ m_1 \le 2\,w_1 \le m_2 \tag{2} \]

With \(p\) the value of **Percent Increase** and \(q\) that of **Pixel Increase**, its radius is

\[ R = p\,w_1 + q \tag{3} \]

With \(d\) the distance from the star's center to a pixel's center, the six shapes are

\[
\begin{aligned}
\text{Bell} &= e^{-d^2/2R^2} &
\text{Truncated Bell} &= \min\bigl(1,\ e^{1/2}\,e^{-d^2/2R^2}\bigr) \\
\text{Linear} &= \max\bigl(0,\ 1 - \tfrac{d}{3R}\bigr) &
\text{Truncated Linear} &= \min\bigl(1,\ 1.5 \max\bigl(0,\ 1 - \tfrac{d}{3R}\bigr)\bigr) \\
\text{Cubic} &= \max\bigl(0,\ 1 - (\tfrac{d}{3R})^3\bigr) &
\text{Quadratic} &= \max\bigl(0,\ 1 - (\tfrac{d}{3R})^4\bigr)
\end{aligned}
\tag{4}
\]

Each disc is painted over the square that reaches \(3R\) from its center. The four polynomial shapes reach 0 there,
and the two bells are cut at about 0.011 and 0.018. Without **Shaped Areas**, a star becomes an ellipse \(4\,w_1\)
long and \(4\,w_2\) wide, turned to the star's angle, and a pixel is painted 1 when at least half of its area, sampled
at 16 points, lies inside it.

### Boundaries

- The search skips a border of **Radius** pixels, so a star that peaks that close to the edge is not found. Discs and
  ellipses are cut at the edges of the image.
- An image, or a preview area, that is not more than 4 × **Radius** + 8 pixels wide and tall is refused with the
  reason, and nothing is made.
- A raw color-filter mosaic is refused, and so is a run the session's memory budget cannot hold. Neither makes
  anything.
- An image in which no star is found gives a black mask, not an error.
- A floating-point image is read as it is, with 1 as full scale. The detector judges saturation as a share of the
  range from the sky to 1, or to the brightest sample when that is lower, so data scaled far above 1 is judged
  against the wrong range.

### Worked example

The image is the linear Carina [master](../../reference/glossary.md#master), shown in the view's
[Autostretch](../../reference/glossary.md#autostretch) display. Keep the detection at its defaults and set **Percent
Increase** to 1.00. For a star the detector fits 3 pixels wide, equation (3) gives a radius of 3 pixels instead of 1.5.
With **Truncated Bell**, equation (4) keeps the mask at 1 out to 3 pixels from the center, where the star has fallen
to a sixteenth of its peak. The mask is 0.22 at 6 pixels and 0.02 at 9, where the disc ends. At the default 0.50 the
same star is white only out to 1.5 pixels, where it is at half its peak. In the preview, choose **Linear** and untick
**Preview area**. **Apply** opens `RGB - STARMASK`, a grayscale image in 32-bit floating point of 7246 × 5429 pixels.

```figure
images:
  - image: ../../assets/images/processes/star-operations/star-mask-before.webp
    alt: >-
      A crop at 100 % of the middle of the linear master in the Autostretch display, pink clouds of nebula
      beside a dark dust lane and many stars.
    label: Image
  - image: ../../assets/images/processes/star-operations/star-mask-after.webp
    alt: >-
      The same place in the new mask, a soft white disc over each star on black, the nebula and the dust lanes gone.
    label: Mask
caption: >-
  The middle of the linear Carina Nebula master at 100 % in the Autostretch display, and the same place in the mask
  Apply made with Percent Increase at 1.00, shown in the Linear display.
```

## Starting points

| Situation | Start | Then adjust |
| --- | --- | --- |
| A mask that covers whole stars, to protect them | **Percent Increase** at 1.00, the rest at its defaults | **Pixel Increase** by 1 or 2 until the halos are white at 100 %. |
| A dense field over bright nebula | **Threshold** at 2, **Min Size** at 4 | **Roundness Threshold** up in steps of 0.1, until the nebula stays black. |
| Elongated or trailed stars | **Roundness Threshold** at 0.2 | Untick **Shaped Areas** to keep their shape and angle. |
| A hard selection of the stars | **Shaped Areas** unticked | Grow or soften it afterwards with Morphological Transformation. |

## When it goes wrong

!!! warning "Apply makes an image, not an attached mask"
    The new image limits nothing until you attach it to an image as its mask, through **Image > Mask > Select Mask**.
    Check which side is white before you run a process through it.

"the searched rectangle is too small for the search radius"
:   **Cause.** The image, or the preview area, is not more than 4 × **Radius** + 8 pixels wide and tall.
:   **Fix.** Lower **Radius**, enlarge the preview area, or untick **Preview area**. Nothing was made.

"the amplitude window's upper bound is at or above its lower bound", "the largest roundness lies within the unit interval and at or above the smallest" or "the size window is finite, not negative and ordered"
:   **Cause.** A **Maximum** is below **Minimum** or **Roundness Threshold**, or **Max Size** is below **Min Size**.
:   **Fix.** Raise the upper value or lower the other, then preview again.

The mask is black
:   **Cause.** No fit was accepted: **Threshold** is too high, or a window refuses every star.
:   **Fix.** Press **Reset**, untick **Amplitude Range** and **Roundness Range**, and lower **Threshold** step by step.

Noise, hot pixels or knots of nebula are in the mask
:   **Cause.** **Threshold** is low, **Relax PSF Checks** is on, or **Min Size** admits fits narrower than a star.
:   **Fix.** Raise **Threshold**, untick **Relax PSF Checks** and raise **Min Size**, checking at 100 %.

The discs cover only the cores of the stars
:   **Cause.** **Percent Increase** multiplies the star's width, and the default halves it.
:   **Fix.** Raise it toward 1.00, then add **Pixel Increase**.

The brightest stars are missing from the mask
:   **Cause.** They are fitted wider than half of **Max Size**, or their peak is above the **Maximum** of **Amplitude
    Range**.
:   **Fix.** Raise **Max Size**, and untick **Amplitude Range**.

Min Size, Max Size, Percent Increase or Star Shape change nothing
:   **Cause.** **Shaped Areas** is unticked, and solid ellipses ignore them.
:   **Fix.** Tick **Shaped Areas**.

The mask Apply made differs from the preview
:   **Cause.** The preview area is searched as an image of its own, or the **Autostretch** display brightened the
    soft edges.
:   **Fix.** Untick **Preview area** and choose **Linear** in **Display Mode** before you judge the mask.

## Related

- [Masks](../../interface/masks.md), to attach the new mask to an image, enable it, invert it and show it.
- [Morphological Transformation](../masking/morphological-transformation.md), to grow, shrink or soften the mask.
- [Star Profiler](../registration-alignment/star-profiler.md), for a list of stars you can inspect and keep, which
  this window does not read.
- [StarNet++ Star Mask](starnet-star-mask.md), for a star mask made by the StarNet2 tool instead of a detector.
- [Range Selection](../analysis/range-selection.md), for a mask of the image's brightness instead of its stars.
- [Process Windows](../../interface/process-windows.md), for the preview pane and what every process window shares.

## Credits

- The star detector is a derived work of the star finder and point-spread-function fit of
  [Siril](https://gitlab.com/free-astro/siril), by Francois Meyer and team free-astro, under the GNU General Public
  License, version 3 or later, and is distributed under that licence.
  Siril's are the candidate scan and its checks, the saturated plateau, the duplicate radius, and the Gaussian and
  Moffat models with their starting point. The banded scan, the sky put in place of invalid samples and the counted
  refusals are AfterNight's.
- The six **Star Shape** profiles, their reach of three radii and the largest value kept where stars overlap are
  adapted from the star mask of [DeepSkyStacker](https://github.com/deepskystacker/DSS), by Luc Coiffier and the
  DeepSkyStacker contributors, under the BSD 3-Clause License. The radius from the wider width, the
  solid ellipse and its rule for a pixel's area are AfterNight's.
- **About > About AfterNight** credits both works and reproduces the DeepSkyStacker licence's notice.
