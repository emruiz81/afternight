---
kind: reference
summary: >-
  What a reader looks up about process windows: each message a window shows or logs, the windows that differ from the
  rest, how the real-time preview works while you drag, and what each kind of run leaves behind.
rewritten: true
---
# Process Windows Reference

This page holds what [Process Windows](process-windows.md) leaves out to stay readable beside a window: the messages
a process window shows and writes to the Session Console, the windows that differ from the rest, how the real-time
preview works while you drag, what each kind of run leaves behind, and what a window keeps between sessions. Each
section can be read on its own, and the lists of windows use the titles of the process pages.

## Messages

### When a window opens

A process that cannot open on the [active image](../reference/glossary.md#active-image) says why in a message box when
you choose it. A process an extension adds is disabled in the menu instead, and the message is its tooltip. A process
that needs a color image is disabled in the menu while a grayscale image is active.

"No selected image to process"
:   No image or [sequence](../reference/glossary.md#sequence) view is active. Click the view you mean, then choose the process
    again.

"This process requires an RGB image.", "This process requires a mono image." or "This process does not support the selected image channel count."
:   The process works on images of other channels: NBRGB Combination, for example, needs an RGB image. Choose
    another image, or convert this one with **Convert to RGB** or **Convert to Grayscale**.

"This process supports still images only, not sequences."
:   Auto Gradient Removal, NBRGB Combination and NB Color Mapper refuse a sequence. Extract a frame and work on it.

"An existing process of this type is already running for the target image"
:   A window of the same process is already open on the image, perhaps behind another window. Use it, or close it.

"An existing process running for the target image requires exclusive access, please close that process before executing other processes."
:   A window that needs the image to itself is open on it ([Windows that differ](#windows-that-differ)). Apply or
    close that window first.

"This process requires exclusive access to the target image, please close all the currently open processes using this image before executing the process."
:   The process you chose needs the image to itself, and another window is open on it. Close the other windows on the
    image first.

`Could NOT create the preview for this process: there is no image to preview`
:   The target held no image when the preview was asked for. Wait until the image has loaded, then tick **Preview**
    again.

### While a window is open

`The target image '<name>' has been closed. The process will be closed as well.`
:   The target's view was closed, and the window closes with it. Nothing was applied.

`The target image '<name>' has changed and isn't supported by this process any more. The process will be closed.`
:   An edit made the target an image the window cannot work on, such as a grayscale image in a window that needs RGB.
    Undo the edit in the image's [history](../reference/glossary.md#history), or open the window on another image.

"The preview failed", with the reason
:   The process refused the preview, and the message strip says why, once for each different reason. Two reasons
    come from the [mask](../reference/glossary.md#mask) on the target's view: "the mask has another size than the image" and
    "the mask has neither one channel nor one for every channel of the result". Change the value the reason names,
    or give the mask the image's size and one channel or three.

"Another process is executing for this image, please cancel that job first before running this process"
:   Another window is applying its process to the same image. It reads "sequence" in place of "image" for a sequence.
    Wait for that run, or cancel it, then apply again.

"Error executing the process", with "An error occurred when running the process:" and the reason
:   The run failed and wrote nothing. The message shows in the window's strip and in a message box. Correct what the
    reason names and apply again. A reason that names the memory budget asks for less memory: close other images, or
    raise **Memory Limit** on the **Processing** page of the settings, which takes effect at the next start.

"Preparing execution...", then `Executing process in sequence. Frame 3/120`
:   The texts of the progress window while a run works. They are no error.

"Canceling execution..."
:   **Cancel** was pressed in the progress window. The run stops at its next stop point.

### In the Session Console

The [Session Console](log-viewer.md) keeps these lines of a window's work.

| Line | When |
| --- | --- |
| `Executing process '<process>' on image '<image>'...` | A run starts on an image; on a sequence the line says `sequence`. |
| `Process '<process>' executed successfully on image '<image>'. Execution time was 1.25 seconds` | An Apply in place has finished and is shown. |
| `Process '<process>' executed successfully, created new processed image '<name>'` | A run that writes a new image has finished. |
| `New image snapshot '<name>' taken from real time preview of process '<process>'` | **Take snapshot** opened its image. |
| `Process '<process>' cancelled by the user` | A run stopped by **Cancel**, or by closing the window. |
| `Process '<process>' produced a result that was not published: its image changed or closed under it` | The target changed or closed while the run worked, and the result was dropped. Apply again on the image as it is now. |
| `Preview of process '<process>' failed: <reason>` | The same failure as the window's "The preview failed". |
| `'<process>': work on a worker threw and its run ends with a failure: <reason>` | The process failed in an unexpected way. The window reports the failure. Try again, and report it with the log if it repeats. |
| `'<process>': work on a worker threw and its run was dropped: <reason>` | The same, for work whose result nothing waited for. Nothing is shown or recorded. |

## Windows that differ

| What differs | Windows |
| --- | --- |
| Need the image to themselves: they do not open beside another window, and no other window opens on the image while they are open | Crop, Rotate, Background Extraction, Auto Gradient Removal, Background Neutralization, Color Calibration, Photometric Color Calibration, Spectrophotometric Color Calibration, Star Profiler v1 and Star Profiler v2 |
| Choose their images themselves and open without one; opening one leaves the Dashboard or the image views as they were | Image Calibration, Image Registration, Image Stacking, Image Integration, Image Integration Lite, Image List Analyzer, Channel Composition, Merge [CFA](../reference/glossary.md#cfa), Narrowband Palette Composer, Starmask Recombination and Profiled Starmask Recombination |
| Keep their values for the next window | Image Calibration, Image Registration, Image Stacking, Image Integration, Image Integration Lite and Image List Analyzer |
| Show a progress window, with **Cancel**, for a single image too | Auto Gradient Removal, Background Extraction, Banding Reduction, Bilateral Filter, Cosmetic Correction, Dark Structure Enhance, DeepSNR, GRAYCstoration, Local Contrast Enhancement, Local Histogram Equalization, Median Filter, Multiscale Linear Transform, NL-Bayes, Noise Generator, Patch PCA Denoise, Sharpen Alpha, Spectrophotometric Color Calibration, Star Mask, Starnet++, Starnet++ Star Mask, Total Variation Denoise, Unsharp Mask and Wavelet Denoise |
| Offer **Display Mode** and [**Autostretch Settings**](../reference/glossary.md#autostretch) in the preview's toolbar | Auto Gradient Removal, Background Extraction, Banding Reduction, Bilateral Filter, Binarize, Cosmetic Correction, Dark Structure Enhance, Exposure Correction, Gaussian Blur, GRAYCstoration, Image Blend, Image Compare, LRGB Combination, Mask Drawing, Median Filter, Morphological Transformation, Multiscale Linear Transform, Narrowband Palette Composer, NB Color Mapper, NBRGB Combination, NL-Bayes, Noise Generator, Patch PCA Denoise, Profiled Starmask Recombination, Range Selection, Star Mask, Starmask Recombination, Star Reduction, Tighten Edges, Total Variation Denoise, Unsharp Mask and Wavelet Denoise |
| Have no [**Preview area**](../reference/glossary.md#preview-area) | Auto Gradient Removal, Background Extraction and Narrowband Palette Composer |
| Open with **Preview** off | Profiled Starmask Recombination |
| Write a new image or sequence in place of the target | Mask Drawing, Narrowband Palette Composer, NB Color Mapper, NBRGB Combination, Profiled Starmask Recombination, Range Selection, Star Mask, Starmask Recombination, Starnet++, Starnet++ Star Mask, and Star Reduction when its settings ask for one |
| Close when the new image is made | Mask Drawing, Profiled Starmask Recombination, Range Selection, Star Mask and Starmask Recombination |
| Do not blend through the target's mask | Mask Drawing, Narrowband Palette Composer, Profiled Starmask Recombination, Range Selection, Star Mask and Starmask Recombination |
| Go on with a run when the window is closed, until the run ends or its progress window's **Cancel** stops it | Background Neutralization, CFA Integer Resample, Color Calibration, Crop, [Debayer](../processes/calibration/debayer.md), DeepSNR, Extract Sequence, Integer Resample, Linear Match, Pixel Math, Resize, Rotate, SCNR, Split CFA, Starnet++ and Starnet++ Star Mask |
| Name the two images of the preview in their own words | Image Blend (PREVIEW and BASE IMAGE), Image Compare (IMAGE 1 and IMAGE 2) and Star Reduction (PREVIEW and TARGET IMAGE) |

The windows without a [real-time preview](../reference/glossary.md#real-time-preview), such as Pixel Math, Crop, Resize, Rotate,
the color calibration windows and the windows of external tools, run their own way. They read the window, the images it names and the view's mask
when you press **Apply** or **OK**, so a change made during the run does not reach it. Which controls they disable
while they work, and what they keep, is their pages' to say. The stacking windows and the windows of extensions have
controls and runs of their own too, and their pages say which parts of [Process Windows](process-windows.md) apply.

### Windows that open with a preview area

The area starts in the middle of the image, at the size below or the image's own when it is smaller, with the label
shown in its frame.

| Window | Size, in pixels of the image | Label |
| --- | --- | --- |
| Banding Reduction | 700 × 700 | Preview: Banding Reduction |
| Bilateral Filter | 800 × 800 | Preview: Bilateral Filter |
| Cosmetic Correction | 600 × 600 | Preview: Cosmetic Correction Filter |
| Dark Structure Enhance | 800 × 800 | Preview: DarkStructureEnhance |
| GRAYCstoration | 500 × 500 | Preview: GRAYCstoration Filter |
| Local Contrast Enhancement | 600 × 600 | Preview: Local Contrast Enhancement |
| Local Histogram Equalization | 600 × 600 | Preview: Local Histogram Equalization |
| Median Filter | 600 × 600 | Preview: Median Filter |
| Multiscale Linear Transform | 1200 × 1200 | Preview: Multiscale Linear Transform |
| NBRGB Combination | 512 × 512 | NBRGB preview |
| NL-Bayes | 500 × 500 | Preview: Non-Local Bayes Filter |
| Noise Generator | 1200 × 1200 | Preview: Noise Generator |
| Patch PCA Denoise | 350 × 350 | Preview: Patch PCA Denoise Filter |
| Sharpen Alpha | 600 × 600 | Preview: Multiscale Sharpen Alpha |
| Star Mask | 600 × 600 | Preview: Star Mask |
| Star Reduction | 1024 × 768 | Star Reduction Preview |
| Total Variation Denoise | 800 × 800 | Preview: Total Variation Denoise |
| Unsharp Mask | 1200 × 1200 | Preview: Unsharp Mask |
| Wavelet Denoise | 500 × 500 | Preview: Wavelet Denoise Filter |

NB Color Mapper opens with the area off; ticked, it is 512 × 512 and labelled "NB Color Mapper preview". Every other
window's area is labelled "Preview area".

### The histogram toolbar of each window

| Windows | Buttons |
| --- | --- |
| Adjust Brightness/Contrast, Adjust [HSL](../reference/glossary.md#hsl), Adjust LCh, ArcSinh [Stretch](../reference/glossary.md#stretch), CLAHE, Color Saturation, Colorize, Exposure Correction, Gamma Correction, High Pass Filter, Image Blend, Local Contrast Enhancement, Local Histogram Equalization, LRGB Combination, NB Color Mapper and NBRGB Combination | **Zoom in**, **Zoom out**, **Zoom reset**, **Show grid**, **Show clipping info**, **Absolute scales**, **Draw histogram areas**, **Show histogram**, **Plot resolution** |
| Image Compare and Statistical Stretch | The same without **Show histogram** |
| Histogram Transformation | **Zoom in**, **Zoom out**, **Zoom reset**, **Show grid**, **Show transfer curve**, **Absolute scales**, **Draw histogram areas**, **Plot resolution** |
| Generalized Hyperbolic Transformation | **Zoom in**, **Zoom out**, **Zoom reset**, **Show grid**, **Transfer curve**, **Show clipping info**, **Absolute scales**, on when the window opens, **Draw histogram areas**, **Plot resolution** |
| Curves Transformation | **Zoom in**, **Zoom out**, **Zoom reset**, **Show grid**, **Show guide**, **Draw histogram areas**, **Show histogram** |

## The preview while you drag

- **One preview at a time.** Previews and runs work away from the window, which stays responsive. While one runs, or
  while the rate below holds the next one back, further changes collapse into one more preview. It starts with the
  values the window holds when the running one ends, and a newer request replaces one still waiting.
- **The rate.** **Edit > Settings... > Processing > Updates per second while dragging** sets the most previews a
  window starts in a second while you drag one of its sliders: 30 unless you change it, from 5 to 60. It is a
  ceiling, not a promise that a process reaches it. A change applies to the next preview, and a window reads it
  again when you return to it.
- **Dragging and resting.** While you drag a slider, or a point of the curve in Curves Transformation, the previews
  run on the threads kept for previews, which leaves the rest of the computer to other work. Letting go, or holding
  the slider still for a moment, runs one more preview with every thread a preview may use: the preview the window
  rests on. A change made without dragging, a value typed or a box ticked, runs one such preview at once.
- **Windows that drag on what the pane shows.** Adjust Brightness/Contrast, Gamma Correction, Exposure Correction,
  Color Saturation, Adjust HSL, Adjust LCh, Curves Transformation, Gaussian Blur, Unsharp Mask, High Pass Filter and
  Range Selection work, while you drag, on the image reduced to the pane's size, or on the part the pane shows at
  100 % and closer. A drag then keeps up on a large frame, and the preview it rests on is computed on the whole image,
  as **Apply** computes it. Over the reduced image, Unsharp Mask and High Pass Filter come close to a reduction of
  their final result rather than matching it, because their clipping does not commute with averaging. Beyond a
  **Sigma** or a radius of 32 in a dragged preview, Gaussian Blur and High Pass Filter blur with three box passes,
  whose cost does not grow with the blur. Their histogram stays the whole image's during the drag, its peaks where
  the final preview puts them, and a change made without dragging shows a quick preview first and the full one a
  moment later. The histogram behind the curve in Curves Transformation keeps the last full preview's counts until
  you let the point go.
- **Windows that drag on the part shown, at full scale.** Median Filter, Bilateral Filter, Cosmetic Correction,
  Morphological Transformation, Tighten Edges, Star Reduction, Starmask Recombination and Profiled Starmask
  Recombination depend on the image's own pixel spacing. While you drag, and while no preview area is drawn, they
  work on the part of the image the pane shows, at the image's own scale and at any zoom: exactly the pixels
  **Apply** writes there. While the pane shows the whole image they work on all of it. Their histogram keeps the last
  full preview's counts until you let go.
- **A new drag stops the last one's full preview.** Dragging again while the full preview of the last drag is still
  computed stops that preview, so the new drag shows at once.
- **The stretch in Autostretch.** In the windows above, and in every window that offers **Display Mode**, a preview
  in Autostretch is stretched as the target is, measured once, so the stretch does not move while you adjust. Histogram Transformation and Generalized Hyperbolic
  Transformation preview the whole image at every step, in one pass over an 8-bit or 16-bit image, and the other
  stretches go on stretching each preview in Autostretch by its own statistics. Every other window previews the whole
  image at every step.
- **Values that change nothing run nothing.** Brightness and contrast, exposure, HSL or LCh at zero, a gamma of 1 or
  a straight curve: the preview shows the target itself. A choice that only picks among images one preview already
  made, as **Preview display** in Auto Gradient Removal, runs nothing either, until a value, the target or its mask
  changes.
- **Previews that are dropped.** A preview whose target or mask changed while it ran, or whose window turned
  **Preview** off, is dropped rather than shown. A failed preview says why in the message strip, never in a box.
- **Apply and Reset.** **Apply** pressed while a preview runs stops that preview and applies. After a finished Apply,
  and after **Reset**, one preview runs with the values left in the window.
- **Turning Preview on and off.** Turned on, the pane shows the target itself, without a copy, until the first
  preview lands. Turned off, the preview's work stops, the pane closes, and its width and the window's size are kept
  for when it returns.
- **Other readers of the image.** A preview is shown as soon as it lands, whatever else reads the image the pane
  shows. The Statistics window and the lens go on reading the image they were reading.
- **What costs time.** A preview area shortens the preview, never the Apply. A mask, a color component of
  **Channel**, a snapshot and a sequence each add work and memory.

## What a run leaves

| Run | The target | A new view | The history | The window's values |
| --- | --- | --- | --- | --- |
| A preview | Unchanged | None | No step | Kept |
| **Take snapshot** | Unchanged | A new image | The new image starts with the process's step | Kept |
| **Apply**, finished in place | Replaced | None | One step on the target | Back to their defaults |
| **Apply**, finished to a new image | Unchanged | A new image or sequence | The new view starts with the process's step | Back to their defaults |
| **Apply** stopped by **Cancel** | Unchanged, the [temporary sequence](../reference/glossary.md#temporary-sequence) removed | None | No step | Kept |
| **Apply** that failed | Unchanged | None | No step | Kept, and the error shown |
| **Reset** | Unchanged | None | No step | Back to their defaults |
| **Close** | Unchanged | None | No step | Discarded, except in the stacking windows |

A run reads the target, the window's values, the channel, the mask with its inversion and, for a preview or a
snapshot, the preview area. What it writes is the process page's to say: the target in place, a new image, a new
sequence, files or a report, or nothing for a window that only measures. The channel selector keeps the target's
metadata, and each process states what becomes of the rest: a geometry change, a mosaic or a sequence's container
have rules of their own.

## What a window keeps between sessions

AfterNight keeps one record for each process. When a window closes, the record takes the window's values, which only
the stacking windows write, and, while **Remember window size and position** is on, the window's size and whether it
was maximized. The option is on the **Interface** page of the settings and is on unless you change it. The next
window of that process opens at the kept size, cut to the screen it opens on, and takes the kept values even when the
option is off. The window's position is not kept. Nothing else is: the target, the preview, the preview area, the
mask, a run and its progress start afresh.

## Troubleshooting

The process is disabled in the menu
:   **Cause.** No image is active, the process needs a color image and a grayscale one is active, or a process an
    extension adds cannot open, for the reason its tooltip gives.
:   **Fix.** Make an image of the right kind active, or read the tooltip, and see
    [When a window opens](#when-a-window-opens).

The window's title names another image than you meant
:   **Cause.** The window works on the image it opened on, or the one chosen in **Target Image**, whatever view is
    active later.
:   **Fix.** Choose the image in **Target Image**, or close the window and open it again from the image you mean.

**Target Image** does not list an open image
:   **Cause.** The window cannot work on that image: its channels, its size or a sequence where the process needs a
    still image.
:   **Fix.** Check what the process page asks of the image.

There is no **Channel** box
:   **Cause.** The process works on the whole image, or has channel controls of its own.
:   **Fix.** Use the process's own controls, or extract the channel or component first.

The preview pane does not appear
:   **Cause.** **Preview** is off, another window's preview of the image turned it off, the window has no real-time
    preview, or the preview could not be made.
:   **Fix.** Tick **Preview**, and read the message strip and the Session Console when the pane still stays away.

**Apply** stays disabled
:   **Cause.** The values change nothing yet, or the process page names another condition.
:   **Fix.** Change a value, and check the page of the process.

The preview lags behind a drag
:   **Cause.** The process is slower than the rate asks, or the rate is low.
:   **Fix.** Pause, draw a preview area, or raise **Updates per second while dragging**. One more preview follows
    when you let go.

**Cancel** is disabled but the run goes on for a moment
:   **Cause.** The run stops at its next stop point, which can lie inside the work on one frame.
:   **Fix.** Wait. Do not end the application to stop a run.

The window opens at an unexpected size
:   **Cause.** **Remember window size and position** is off, the screen changed, or the window has a fixed size and
    cannot be maximized.
:   **Fix.** Turn the option on and resize the window once; a kept size is cut to the screen.

The values are not there when the window opens again
:   **Cause.** Only the stacking windows keep their values.
:   **Fix.** Note the values you need, or apply them before you close the window.

The history has no step after a preview, a reset or a cancelled run
:   **Cause.** None of them changes the image.
:   **Fix.** Nothing to fix: only a finished Apply, a snapshot or a new image adds a step.
