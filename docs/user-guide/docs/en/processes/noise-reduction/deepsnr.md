---
kind: process
stage: noise
menu: Process > Noise Reduction > DeepSNR
summary: >-
  Reduces noise with DeepSNR, a separately installed command-line tool that runs a trained neural network over the
  image in windows of 512 pixels, and replaces the image with its answer, brought back to the image's own scale.
data_in: either
data_out: either
destructive: true
preview: none
sequences: true
masks: none
external: The DeepSNR command-line tool, 1.2.0 or newer, with its ONNX models, installed separately
glance:
  enabled: >-
    An image or a sequence view is active, no DeepSNR window is open on it yet, and no window that needs the image for
    itself is open on it. The window opens whether or not DeepSNR is installed, and OK waits until the window says the
    program is ready.
  inputs: >-
    One grayscale or RGB image or sequence of any pixel type, at least 512 by 512 pixels and not a raw color-filter
    mosaic.
  outputs: >-
    The same view, replaced, with one history step. Its size, channels, pixel type, header, plate solution and recorded
    processing state are kept, and every sample is marked valid. The temporary files are removed and no file is kept.
  state: >-
    A calibrated and stacked master, linear or stretched, after gradient removal, whose noise changes from one pixel
    to the next rather than in patterns.
derived: true
rewritten: true
---
# DeepSNR

DeepSNR reduces noise with a trained neural network. AfterNight hands the image, as a 16-bit file, to the DeepSNR
command-line tool you install yourself, runs it on your computer, and brings its answer back to the image's own scale
and [pixel type](../../reference/glossary.md#pixel-type). The program works in windows of 512 pixels and is made for
noise that changes from one pixel to the next. There is no preview and no strength control.

**OK** replaces the image or [sequence](../../reference/glossary.md#sequence) with the denoised one, keeps its size,
channels and header, and adds one step to its [history](../../reference/glossary.md#history). The window stays open.

```figure
image: ../../assets/images/processes/noise-reduction/deepsnr.webp
alt: >-
  The DeepSNR window. Under the title bar, the process name and a line that says it reduces high-frequency noise with
  the DeepSNR command-line tool. In a card below, the Pre-Stretch linear image check box, ticked; Stride, with a box
  reading Default (480) and a greyed box reading 480 beside it; Weights, an empty field showing Default
  DeepSNR_weights_v2.onnx in grey, with a Browse button; Target Image reading RGB; the line DeepSNR is configured and
  ready to use; and a Configure button. OK and Close run along the bottom.
caption: >-
  DeepSNR on the linear Carina master with Pre-Stretch linear image ticked, Stride at Default (480) and Weights empty,
  once AfterNight knows where the program is.
pins:
  - {n: 1, x: 75.0, y: 71.0}
  - {n: 2, x: 33.0, y: 79.8}
  - {n: 3, x: 97.3, y: 62.1}
  - {n: 4, x: 3.0, y: 32.8}
  - {n: 5, x: 97.3, y: 41.8}
  - {n: 6, x: 97.3, y: 51.5}
  - {n: 7, x: 44.0, y: 92.4}
```

## Before you start

- Install DeepSNR 1.2.0 or newer from its author's site, and keep its package together as it comes. AfterNight does
  not include it, and the network is needed only to download it.
- DeepSNR has a licence of its own, not AfterNight's. It allows astrophotography only, forbids using the program, its
  parts or its results to build commercial software, and limits sharing full-size raw images that show what it does.
  Read `LICENSE.txt` in its package.
- Denoise a calibrated and stacked [master](../../reference/glossary.md#master) whose noise is random. Its author
  recommends data of monochrome cameras, in grayscale, RGB or narrowband, and says drizzled one-shot color data may
  work.
- The image must be grayscale or RGB, at least 512 pixels on each side, and not a raw mosaic:
  [debayer](../../reference/glossary.md#debayer) one first.
- **OK** replaces the image, and its history keeps the step before. A [mask](../../reference/glossary.md#mask) on the
  view is not used, and nothing sets the strength: keep a duplicate to compare or blend.
- The temporary files location, on the **General** page of the settings, needs room for a 16-bit copy of the image
  and the program's answer.

## Steps

1. **Open the window.** Make the master the [active image](../../reference/glossary.md#active-image) and choose
   **Process > Noise Reduction > DeepSNR**. The line above **Configure** says whether the program is ready.
2. **Point AfterNight at DeepSNR.** When the line names a problem, click **Configure**, then **Detect** beside
   **DeepSNR:** and the settings' **OK**. The line reads "DeepSNR is configured and ready to use."
3. **Check the target.** **Target Image** names the image or sequence **OK** replaces.
4. **Choose the pre-stretch.** Tick **Pre-Stretch linear image** for a
   [linear image](../../reference/glossary.md#linear-image), so the program sees a visible picture. Leave it off for a
   stretched one.
5. **Leave the stride.** Keep **Stride** at **Default (480)** for the first run.
6. **Leave the weights.** Keep **Weights** empty, so the program runs its own model.
7. **Run.** Click **OK**. A progress window follows the program, and its **Cancel** stops it. The view shows the
   denoised image, and its history lists **DeepSNR** with the stride and the pre-stretch.
8. **Check at 100 %.** Zoom the view to 100 % over faint nebula, small stars and the background, and compare with the
   step before in the image's history, in the same display mode. The grain is gone, and faint structure and small
   stars are as they were.

## Controls

What every process window shares is on [Process Windows](../../interface/process-windows.md).

```controls
window: DeepSNR
controls:
  - name: Pre-Stretch linear image
    kind: check
    default: "Off"
    effect: Stretches the copy the program is handed, each channel by a curve from its own statistics, and brings the answer back through the inverse of that curve. Off hands the image as it is, mapped into 0 to 1 by equation (1).
    range: The curve is equation (2). It puts each channel's shadows end 2.8 deviations below its median and the median near 0.25. Neither number can be changed.
    enabled: Once the line above **Configure** says DeepSNR is ready. Disabled while a run works.
    tradeoff: >-
      A linear image reaches the program as a gray, grainy picture instead of a nearly black one. The way back cannot
      restore what the 16-bit hand-off rounded or what the model changed, so the round trip is close, not exact. A
      stretched image stretched again can come back with wrong tones. It adds a measurement of the whole image and two
      passes over it.
    scope: The whole image, or every frame of a sequence, each channel on its own. The window does not look at whether the image is linear, so the choice is yours.
    persistence: Until the window closes. The history records it as enabled or disabled.
  - name: Stride
    object: stridePreset
    kind: combo
    default: Default (480)
    options: [Default (480), Conservative (256), Custom]
    effect: Sets how many pixels apart the program places its windows of 512 pixels. **Default (480)**, the program's own default, overlaps neighbours by 32 pixels, **Conservative (256)** by 256, and **Custom** takes the box beside it.
    enabled: Once the line above **Configure** says DeepSNR is ready. Disabled while a run works.
    tradeoff: More overlap can hide the seams between windows, and repeats work. **Conservative (256)** evaluates more than three times as many windows as **Default (480)**. Less overlap is faster and leaves the program fewer shared pixels where it merges the windows.
    scope: The whole image, or every frame of a sequence, all channels together. The program receives it as `--stride`.
    persistence: Until the window closes. The history records the stride that ran.
  - name: Stride
    object: customStrideValue
    kind: spinbox
    default: "480"
    widget: {min: 2, max: 512, step: 2}
    effect: Sets the stride when **Custom** is chosen. Lower overlaps the windows more, and 512 leaves no overlap between neighbours.
    range: Even values only. An odd value you type is lowered to the even value below it.
    enabled: With **Custom** chosen, once the line above **Configure** says DeepSNR is ready. Disabled while a run works.
    tradeoff: Halving the stride roughly quadruples the windows the model evaluates, and the time with them, and a very small stride need not look better. 512 does the least work and gives the merge no shared pixels.
    persistence: Until the window closes. It keeps its value while the list shows another choice.
  - name: Weights
    kind: field
    default: ""
    effect: Names a model file for the program to run instead of its own. Empty runs the program's default, model 2, which takes grayscale and RGB images.
    range: >-
      The empty field shows "Default DeepSNR_weights_v2.onnx" in grey. A path is passed to the program after
      `--weights` as you typed it, without the spaces around it, and a relative path is read from the program's own
      folder.
    enabled: Once the line above **Configure** says DeepSNR is ready. Disabled while a run works.
    tradeoff: >-
      AfterNight does not check the file, its model, the channels it takes or its licence. A file the program cannot
      use makes the run fail, and another model gives another result. Weights are no strength control. A larger model
      takes longer to load and more memory.
    scope: The whole image, or every frame of a sequence, with the channels the model takes.
    persistence: Until the window closes. It is not saved, and the history step leaves it out. The lineage keeps it among the program's arguments.
    note: The package's folder of models holds `DeepSNR_weights_v1.onnx` and `DeepSNR_weights_v2.onnx`. Model 1 is the older model and takes RGB images only. Change the weights only after a run with the default model to compare with, and with a model whose origin, channels and licence you know.
  - name: Browse
    kind: button
    default: ""
    effect: Opens the file dialog "Select DeepSNR weights" and puts the file you choose in **Weights**.
    range: >-
      The dialog lists ONNX models, or all files. It opens in the folder of the file in **Weights**. With the field
      empty, it opens in the package's folder of models, `lib\deepsnr` beside the program's `bin` folder when there
      is one, such as `C:\Program Files\DeepSNR\lib\deepsnr`, and in the program's own folder otherwise.
    enabled: Once the line above **Configure** says DeepSNR is ready. Disabled while a run works.
    tradeoff: A model of unknown origin can fail, give an unsuitable result or come under another licence.
    persistence: The dialog's folder is remembered for the next time. The path stays in **Weights** until the window closes.
  - name: Target Image
    kind: combo
    default: The active image
    options: [The active image, Every other open image or sequence]
    effect: Chooses the image or sequence that **OK** denoises and replaces.
    range: It lists every open view, whatever its size, channels or kind. Whether the program can take the image is checked when you press **OK**, before anything is written.
    enabled: Whether or not DeepSNR is ready. Disabled while a run works.
    tradeoff: >-
      A larger image needs more disk, memory and time, and a sequence runs the program once for each frame. A crop to
      try settings on must be 512 pixels a side or more, and shows neither the seams nor how the model suits the rest
      of the field: try them on a duplicate of the whole image.
    persistence: Until the window closes. A run keeps the view that was chosen when you pressed **OK**.
  - name: Status line
    kind: readout
    default: DeepSNR executable path is empty.
    effect: Says whether the program AfterNight knows is one it can run. "DeepSNR is configured and ready to use." enables the controls and **OK**. Any other text keeps them disabled and says why.
    range: >-
      The other texts are "DeepSNR executable path is empty.", "DeepSNR executable was not found.", "DeepSNR path is
      not executable.", "The configured executable returned machine information for a product other than DeepSNR.",
      "Could not detect DeepSNR version. DeepSNR 1.2.0 or newer is required." and "DeepSNR 1.1.0 detected. DeepSNR
      1.2.0 or newer is required.", with the version the program gave.
    enabled: >-
      Worked out when the window opens and when the settings close after **Configure**. The program is asked
      `--machine-info` first, and its version, and whether it reports its progress for machines, are read from that
      answer. A program that gives no such answer is asked `--version`. Both answers must come within 3 seconds, which
      the window can wait for while it opens.
    tradeoff: >-
      Ready means the program answered a version of 1.2.0 or newer, not that its models and runtime can complete a
      run. AfterNight checks no newest version and no package hash, and a package can change without AfterNight.
    scope: The window, and every run, which asks the program again before it writes anything.
    persistence: The program's path is kept in the settings, as `tools/deepsnrPath`, for every session and every DeepSNR window. The line is worked out again each time. After a run it reads ready again without asking the program.
    note: >-
      For a path that is not found or not executable, click **Configure** and choose the program itself. Keep the
      program, its runtime files, its models, its README and its licence where its installer or archive put them, and
      do not mix files of different packages. For a version that cannot be read, run `deepsnr --machine-info` or
      `deepsnr --version` in a terminal to see what the program answers.
  - name: Configure
    kind: button
    default: ""
    effect: Opens the settings at **External Tools**, whose **DeepSNR:** holds the program's path. When the settings close, the window reads the saved path and asks the program again.
    range: >-
      **Detect** there looks for `deepsnr.exe` on the system's `PATH`, then in `C:\Program Files\DeepSNR\bin`,
      `C:\Program Files\DeepSNR`, and `DeepSNR`, `AppData\Local\DeepSNR\bin` and `AppData\Local\DeepSNR` in your home
      folder. On Linux and macOS it looks for `deepsnr` in `/usr/local/bin`, `~/DeepSNR` and `~/.local/bin`, with
      `/usr/bin`, `/opt/DeepSNR/bin` and `/opt/deepsnr/bin` on Linux and `/opt/homebrew/bin` on macOS. It accepts only
      a supported version. The page then shows "✓ DeepSNR 1.2.2 detected", with the version found.
    enabled: Whether or not DeepSNR is ready. Disabled while a run works.
    tradeoff: The check starts the program and sends it no image. A path that fails the check can still be saved, and the window then stays disabled and says why.
    persistence: The path is kept for every session. The window's own values are not saved.
  - name: OK
    kind: button
    default: ""
    effect: Runs DeepSNR over the target with the values as they are when you press it, and replaces the image, or every frame of the sequence, with the answer. The window stays open with its values for another run.
    enabled: Once the line above **Configure** says DeepSNR is ready. It is disabled, with every control but **Close**, until the run has ended.
    tradeoff: >-
      The run holds the image, its result and the program's working memory, and writes a 16-bit copy of the image and
      the program's answer to disk. It can take minutes on a large master, and loading the model, the two files and
      the conversions add to the program's own time. The program chooses its own device: on Windows it tries CUDA when
      a CUDA runtime was put beside it, then DirectML on the graphics card, then the processor. Its packages for Linux
      and Intel macOS run ONNX Runtime, and those for Apple silicon CoreML. A runtime replaced by hand can keep it from
      starting: restore the package's own files then. AfterNight's own thread and memory settings do not reach it.
    scope: >-
      The whole image, every channel, without the mask on the view. On a sequence, every frame, whatever frames the
      player has selected, each debayered when the view debayers its frames, written into a new sequence that replaces
      the old one only when every frame is done.
    persistence: >-
      The history step is **DeepSNR** and reads, for example, `Denoised with DeepSNR. Stride: 480. Pre-stretch:
      enabled`. It records neither the weights nor the program's version, which the lineage keeps. The view keeps its
      name, and nothing is saved to a file.
    note: >-
      A failed run says why in a message titled "Error running DeepSNR", whose text starts "An error occurred when
      running DeepSNR process:", and in the log, and the image stays as it was. "Could NOT set temporary dir for
      DeepSNR execution" means the temporary files location cannot hold a new folder: check its place, its permissions
      and its free space on the General page of the settings. "the tool wrote no image file" and "the tool's image is
      ... where the image is ..." mean the program ended well but left no answer, or one of another size: restore the
      package and clear **Weights**. A sequence that another run holds shows "Another process is executing for this
      sequence, please cancel that job first before running this process" under the title `DeepSNR <name>`, and
      nothing runs.
  - name: Close
    kind: button
    default: ""
    effect: Closes the window. Its values are discarded.
    enabled: Always, also while a run works.
    note: Closing the window while a run works hides it, and it closes when the run has ended. It does not stop the run, which the progress window's **Cancel** does.
```

```controls
window: Progress
controls:
  - name: Progress
    kind: readout
    default: ""
    effect: >-
      Follows the run, under the title `DeepSNR <name>`, for an image and for a sequence. On an image, the text names
      each stage, "Preparing DeepSNR execution...", "Executing DeepSNR..." and "Processing output...", and the bar
      follows the program's own percentages from 10 to 90 percent. On a sequence, the text reads
      `Executing DeepSNR on <name>` and the bar counts the frames.
    tradeoff: The bar moves forward only. A program that does not report its progress for machines has its printed percentages read instead.
    note: >-
      The log keeps the line `Executing <program> <arguments>`. The program's other lines go to the log at the Debug
      level, set on the Logging page of the settings, where its line "Backend: ONNX Runtime (DirectML execution
      provider)" or the like names the device it chose. When the program fails, its lines follow the error message.
  - name: Cancel
    kind: button
    default: ""
    effect: Stops the run. The program is ended within about a second, and the progress window closes without a result and without a message.
    tradeoff: The program is ended at once, with no chance to finish what it was doing. Its temporary files are removed. When the time was the reason, a larger stride shortens the next run.
    persistence: The image or sequence and its history stay as they were.
```

## How it works

1. The run refuses a raw mosaic, an image of other than one or three channels and one under 512 pixels on a side,
   before anything is written. It then asks the program its version again.
2. The engine writes the image as unsigned 16-bit codes to `srcFile.tiff`, in a folder of the run's own under the
   temporary files location: each sample mapped by equation (1) and, with the pre-stretch, stretched by equation (2).
   The program receives the samples only, not the image's file or header.
3. AfterNight starts the program from its own folder with `--input`, `--output` and `--stride`, adds `--weights` and
   `--machine-progress` when they apply, and never passes `--model`, `--quiet` or `--eight`. The program denoises
   window by window and writes `dstFile.tiff`, a 16-bit TIFF compressed with LZW.
4. The answer is held to the image's width, height and channels and brought back through the inverses of equations
   (2) and (1), in double precision. It is written once in the image's pixel type, rounded and held to its range.
5. The result replaces the target with one history step. It keeps the image's
   [header cards](../../reference/glossary.md#header-card), [plate solution](../../reference/glossary.md#plate-solution),
   acquisition data and [processing state](../../reference/glossary.md#processing-state), not the answer file's
   header. Every sample is marked valid, the [lineage](../../reference/glossary.md#lineage) gains an entry, and the
   temporary folder is removed.

### Model

For a floating-point image, \(L\) is the smaller of 0 and the smallest finite valid sample, and \(H\) the larger of 1
and the largest. A sample \(v\) is handed as

\[ h = \frac{v - L}{H - L} \tag{1} \]

and written as the nearest of the codes 0 to 65,535, \(65535\,h\). An integer image uses \(L = 0\) and \(H = 1\), its
samples read as [fractions of full scale](../../reference/glossary.md#fraction-of-full-scale). Samples below 0 or
above 1 reach the program whole, and the rest share fewer codes.

The pre-stretch uses the [midtones transfer function](../../reference/glossary.md#midtones-transfer-function)
\(f_m(t) = (m - 1)\,t \,/\, \bigl((2m - 1)\,t - m\bigr)\). For each channel \(j\), let \(v_j\) be the median of its
handed values and \(q_j\) 1.4826 times their [median absolute deviation](../../reference/glossary.md#median-absolute-deviation),
or 0.001 when that is zero. The shadows end \(S_j\), the balance \(m_j\) and the stretched value \(p\) of a handed
value \(h\) are

\[ S_j = \max(0,\ v_j - 2.8\,q_j), \qquad m_j = f_{0.25}(v_j - S_j), \qquad p = f_{m_j}\!\Bigl(\frac{h - S_j}{1 - S_j}\Bigr) \tag{2} \]

and a value at or below \(S_j\) becomes 0. When every channel's median is above 0.5, the roles turn round, as in
[Histogram Transformation](../stretching/histogram-transformation.md). The program's answer \(p'\) comes back as
\(S_j + (1 - S_j)\,f_{1 - m_j}(p')\), and then through equation (1) the other way.

The program places a window of 512 pixels every \(s\) pixels, the stride, so two neighbours share

\[ o = 512 - s \tag{3} \]

pixels. Where it places the last windows and how it blends them is not published.

### Boundaries

- The program refuses floating-point images, so every image is handed as 16-bit codes, and a 32-bit or 64-bit result
  gains no precision. A program that changes nothing gives the image back within one code of the handed range,
  \((H - L)/65535\), which is 0.000015 for an image between 0 and 1. Through the pre-stretch it comes back within
  0.000062, or four codes of a 16-bit image.
- A pixel the image's [validity rule](../../reference/glossary.md#validity-rule) rejects is handed as 0, left out of
  \(L\), \(H\) and the statistics, and comes back valid with the program's answer. A sample that is not a finite
  number in a pixel the rule admits is refused with "a source holds a sample that is not finite in a pixel its
  validity admits": replace it, or mark it invalid, first. A channel without a valid sample cannot be pre-stretched.
- How the trained model treats the edges, invalid pixels and the meeting of its windows is the program's own, and
  not published.
- A failed, crashed or cancelled run leaves the target as it was.

### Worked example

The linear Carina master is 7246 by 5429 pixels. Its channels have their median at 0.0277, and 1.4826 times their
median absolute deviation is 0.0026. Its samples run from about -0.02 to 1.84, so equation (1) takes \(L = -0.02\)
and \(H = 1.84\). One code of the handed file is then 0.000028, and that spread covers about 90 codes.
The median is handed as 0.0256, near code 1,680: without the pre-stretch the program sees a nearly black picture.

Tick **Pre-Stretch linear image**, leave **Stride** at **Default (480)** and **Weights** empty, and click **OK**. For
each channel, equation (2) gives \(S \approx 0.0217\) and \(m \approx 0.0116\). The median is handed at about 0.254,
near code 16,650, and the spread at about 0.07: the program sees a gray, grainy background. On the way back, the
inverse returns the background near 0.0277. The history reads
`Denoised with DeepSNR. Stride: 480. Pre-stretch: enabled`.

## Starting points

| Situation | Start | Then adjust |
| --- | --- | --- |
| A linear master | **Pre-Stretch linear image** ticked, **Default (480)**, **Weights** empty | Compare at 100 %. If the tones look wrong, try the pre-stretch off on a duplicate. |
| A stretched image | **Pre-Stretch linear image** off, **Default (480)** | Compare at 100 %, under the same display. |
| Squares or seams in the result | Undo, then **Conservative (256)** with the same **Weights** | **Custom** below 256 only if 256 still shows them. |
| The result looks too smooth | **Weights** empty, since nothing sets the strength | Blend it with a duplicate of the original afterwards. |

## When it goes wrong

!!! warning "A sequence is denoised whole"
    **OK** runs the program once for every frame, whatever frames the player has selected, and replaces the sequence
    as one history step. To denoise one frame, extract it first with **Image > Sequence > Extract Frame**.

!!! warning "Patterned noise stays"
    Walking noise, banding and calibration residue look like structure to the model. Prevent walking noise with
    [dithering](../../reference/glossary.md#dithering), and correct the rest at calibration and stacking or with a
    process made for the pattern, before DeepSNR. A smaller stride or other weights do not make up for them.

"DeepSNR executable path is empty." and **OK** stays disabled
:   **Cause.** AfterNight does not know where the program is.
:   **Fix.** Click **Configure**, click **Detect** beside **DeepSNR:** or browse to the program, and click the
    settings' **OK**.

"Could not detect DeepSNR version. DeepSNR 1.2.0 or newer is required."
:   **Cause.** The program gave no version within 3 seconds, or one older than 1.2.0, which the line then names.
:   **Fix.** Install a current DeepSNR package whole, and choose its program again.

"DeepSNR requires input images at least 512x512 pixels."
:   **Cause.** The image is smaller than the program's window on a side.
:   **Fix.** Use a larger image or another denoiser, rather than enlarge the image.

"DeepSNR takes a grey or colour picture, not a colour filter array mosaic; demosaic the image first"
:   **Cause.** The image is a raw mosaic. "DeepSNR supports grayscale and RGB images only." means it has neither one
    channel nor three.
:   **Fix.** Debayer the mosaic, or make a grayscale or RGB copy.

"DeepSNR exited with code 1." followed by the program's lines
:   **Cause.** The program refused the run, often for a model file, its runtime, its device or its input.
:   **Fix.** Read the program's lines, clear **Weights**, restore the package as installed, and check the free disk
    space.

"Could not start the DeepSNR process." or "DeepSNR process crashed."
:   **Cause.** The program's runtime files are missing or were replaced, or security software stopped it.
:   **Fix.** Reinstall the package whole, and run again with **Weights** empty.

The result shows a pattern of squares
:   **Cause.** The windows overlap too little for this image, or the model does not suit it.
:   **Fix.** Undo the step, choose **Conservative (256)** with the same **Weights**, and compare the same places at
    100 %.

Faint structure, small stars or the tones changed
:   **Cause.** The model smoothed what it took for noise, or the pre-stretch ran on a stretched image, which the
    window does not check.
:   **Fix.** Undo the step, tick the pre-stretch only for a linear image, and blend the result with a duplicate of the
    original to keep part of it.

## Related

- [Image Integration](../stacking-integration/image-integration.md) or
  [Image Stacking](../stacking-integration/image-stacking.md), and
  [Auto Gradient Removal](../background-extraction/auto-gradient-removal.md) or
  [Background Extraction](../background-extraction/background-extraction.md), which come before.
- [Wavelet Denoise](wavelet-denoise.md), [Total Variation Denoise](total-variation-denoise.md),
  [NL-Bayes](nl-bayes.md) and [Patch PCA Denoise](patch-pca-denoise.md), denoisers you can preview and dose.
- [Banding Reduction](banding-reduction.md), for patterned noise.
- [Settings](../../interface/settings-dialog.md), whose **External Tools** page holds the program's path.
- [Image History](../../interface/image-history.md), to compare with the step before and undo it.
- [Process Windows](../../interface/process-windows.md), for what every process window shares.

## Credits

- [DeepSNR](https://starnetastro.com/cli-tools/deepsnr/), the command-line tool and its trained models, by Mikita
  (Nikita) Misiura, who holds their copyright, under the DeepSNR Software License Agreement that comes with it as
  `LICENSE.txt`. AfterNight runs the program as it is installed and neither includes nor changes it.
- The [DeepSNR command-line reference](https://starnetastro.com/documentation/deepsnr/command-line-tool/), for its
  options, inputs, outputs, models, 512-pixel minimum, stride and package layout.
- The [DeepSNR guidance on input data](https://starnetastro.com/documentation/deepsnr/data-quality/), for the cameras
  and channels it suits and its limits with correlated noise such as walking noise.
- The [locations of the command-line installers](https://starnetastro.com/documentation/cli-installers/), for the
  versions, hashes, runtimes and installed paths of each platform.
- [ONNX Runtime](https://onnxruntime.ai), by Microsoft, under the MIT licence, which the Windows package of DeepSNR
  carries with its notices to run the model.
- DeepSNR names NAFNet as the basis of its network: the
  [official NAFNet implementation](https://github.com/megvii-research/NAFNet), and Liangyu Chen, Xiaojie Chu, Xiangyu
  Zhang and Jian Sun, [Simple Baselines for Image Restoration](https://arxiv.org/abs/2204.04676), ECCV 2022, which
  describes a restoration network that replaces the usual nonlinear activation functions with simpler operations such
  as multiplication. They explain the family of the network, not DeepSNR's weights or its training.
