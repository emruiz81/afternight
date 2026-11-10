---
kind: process
stage: finish
menu: Process > Intensity Transformations > Curves Transformation
summary: >-
  Reshapes tone and color through a curve you draw with control points, usually after the main stretch.
data_in: nonlinear
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
  inputs: >-
    One grayscale or RGB image or sequence with samples between 0 and 1, and an optional mask of the image's size
    on its view.
  outputs: >-
    The same view, replaced, with one history step. Its size and pixel type are kept. No file, preset or curve is
    written. A mosaic sequence decoded with automatic debayering is replaced by an RGB one.
  state: >-
    A stretched image, mono or RGB, ready for its contrast, saturation or color balance to be finished. A gentle
    first stretch is possible, but nothing here is derived from the image's statistics.
derived: false
rewritten: true
---
# Curves Transformation

Curves Transformation remaps an image through a curve you draw. The curve runs from input brightness, left to
right, to output brightness, bottom to top. Where it lies above the diagonal the image gets brighter, and where it
lies below, darker. It acts on every channel, on one channel, or on one component of a color space, such as
lightness or saturation, after the main [stretch](../../reference/glossary.md#stretch).

**Apply** replaces the image or [sequence](../../reference/glossary.md#sequence) with the curved one, keeps its size
and [pixel type](../../reference/glossary.md#pixel-type), and adds one step to its
[history](../../reference/glossary.md#history). The result is recorded as nonlinear.

```figure
image: ../../assets/images/processes/stretching/curves-transformation.webp
alt: >-
  The Curves Transformation window. On the left, the curve editor: a white curve that sags below a dotted diagonal
  through one point, over a grid and the three colored lines of a histogram with its peak near the left edge, and
  two lines of clipping percentages in its top left corner. Under it a row of toolbar buttons, the Interpolation
  box, Target Image, Channel and Preview. On the right, the preview shows a red nebula on a dark background. Reset,
  Close and Apply run along the bottom.
caption: >-
  Curves Transformation on a stretched master with one point pulled down from 0.28 to 0.14. The histogram behind
  the curve is the result's, and the preview shows what Apply writes.
pins:
  - {n: 2, x: 10.4, y: 84.3}
  - {n: 3, x: 3.6, y: 59.6}
  - {n: 4, x: 18.0, y: 44.7}
  - {n: 6, x: 64.0, y: 54.0}
  - {n: 7, x: 90.4, y: 96.0}
```

## Before you start

- Finish background correction and color calibration while the image is still linear. A curve makes faint
  gradients and color casts harder to correct afterwards.
- Make the main stretch first. Curves derives no [black point](../../reference/glossary.md#black-point), median or
  highlight protection from the image.
- **Apply** replaces the image. Its history keeps the step before, but not the points of the curve: duplicate the
  image when you must be able to return.
- The image is grayscale or RGB, with samples between 0 and 1. A floating-point sample outside that range takes
  the curve's value at the nearer end.
- A raw color-filter mosaic is curved as a grayscale image: [debayer](../../reference/glossary.md#debayer) it first. On a sequence, the view's automatic
  debayer option decides: with it on, every frame is decoded to RGB before the curve and the sequence is replaced
  by an RGB one.
- A [mask](../../reference/glossary.md#mask) on the view blends the curved image with the original after the curve.
  White takes the result, black keeps the original, gray mixes them, and **Invert Mask** swaps the two.

## Steps

1. **Open the window.** Make the stretched image active and choose **Process > Intensity Transformations > Curves
   Transformation**. The editor shows a straight line from corner to corner over the image's histogram, and the
   preview shows the image unchanged.
2. **Choose the channel.** Leave **Channel** at **RGB** to send the three channels through one curve. Choose a
   lightness or value component to change tone with less effect on color.
3. **Choose the interpolation.** Leave **Interpolation** at **Cubic** for a smooth curve. Choose **Steffen** or
   **Monotone Cubic** when the curve must not swing past its points.
4. **Add a point and drag it.** Press the left button on the line, where the pointer turns into a hand, and drag.
   Up brightens that range of inputs, down darkens it, and the curve and the preview follow.
5. **Anchor what must stay.** Click the line on each side of the point to pin the ranges you want to protect. The
   dotted diagonal shows where the image is unchanged, and the histogram where its samples lie. A right click on a
   point removes it.
6. **Check the preview at 100 %.** Zoom the preview into the shadows, the transitions and the brightest stars, and
   press ++space++ over it to compare with the original. Read **Clipping Low** and **Clipping High** in the editor
   as a first warning.
7. **Apply.** Choose **Apply**. The image in its view is the curved one, its history lists **Curves
   Transformation** with the channel and the interpolation, and the editor returns to a straight line.

## Controls

The zoom, grid and area buttons under the editor change only how it is drawn, and are the same in every window
that has them: see [Process Windows](../../interface/process-windows.md).

```controls
window: The curve editor
controls:
  - name: Curve
    kind: readout
    default: ""
    effect: Shows the curve as a white line with a dot on each point. Input runs along the bottom and output up the side, both from 0 to 1, over a grid of eight by eight squares.
    range: It opens as the straight line between two points, at input 0 and output 0 and at input 1 and output 1, which changes nothing.
    enabled: Always shown.
    tradeoff: The curve is drawn as you set it, without the mask.
    scope: The curve of the choice in **Channel**, for the preview and Apply, on an image or a sequence.
    persistence: Until **Reset** or a finished Apply returns the straight line. Closing the window discards it.
    note: The window has no list of points, no box to type a point's values, no preset, no curve file and no undo. Turning the wheel with ++ctrl++ held zooms the editor around the pointer, by 1.2 or 0.8 for each notch, and dragging with the middle button pans it once it is enlarged. Neither changes what is computed.
  - name: Points
    kind: slider
    default: The two end points
    widget: {min: 0, max: 1}
    effect: A press of the left button on a point takes hold of it, and a press within about 5 pixels of the line adds a point where you pressed and takes hold of that. Dragging moves it. Up brightens the inputs around the point, down darkens them, and sideways changes which inputs it acts on.
    range: The two end points move up and down only. A point between them stays between its neighbors, at least 0.0001 from each, and every output stays between 0 and 1. A right click removes a point between the ends at once. The end points cannot be removed.
    enabled: The pointer turns into a hand where a press takes or adds a point. The editor takes no press while Apply runs.
    tradeoff: Points close together, or far from the diagonal, give abrupt contrast, more visible noise or a curve that swings past them. Removing a point widens the reach of its neighbors.
    scope: While you hold a point, the preview is computed from what the preview pane shows, which is the image reduced to the pane's size with each pixel the mean of its block, or the part of it shown at 1:1 and closer. When you let go, or hold the point still, the whole image follows.
    persistence: Until **Reset** or a finished Apply.
    note: A press a few pixels off the line already bends the curve a little, because the point is placed where you pressed.
  - name: Histogram
    kind: readout
    default: ""
    effect: Shows, behind the curve, the counts of the image the preview shows. That is the result of the curve, or the original while the preview shows the original.
    range: 256 bins, one line for a grayscale image and three for RGB. A sample at full scale has the last bin to itself.
    enabled: With **Show histogram** and **Preview** on.
    tradeoff: It is counted again after each preview of the whole image, and keeps its last counts while you hold a point. It counts the preview with the mask blended in, so it is a guide and not a measurement of the result at full precision.
    scope: The window only. Its lines follow the channel the preview displays, not **Channel**. On a sequence it counts the frame the preview shows.
  - name: Clipping Low and Clipping High
    kind: readout
    default: ""
    effect: Give the share of samples at or below 0 and at or above 1 in the image the histogram counts, in percent with three decimals.
    range: The counts of all channels together. A value just inside 0 or 1 is not counted.
    enabled: Always shown, in the editor's top left corner.
    tradeoff: The two numbers are counted with the histogram. With **Show histogram** or **Preview** off they keep their last values.
    scope: The window only, with the mask blended in.
    note: For exact counts, use Image Statistics after Apply.
  - name: Coordinates
    kind: readout
    default: ""
    effect: Shows the input under the pointer and the height of the histogram there, while you hold the left button away from the line and the points.
    range: Three decimals. The height is a share of the tallest bin displayed, one value for a grayscale image and one for each displayed channel of RGB. It is not the value of a point, and no point can be set from it.
    enabled: With the histogram shown. It does not appear while you take or drag a point.
    persistence: Until you release the button.
  - name: Gradient strip
    kind: readout
    default: ""
    effect: Shows the direction of the input axis under the plot, from black on the left to white on the right.
    range: While the preview displays the red, green or blue channel alone, the strip ends in that color.
    enabled: Always shown.
```

```controls
window: Curves Transformation
controls:
  - name: Show guide
    kind: toolbutton
    default: "On"
    effect: Shows or hides the dotted diagonal, which is the curve of an unchanged image. A curve above it brightens and one below it darkens.
    scope: The editor's drawing only.
    persistence: Until the window closes. **Reset** leaves it as it is.
  - name: Show histogram
    kind: toolbutton
    default: "On"
    effect: Shows or hides the histogram behind the curve. Off also disables **Draw histogram areas**, which keeps its state.
    enabled: Always. The histogram itself needs **Preview** on.
    scope: The editor's drawing only.
    persistence: Until the window closes. **Reset** leaves it as it is.
  - name: Interpolation
    kind: combo
    default: Cubic
    options: [Linear, Cubic, Akima, Steffen, Monotone Cubic]
    effect: Chooses how the curve passes between the same points. **Linear** joins them with straight segments. **Cubic** is the smoothest. **Akima** keeps a bend from spreading past its neighbors. **Steffen** and **Monotone Cubic** never turn between two points.
    range: Two points always give a straight line. With fewer than five points, **Akima** and **Steffen** draw the **Cubic** curve.
    enabled: Disabled while Apply runs.
    tradeoff: "**Cubic** and **Akima** can swing past their points. What leaves the range 0 to 1 is cut to black or white, which does not stop the curve from turning between points. **Linear** shows a corner at each point."
    scope: The preview and Apply, for every choice of **Channel**, on an image or a sequence.
    persistence: Until the window closes. **Reset** and Apply keep the choice and build the new straight line with it. A new window opens with **Cubic**.
  - name: Target Image
    kind: combo
    default: The active image
    options: [The active image, Every other open image or sequence the window can work on]
    effect: Chooses the image Apply replaces. The choices of **Channel**, the preview and the histogram follow it.
    tradeoff: A larger image or a sequence takes more time, memory and disk.
    persistence: Until the window closes. **Reset** leaves it as it is.
  - name: Channel
    kind: combo
    default: RGB
    options: [RGB, Red, Green, Blue, CIE L* (Lightness), CIE a*, CIE b*, CIE c*, CIE h*, HSL Hue, HSL Saturation, HSL Lightness, HSV Hue, HSV Saturation, HSV Value, YCbCr Luma, YCbCr Blue Chroma, YCbCr Red Chroma]
    effect: Chooses what goes through the curve. **RGB** sends each of the three channels through it on its own. A color sends that channel alone and leaves the other two as they are. Every other choice converts the image to that color space, sends the one component through the curve and converts back.
    range: Every component is scaled to the range 0 to 1. CIE lightness runs from 0 to 100, chroma from 0 to 200 and the CIE hue from -180 to 180 degrees. The CIE a and b components and the two YCbCr chroma components are signed, with neutral near 0.5. HSL and HSV hue is a share of a full turn. An image whose samples state no full scale, as files are read, is converted as fractions of full scale.
    enabled: An RGB image. A grayscale image shows a disabled **Grayscale** instead. Disabled while Apply runs.
    tradeoff: One curve on **RGB** does not keep the ratios between the channels, so it changes saturation. A component costs two conversions of the whole image and a working copy of it, in 32-bit floating point, or 64-bit for a 64-bit image. An integer image is rounded once on the way back, so prefer floating point for strong component curves.
    scope: The preview and Apply, on an image or a sequence, before the mask is blended. It does not choose the lines of the histogram.
    persistence: Until the window closes. **Reset** and Apply return it to **RGB**. A grayscale image keeps **Grayscale**.
    note: Hue is treated as a line from 0 to 1 and not as a circle. A tone curve is rarely right for the signed components, where the middle of the range means no color. The conversion back is the exact inverse of the conversion there, so the two components left alone return the colors they came from.
  - name: Preview
    kind: check
    default: "On"
    effect: Shows the curved image beside the controls, with the mask blended in. Off removes it, and the editor's histogram with it.
    tradeoff: Each change computes the image again and keeps a second copy of it. While you hold a point only what the pane shows is computed. The whole image, its conversions and the histogram follow when you let go, which takes longer on a large image. Turn it off while you place many points on a large image.
    scope: The window only. The image is not changed. On a sequence it shows the frame the view shows.
    persistence: Until the window closes. **Reset** leaves it as it is.
    note: At rest the preview is what Apply writes, and a **Preview area** shows exactly what Apply writes there.
  - name: Reset
    kind: button
    default: ""
    effect: Returns the curve to the straight line, **Channel** to **RGB** and the editor's zoom to fit.
    range: It leaves **Interpolation**, the target, **Preview** and the toolbar's buttons as they are, and builds the straight line with the interpolation shown.
    scope: The window only. The image is not changed, and Apply is disabled afterwards.
  - name: Apply
    kind: button
    default: ""
    effect: Sends the image, or every frame of the sequence, through the curve, and then returns the curve, **Channel** and the editor's zoom as **Reset** does.
    enabled: When the curve is anything but the straight line between its two end points. It is disabled while the window opens.
    range: An unsigned 8- or 16-bit image gets one table entry for each of its codes, held in single precision.
    tradeoff: The work grows with the number of samples and runs on several threads. A component choice adds two conversions, and a sequence is written again in a temporary place.
    scope: The whole image or sequence, on the choice in **Channel**, with the mask blended afterwards.
    persistence: The image and its history keep the result. The history and the image's lineage record the texts of **Channel** and **Interpolation**, and not the points, the toolbar's buttons or the mask.
    note: While it runs, the editor, **Interpolation**, **Channel**, **Reset** and **Close** are disabled. Closing the window instead discards the curve. Nothing of the window is saved between sessions.
```

```controls
window: Sequence progress
controls:
  - name: Progress
    kind: readout
    default: ""
    effect: Shows the frame being curved, from 0 to 100 percent. Every frame gets the same curve, interpolation and channel.
    enabled: Shown only while Apply works on a sequence.
    tradeoff: The sequence is written again in a temporary place, which needs the disk space of a second copy. It replaces the original only when every frame succeeded.
  - name: Cancel
    kind: button
    default: ""
    effect: Asks the run to stop. The temporary sequence is removed and the original stays as it was.
    range: The frame being worked on may finish before the request is noticed.
    note: A run that fails leaves the original in the same way. Run it again with the final curve, and keep enough free disk space.
```

## How it works

1. The editor keeps the points in order of input, with the ends at 0 and 1 and every output between 0 and 1.
   After each change it builds the curve through them with the interpolation in force.
2. The preview and Apply turn the curve into the same table. An unsigned 8- or 16-bit image gets the nearest
   value to the curve for each of its codes. A floating-point image is read through 8,193 entries, joined by
   straight lines.
3. **RGB**, **Grayscale** and a single color send the samples through the table directly. A component choice
   converts the image to that color space, sends the one component through and converts back.
4. A mask on the view then blends the result with the original.
5. The image replaces the target and its history gets one step. Its header, its acquisition data and its
   [plate solution](../../reference/glossary.md#plate-solution) are kept. Its
   [processing state](../../reference/glossary.md#processing-state) is recorded as nonlinear with the operation
   `curves-transformation`, and no transfer function is stated.

### Model

The points are \((x_0, y_0), \ldots, (x_n, y_n)\), with their inputs in increasing order from \(x_0 = 0\) to
\(x_n = 1\). The interpolation builds a continuous function \(S\) that passes through every point, and a sample
\(x\) becomes

\[ y(x) = \min\bigl(1, \max\bigl(0, S(x)\bigr)\bigr), \qquad S(x_i) = y_i \tag{1} \]

A sample equal to a point's input therefore gets that point's output, whatever the interpolation. Between the
points the interpolation decides. **Cubic** is the natural cubic [spline](../../reference/glossary.md#spline), whose
curvature is zero at both ends.

With **RGB**, equation (1) is applied to the red, green and blue samples \(R\), \(G\) and \(B\) of a pixel
separately:

\[ (R', G', B') = \bigl(y(R),\ y(G),\ y(B)\bigr) \tag{2} \]

Unless the curve is a straight line through the origin, the ratios between the three change, and with them the
saturation.

With a component choice, \(C\) is the conversion to the color space, \(C^{-1}\) the conversion back and \(c_k\) the
chosen one of its three components:

\[ (c_0, c_1, c_2) = C(R, G, B), \qquad c'_k = y(c_k), \qquad (R', G', B') = C^{-1}(c_0, \ldots, c'_k, \ldots, c_2) \tag{3} \]

### Boundaries

- **Apply** is enabled for any curve but the straight line between the two end points. A point added on that line
  enables it, and Apply then writes a history step for an image that looks unchanged.
- A **Cubic** or **Akima** curve can leave the range 0 to 1 between two points. Equation (1) cuts it there, so that
  stretch of the curve is flat black or white.
- After a component choice, a floating-point image keeps the values the conversion back puts outside 0 to 1. An
  integer image stops at the ends of its range, rounded to the nearest code.
- A signed or 32-bit integer image is read as
  [fractions of full scale](../../reference/glossary.md#fraction-of-full-scale), curved through the floating-point
  table and rounded back. A 64-bit floating-point image is computed in double precision. For a smooth curve the table is
  within a millionth of the curve.
- A sample that is not a number stays one, without a message. Pixels the image marks as invalid are left
  unchanged.

### Worked example

The [master](../../reference/glossary.md#master) in the picture above was stretched with Histogram Transformation,
**Shadows** and [**Midtones**](../../reference/glossary.md#midtones) at 0.02,
which leaves its background near 0.28. With **Channel** at **RGB** and **Interpolation** at **Cubic**, one point is
taken on the line at input 0.28, over the peak of the histogram, and dragged straight down to 0.14. By
equation (1) the background becomes 0.14, half as bright, and black and white stay where they were. The cubic
curve through the three points takes a sample of 0.50 to 0.34, one of 0.75 to 0.65 and one of 0.90 to 0.86. The
peak of the histogram in the editor moves to 0.14.

```figure
images:
  - image: ../../assets/images/processes/stretching/curves-transformation-before.webp
    alt: The whole frame before the curve, the red and white clouds of the nebula across a field of stars on a gray background.
    label: Before
  - image: ../../assets/images/processes/stretching/curves-transformation-after.webp
    alt: The whole frame after the curve, the same nebula on a background half as bright, its faint outer parts darker and its bright core and the stars nearly as before.
    label: After
caption: >-
  The stretched Carina Nebula master before and after Apply with one point taking 0.28 to 0.14. Both are the whole
  frame as the view shows it in the Linear display mode, without a display stretch.
```

A lightness curve works the same way through equation (3). With **Channel** at **CIE L\* (Lightness)** and
**Interpolation** at **Steffen**, add three points: one low and one high on the line as anchors, and one between
them slightly above it. With the two ends that makes five points, so the Steffen rule is in force and the curve
rises all the way. Only the lightness of each pixel goes through the curve.

## Starting points

| Situation | Start | Then adjust |
| --- | --- | --- |
| More contrast in one range of tones | **RGB** and **Cubic**, one gentle point, then an anchor on each side | Move the main point. Flat or steep transitions, crushed ends and more noise mean too much. |
| Contrast without a change of color | **CIE L\* (Lightness)** or [**HSV Value**](../../reference/glossary.md#hsv), with a gentle curve | Check strongly colored regions, where the conversion back can leave the range. |
| More or less color | [**HSL Saturation**](../../reference/glossary.md#hsl) or **CIE c\***, with broad, small moves | Stop at color noise or neon colors. Use a hue curve only to move a color on purpose. |
| A curve through a mask | A gentle curve first, with the mask off | Enable the mask and judge the blended preview. Soften a visible edge in the mask, not in the curve. |

## When it goes wrong

!!! warning "Apply cannot be taken back from this window, and the curve is not kept"
    Apply replaces the image and returns the editor to a straight line. The history records the channel and the
    interpolation only, so the curve cannot be drawn again from it. Duplicate the image first, or note where the
    points were.

Apply is disabled
:   **Cause.** The curve is the straight line between its two end points, which changes nothing.
:   **Fix.** Add a point or move an end point.

Akima or Steffen draws the same curve as Cubic
:   **Cause.** With fewer than five points both draw the **Cubic** curve, and two points always give a straight line.
:   **Fix.** Add well-spaced points, or choose **Monotone Cubic**, which is in force from three points.

The curve swings past its points, or lies flat against black or white
:   **Cause.** A **Cubic** or **Akima** curve left the range between two points and was cut there.
:   **Fix.** Remove points that lie close together, bend the curve less, or choose **Steffen**, **Monotone Cubic** or
    **Linear**.

Strong colors shift after a curve on RGB
:   **Cause.** Each channel goes through the curve on its own, which changes the ratios between them and can push
    one channel to its limit.
:   **Fix.** Bend the curve less, or use a lightness or value component.

A hue curve breaks one color abruptly
:   **Cause.** Hue is treated as a line from 0 to 1, although the colors at its two ends are neighbors.
:   **Fix.** Keep hue curves away from the two ends, or use a tool made for one range of colors.

The preview and the applied image differ
:   **Cause.** The preview was still on its way, a point was still held and the preview showed the reduced image,
    or the mask, the target or the display changed in between.
:   **Fix.** Let the point go, wait for the preview, and compare under the same mask and display.

After Apply the curve is straight again and Channel shows RGB
:   **Cause.** A finished Apply returns the curve, **Channel** and the editor's zoom to their defaults.
:   **Fix.** Draw the next curve from the result, and choose the channel again.

A mosaic sequence is RGB after Apply
:   **Cause.** Automatic debayering was on for the sequence view, so each frame was decoded to RGB before the curve.
:   **Fix.** Turn automatic debayering off when the frames must stay mosaics.

## Related

- [Background Neutralization](../color-calibration/background-neutralization.md),
  [Photometric Color Calibration](../color-calibration/photometric-cc.md) and
  [Spectrophotometric Color Calibration](../color-calibration/spectrophotometric-cc.md), which need the image
  linear and come before.
- [Histogram Transformation](histogram-transformation.md),
  [Generalized Hyperbolic Transformation](gh-transformation.md) and [ArcSinh Stretch](arcsinh-stretch.md), for
  the main stretch.
- [Color Saturation](../color-adjustment/color-saturation.md), to finish the colors before noise reduction and
  sharpening.
- [Image Statistics](../../interface/image-statistics.md), for exact counts of black and white samples after
  Apply.
- [Image History](../../interface/image-history.md), to return to the image before the curve.
- [Process Windows](../../interface/process-windows.md), for the preview, the editor's toolbar and what every
  process window shares.

## Credits

- The [GNU Scientific Library's interpolation reference](https://www.gnu.org/software/gsl/doc/html/interp.html)
  defines the linear, natural cubic, Akima and Steffen rules, and the number of points each needs, that
  **Linear**, **Cubic**, **Akima** and **Steffen** follow. AfterNight computes the rules itself and does not use
  the library for them.
- F. N. Fritsch and J. Butland,
  [A method for constructing local monotone piecewise cubic interpolants](https://doi.org/10.1137/0905021),
  SIAM Journal on Scientific and Statistical Computing 5 (1984), give the rule **Monotone Cubic** follows.
