---
kind: process
stage: color
menu: Process > Color Spaces > NBRGB Combination
summary: >-
  Adds the line signal of up to three narrowband masters to the red, green and blue of a color image of their size,
  after taking out the continuum their bandwidths imply, as a new image.
data_in: linear
data_out: linear
destructive: false
preview: real-time
sequences: false
masks: post-blend
external: none
glance:
  enabled: >-
    An RGB still image is active, no NBRGB Combination window is open on it yet, and no window that needs the image
    for itself is open on it. Apply waits for a narrowband image.
  inputs: >-
    One RGB still image, up to three grayscale still images of exactly its width and height in any pixel type, and an
    optional mask on the color image's view.
  outputs: >-
    One new RGB image beside the color image, in its size and pixel type. The color image and the narrowband images
    stay as they are, and no file is written.
  state: >-
    A linear color master and linear narrowband masters of one target, registered and cropped alike, with gradients
    removed and their levels brought together.
derived: false
---
# NBRGB Combination

NBRGB Combination adds the line signal of up to three narrowband [masters](../../reference/glossary.md#master) to the
red, green and blue channels of a color image of their size. Each narrowband image first loses the
[continuum](../../reference/glossary.md#continuum) that its bandwidth, against the width of the color image's filters,
says it shares with the color channel. What remains is centered on its median, scaled and added to the channel. A
straight line fitted from it to that sum gives a second answer, and each pixel keeps the brighter of the two.

**Apply** opens the result as a new image beside the color image, in its size and
[pixel type](../../reference/glossary.md#pixel-type). The color image and the narrowband images stay as they are.

```figure
image: ../../assets/images/processes/color-composition/nbrgb-combination.webp
alt: >-
  The NBRGB Combination window. On the left, a histogram over its toolbar, then the Sources group with RGB naming the
  color image and RGB bandwidth at 100.00, the Narrowband Channels group with the Ha master in Red at 7.00 and 1.20 and
  no image in Green and Blue, and the Options group with Reject low, Reject high, Non Linear rescale and Preview at
  NBRGB. On the right, the preview shows the field in gray with a faint teal glow and many stars, and a small framed
  area labelled NBRGB preview at its middle. Reset, Close
  and Apply run along the bottom.
caption: >-
  NBRGB Combination on a color image made from the SII and OIII masters, with the Ha master in Red at the default
  bandwidths and scale: the framed area in the middle of the preview shows the line signal of Ha added to red.
pins:
  - {n: 1, x: 9.5, y: 43.9}
  - {n: 2, x: 2.6, y: 58.8}
  - {n: 3, x: 2.6, y: 47.3}
  - {n: 4, x: 37.9, y: 55.9}
  - {n: 5, x: 2.6, y: 84.3}
  - {n: 6, x: 58.5, y: 19.1}
```

## Before you start

- Register the narrowband masters to the color image and crop them alike. The image boxes compare width and height
  only: nothing matches stars, world coordinates, scale, rotation or star profiles.
- Work on [linear images](../../reference/glossary.md#linear-image) with their gradients removed and their levels
  brought together. Equation (1) means something only while every sample is proportional to light.
- Know your filters' effective widths in nanometres. The window reads none from the files, takes one width for the
  three broadband filters, and needs each narrowband width below it.
- The color image is an RGB still image, and each narrowband image a grayscale still image of its width and height,
  in any pixel type. The menu command is greyed while a grayscale image is active, and a
  [sequence](../../reference/glossary.md#sequence) is refused.
- A [mask](../../reference/glossary.md#mask) on the color image's view blends the result with the color image, in
  the preview too.
- The fit gives each channel one scale and one offset. It does not replace registration, gradient removal or color
  calibration.

## Steps

1. **Open the window.** Make the color image the [active image](../../reference/glossary.md#active-image) and choose
   **Process > Color Spaces > NBRGB Combination**. **RGB** names it, the three image boxes read `<--NO IMAGE-->`, and
   **Apply** is dimmed.
2. **Choose the narrowband masters.** Choose the Ha master in **Red**, and an OIII master in **Green** or **Blue** if
   you have one. **Apply** enables, and the framed area in the middle of the preview shows the combination.
3. **Enter the bandwidths.** Type the width of the broadband filters in **RGB bandwidth**, and the width of each
   narrowband filter in its row's **Bandwidth**.
4. **Set the scale.** Raise a row's **Scale** for more of its line signal, or lower it for less, one channel at a
   time.
5. **Check the inputs.** Choose **NBr**, **NBg** or **NBb** in **Preview** to see a narrowband master in the preview,
   and hold ++space++ to compare it with the color image. Return to **NBRGB**.
6. **Check the whole image.** Untick **Preview area** in the preview's toolbar, or move the frame over background,
   nebula and stars. The median and the fit are measured on what the preview computes.
7. **Apply.** Choose **Apply**. A new image, `<color image> - NBRGB`, opens as a tab beside the color image, with the
   line signal in its channels.

## Controls

The window has no **Preview** check box: the preview is always on, and opens with a 512 × 512
[preview area](../../reference/glossary.md#preview-area) in the middle of the image. Its histogram, the histogram's
toolbar and the preview's toolbar are those of every process window: see
[Process Windows](../../interface/process-windows.md).

```controls
window: NBRGB Combination
controls:
  - name: RGB
    kind: combo
    default: The active image
    options: [The active image, Every other open RGB still image]
    effect: Chooses the color image, which gives the channels the narrowband images are added to, the size, pixel type, name and header of the result, and the mask. It is never changed.
    range: It lists the open still images of three color channels, never a grayscale image or a sequence.
    enabled: Disabled while a run works.
    tradeoff: Nothing checks that the narrowband images are registered to it.
    persistence: Until the window closes. **Reset** leaves it. Choosing another returns the three image boxes to `<--NO IMAGE-->` and fills their lists again.
  - name: RGB bandwidth
    kind: spinbox
    default: "100.00"
    widget: {min: 0.1, max: 1000, step: 1, decimals: 2}
    effect: Sets the width, in nanometres, of the broadband filters the color image was taken through, one value for all three. Higher takes less of each color channel out of its narrowband image, lower takes more, by equation (1).
    range: It must exceed the **Bandwidth** of every row that names an image, or **Apply** stays dimmed and the preview shows the color image unchanged.
    tradeoff: A wrong width biases every selected channel. A width close to a narrowband width divides by a small number, which multiplies noise and gradients.
    scope: The preview and Apply, for every channel with a narrowband image.
    persistence: Until the window closes. **Reset** and a finished Apply return it to 100.
  - name: Red
    object: redImageSelector
    kind: combo
    default: "<--NO IMAGE-->"
    options: ["<--NO IMAGE-->"]
    effect: Chooses the narrowband image added to the red channel, such as an Ha master. At `<--NO IMAGE-->` red is copied from the color image unchanged.
    range: After `<--NO IMAGE-->` the list holds every open grayscale still image of exactly the color image's width and height, in any pixel type. The tooltip of each item gives its file, size, channels, pixel type and memory.
    enabled: The list follows the views as they open, close, change or are renamed. A view that closes or no longer qualifies returns the box to `<--NO IMAGE-->`.
    tradeoff: Nothing checks its registration, filter, exposure or star size. A master off by a pixel gives red halos and doubled stars. Each image chosen adds a working copy of its size.
    scope: The image its view shows when the preview or **Apply** asks for it, read as a fraction of its own full scale, and cropped to a preview area as the color image is.
    persistence: Kept by **Reset** and **Apply** while the image stays open and qualifies. Nothing is kept after the window closes.
  - name: Red
    object: redBandwidth
    kind: spinbox
    default: "7.00"
    widget: {min: 0.1, max: 500, step: 0.5, decimals: 2}
    effect: Sets the width, in nanometres, of the red row's narrowband filter. Higher takes more of the red channel out of the narrowband image as continuum, lower takes less, by equation (1).
    range: It must stay below **RGB bandwidth**. It is read only while the row names an image.
    tradeoff: Near **RGB bandwidth** the small denominator of equation (1) amplifies noise.
    scope: The preview and Apply, red only.
    persistence: Until the window closes. **Reset** and a finished Apply return it to 7.
  - name: Red
    object: redScale
    kind: spinbox
    default: "1.20"
    widget: {min: 0, max: 10, step: 0.05, decimals: 2}
    effect: Sets how strongly the line signal is added to red, by equation (2). Higher adds more of the narrowband structure around its median, lower adds less. At 0 the boost is gone, but the fitted line of equation (3) can still raise red.
    range: It is read only while the row names an image.
    tradeoff: High values clip highlights and amplify noise and registration errors.
    scope: The preview and Apply, red only.
    persistence: Until the window closes. **Reset** and a finished Apply return it to 1.20.
  - name: Green
    object: greenImageSelector
    kind: combo
    default: "<--NO IMAGE-->"
    options: ["<--NO IMAGE-->"]
    effect: Chooses the narrowband image added to the green channel, such as an OIII master. At `<--NO IMAGE-->` green is copied unchanged.
    range: The same list as the red row. One image may be chosen in several rows.
    enabled: As the red row.
    tradeoff: A master off by a pixel gives green halos and doubled stars, and adds a working copy of its size.
    scope: As the red row, for green.
    persistence: As the red row.
  - name: Green
    object: greenBandwidth
    kind: spinbox
    default: "8.50"
    widget: {min: 0.1, max: 500, step: 0.5, decimals: 2}
    effect: Sets the width, in nanometres, of the green row's narrowband filter, as the red row's does.
    range: It must stay below **RGB bandwidth**. It is read only while the row names an image.
    tradeoff: Near **RGB bandwidth** it amplifies noise.
    scope: The preview and Apply, green only.
    persistence: Until the window closes. **Reset** and a finished Apply return it to 8.50.
  - name: Green
    object: greenScale
    kind: spinbox
    default: "1.20"
    widget: {min: 0, max: 10, step: 0.05, decimals: 2}
    effect: Sets how strongly the line signal is added to green, as the red row's does.
    range: It is read only while the row names an image.
    tradeoff: High values clip highlights and shift the color.
    scope: The preview and Apply, green only.
    persistence: Until the window closes. **Reset** and a finished Apply return it to 1.20.
  - name: Blue
    object: blueImageSelector
    kind: combo
    default: "<--NO IMAGE-->"
    options: ["<--NO IMAGE-->"]
    effect: Chooses the narrowband image added to the blue channel. At `<--NO IMAGE-->` blue is copied unchanged.
    range: The same list as the red row.
    enabled: As the red row.
    tradeoff: A master off by a pixel gives blue halos and doubled stars, and adds a working copy of its size.
    scope: As the red row, for blue.
    persistence: As the red row.
  - name: Blue
    object: blueBandwidth
    kind: spinbox
    default: "8.50"
    widget: {min: 0.1, max: 500, step: 0.5, decimals: 2}
    effect: Sets the width, in nanometres, of the blue row's narrowband filter, as the red row's does.
    range: It must stay below **RGB bandwidth**. It is read only while the row names an image.
    tradeoff: Near **RGB bandwidth** it amplifies noise.
    scope: The preview and Apply, blue only.
    persistence: Until the window closes. **Reset** and a finished Apply return it to 8.50.
  - name: Blue
    object: blueScale
    kind: spinbox
    default: "1.20"
    widget: {min: 0, max: 10, step: 0.05, decimals: 2}
    effect: Sets how strongly the line signal is added to blue, as the red row's does.
    range: It is read only while the row names an image.
    tradeoff: High values clip highlights and amplify the noise of blue.
    scope: The preview and Apply, blue only.
    persistence: Until the window closes. **Reset** and a finished Apply return it to 1.20.
  - name: Reject low
    kind: spinbox
    default: "0.000"
    widget: {min: 0, max: 1, step: 0.01, decimals: 3}
    effect: Sets the lowest boosted value a pixel may have to take part in the fit of equation (3). Higher leaves more of the dark pixels out of the fit.
    range: It must stay below **Reject high**, or **Apply** stays dimmed. A pixel whose line or boosted value is 0 never takes part.
    tradeoff: Set too high, it leaves fewer than three pixels, and the fit is dropped without a message.
    scope: The fit of every selected channel, in the preview and Apply.
    persistence: Until the window closes. **Reset** and a finished Apply return it to 0.
  - name: Reject high
    kind: spinbox
    default: "0.920"
    widget: {min: 0, max: 1, step: 0.01, decimals: 3}
    effect: Sets the highest boosted value a pixel may have to take part in the fit. Lower leaves more of the bright pixels, such as star cores, out of the fit.
    range: It must stay above **Reject low**, or **Apply** stays dimmed.
    tradeoff: Too low leaves out structure the fit should follow. Too high lets clipped pixels pull it.
    scope: The fit of every selected channel, in the preview and Apply.
    persistence: Until the window closes. **Reset** and a finished Apply return it to 0.92.
  - name: Non Linear rescale
    kind: check
    default: "Off"
    effect: Spreads the line, the boosted channel and the result each over 0 to 1 by its own minimum and maximum, before each is held between 0 and 1. Off, each is only held.
    range: It does not make nonlinear data linear, and it is not a display stretch.
    tradeoff: It loses the levels of the channel and of the line, lets one outlier set the scale, and changes the color balance. It adds three passes over each selected channel.
    scope: Every selected channel, in the preview and Apply, before the mask. Not the previews of **NBr**, **NBg** and **NBb**.
    persistence: Until the window closes. **Reset** and a finished Apply turn it off. The history adds `, rescaled` while it is on, and it does not change what the result is recorded as.
  - name: Preview
    kind: combo
    default: NBRGB
    options: [NBRGB, RGB, NBr, NBg, NBb]
    effect: Chooses what the preview shows. **NBRGB** shows the combination and **RGB** the color image. **NBr**, **NBg** and **NBb** show the red, green or blue row's narrowband image as a gray color image, to check its registration.
    range: A row that names no image shows the color image instead.
    tradeoff: A mask on the view still blends the input shown with the color image, so it can look mixed.
    scope: The preview and **Take snapshot** only. **Apply** always writes the combination.
    persistence: Until the window closes. **Reset** and a finished Apply return it to **NBRGB**.
  - name: Reset
    kind: button
    default: ""
    effect: Returns **RGB bandwidth**, the bandwidths and scales of the three rows, **Reject low**, **Reject high**, **Non Linear rescale** and **Preview** to their defaults.
    range: It leaves **RGB**, the three image boxes, the preview area and the histogram's settings as they are.
    enabled: Also while a run works. The run keeps the values it started with.
  - name: Apply
    kind: button
    default: ""
    effect: Combines the color image with the chosen narrowband images as the window stands, and opens the result as a new image, a tab beside the color image named `<color image> - NBRGB`. The values then return to their defaults, and the image boxes keep their choices.
    enabled: While **RGB** names an RGB still image, a row names an image of its size, every such row's bandwidth is below **RGB bandwidth**, and **Reject high** is above **Reject low**. During a run **RGB**, **Preview area** and **Take snapshot** are disabled, the other controls, **Reset** and **Close** are not, and a second press does nothing.
    tradeoff: There is no progress window and no **Cancel**. The line, its median, the boost and the fit run on one thread, and only the copy and the final write are spread over the processor's threads. The run holds five working planes of the image's size, a copy of each narrowband image and the new image.
    scope: The whole image, through the mask on the color image's view. A preview area does not limit it.
    persistence: "The new image's history starts with a step that reads like `Created NBRGB combination from RGB (100.0 nm) and narrowband channels R: 'Ha' 7.0 nm x1.20; fit 0.000 to 0.920`, and shows the color image's steps above it as inherited lineage. The result is recorded as linear when the color image and every chosen narrowband image are declared linear, as nonlinear when one is declared nonlinear, and otherwise its state is not stated, with the operation `nbrgb-combination`. No continuum image, fit coefficients, mask, preset, report or file is written."
    note: "The header is built from the color image and the chosen narrowband images. The exposure is the light they hold together: the sum, over the distinct acquisitions, of each one's `TOTEXP`, else its `LIVETIME`, else its exposure times its `STACKCNT`, else `NCOMBINE`, else one. Images that state the same exposure start are one acquisition and count once, with the largest integration and frame count any of them states. An image that states no start counts on its own. Their `EXPOSURE` and `EXPTIME` cards are dropped, and the result states its own `EXPTIME` and `TOTEXP`, `LIVETIME` when an image states one, and `STACKCNT` and `NCOMBINE` summed the same way, each when an image states it. The observation start is the earliest, with its `DATE-OBS`, `DATE-LOC`, `MJD-OBS`, `DATE-BEG`, `MJD-BEG` and `TIME-OBS` together. The object, instrument, telescope, observer, filter, sensor temperature, gain and binning are kept when every image states the same, and so is every other card the color image shares with all of them. The color image's pointing cards `RA`, `DEC`, `OBJCTRA`, `OBJCTDEC`, `OBJRA`, `OBJDEC`, `TELRA` and `TELDEC`, its WCS cards and its plate solution are kept, without a check that the narrowband images share its grid. No processing-state or file identity card of an image is kept."
```

## How it works

1. The color image and each chosen narrowband image are read as
   [fractions of full scale](../../reference/glossary.md#fraction-of-full-scale), in 32-bit floating point, or 64-bit
   when the color image is 64-bit floating point or a 32-bit integer.
2. For each row that names an image, equation (1) takes out of the narrowband image the continuum its color channel
   stands for, and equation (2) adds what remains, centered on its median and scaled, to that channel.
3. A straight line fitted from the line signal to the boosted channel gives a second answer, and each pixel keeps the
   brighter of the two by equation (3). A channel without a narrowband image is copied unchanged.
4. A mask on the color image's view then blends the result with the color image.
5. The result is written once in the color image's pixel type, rounded to the nearest code for an integer type, and
   opens as a new image.

### Model

For one channel, take \(R\) its sample in the color image, \(N\) the narrowband sample, \(B\) the value of **RGB
bandwidth** and \(B_c\) the row's **Bandwidth**. With \(q = B_c / B\), the line signal is

\[ C = \frac{N - q\,R}{1 - q} \tag{1} \]

held between 0 and 1. Equation (1) treats both filters as rectangular passbands, and needs \(1 - q\) above
\(10^{-6}\). With \(\tilde C\) the median of \(C\) over the image and \(s\) the row's **Scale**, the boosted channel is

\[ G = R + s\,(C - \tilde C) \tag{2} \]

held between 0 and 1. A straight line \(M = a\,C + b\) is fitted to \(G\) by least squares, over the pixels where
\(C\) and \(G\) are not 0 and \(G\) lies from **Reject low** to **Reject high**, both included. Without three such
pixels, or when their \(C\) spreads by less than \(10^{-8}\), \(M = C\). The channel becomes

\[ O = \min\bigl(1,\ \max(G,\ M)\bigr) \tag{3} \]

With **Non Linear rescale** on, \(C\), \(G\) and \(O\) are each first spread over 0 to 1 by their own minimum and
maximum, as \((x - x_{min}) / (x_{max} - x_{min})\).

### Boundaries

- A pixel where a sample of the color image or of a chosen narrowband image is not a finite number, or that the color
  image marks as invalid, keeps the color image's samples and takes no part in the median or the fit. When no pixel
  is left, the run stops.
- With a preview area, the median, the fit and the rescale are measured on the area alone, so the preview can differ
  from what **Apply** writes over the whole image.
- Every stage is held between 0 and 1, so a floating-point sample outside that range in a combined channel is
  clipped. The channels without a narrowband image keep theirs.

### Worked example

The guide's data holds no broadband image of the Eagle Nebula, so the example's color image stands in for one. It is
Channel Composition in **RGB** of the SII master in **Red** and the OIII master in **Green** and **Blue**, with
**Linear Match** on: 3008 × 3004 pixels in 32-bit floating point, linear, with a red median of 0.0364. Choose the Ha
master, whose median is 0.0378, in **Red**, and keep the defaults. Then \(q = 0.07\), the median of equation (1) is
0.0379, and the fit over nine million pixels gives \(a = 1.77\) and \(b = -0.0305\). Where the nebula reads 0.0459 in
Ha and 0.0364 in red, equation (2) gives 0.0469 and the fitted line 0.0519, so red becomes 0.0519. Where the
background reads 0.0375 in Ha and 0.0367 in red, red becomes 0.0362. Green and blue are unchanged.

The new image, `RGB - NBRGB`, states `EXPTIME` and `TOTEXP` 19200, the color image's 11700 seconds and the Ha
master's 7500, `STACKCNT` 64, and the Ha master's `DATE-OBS`, which is the earliest.

```figure
images:
  - image: ../../assets/images/processes/color-composition/nbrgb-combination-before.webp
    alt: The whole color image made from the SII and OIII masters, a gray background with many stars and a faint teal glow at the middle.
    label: Before
  - image: ../../assets/images/processes/color-composition/nbrgb-combination-after.webp
    alt: The whole combined image, the same field with the clouds and pillars of the Eagle Nebula in red over the gray background.
    label: After
caption: >-
  The color image before and after Apply with the Ha master in Red at the defaults. Both are the whole frame in the
  view's Autostretch with Linked channels on, so that one stretch shows the three channels alike.
```

The fit needs three pixels. With red at 0.20, 0.40 and 0.60, a narrowband image at 0.326, 0.562 and 0.798 and a red
**Bandwidth** of 10, equation (1) gives 0.34, 0.58 and 0.82, and equation (2) gives 0, 0.40 and 0.888. The 0 leaves
the fit, so \(M = C\), and red becomes 0.34, 0.58 and 0.888.

## Starting points

| Situation | Start | Then adjust |
| --- | --- | --- |
| A broadband RGB master and an Ha master | Ha in **Red**, your filters' effective widths, **Scale** at its default | **Scale** of **Red** in small steps. |
| RGB, Ha and OIII masters | Ha in **Red**, OIII in **Green** and **Blue** | One row's **Scale** at a time. |
| Bright cores clip or pull the color | **Scale** toward 1 | **Reject high** lower, so clipped pixels stay out of the fit. |
| A first look at registration | **NBr** in **Preview** | Hold ++space++ over stars, then return to **NBRGB**. |

## When it goes wrong

!!! warning "The window reads no filter widths"
    The bandwidths are yours. The window cannot tell which filter a master was taken through, nor whether it sits in
    the right row.

!!! warning "Non Linear rescale changes the data three times"
    It spreads the line, the boosted channel and the result each by its own minimum and maximum. It changes the
    samples themselves, and does more than prevent clipping. Leave it off for linear masters.

!!! warning "Apply shows no progress and has no Cancel"
    The run works in the background. Closing the window stops it and nothing opens, so wait for the new image. A
    change made during the run reaches the next one only.

Apply stays dimmed
:   **Cause.** No row names an image, a chosen row's **Bandwidth** is not below **RGB bandwidth**, **Reject high** is
    not above **Reject low**, or a chosen image was closed.
:   **Fix.** Choose a master again, lower the narrowband width or raise **RGB bandwidth**, and order the two limits.

A master is not in the Red, Green or Blue list
:   **Cause.** It is a color image or a sequence, its width or height differs from the color image's, or it is not
    open.
:   **Fix.** Take one channel out of a color image, or register and crop the master to the color image's grid.

The preview shows the color image unchanged
:   **Cause.** **Apply** is still dimmed, or **Preview** shows **RGB** or a row that names no image.
:   **Fix.** Correct what keeps **Apply** dimmed, and choose **NBRGB** in **Preview**.

NBr, NBg or NBb looks mixed with the color image
:   **Cause.** The mask on the color image's view blends whatever the preview shows.
:   **Fix.** Disable the mask while you check the inputs, and enable it again before **Apply**.

The fit seems to have no effect
:   **Cause.** Fewer than three pixels lie between **Reject low** and **Reject high**, or the line signal is nearly
    constant, so the line is used as it is.
:   **Fix.** Widen the two limits, and check the narrowband master in **NBr**, **NBg** or **NBb**.

Noise, gradients or clipped stars grow strongly
:   **Cause.** **Scale** is high, a narrowband width is close to **RGB bandwidth**, **Non Linear rescale** is on, or
    the inputs still carry gradients.
:   **Fix.** Press **Reset**, check the widths, remove gradients first, and raise **Scale** in small steps.

Doubled stars or colored rims
:   **Cause.** The masters have one size but are not registered to each other, or their stars differ in size.
:   **Fix.** Register and crop them to one grid, and match their star sizes, before you combine them.

A snapshot, or the result of Apply, differs from the preview
:   **Cause.** **Preview** shows an input rather than **NBRGB**, or a preview area measured the median and the fit on
    itself alone.
:   **Fix.** Choose **NBRGB** and untick **Preview area** for a last look before **Apply** or **Take snapshot**.

## Related

- [Channel Composition](channel-composition.md), to compose a color image from red, green and blue masters.
- [Image Registration](../registration-alignment/image-registration.md), to put every master on one grid first.
- [Linear Match](../color-calibration/linear-match.md), to bring the levels of the masters together before.
- [NB Color Mapper](nb-color-mapper.md), to give narrowband masters hues of your choice and blend them.
- [Narrowband Palette Composer](narrowband-palette-composer.md), to map narrowband masters to fixed palettes.
- [Process Windows](../../interface/process-windows.md), for the preview, the histogram toolbar and what every
  process window shares.
