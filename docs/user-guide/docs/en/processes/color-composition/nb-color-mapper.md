---
kind: process
stage: stretch
menu: Process > Color Spaces > NB Color Mapper
summary: >-
  Gives each narrowband master, or a color image, a stretch and a hue of its own and screen-blends them into one new
  color image, with an optional background neutralization and output stretch.
data_in: linear
data_out: nonlinear
destructive: false
preview: real-time
sequences: false
masks: post-blend
external: none
glance:
  enabled: >-
    A still image is active, no NB Color Mapper window is open on it yet, and no window that needs the image for
    itself is open on it. Apply waits for every layer's image to be open and of one size.
  inputs: >-
    Up to 64 layers, each an open grayscale or RGB still image of the first layer's width and height, in any pixel
    type. One image may serve several layers. An optional mask on the active image's view.
  outputs: >-
    One new image beside the active image, RGB or grayscale, in 32-bit floating point, or 64-bit when a layer is 64-bit
    floating point or a 32-bit integer, and on request one more image for each included layer. The sources stay as
    they are, and no file is written.
  state: >-
    Registered linear narrowband masters of one target, cropped alike, with gradients removed and their levels
    brought together.
derived: true
---
# NB Color Mapper

NB Color Mapper turns narrowband [masters](../../reference/glossary.md#master) into one color image with colors you
choose. Each master is a layer with its own [black point](../../reference/glossary.md#black-point) and
[midtones](../../reference/glossary.md#midtones) balance. A grayscale layer takes a hue and a saturation of
[HSV](../../reference/glossary.md#hsv), and can first be modified by another layer, while an RGB layer is used as it
is. The layers are then screen-blended, like light added through slides. A background neutralization and an output
[stretch](../../reference/glossary.md#stretch) can follow. It is a free mapping of colors, not a fixed palette or a
spectral model.

**Apply** opens the result as a new image beside the [active image](../../reference/glossary.md#active-image),
recorded as nonlinear. The sources stay as they are.

```figure
image: ../../assets/images/processes/color-composition/nb-color-mapper.webp
alt: >-
  The NB Color Mapper window. On the left, under Histogram Controls, a histogram of three peaks close together over
  its toolbar, then Midtones at 0.020 and Blackpoint at 0.036, and the Layers table with two rows: the
  Ha master at H 0 / S 1.00 and the OIII master at H 180 / S 1.00, the second selected. On the right, the preview
  shows the Eagle Nebula in red over a teal background with many stars. Reset, Close and Apply run along the
  bottom.
caption: >-
  NB Color Mapper with the Ha master in red and the OIII master in cyan, each stretched on its own, the background
  neutralized, and the preview in the Linear display mode under Preview STF.
pins:
  - {n: 1, x: 62.0, y: 25.5}
  - {n: 2, x: 2.6, y: 36.6}
  - {n: 4, x: 2.6, y: 56.4}
```

## Before you start

- Register the masters to one grid and crop them alike, against the first layer's image.
- Remove the gradients of each master and bring very different levels together first. The window stretches each
  layer and can neutralize the background, but knows nothing of exposures, filters or bandwidths.
- Decide whether **Apply** writes a stretched image. **Preview STF** stretches only the preview's samples, and the
  view's [Autostretch](../../reference/glossary.md#autostretch) only displays them.
- A [mask](../../reference/glossary.md#mask) on the active image's view blends the active image with the whole
  result, after every other step, and never weights one layer. Disable it while you choose colors.
- Every layer is a still image. A [sequence](../../reference/glossary.md#sequence) is refused when you choose the
  process.

## Steps

1. **Open the window.** Make the first master active and choose **Process > Color Spaces > NB Color Mapper**.
   **Layers** lists it as #1, and the preview shows it in pale red, the default color, stretched by **Preview STF**.
   Set the preview's **Display Mode** to **Linear**, so that it is stretched once.
2. **Stretch the layer.** Set **Blackpoint** just below the master's median, as Image Statistics gives it, and lower
   **Midtones** until its signal shows in the preview.
3. **Color it.** Drag in the hue wheel, or type **Hue**, and set **Saturation**.
4. **Add the next layer.** Choose **Add**: a new row copies the selected one and is selected. Choose the next master
   in **Image**, then give it its own stretch and color.
5. **Modify a layer, if you want.** Choose **Mode**, then the modifying row in **With layer**, and set **Factor**.
6. **Neutralize the background.** Tick **Enable** under **Background Neutralization** when the background shows a
   cast.
7. **Choose the output.** Tick **Apply STF to combined output** for a stretched result, and **Create colorized layer
   images** for one more image per layer.
8. **Apply.** Choose **Apply**. The result opens as a tab beside the active image, named after the first layer's
   image with ` - NB Color Mapper`.

## Controls

The controls scroll in one column. **Midtones**, **Blackpoint** and the controls from **Image** to **Median offset**
edit the row selected in **Layers** at once. Those from **Enable** down apply to the whole map. The preview is always
on, with no **Preview** check box, and **Preview area** is off when the window opens. For the histogram and the
toolbars, see [Process Windows](../../interface/process-windows.md).

```controls
window: The layer list
controls:
  - name: Layers
    kind: readout
    default: "#1, the active image"
    effect: Lists the layers, one row each, under **Include** (Yes or No), **Layer** (#1, #2 and so on), **Image**, **Color** (as `H 0 / S 0.50`, RGB, or Excluded) and **Modifier** (None, or as `Add #2`). Click a row to load it into the controls of the window.
    range: The active image is row #1 when the window opens, if it is a grayscale or RGB still image. The cells cannot be edited. Up to 64 rows can be mapped, and more make the run fail.
    tradeoff: Every row, included or not, is read and stretched for each preview and Apply, so remove a row you do not need rather than exclude it.
    persistence: Until the window closes. **Reset** and **Apply** keep every row. Nothing is kept between sessions.
    note: The order of the rows does not change the blend. The first row gives the result its size, its name, its header and its plate solution.
```

```controls
window: NB Color Mapper
controls:
  - name: Midtones
    kind: spinbox
    default: "0.500"
    widget: {min: 0.001, max: 0.999, step: 0.01, decimals: 3}
    effect: Sets the midtones balance of the selected layer's own transfer, by equation (1). Below 0.5 it brightens what lies between the black point and white, above 0.5 it darkens it. At 0.5, with **Blackpoint** at 0, the layer is used as it is.
    range: It edits the selected row, grayscale or RGB, included or not, a modifier or not.
    tradeoff: Near either end, tones collapse and noise and clipping show.
    scope: Every channel of the selected layer, before its modifier and its color. A row used as a modifier is read through its own transfer.
    persistence: Kept for each row until the window closes. **Reset** and a finished Apply return the selected row's to 0.5.
  - name: Blackpoint
    kind: spinbox
    default: "0.000"
    widget: {min: 0, max: 1, step: 0.001, decimals: 3}
    effect: Sets the selected layer's black point. Samples at or below it become black and the rest spread over the range, by equation (1).
    range: It edits the selected row, as **Midtones** does.
    tradeoff: Set high, it erases faint signal, and leaves a modifier with little to act with.
    scope: As **Midtones**.
    persistence: Kept for each row until the window closes. **Reset** and a finished Apply return the selected row's to 0.
  - name: Add
    object: addLayerButton
    kind: button
    default: ""
    effect: Adds a row at the end that copies the selected row, image included, and selects it, so that the next image you choose in **Image** goes into it.
    range: It does nothing while **Image** reads `<--NO IMAGE-->`. One image may fill several rows, each with its own mapping.
    tradeoff: Each row adds a working copy of its image and work to every preview.
    persistence: The row stays until you remove it or the window closes.
  - name: Remove
    object: removeLayerButton
    kind: button
    default: ""
    effect: Removes the selected row. A row that used it as its modifier returns to **None** in **Mode**, and the rows after it move up one number.
    range: It does nothing without a selected row, and cannot be undone in the window.
  - name: Image
    kind: combo
    default: "<--NO IMAGE-->"
    options: ["<--NO IMAGE-->", Every open grayscale or RGB still image of the first row's size]
    effect: Chooses the image of the selected row; the first row starts with the active image. A grayscale image is a layer to color, an RGB image a layer used as it is. The image itself is not changed.
    range: It lists the open still images of one or three channels with the width and height of the first row's image, or of any size while that image is not open. The tooltip of each item gives its file, size, channels, pixel type and memory.
    enabled: The list follows the views as they open, close, change or are renamed, and a renamed image renames its rows. While a row's image is closed, **Apply** stays dimmed.
    tradeoff: A wrong or unregistered image shows as colored halos and doubled stars.
    persistence: Kept for each row. **Reset** leaves it.
  - name: Include layer
    kind: check
    default: "On"
    effect: On, the selected row takes part in the screen blend. Off, it stays out of the blend, and can still be another row's modifier.
    range: At least one row must be included, or **Apply** stays dimmed.
    tradeoff: An excluded row is still read and stretched, and still counts in the header.
    persistence: Kept for each row. **Reset** and a finished Apply turn the selected row's on.
  - name: Hue
    kind: spinbox
    default: "0.000"
    widget: {min: 0, max: 359.99, step: 1, decimals: 3}
    effect: Sets the hue the selected grayscale row takes, in degrees, by equation (3). 0 is red, 60 yellow, 120 green, 180 cyan, 240 blue and 300 magenta. A hue is a color of your choice, not a filter.
    range: The hue wheel sets it too.
    enabled: For a grayscale row that no other row uses as its modifier. An RGB row ignores it.
    tradeoff: It moves the layer's light between the channels, and does not scale it.
    scope: The selected row, after its modifier.
    persistence: Kept for each row. **Reset** and a finished Apply return the selected row's to 0.
  - name: Saturation
    kind: spinbox
    default: "0.500"
    widget: {min: 0, max: 1, step: 0.01, decimals: 3}
    effect: Sets how strongly the selected grayscale row takes its hue, by equation (3). At 0 the layer stays gray, at 1 it is the pure hue.
    range: The saturation strip sets it too.
    enabled: As **Hue**.
    tradeoff: A strong saturation separates the colors, and shows registration and star size differences. When every included row stays gray and none is RGB, the result is grayscale.
    persistence: Kept for each row. **Reset** and a finished Apply return the selected row's to 0.50.
  - name: Mode
    kind: combo
    default: None
    options: [None, Add, Multiply, Boost]
    effect: Chooses how another row modifies the selected grayscale row before it is colored, by equation (2). **Add** adds the modifier, **Multiply** multiplies by it, and **Boost** strengthens the row where the modifier is bright.
    enabled: As **Hue**. An RGB row and a row used as a modifier are never modified.
    tradeoff: "**Add** and **Boost** can clip highlights. **Multiply** can take faint signal out."
    persistence: Kept for each row. **Reset** and a finished Apply return the selected row's to **None**.
    note: A mode without a row in **With layer** leaves the row unmodified, and the **Modifier** column reads None.
  - name: With layer
    kind: combo
    default: None
    options: [None]
    effect: Chooses the row that modifies the selected row. A grayscale modifier gives its stretched value, an RGB one 0.2989 of its stretched red, 0.5870 of its green and 0.1140 of its blue.
    range: After **None** it lists every other row, as `#2 <image>`. A row cannot modify itself, and the modifier may be excluded.
    enabled: While **Mode** is not **None**.
    tradeoff: A noisy or misaligned modifier carries its noise and stars into the row.
    persistence: Kept for each row. Removing the modifier row returns **Mode** to **None**.
    note: The modifier is read through its own **Midtones** and **Blackpoint**, not its color or its own modifier. While it serves, it is neither colored nor modified, and adds as gray when it is included.
  - name: Factor
    kind: spinbox
    default: "1.00"
    widget: {min: 0, max: 10, step: 0.05, decimals: 2}
    effect: Scales the modifier, by equation (2). Higher makes it act more strongly. At 0, **Add** and **Boost** change nothing and **Multiply** turns the row black.
    enabled: While **Mode** is not **None**.
    tradeoff: High values clip the row.
    persistence: Kept for each row. **Reset** and a finished Apply return the selected row's to 1.
  - name: Invert modifier
    kind: check
    default: "Off"
    effect: Uses one minus the modifier, so that its dark areas act strongly and its bright ones weakly.
    enabled: While **Mode** is not **None**.
    tradeoff: It can lift the background of the row.
    persistence: Kept for each row. **Reset** and a finished Apply turn the selected row's off.
  - name: Median offset
    kind: check
    default: "Off"
    effect: Subtracts the modifier's median, after any inversion, and keeps what lies above it, so that only signal above the modifier's background acts.
    enabled: While **Mode** is not **None**.
    tradeoff: It costs a pass over the modifier to find its median, over the image or the preview area.
    persistence: Kept for each row. **Reset** and a finished Apply turn the selected row's off.
  - name: Enable
    kind: check
    default: "Off"
    effect: After the blend, measures each channel's background inside a window around its median, takes it out and spreads the three channels alike, by equation (5).
    range: For an RGB result only. While it is on, **Upper sigma** must be above **Lower sigma**, or **Apply** stays dimmed.
    tradeoff: It removes a color cast, but where nebula fills the frame it takes signal for background. A channel whose window holds no sample is left as it is, without a message.
    scope: The preview and Apply, before the output stretch. A preview area measures itself alone. Not the colorized layer images.
    persistence: Until the window closes. **Reset** and a finished Apply turn it off. The history adds `background neutralized`.
  - name: Rescale high
    kind: check
    default: "Off"
    effect: On, the spread reaches up to each channel's highest corrected value. Off, it reaches up to one minus the channel's background.
    range: It acts only while **Enable** is on and the result is RGB.
    tradeoff: On gives back headroom, but lets one outlier set the range and can change the balance.
    persistence: Until the window closes. **Reset** and a finished Apply turn it off.
  - name: Lower sigma
    kind: spinbox
    default: "-2.80"
    widget: {min: -10, max: 10, step: 0.1, decimals: 2}
    effect: Sets the lower edge of the background window, in deviations from the channel's median. Higher keeps more dark samples out, lower lets them in.
    range: It must stay below **Upper sigma** while **Enable** is on. It stays editable, and is not read, while **Enable** is off.
    tradeoff: A narrow window can hold no sample. A wide one can take in defects.
    persistence: Until the window closes. **Reset** and a finished Apply return it to -2.80.
  - name: Upper sigma
    kind: spinbox
    default: "2.00"
    widget: {min: -10, max: 10, step: 0.1, decimals: 2}
    effect: Sets the upper edge of the background window. Lower keeps more bright structure, such as stars and nebula, out. Higher lets it in.
    range: It must stay above **Lower sigma** while **Enable** is on.
    tradeoff: Too high takes stars and nebula for background. Too low can leave no sample.
    persistence: Until the window closes. **Reset** and a finished Apply return it to 2.
  - name: Preview STF
    kind: check
    default: "On"
    effect: Stretches the preview and **Take snapshot** by equation (6), so that linear layers can be seen. It changes the preview's samples, not only their display.
    range: It never touches what **Apply** writes.
    tradeoff: With the preview's **Display Mode** at **Autostretch**, the preview is stretched twice and looks far brighter than the result. It costs a pass to find the median.
    scope: The preview and snapshots, before the mask. **Switch image** shows the active image without it.
    persistence: Until the window closes. **Reset** and a finished Apply turn it on.
  - name: STF target
    kind: spinbox
    default: "0.250"
    widget: {min: 0.01, max: 0.99, step: 0.01, decimals: 3}
    effect: Sets where the output stretch puts the median of the mean of the channels, by equation (6). Higher brightens, lower darkens.
    range: It serves **Preview STF** and **Apply STF to combined output** alike, and is checked even while both are off.
    tradeoff: Near either end, tones compress and noise and clipping show.
    persistence: Until the window closes. **Reset** and a finished Apply return it to 0.25.
  - name: Create colorized layer images
    kind: check
    default: "Off"
    effect: Apply also opens one image for each included row, mapped alone through its transfer, its modifier and its color, without the other rows, the background step, the output stretch or the mask.
    range: An excluded row gives none. A row that stays gray, or that another row uses as its modifier, gives a grayscale image.
    tradeoff: Each is a full-size image in the result's precision, three channels for a colored row.
    scope: Apply only. Previews and snapshots never make them.
    persistence: "Until the window closes. **Reset** and a finished Apply turn it off. Each image opens beside the active image after the result, named `<first layer's image> - <row's image> colorized`, with a history that starts with `NB Color Mapper colorized layer`. It keeps its own image's header and plate solution, its lineage gains the coloring, and it is recorded as nonlinear with the operation `nb-color-mapper-layer`."
  - name: Apply STF to combined output
    kind: check
    default: "Off"
    effect: Applies the output stretch of equation (6) to the image **Apply** writes, so that it opens stretched.
    range: Not the colorized layer images.
    tradeoff: Off leaves a result to stretch yourself. It is recorded as nonlinear either way.
    scope: Apply only, before the mask.
    persistence: Until the window closes. **Reset** and a finished Apply turn it off. The history adds `stretched`.
  - name: Reset
    kind: button
    default: ""
    effect: Returns every control from **Enable** down to its default, and the selected row's **Include layer**, **Hue**, **Saturation**, **Midtones**, **Blackpoint** and modifier to theirs.
    range: It keeps every row with its image, the other rows' values, the preview area and the histogram's settings.
    enabled: Disabled while a run works.
  - name: Apply
    kind: button
    default: ""
    effect: Maps every row as the window stands and opens the result as a new image, a tab beside the active image, named `<first row's image> - NB Color Mapper`. Then it does what **Reset** does.
    enabled: While the active image is a still image, every row names an open grayscale or RGB image of the first row's size, a row is included, and the background window is ordered while **Enable** is on. While the run works, the controls of the window, **Apply**, **Reset**, **Close**, **Preview area** and **Take snapshot** are disabled.
    tradeoff: There is no progress window and no **Cancel**. Each row is read as a working copy. The blend is spread over the processor's threads, while the background step, a median offset and the output stretch rank the whole image on one thread, in one or two planes of 8 bytes a pixel.
    scope: Every row's whole image, then the mask on the active image's view, which blends the active image with the result. A preview area does not limit it.
    persistence: "The history starts with a step that reads like `Created NB Color Mapper combination from 2 layers, 2 included, background neutralized, stretched`, and shows the active image's steps above it as inherited lineage. The result is recorded as nonlinear with the operation `nb-color-mapper`, whatever the output stretch. No palette, preset, report, mask or file is written."
    note: "The header is built from every row's image, excluded and modifier rows included, the first leading. The exposure is the light they hold together: the sum, over the distinct acquisitions, of each one's `TOTEXP`, else its `LIVETIME`, else its exposure times its `STACKCNT`, else `NCOMBINE`, else one. Images that state the same exposure start are one acquisition and count once, with the largest integration and frame count any of them states, and one that states no start counts on its own. A single row keeps its own exposure. With several, their `EXPOSURE` and `EXPTIME` cards are dropped, and the result states its own `EXPTIME` and `TOTEXP`, `LIVETIME` when an image states one, and `STACKCNT` and `NCOMBINE` summed the same way, each when an image states it. The observation start is the earliest, with its `DATE-OBS`, `DATE-LOC`, `MJD-OBS`, `DATE-BEG`, `MJD-BEG` and `TIME-OBS` together. The object, instrument, telescope, observer, filter, sensor temperature, gain and binning are kept when every image states the same, and so is every other card the first row's image shares with all of them. The first row's pointing cards `RA`, `DEC`, `OBJCTRA`, `OBJCTDEC`, `OBJRA`, `OBJDEC`, `TELRA` and `TELDEC`, its WCS cards and its plate solution are kept, without a check that the other images share its grid. No processing-state or file identity card of an image is kept."
```

```controls
window: The color picker
controls:
  - name: Hue wheel
    kind: action
    default: ""
    effect: Click or drag inside the wheel to set **Hue** by the angle around its center, red at the top and the hues following clockwise.
    enabled: With **Hue**. Its group is disabled with it.
  - name: Saturation strip
    kind: action
    default: ""
    effect: Click or drag in the strip beside the wheel to set **Saturation**, 0 at its top and 1 at its bottom.
    enabled: With **Saturation**.
  - name: Swatch
    kind: readout
    default: ""
    effect: Shows the color of the hue and saturation chosen, at full value.
```

## How it works

1. Every row's image is read as a [fraction of full scale](../../reference/glossary.md#fraction-of-full-scale), in
   32-bit floating point, or 64-bit when a row's image is 64-bit floating point or a 32-bit integer.
2. In one pass over the image, each layer goes through its own transfer, equation (1). A grayscale layer then goes
   through its modifier, equation (2), and takes its color, equation (3).
3. The included layers are screen-blended, equation (4).
4. With **Enable** on, each channel's background is measured and taken out, equation (5). With the output stretch,
   the median of the mean of the channels moves to **STF target**, equation (6).
5. A mask on the active image's view blends the active image with the result, which opens as a new image beside it.

### Model

For a sample \(x\) of a layer, with \(b\) its **Blackpoint** and \(m\) its **Midtones**, take \(t = (x - b)/(1 - b)\).
The layer's value is

\[ v = \frac{(m - 1)\,t}{(2m - 1)\,t - m} \tag{1} \]

or 0 at or below \(b\) and 1 at or above 1. With \(u\) the modifier's value by equation (1), first \(1 - u\) with
**Invert modifier** and then \(\max(0, u - \tilde u)\) with **Median offset**, where \(\tilde u\) is its median, and
\(k\) the **Factor**, the modified value is

\[ v_{Add} = v + k\,u, \qquad v_{Multiply} = v\,k\,u, \qquad v_{Boost} = v\,(1 + k\,u) \tag{2} \]

held between 0 and 1. With \(s\) the **Saturation**, a hue of 0 colors \(v\) as

\[ (R, G, B) = \bigl(v,\ (1 - s)\,v,\ (1 - s)\,v\bigr) \tag{3} \]

and the other hues move the full value \(v\) and the least, \((1 - s)\,v\), between the channels: 120 gives
\(((1 - s)v, v, (1 - s)v)\), 180 gives \(((1 - s)v, v, v)\), and 30 at full saturation gives \((v, 0.5v, 0)\). With
\(L_{i}\) the value of included layer \(i\) in one channel, a gray layer the same in all three, the blend is

\[ O = 1 - \prod_{i} \bigl(1 - L_{i}\bigr) \tag{4} \]

The background step takes, in each channel, the median \(M\) and \(\sigma\), 1.4826 times the
[median absolute deviation](../../reference/glossary.md#median-absolute-deviation). Its level \(B\) is the median of
the samples from \(M + l\,\sigma\) to \(M + h\,\sigma\), with \(l\) and \(h\) the two sigma values, each edge held
between 0 and 1. With \(d_{min}\) the lowest \(O - B\) of the three channels, and \(d_{max}\) the highest \(1 - B\), or
the highest \(O - B\) with **Rescale high**, each channel becomes

\[ O' = \frac{O - B - d_{min}}{d_{max} - d_{min}} \tag{5} \]

when \(d_{min}\) is below 0 or \(d_{max}\) above 1, and \(O - B\) otherwise, held between 0 and 1. The output stretch
takes \(P\), the median of \((R + G + B)/3\), and \(T\), the **STF target**, and applies equation (1) with \(b = 0\)
to every channel, with the balance

\[ m = \frac{(T - 1)\,P}{(2T - 1)\,P - T} \tag{6} \]

which sends \(P\) to \(T\). A median of 0 or 1 skips it.

### Boundaries

- A sample that is not a finite number is read as 0, which adds no light. Every pixel is mapped.
- A row that another row uses as its modifier is neither modified nor colored, and an RGB row is never either.
- The background step does nothing to a grayscale result, and leaves a channel without a sample in its window as it
  is.
- Every value is held between 0 and 1 at each step, so a floating-point sample above 1 becomes 1.

### Worked example

Make the Ha master of the Eagle Nebula active, a 3008 × 3004
[linear image](../../reference/glossary.md#linear-image) in 32-bit floating point, and open the window: it is row #1.
Set **Saturation** to 1, **Blackpoint** to 0.035 and **Midtones** to 0.020, and leave **Hue** at 0. Choose **Add**,
choose the OIII master in **Image**, and set **Hue** to 180 and **Blackpoint** to 0.036. Tick
**Enable** and **Apply STF to combined output**. Equation (1) lifts the Ha median, 0.03783, to 0.126 in red, and the
OIII median, 0.03863, to 0.118 in green and blue. The background step measures 0.122 in red and 0.118 in green and
blue, and sets all three to 0.122, a neutral gray. The output stretch finds \(P = 0.128\), and equation (6) gives a
balance of 0.306, which sends it to 0.25. A pixel of the nebula at 0.0459 in Ha and 0.0396 in OIII ends at 0.56 in
red and 0.30 in green and blue.

The new image, `<Ha master> - NB Color Mapper`, is RGB in 32-bit floating point. It states `TOTEXP` 13500 and
`STACKCNT` 45, the sums of the two masters, and the Ha master's `DATE-OBS`, the earlier.

```figure
images:
  - image: ../../assets/images/processes/color-composition/nb-color-mapper-before.webp
    alt: The whole Ha master in the view's Autostretch, a gray picture of the Eagle Nebula's clouds, pillars and dark lanes among many stars.
    label: Before
  - image: ../../assets/images/processes/color-composition/nb-color-mapper-after.webp
    alt: >-
      The whole mapped image, the same field with the nebula in red and pink over a teal background, brighter
      toward the left edge.
    label: After
caption: >-
  The Ha master, and the image Apply opens from it and the OIII master with the settings above. The Ha master is shown
  in the view's Autostretch, and the result in the Linear display mode, since its own output stretch is in its
  samples.
```

A modifier works pixel by pixel. With **Mode** at **Add**, **Factor** at 0.5 and an excluded modifier at 0.50, a
layer at 0.20 becomes 0.45, which hue 0 at full saturation colors \((0.45, 0, 0)\). Blended with an RGB layer at
\((0.20, 0.30, 0.40)\), equation (4) gives \((0.56, 0.30, 0.40)\).

## Starting points

| Situation | Start | Then adjust |
| --- | --- | --- |
| Ha and OIII masters | Ha at **Hue** 0, OIII at 180, **Saturation** 1 | Each layer's **Blackpoint** just below its median, then **Midtones**. |
| Ha, OIII and SII masters | One row each, at the hues you want | One layer at a time, with **Include layer** off on the others. |
| A cast in the background | **Enable** with the sigma values at their defaults | A narrower window where nebula fills the frame, and **Rescale high** last. |
| A result to stretch later | **Apply STF to combined output** off | Stretch the result with Histogram Transformation. |

## When it goes wrong

!!! danger "One size is not one grid"
    The window compares width and height only: nothing matches stars, world coordinates, scale, rotation or star
    profiles. An unregistered layer gives colored halos and doubled stars, and a modifier carries its stars into the
    layer it modifies.

!!! warning "Apply and Reset return the selected row to its defaults"
    The selected row loses its hue, saturation, stretch and modifier, and the other rows keep theirs. Note its
    values before you apply.

!!! warning "The window stays on the image it opened on"
    It has no **Target Image**. That image's mask blends the result, **Switch image** shows it, the result opens
    beside it, and it names the result when no row names an image, even when it is no longer a layer. Open the window
    from the master you want as the first layer.

!!! warning "Apply shows no progress and has no Cancel"
    The controls, **Apply**, **Reset** and **Close** stay disabled until the run ends. Closing the window another way
    stops the run, and nothing opens.

Apply stays dimmed, and the preview shows the active image unchanged
:   **Cause.** A row's image is closed or of another size, no row is included, or **Upper sigma** is not above
    **Lower sigma** while **Enable** is on.
:   **Fix.** Open or replace the image, or remove its row, include a row, and order the two sigma values.

Hue, Saturation and Mode are disabled
:   **Cause.** The selected row's image is RGB, another row uses this row as its modifier, or **Image** reads
    `<--NO IMAGE-->`.
:   **Fix.** Select a grayscale row. To color a modifier too, add a second row of the same image.

The preview is far brighter than the result
:   **Cause.** **Preview STF** is on while the preview shows **Autostretch**, or **Apply STF to combined output** is
    off, or a [preview area](../../reference/glossary.md#preview-area) measured its own median.
:   **Fix.** Show the preview in **Linear**, tick **Apply STF to combined output** if you want the result stretched,
    and untick **Preview area** for a last look.

A modifier has no visible effect, or an excluded row still changes the result
:   **Cause.** **With layer** reads None, **Factor** is 0 with **Add** or **Boost**, **Median offset** left nothing
    above the median, or the excluded row is another row's modifier.
:   **Fix.** Choose a row in **With layer**, raise **Factor**, untick **Median offset**, or set the dependent row's
    **Mode** to **None**.

The background keeps a cast, or its colors shift strongly
:   **Cause.** Nebula fills the background window, **Rescale high** let an outlier set the range, or a channel's
    window held no sample.
:   **Fix.** Narrow the two sigma values, or turn **Rescale high** off.

The result is grayscale
:   **Cause.** Every included row is gray, at **Saturation** 0 or used as a modifier, and none is RGB.
:   **Fix.** Raise **Saturation** on a grayscale row that no other row modifies.

## Related

- [Narrowband Imaging](../../workflows/narrowband-imaging.md), from narrowband frames to a color image.
- [Image Registration](../registration-alignment/image-registration.md), to put the masters on one grid first.
- [Channel Composition](channel-composition.md), to put masters into red, green and blue as they are.
- [NBRGB Combination](nbrgb-combination.md), to add narrowband signal to a broadband color image.
- [Narrowband Palette Composer](narrowband-palette-composer.md), for fixed palettes such as
  [SHO](../../reference/glossary.md#sho), side by side.
- [Process Windows](../../interface/process-windows.md), for the preview, the histogram toolbar and what every
  process window shares.

## Credits

- The layer mapping, with each layer's black point and midtones transfer, the four modification modes with their
  inversion and median offset, the HSV coloring, the screen blend, the background neutralization's window and the
  output stretch, with their formulas and constants, is derived from
  [NBColorMapper 2.2](https://cosmicphotons.com/scripts/), a PixInsight script by Mike Cranfield of Cosmic Photons,
  Copyright (C) 2023, 2024 Mike Cranfield, under the GNU General Public License version 3, and is distributed under
  that licence. The single pass over the image, the bounded memory and the handling of invalid samples are
  AfterNight's. No PixInsight, PJSR or Pleiades code, interface or asset is included. **About > About AfterNight**
  credits it.
