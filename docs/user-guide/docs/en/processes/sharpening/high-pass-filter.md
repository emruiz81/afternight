---
kind: process
stage: finish
menu: Process > Details > High Pass
summary: >-
  Replaces the image with its detail layer: the image less a Gaussian blur of the radius you set, centered on middle
  gray, for a blend you make afterwards.
data_in: nonlinear
data_out: nonlinear
destructive: true
preview: real-time
sequences: true
masks: post-blend
external: none
glance:
  enabled: >-
    An image or a sequence view is active, no High Pass Filter window is open on it yet, and no window that needs the
    image for itself is open on it.
  inputs: >-
    One grayscale or RGB image or sequence with finite samples between 0 and 1, and an optional mask of the image's
    size on its view.
  outputs: >-
    The same view, replaced by the detail layer, with one history step. Its size, channels, pixel type, header and
    recorded processing state are kept. No file, preset or report is written.
  state: >-
    A stretched copy of an image after noise reduction, from which you want a detail layer to blend with the image
    afterwards.
derived: false
---
# High Pass Filter

This process replaces the image with its detail: the image less a Gaussian blur of **Radius**, raised to middle gray.
Smooth areas become a flat gray of 0.5, whatever their brightness, and every edge, star and filament stands out
lighter or darker than the gray. It does not sharpen the image itself: there is no amount and no blend back. The
layer is made for a blend you make afterwards. The menu names the process **High Pass**.

**Apply** replaces the image or [sequence](../../reference/glossary.md#sequence) with the layer, keeps its size and
[pixel type](../../reference/glossary.md#pixel-type), and adds one step to its
[history](../../reference/glossary.md#history).

```figure
image: ../../assets/images/processes/sharpening/high-pass-filter.webp
alt: >-
  The High Pass Filter window. On the left, a histogram with one narrow peak in the middle and the lines Clipping Low
  and Clipping High in its corner, a row of toolbar buttons with a Plot resolution box, the Radius box with its
  slider, then Target Image and the Preview check box. On the right, a toolbar of zoom buttons, a channel box, a
  switch, the Preview area check box and a camera button over a gray preview in which the stars and the edges of the
  nebula stand out lighter and darker. Reset, Close and Apply run along the bottom.
caption: >-
  High Pass Filter on the stretched master with Radius at 5.00. The histogram gathers at 0.5 and the preview shows the
  layer Apply writes.
pins:
  - {n: 2, x: 2.0, y: 57.2}
  - {n: 3, x: 40.0, y: 18.0}
  - {n: 4, x: 73.6, y: 19.8}
  - {n: 5, x: 91.0, y: 19.8}
  - {n: 6, x: 91.0, y: 96.0}
```

## Before you start

- Duplicate the image, or plan to keep the layer as a snapshot instead of applying it.
- Reduce the noise and the [hot pixels](../../reference/glossary.md#hot-pixel) left over first. Noise from pixel to
  pixel is detail to this filter, and lands in the layer.
- [Debayer](../../reference/glossary.md#debayer) a [CFA](../../reference/glossary.md#cfa) mosaic first. A raw mosaic is
  filtered as one grayscale plane, and the layer holds the sensor pattern. The frames of a sequence are read with the
  automatic debayer setting of its view.
- The window has no **Channel**: red, green and blue are filtered each on its own. Extract the luminance into an image
  of its own first when you want a layer without color.
- Bring the samples between 0 and 1, and replace any sample that is not a finite number.
- A [mask](../../reference/glossary.md#mask) on the view mixes the layer with the original image. It does not add
  masked detail to the image.

## Steps

1. **Open the window.** Make the image the active one and choose **Process > Details > High Pass**. The preview turns
   gray with the structure of the image in relief, and the histogram gathers in a narrow peak at 0.5.
2. **Set the radius.** Set **Radius** to about the size of the largest structure the layer should hold. Lower keeps
   only the finest detail, higher keeps broader structure too.
3. **Read the histogram.** Check **Clipping Low** and **Clipping High** in its corner: they give the share of the
   layer cut at black or at white, where the detail was stronger than the gray can hold.
4. **Check at 100 %.** Zoom the preview into the stars and the background, and hold ++space++ to compare with the
   original. Tick [**Preview area**](../../reference/glossary.md#preview-area) to work on a part of the image while
   you adjust.
5. **Keep the original, if you wish.** Choose **Take snapshot** in the preview's toolbar to open the layer as a new
   image and leave the target as it is.
6. **Apply.** Or choose **Apply**. The view shows the gray layer, its history lists **High Pass Filter** with the
   radius, and **Radius** returns to its default.

## Controls

The histogram and its toolbar, **Target Image**, **Preview**, the preview pane, **Close** and the progress window of a
sequence work as in every process window: see [Process Windows](../../interface/process-windows.md). What is
particular to this window is below.

```controls
window: High Pass Filter
controls:
  - name: Radius
    kind: spinbox
    default: "10.00"
    widget: {min: 0.1, max: 1000, step: 0.5, decimals: 2}
    effect: Sets the standard deviation, in pixels, of the Gaussian blur taken away. Higher leaves broader structure in the layer, lower only the finest detail. At the lowest values the blur hardly changes the image, and the layer is almost a uniform gray.
    range: It is a standard deviation, although the label says Radius. The slider moves the value in 1,000 equal steps of about 1.
    enabled: Disabled while Apply runs.
    tradeoff: >-
      The blur reaches four times **Radius** on each side, so its cost grows with it. Up to 32 one kernel does it.
      Beyond, one wide kernel does it, as far as 1,024, when the processor has AVX2 and the image's shorter side is
      more than twice the reach. Otherwise several narrower passes add up to the same blur, apart from the small
      tail each pass leaves out, more of them the larger the value, and Apply is slow.
    scope: The preview and Apply, on every channel of the image, before the mask is blended.
    persistence: Until the window closes. **Reset** and a finished Apply return it to its default.
    note: >-
      While you drag the slider, the preview works on the image reduced to the pane's size, with the radius reduced by
      the same factor, or on the part shown at 100 % and closer. Above 32, a dragged preview blurs with three box
      passes, whose cost does not grow with the radius and which stay within a few parts in a thousand of the
      Gaussian. The reduced result comes close to a reduction of the layer but is not one, most visibly around bright
      stars, because the layer is clipped after the blur. When you let go, the preview works on the whole image, as
      Apply does. A typed value is shown the first way at once, and on the whole image a moment later.
  - name: Target Image
    kind: combo
    default: The active image
    options: [The active image, Every other open image or sequence the window can work on]
    effect: Chooses the image or sequence Apply replaces, with the mask on its view. The histogram and the preview follow it.
    persistence: Until the window closes. **Reset** leaves it as it is.
  - name: Preview
    kind: check
    default: "On"
    effect: Shows the layer beside the controls, with the mask blended in. Off removes the pane, and the histogram counts the image as it is.
    range: >-
      The pane opens on the whole image. **Preview area** starts off. Ticked, it is 600 by 600 pixels in the middle
      of the image, or the whole image when it is smaller, and is labelled "Preview area".
    tradeoff: The area shortens each preview but can hide broad structure that the layer holds at a large radius.
    scope: The window only. The pane has no **Display Mode** and shows the samples as they are.
    persistence: Until the window closes. **Reset** leaves it as it is.
  - name: Reset
    kind: button
    default: ""
    effect: Returns **Radius** to its default. With **Preview** off, the histogram is counted again from the image.
    enabled: Disabled while Apply runs.
  - name: Apply
    kind: button
    default: ""
    effect: Replaces the whole image, or every frame of the sequence, with its layer at **Radius** as it is when you press it, and then returns **Radius** to its default.
    enabled: Whatever the value. Disabled while a run works, as **Radius**, **Target Image**, **Reset** and **Close** are.
    tradeoff: A single image shows no progress window, nor does a snapshot, and its run cannot be cancelled once Apply starts. On a sequence, **Cancel** in the progress window is noticed by the blur between two rows. A run the memory limit cannot hold is refused with "An error occurred when running the process:" and the reason, and nothing is written.
    persistence: "The history step is **High Pass Filter** and reads, for example, `Apply high pass filter with radius 5`. It does not record the gray level of 0.5, the mask or how many passes the blur took. The header cards, the other metadata and the recorded processing state are kept, so the state describes the image the layer came from."
```

## How it works

1. Every channel is read as a [fraction of full scale](../../reference/glossary.md#fraction-of-full-scale) and
   blurred with a Gaussian whose standard deviation is **Radius**, along the rows and then down the columns. Its taps
   are the Gaussian at each pixel's centre, out to four times the radius and scaled to add up to 1. Beyond the edge
   of the image the blur reads the image mirrored, without repeating the edge sample.
2. The blur is subtracted from the image, 0.5 is added, and the result is held to 0 to 1.
3. An image whose samples are all valid is worked on a single-precision copy, blurred by the same kernels as Gaussian
   Blur, and converted back once, rounded to the nearest code for an integer image. An image that marks some samples
   as invalid is filtered in its stored units up to a **Radius** of 32, the blur summed in double precision and
   reading a stand-in where a neighbour is invalid, and on the copy beyond.
4. A mask on the view then blends the layer with the original.
5. The layer replaces the target and its history gets one step. Its header and its recorded
   [processing state](../../reference/glossary.md#processing-state) are kept.

### Model

With \(I\) a sample and \(G_r * I\) the Gaussian blur of radius \(r\) at that sample, the layer is

\[ H = \min\bigl(1,\ \max\bigl(0,\ I - G_r * I + 0.5\bigr)\bigr) \tag{1} \]

On a smooth area the blur equals the image, so the layer is 0.5 whatever the level. Where the image is brighter
than its blur the layer rises above gray, and where it is darker it falls below, so a hard edge gives a light and a
dark band side by side. Detail stronger than 0.5 either way is cut. **Radius** decides what the blur takes away, not
how strong the layer is.

With \(m\) the weight of a mask at a pixel, the result is

\[ O = (1 - m)\,I + m\,H \tag{2} \]

which mixes the image with the gray layer. With the image at 0.20, the layer at 0.70 and a mask of 0.25, the result
is 0.325, not 0.20 plus a quarter of the detail.

### Boundaries

- At the lowest **Radius** the layer is almost a uniform 0.5. The minimum gives neither the image nor its detail.
- A sample outside 0 to 1, or detail stronger than 0.5, is cut. A sample that is not a finite number spreads into
  its neighbours through the blur.
- Near the edges of the image, a large radius reads mirrored structure from inside it.
- A 64-bit floating-point image is computed in single precision. In an integer image the gray of 0.5 is rounded to
  the nearest code.
- With a mask, a hard mask edge shows as a sudden change between the image and the gray layer.

### Worked example

The image is the linear [master](../../reference/glossary.md#master) after Histogram Transformation with **Shadows**
and **Midtones** at 0.02, which leaves the background near 0.28. With **Radius** at 5.00, equation (1) turns the
smooth background into 0.5, with its grain around it. A sample of 0.80 at the edge of a bright filament, whose blur is
0.60, becomes 0.70. One of 0.40 under the same blur becomes 0.30. A star core of 0.98 over a blur of 0.35 would reach
1.13 and is held at 1, which **Clipping High** counts.

```figure
images:
  - image: ../../assets/images/processes/sharpening/high-pass-filter-before.webp
    alt: A crop of the stretched nebula at 100 percent, bright clouds beside dark dust lanes and many small stars on a gray background.
    label: Before
  - image: ../../assets/images/processes/sharpening/high-pass-filter-after.webp
    alt: The same crop as a flat middle gray in which the stars stand out white with dark rings and the edges of the clouds and dust lanes show as light and dark lines.
    label: After
caption: >-
  The middle of the stretched Carina Nebula master at 100 % before and after Apply with Radius at 5.00. Both are shown
  in the Linear display mode, without a display stretch.
```

## Starting points

| Situation | Start | Then adjust |
| --- | --- | --- |
| A detail layer to blend with the image | **Radius** at 10, with **Take snapshot** | Lower **Radius** while you watch at 100 % until only the detail you want is left. |
| The layer is cut at black or white | A smaller **Radius** | Tame the strongest edges of the image before you filter it. |
| Colored edges in the layer | A luminance image of its own | Check the channels with **View Channel** in the preview's toolbar. |
| A layer for one region | The layer without a mask | Blend it into that region afterwards, through the mask, with the tool that blends it. |

## When it goes wrong

!!! warning "Apply replaces the image with a gray layer"
    The view no longer shows the image, only its detail around gray, and its recorded state still describes the
    image. Keep a duplicate, or use **Take snapshot**, and undo the step in the image's history if you applied by
    mistake.

The result is gray instead of sharpened
:   **Cause.** The output is the detail around 0.5, not the image with its detail raised.
:   **Fix.** Undo the step, keep the layer as a snapshot, and lay it over the image with a blend mode.

The layer is an almost uniform gray
:   **Cause.** **Radius** is so small that the blur barely changes the image.
:   **Fix.** Raise **Radius**.

Bright and dark rings surround the stars
:   **Cause.** A detail layer has a light and a dark band at every edge, and a larger radius widens them.
:   **Fix.** Lower **Radius**, and check the rings before you blend the layer.

The layer has colored edges
:   **Cause.** Red, green and blue are filtered each on its own, and the window has no **Channel**.
:   **Fix.** Filter a luminance image of its own instead.

The histogram reaches black or white
:   **Cause.** The detail was stronger than 0.5 either way and was cut.
:   **Fix.** Lower **Radius**, or tame the strongest edges before you filter the image.

Part of the result looks normal and part gray
:   **Cause.** A mask mixes the image with the layer.
:   **Fix.** Make the layer without the mask, and blend it afterwards.

The preview looks different from the image in its view
:   **Cause.** The pane shows the samples as they are, and the view may show the image in
    [Autostretch](../../reference/glossary.md#autostretch).
:   **Fix.** Compare with the view in its Linear display mode.

Apply, or the preview after you let go of the slider, is slow at a large radius
:   **Cause.** The blur reaches four times **Radius**, and beyond 32 it may take several passes.
:   **Fix.** Lower **Radius**, or draw a **Preview area** while you adjust. A dragged preview shows a faster
    approximation.

## Related

- [Wavelet Denoise](../noise-reduction/wavelet-denoise.md) and the other noise reduction processes, which belong
  before the layer is made.
- [Image Blend](../analysis/image-blend.md), to lay the layer over the image with a blend mode such as Overlay, Soft
  Light or Linear Light.
- [Unsharp Mask](unsharp-mask.md) and [Sharpen Alpha](sharpen-alpha.md), to sharpen the image itself.
- [Gaussian Blur](../noise-reduction/gaussian-blur.md), the blur this process takes away, by the same kernels.
- [Image History](../../interface/image-history.md) and [Saving and Exporting](../../interface/saving-and-exporting.md),
  to undo the step and to keep the layer as a file of its own.
- [Process Windows](../../interface/process-windows.md), for the histogram, the preview and what every process window
  shares.

## Credits

- [Peter Kovesi, "Fast Almost-Gaussian Filtering"](https://www.peterkovesi.com/papers/FastGaussianSmoothing.pdf), the
  repeated-box approximation of a Gaussian that a dragged preview uses above a **Radius** of 32.
