---
kind: process
stage: any
menu: Process > Utilities > Image Blend
summary: >-
  Combines the image with a second image of the same size in one of 27 blend modes, after an optional stretch of each
  and a filter of the second, at the opacity you set.
data_in: either
data_out: nonlinear
destructive: true
preview: real-time
sequences: true
masks: post-blend
external: none
glance:
  enabled: >-
    An image or a sequence view is active, no Image Blend window is open on it yet, and no window that needs the image
    for itself is open on it. OK is enabled before a blend image is chosen.
  inputs: >-
    The base, one grayscale or RGB image or sequence, a blend image of exactly its width and height, and an optional
    mask on the base's view.
  outputs: >-
    The base, replaced, with one history step, in its own size and pixel type and with three channels when the blend
    makes color. No file is written.
  state: >-
    Two images of the same pixel grid at the same stage: an image and a detail layer made from it, two processings of
    one image, or an image and a second one to add or subtract.
derived: false
---
# Image Blend

Image Blend combines the image, the base, with a second image of the same size, the blend image, pixel by pixel. You
choose one of 27 blend modes: the darker or the lighter sample, a product, a sum, a difference, a contrast mode such as
Overlay, or a color component of one image with the rest of the other. Each image can be stretched first, the blend
image can be blurred, reduced to its detail or colored, and **Opacity** mixes the result back toward the base.

**OK** replaces the base or every frame of its [sequence](../../reference/glossary.md#sequence), keeps its size and
[pixel type](../../reference/glossary.md#pixel-type), and adds one step to its
[history](../../reference/glossary.md#history). The result is recorded as nonlinear, whatever the mode.

```figure
image: ../../assets/images/processes/analysis/image-blend.webp
alt: >-
  The Image Blend window. On the left, a histogram over its toolbar, then the Base image group with its Image box and
  three Stretch rows, the Blend image group with its Image box, three Stretch rows with Black Point and Midtones at
  0.0200, Filter Type at High Pass with the Radius row under it, and Opacity, then Blend mode at Overlay with the
  Preserve lightness check box, and Preview. On the right, the preview shows the stretched red nebula with its edges
  and stars drawn sharply. Reset, OK and Close run along the bottom.
caption: >-
  Image Blend on the stretched Carina master, with the linear master stretched alike as the blend image, filtered by
  High Pass at a radius of 10 and laid over the base in Overlay.
pins:
  - {n: 2, x: 2.0, y: 54.1}
  - {n: 3, x: 2.0, y: 60.0}
  - {n: 4, x: 2.0, y: 72.5}
  - {n: 5, x: 2.0, y: 85.8}
  - {n: 6, x: 2.0, y: 79.2}
  - {n: 7, x: 88.0, y: 22.0}
  - {n: 8, x: 80.4, y: 97.0}
```

## Before you start

- **OK** replaces the base and records it as nonlinear, with any mode and any **Opacity**. Duplicate it first, or use
  its history to return.
- Register and crop the two images alike. The blend image is offered by its width and height alone: nothing aligns,
  resamples or normalizes it, or checks its world coordinates, channels, exposure or background.
- Bring the two to the same stage. A [linear image](../../reference/glossary.md#linear-image) blended with a
  stretched one mixes two scales: the **Stretch** groups can [stretch](../../reference/glossary.md#stretch) either
  image for the blend.
- For a detail layer under Overlay, Soft Light or Linear Light, use a duplicate of the base as the blend image and
  **High Pass** in **Filter Type**, or a layer made by High Pass Filter.
- A [mask](../../reference/glossary.md#mask) on the base's view blends the result with the original after
  **Opacity**: white takes the result, black keeps the base. Set the blend without it first.
- On a sequence, every frame is blended with the one blend image. Two sequences are not paired frame by frame.

## Steps

1. **Open the window.** Make the image you want to change the
   [active image](../../reference/glossary.md#active-image) and choose **Process > Utilities > Image Blend**. The
   **Image** box of **Base image** names it, the **Image** box of **Blend image** reads `<--NO IMAGE-->`, and the
   controls under it are dimmed.
2. **Choose the blend image.** Pick it in the **Image** box of **Blend image**. The dimmed controls enable, and the
   preview shows the blend image, because **Blend mode** starts at **Replace**.
3. **Stretch an image if it needs it.** Leave both **Stretch** groups at their defaults unless one image must be
   brought to the other's levels.
4. **Filter the blend image.** Choose **High Pass** or **Gaussian Blur** in **Filter Type** and set **Radius**, or
   **Hue** or **Color** and set **Saturation** and **Hue**. Leave **None** to blend the image as it is.
5. **Choose the mode.** Pick it in **Blend mode**. The preview shows the blend.
6. **Set the strength.** Lower **Opacity** to mix the result back toward the base.
7. **Check at 100 %.** Zoom the preview into stars, edges and background, and hold ++space++ over it to see the BASE
   IMAGE. The histogram counts the result.
8. **Choose OK.** The base is replaced, its history lists **Image Blend** with the mode and the blend image's name,
   and the controls return to their defaults. The blend image stays chosen.

## Controls

The histogram, its toolbar, the preview's toolbar and the progress window of a sequence are those of every process
window: see [Process Windows](../../interface/process-windows.md). The preview is labelled PREVIEW, and BASE IMAGE
while you switch to the original.

```controls
window: Image Blend
controls:
  - name: Image
    object: targetImageSelector
    kind: combo
    default: The active image
    options: [The active image, Every other open image or sequence the window can work on]
    effect: Chooses the base, the image OK replaces. Its mask blends the result, and its pixel grid, plate solution and history are the result's.
    enabled: It is the only control of the window disabled while a run works.
    tradeoff: The wrong base is replaced. Check the window's title before OK.
    persistence: Until the window closes. Choosing another fills the blend image's list again and returns it to `<--NO IMAGE-->`. **Reset** leaves it.
  - name: Black Point
    object: srcImageBlackPointValue
    kind: spinbox
    default: "0.0000"
    widget: {min: 0, max: 1, step: 0.01, decimals: 4}
    effect: Sets the black point of the base's stretch. Higher sends more of the low samples to black and spreads the rest over the range.
    range: A black point above **Highlights** is refused when the preview or OK runs. Equal to them, the stretch is a black and white threshold.
    tradeoff: What it sends to black is lost to the mode. **Opacity** and the mask still mix with the base as it was.
    scope: Every channel of the base, before the mode. **Opacity** and the mask mix with the base unstretched.
    persistence: Until the window closes. **Reset** and a finished OK return it to 0.
    note: The slider beside the box moves the same value. The three values at their defaults leave the base as it is, and no stretch is run.
  - name: Highlights
    object: srcImageHighlightsValue
    kind: spinbox
    default: "1.0000"
    widget: {min: 0, max: 1, step: 0.01, decimals: 4}
    effect: Sets the white point of the base's stretch. Lower sends more of the high samples to white and spreads the rest.
    tradeoff: Lowering it flattens star cores and bright structure in what the mode receives.
    scope: Every channel of the base, before the mode.
    persistence: Until the window closes. **Reset** and a finished OK return it to 1.
  - name: Midtones
    object: srcImageMidtonesValue
    kind: spinbox
    default: "0.5000"
    widget: {min: 0, max: 1, step: 0.01, decimals: 4}
    effect: Sets the balance of the base's stretch. Below 0.5 it brightens what lies between the two points, above 0.5 it darkens it.
    tradeoff: Near either end of its range almost everything collapses to black or white.
    scope: Every channel of the base, before the mode.
    persistence: Until the window closes. **Reset** and a finished OK return it to 0.5.
  - name: Image
    object: secondImageSelector
    kind: combo
    default: "<--NO IMAGE-->"
    options: ["<--NO IMAGE-->"]
    effect: Chooses the blend image, which the mode combines with the base. Choosing one enables the controls under it and **Blend parameters**.
    range: After `<--NO IMAGE-->` the list holds every other open view of exactly the base's width and height, whatever its channels, pixel type, state or header. An image of another size is left out, never resampled. The tooltip of each item gives its file, size, channels, pixel type and memory.
    enabled: The list follows the views as they open, close, change or are renamed. When the image chosen closes, the box returns to `<--NO IMAGE-->`.
    tradeoff: Nothing checks the registration, the channels, the world coordinates, the exposure or the background. A pair off by a pixel gives doubled edges.
    scope: The image its view shows when the preview, a snapshot or OK is asked for, which is the current frame of a sequence, for every frame of a base sequence too. A preview area crops it to the same area as the base. Its own mask plays no part.
    persistence: Until the window closes or the base changes. **Reset** and OK leave it.
  - name: Black Point
    object: imageBlendBlackPointValue
    kind: spinbox
    default: "0.0000"
    widget: {min: 0, max: 1, step: 0.01, decimals: 4}
    effect: Sets the black point of the blend image's stretch, as the base's does for the base.
    range: A black point above **Highlights** is refused when the preview or OK runs.
    enabled: With a blend image.
    scope: Every channel of the blend image, before its filter and the mode.
    persistence: Until the window closes. **Reset** and a finished OK return it to 0.
  - name: Highlights
    object: imageBlendHighlightsValue
    kind: spinbox
    default: "1.0000"
    widget: {min: 0, max: 1, step: 0.01, decimals: 4}
    effect: Sets the white point of the blend image's stretch. Lower sends more of its high samples to white.
    enabled: With a blend image.
    scope: Every channel of the blend image, before its filter and the mode.
    persistence: Until the window closes. **Reset** and a finished OK return it to 1.
  - name: Midtones
    object: imageBlendMidtonesValue
    kind: spinbox
    default: "0.5000"
    widget: {min: 0, max: 1, step: 0.01, decimals: 4}
    effect: Sets the balance of the blend image's stretch. Below 0.5 it brightens the blend image, above 0.5 it darkens it.
    enabled: With a blend image.
    tradeoff: A strong stretch redistributes the blend image's tones before the mode sees them.
    scope: Every channel of the blend image, before its filter and the mode.
    persistence: Until the window closes. **Reset** and a finished OK return it to 0.5.
  - name: Filter Type
    kind: combo
    default: None
    options: [None, Gaussian Blur, High Pass, Hue, Color]
    effect: Chooses what is done to the blend image after its stretch. **Gaussian Blur** blurs it. **High Pass** keeps its detail around middle gray. **Hue** gives its lightness one hue, and **Color** gives each of its channels one hue.
    enabled: With a blend image. **Gaussian Blur** and **High Pass** show **Radius**. **Hue** and **Color** show **Saturation** and **Hue**.
    tradeoff: A filter adds a full-size working copy and its own work to every preview. **Hue** and **Color** make the result color.
    scope: The blend image only, before the mode.
    persistence: Until the window closes. **Reset** and a finished OK return it to **None**.
  - name: Radius
    kind: spinbox
    default: "10.000"
    widget: {min: 0.1, max: 1000, step: 0.01, decimals: 3}
    effect: Sets the standard deviation of the blur, in pixels. Higher blurs larger structures away, so **High Pass** keeps coarser detail. Lower keeps only the finest detail.
    enabled: Shown for **Gaussian Blur** and **High Pass**.
    tradeoff: Its work grows with the radius, and with its square where the blur runs in passes. Near the edges a large radius reads the image mirrored.
    scope: The blend image only.
    persistence: Until the window closes. **Reset** and a finished OK return it to 10.
    note: Above about 32 pixels the blur runs as one kernel up to 4,096 pixels wide, on a blend image more than twice that kernel's radius wide and high, and otherwise as several exact passes whose variances add up to it.
  - name: Saturation
    kind: spinbox
    default: "1.000"
    widget: {min: 0, max: 1, step: 0.01, decimals: 3}
    effect: Sets how strong the color of **Hue** and **Color** is. 0 leaves the blend image gray, 1 gives the full color.
    enabled: Shown for **Hue** and **Color**.
    scope: The blend image only.
    persistence: Until the window closes. **Reset** and a finished OK return it to 1.
  - name: Hue
    kind: spinbox
    default: "0.000"
    widget: {min: 0, max: 360, step: 0.01, decimals: 3}
    effect: Sets the color of **Hue** and **Color**, in degrees around the color wheel. 0 and 360 are red, 120 green and 240 blue.
    enabled: Shown for **Hue** and **Color**.
    scope: The blend image only.
    persistence: Until the window closes. **Reset** and a finished OK return it to 0.
  - name: Opacity
    kind: spinbox
    default: "1.0000"
    widget: {min: 0, max: 1, step: 0.05, decimals: 4}
    effect: Mixes the result with the base. 1 takes the result whole, 0 gives the base back, and between them the two are mixed in proportion.
    enabled: With a blend image.
    tradeoff: Lower softens everything the window does, the stretches and the filter included.
    scope: After the mode and **Preserve lightness**, with the base as it was before its stretch. The mask blends after it.
    persistence: Until the window closes. **Reset** and a finished OK return it to 1.
  - name: Blend mode
    kind: combo
    default: Replace
    options: [Replace, Darken/Min, Multiply, Color Burn, Linear Burn, Darker Color, Lighten/Max, Screen, Color Dodge, Linear Dodge/Add, Lighter Color, Overlay, Soft Light, Hard Light, Vivid Light, Linear Light, Pin Light, Difference, Exclusion, Subtract, Divide, Power, Arctan, Hue, Saturation, Color, Lightness]
    effect: Chooses how the blend image combines with the base. **Replace** takes the blend image, the next five darken, the five after them lighten, the six from **Overlay** add contrast, **Difference** to **Divide** compare or subtract, **Power** and **Arctan** are functions of the two, and the last four take color components of one image and the rest of the other.
    range: Lines in the list separate the groups. Every result is held between 0 and 1.
    enabled: With a blend image.
    tradeoff: The contrast modes clip readily. **Darker Color**, **Lighter Color** and the four component modes convert every pixel to another color model and cost more. **Hue**, **Saturation**, **Color** and **Lightness** make the result color.
    scope: Every channel, before **Opacity** and the mask.
    persistence: Until the window closes. **Reset** and a finished OK return it to **Replace**.
    note: Every formula is on the reference page.
  - name: Preserve lightness
    kind: check
    default: "Off"
    effect: Gives the result the CIE lightness of the stretched base after the mode, so the result keeps the base's brightness and takes its color from the blend.
    enabled: With a blend image, when the base or the blend image is color, and with a mode other than **Color** and **Lightness**, which settle the lightness themselves.
    tradeoff: Two color conversions of the whole image for each preview, so while it is ticked a dragged slider updates the preview only when you let go. Colors outside the range are held at full scale.
    scope: A color result only. **Opacity** and the mask act afterwards, and can move the lightness again.
    persistence: Until the window closes. **Reset** and a finished OK return it to off.
  - name: Preview
    kind: check
    default: "On"
    effect: Shows the blended image beside the controls, with the mask blended in. Off removes the pane.
    persistence: Until the window closes. **Reset** leaves it as it is.
  - name: OK
    kind: button
    default: ""
    effect: Blends the base, or every frame of a base sequence, with the blend image as the window stands, replaces it and returns the controls to their defaults.
    enabled: Always, also before a blend image is chosen. Then it applies the base's stretch alone and records the result as nonlinear, or leaves the samples as they were with the stretch at its defaults.
    scope: The whole base, through the mask on its view. Each frame of a sequence is stretched by the base's values and blended with the one blend image.
    persistence: >-
      The history step is **Image Blend**, read as `<mode> with <blend image>`, such as `Overlay with M42 stretched`.
      Without a blend image it reads like `Base stretch: shadows 0.000, midtones 0.250, highlights 1.000`.
    note: While the run works only the base's **Image** is disabled. A change made meanwhile does not reach the run. **Close** then hides the window, and a run on a single image still finishes.
  - name: Reset
    kind: button
    default: ""
    effect: Returns both stretches, **Filter Type**, **Radius**, **Saturation**, **Hue**, **Opacity**, **Blend mode** and **Preserve lightness** to their defaults.
    range: It leaves both **Image** boxes, **Preview** and the toolbar's choices as they are.
```

## How it works

1. Both images are read as [fractions of full scale](../../reference/glossary.md#fraction-of-full-scale): the base
   as \(A\), the blend image as \(B\). An integer base is worked on a floating-point copy.
2. Each image is stretched by its own values, when they are not the defaults, and the blend image is then filtered.
   A grayscale image meets a color one as three equal channels.
3. The mode combines \(A\) and \(B\), and **Preserve lightness** puts back the lightness of the stretched base.
4. **Opacity** mixes the result with the base as it was before its stretch, and a mask on the base's view then
   blends the two again.
5. The result is written once in the base's pixel type, rounded and held to the ends of the type. It replaces the
   base, its
   [processing state](../../reference/glossary.md#processing-state) is recorded as nonlinear with the operation
   `image-blend`, and its header is built from both images.

### Model

A stretch is the [midtones transfer function](../../reference/glossary.md#midtones-transfer-function) of Histogram
Transformation. A sample \(x\) is placed between the [black point](../../reference/glossary.md#black-point) \(S\)
of **Black Point** and the [white point](../../reference/glossary.md#white-point) \(H\) of **Highlights**, and with
\(m\) the value of **Midtones** it becomes

\[ t = \frac{x - S}{H - S}, \qquad f(t) = \frac{(m - 1)\,t}{(2m - 1)\,t - m} \tag{1} \]

with 0 below \(S\) and 1 above \(H\). **High Pass** subtracts the Gaussian blur \(G_r * B\) of radius \(r\) from the
blend image and centers the rest on middle gray:

\[ B' = \min\bigl(1,\ \max(0,\ B - G_r * B + 0.5)\bigr) \tag{2} \]

With \(R\) the result of the mode, \(o\) the value of **Opacity**, \(A_0\) the base before its stretch and \(w\) the
weight of the mask at the pixel, the image written is

\[ O = (1 - w)\,A_0 + w\,\bigl((1 - o)\,A_0 + o\,R\bigr) \tag{3} \]

A detail layer, from equation (2) or from High Pass Filter, is middle gray where the image is smooth and lighter or
darker by some \(d\) at its detail: \(B = 0.5 + d\). Overlay, Soft Light and Linear Light leave the base as it is
where \(d = 0\), and add the detail in proportions of their own:

\[ R_{\text{Overlay}} = A + 2\min(A,\ 1 - A)\,d, \qquad R_{\text{Soft Light}} = A + 2A(1 - A)\,d, \qquad R_{\text{Linear Light}} = A + 2d \tag{4} \]

each held between 0 and 1. Overlay gives the [midtones](../../reference/glossary.md#midtones) the whole detail and
the shadows and highlights less. Soft Light gives the midtones half as much and comes close to Overlay near black.
Linear Light adds twice the detail at every level, so it is the strongest and clips first. Laid over the image
\(I\) it was made from, Linear Light gives \(I + 2(I - G_r * I)\): the image with twice its detail added.

| Base \(A\) | Layer \(B\) | Overlay | Soft Light | Linear Light |
| --- | --- | --- | --- | --- |
| 0.10 | 0.60 | 0.12 | 0.118 | 0.30 |
| 0.28 | 0.60 | 0.336 | 0.320 | 0.48 |
| 0.50 | 0.40 | 0.40 | 0.45 | 0.30 |
| 0.80 | 0.60 | 0.84 | 0.832 | 1.00 |

### Boundaries

- A black point above the highlights of either stretch is refused: the preview reports "a transfer's parameters must
  be finite, within the unit interval and ordered", and OK writes nothing.
- A pixel either image marks as invalid keeps the base's samples. A sample that is not a finite number, in a pixel
  its image states valid, stops the run.
- The blend image is the one its view shows when the run is asked for. One whose size has changed since is refused:
  "the blend image does not have the target's size".
- The result has three channels when either image has three, or the mode or the filter makes color, and one
  otherwise. A [CFA](../../reference/glossary.md#cfa) mosaic is blended as a grayscale image, and the result states
  no color filter array.
- An integer result is held at the ends of its type, so a sum in **Linear Dodge/Add** stops at the largest code.

### Worked example

The base is the Carina [master](../../reference/glossary.md#master) after Histogram Transformation with **Shadows**
and **Midtones** at 0.02, which leaves the background near 0.28. The blend image is the linear master, with
**Black Point** and **Midtones** of its **Stretch** at 0.02, which makes it the same picture. **Filter Type** is
**High Pass** at a **Radius** of 10, and **Blend mode** is **Overlay**. By equation (4) the smooth background stays at 0.28. A sample of 0.80 at the edge of a bright filament,
whose blur is 0.60, becomes 0.88, and one of 0.40 beside it under the same blur becomes 0.24. The background's grain
is in the layer too, and at 0.28 Overlay adds 0.56 times it, so the grain grows by about half.

```figure
images:
  - image: ../../assets/images/processes/analysis/image-blend-before.webp
    alt: A crop of the stretched nebula at 100 percent, bright clouds beside dark dust lanes and many small stars on a gray background.
    label: Before
  - image: ../../assets/images/processes/analysis/image-blend-after.webp
    alt: The same crop with harder edges along the clouds and the dust lanes, crisper stars and a grainier background.
    label: After
caption: >-
  The middle of the stretched Carina Nebula master at 100 % before and after OK with its High Pass layer in Overlay.
  Both are shown in the Linear display mode, without a display stretch.
```

With **Soft Light** in place of Overlay, the same two samples become 0.864 and 0.304. With **Linear Light** they
become 1 and 0: the filament's edge is clipped to white and its dark side to black.

## Starting points

| Situation | Start | Then adjust |
| --- | --- | --- |
| Detail over the image itself | A duplicate as the blend image, **High Pass**, **Radius** 10, **Overlay** | **Soft Light** for less and **Linear Light** for more, then **Opacity**, then a mask to keep the background. |
| What changed between two versions | **Difference**, both stretches and **Filter Type** at their defaults | Identical samples turn black. Register the pair first, and lower **Opacity** only to see the base around the changes. |
| Stars or emission added to an image | **Screen** or **Linear Dodge/Add**, **Opacity** below 1 | Match the two images' levels with the **Stretch** groups, then mask what must not brighten. |
| Color from another image | **Color**, or **Hue** or **Saturation** with **Preserve lightness** | Keep **Opacity** at 1 at first, and check the brightest colors at 100 % for clipping. |

## When it goes wrong

!!! warning "OK replaces the base and records it as nonlinear"
    Every OK marks the result nonlinear, whatever the mode, the **Opacity** or the mask. Duplicate the base first, and
    use its history to return.

The image you want is not in the blend image's list
:   **Cause.** It is the base itself, or its width or height differs from the base's.
:   **Fix.** Register, crop or resize it to the base's grid, or open the window on the other image.

The blend controls stay dimmed
:   **Cause.** No blend image is chosen, or the one chosen was closed.
:   **Fix.** Choose the blend image in the **Image** box of **Blend image**.

The preview shows the blend image, not a blend
:   **Cause.** **Blend mode** is **Replace**, as it is when the window opens and after every OK.
:   **Fix.** Choose the mode you want.

"a transfer's parameters must be finite, within the unit interval and ordered"
:   **Cause.** **Black Point** is above **Highlights** in one of the **Stretch** groups.
:   **Fix.** Lower **Black Point** below **Highlights**, or press **Reset**.

Preserve lightness is dimmed
:   **Cause.** The mode is **Color** or **Lightness**, or both images are grayscale.
:   **Fix.** None is needed: those modes settle the lightness themselves, and a grayscale pair has no color.

Black spots where the blend image is white, in Color Dodge
:   **Cause.** **Color Dodge** gives 0 where the blend sample is exactly 1.
:   **Fix.** Lower the blend image's **Highlights** a little, or use **Screen** or **Linear Dodge/Add**.

The background is grainier after Overlay, Soft Light or Linear Light
:   **Cause.** The detail layer carries the grain with the detail, and the mode adds both.
:   **Fix.** Lower **Opacity**, use a gentler mode, or protect the background with a mask.

OK only stretched the base, or seemed to do nothing
:   **Cause.** OK is enabled without a blend image, and then applies the base's stretch alone, or nothing at its
    defaults.
:   **Fix.** Undo the step in the image's history, choose the blend image first and apply again.

## Related

- [Image Blend Reference](image-blend-reference.md), for the formula of every mode, the filters and the header of
  the result.
- [High Pass Filter](../sharpening/high-pass-filter.md), to make a detail layer as an image of its own.
- [Image Registration](../registration-alignment/image-registration.md) and [Image Compare](image-compare.md), to
  align the two images and to look at them side by side without changing either.
- [Starmask Recombination](../star-operations/starmask-recombination.md) and
  [Pixel Math](../pixel-math/pixel-math.md), to put the stars back after separate processing, and for a formula of
  your own.
- [Masks](../../interface/masks.md), to make, attach and invert the mask the result is blended through.
- [Process Windows](../../interface/process-windows.md), for the preview, the histogram toolbar and what every
  process window shares.
