---
kind: process
stage: finish
menu: Process > Star Processing > Starmask Recombination
summary: >-
  Adds a stars image back to a matching starless image, everywhere or through a mask image, and opens the result as a
  new image.
data_in: nonlinear
data_out: nonlinear
destructive: false
preview: real-time
sequences: false
masks: none
external: none
glance:
  enabled: >-
    Always. The window chooses its images itself and opens without an active image. Preview and OK wait for a
    starless image and a stars image.
  inputs: >-
    A starless image and a stars image of the same width, height and channels, one or three, and an optional mask
    image of their width and height with one channel or theirs.
  outputs: >-
    One new image named after the two images with " - MERGED", beside the starless image, in the higher of their two
    pixel types. The inputs stay as they are. No file is written.
  state: >-
    A starless image and a stars image of the same stretched image, aligned and at the same stage, each processed on
    its own.
derived: false
rewritten: true
---
# Starmask Recombination

Starmask Recombination puts the stars back on a [starless image](../../reference/glossary.md#starless-image). It adds
a stars image to the starless one, pixel by pixel, after you have processed each on its own: the nebula without its
stars, the stars without the nebula. An optional [mask](../../reference/glossary.md#mask) image limits where the stars
return. The window chooses its images itself, and opens without an
[active image](../../reference/glossary.md#active-image).

**OK** opens the result as a new image beside the starless image and closes the window. The starless image, the
stars and the mask stay as they were. The result is recorded as nonlinear.

```figure
image: ../../assets/images/processes/star-operations/starmask-recombination.webp
alt: >-
  The Starmask Recombination window. On the left, Starless Image and Stars Image each name an image of the Carina
  nebula, Mask (optional) reads NO IMAGE beside a dimmed Invert check box, and Preview is ticked. On the right, the
  preview shows the nebula with its stars. OK and Close run along the bottom.
caption: >-
  Starmask Recombination with the starless Carina image and the star image Starnet++ made from it, without a mask.
  The preview shows the sum OK writes.
pins:
  - {n: 2, x: 2.0, y: 18.2}
  - {n: 3, x: 2.0, y: 23.1}
  - {n: 4, x: 2.0, y: 28.1}
  - {n: 5, x: 12.0, y: 88.8}
  - {n: 6, x: 77.5, y: 95.8}
```

## Before you start

- Make the two images from one image, at the same stage, so they are aligned pixel for pixel and of the same size
  and channels. The window does not align, resample or match them.
- Use a stars image made for addition, the image less its starless version. Starnet++ makes one with
  **Subtractive Mask**.
- Keep floating-point images when the stars may add up above 1. A pair that includes an integer image is added as
  [fractions of full scale](../../reference/glossary.md#fraction-of-full-scale), and the sum is held at 1.
- Use still images. The window is made for one image, not a [sequence](../../reference/glossary.md#sequence).
- A mask image must have the pair's width and height, and one channel or as many as the pair. The mask on a view
  does not count here.

## Steps

1. **Open the window.** Choose **Process > Star Processing > Starmask Recombination**. Its three boxes read
   `<--NO IMAGE-->`, and **Preview** and **OK** are disabled.
2. **Choose the starless image.** Pick it in **Starless Image**. It becomes the window's target, and its name is
   the first part of the result's.
3. **Choose the stars.** Pick them in **Stars Image**. **Preview** and **OK** enable, and the preview shows the sum.
4. **Limit where the stars return, if you want.** Pick a mask image in **Mask (optional)**: white puts the stars
   back, black keeps the starless image. Tick **Invert** to swap the two.
5. **Check at 100 %.** Zoom the preview into bright cores, halos and the mask's edges, and hold ++space++ over it to
   see the starless image.
6. **Choose OK.** The result opens beside the starless image as `<starless> - <stars> - MERGED`, and the window
   closes.

## Controls

The preview, its toolbar and its progress window are those of every process window: see
[Process Windows](../../interface/process-windows.md). The original the preview switches to is the starless image,
and its display follows the starless image's view.

```controls
window: Starmask Recombination
controls:
  - name: Starless Image
    kind: combo
    default: "<--NO IMAGE-->"
    options: ["<--NO IMAGE-->", Every open image or sequence]
    effect: Chooses the base the stars are added to. It becomes the window's target, gives the result its first name, its header and its record, and sets the preview's display.
    range: The list holds every open view, whatever its size, channels, pixel type or state. The same image may be picked in more than one box.
    tradeoff: A pair that is not aligned, or not at the same stage, gives doubled, shifted or missing structure.
    scope: The preview, a snapshot and OK. Its view is never changed.
    persistence: Until the window closes. Closing the image returns the box to `<--NO IMAGE-->`.
  - name: Stars Image
    kind: combo
    default: "<--NO IMAGE-->"
    options: ["<--NO IMAGE-->", Every open image or sequence]
    effect: Chooses the stars added to the starless image, as the image its view shows when a run is asked for.
    range: The run refuses an image whose width, height or channels differ from the starless image's.
    tradeoff: Whatever it holds besides stars is added too. A pixel type higher than the starless image's becomes the result's.
    scope: The preview, a snapshot and OK, cropped to a preview area as the starless image is.
    persistence: Until the window closes. Its name is the second part of the result's.
  - name: Mask (optional)
    kind: combo
    default: "<--NO IMAGE-->"
    options: ["<--NO IMAGE-->", Every open image or sequence]
    effect: Chooses an image that weighs, pixel by pixel, between the starless image and the sum. `<--NO IMAGE-->` puts every star back.
    range: It must have the pair's width and height. One channel weighs every channel, and as many channels as the pair weigh each channel by its own. Its samples are read as fractions of full scale and held between 0 and 1.
    enabled: Choosing an image enables **Invert**.
    tradeoff: A color mask can weigh the channels differently and shift star colors.
    scope: This window only, cropped to a preview area as the pair is. The mask on a view, and the view's mask settings, are not used.
    persistence: Until the window closes.
  - name: Invert
    kind: check
    default: "Off"
    effect: Off, white takes the sum and black keeps the starless image. On, the mask is read the other way round.
    range: The mask is held between 0 and 1 first, then inverted.
    enabled: While **Mask (optional)** names an image. Clearing the mask keeps the tick, which then does nothing.
    tradeoff: Inverting a protection mask the wrong way puts stars back where they should stay out.
    persistence: Until the window closes. The window has no **Reset**.
  - name: Preview
    kind: check
    default: "On"
    effect: Shows the recombined image beside the boxes, as in every process window.
    enabled: While a starless image and a stars image are chosen. The window grows when the pane appears.
    tradeoff: Each change adds the two images again, with the mask and a copy of each integer image.
    persistence: Until the window closes.
  - name: OK
    kind: button
    default: ""
    effect: Adds the stars to the starless image through the mask and opens the result beside the starless image as `<starless> - <stars> - MERGED`. The window then closes.
    enabled: While a starless image and a stars image are chosen. It does not check that they match. The run does.
    tradeoff: The result is a full new image, and an integer image or mask is copied to floating point first. Its time grows with the pixels and the channels.
    scope: The whole image, with the stars, the mask and **Invert** as they are when you press it. A change made afterwards reaches the next run only.
    persistence: >-
      The new image's history starts with a **Starmask Recombination** step that reads `StarmaskRecombinationProcess:
      Image created from starless image '<starless>' and starmask image '<stars>' using operation add`, followed by
      `, through mask '<mask>'` and ` inverted` when they apply.
  - name: Close
    kind: button
    default: ""
    effect: Closes the window without a run, as in every process window.
    note: An image made by an earlier OK or a snapshot stays as it is.
```

## How it works

1. When a run is asked for, the window reads the stars and the mask, each cropped to the
   [preview area](../../reference/glossary.md#preview-area) when there is one, beside the starless image, which is its
   target.
2. It refuses a pair whose width, height or channels differ, an image of other than one or three channels, and a
   mask of another size.
3. Both images are read in one way. Two floating-point images that state a
   [physical quantity](../../reference/glossary.md#physical-quantity), as a floating-point [FITS](../../reference/glossary.md#fits) file does, are read
   as they are. Any other pair is read as fractions of full scale. The mask is always read as fractions, held between 0
   and 1.
4. Each sample is added in double precision, held at 1 for fractions, blended over the starless image by the mask,
   and written once in the higher of the two [pixel types](../../reference/glossary.md#pixel-type), rounded to the
   nearest code.
5. The result keeps the starless image's [header cards](../../reference/glossary.md#header-card), acquisition data,
   exposure included, and [plate solution](../../reference/glossary.md#plate-solution). Its
   [lineage](../../reference/glossary.md#lineage) adds the recombination, and it is recorded as nonlinear with the
   operation `starmask-recombination`.

### Model

Let \(B\) be a sample of the starless image, \(S\) the stars sample at the same place and \(A\) the mask value there,
held between 0 and 1. For a pair read as fractions, the sum is

\[ R = \min(B + S,\ 1) \tag{1} \]

and for two images of physical quantities it is \(R = B + S\), above 1 if it comes to that. There is no lower limit,
so a negative floating-point sum stays negative, and an integer result stops at the ends of its type. Through the
mask, the result is

\[ O = B\,(1 - A) + R\,A \tag{2} \]

and with **Invert**, \(A\) is replaced by \(1 - A\). Without a mask, \(O = R\). When the sum stays below 1, equation
(2) is \(O = B + A\,S\): a mask of 0 puts no star back and a mask of 1 the whole stars image. The pixel types rank, from
lowest to highest, 8-bit signed, 8-bit unsigned, 16-bit signed, 16-bit unsigned, 32-bit signed, 32-bit unsigned,
32-bit floating point and 64-bit floating point. The mask does not change the result's type.

### Boundaries

- A pair whose size or channels differ is refused with "the star image does not have the starless image's size and
  channels". An image of another number of channels than one or three is refused with "a recombination reads one
  channel or three".
- A mask of another size, or of a channel count that is neither one nor the pair's, is refused with "the mask is not
  one channel or the images' channels at their width and height".
- A pixel that the stars or the mask marks as invalid keeps the starless image's samples, and a sample the starless
  image marks as invalid is written as 0. A sample that is not a number in a valid pixel stops the run with "a source
  holds a sample that is not finite in a pixel its validity admits".
- A floating-point image that states fractions of full scale is added as fractions, and its sum is held at 1, even
  beside another floating-point image.
- A stopped run writes nothing.

### Worked example

The two images come from the Carina [master](../../reference/glossary.md#master) after Histogram Transformation with
**Shadows** and **Midtones** at 0.02. [Starnet++](starnet.md) made from it the starless image and, with
**Subtractive Mask**, the star image. Choose them in **Starless Image** and **Stars Image**, leave **Mask (optional)**
at `<--NO IMAGE-->` and choose **OK**.

A pixel where the starless image holds 0.36 and the stars 0.14 becomes 0.50 by equation (1). Through a mask pixel of
0.25, equation (2) gives \(0.36 + 0.25 \times 0.14 = 0.395\), and with **Invert** on
\(0.36 + 0.75 \times 0.14 = 0.465\). Where the star image is the master less its starless version, the sum without a
mask comes back close to the stretched master.

```figure
images:
  - image: ../../assets/images/processes/star-operations/starmask-recombination-before.webp
    alt: A crop at 100 % of the middle of the starless Carina image, the nebula and its dark lanes with no stars.
    label: Before
  - image: ../../assets/images/processes/star-operations/starmask-recombination-after.webp
    alt: The same crop of the new merged image, the same nebula with its field of stars back in place.
    label: After
caption: >-
  The middle of the starless Carina image at 100 %, and of the image OK made by adding its stars back without a mask.
  Both crops are shown in the Linear display mode.
```

## Starting points

| Situation | Start | Then adjust |
| --- | --- | --- |
| Every star back | Floating-point images, no mask | Look at cores and halos at 100 %. Change the stars image itself, not this window. |
| Stars back only in places | A grayscale mask of the pair's size, **Invert** off | Look at the mask's edges at 100 %. Tick **Invert** only for a mask drawn the other way round. |
| Images of different pixel types | Convert both images, and the mask, to floating point first | Recombine. An integer result stops at its largest code. |

## When it goes wrong

!!! warning "A stars image made by unscreen comes back too bright"
    The window adds. A stars image made by unscreen, such as **Unscreen Star Layer** of Starnet++ or the reduced
    stars of [Star Reduction](star-reduction.md), is larger than the image less its starless version, so its stars
    return brighter than they were. Use a stars image made by subtraction.

Preview and OK stay disabled
:   **Cause.** **Starless Image** or **Stars Image** reads `<--NO IMAGE-->`.
:   **Fix.** Choose an image in both.

"the star image does not have the starless image's size and channels"
:   **Cause.** The two images differ in width, height or number of channels.
:   **Fix.** Make both from the same image, cropped and resized alike, with the same channels.

"the mask is not one channel or the images' channels at their width and height"
:   **Cause.** The mask image has another size, or another number of channels.
:   **Fix.** Give the mask the pair's width and height, and one channel or as many as the pair.

The stars look doubled or shifted
:   **Cause.** The two images are not aligned pixel for pixel.
:   **Fix.** Make them again from one image, without registering, cropping or resizing one of them alone.

Bright cores are flat at the top
:   **Cause.** The pair includes an integer image, or one in fractions of full scale, so the sum is held at 1.
:   **Fix.** Convert both images to floating point first, or dim the stars image before you add it.

The window closed after OK
:   **Cause.** A finished OK opens a new image and closes the window.
:   **Fix.** Look for `<starless> - <stars> - MERGED` beside the starless image. Open the window again for another
    try.

## Related

- [Starnet++](starnet.md), to make the starless image and a stars image to add back.
- [Profiled Starmask Recombination](profiled-starmask-recomb.md), to put back only the stars a star profile lists.
- [Star Reduction](star-reduction.md), to make the stars smaller instead of separating them.
- [Star Mask](star-mask.md), to make a mask image of the stars for **Mask (optional)**.
- [Process Windows](../../interface/process-windows.md), for the preview and what every process window shares.
