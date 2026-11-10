---
kind: process
stage: any
menu: Process > Intensity Transformations > Binarize
summary: >-
  Sets every sample to the highest or the lowest value of its pixel type, by whether it reaches a threshold, to turn
  a soft mask or an image into a hard black and white one.
data_in: either
data_out: nonlinear
destructive: true
preview: real-time
sequences: true
masks: post-blend
external: none
glance:
  enabled: >-
    An image or a sequence view is active, no Binarize window is open on it yet, and no window that needs the image
    for itself is open on it.
  inputs: >-
    One grayscale, RGB or single-plane mosaic image or sequence of any pixel type, and an optional mask of the image's
    size on its view.
  outputs: >-
    The same view, replaced, with one history step. Its size, channels and pixel type are kept, and every sample holds
    the smallest or the largest value of its type. No file is written.
  state: >-
    A duplicate, a mask or a classification image whose cutoff you know. The threshold works the same on linear and
    stretched data.
derived: false
---
# Binarize

Binarize cuts every sample to one of two values at a threshold. A sample at or above it becomes the largest value of
its [pixel type](../../reference/glossary.md#pixel-type), every other sample the smallest: 1 and 0 in floating point.
One threshold serves every channel, or red, green and blue each get their own. Use it to turn a soft
[mask](../../reference/glossary.md#mask) into a hard one, or to sort samples into two classes without a transition.

**Apply** replaces the image or [sequence](../../reference/glossary.md#sequence) with the binary one, keeps its size,
channels and pixel type, and adds one step to its [history](../../reference/glossary.md#history). It opens no new
image. The result is recorded as nonlinear.

```figure
image: ../../assets/images/processes/masking/binarize.webp
alt: >-
  The Binarize window. On the left, the RGB/K box at 0.500000 with its slider, the Green and Blue boxes grayed out at
  the same value, Selection at Joint RGB/K channels, then Target Image and the Preview box, ticked. On the right, a
  toolbar over the preview of the nebula cut to pure colors, white and red clouds and white stars on a black
  background. Reset, Close and Apply run along the bottom.
caption: >-
  Binarize on the stretched Carina master with RGB/K at 0.50 in the joint mode. Each channel is cut on its own, so the
  red nebula stays pure red and only what is bright in all three channels turns white.
pins:
  - {n: 2, x: 2.0, y: 35.1}
  - {n: 3, x: 2.0, y: 20.1}
  - {n: 4, x: 2.0, y: 25.1}
  - {n: 6, x: 90.0, y: 96.0}
```

## Before you start

- Work on a duplicate or on a mask image. The gray levels are lost on **Apply**, and only the image's history brings
  them back.
- Each channel is cut on its own, and an RGB image stays RGB. For a one-channel mask, use **Convert to Grayscale** on
  a duplicate first.
- Turn off the mask on the view for a result that is black and white everywhere. A mask blends the original back
  wherever it is not white.
- Choose the cutoff from the image's own values. The window estimates no background, no noise and no best threshold.
- A color-filter mosaic is cut as one plane, as it is stored. [Debayer](../../reference/glossary.md#debayer) it, or
  extract the channel you mean, first.

## Steps

1. **Open the window.** Make the duplicate the [active image](../../reference/glossary.md#active-image) and choose
   **Process > Intensity Transformations > Binarize**. The preview shows the image already cut at the default
   threshold.
2. **Choose the channels.** Leave **Selection** at **Joint RGB/K channels** to cut every channel at one value, or
   choose **Individual RGB/K channels** to give red, green and blue their own. The first box is then labelled
   **Red**.
3. **Set the threshold.** Raise **RGB/K** until only what you want stays white, or lower it to keep more. The
   preview follows.
4. **Set green and blue.** In the individual mode, set **Green** and **Blue** the same way.
5. **Check at 100 %.** Set the preview's **Display Mode** to **Linear**, zoom to 100 % and look at both sides of the
   boundary. Hold ++space++ over the preview to compare with the original.
6. **Apply.** Choose **Apply**. The image holds only two values, its history lists **Binarize** with the thresholds,
   and the controls return to their defaults. The window stays open.

## Controls

**Target Image**, the preview pane, **Close** and the progress window of a sequence work as in every process window:
see [Process Windows](../../interface/process-windows.md). The window has no histogram and no **Channel**.

```controls
window: Binarize
controls:
  - name: RGB/K
    kind: spinbox
    default: "0.500000"
    widget: {min: 0, max: 1, step: 0.05, decimals: 6}
    effect: Sets the threshold of every channel in the joint mode, or of red in the individual mode. A sample at or above it becomes white, every other sample black. Higher leaves fewer samples white.
    range: A fraction of full scale. At 0 every sample from black to white becomes white, and at 1 only the largest value does. The slider moves in 1,000 steps of 0.001. In an integer image the threshold is first turned into a code of the pixel type, as equation (1) says.
    enabled: It reads **Red** in the individual mode. In the joint mode, each change copies the value into **Green** and **Blue**. It stays enabled while Apply runs, which keeps the value it took when you pressed it.
    tradeoff: The boundary is hard. No soft edge, smoothing or removal of single pixels follows.
    scope: The preview and Apply, on every channel or on red, before the mask.
    persistence: Until the window closes. **Reset** and a finished Apply return it to 0.5.
  - name: Green
    kind: spinbox
    default: "0.500000"
    widget: {min: 0, max: 1, step: 0.05, decimals: 6}
    effect: Sets the threshold of the green channel in the individual mode.
    range: As **RGB/K**.
    enabled: Only for an RGB image with **Selection** at **Individual RGB/K channels**. In the joint mode it is disabled and shows the value of **RGB/K**.
    tradeoff: Thresholds that differ between the channels give colored edges and speckles.
    scope: The preview and Apply, on green only.
    persistence: Until the window closes. Choosing the joint mode overwrites it with **RGB/K**. **Reset** and a finished Apply return it to 0.5.
  - name: Blue
    kind: spinbox
    default: "0.500000"
    widget: {min: 0, max: 1, step: 0.05, decimals: 6}
    effect: Sets the threshold of the blue channel in the individual mode.
    range: As **RGB/K**.
    enabled: Only for an RGB image with **Selection** at **Individual RGB/K channels**. In the joint mode it is disabled and shows the value of **RGB/K**.
    tradeoff: Thresholds that differ between the channels give colored edges and speckles.
    scope: The preview and Apply, on blue only.
    persistence: Until the window closes. Choosing the joint mode overwrites it with **RGB/K**. **Reset** and a finished Apply return it to 0.5.
  - name: Selection
    kind: combo
    default: Joint RGB/K channels
    options: [Joint RGB/K channels, Individual RGB/K channels]
    effect: "**Joint RGB/K channels** cuts every channel at **RGB/K**. **Individual RGB/K channels** cuts red, green and blue at their own thresholds, and an alpha channel at 0, so every ordinary alpha sample becomes the largest value."
    enabled: For an RGB image, with or without alpha. Any other image keeps the joint mode, and the box is disabled. It stays enabled while Apply runs.
    tradeoff: Neither mode computes a luminance or asks all three channels to pass. Each channel is cut on its own.
    scope: The preview and Apply. A change runs a new preview.
    persistence: Until the window closes. **Reset** and a finished Apply return it to the joint mode.
  - name: Target Image
    kind: combo
    default: The active image
    options: [The active image, Every other open image or sequence the window can work on]
    effect: Chooses the image or sequence Apply replaces, with the mask on its view. **Selection** follows its channels.
    enabled: Disabled while Apply runs.
    tradeoff: Apply destroys the gray levels of the image chosen here. Check it before you apply.
    persistence: Until the window closes. **Reset** leaves it as it is.
  - name: Preview
    kind: check
    default: "On"
    effect: Shows the binary image beside the controls, with the mask blended in.
    enabled: It stays enabled while Apply runs.
    tradeoff: Each change cuts the whole image again on a copy of it. A **Preview area** cuts only its part, and shows exactly what Apply writes there, because no sample depends on its neighbours.
    scope: "The window only. The pane's toolbar offers **Display Mode**: judge the result in **Linear**, since an Autostretch display can hide the gray a mask left. **Preview area** is off when the window opens, and ticked it starts at 600 by 600 pixels in the middle of the image."
    persistence: Until the window closes. **Reset** leaves it as it is.
    note: A snapshot, named `<target> - Binarize - SNAPSHOT`, holds what the preview shows. It keeps the target's metadata and is not recorded as nonlinear, so it is not what Apply writes there.
  - name: Close
    kind: button
    default: ""
    effect: Closes the window without applying.
    enabled: It stays enabled while Apply runs.
  - name: Reset
    kind: button
    default: ""
    effect: Returns the three thresholds to 0.5 and **Selection** to the joint mode, and runs a preview.
    range: It leaves **Target Image**, **Preview** and the preview's toolbar as they are.
    enabled: It stays enabled while Apply runs, and the run keeps the values it took.
  - name: Apply
    kind: button
    default: ""
    effect: Cuts the whole image, or every frame of the sequence, with the thresholds and **Selection** as they are when you press it, then returns them to their defaults. The window stays open.
    enabled: From the moment the window opens, at any value. While the run works, **Target Image**, **Preview area** and **Take snapshot** are disabled. The thresholds, **Selection**, **Preview**, **Reset**, **Close** and **Apply** stay enabled, and a second press does nothing until the run ends.
    tradeoff: One pass over each channel on the threads the run is given, with no floating-point working copy. The result is copied once more when its header is restated, and the history keeps a copy of the image before. A mask adds a blend, and a sequence the decoding, encoding and disk space of a second copy.
    scope: The whole image or sequence, never limited by a preview area, with the mask blended afterwards.
    persistence: "The history step is **Binarize** and reads `K: 0.5` for a grayscale or mosaic image, `R: 0.5, G: 0.5, B: 0.5` for RGB in the joint mode, and `R: 0.4, G: 0.5, B: 0.6` for those thresholds in the individual mode. Undoing it restores the image. The rest of its metadata stays attached, and what it still means after the cut is for you to judge."
```

## How it works

1. **Selection** gives the thresholds: one for every channel, or one each for red, green and blue and 0 for an alpha
   channel. The individual mode applies only to a color image, red, green and blue with or without alpha.
2. Each threshold is turned into a value of the image's pixel type. Every stored sample of the channel is compared
   with it, on a copy of the image or of the [preview area](../../reference/glossary.md#preview-area).
3. A sample from that value up to the largest value of the type becomes the largest value, and every other sample
   the smallest. The image keeps its size, pixel type and channels.
4. A mask on the view then blends the result with the original.
5. The image replaces the target and its history gets one step. It keeps every
   [header card](../../reference/glossary.md#header-card) the process does not set, its
   [plate solution](../../reference/glossary.md#plate-solution) and its acquisition data. A saved file records
   `ANLIN` as `nonlinear`, `ANASRC` as `process`, `ANOP` as `binarize`, and `ANBASIS` as `display-rgb` or
   `monochrome`. A linear encoding is no longer stated, every sample is stated valid, and its
   [lineage](../../reference/glossary.md#lineage) gains an entry with the threshold, as `threshold 0.5`, or the three,
   as `thresholds 0.4, 0.5, 0.6`.

### Model

With \(m\) and \(M\) the smallest and the largest values of the pixel type, 0 and 255 for 8-bit unsigned, -32768 and
32767 for 16-bit signed, 0 and 1 for floating point, and \(t\) the threshold you set, the value compared is

\[ c = \lfloor M\,t \rfloor \text{ unsigned}, \qquad c = \operatorname{round}\bigl(t\,(M - m) + m\bigr) \text{ signed}, \qquad c = t \text{ floating point} \tag{1} \]

where a signed half is rounded away from zero, and \(t\) is held in single precision for a 32-bit floating-point
image. A stored sample \(x\) becomes

\[ B(x) = \begin{cases} M & \text{for } c \le x \le M \\ m & \text{otherwise} \end{cases} \tag{2} \]

In floating point, that is 1 for \(x \ge t\) and 0 below, and a sample equal to the threshold turns white. A sample
that is not a number or is infinite is never selected and becomes \(m\), and a floating-point sample above 1 becomes 0.
In an 8-bit image a threshold of 0.5 compares at code 127, about 0.498: code 127 becomes 255 and code 126 becomes 0.

With a mask of value \(a\) on the view, not inverted, the original \(X\) and the binary result \(B(X)\) are mixed as

\[ Y = X\,(1 - a) + B(X)\,a \tag{3} \]

Inverting the mask swaps the two weights. A gray mask therefore leaves values between the two levels.

### Boundaries

- No sample depends on its neighbours. The image's edges get no special treatment, no halo forms, and the channels
  are cut independently.
- A mosaic image is cut as one plane and keeps its mosaic. A sequence's frames are read as its view shows them, so a
  mosaic sequence with automatic debayer on is cut as RGB.
- Nothing smooths, connects or removes small groups of pixels, nothing resamples, and no threshold is refused within 0
  to 1.
- The image's [validity rule](../../reference/glossary.md#validity-rule) is not consulted. Every sample, also one the
  image marks as holding no data, is compared by its value, and the result states that every sample is valid. A
  sample that is not a number does not stop the run.
- A run the memory budget cannot hold, for its copy of the image or the history's, is refused with the reason, and
  the target and its history stay as they were. Close other images, or work on a smaller crop.

### Worked example

The picture above shows the window on the linear Carina [master](../../reference/glossary.md#master) after Histogram
Transformation with **Shadows** and **Midtones** at 0.02, which leaves the background near 0.28, with **Selection** at
**Joint RGB/K channels** and **RGB/K** at 0.50. Equation (2) sends the background to black. Each channel is cut on
its own: a red nebula pixel of (0.70, 0.40, 0.35) becomes (1, 0, 0), pure red, while the core and the stars, above
0.50 in all three channels, turn white.

```figure
images:
  - image: ../../assets/images/processes/masking/binarize-before.webp
    alt: The whole stretched frame before Apply, the red and white clouds of the nebula across a field of stars on a dark gray background.
    label: Before
  - image: ../../assets/images/processes/masking/binarize-after.webp
    alt: The whole frame after Apply, the bright clouds cut into flat areas of white and pure red with white stars on a black background.
    label: After
caption: >-
  The stretched Carina master before and after Apply with RGB/K at 0.50 in the joint mode. Both are the whole frame
  in the Linear display mode, without a display stretch.
```

In the individual mode with **Red** at 0.25, **Green** at 0.50 and **Blue** at 0.75, a pixel of (0.25, 0.49, 0.80)
becomes (1, 0, 1): red reaches its threshold exactly, green falls short and blue passes. A pixel of
(0.24, 0.50, 0.74) becomes (0, 1, 0).

## Starting points

| Situation | Start | Then adjust |
| --- | --- | --- |
| A soft grayscale mask to harden | No mask on the view, **Joint RGB/K channels**, **RGB/K** at 0.5 | Raise **RGB/K** to shrink the white area, lower it to grow it, and check at 100 %. |
| An RGB image and one cutoff | **Joint RGB/K channels** | Watch for pure colors at the edges. For a one-channel mask, convert a duplicate to grayscale first. |
| Different cutoffs for each channel | **Individual RGB/K channels** after a joint preview | Set **Red**, **Green** and **Blue** in turn, then look at the RGB view for colored edges. |
| A color-filter mosaic | Debayer it, or extract the channel you mean | Then threshold the result, which is easier to read than a cut mosaic. |

## When it goes wrong

!!! danger "Apply destroys the gray levels"
    **Apply** replaces the image in place and opens no new image. Undo the step in the image's history to return,
    and work on a duplicate next time.

Parts of the result are still gray
:   **Cause.** A mask on the view blended the original back in where it was not white.
:   **Fix.** Undo the step, turn the mask off, and apply again.

The result has pure red, green or blue areas
:   **Cause.** Each channel is cut on its own, so a pixel can pass in one channel and fail in another.
:   **Fix.** Convert a duplicate to grayscale before **Binarize**, or bring the individual thresholds closer.

**Green** and **Blue** cannot be edited
:   **Cause.** **Selection** is at **Joint RGB/K channels**, or the image is not RGB.
:   **Fix.** Choose **Individual RGB/K channels**. A grayscale image has one threshold only.

**Green** and **Blue** lost their values
:   **Cause.** Choosing the joint mode copies **RGB/K** into both.
:   **Fix.** Note the values before you switch, and enter them again in the individual mode.

In an 8-bit or 16-bit image, a sample just below the threshold turned white
:   **Cause.** The threshold is cut down to a whole code before the comparison.
:   **Fix.** Raise the threshold by one code, 1/255 or 1/65535, when the difference matters.

Pixels that held no number turned black
:   **Cause.** A sample that is not a number is never selected.
:   **Fix.** Repair or replace those pixels before **Binarize** when they should be white.

The history shows other values than the window
:   **Cause.** Apply took the values when you pressed it, and a finished Apply returned the controls to their
    defaults.
:   **Fix.** Read the history step: it holds the values the run used.

## Related

- [Range Selection](../analysis/range-selection.md), for a mask between two limits, with soft edges, opened as a new
  image.
- [Mask Drawing](mask-drawing.md), for a region outlined by hand.
- [Morphological Transformation](morphological-transformation.md), to grow, shrink, open or close a binary mask.
- [Masks](../../interface/masks.md), to attach the result to another image as its mask.
- [Image Statistics](../../interface/image-statistics.md), to check that each channel holds only its two values.
- [Process Windows](../../interface/process-windows.md), for the preview and what every process window shares.
