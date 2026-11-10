---
kind: process
stage: noise
menu: Process > Star Processing > Starnet++
summary: >-
  Removes the stars from an image or a sequence with StarNet2, a separately installed program, and opens the starless
  image, and the star mask or the star layer you ask for, as new views beside it.
data_in: either
data_out: nonlinear
destructive: false
preview: none
sequences: true
masks: none
external: The StarNet2 command-line tool, 2.5.3 or newer, with its ONNX model, installed separately
glance:
  enabled: >-
    An image or a sequence view is active, no Starnet++ window is open on it yet, and no window that needs the image
    for itself is open on it. The window opens whether or not StarNet2 is installed, and OK waits until the window
    says the program is ready.
  inputs: >-
    One grayscale or RGB image or sequence of any pixel type, not a raw color-filter mosaic. StarNet2 asks for at
    least 512 by 512 pixels.
  outputs: >-
    New views beside the target: the starless image, and the star mask, the star layer or both when you ask, in the
    target's pixel type and recorded as nonlinear. The target and its history are not changed, and no file is kept.
  state: >-
    A stacked master, linear with Pre-Stretch linear image ticked, or stretched with it off. Star Reduction and
    Starmask Recombination need a starless image of the stretched image.
derived: true
rewritten: true
---
# Starnet++

Starnet++ removes the stars from an image with StarNet2, a trained neural network that runs as a separate program
you install yourself. AfterNight hands it the image as a 16-bit file, runs it on your computer, and brings its
answers back to the image's own scale and [pixel type](../../reference/glossary.md#pixel-type). It takes a
[linear image](../../reference/glossary.md#linear-image) through an optional pre-stretch, or a stretched one as it is.
There is no preview.

**OK** opens the [starless image](../../reference/glossary.md#starless-image) as a new view beside the target and,
when you ask, the star [mask](../../reference/glossary.md#mask) and the star layer the program makes. The target
and its [history](../../reference/glossary.md#history) stay as they were, and the window stays open.

```figure
image: ../../assets/images/processes/star-operations/starnet.webp
alt: >-
  The Starnet++ window. Under the title bar, the process name and a line that says it extracts stars or starless
  output with StarNet2. In a card below, the check boxes Upsample x2 and Pre-Stretch linear image, clear, and
  Highlight protection and Generate Stars Output, ticked; Output reading Subtractive Mask; Stride reading Standard
  (256), with a grayed box reading 256 beside it; Weights, an empty field showing Default StarNet2_weights.onnx in
  gray, with a Browse button; Target Image reading RGB; the line StarNet2 is configured and ready to use; and a
  Configure button. OK and Close run along the bottom.
caption: >-
  Starnet++ at its defaults on the stretched Carina master, once AfterNight knows where StarNet2 is: the line above
  Configure says the program is ready.
pins:
  - {n: 1, x: 3.0, y: 77.7}
  - {n: 2, x: 3.0, y: 84.5}
  - {n: 3, x: 3.0, y: 70.3}
  - {n: 4, x: 3.0, y: 30.7}
  - {n: 5, x: 3.0, y: 41.9}
  - {n: 6, x: 3.0, y: 55.3}
  - {n: 7, x: 41.5, y: 94.2}
```

## Before you start

- Install StarNet2 2.5.3 or newer from its author's site, and keep its package together as it comes: the program,
  its model, its runtime files, its README and its licence. AfterNight does not include it. A native installer may
  put the program and the model in separate folders, as its README says.
- StarNet2 has a licence of its own, not AfterNight's. It allows astrophotography only, forbids using the program, its
  parts or its results to build commercial software or to train other networks, and limits sharing full-size raw
  images that show what it does. Read `LICENSE.txt` in its package.
- The image must be grayscale or RGB and not a raw mosaic: [debayer](../../reference/glossary.md#debayer) one first.
  StarNet2 asks for at least 512 by 512 pixels, which AfterNight does not check before the run.
- Choose when the stars come out. On the linear [master](../../reference/glossary.md#master), tick the pre-stretch.
  For [Star Reduction](star-reduction.md) and [Starmask Recombination](starmask-recombination.md), make the starless
  image from the stretched image, with the pre-stretch off.
- **OK** opens new views and leaves the target as it is. The mask on the view is not used, and the window has no
  preview, **Apply**, **Reset** or **Channel**. A star mask it opens is an ordinary image until you attach it through
  [Masks](../../interface/masks.md).
- The temporary files location, on the **General** page of the settings, needs room for a 16-bit copy of the image
  and up to three 16-bit answers, for each image or frame.

## Steps

1. **Open the window.** Make the image or [sequence](../../reference/glossary.md#sequence) the
   [active image](../../reference/glossary.md#active-image) and choose **Process > Star Processing > Starnet++**. The
   line above **Configure** says whether AfterNight can run StarNet2.
2. **Point AfterNight at StarNet2.** When the line names a problem, click **Configure**, then **Detect** beside
   **StarNet2:** and the settings' **OK**. The line reads "StarNet2 is configured and ready to use."
3. **Check the target.** **Target Image** names the image or sequence StarNet2 receives. The new views take its
   name.
4. **Choose the pre-stretch.** Tick **Pre-Stretch linear image** for a linear image, so the program sees a visible
   picture. Leave it off for a stretched image.
5. **Choose the star output.** Leave **Generate Stars Output** ticked and choose in **Output**: **Subtractive Mask**
   for stars you will add back, **Unscreen Star Layer** for stars you will screen back. Untick it for the starless
   image alone.
6. **Leave the rest for a first run.** Keep **Highlight protection** ticked, **Upsample x2** off, **Stride** at
   **Standard (256)** and **Weights** empty.
7. **Run.** Click **OK**. A progress window follows the program, and its **Cancel** stops it. The views
   `<target> - starless` and the star output open as tabs beside the target.
8. **Check at 100 %.** Zoom the starless view and the target to 100 % in the same display mode. The nebula stands
   without its stars: look there and in the star output for stars left behind, detail taken away, rings around
   bright stars and seams.

## Controls

What every process window shares is on [Process Windows](../../interface/process-windows.md). This window has no
preview: it reads its controls when you press **OK**.

```controls
window: Starnet++
controls:
  - name: Upsample x2
    kind: check
    default: "Off"
    effect: Asks StarNet2 for its intermediate 2x mode, `--upsample`. The program enlarges the image twice, removes the stars, and returns its answers at the image's size. It helps with very tight stars and with marks left around removed stars.
    enabled: Once the line above **Configure** says StarNet2 is ready.
    tradeoff: Its author says it can take about four times as long, and it needs more memory. Very large stars can become harder to remove. Use it when an image needs it, not by default.
    scope: The whole image, or every frame of a sequence.
    persistence: Until the window closes. The lineage keeps it among the program's arguments.
  - name: Pre-Stretch linear image
    kind: check
    default: "Off"
    effect: Stretches the copy the program receives, each channel by a curve from its own statistics, and brings the starless image and the star layer back through the inverse of that curve. Off hands the image as it is, mapped into 0 to 1 by equation (1).
    range: The curve is equation (2). It puts each channel's black point 2.8 deviations below its median and the median near 0.25. Neither number can be changed.
    enabled: Once the line above **Configure** says StarNet2 is ready.
    tradeoff: >-
      A linear image reaches the program as a visible picture instead of a nearly black one. The way back cannot
      restore what the curve sent to black or what the 16-bit file rounded, so the round trip is close, not exact.
      It changes the samples of the copy, unlike the Autostretch display of a view. A stretched image stretched again
      comes back with wrong tones, and the window does not check which kind of image it has.
    scope: Each channel on its own, in the copy only. The star mask is not brought back through the inverse, so it stays in the units of the stretched copy.
    persistence: Until the window closes. The lineage records `pre-stretch unlinked`, or `pre-stretch none` when it is off.
  - name: Highlight protection
    kind: check
    default: "On"
    effect: On, StarNet2 keeps its own protection of bright and nearly saturated regions in the starless image, as it does by default. Off passes `--disable-highlights-protection`.
    enabled: Once the line above **Configure** says StarNet2 is ready.
    tradeoff: Its author offers Off for comparison or troubleshooting. Off can leave worse marks around bright stars.
    persistence: Until the window closes.
  - name: Generate Stars Output
    kind: check
    default: "On"
    effect: On, the run also opens the star output that **Output** names. Off, it opens the starless image alone.
    enabled: Once the line above **Configure** says StarNet2 is ready. Its tick enables **Output**.
    tradeoff: The program runs once either way. Each output adds a file the program writes, a read, a conversion and a new view, with the disk and memory they take.
    scope: New views only. A star mask it opens is an ordinary image until you attach it to a view as its mask.
    persistence: Until the window closes.
  - name: Output
    kind: combo
    default: Subtractive Mask
    options: [Subtractive Mask, Unscreen Star Layer, Both]
    effect: >-
      **Subtractive Mask** opens `<target> - star mask`, what the program took away: the image less the starless
      image, held between 0 and 1, by equation (5). **Unscreen Star Layer** opens `<target> - stars`, the stars as a
      layer to screen onto the starless image, by equation (4). **Both** opens the two, the mask first.
    range: The program receives `--mask`, `--unscreen` or both.
    enabled: With **Generate Stars Output** ticked, once the line above **Configure** says StarNet2 is ready.
    tradeoff: >-
      The two are different images and are not interchangeable. Add the star mask back, with Starmask Recombination
      for example. Screen the star layer back: added, its stars return too bright. **Both** lets you compare them.
    scope: >-
      With **Pre-Stretch linear image**, the star layer comes back through the inverse curve, which puts its black at
      each channel's black point rather than at 0, and the star mask stays in the stretched units.
    persistence: Until the window closes.
  - name: Stride
    object: stridePreset
    kind: combo
    default: Standard (256)
    options: [Standard (256), Wide-field (384), Custom]
    effect: Sets how many pixels apart the program places the windows of 512 pixels it works in. **Standard (256)** is the program's own default, **Wide-field (384)** places them further apart, and **Custom** takes the box beside it.
    enabled: Once the line above **Configure** says StarNet2 is ready.
    tradeoff: Its author starts most images at 256, suggests 384 for wide fields of generally small stars, and finds values below 256 rarely useful. How the program blends its windows is not published.
    scope: The whole image, or every frame of a sequence, all channels together. The program receives it as `--stride`.
    persistence: Until the window closes. The lineage keeps the stride that ran.
  - name: Stride
    object: customStrideValue
    kind: spinbox
    default: "256"
    widget: {min: 2, max: 512, step: 2}
    effect: Sets the stride when **Custom** is chosen. Lower overlaps the windows more.
    range: Even values only. An odd value you type is lowered to the even value below it.
    enabled: With **Custom** chosen, once the line above **Configure** says StarNet2 is ready.
    tradeoff: A smaller stride places more windows and takes longer. Extreme values change how the windows overlap, and there is no preview to judge them.
    persistence: Until the window closes. It keeps its value while the list shows another choice.
  - name: Weights
    kind: field
    default: ""
    effect: Names a model file for the program to run instead of its own. Empty runs the model of the package, `StarNet2_weights.onnx`.
    range: >-
      The empty field shows "Default StarNet2_weights.onnx" in gray. A path is passed to the program after `--weights`
      as you typed it, without the spaces around it, and a relative path is read from the program's own folder, where
      it is started.
    enabled: Once the line above **Configure** says StarNet2 is ready.
    tradeoff: >-
      AfterNight does not check the file, its model or its licence before the run. A missing or unsuitable file makes
      the run fail when the program starts. Another model gives another result, and may take another time.
    scope: The whole image, or every frame of a sequence.
    persistence: Until the window closes. It is not saved. The lineage keeps it among the program's arguments.
    note: Try a model on a duplicate, with every other control as it was for a run with the default model, so the two results can be compared.
  - name: Browse
    kind: button
    default: ""
    effect: Opens the file dialog "Select StarNet2 weights" and puts the file you choose in **Weights**.
    range: >-
      The dialog lists ONNX models, or all files. It opens in the folder of the file in **Weights**, or, with the
      field empty, in the program's own folder. The Windows installer keeps the model in `lib\starnet2` beside the
      program's `bin` folder, as `C:\Program Files\StarNet2\lib\starnet2`.
    enabled: Once the line above **Configure** says StarNet2 is ready.
    tradeoff: Choosing a file does not check it.
    persistence: The path stays in **Weights** until the window closes.
  - name: Target Image
    kind: combo
    default: The active image
    options: [The active image, Every other open image or sequence the window can work on]
    effect: Chooses the image or sequence the program receives. The new views are named after it and open beside it.
    range: Whether the program can take the image is checked when you press **OK**, before anything is written.
    enabled: Whether or not StarNet2 is ready, and also while a run works. A run keeps the view that was chosen when you pressed **OK**, and a change reaches the next run.
    tradeoff: A larger image or a sequence needs more disk, memory and time.
    scope: The whole image, every channel, without the mask on the view.
    persistence: Until the window closes.
  - name: Status line
    kind: readout
    default: StarNet2 executable path is empty.
    effect: Says whether the program the settings name is one AfterNight can run. "StarNet2 is configured and ready to use." enables the controls and **OK**, in green. Any other text, in orange, keeps them disabled and says why.
    range: >-
      The other texts are "StarNet2 executable path is empty.", "StarNet2 executable was not found.", "StarNet2 path is
      not executable.", "The configured executable returned machine information for a product other than StarNet2.",
      "Could not detect StarNet2 version. StarNet2 2.5.3 or newer is required." and "StarNet2 2.5.0 detected. StarNet2
      2.5.3 or newer is required.", with the version the program gave.
    enabled: >-
      Worked out when the window opens and when the settings close after **Configure**. The program is asked
      `--machine-info` first, and a program that gives no such answer is asked `--version`, as StarNet2 2.5.3 is. Both
      answers must come within 3 seconds, which the window can wait for while it opens.
    tradeoff: Ready means the program answered a version of 2.5.3 or newer, not that its model and runtime can complete a run.
    scope: The window, and every run, which asks the program again before it writes anything.
    persistence: The program's path is kept in the settings, as `tools/starnetPath`, for every session and for both StarNet2 windows. The line is worked out again each time.
  - name: Configure
    kind: button
    default: ""
    effect: Opens the settings at **External Tools**, whose **StarNet2:** holds the program's path. When the settings close, the window reads the saved path and asks the program again.
    range: >-
      **Detect** there looks for `starnet2.exe` on the system's `PATH`, then in `C:\Program Files\StarNet2\bin`,
      `C:\Program Files\StarNet2`, and `StarNet2`, `AppData\Local\StarNet2\bin` and `AppData\Local\StarNet2` in your
      home folder. On Linux it looks for `starnet2` in `/usr/bin`, `/usr/local/bin`, `/opt/StarNet2/bin`,
      `/opt/starnet2/bin`, `~/StarNet2` and `~/.local/bin`, and on macOS in `/usr/local/bin`, `/opt/homebrew/bin`,
      `~/StarNet2` and `~/.local/bin`. It accepts only a supported version, and the page then shows
      "✓ StarNet2 2.5.3 detected", with the version found.
    enabled: Always, also while the other controls are disabled and while a run works.
    tradeoff: The check starts the program and sends it no image, so it does not check the model.
    persistence: The path is kept for every session. The window's own values are not saved.
  - name: OK
    kind: button
    default: ""
    effect: >-
      Reads the window as it is, opens the progress window and runs StarNet2 over the target. When every answer is
      back, it opens the starless image, then the star mask, then the star layer, as new views beside the target. The
      window stays open with its values for another run.
    enabled: >-
      Once the line above **Configure** says StarNet2 is ready. It is disabled from the press until the run has ended.
      The other controls stay enabled, and what you change reaches the next run only.
    tradeoff: >-
      The run writes a 16-bit copy of the image and the program's answers to disk and holds them in memory. The
      program's own work dominates, and grows with the image's area, **Upsample x2** and a smaller stride. It chooses
      its own device: its Windows package tries CUDA when a CUDA runtime was put beside it, then DirectML on the
      graphics card, then the processor. AfterNight neither chooses nor reports it, and its own thread and memory
      settings do not reach the program.
    scope: >-
      The whole image. On a sequence, every frame, whatever frames the player has selected, each debayered when the
      view debayers its frames, with one run of the program for each frame. Each output becomes a new sequence of
      every frame, published whole.
    persistence: >-
      The target and its history are not changed. The new views' histories begin with `StarNet2: Starless image
      created from '<target>'`, `StarNet2: Star mask created from '<target>'` and `StarNet2: Unscreen stars image
      created from '<target>'`. A name already in use is made unique. Nothing is saved to a file.
    note: >-
      A failed run says why in a message titled "Error running StarNet2", whose text starts "An error occurred when
      running StarNet2 process:", and in the log, and opens nothing.
  - name: Close
    kind: button
    default: ""
    effect: Closes the window. Its values are discarded.
    enabled: Always, also while a run works.
    note: Closing the window while a run works hides it, and it closes when the run has ended. It does not stop the run, whose views still open. The progress window's **Cancel** stops it.
```

```controls
window: Progress
controls:
  - name: Progress
    kind: readout
    default: ""
    effect: >-
      Follows the run, under the title `Starnet++ <name>`. On an image, the text names each stage, "Preparing StarNet2
      execution...", "Executing StarNet2..." and "Processing output...", and the bar follows the program's own
      percentages from 10 to 90 percent, then 95 while the answers are read. On a sequence, the text reads
      `Executing StarNet2 on <name>` and the bar counts the frames.
    tradeoff: >-
      The bar moves forward only. StarNet2 2.5.3 prints its percentages, which AfterNight reads. A version whose
      `--machine-info` answer says it reports its progress for machines is asked for `--machine-progress`, and that
      report, on the program's error stream, is read instead.
    note: >-
      The log keeps the line `Executing <program> <arguments>`. The program's other lines go to the log at the Debug
      level, set on the Logging page of the settings, where a line such as "Backend: ONNX Runtime (DirectML execution
      provider)" names the device it chose. When the program fails, its lines follow the error message.
  - name: Cancel
    kind: button
    default: ""
    effect: Stops the run. The program's output is read at least every 300 milliseconds, and the program is ended at the next read. The progress window closes without a view and without a message.
    range: The stop is also seen before anything is written, once the copy for the program is written, and after each answer is read.
    tradeoff: The program is ended at once, with no chance to finish. The temporary files are removed.
    persistence: The target and its history stay as they were, and no view opens.
```

## How it works

1. When you press **OK**, the window reads its controls and its target. The run refuses a raw mosaic and an image of
   other than one or three channels before anything is written, then asks the program its version again.
2. The engine writes the image as unsigned 16-bit codes, rounded to the nearest code with ties to even, to
   `srcFile.tiff`, whole or not at all, in a folder of the run's own named after `starnet2` under the temporary files
   location. Each sample is mapped by equation (1) and, with the pre-stretch, stretched by equation (2). The program
   receives the samples only, not the image's file or header.
3. AfterNight starts the program from its own folder with `--input`, `--output` and `--stride`, adds `--mask`,
   `--unscreen`, `--weights`, `--upsample`, `--disable-highlights-protection` and `--machine-progress` when they
   apply, and never passes `--eight` or `--quiet`. The program writes its answers as 16-bit TIFF files.
4. Each answer is read, held to the image's width, height and channels, and computed in double precision: the
   starless image by equation (3), the star layer by equation (4), and the star mask as the program wrote it, as a
   [fraction of full scale](../../reference/glossary.md#fraction-of-full-scale). Each is written once in the target's
   pixel type, rounded and held to its range.
5. Once every answer is back, the views open. Each keeps the target's acquisition data,
   [plate solution](../../reference/glossary.md#plate-solution) and [lineage](../../reference/glossary.md#lineage),
   and has no other [header card](../../reference/glossary.md#header-card) of the target. Its
   [processing state](../../reference/glossary.md#processing-state) records it as nonlinear under `starnet-starless`,
   `starnet-star-mask` or `starnet-stars`, even a starless image brought back linear. Its lineage gains an entry with
   the program's name and version, its arguments, `handed as UInt16` and the pre-stretch. The temporary folder is
   removed.

### Model

For a floating-point image, \(L\) is the smaller of 0 and the smallest finite valid sample, and \(H\) the larger of 1
and the largest. A sample \(v\) is handed as

\[ h = \frac{v - L}{H - L} \tag{1} \]

and written as the nearest of the codes 0 to 65,535, \(65535\,h\). An integer image uses \(L = 0\) and \(H = 1\), its
samples read as fractions of full scale. Samples below 0 or above 1 reach the program whole, and the rest share fewer
codes.

The pre-stretch uses the [midtones transfer function](../../reference/glossary.md#midtones-transfer-function)
\(f_m(t) = (m - 1)\,t \,/\, \bigl((2m - 1)\,t - m\bigr)\). For each channel \(j\), let \(v_j\) be the median of its
handed values and \(q_j\) 1.4826 times their
[median absolute deviation](../../reference/glossary.md#median-absolute-deviation), or 0.001 when that is zero. The
[black point](../../reference/glossary.md#black-point) \(S_j\), the balance \(m_j\) and the stretched value \(p\) of
a handed value \(h\) are

\[ S_j = \max(0,\ v_j - 2.8\,q_j), \qquad m_j = f_{0.25}(v_j - S_j), \qquad p = f_{m_j}\!\Bigl(\frac{h - S_j}{1 - S_j}\Bigr) \tag{2} \]

and a value at or below \(S_j\) becomes 0. When every channel's median is above 0.5, the roles turn round, as in
[Histogram Transformation](../stretching/histogram-transformation.md). Let
\(g_j(p') = S_j + (1 - S_j)\,f_{1 - m_j}(p')\) be the inverse of that curve, which gives \(S_j\) for an answer of 0.
Without the pre-stretch, \(g_j(p') = p'\). A starless answer \(s'\) comes back as

\[ s = L + (H - L)\,g_j(s') \tag{3} \]

AfterNight treats the star layer as one the program screens onto its starless answer, so a layer answer \(u'\) gets
back the span and not the offset:

\[ u = (H - L)\,g_j(u') \tag{4} \]

Let \(r\) be what the program received: \(h\), or \(p\) with the pre-stretch. StarNet2 documents its star mask as
the positive difference between \(r\) and its starless answer, each between 0 and 1:

\[ k = \min\bigl(\max(r - s',\ 0),\ 1\bigr) \tag{5} \]

The mask is read as it is, without equations (1) and (3) the other way. So for an image between 0 and 1 without the
pre-stretch, the starless image plus the star mask gives the image back, to within rounding. With the pre-stretch, or
with samples beyond 0 to 1, they do not add up to it.

### Boundaries

- StarNet2 2.5.3 refuses floating-point images, so every image is handed as 16-bit codes in a TIFF file, also to a
  version that reads [FITS](../../reference/glossary.md#fits) files, and a 32-bit or 64-bit result gains no
  precision. A program that changes nothing gives the image back within one code of the handed range,
  \((H - L)/65535\), which is 0.000015 for an image between 0 and 1. Through the pre-stretch it comes back within
  0.000062, or four codes of a 16-bit image, and samples the curve sent to black come back at the black point.
- A pixel the image's [validity rule](../../reference/glossary.md#validity-rule) rejects is handed as 0, left out of
  \(L\), \(H\) and the statistics, and comes back valid with the program's answer. A sample that is not a finite
  number in a pixel the rule admits is refused with "a source holds a sample that is not finite in a pixel its
  validity admits". A channel without a valid sample cannot be pre-stretched.
- Every answer is read before any view opens, so a failure of the program or of one answer opens nothing. Only a
  view the application itself cannot open leaves the views opened before it.
- The temporary folder is removed however the run ends, and a failed or cancelled run leaves the target as it was.
- How the network treats the image's edges, the meeting of its windows, the highlight protection and the star layer
  is the program's own, and not published.

### Worked example

The target is the Carina master after Histogram Transformation with **Shadows** and **Midtones** at 0.02. Its samples
lie between 0 and 1, so equation (1) uses \(L = 0\) and \(H = 1\) and hands each sample as it is: the background near
0.28 becomes code 18,350. Leave the controls as they open: **Pre-Stretch linear image** off, **Highlight protection**
on, **Generate Stars Output** on with **Subtractive Mask**, **Standard (256)** and **Weights** empty. Click **OK**.
When the run ends, the views `RGB - starless` and `RGB - star mask` open beside `RGB`, in its 32-bit floating-point
pixel type.

Where StarNet2 leaves a pixel as it was, the star mask holds 0. Where the master holds 0.90 on a star and the starless
image 0.40, equation (5) gives the mask 0.50, and [Starmask Recombination](starmask-recombination.md) adds the two back
to 0.90.

```figure
images:
  - image: ../../assets/images/processes/star-operations/starnet-before.webp
    alt: A crop at 100 % of the middle of the stretched nebula, pink clouds of nebula beside a dark dust lane, dense with stars.
    label: Before
  - image: ../../assets/images/processes/star-operations/starnet-after.webp
    alt: The same crop of the starless view, the same clouds and dust lanes with the stars gone and the nebula filled in where they were.
    label: After
caption: >-
  The middle of the stretched Carina master at 100 %, and of the starless view that OK opened at the window's
  defaults. Both crops are shown in the Linear display mode.
```

On the linear master instead, tick **Pre-Stretch linear image**. Its samples run from about -0.02 to 1.84, so
equation (1) hands the median of 0.0277 as 0.0256, and equation (2) gives each channel \(S \approx 0.0217\) and
\(m \approx 0.0116\). The program then sees the background near 0.25 instead of a nearly black picture. Where it
leaves the background as it was, equation (3) brings it back near 0.0277.

## Starting points

| Situation | Start | Then adjust |
| --- | --- | --- |
| A linear master, before the [stretch](../../reference/glossary.md#stretch) | **Pre-Stretch linear image** ticked, every other control as it opens | Compare at 100 %. If bright filaments are gone, try a darker stretched image with the pre-stretch off. |
| A stretched image, for Star Reduction or Starmask Recombination | **Pre-Stretch linear image** off, **Subtractive Mask** | Add the mask back to the starless image, and compare the sum with the image. |
| A wide field of small stars | **Wide-field (384)** | Compare with **Standard (256)** on a duplicate: small stars left, seams and time. |
| Very tight stars left behind, or marks around removed stars | **Standard (256)** with **Upsample x2** | Expect about four times the time, and check the largest stars. Change one control per run. |

## When it goes wrong

!!! warning "A sequence is processed whole"
    **OK** runs the program once for every frame, whatever frames the player has selected, and opens one new
    sequence for each output. To process one frame, extract it first with **Image > Sequence > Extract Frame**.

!!! warning "The star mask and the star layer go back in different ways"
    The star mask is a difference, to add back. The star layer is a layer to screen back, and added it makes the stars
    too bright. With the pre-stretch, the mask stays stretched while the starless image comes back linear, so the
    two no longer add up to the image.

!!! danger "A plausible starless image can hide lost detail"
    StarNet2 can take compact nebula or galaxy detail for stars, and can leave soft, saturated, elongated or
    undersampled stars behind. Look at the star output at 100 %, not only at the starless image.

"StarNet2 executable path is empty." and **OK** stays disabled
:   **Cause.** AfterNight does not know where the program is.
:   **Fix.** Click **Configure**, click **Detect** beside **StarNet2:** or browse to the program, and click the
    settings' **OK**.

"Could not detect StarNet2 version. StarNet2 2.5.3 or newer is required."
:   **Cause.** The program gave no version within 3 seconds, or one older than 2.5.3, which the line then names.
:   **Fix.** Install a current StarNet2 package whole, and choose its program again. Run `starnet2 --version` in a
    terminal to see what it answers.

**OK** is disabled although the line says the program is ready
:   **Cause.** A run of this window is still working.
:   **Fix.** Wait for the progress window to close, or click its **Cancel**.

"StarNet2 takes a grey or colour picture, not a colour filter array mosaic; demosaic the image first"
:   **Cause.** The image is a raw mosaic. "StarNet2 supports grayscale and RGB images only." means it has neither one
    channel nor three.
:   **Fix.** Debayer the mosaic, let the sequence's view debayer its frames, or make a grayscale or RGB copy.

"StarNet2 exited with code 1." followed by the program's lines
:   **Cause.** The program refused the run, often for an image under 512 pixels a side, a model file or its runtime.
:   **Fix.** Read the program's lines, use an image of at least 512 by 512 pixels, clear **Weights**, and restore the
    package as installed.

"Could not start the StarNet2 process." or "StarNet2 process crashed."
:   **Cause.** The program's runtime files are missing or were replaced, or security software stopped it.
:   **Fix.** Reinstall the package whole, keep its files where it put them, and run again with **Weights** empty. A
    package that reads FITS files also needs the CFITSIO and zlib files it comes with.

"the tool wrote no image file", or "the tool's image is ... where the image is ..."
:   **Cause.** The program ended well but left no answer, or one of another size. The same is said of the mask and
    the star layer.
:   **Fix.** Restore the package and clear **Weights**.

"Could NOT set temporary dir for StarNet2 execution"
:   **Cause.** The temporary files location cannot hold a new folder.
:   **Fix.** Check its place, its permissions and its free space on the **General** page of the settings.

"Another process is executing for this image, please cancel that job first before running this process"
:   **Cause.** Another process is still changing the target. It reads "sequence" for a sequence.
:   **Fix.** Wait until its result is shown, or cancel it, then click **OK** again.

The starless image looks stretched, or its tones are wrong
:   **Cause.** The pre-stretch ran on a stretched image, or the curve sent faint detail to black, which the way back
    cannot restore.
:   **Fix.** Run again from the unchanged image, with **Pre-Stretch linear image** off for a stretched image.

The window disappeared while the run went on
:   **Cause.** **Close** was pressed during the run, which hides the window until the run ends.
:   **Fix.** Wait for the views to open, or click **Cancel** in the progress window to stop the run.

## Related

- [Starnet++ Star Mask](starnet-star-mask.md), for a star mask from StarNet2 shaped to one channel or to black and
  white.
- [Star Mask](star-mask.md), for a star mask found by measuring the stars, without an external program.
- [Star Reduction](star-reduction.md), whose **Generate starless view** opens this window to make its starless
  image, with no star output, and closes it when the image opens.
- [Starmask Recombination](starmask-recombination.md), to add the star mask back to the starless image.
- [Settings](../../interface/settings-dialog.md), whose **External Tools** page holds the program's path.
- [Process Windows](../../interface/process-windows.md), for what every process window shares.

## Credits

- [StarNet2](https://starnetastro.com/cli-tools/starnet/), the command-line tool and its trained model, by Mikita
  (Nikita) Misiura, who holds their copyright, under the StarNet2 Software License Agreement that comes with it as
  `LICENSE.txt`. AfterNight runs the program as it is installed and neither includes nor changes it.
- The [StarNet command-line reference](https://starnetastro.com/documentation/starnet/command-line-tool/), for its
  options, inputs, outputs, star mask, star layer, stride, upsampling, highlight protection, minimum size and package
  layout.
- The [StarNet tips and tricks](https://starnetastro.com/documentation/starnet/tips-and-tricks/), for its advice on
  stride, upsampling, color input and checking what the starless image lost.
- The [locations of the command-line installers](https://starnetastro.com/documentation/cli-installers/), for the
  versions, runtimes and installed paths of each platform.
- [ONNX Runtime](https://onnxruntime.ai), by Microsoft, under the MIT licence, which the Windows package of StarNet2
  carries with its notices to run the model.
