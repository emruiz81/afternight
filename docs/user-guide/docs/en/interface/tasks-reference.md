---
kind: reference
summary: >-
  What a reader looks up about tasks: each message a task shows or logs, the tasks that differ, the column, leaving
  and floating a task, how the preview works while you drag, and what each run leaves behind.
---
# Tasks Reference

This page holds what [Tasks](tasks.md) leaves out to stay readable beside a [task](../reference/glossary.md#task): the messages a task shows and writes
to the Session Console, the tasks that differ from the rest, the column, what happens while you are away from a task
and when it floats, how the [real-time preview](../reference/glossary.md#real-time-preview) works while you drag, what each kind of run leaves behind, and what a
task keeps between sessions. Each section can be read on its own, and the lists of tasks use the titles of the
process pages.

## Messages

### When a task opens

A process that cannot open on the [active image](../reference/glossary.md#active-image) says why in a message box when
you choose it. A process an extension adds is disabled in the menu instead, and the message is its tooltip. A process
that needs a color image is disabled in the menu while a grayscale image is active.

"No selected image to process"
:   No image or [sequence](../reference/glossary.md#sequence) view is active. Click the view you mean, then choose the
    process again.

"This process requires an RGB image.", "This process requires a mono image." or "This process does not support the selected image channel count."
:   The process works on images of other channels: NBRGB Combination, for example, needs an RGB image. Choose
    another image, or convert this one with **Convert to RGB** or **Convert to Grayscale**.

"This process supports still images only, not sequences."
:   Auto Gradient Removal, NBRGB Combination and NB Color Mapper refuse a sequence. Extract a frame and work on it.

"An existing process of this type is already running for the target image"
:   A task of the same process is already open on the image. Choose it in the [rail](../reference/glossary.md#rail), or close it.

"An existing process running for the target image requires exclusive access, please close that process before executing other processes."
:   A task that needs the image to itself is open on it ([Tasks that differ](#tasks-that-differ)). Apply or close
    that task first.

"This process requires exclusive access to the target image, please close all the currently open processes using this image before executing the process."
:   The process you chose needs the image to itself, and another task is open on it. Close the other tasks on the
    image first.

`Could NOT create the preview for this process: there is no image to preview`
:   The target held no image when the preview was asked for. Wait until the image has loaded, then tick **Preview**
    again.

### While a task is open

`The target image '<name>' has been closed. The process will be closed as well.`
:   The target's view was closed, and the task closes with it. Nothing was applied.

`The target image '<name>' has changed and isn't supported by this process any more. The process will be closed.`
:   An edit made the target an image the task cannot work on, such as a grayscale image in a task that needs RGB.
    Undo the edit in the image's [history](../reference/glossary.md#history), or open the task on another image.

"The preview failed", with the reason
:   The process refused the preview, and the message strip says why, once for each different reason. Two reasons
    come from the [mask](../reference/glossary.md#mask) on the target's view: "the mask has another size than the
    image" and "the mask has neither one channel nor one for every channel of the result". Change the value the reason
    names, or give the mask the image's size and one channel or three.

"Another process is executing for this image, please cancel that job first before running this process"
:   Another task is applying its process to the same image. It reads "sequence" in place of "image" for a sequence.
    Wait for that run, or cancel it, then apply again.

"Error executing the process: An error occurred when running the process:" and the reason
:   The run failed and wrote nothing. The message strip says it; a task opens no message box for it. Correct what the
    reason names and apply again. A reason that names the memory budget asks for less memory: close other images, or
    raise **Memory Limit** on the **Processing** page of the settings, which takes effect at the next start.

"applying", then "The task keeps running if you leave this page."
:   The run's row in the column while a run works. It is no error. A stacking tool says its own step instead, such as
    "calibrating", "registering" or "stacking".

"Gamma Correction applied", with the time the run took and **Undo**
:   The message at the foot of the centre when a run of a task in the main window has finished. **Undo** undoes it on
    the target. The task header, floating or not, says "applied 2.8 s ago" for a minute.

"stars_only opened in Workspace 1" or "3 images opened in Workspace 1", with **Show**
:   A run made new images, which opened in a [workspace](../reference/glossary.md#workspace) while you stayed on the task. **Show** shows the first of them.

"Image Integration finished", "failed" or "stopped", with **Show**
:   A task that fills its page whole ended its run while another place was shown. **Show** shows the task.

### In the Session Console

The [Session Console](log-viewer.md) keeps these lines of a task's work.

| Line | When |
| --- | --- |
| `Executing process '<process>' on image '<image>'...` | A run starts on an image; on a sequence the line says `sequence`. |
| `Process '<process>' executed successfully on image '<image>'. Execution time was 1.25 seconds` | An Apply in place has finished and is shown. |
| `Process '<process>' executed successfully, created new processed image '<name>'` | A run that writes a new image has finished. |
| `Process '<process>' executed successfully on '<image>'` | A run of a task that runs its own way ([Tasks that differ](#tasks-that-differ)) has finished; no time is given. |
| `<step>: sequence '<name>'` | The same tasks' run over a sequence has finished, with the step the history records. |
| `<step> into new image '<name>'` | The same tasks have opened a new image; `into new sequence` for a sequence. |
| `New image snapshot '<name>' taken from real time preview of process '<process>'` | **Take snapshot** opened its image. |
| `Process '<process>' cancelled by the user` | A run stopped by **Cancel**. |
| `Process '<process>' produced a result that was not published: its image changed or closed under it` | The target changed or closed while the run worked, and the result was dropped. Apply again on the image as it is now. |
| `Preview of process '<process>' failed: <reason>` | The same failure as the message strip's "The preview failed". |
| `'<process>': the page cannot show its target: <reason>` | A task whose canvas shows its target could not show it there. |
| `'<process>': work on a worker threw and its run ends with a failure: <reason>` | The process failed in an unexpected way. The task reports the failure. Try again, and report it with the log if it repeats. |
| `'<process>': work on a worker threw and its run was dropped: <reason>` | The same, for work whose result nothing waited for. Nothing is shown or recorded. |

## Tasks that differ

| What differs | Tasks |
| --- | --- |
| Need the image to themselves: they do not open beside another task on the image, and no other task opens on the image while they are open | Crop, Rotate, Background Extraction, Auto Gradient Removal, Background Neutralization, Color Calibration, Photometric Color Calibration, Spectrophotometric Color Calibration, Star Profiler v1 and Star Profiler v2 |
| Choose their images themselves and open without a target: the task header names the process alone, and the canvas names the images still to choose | Channel Composition, Merge [CFA](../reference/glossary.md#cfa), Narrowband Palette Composer, Starmask Recombination and Profiled Starmask Recombination |
| Fill their page whole, with no canvas and no column: their own menus are under the task header's menu button, and a run shows at the page's head with **Cancel** | Image Calibration, Image Registration, Image Stacking, Image Integration, Image Integration Lite and Image List Analyzer |
| Show their run in their own page, with its own **Cancel** | Image Integration and Image Integration Lite |
| Name the job they hold in the task header in place of the process and its image | Image Integration |
| Keep their values for the next task | Image Calibration, Image Registration, Image Stacking, Image Integration, Image Integration Lite and Image List Analyzer |
| Show a view of their target in the canvas, with no preview: they have no toggles and no compact [readout](../reference/glossary.md#readout), and those that mark a rectangle or stars mark them on that view | Background Neutralization, CFA Integer Resample, Color Calibration, Crop, [Debayer](../processes/calibration/debayer.md), DeepSNR, Extract Sequence, Integer Resample, Linear Match, Photometric Color Calibration, Pixel Math, Resize, Rotate, SCNR, Spectrophotometric Color Calibration, Split CFA, Starnet++, Starnet++ Star Mask, Star Profiler v1 and Star Profiler v2 |
| Run their own way, without a real-time preview, and write their run to the Session Console without its time | Background Neutralization, CFA Integer Resample, Color Calibration, Crop, [Debayer](../processes/calibration/debayer.md), DeepSNR, Extract Sequence, Integer Resample, Linear Match, Pixel Math, Resize, Rotate, SCNR, Split CFA, Starnet++ and Starnet++ Star Mask |
| Offer **Display Mode** and [**Autostretch Settings**](../reference/glossary.md#autostretch) in the canvas's toolbar | Auto Gradient Removal, Background Extraction, Banding Reduction, Bilateral Filter, Binarize, Channel Composition, Cosmetic Correction, Dark Structure Enhance, Exposure Correction, Gaussian Blur, GRAYCstoration, Image Blend, Image Compare, LRGB Combination, Mask Drawing, Median Filter, Merge CFA, Morphological Transformation, Multiscale Linear Transform, Narrowband Palette Composer, NB Color Mapper, NBRGB Combination, NL-Bayes, Noise Generator, Patch PCA Denoise, Profiled Starmask Recombination, Range Selection, Star Mask, Starmask Recombination, Star Reduction, Tighten Edges, Total Variation Denoise, Unsharp Mask and Wavelet Denoise |
| Have no [**Preview area**](../reference/glossary.md#preview-area) | Auto Gradient Removal, Background Extraction and Narrowband Palette Composer |
| Show the selected file in a still preview, never processed, with **Display Mode**, **Autostretch Settings** and **Show statistics** in its toolbar | Image Calibration, Image Registration, Image Stacking, Image List Analyzer and the source tabs of Image Integration |
| Show a grid of palettes in the canvas, and have no **Preview** | Narrowband Palette Composer |
| Have no **Preview** toggle: the preview is always on | Image Compare, LRGB Combination, NB Color Mapper and NBRGB Combination |
| Keep **Preview** among their own controls, not in the toggles row | Histogram Transformation |
| Open with **Preview** off | Profiled Starmask Recombination |
| Write a new image or sequence in place of the target | Channel Composition, [Debayer](../processes/calibration/debayer.md), Mask Drawing, Merge CFA, Narrowband Palette Composer, NB Color Mapper, NBRGB Combination, Profiled Starmask Recombination, Range Selection, Star Mask, Split CFA, Starmask Recombination, Starnet++, Starnet++ Star Mask, and Pixel Math and Star Reduction when their settings ask for one |
| Close their task when the new image is made | Channel Composition, [Debayer](../processes/calibration/debayer.md), Mask Drawing, Merge CFA, Profiled Starmask Recombination, Range Selection, Split CFA, Star Mask and Starmask Recombination |
| Do not blend through the target's mask | Mask Drawing, Narrowband Palette Composer, Profiled Starmask Recombination, Range Selection, Star Mask and Starmask Recombination |
| Name the two images of the preview in their own words | Image Blend (PREVIEW and BASE IMAGE), Image Compare (IMAGE 1 and IMAGE 2, and **Image 1** and **Image 2** on its **Processed** toggle) and Star Reduction (PREVIEW and TARGET IMAGE) |
| Show "extension" after the process's name in the column: the process's code runs in a program of its own | The processes an extension adds |

The tasks without a real-time preview, such as Pixel Math, Crop, Resize,
Rotate, the color calibration tasks and the tasks of external tools, read the task, the images it names and, for those
that use one, the view's mask, when you press **Apply** or **OK**, so a change made during the run does not reach it.
Which controls they disable while they work, and what they keep, is their pages' to say. The stacking tools and the
tasks of extensions have controls and runs of their own too, and their pages say which parts of [Tasks](tasks.md)
apply.

### Tasks that open with a preview area

The area starts in the middle of the image, at the size below or the image's own when it is smaller, with the label
shown in its frame.

| Task | Size, in pixels of the image | Label |
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
task's area is labelled "Preview area".

### The histogram toolbar of each task

| Tasks | Buttons |
| --- | --- |
| Adjust Brightness/Contrast, Adjust [HSL](../reference/glossary.md#hsl), Adjust LCh, [ArcSinh Stretch](../processes/stretching/arcsinh-stretch.md), CLAHE, Color Saturation, Colorize, Exposure Correction, Gamma Correction, High Pass Filter, Image Blend, Local Contrast Enhancement, Local Histogram Equalization, LRGB Combination, NB Color Mapper and NBRGB Combination | **Zoom in**, **Zoom out**, **Zoom reset**, **Show grid**, **Show clipping info**, **Absolute scales**, **Draw histogram areas**, **Show histogram**, **Plot resolution** |
| Image Compare and [Statistical Stretch](../processes/stretching/statistical-stretch.md) | The same without **Show histogram** |
| Histogram Transformation | **Zoom in**, **Zoom out**, **Zoom reset**, **Show grid**, **Show transfer curve**, **Absolute scales**, **Draw histogram areas**, **Plot resolution** |
| Generalized Hyperbolic Transformation | **Zoom in**, **Zoom out**, **Zoom reset**, **Show grid**, **Transfer curve**, **Show clipping info**, **Absolute scales**, on when the task opens, **Draw histogram areas**, **Plot resolution** |
| Curves Transformation | **Zoom in**, **Zoom out**, **Zoom reset**, **Show grid**, **Show guide**, **Draw histogram areas**, **Show histogram** |

## The column

- **Its width** is the least the process's controls take: 360 pixels for most, 520 for those with a wide row such as
  a histogram's toolbar, and 320 or 440 in a main window under 1,200 pixels wide. The canvas takes the rest.
- **The header** names the menu the process sits in, in capitals, over the process's name, whose tooltip says what
  the process does. A process an extension adds shows "extension" after its name, with the tooltip "Runs in its own
  process".
- **The compact readout** is at the column's head in a task with a preview. A task whose canvas shows its target, and
  a task that fills its page whole, have none. Its chevron opens the target's full [readout](readout.md) over the
  column, and closes it again.
- **The body** scrolls up and down, never sideways, and the footer stays in view. **Target Image** and **Channel**
  stand side by side at its head. A list narrower than its longest choice shortens the choice it shows, and its list
  shows each one whole.
- **Sections.** When the process's controls are in several groups, each group is a section that opens and closes,
  titled as the group. Opening one that would push the body past the column's height closes the one used longest
  ago. Which sections are closed is kept for the next task of that process.
- **The message strip** shows information, a warning or an error, sometimes with how far a step has come, and
  sometimes with buttons at its end, such as Restart. During a run the run's row takes its place.

## Leaving a task and coming back

- **A task's page is kept** while another place or task is shown, and comes back as you left it. Showing a task makes
  its target the active image.
- **A run goes on** while you are away. The task's item in the rail turns while it works, with how far it has come,
  and a dot on it says how a run ended - a check or a cross - until you show the task again. See
  [Rail and Places](rail-and-places.md).
- **The target while a run writes it.** Its view in the workspace is covered by a notice, "Gamma Correction is
  applying", with how far the run has come and **Show**, which shows the task. The image cannot be edited until the
  run ends.
- **The target's tab** carries the process's glyph while a task is open on it: "Gamma Correction is open on this image.
  Click to go to the task". During a run it reads "Gamma Correction is applying to this image: it is locked until the
  run ends. Click to go to the task".
- **The target's history strip** shows the task's step after the image's steps: "pending", with the tooltip "The open
  task: not in the history until Apply", and "writing" during a run.
- **When a run ends,** the task header says how; a run that succeeded also shows a message with **Undo**, at the foot
  of the centre, or, in a floating task, over its canvas in its own window; and a new image that a run makes opens
  beside the image it came from, or in the current workspace, without leaving the task. The end of a run is said to a
  screen reader: the process applied, failed with why, or stopped.
- **On a task's page,** the zoom of the canvas follows **Zoom In**, **Zoom Out**, **Zoom 100%** and **Fit To Window**,
  and **Undo**, **Redo**, **Save as...**, **Image History**, the **Image** menu and another process act on the
  target. **Close**, **Close All Others** and **Maximize View** are disabled there: an image is closed from its
  workspace. The breadcrumb names the process and the target, and the status line describes the canvas and its zoom.
- **Process > Apply**, ++ctrl+enter++, applies the task shown, wherever the focus is, and **Window > Close Task**
  closes it. A closed task's page leaves the centre, which shows the place you were on before it: a workspace while
  one has images, or Home.

## Floating and dragging

- **Float** with **Float as window** in the task header, or **Float as Window** in the task's rail menu, the
  **Window** menu or the [command palette](command-palette.md). The page leaves the centre, which shows the place you
  were on before, and opens in a window of its own over it. The task's item in the rail is drawn faint, and a click on
  it brings the window forward.
- **The window's size** is the one the last floating window of that process had, kept when it was attached or closed,
  or three quarters of the centre the first time. It is held within the screen, and is never smaller than 720 × 480.
  **Remember window size and position** in the settings does not change this.
- **The window** is titled with the process and its image, as "Gamma Correction - M42", and holds the whole page,
  with **Attach** in place of **Float as window**. It is a window of its own: the main window's shortcuts do not act
  in it, while ++esc++, ++ctrl+enter++, the process's own shortcuts and ++f1++ do. Closing it by its title bar closes
  the task, except during a run, when the window stays. A [project](../reference/glossary.md#project) does not keep it in its saved layout, and closing
  the main window closes it.
- **Attach** with **Attach** in the window's header, the task's rail menu or the **Window** menu. The page comes back
  to the centre as the task shown, with the focus in it.
- **Pull a task out of the rail.** Press its item and move: a ghost of it follows the pointer. Released inside the
  main window, nothing changes, and ++esc++ cancels. Past the window's edge the task floats under the pointer and
  follows it until you release the button. Under Wayland the ghost goes on, and the task floats where you release the
  button outside the window.
- **Drag a window back.** Drag a floating task's window by its title bar over the main window's rail or centre: the
  zone is highlighted and the rail marks where the task goes. Released there, the task attaches. Released over the top
  bar, the readout, the status line, the Session Console or outside, it stays a window.
- **Floating or attached, it is one task:** its run, its values, its preview and its lock on the target are
  untouched, and a task floats or attaches during a run too.

### An extension's own window

An extension whose process shows a window of its own program is a task in the rail too. A click on its item asks the
program to bring its window forward; its menu has **Show** and **Close**, which ends its session, and it neither floats
nor attaches. Its run holds no lock on the image. When its program stops working, the item stays, marked failed, until
you close it. See [Using Extensions](../extensions/using-extensions.md).

## The preview while you drag

- **One preview at a time.** Previews and runs work away from the task, which stays responsive. While one runs, or
  while the rate below holds the next one back, further changes collapse into one more preview. It starts with the
  values the task holds when the running one ends, and a newer request replaces one still waiting.
- **The rate.** **Edit > Settings... > Processing > Updates per second while dragging** sets the most previews a task
  starts in a second while you drag one of its sliders: 30 unless you change it, from 5 to 60. It is a ceiling, not a
  promise that a process reaches it. A change applies to the next previews a task starts.
- **Dragging and resting.** While you drag a slider, or a point of the curve in Curves Transformation, the previews
  run on the threads kept for previews, which leaves the rest of the computer to other work. Letting go, or holding
  the slider still for a moment, runs one more preview with every thread a preview may use: the preview the task
  rests on. A change made without dragging, a value typed or a box ticked, runs one such preview at once.
- **Tasks that drag on what the canvas shows.** Adjust Brightness/Contrast, Gamma Correction, Exposure Correction,
  Color Saturation, Adjust HSL, Adjust LCh, Curves Transformation, Gaussian Blur, Unsharp Mask, High Pass Filter and
  Range Selection work, while you drag, on the image reduced to the canvas's size, or on the part the canvas shows at
  100 % and closer. A drag then keeps up on a large frame, and the preview it rests on is computed on the whole image,
  as **Apply** computes it. Over the reduced image, Unsharp Mask and High Pass Filter come close to a reduction of
  their final result rather than matching it, because their clipping does not commute with averaging. Beyond a
  **Sigma** or a radius of 32 in a dragged preview, Gaussian Blur and High Pass Filter blur with three box passes,
  whose cost does not grow with the blur. Their histogram stays the whole image's during the drag, its peaks where
  the final preview puts them, and a change made without dragging shows a quick preview first and the full one a
  moment later. The histogram behind the curve in Curves Transformation keeps the last full preview's counts until
  you let the point go.
- **Tasks that drag on the part shown, at full scale.** Median Filter, Bilateral Filter, Cosmetic Correction,
  Morphological Transformation, Tighten Edges, Star Reduction, Starmask Recombination and Profiled Starmask
  Recombination depend on the image's own pixel spacing. While you drag, and while no preview area is drawn, they
  work on the part of the image the canvas shows, at the image's own scale and at any zoom: exactly the pixels
  **Apply** writes there. While the canvas shows the whole image they work on all of it. Their histogram keeps the
  last full preview's counts until you let go.
- **A new drag stops the last one's full preview.** Dragging again while the full preview of the last drag is still
  computed stops that preview, so the new drag shows at once.
- **The stretch in Autostretch.** In the tasks above, and in every task that offers **Display Mode**, a preview in
  Autostretch is stretched as the target is, measured once, so the [stretch](../reference/glossary.md#stretch) does not
  move while you adjust. Histogram Transformation and Generalized Hyperbolic Transformation preview the whole image at
  every step, in one pass over an 8-bit or 16-bit image, and the other stretches go on stretching each preview in
  Autostretch by its own statistics. Every other task previews the whole image at every step.
- **Values that change nothing run nothing.** Brightness and contrast, exposure, HSL or LCh at zero, a gamma of 1 or
  a straight curve: the preview shows the target itself. A choice that only picks among images one preview already
  made, as **Preview display** in Auto Gradient Removal, runs nothing either, until a value, the target or its mask
  changes.
- **Previews that are dropped.** A preview whose target or mask changed while it ran, or whose task turned
  **Preview** off, is dropped rather than shown. A failed preview says why in the message strip, never in a box.
- **Apply and Reset.** **Apply** pressed while a preview runs stops that preview and applies. After a finished Apply,
  and after **Reset**, one preview runs with the values left in the task.
- **Turning Preview on and off.** Turned on, the canvas shows the target itself, without a copy, until the first
  preview lands. Turned off, the preview's work stops and the canvas says "The preview is off".
- **Other readers of the image.** A preview is shown as soon as it lands, whatever else reads the image the canvas
  shows. The Statistics window and the lens go on reading the image they were reading.
- **What costs time.** A preview area shortens the preview, never the Apply. A mask, a color component of
  **Channel**, a snapshot and a sequence each add work and memory.

## What a run leaves

| Run | The target | A new view | The history | The task's values |
| --- | --- | --- | --- | --- |
| A preview | Unchanged | None | No step | Kept |
| **Take snapshot** | Unchanged | A new image | The new image starts with the process's step | Kept |
| **Apply**, finished in place | Replaced | None | One step on the target | Back to their defaults |
| **Apply**, finished to a new image | Unchanged | A new image or sequence | The new view starts with the process's step | Back to their defaults |
| **Apply** stopped by **Cancel** | Unchanged, the [temporary sequence](../reference/glossary.md#temporary-sequence) removed | None | No step | Kept |
| **Apply** that failed | Unchanged | None | No step | Kept, and the error shown |
| **Reset** | Unchanged | None | No step | Back to their defaults |
| **Close** | Unchanged | None | No step | Discarded, except in the stacking tools |

A run reads the target, the task's values, the channel, the mask with its inversion and, for a preview or a
snapshot, the preview area. What it writes is the process page's to say: the target in place, a new image, a new
sequence, files or a report, or nothing for a task that only measures. The channel selector keeps the target's
metadata, and each process states what becomes of the rest: a geometry change, a mosaic or a sequence's container
have rules of their own. A run over a sequence writes its frames again to a temporary file whose name starts with
`SEQ_SWAP`, in the session's temporary folder, which needs the disk space of a second copy until the run ends; see
[Sequences](sequences.md).

## What a task keeps between sessions

AfterNight keeps one record for each process. When a task closes, the record takes the task's values, which only the
stacking tools read again, and when a floating task attaches or closes, the size of its window. The sections of the
column that were closed are kept apart, for the next task of that process. A task's page has no size of its own: it
fills the centre. Nothing else is kept: the target, the preview, the preview area, the mask and a run start afresh.

The stacking tools keep different things:

| Task | What the next task takes |
| --- | --- |
| Image Calibration, Image Registration, Image Stacking | Its values and its list of files, which it reads again |
| Image Integration | Its project, its frames and settings, saved in Image Integration's own session database every 5 seconds while they change, and when the task closes |
| Image Integration Lite | Its Output folder only; every other control opens at its default |
| Image List Analyzer | Its playback rate only |

Each menu choice of a stacking tool opens another task, and the last one closed writes the record.

## Troubleshooting

The process is disabled in the menu
:   **Cause.** No image is active, the process needs a color image and a grayscale one is active, or a process an
    extension adds cannot open, for the reason its tooltip gives.
:   **Fix.** Make an image of the right kind active, or read the tooltip, and see
    [When a task opens](#when-a-task-opens).

The task header names another image than you meant
:   **Cause.** The task works on the image it opened on, or the one chosen in **Target Image**, whatever view is
    active later.
:   **Fix.** Choose the image in **Target Image**, or close the task and open it again from the image you mean.

**Target Image** does not list an open image
:   **Cause.** The task cannot work on that image: its channels, its size or a sequence where the process needs a
    still image.
:   **Fix.** Check what the process page asks of the image.

There is no **Channel** box
:   **Cause.** The process works on the whole image, or has channel controls of its own.
:   **Fix.** Use the process's own controls, or extract the channel or component first.

The canvas shows no preview
:   **Cause.** **Preview** is off, another task's preview of the image turned it off, the task has no real-time
    preview and shows its target, or the preview could not be made.
:   **Fix.** Tick **Preview**, and read the message strip and the Session Console when the canvas still says "The
    preview is off".

**Apply** stays disabled
:   **Cause.** The values change nothing yet, or the process page names another condition.
:   **Fix.** Change a value, and check the page of the process.

The preview lags behind a drag
:   **Cause.** The process is slower than the rate asks, or the rate is low.
:   **Fix.** Pause, draw a preview area, or raise **Updates per second while dragging**. One more preview follows
    when you let go.

The run goes on for a moment after **Cancel**
:   **Cause.** The run stops at its next stop point, which can lie inside the work on one frame.
:   **Fix.** Wait. Do not end the application to stop a run.

The task's page is gone from the centre
:   **Cause.** It floats in a window of its own, perhaps minimized or behind another window, or an extension put its
    task away.
:   **Fix.** Choose the task's item in the rail: it shows the page, or brings its window forward.

A floating task's window does not attach where it was dropped
:   **Cause.** It was dropped over the top bar, the readout, the status line or the Session Console, where a task
    stays a window.
:   **Fix.** Drop it over the rail or the centre, or choose **Attach** in its header.

A floating task's window opens at an unexpected size
:   **Cause.** It takes the size of the last floating window of that process, held within the screen and never under
    720 × 480.
:   **Fix.** Resize it once; the next one opens at that size.

The values are not there when the task opens again
:   **Cause.** Only the stacking tools keep their values.
:   **Fix.** Note the values you need, or apply them before you close the task.

The history has no step after a preview, a reset or a cancelled run
:   **Cause.** None of them changes the image.
:   **Fix.** Nothing to fix: only a finished Apply, a snapshot or a new image adds a step.
