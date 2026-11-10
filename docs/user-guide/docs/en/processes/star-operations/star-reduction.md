---
kind: process
stage: finish
menu: Process > Star Processing > Star Reduction
summary: >-
  Makes the stars of a stretched image smaller and fainter by comparing it with a starless version of itself, and can
  protect the faint stars with a mask it builds.
data_in: nonlinear
data_out: nonlinear
destructive: false
preview: real-time
sequences: true
masks: post-blend
external: Optional StarNet2 2.5.3 or newer, to generate the starless image
glance:
  enabled: >-
    An image or a sequence view is active, no Star Reduction window is open on it yet, and no window that needs the
    image for itself is open on it. OK waits for a starless image to be chosen in the window.
  inputs: >-
    One grayscale or RGB image or sequence, a starless image of the same width, height, channels and color layout in
    any pixel type, and an optional mask on the target's view.
  outputs: >-
    A new image or sequence named after the target with " - star reduced", or the target replaced with one history
    step; for a still image, also the stars taken away as a new image. No file is written.
  state: >-
    A stretched master, with a starless image made from it at the same stage of processing.
derived: true
---
# Star Reduction

Star Reduction makes the stars of a stretched image smaller and fainter. It compares the image with a
[starless image](../../reference/glossary.md#starless-image) of it and pulls each star toward the background the
starless image holds there. Where the two agree, as on the sky and the nebula, nothing changes. Two of its four
methods work through the [midtones transfer function](../../reference/glossary.md#midtones-transfer-function). A
[mask](../../reference/glossary.md#mask) on the image's view, such as the small-star protection mask the window
builds, keeps faint stars from vanishing.

**OK** opens the result as a new image, or replaces the image or
[sequence](../../reference/glossary.md#sequence) when you ask, and can open the stars it took away as an image of
their own. The result is recorded as nonlinear.

```figure
image: ../../assets/images/processes/star-operations/star-reduction.webp
alt: >-
  The Star Reduction window. On the left, the Views group with Target image, Starless image, Generator and the
  Generate starless view button; the Small star protection group with its button and check box; the Star reduction
  parameters group with Reduction method at Star Method, a dimmed Stretch factor, Mode at Strong and Iterations at 2;
  the Output options group with its check boxes and name fields; and Show preview. On the right, the preview of a
  stretched nebula with a framed area in its middle. Reset runs along the bottom on the left, OK and Close on the
  right.
caption: >-
  Star Reduction on the stretched Carina master against its starless image, with Star Method, Strong and Iterations
  at 2. The framed area in the preview shows the reduced stars.
pins:
  - {n: 2, x: 2.0, y: 21.1}
  - {n: 3, x: 2.0, y: 47.9}
  - {n: 4, x: 2.0, y: 58.4}
  - {n: 5, x: 2.0, y: 36.5}
  - {n: 6, x: 23.5, y: 66.5}
  - {n: 7, x: 36.6, y: 15.7}
  - {n: 8, x: 78.8, y: 96.5}
```

## Before you start

- Make the starless image from this image after its [stretch](../../reference/glossary.md#stretch), at the same
  stage, with the same size, channels and alignment. [Starnet++](starnet.md) makes one, and the window can start it.
- Check the starless image at 100 % for nebula or galaxy detail it removed. What it lacks there is reduced as if it
  were a star.
- [Debayer](../../reference/glossary.md#debayer) a raw color-filter image first: a mosaic is refused.
- Keep **Create new star-reduced image** on while you tune. **Reduce stars on target image** replaces the image, and
  only its [history](../../reference/glossary.md#history) gives it back.
- For a sequence, keep one starless still image open. Every frame is reduced against it.

## Steps

1. **Open the window.** Make the stretched image the [active image](../../reference/glossary.md#active-image) and
   choose **Process > Star Processing > Star Reduction**. The preview shows the image unchanged, with the frame of a
   [preview area](../../reference/glossary.md#preview-area) in its middle.
2. **Choose the starless image.** Pick it in **Starless image**, which lists only the open images that match the
   target. Without one, choose a **Generator**, press **Generate starless view** and run it: the image it makes is
   selected when it opens.
3. **Choose a method.** Leave **Reduction method** at **Star Method** unless the glow around the stars, or bright
   structure, asks for another.
4. **Set the strength.** With Star or Lightness, raise **Iterations** for more, or choose **Moderate** or **Soft** in
   **Mode** for less. With Transfer or Halo, lower **Stretch factor** for more.
5. **Protect the small stars.** If faint stars vanish in the preview, press **Initialise small star protection** and
   keep the window open until the mask opens as a new image. It becomes the image's mask, and **Enable small star
   protection** is ticked.
6. **Choose what OK makes.** Leave **Create new star-reduced image** on, or tick **Reduce stars on target image** to
   replace the image. **Create reduced-stars-only image** adds an image of the stars taken away.
7. **Check at 100 %.** Zoom the preview into the stars and the faint nebula, and hold ++space++ over it to compare
   with the target image.
8. **Choose OK.** The reduced image opens beside the target, or replaces it, and the reduced stars open as a second
   image. The controls return to their defaults.

## Controls

The preview, its toolbar and the progress window of a sequence are those of every process window: see
[Process Windows](../../interface/process-windows.md). The preview names its two images PREVIEW and TARGET IMAGE.

```controls
window: Star Reduction
controls:
  - name: Target image
    kind: combo
    default: The active image
    options: [The active image, Every other open image or sequence the window can work on]
    effect: Chooses the image whose stars are reduced. It names the new image, carries the mask, and is the image OK replaces when you ask.
    tradeoff: The target and the starless image must show the same scene, aligned pixel for pixel. The window cannot check that.
    persistence: Until the window closes. Choosing another target lists the starless images again, and keeps the chosen one when it still matches.
  - name: Starless image
    kind: combo
    default: "<--NO IMAGE-->"
    options: ["<--NO IMAGE-->"]
    effect: Chooses the starless version of the target that each sample is reduced against.
    range: It lists the open images other than the target that have its width, height, number of channels and color layout. Their pixel types may differ.
    enabled: OK and **Initialise small star protection** wait for a choice here. Closing the chosen image returns the box to `<--NO IMAGE-->`.
    tradeoff: A starless image that is not aligned, or that lost nebula or galaxy detail, reduces that detail as if it were stars.
    scope: The preview, a snapshot and OK, cropped with the target to a preview area. Every frame of a sequence is reduced against this one image, as it is when you press OK.
    persistence: Until the window closes. **Reset** and a finished OK leave it as it is.
    note: It is read in its own pixel type as fractions of full scale and brought to the target's working precision, never to the target's pixel type, so an integer target does not round a floating-point starless image.
  - name: Generator
    kind: combo
    default: actionStarnet
    options: [actionStarnet]
    effect: Chooses the process **Generate starless view** opens. It lists **Starnet++** and every extension that declares a starless output, those ready to run first.
    range: Each item is a generator's name, as **Starnet++**. The tooltip of an item that cannot run gives the reason.
    enabled: When at least one generator is registered. It opens on the first one ready to run.
    tradeoff: How long the generator takes, and how good its starless image is, are the generator's.
    persistence: Until the window closes.
  - name: Generate starless view
    kind: button
    default: ""
    effect: Opens the generator on the target. **Starnet++** opens set for this use, its stars output off and set to the unscreen layer, closing when done, and the starless image it makes is selected in **Starless image**. With another generator, the next matching image that opens is selected.
    enabled: When the chosen generator can run. **Starnet++** needs StarNet2 2.5.3 or newer, set on the **External Tools** page of the settings.
    tradeoff: The generator runs an external tool or a model, and makes a new image.
    scope: It runs nothing of Star Reduction. The image it makes stays open whatever you do here.
    note: >-
      Without a usable generator it reports "No configured starless generator is available.", "The selected starless
      generator is no longer registered." or "Could not launch the selected starless generator."
  - name: Initialise small star protection
    kind: button
    default: ""
    effect: Builds a protection mask from the whole target and starless image, dark on the small stars and light elsewhere. When it is ready it opens as `<target> - star reduced mask`, becomes the target's mask, enabled, not inverted and hidden, and ticks **Enable small star protection**.
    range: The mask does not depend on the method, the strength or the preview area. It averages the channels of each image, spreads each over its own minimum and maximum, takes where the target rises above the starless image, keeps the small structures an 11-pixel median misses, widens them with a cross-shaped dilation, smooths them over 7 by 7 pixels and inverts the result.
    enabled: With a starless image chosen. It is disabled while a mask is built, and does nothing while OK runs.
    tradeoff: It works in the background over the whole image, with no progress window and no Cancel. It costs more than a reduction, with copies of both images and three 32-bit planes of the image's size. Closing the window before the mask is ready discards it.
    scope: Through the target's mask, the preview, a snapshot and OK, as every mask on a view. Black keeps the target, white takes the reduction.
    persistence: >-
      The mask image stays open. It is a one-channel floating-point image, 64-bit for a 64-bit or 32-bit integer
      target and 32-bit otherwise. Its history starts with `Star Reduction: Small-star mask created from '<target>'`.
    note: >-
      Without a starless image it reports "Select a compatible starless image before creating a protection mask." A
      failure to build the mask is reported in a message.
  - name: Enable small star protection
    kind: check
    default: "Off"
    effect: Enables or disables the target's mask, and so the protection, for the preview and OK.
    enabled: It acts on any mask the target has until the window builds its own. After that it acts only on the mask the window built, while that mask is the target's.
    tradeoff: Protected stars keep more of the target. Too wide a protection leaves stars unreduced.
    persistence: It changes the target's mask, which stays as you leave it. The window keeps no setting of its own.
  - name: Reduction method
    kind: combo
    default: Star Method
    options: [Transfer Method, Halo Method, Star Method, Lightness Method]
    effect: Chooses the equation. **Star Method** is the general one. **Halo Method** aims at the glow around stars. **Lightness Method** weighs the reduction by the pixel's lightness, gentler on bright structures. **Transfer Method** reduces through a midtones transfer.
    enabled: Transfer and Halo enable **Stretch factor**. Star and Lightness enable **Mode** and **Iterations**.
    scope: Every channel of the target, in the preview, a snapshot and OK.
    persistence: Until the window closes. **Reset** and a finished OK return it to **Star Method**.
  - name: Stretch factor
    kind: spinbox
    default: "0.20"
    widget: {min: 0.01, max: 0.99, step: 0.01, decimals: 2}
    effect: Sets how strongly Transfer and Halo reduce. Lower reduces more. At 0.50 nothing changes, and above it stars grow brighter.
    range: The midtones balance of equations (5) and (6) is one minus this value, which must lie strictly between 0 and 1.
    enabled: With Transfer or Halo. Its value is also used by **Enable reverse stretch**, with any method, even while the box is disabled.
    tradeoff: Near either end of its range the curve is steep and the change is strong. Every result is held between 0 and 1.
    persistence: Until the window closes. **Reset** and a finished OK return it to its default.
  - name: Mode
    kind: combo
    default: Strong
    options: [Strong, Moderate, Soft]
    effect: Chooses how much of the iterative result is kept. **Strong** keeps it all. **Moderate** gives back some of each star, and **Soft** gives back the most, keeping more of its color and core.
    enabled: With Star or Lightness.
    persistence: Until the window closes. **Reset** and a finished OK return it to **Strong**.
  - name: Iterations
    kind: spinbox
    default: "1"
    widget: {min: 1, max: 3, step: 1}
    effect: Repeats the Star or Lightness step. More reduces more.
    enabled: With Star or Lightness.
    tradeoff: Too many can flatten the cores of bright stars.
    persistence: Until the window closes. **Reset** and a finished OK return it to 1.
  - name: Reduce stars on target image
    kind: check
    default: "Off"
    effect: On, OK replaces the image or the sequence with the reduced one. Ticking it clears **Create new star-reduced image**.
    tradeoff: It changes the target, and only its history gives the image back.
    scope: OK only. The preview and a snapshot never change the target.
    persistence: Until the window closes. **Reset** and a finished OK turn it off.
  - name: Create new star-reduced image
    kind: check
    default: "On"
    effect: On, OK opens the result as a new image, or a new sequence, and leaves the target as it is.
    range: One of the two outputs is always chosen. Clearing both ticks this one again.
    tradeoff: The new image or sequence takes memory, and a sequence disk space.
    persistence: Until the window closes. **Reset** and a finished OK turn it on.
  - name: Image id
    kind: field
    default: "<Auto>"
    effect: Names the new image. `<Auto>`, or nothing, names it after the target with ` - star reduced`. Other text is used as typed, without leading and trailing spaces.
    enabled: With **Create new star-reduced image**.
    range: A name already in use is made unique.
    persistence: Until the window closes. **Reset** and a finished OK return it to `<Auto>`.
  - name: Create reduced-stars-only image
    kind: check
    default: "On"
    effect: On, a finished OK on a still image also opens the stars it took away, beside the target, as `<target> - reduced stars`.
    range: The stars are extracted from the result as the mask weighs it, so a protected area gives none. Equation (7) gives them, or equation (8) with **Enable reverse stretch**.
    enabled: It enables **Enable reverse stretch** and **Stars id**.
    tradeoff: It adds copies of the image and one more image in memory.
    scope: A still image, after OK only. A preview, a snapshot and a sequence make no stars image.
    persistence: >-
      The stars image has the target's pixel type and channels, and its history starts with `Star Reduction:
      Reduced-stars image created from '<target>'`. **Reset** and a finished OK turn the box on.
  - name: Enable reverse stretch
    kind: check
    default: "Off"
    effect: Extracts the stars through the midtones transfer of **Stretch factor**, its inverse after, instead of directly.
    enabled: With **Create reduced-stars-only image**.
    tradeoff: It gives another stars image, meant to follow a Transfer or Halo reduction. It uses **Stretch factor** with every method.
    persistence: Until the window closes. **Reset** and a finished OK turn it off.
  - name: Stars id
    kind: field
    default: "<Auto>"
    effect: Names the stars image. `<Auto>`, or nothing, names it after the target with ` - reduced stars`.
    enabled: With **Create reduced-stars-only image**.
    range: A name already in use is made unique.
    persistence: Until the window closes. **Reset** and a finished OK return it to `<Auto>`.
  - name: Show preview
    kind: check
    default: "On"
    effect: Shows the preview pane, as **Preview** does in every process window. It shows the target unchanged until a starless image is chosen.
    scope: The window only. The preview area, which opens on, crops the target, the starless image and the mask alike. OK always works on the whole image.
    persistence: Until the window closes. **Reset** leaves it as it is.
  - name: OK
    kind: button
    default: ""
    effect: Reduces the whole image, or every frame of the sequence, with the values, the starless image and the mask as they are when you press it, makes the outputs you chose, and returns the controls to their defaults. The window stays open.
    enabled: While a starless image is chosen and no run works.
    tradeoff: Each run makes floating-point working copies of the target and of the starless image, and its time grows with the pixels, the channels and **Iterations**.
    persistence: In place, the history gets one step, **Star Reduction**, described as `Star reduction using <starless image>`. A new image or sequence starts its history with that step, and the target and its history stay as they were.
    note: The starless image, the generator and the masks stay as they are.
  - name: Close
    kind: button
    default: ""
    effect: Closes the window without a run, as in every process window.
    note: The images the window made stay open. A protection mask still being built is discarded.
  - name: Reset
    kind: button
    default: ""
    effect: Returns the method, the strength and the four output options and two names to their defaults.
    range: It leaves the target, the starless image, the generator, **Enable small star protection**, the masks and the images the window made as they are.
```

## How it works

1. When a run is asked for, the window takes the starless image, the mask enabled on the target's view and, for a
   preview, the preview area, to which it crops both images alike.
2. Both images are read as [fractions of full scale](../../reference/glossary.md#fraction-of-full-scale) in floating
   point, 64-bit for a 64-bit or 32-bit integer target and 32-bit otherwise. A sample that is not a number becomes 0,
   and every sample is held between 0 and 1.
3. Each sample of each channel is reduced against the starless sample at the same place, by the equation of the
   method. The result is held between 0 and 1 and written in the target's
   [pixel type](../../reference/glossary.md#pixel-type), rounded to the nearest code.
4. The mask then blends the result with the target. After OK on a still image, the stars image is extracted from
   that blended result, and opens once the result is shown.
5. The result is recorded as nonlinear with the operation `star-reduction`. It keeps the target's
   [header cards](../../reference/glossary.md#header-card), acquisition data, exposure included, and
   [plate solution](../../reference/glossary.md#plate-solution), takes nothing from the starless image, and its
   [lineage](../../reference/glossary.md#lineage) names the method, the strength, the iterations, the stretch factor
   and the starless image.

### Model

Let \(t\) be a sample of the target and \(s\) the starless sample at the same place, both between 0 and 1. The Star
Method takes, for the first iteration and each further one,

\[ v_1 = t^2 + (1 - t)\,s, \qquad v_{k+1} = v_k^2 + (1 - v_k)\,s \tag{1} \]

so \(v_1 - s = t\,(t - s)\): the part of a sample above the starless image is multiplied by the sample itself. A
faint star wing loses most of it and a white core keeps it all. Where \(t = s\), nothing changes. The Lightness
Method uses one weight \(l\) for the whole pixel, the CIE L* of the target pixel divided by 100, or \(t\) itself in
a grayscale image:

\[ v_1 = l\,t + (1 - l)\,s, \qquad v_{k+1} = l\,v_k + (1 - l)\,s \tag{2} \]

The red, green and blue are read as linear when the image states that it is linear, and as sRGB otherwise. After
the last iteration, **Strong** keeps \(v\), **Moderate** keeps \(v'\) and **Soft** keeps \(v''\):

\[ v' = \max\bigl(v,\ t\,v + v\,(1 - v)\bigr) \tag{3} \]

\[ v'' = \tfrac12 \bigl[ (1 - t)\,t + (1 + t)\,v' \bigr] \tag{4} \]

Let \(M_m\) be the midtones transfer function at the balance \(m\), and \(f\) the value of **Stretch factor**. The
Transfer Method gives

\[ r = 1 - \frac{1 - M_{1-f}(t)}{1 - M_{1-f}(s)}\,(1 - s) \tag{5} \]

and the Halo Method

\[ r = t - \tfrac12 (1 + t) \left[ \frac{1 - M_{1-f}(t)}{1 - M_{1-f}(s)} - \frac{1 - t}{1 - s} \right]
\left(1 - \tfrac{s}{2}\right) \tag{6} \]

With \(f = 0.5\), \(M_{0.5}\) changes nothing and both give \(r = t\). A ratio whose denominator is zero counts as 0,
and every result is held between 0 and 1. From the reduced sample \(r\), as the mask weighs it, the stars image is

\[ \frac{r - s}{1 - s} \tag{7} \]

or 0 where \(s = 1\). With **Enable reverse stretch** it is instead, the ratio held between 0 and 1,

\[ M_{1-f}\!\left( \frac{M_f(r) - M_f(s)}{1 - M_f(s)} \right) \tag{8} \]

### Boundaries

- Without a starless image the preview shows the target, and OK is refused with "select a starless image of the
  target to reduce against". One that no longer matches when the run reads it is refused with "the starless image
  does not have the target's size and channels".
- A raw mosaic is refused with "star reduction has no meaning on an unresolved colour filter array", and the
  Lightness Method on an image with an alpha channel with "a lightness reduction is defined for monochrome or RGB
  samples".
- A sample that is not a number, or one outside 0 to 1, does not stop the run: it is read as 0, or as the nearer end.
- A run stopped by **Cancel**, or by closing the window, stops between blocks of rows. It writes nothing, opens no
  image and shows no error, and its [temporary sequence](../../reference/glossary.md#temporary-sequence) is removed. A
  run refused for lack of memory, for example, writes nothing and says why.
- When the starless image is renamed, closed or changed, the list follows it. A run already started keeps the image
  it was given.

### Worked example

The target is the Carina [master](../../reference/glossary.md#master) after Histogram Transformation with
**Shadows** and **Midtones** at 0.02, and the starless image is the one Starnet++ made from it at its defaults.
Choose **Star Method**, **Strong** and **Iterations** 2. For the pictures, **Reduce stars on target image** was on and
**Create reduced-stars-only image** off, so the view itself shows the result.

On the sky, where both images hold about 0.28, equation (1) changes nothing. A star's wing at 0.70 over a starless
0.28 becomes 0.574, then 0.449: the part above the sky falls from 0.42 to 0.17. A core at 0.95 becomes 0.86 and a
white core stays white, so each star keeps its center and loses its skirt. With three iterations, a sample of 0.82
over a starless 0.43 passes through 0.750 and 0.670 to 0.591.

```figure
images:
  - image: ../../assets/images/processes/star-operations/star-reduction-before.webp
    alt: A crop at 100 % of the middle of the stretched nebula, dense with stars, each with a soft glow around a bright core.
    label: Before
  - image: ../../assets/images/processes/star-operations/star-reduction-after.webp
    alt: The same crop after the reduction, with the same stars smaller and dimmer, their glows mostly gone, and the nebula and sky unchanged.
    label: After
caption: >-
  The middle of the stretched Carina master at 100 %, before and after Star Reduction with Star Method, Strong and
  Iterations at 2. Both crops are shown in the Linear display mode.
```

## Starting points

| Situation | Start | Then adjust |
| --- | --- | --- |
| A general reduction | **Star Method**, **Strong**, 1 iteration, a new image | **Iterations** for more, then **Mode** for less. |
| Stars are fine, their glow is too strong | **Halo Method**, **Stretch factor** at its default | Lower **Stretch factor** in small steps at 100 %, and compare with **Star Method**. |
| Bright colored stars lose their color | **Lightness Method**, **Moderate** or **Soft**, 1 iteration | **Mode** toward **Strong** before **Iterations**. |
| Faint stars vanish | **Initialise small star protection** | Look at the mask itself, then the reduction again. |

## When it goes wrong

!!! danger "A starless image of another state gives a wrong result without a warning"
    The window checks the size, the channels and the color layout, not that the two images show the same thing.
    A starless image that is shifted, cropped differently, stretched differently or missing nebula detail is reduced
    against all the same. Make it from this image, at this stage, and check it at 100 %.

!!! warning "A Stretch factor above 0.50 makes stars brighter"
    With the Transfer and Halo methods, lower values reduce more. At 0.50 nothing changes, and above it stars grow.

OK is disabled
:   **Cause.** **Starless image** reads `<--NO IMAGE-->`, or a run is working.
:   **Fix.** Choose a starless image, or wait for the run to end.

Generate starless view is disabled, and its tooltip reads "Configure StarNet2 CLI 2.5.3 or newer in Settings > External Tools to generate a starless view."
:   **Cause.** StarNet2 is not set, or not in a version the window can use, and no other generator is ready.
:   **Fix.** Set StarNet2 on the **External Tools** page of the settings, or make the starless image another way.

The starless image is not in the list
:   **Cause.** It is the target itself, or differs from it in width, height, channels or color layout.
:   **Fix.** Make or open a starless image of the same image, aligned and cropped alike.

"the starless image does not have the target's size and channels"
:   **Cause.** The chosen image changed after you chose it.
:   **Fix.** Choose a matching starless image again.

"star reduction has no meaning on an unresolved colour filter array"
:   **Cause.** The target is a raw mosaic.
:   **Fix.** Debayer the target and make the starless image from the debayered one.

Faint small stars disappear
:   **Cause.** No protection mask is enabled, it is inverted, or it is not dark on those stars.
:   **Fix.** Look at the target's mask, keep it enabled and not inverted, or build it again.

The protection mask never appears
:   **Cause.** The window closed before the mask was ready, or building it failed and said so.
:   **Fix.** Press the button again and keep the window open until the mask opens.

The reduced-stars image is missing
:   **Cause.** The option was off, the target is a sequence, or the run was a preview, a snapshot or a stopped one.
:   **Fix.** Tick **Create reduced-stars-only image** and run OK to its end on a still image.

## Related

- [Starnet++](starnet.md), to make the starless image this window needs.
- [Starmask Recombination](starmask-recombination.md), to put a stars image back on a starless image.
- [Star Mask](star-mask.md) and [Starnet++ Star Mask](starnet-star-mask.md), to make a star mask of your own.
- [Masks](../../interface/masks.md), to see, invert or remove the protection mask.
- [Image History](../../interface/image-history.md), to undo a reduction made on the target.
- [Process Windows](../../interface/process-windows.md), for the preview and what every process window shares.

## Credits

- The [StarNet command-line tool reference](https://starnetastro.com/documentation/starnet/command-line-tool/)
  describes the StarNet2 program **Generate starless view** starts when **Starnet++** is the generator. Star Reduction
  itself does not call StarNet2.
