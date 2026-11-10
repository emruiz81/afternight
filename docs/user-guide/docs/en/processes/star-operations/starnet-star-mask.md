---
kind: process
stage: noise
menu: Process > Mask Generation > Starnet++ Star Mask
summary: >-
  Makes a star mask with StarNet2, a separately installed program, and opens it as a new view beside the target: as
  the program wrote it, or reduced to its lightness and to black and white.
data_in: either
data_out: nonlinear
destructive: false
preview: none
sequences: true
masks: none
external: The StarNet2 command-line tool, 2.5.3 or newer, with its ONNX model, installed separately
glance:
  enabled: >-
    An image or a sequence view is active, no Starnet++ Star Mask window is open on it yet, and no window that needs
    the image for itself is open on it. The window opens whether or not StarNet2 is installed, and OK waits until the
    window says the program is ready.
  inputs: >-
    One grayscale or RGB image or sequence of any pixel type, not a raw color-filter mosaic. StarNet2 asks for at
    least 512 by 512 pixels.
  outputs: >-
    One new view beside the target, `<target> - STARMASK`, an image or a sequence in the target's pixel type, with the
    target's channels or one, and recorded as nonlinear. The target and its history are not changed, and no file is
    kept.
  state: >-
    A stacked master, linear with Pre-Stretch linear image ticked, or stretched with it off.
derived: true
---
# Starnet++ Star Mask

This window asks StarNet2, a trained neural network that runs as a separate program you install yourself, for a star
[mask](../../reference/glossary.md#mask). AfterNight hands it the image as a 16-bit file, and can reduce the mask it
answers to its lightness and then to black and white, with soft edges. There is no preview.

**OK** opens the mask as `<target> - STARMASK`, a new view beside the target, which stays as it was. The window stays
open. To use the mask, attach it to a view through [Masks](../../interface/masks.md).

```figure
image: ../../assets/images/processes/star-operations/starnet-star-mask.webp
alt: >-
  The Starnet++ Star Mask window. Under the title bar, the process name and a line that says it generates native
  StarNet2 star masks. In the Mask Settings group, Lightness and Binary Mask ticked, and Lower limit at 0.100000,
  Upper limit at 1.000000 and Smoothness at 2.00, each with its slider. In the StarNet2 Settings group, Pre-Stretch
  linear image clear, Highlight protection ticked, Stride reading Standard (256) beside a grayed box reading 256,
  Weights, an empty field showing Default StarNet2_weights.onnx in gray, with a Browse button, Upsample x2 clear, the
  line StarNet2 is configured and ready to use, and a Configure button. Below the groups, Target Image reading RGB.
  OK and Close run along the bottom.
caption: >-
  Starnet++ Star Mask on the stretched Carina master with Lightness and Binary Mask ticked and Smoothness at 2, once
  AfterNight knows where StarNet2 is.
pins:
  - {n: 1, x: 2.0, y: 72.9}
  - {n: 2, x: 2.0, y: 77.2}
  - {n: 3, x: 2.0, y: 85.0}
  - {n: 4, x: 2.0, y: 51.2}
  - {n: 5, x: 2.0, y: 20.9}
  - {n: 6, x: 2.0, y: 29.2}
  - {n: 7, x: 2.0, y: 59.6}
  - {n: 8, x: 66.0, y: 95.9}
```

## Before you start

- Install StarNet2 2.5.3 or newer with its whole package, as [Starnet++](starnet.md) explains, and read its own
  licence in `LICENSE.txt`. AfterNight does not include it.
- The image must be grayscale or RGB and not a raw mosaic: [debayer](../../reference/glossary.md#debayer) one first.
  StarNet2 asks for at least 512 by 512 pixels, which AfterNight does not check before the run.
- A grayscale image gives a mask of one channel, which **Lightness** and **Binary Mask** leave as it is.
- The mask already on the view, and the view's display mode, do not change what the program receives.
- The temporary files location, on the **General** page of the settings, needs room for a 16-bit copy of the image
  and the program's [starless image](../../reference/glossary.md#starless-image) and mask, for each image or frame.

## Steps

1. **Open the window.** Make the image or [sequence](../../reference/glossary.md#sequence) the
   [active image](../../reference/glossary.md#active-image) and choose **Process > Mask Generation > Starnet++ Star
   Mask**. The line above **Configure** says whether AfterNight can run StarNet2.
2. **Point AfterNight at StarNet2.** When the line names a problem, click **Configure**, then **Detect** beside
   **StarNet2:** and the settings' **OK**. The line reads "StarNet2 is configured and ready to use."
3. **Check the target.** **Target Image** names the image or sequence StarNet2 receives. The mask takes its name.
4. **Choose the pre-stretch.** Tick **Pre-Stretch linear image** for a
   [linear image](../../reference/glossary.md#linear-image), so the program sees a visible picture. Leave it off for
   a stretched image.
5. **Choose one channel or three.** Leave **Lightness** off to keep the program's color mask. Tick it for a gray mask
   of one channel.
6. **Make it black and white, if you want.** Tick **Binary Mask**, keep the stars' lightness between **Lower limit**
   and **Upper limit**, and raise **Smoothness** to soften the edges.
7. **Leave the program's settings.** Keep **Highlight protection** ticked, **Stride** at **Standard (256)**,
   **Weights** empty and **Upsample x2** off for a first run.
8. **Run.** Click **OK**. A progress window follows the program, and its **Cancel** stops it. The view
   `<target> - STARMASK` opens as a tab beside the target.
9. **Check at 100 %.** Compare the mask with the image at 100 %, in the same place. The stars are bright and the
   nebula dark.

## Controls

What every process window shares is on [Process Windows](../../interface/process-windows.md). This window reads its
controls when you press **OK**.

```controls
window: Starnet++ Star Mask
controls:
  - name: Lightness
    kind: check
    default: "Off"
    effect: For an RGB image, reduces the program's color mask to one gray channel, its CIE L* lightness divided by 100. Off keeps the mask as the program wrote it, with the image's three channels.
    enabled: Once the line above **Configure** says StarNet2 is ready. Its tick enables **Binary Mask**.
    tradeoff: The differences between the mask's colors are lost. The mask of a grayscale image has one channel already, and is left as it is.
    scope: The new mask only, after the program's run. The red, green and blue are read as the **Channel** list of a process window reads a color image.
    persistence: Until the window closes. The mask's lineage records the extraction of the lightness.
  - name: Binary Mask
    kind: check
    default: "Off"
    effect: Makes the lightness black and white by equation (3), 1 between **Lower limit** and **Upper limit** and 0 outside, then smooths it by **Smoothness**.
    enabled: With **Lightness** ticked, once the line above **Configure** says StarNet2 is ready. With **Lightness** off it is disabled, and its tick does nothing.
    tradeoff: The edges are hard until **Smoothness** softens them, with no ramp between the limits. A mask of one channel, from a grayscale image, is not selected, whatever the label suggests.
    persistence: Until the window closes. The mask's lineage records the limits and the smoothness.
  - name: Lower limit
    kind: spinbox
    default: "0.100000"
    widget: {min: 0, max: 1, step: 0.01, decimals: 6}
    effect: Sets the faintest lightness the mask keeps. Higher leaves the faint wings of stars and faint stars out. Lower takes more of them in, and more of what is not a star.
    range: The limit itself is kept. Raising it above **Upper limit** raises **Upper limit** with it. With the two equal, only that one level is kept.
    enabled: With **Lightness** and **Binary Mask** ticked, once the line above **Configure** says StarNet2 is ready.
    persistence: Until the window closes.
  - name: Upper limit
    kind: spinbox
    default: "1.000000"
    widget: {min: 0, max: 1, step: 0.05, decimals: 6}
    effect: Sets the brightest lightness the mask keeps. Lower leaves the brightest parts of the mask out.
    range: The limit itself is kept. Lowering it below **Lower limit** lowers **Lower limit** with it.
    enabled: With **Lightness** and **Binary Mask** ticked, once the line above **Configure** says StarNet2 is ready.
    persistence: Until the window closes.
  - name: Smoothness
    kind: spinbox
    default: "0.00"
    widget: {min: 0, max: 100, step: 1, decimals: 2}
    effect: Blurs the black and white mask with a Gaussian of this sigma, in pixels, the blur of Gaussian Blur. 0 keeps the edges hard.
    range: The blur is exact at every value the box offers.
    enabled: With **Lightness** and **Binary Mask** ticked, once the line above **Configure** says StarNet2 is ready.
    tradeoff: A larger sigma softens the edges further, spreads the mask beyond the stars, can join neighbouring stars into one region, and takes longer.
    scope: After the selection. It does not change the limits.
    persistence: Until the window closes.
  - name: Pre-Stretch linear image
    kind: check
    default: "Off"
    effect: Stretches the copy the program receives with one curve for every channel, from their statistics together, by equation (1). The mask is made from that stretched copy. Off hands the image as it is.
    enabled: Once the line above **Configure** says StarNet2 is ready.
    tradeoff: A linear image reaches the program as a visible picture instead of a nearly black one. The mask is never brought back through the inverse curve, unlike the starless image of Starnet++. A stretched image stretched again gives another mask.
    scope: Every channel alike, in the copy only.
    persistence: Until the window closes. The lineage records `pre-stretch linked`, or `pre-stretch none` when it is off.
  - name: Highlight protection
    kind: check
    default: "On"
    effect: On, StarNet2 keeps its own protection of bright and nearly saturated regions, as it does by default. Off passes `--disable-highlights-protection`.
    enabled: Once the line above **Configure** says StarNet2 is ready.
    tradeoff: Its author offers Off for comparison or troubleshooting. Off can leave worse marks around bright stars.
    persistence: Until the window closes.
  - name: Stride
    object: stridePreset
    kind: combo
    default: Standard (256)
    options: [Standard (256), Wide-field (384), Custom]
    effect: Sets how many pixels apart the program places the windows of 512 pixels it works in. **Standard (256)** is the program's own default, **Wide-field (384)** places them further apart, and **Custom** takes the box beside it.
    enabled: Once the line above **Configure** says StarNet2 is ready.
    tradeoff: Its author starts most images at 256 and suggests 384 for wide fields of generally small stars. A smaller stride usually costs more. How the program blends its windows is not published.
    scope: The program receives it as `--stride`.
    persistence: Until the window closes.
  - name: Stride
    object: customStrideValue
    kind: spinbox
    default: "256"
    widget: {min: 2, max: 512, step: 2}
    effect: Sets the stride when **Custom** is chosen. Lower overlaps the windows more.
    range: Even values only. An odd value you type is lowered to the even value below it.
    enabled: With **Custom** chosen, once the line above **Configure** says StarNet2 is ready.
    tradeoff: A smaller stride takes longer, and extreme values change how the windows overlap.
    persistence: Until the window closes. It keeps its value while the list shows another choice.
  - name: Weights
    kind: field
    default: ""
    effect: Names a model file for the program to run instead of its own. Empty runs the model of the package, `StarNet2_weights.onnx`.
    range: >-
      The empty field shows "Default StarNet2_weights.onnx" in gray. A path is passed after `--weights` as you typed
      it, without the spaces around it, and a relative path is read from the program's own folder, where it is
      started.
    enabled: Once the line above **Configure** says StarNet2 is ready.
    tradeoff: AfterNight does not check the file, its model or its licence before the run. An unsuitable file makes the run fail when the program starts, and another model gives another mask.
    persistence: Until the window closes. It is not saved. The lineage keeps it among the program's arguments.
  - name: Browse
    kind: button
    default: ""
    effect: Opens the file dialog "Select StarNet2 weights" and puts the file you choose in **Weights**.
    range: The dialog lists ONNX models, or all files. It opens in the folder of the file in **Weights**, or, with the field empty, in the program's own folder.
    enabled: Once the line above **Configure** says StarNet2 is ready.
    tradeoff: Choosing a file does not check it.
  - name: Upsample x2
    kind: check
    default: "Off"
    effect: Asks StarNet2 for its intermediate 2x mode, `--upsample`, which helps with very tight stars and with marks around them.
    enabled: Once the line above **Configure** says StarNet2 is ready.
    tradeoff: Its author says it can take about four times as long, and it needs more memory. Very large stars can become harder to handle.
    persistence: Until the window closes.
  - name: Status line
    kind: readout
    default: StarNet2 executable path is empty.
    effect: Says whether the program the settings name is one AfterNight can run. "StarNet2 is configured and ready to use." enables the controls and **OK**. Any other text keeps them disabled and says why.
    range: The texts are the ones the Starnet++ window shows, such as "StarNet2 executable was not found." and "Could not detect StarNet2 version. StarNet2 2.5.3 or newer is required."
    enabled: Worked out when the window opens and when the settings close after **Configure**, with `--machine-info` and then `--version`, within 3 seconds.
    tradeoff: Ready means the program answered a version of 2.5.3 or newer, not that its model and runtime can complete a run.
    scope: The window, and every run, which asks the program again before it writes anything.
    persistence: The program's path is the one the Starnet++ window uses, kept in the settings for every session.
  - name: Configure
    kind: button
    default: ""
    effect: Opens the settings at **External Tools**, whose **StarNet2:** holds the program's path. When the settings close, the window reads the saved path and asks the program again.
    range: "**Detect** there finds the program on the system's `PATH` or in the usual folders of its installers."
    enabled: Always, also while the other controls are disabled and while a run works.
    tradeoff: The check starts the program and sends it no image, so it does not check the model.
    persistence: The path is kept for every session. The window's own values are not saved.
  - name: Target Image
    kind: combo
    default: The active image
    options: [The active image, Every other open image or sequence the window can work on]
    effect: Chooses the image or sequence the program receives. The mask is named after it and opens beside it.
    range: Whether the program can take the image is checked when you press **OK**, before anything is written.
    enabled: Whether or not StarNet2 is ready, and also while a run works. A run keeps the view that was chosen when you pressed **OK**, and a change reaches the next run.
    tradeoff: An image another process is still changing is refused with "Another process is executing for this image, please cancel that job first before running this process", with "sequence" for a sequence. Click **OK** again once its result is shown.
    scope: The whole image, every channel, without the mask on the view.
    persistence: Until the window closes.
  - name: OK
    kind: button
    default: ""
    effect: Reads the window as it is, opens the progress window, runs StarNet2 for its star mask, shapes the mask as the window asks and opens it as `<target> - STARMASK` beside the target. The window stays open with its values for another run.
    enabled: >-
      Once the line above **Configure** says StarNet2 is ready. It is disabled from the press until the run has ended.
      The other controls stay enabled, and what you change reaches the next run only.
    tradeoff: >-
      StarNet2 always writes a starless image too, which AfterNight leaves unread. The run writes a 16-bit copy of the
      image and the program's two answers to disk and holds the copy and the mask in memory. **Lightness**,
      **Binary Mask** and **Smoothness** each add a pass over the image. The program's own work dominates, and grows
      with the image's area, **Upsample x2** and a smaller stride.
    scope: >-
      The whole image. On a sequence, every frame, whatever frames the player has selected, each debayered when the
      view debayers its frames, with one run of the program for each frame, into one new sequence published whole.
    persistence: >-
      The target and its history are not changed. The mask's history begins with `StarNet2: Starmask image created
      from '<target>'`. A name already in use is made unique. Nothing is saved to a file.
    note: >-
      A failed run says why in a message titled "Error running StarNet2", whose text starts "An error occurred when
      running StarNet2 process:", and in the log, and opens nothing.
  - name: Close
    kind: button
    default: ""
    effect: Closes the window. Its values are discarded.
    enabled: Always, also while a run works.
    note: Closing the window while a run works hides it, and it closes when the run has ended. It does not stop the run, whose mask still opens. The progress window's **Cancel** stops it.
```

```controls
window: Progress
controls:
  - name: Progress
    kind: readout
    default: ""
    effect: >-
      Follows the run, under the title `Starnet++ Star Mask <name>`. On an image, the text names each stage,
      "Preparing StarNet2 execution...", "Executing StarNet2..." and "Processing output...", and the bar follows the
      program's own percentages from 10 to 90 percent, then 95 while the mask is read and shaped. On a sequence, the
      text reads `Executing StarNet2 on <name>` and the bar counts the frames.
    tradeoff: The bar moves forward only. The program's printed percentages are read, or its report for machines when its `--machine-info` answer offers one.
    note: The log keeps the line `Executing <program> <arguments>`, and the program's other lines at the Debug level.
  - name: Cancel
    kind: button
    default: ""
    effect: Stops the run. The program's output is read at least every 300 milliseconds, and the program is ended at the next read. The progress window closes quietly, without a view and without a message. Click **OK** again for another run.
    range: The stop is also seen while the copy for the program is written, before the program starts, and before the mask is taken.
    persistence: The target and its history stay as they were, and no view opens. The temporary files are removed.
```

## How it works

1. When you press **OK**, the window reads its controls and its target. The run refuses a raw mosaic and an image of
   other than one or three channels before anything is written, then asks the program its version again.
2. The engine writes the image as unsigned 16-bit codes, rounded to the nearest code with ties to even, to
   `srcFile.tiff`, whole or not at all, in a folder of the run's own named after `starnet2` under the temporary files location. It maps the samples into 0 to 1 as
   [Starnet++](starnet.md) does and, with the pre-stretch, [stretches](../../reference/glossary.md#stretch) them by
   equation (1).
3. AfterNight starts the program from its own folder with `--input`, `--output`, `--mask` and `--stride`, and adds
   `--weights`, `--upsample`, `--disable-highlights-protection` and `--machine-progress` when they apply. The program
   writes a starless image and its star mask. AfterNight reads only the mask.
4. The mask is held to the image's width, height and channels, read as a
   [fraction of full scale](../../reference/glossary.md#fraction-of-full-scale) and written in the target's
   [pixel type](../../reference/glossary.md#pixel-type), rounded to the nearest value and held to its range. It is the
   program's own answer, never brought back through the curve or the range. **Lightness** and **Binary Mask** then
   shape a color mask by equations (3) and (4), in floating point, and the result goes back to the same pixel type.
5. The mask opens beside the target. It keeps the target's acquisition data,
   [plate solution](../../reference/glossary.md#plate-solution) and [lineage](../../reference/glossary.md#lineage),
   and has no other [header card](../../reference/glossary.md#header-card) of the target. Its
   [processing state](../../reference/glossary.md#processing-state) records it as nonlinear under
   `starnet-star-mask`, and its lineage gains an entry with the program's name and version, its arguments,
   `handed as UInt16` and the pre-stretch.

### Model

The pre-stretch uses the [midtones transfer function](../../reference/glossary.md#midtones-transfer-function)
\(f_m(t) = (m - 1)\,t \,/\, \bigl((2m - 1)\,t - m\bigr)\). For each channel \(j\), let \(v_j\) be the median of its
handed values and \(q_j\) 1.4826 times their
[median absolute deviation](../../reference/glossary.md#median-absolute-deviation), or 0.001 when that is zero. With
a bar for the mean over the channels, the one [black point](../../reference/glossary.md#black-point) \(S\) and balance
\(m\) of every channel, and the stretched value \(p\) of a handed value \(h\), are

\[ S = \max\Bigl(0,\ \overline{v_j - 2.8\,q_j}\Bigr), \qquad m = f_{0.25}\bigl(\overline{v_j} - S\bigr), \qquad p = f_m\!\Bigl(\frac{h - S}{1 - S}\Bigr) \tag{1} \]

When every channel's median is above 0.5, the roles turn round, as in
[Histogram Transformation](../stretching/histogram-transformation.md). StarNet2 documents its star mask as the
positive difference between what it received, \(r\), which is \(h\) or \(p\), and its starless answer \(s'\):

\[ k = \min\bigl(\max(r - s',\ 0),\ 1\bigr) \tag{2} \]

With **Lightness**, a color mask becomes one channel \(\ell\), the CIE L* of each pixel divided by 100. With
**Binary Mask**, let \(a\) be **Lower limit** and \(b\) **Upper limit**:

\[ B = \begin{cases} 1 & \text{if } a \le \ell \le b \\ 0 & \text{otherwise} \end{cases} \tag{3} \]

With **Smoothness** \(\sigma\) above 0, the result is \(B\) blurred by a Gaussian of that sigma, in pixels:

\[ G_\sigma(x, y) \propto \exp\!\Bigl(-\frac{x^2 + y^2}{2\sigma^2}\Bigr) \tag{4} \]

so an edge of the mask fades over about \(3\sigma\) pixels on each side. The selection comes first and the blur after,
as in [Range Selection](../analysis/range-selection.md).

### Boundaries

- StarNet2 2.5.3 refuses floating-point images, so every image is handed as 16-bit codes. For a floating-point image
  with samples below 0 or above 1, the mask is a fraction of the handed range, not of the image's own values.
- A pixel the image's [validity rule](../../reference/glossary.md#validity-rule) rejects is handed as 0 and left out of
  the range and the statistics. A sample that is not a finite number in a pixel the rule admits is refused with "a
  source holds a sample that is not finite in a pixel its validity admits".
- A failure of the program, or a mask it did not write or wrote at another size, opens nothing. On a sequence, a
  failure in any frame opens no sequence.
- The temporary folder and its three files are removed however the run ends, cancelled or not.
- How the network decides what is a star, and how it treats the edges and the meeting of its windows, is the
  program's own and not published.

### Worked example

The target is the Carina [master](../../reference/glossary.md#master) after Histogram Transformation with
**Shadows** and **Midtones** at 0.02. Its samples lie between 0 and 1, so the program receives them as they are.
Leave **Pre-Stretch linear image** off and the program's settings as they open. Tick **Lightness** and **Binary
Mask**, leave the limits at 0.10 and 1.00, set **Smoothness** to 2 and click **OK**. The view `RGB - STARMASK` opens
beside `RGB`, with one channel, in its 32-bit floating-point pixel type.

Where StarNet2 leaves a pixel as it was, equation (2) gives the mask 0: the background and the nebula are black.
Where the master holds 0.90 on a star and the starless image 0.40, the mask holds 0.50, whose lightness is above 0.5,
so equation (3) makes it white. Star edges of a lightness below 0.10 turn black, and equation (4) softens every edge
over about 6 pixels.

```figure
images:
  - image: ../../assets/images/processes/star-operations/starnet-star-mask-before.webp
    alt: A crop at 100 % of the middle of the stretched nebula, pink clouds of nebula beside a dark dust lane, dense with stars.
    label: Before
  - image: ../../assets/images/processes/star-operations/starnet-star-mask-after.webp
    alt: The same crop of the new mask, black with soft white discs where the stars were, large for the bright stars and small for the faint ones.
    label: After
caption: >-
  The middle of the stretched Carina master at 100 %, and of the mask that OK opened with Lightness and Binary Mask
  ticked and Smoothness at 2. Both crops are shown in the Linear display mode.
```

## Starting points

| Situation | Start | Then adjust |
| --- | --- | --- |
| The program's own mask | Every mask control off | Compare it with the image at 100 %, then decide on **Lightness**. |
| One gray mask | **Lightness** ticked, **Binary Mask** off | Compare it with the color mask first. |
| A black and white mask with soft edges | **Lightness** and **Binary Mask**, **Smoothness** at 2 | The limits first, then the smallest **Smoothness** that hides the edges. |
| Faint or tight stars missing | **Upsample x2**, or a lower **Lower limit** | Check that the nebula stays out. Change the stride only for a reason. |

## When it goes wrong

!!! warning "A sequence is processed whole"
    **OK** runs the program once for every frame, whatever frames the player has selected, and opens one new
    sequence. To make the mask of one frame, extract it first with **Image > Sequence > Extract Frame**.

!!! warning "The mask follows the stretched copy"
    With **Pre-Stretch linear image**, the mask is made from the stretched copy and stays in its units, not the
    linear image's.

"StarNet2 executable path is empty." and **OK** stays disabled
:   **Cause.** AfterNight does not know where the program is.
:   **Fix.** Click **Configure**, click **Detect** beside **StarNet2:** or browse to the program, and click the
    settings' **OK**.

"Could not detect StarNet2 version. StarNet2 2.5.3 or newer is required."
:   **Cause.** The program gave no version within 3 seconds, or one older than 2.5.3, which the line then names.
:   **Fix.** Install a current StarNet2 package whole, and choose its program again.

The limits and Smoothness do not change the mask
:   **Cause.** The image is grayscale, so the mask has one channel and is not shaped.
:   **Fix.** Use the mask as it is, or shape it with Range Selection afterwards.

"StarNet2 takes a grey or colour picture, not a colour filter array mosaic; demosaic the image first"
:   **Cause.** The image is a raw mosaic. "StarNet2 supports grayscale and RGB images only." means it has neither one
    channel nor three.
:   **Fix.** Debayer the mosaic, let the sequence's view debayer its frames, or make a grayscale or RGB copy.

"StarNet2 exited with code 1." followed by the program's lines
:   **Cause.** The program refused the run, often for an image under 512 pixels a side, a model file, its runtime or
    the disk space.
:   **Fix.** Read the program's lines, use an image of at least 512 by 512 pixels, clear **Weights**, and restore the
    package as installed.

"Could not start the StarNet2 process." or "StarNet2 process crashed."
:   **Cause.** The program's files are missing, blocked or were replaced.
:   **Fix.** Check the program's path and its permissions, and reinstall the package whole.

"the tool wrote no mask file", or "the tool's mask is ... where the image is ..."
:   **Cause.** The program ended well but left no mask, or one of another size.
:   **Fix.** Check the package's runtime files and its model, and that the image is large enough.

The mask covers nebula detail, or misses stars
:   **Cause.** The network took compact structure for stars, or missed soft, saturated or very tight ones.
:   **Fix.** Try the image without the pre-stretch or with it, then **Upsample x2**, and change the stride only with a
    reason. Paint out what remains with [Mask Drawing](../masking/mask-drawing.md).

## Related

- [Starnet++](starnet.md), for the starless image and the star layer from the same program.
- [Star Mask](star-mask.md), for a star mask found by measuring the stars, without an external program.
- [Range Selection](../analysis/range-selection.md), to shape a mask of one channel by brightness.
- [Masks](../../interface/masks.md), to attach, show and invert the mask on a view.
- [Star Reduction](star-reduction.md), one of the processes a star mask can protect or limit.
- [Process Windows](../../interface/process-windows.md), for what every process window shares.

## Credits

- [StarNet2](https://starnetastro.com/cli-tools/starnet/), the command-line tool and its trained model, by Mikita
  (Nikita) Misiura, who holds their copyright, under the StarNet2 Software License Agreement that comes with it as
  `LICENSE.txt`. AfterNight runs the program as it is installed and neither includes nor changes it.
- The [StarNet command-line reference](https://starnetastro.com/documentation/starnet/command-line-tool/), for its
  options, inputs, star mask output, stride, upsampling, highlight protection and minimum size.
- The [StarNet tips and tricks](https://starnetastro.com/documentation/starnet/tips-and-tricks/), for its advice on
  stride, upsampling, color input and checking a mask for structure the program took away.
- [ONNX Runtime](https://onnxruntime.ai), by Microsoft, under the MIT licence, which the Windows package of StarNet2
  carries with its notices to run the model.
