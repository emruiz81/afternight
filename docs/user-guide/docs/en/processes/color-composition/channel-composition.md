---
kind: process
stage: color
menu: Process > Color Spaces > Channel Composition
summary: >-
  Composes up to three open grayscale images into a new color image, as its red, green and blue or as the components
  of another color model.
data_in: either
data_out: either
destructive: false
preview: real-time
sequences: true
masks: none
external: none
glance:
  enabled: >-
    Always. The task chooses its images itself and opens without an active image, as a page of its own beside other
    tasks. OK waits for an image in its first box.
  inputs: >-
    Up to three open grayscale images of one width and height, or three grayscale sequences. The first is required.
  outputs: >-
    One new color image or sequence beside the first component, in the highest pixel type of the components. The
    components stay as they are, and no file is written.
  state: >-
    Red, green and blue masters of one target, registered and cropped alike and still linear, or the three
    components of another color model as Extract Channel stores them.
derived: false
---
# Channel Composition

Channel Composition makes one color image from up to three open grayscale images. In **RGB** they become its red,
green and blue channels as they are. In the seven other choices of **Color Space** they are the three components of
another [color model](../../reference/glossary.md#color-model), such as [CIE Lab](../../reference/glossary.md#cie-lab)
or [YCbCr](../../reference/glossary.md#ycbcr), and are converted to red, green and blue. **Linear Match** can first
bring the second and third to the first one's levels. The [task](../../reference/glossary.md#task) chooses its images itself and needs no
[active image](../../reference/glossary.md#active-image).

**OK** opens the result as a new image beside the first component, or as a new
[sequence](../../reference/glossary.md#sequence) when the components are sequences, and closes the task. The
components stay as they were.

```figure
image: ../../assets/images/processes/color-composition/channel-composition.webp
alt: >-
  The Channel Composition task's column. At its top the compact readout of the preview: a small histogram with a red,
  a green and a blue peak close together near its dark end, the clipped fractions, and Min, Max, Mean, Median and
  StdDev, the median 0.0702. Below it the column's header, the Preview box ticked, Preview area unticked and the
  Processed button, Color Space at RGB, the Red, Green and Blue boxes each naming a stack of the Carina set, shortened
  to stacked_828...rina_Nebula, and Linear Match ticked. Close and OK stand in its footer.
caption: >-
  Channel Composition in RGB with the red, green and blue stacks of the Carina set in its three boxes and Linear Match
  ticked. The readout at the top is counted from the composed preview.
```

## Before you start

- Register the components to one reference and crop them alike. The task compares their width and height only:
  each pixel is combined with the pixels at the same place, and nothing is aligned or resampled.
- Every component is a grayscale image, or every one a grayscale sequence. A color image is not offered.
- Put first the component whose grid, [plate solution](../../reference/glossary.md#plate-solution) and pointing the
  result should carry. A sequence's container, frame count and frame rate come from it too.
- Fill all three boxes. A box left empty is a plane of zeros, which is black only in **RGB**.
- For any color model but RGB, use the three components of one image as **Image > Extract Channel** stores them,
  each a value from 0 to 1 by AfterNight's
  [component encoding](../../reference/glossary.md#component-encoding). No color profile is read or applied.
- **Linear Match** scales and shifts a component. It neither registers it nor calibrates its color: calibrate the
  color of the result afterwards.

## Steps

1. **Open the components.** Open the registered grayscale [masters](../../reference/glossary.md#master), or the
   three sequences.
2. **Open the task.** Choose **Process > Color Spaces > Channel Composition**, or find it in the
   [command palette](../../interface/command-palette.md) with ++ctrl+k++. The task opens as a page: the canvas at the
   left names the component still to choose, the column at the right holds the controls, and **OK** is dimmed.
3. **Choose the color space.** Leave **Color Space** at **RGB** for red, green and blue masters. Another choice
   renames the three boxes after its components, such as **L**, **a** and **b**.
4. **Assign the components.** Choose the red master in **Red**, the green one in **Green** and the blue one in
   **Blue**. Once the first box names an image, **OK** enables and the canvas shows the composed preview.
5. **Match the levels, or not.** Tick **Linear Match** to give the green and blue masters the red one's background
   level and spread. Leave it off for matched masters and for every color model but RGB.
6. **Choose OK** in the column's footer. The new image opens beside the first component, named after the color
   space, and the task closes.
7. **Check the result.** Show it in the view's [Autostretch](../../reference/glossary.md#autostretch) with **Linked
   channels** on in its Autostretch Settings, which shows the three channels alike. Its
   [history](../../reference/glossary.md#history) starts with the names of the three components.

## Controls

The task has no histogram, no **Target Image** and no **Channel** box, and reads no
[mask](../../reference/glossary.md#mask). Its controls are read when you press **OK**.

```controls
window: Channel Composition
controls:
  - name: Color Space
    kind: combo
    default: RGB
    options: [RGB, CIE XYZ, CIE L*a*b*, CIE L*c*h*, HSL, HSV, HSI, YCbCr]
    effect: Chooses what the three images are. **RGB** takes them as red, green and blue. Every other choice takes them as the three components of that color model and converts them to red, green and blue.
    range: "The three boxes are renamed after the components: X, Y and Z for **CIE XYZ**, L, a and b for **CIE L*a*b***, L, c and h for **CIE L*c*h***, H, S and L for **HSL**, H, S and V for **HSV**, H, S and I for **HSI**, and Y, Cb and Cr for **YCbCr**."
    enabled: Disabled while a run works.
    tradeoff: Every choice but **RGB** converts each pixel and holds each channel between 0 and 1, so a color outside the range is clipped. CIE components and HSI cost more than the others. A hue or a chroma cannot stand in for a level of light.
    scope: The whole of each image, or each frame of the sequences.
    persistence: Until the task closes. Each new task opens at **RGB**.
  - name: Red
    kind: combo
    default: "<--NO IMAGE-->"
    options: ["<--NO IMAGE-->"]
    effect: Chooses the first component, red in **RGB**. It is required. Its image gives the result its size and its place in the workspace, and leads its header. The preview in the canvas is composed over it.
    range: After `<--NO IMAGE-->` the list holds every open grayscale image and sequence. The box's label follows **Color Space**, as **X**, **L**, **H** or **Y**. The tooltip of each item gives its file, size, channels, pixel type and memory.
    enabled: The list follows the views as they open, close, change or are renamed. A view that closes or stops being grayscale leaves every list, and a box that named it returns to `<--NO IMAGE-->`. Disabled while a run works.
    tradeoff: A wrong first image changes the whole result. Its world coordinates and plate solution are kept whether or not the other components share its grid. The task compares neither stars nor world coordinates.
    scope: The image its view shows when you press **OK**, or every frame of its sequence. With **Linear Match** on it is the reference, and is not changed.
    persistence: Until the task closes or its view goes.
    note: "Until it names an image the canvas reads \"Choose the images to see the result\" over \"Still to choose: Red component\", with the box's label of the color space chosen. The task header names the process alone, whatever the boxes name."
  - name: Green
    kind: combo
    default: "<--NO IMAGE-->"
    options: ["<--NO IMAGE-->"]
    effect: Chooses the second component, green in **RGB**. In the other models it is Y, a*, c*, the saturation of HSL, HSV or HSI, or Cb. Left at `<--NO IMAGE-->`, it is a plane of zeros.
    range: The same list as **Red**. One view may fill several boxes.
    enabled: Optional. It must be an image when the first is an image and a sequence when the first is a sequence, of the first one's width and height.
    tradeoff: Zero is not neutral for every component. An a* or a Cb of 0 is the far end of its axis, and gives a strong cast.
    scope: The image its view shows when you press **OK**, or every frame of its sequence. With **Linear Match** on, a matched copy of it.
    persistence: Until the task closes or its view goes.
  - name: Blue
    kind: combo
    default: "<--NO IMAGE-->"
    options: ["<--NO IMAGE-->"]
    effect: Chooses the third component, blue in **RGB**. In the other models it is Z, b*, h, the lightness of HSL, the value of HSV, the intensity of HSI, or Cr. Left at `<--NO IMAGE-->`, it is a plane of zeros.
    range: The same list as **Red**.
    enabled: Optional, under the same conditions as **Green**.
    tradeoff: A zero is black where the component is a lightness, a value or an intensity, and a strong cast where it is b* or Cr.
    scope: The image its view shows when you press **OK**, or every frame of its sequence. With **Linear Match** on, a matched copy of it.
    persistence: Until the task closes or its view goes.
  - name: Linear Match
    kind: check
    default: "Off"
    effect: Scales and shifts the second and third components, before they are composed, so that their median and their median absolute deviation equal the first one's, by equation (4).
    range: The two numbers are measured only where both planes hold a sample from 0.0000001 to 0.875 of full scale, so zeros and the brightest stars take no part. The match then applies to every sample.
    enabled: Disabled while a run works.
    tradeoff: It gives the channels one background level and one spread, which is not a color calibration. It knows nothing of hue, neutral chroma, filters or exposure, so it spoils the components of every model but RGB. A floating-point result can leave 0 to 1. A pair without a sample inside the limits fails the run.
    scope: Working copies of the second and third components, for this run. The images in their views are not changed.
    persistence: Until the task closes. Each new task opens with it off.
    note: Its scale is always positive, so a component that runs against the first is scaled, never turned over. It costs a pass over each plane, the statistics of each pair and a copy of each matched component.
  - name: Preview
    kind: check
    default: "On"
    effect: Shows the composed image in the canvas, as **OK** makes it, once the first box names an image. A change of a box, of **Color Space** or of **Linear Match** shows it again.
    enabled: It stands in the column's toggles row. Disabled until the first box names an image, and while a run works.
    tradeoff: The preview composes the whole of the three images at each change, as **OK** does, and holds a color result as large as an image, three times a grayscale one.
    scope: "The task only. **Preview area**, beside it in the toggles row, shows what **OK** makes of that area of the three images. Three sequences show the composition of the frames their views show."
    persistence: Until the task closes.
    note: The canvas opens in the display mode of the first component's view, and **Display Mode** in its floating toolbar changes it.
  - name: OK
    kind: button
    default: ""
    effect: Composes the three components as the task stands and opens the result beside the first component, as a new image or a new sequence of three channels, without alpha. The task then closes. A failure shows "Error running Channel Composition" with the reason in the column's message strip, and the task stays open.
    enabled: While the first box names an image and no run works. During a run **Cancel** takes its place in the footer, and **Close** and the other controls are disabled.
    tradeoff: A full image of three channels in memory, a working copy of each component, and for sequences a second copy on disk. The work is spread over the processor's threads.
    scope: The whole of each image, whatever the preview's area. No mask plays a part.
    persistence: "The new image is unsaved and named after the color space: `RGB`, `CIE XYZ`, `CIE L*a*b`, `CIE L*c*h`, `HSL`, `HSV`, `HSI` or `YCbCr`, with `_1`, `_2` and so on added when the name is taken. A new sequence is named after its temporary file. The history starts with a step that reads `ChannelComposition: created from image channels R:'<red>' G:'<green> B:'<blue>'`, with `-` for an empty box and the model's component names, such as L, a and b, in place of R, G and B. It does not say whether Linear Match was on, nor give its scale and offset."
    note: "The header is built from the components. The exposure is the light they hold together: the sum, over the distinct acquisitions, of each one's `TOTEXP`, else its `LIVETIME`, else its exposure times its `STACKCNT`, else `NCOMBINE`, else one. Components that state the same exposure start are one acquisition, such as an image, its starless image and a channel taken from it, and count once with the largest integration and frame count any of them states. A component that states no start counts on its own, so one view in two boxes counts once only when it states a start. One component keeps its own exposure. The components' `EXPOSURE` and `EXPTIME` cards are dropped, and the result states its own `EXPTIME` and `TOTEXP`, `LIVETIME` when a component states one, and `STACKCNT` and `NCOMBINE` summed the same way, each when a component states it. The observation start is the earliest, with its `DATE-OBS`, `DATE-LOC`, `MJD-OBS`, `DATE-BEG`, `MJD-BEG` and `TIME-OBS` together. The object, instrument, telescope, observer, filter, sensor temperature, gain and binning are kept when every component states the same, and so is every other card. The first component's pointing cards `RA`, `DEC`, `OBJCTRA`, `OBJCTDEC`, `OBJRA`, `OBJDEC`, `TELRA` and `TELDEC` are kept, and so are its WCS and SIP cards and its plate solution, without a check that the others share its grid. No processing-state or file identity card of a component is kept."
```

```controls
window: The run in the column
controls:
  - name: Overall and Processing
    kind: readout
    default: ""
    effect: Show, in the column's run row, the share of the frames written, from 0 to 100 percent, under **Overall**. **Processing** has no share of its own here, and a slice slides along it.
    range: The share is counted against the longest of the sequences, so it stops short of 100 when a later component has more frames than the first.
    enabled: Only when the components are sequences. Over still images the row reads "applying". No progress window opens.
    tradeoff: The new sequence is written to a temporary file, which needs the disk space of a second copy.
  - name: Cancel
    kind: button
    default: ""
    effect: Stands in the footer in place of **OK** during a run, and stops it, inside the work on a frame as well as between frames. Nothing opens, the unfinished sequence leaves no file, and the task stays open.
```

## How it works

1. Each component is read as a [fraction of full scale](../../reference/glossary.md#fraction-of-full-scale) into a
   floating-point plane: 64-bit when a component is 64-bit floating point or a 32-bit integer, 32-bit otherwise. An
   empty box is a plane of zeros at the first component's size.
2. With **Linear Match** on, the second and third planes are matched to the first by equation (4).
3. In **RGB** the three planes become red, green and blue as they are. A CIE model is converted through XYZ to
   sRGB-encoded red, green and blue with a D65 white. [HSL](../../reference/glossary.md#hsl),
   [HSV](../../reference/glossary.md#hsv), [HSI](../../reference/glossary.md#hsi) and YCbCr give red, green and blue
   as stored. Every model but RGB is computed in double precision, and each channel is held between 0 and 1.
4. The result is written once in the highest [pixel type](../../reference/glossary.md#pixel-type) of the components,
   ranked from signed and unsigned 8-bit, through 16-bit and 32-bit integers, to 32-bit and 64-bit floating point.
   An integer result is rounded to the nearest code and held at the ends of its type.
5. The new image's [processing state](../../reference/glossary.md#processing-state) is its own, with the operation
   `channel-composition`. In RGB it is linear when every component is declared linear and nonlinear when one is
   declared nonlinear, and otherwise it is not stated. Every other model records it as nonlinear.

### Model

A component sample \(c_0\), \(c_1\) or \(c_2\) is a value from 0 to 1, from the first, second and third box. In
**CIE L\*a\*b\*** they are the lightness \(L^*\) and the axes \(a^*\) and \(b^*\). In **CIE L\*c\*h\*** the second and
third are the chroma \(C^*\) and the hue angle \(h\) in degrees:

\[ L^* = 100\,c_0, \qquad a^* = 255\,c_1 - 128, \qquad b^* = 255\,c_2 - 128, \qquad C^* = 200\,c_1, \qquad h = 360\,c_2 - 180 \tag{1} \]

so a neutral \(a^*\) or \(b^*\) is 0.502, and a plane of zeros is \(-128\). For **CIE XYZ** each plane is the
tristimulus value over the white's, with \(X_w = 0.95047\) and \(Z_w = 1.08883\) for D65:

\[ X = X_w\,c_0, \qquad Y = c_1, \qquad Z = Z_w\,c_2 \tag{2} \]

For **YCbCr**, with the luma \(Y = c_0\) and the color differences \(C_b = c_1\) and \(C_r = c_2\), neutral at 0.5,
the full-range BT.601 weights give

\[ R = Y + 1.402\,(C_r - 0.5), \qquad B = Y + 1.772\,(C_b - 0.5), \qquad G = \frac{Y - 0.299\,R - 0.114\,B}{0.587} = Y - 0.344136\,(C_b - 0.5) - 0.714136\,(C_r - 0.5) \tag{3} \]

For **HSL**, **HSV** and **HSI** the hue is a fraction of a turn, and a hue of 1 is the hue of 0. The intensity of
HSI is the mean of the three channels, its hue read in sectors of 120 degrees. The value of HSV is the largest
channel, and the lightness of HSL the mean of the largest and the smallest.

With **Linear Match** on, take \(r\) for the first plane and \(x\) for a later one, the medians \(\tilde r\) and
\(\tilde x\) and the [median absolute deviations](../../reference/glossary.md#median-absolute-deviation)
\(\sigma_r\) and \(\sigma_x\). Each sample of \(x\) becomes

\[ x' = a\,x + b, \qquad a = \frac{\sigma_r}{\sigma_x}, \qquad b = \tilde r - a\,\tilde x \tag{4} \]

### Boundaries

- Sequences are composed frame by frame and may differ in length. The result has the first one's frames: a frame missing from a shorter later
  sequence is a plane of zeros, and frames beyond the first one's count are not read.
- A new sequence is written whole, or not at all, in the first sequence's container: a [SER](../../reference/glossary.md#ser)
  file at 8 or 16 bits, an image list, or an AVI or another video at 8 bits and an even width and height. It plays at
  the first one's frame rate, or one frame a second when it states none.
- In **RGB** a floating-point result keeps samples below 0 and above 1, and an integer one is held at the ends of its
  type. A match that cannot be measured fails the run rather than leave a black channel.
- **Cancel** in the footer stops a run, and nothing opens. The task does not close while its run works.

### Worked example

The red, green and blue masters of the Carina Nebula are 7246 × 5429, in 32-bit floating point, from one stacking
run on one grid. Their medians are 0.0277, 0.0800 and 0.1031, and their median absolute deviations 0.00175, 0.00108
and 0.00159. Assign them in that order in **RGB** and tick **Linear Match**. Equation (4) gives green \(a = 1.62\)
and \(b = -0.102\), and blue \(a = 1.10\) and \(b = -0.086\), so every channel's median is 0.0277 and its deviation
0.00175. The result is the RGB master the other pages of this guide use. With **Linear Match** off the channels keep
their medians, and the background is blue-green until its color is calibrated.

The new image states `EXPTIME` and `TOTEXP` 58560, the sum of the masters' 25920, 18840 and 13800 seconds, and
`STACKCNT` 488. Its `DATE-OBS` is the red master's, which is the earliest, its `FILTER` card is gone, and its world
coordinates are those of the red master.

```figure
images:
  - image: ../../assets/images/processes/color-composition/channel-composition-before.webp
    alt: The whole red master in the view's Autostretch, a gray picture of the nebula's clouds, dark dust lanes and many stars.
    label: Before
  - image: ../../assets/images/processes/color-composition/channel-composition-after.webp
    alt: The whole composed image in the same display, the nebula's red and white clouds and its stars over a gray background.
    label: After
caption: >-
  The red master, and the color image OK composes from the three masters with Linear Match on. Both are the whole
  frame in the view's Autostretch. With Linear Match on, the three channels share one median and spread, so the
  display shows their balance as it is.
```

## Starting points

| Situation | Start | Then adjust |
| --- | --- | --- |
| Red, green and blue masters of one night | **RGB**, the three masters in order, **Linear Match** off | Calibrate the color next, or tick **Linear Match** first to give the channels one background level. |
| Masters whose levels differ widely | **RGB** with **Linear Match**, the best master first | Check the medians in Image Statistics, then calibrate the color. |
| Components taken from one color image | Their color model, the three in the order the model names them, **Linear Match** off | Check that a neutral area stays gray before you work on. |
| A trial of the assignment | Duplicated crops of the components | Compose the full images once the order and the model are right. |

## When it goes wrong

!!! danger "An empty box is a plane of zeros, not a neutral component"
    In RGB it is a black channel. In CIE L\*a\*b\* or YCbCr a zero a\*, b\*, Cb or Cr is the far end of its axis,
    and the whole image takes a strong cast. Fill all three boxes for every model but RGB.

OK stays dimmed
:   **Cause.** The first box reads `<--NO IMAGE-->`, or the image it named was closed or is no longer grayscale.
:   **Fix.** Choose a grayscale image in the first box. The second and third alone do not enable **OK**.

The image you want is not in the lists
:   **Cause.** It is a color image, or it is not open.
:   **Fix.** Open it, or take the channel or component you need out of the color image with **Image > Extract
    Channel**.

"All the images must have the same size"
:   **Cause.** A component's width or height differs from the first one's.
:   **Fix.** Register and crop every component to the same grid, then compose them again.

"All the selected images must be either sequences or images"
:   **Cause.** One box names a sequence and another a still image.
:   **Fix.** Choose only images or only sequences.

Colored fringes on the stars and the edges
:   **Cause.** The components have one size but were not registered to each other.
:   **Fix.** Register them to one reference with a common crop, and compose them again.

The colors have a strong cast
:   **Cause.** In RGB the channels' levels differ. In another model a component is missing, in the wrong box or not
    of that model.
:   **Fix.** In RGB, tick **Linear Match** or calibrate the color afterwards. In another model, give the three
    components of one extraction in their order.

Linear Match fails the run
:   **Cause.** A component and the first share no sample between 0.0000001 and 0.875 of full scale.
:   **Fix.** Turn **Linear Match** off, or use components whose background lies inside those limits.

The new sequence is shorter, ends black in one channel, or comes back at 8 bits
:   **Cause.** The first sequence decides the frame count and the container, and an AVI or a video holds 8 bits.
:   **Fix.** Put first a SER or an image list with the frames you want, and give every component the same frames.

## Related

- [Image Registration](../registration-alignment/image-registration.md), to bring the components onto one grid
  first.
- [Linear Match](../color-calibration/linear-match.md), which matches one image to another as images: use it or this
  task's **Linear Match**, not both.
- [Background Neutralization](../color-calibration/background-neutralization.md) and
  [Photometric Color Calibration](../color-calibration/photometric-cc.md), to calibrate the color of the result.
- [LRGB Combination](lrgb-combination.md), to give a stretched color image the lightness of a luminance master.
- [Non-Interactive Processes](../utility/non-interactive-processes.md), whose **Extract Channel** commands make the
  components this task composes back.
- [Tasks](../../interface/tasks.md), for the canvas, the column and the run that every task shares.

## Credits

- International Color Consortium, [How to interpret the sRGB color space](https://www.color.org/sRGB.pdf), for the
  sRGB transfer, primaries and D65 white the conversions use.
- CIE, [Colorimetry, Part 4: CIE 1976 L\*a\*b\* Colour Space](https://cie.co.at/publications/colorimetry-part-4-cie-1976-lab-colour-space-1),
  for the lightness, the two axes, the chroma and the hue of equation (1).
- ITU-R, [Recommendation BT.601](https://www.itu.int/rec/R-REC-BT.601), for the luma weights of equation (3).
