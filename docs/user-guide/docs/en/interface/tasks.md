---
kind: interface
summary: >-
  What every task shares: its item in the rail, its header, the canvas with the preview, the column with the
  process's controls, Reset, Close and Apply, its run, and a task floated in a window of its own.
controls_ignore:
  - label: Expand the full readout over the process
    reason: >-
      The chevron of the compact readout opens nothing; the full readout of an image is the readout column beside a
      workspace.
---
# Tasks

A command of the **Process** menu that has settings opens a [task](../reference/glossary.md#task): a page in the centre of the main window, where the
process works on the [active image](../reference/glossary.md#active-image), with an item in the [rail](../reference/glossary.md#rail) while it is
open. A command without settings runs at once: see
[Non-Interactive Processes](../processes/utility/non-interactive-processes.md). This page describes what every task
shares; each process page describes its own controls, and [Tasks Reference](tasks-reference.md) holds the rest.

```figure
image: ../assets/images/interface/tasks.webp
alt: >-
  The main window showing the Histogram Transformation task. At the left, the rail with Home, the workspace and the
  Integration place, and under them the task's item. Under the top bar, the task header with the image's thumbnail,
  Histogram Transformation and the image's name, and at its right the Float as window and Close buttons. Most of the
  page is the canvas: the stretched preview of the Carina Nebula among its stars, the word PREVIEW in its upper left
  corner, and near its foot a floating toolbar of zoom buttons, a channel box, a camera button and More. Under the
  canvas, the history strip with Initial state and a dashed Histogram Transformation chip marked pending. At the right,
  the column: a small histogram with clipping shares and five statistics, the word ADJUSTMENTS over the process's name
  and a close button, Preview area and Processed, Target Image and the process's two histograms, and Reset, Close and
  Apply along its foot. The status line runs along the bottom of the window.
caption: >-
  Histogram Transformation as a task, with Shadows and Midtones at 0.02 on a linear master: the canvas with the preview
  at the left, the column with the process's controls at the right.
pins:
  - {n: 1, x: 1.0, y: 25.5}
  - {n: 2, x: 3.7, y: 10.4}
  - {n: 3, x: 4.4, y: 17.9}
  - {n: 4, x: 18.8, y: 89.6}
  - {n: 5, x: 3.5, y: 95.1}
  - {n: 6, x: 63.8, y: 14.0}
  - {n: 7, x: 63.8, y: 30.5}
  - {n: 8, x: 64.0, y: 35.8}
  - {n: 9, x: 63.8, y: 40.3}
  - {n: 10, x: 64.9, y: 94.4}
owed: true
```

## What you see

1. **The task in the rail.** The target's thumbnail with the process's glyph. A ring turns around it during a run,
   and a dot says how a run ended while you were elsewhere.
2. **The task header.** The process "on" its image, and where the task stands: "preview up to date", "applying - 42 %",
   "applied 2.8 s ago" or "failed". **Float as window** and **Close** stand at its right.
3. **The canvas.** The [real-time preview](../reference/glossary.md#real-time-preview): the result as **Apply** would
   write it, with the [mask](../reference/glossary.md#mask) of the target's view blended in. A task with no preview
   shows its target here.
4. **The floating toolbar.** The canvas's zoom and channel, a snapshot of the preview, and **More**.
5. **The history strip.** The target's [history](../reference/glossary.md#history), with the task's step marked
   "pending" until you apply.
6. **The compact readout.** The preview's histogram, clipping and statistics, each with its change against the
   original.
7. **The column's header.** The menu the process sits in, over its name, and **Close**.
8. **The toggles.** **Preview**, **Preview area** and **Processed**.
9. **The body.** **Target Image** and **Channel**, then the process's controls. A message strip over it says what the
   task has to report.
10. **The footer.** **Reset**, **Close** and **Apply**, always in view; **Cancel** replaces **Apply** during a run.

## What you can do

### Open a task

Make the image or [sequence](../reference/glossary.md#sequence) the active image, then choose the process from the
**Process** menu or find it in the [command palette](command-palette.md) with ++ctrl+k++. The task opens as a page and
the centre shows it. A task does not open twice on one image, and a process that cannot open says why in a message
([When a task opens](tasks-reference.md#when-a-task-opens)). Some tasks choose their images themselves, and the
stacking tools fill their page whole ([Tasks that differ](tasks-reference.md#tasks-that-differ)).

### Preview and compare

1. Change a value. While you drag a slider, the preview follows at the rate set in the settings; when you let go, one
   more preview shows what **Apply** would write.
2. Hold ++space++, or the right mouse button, over the canvas to see the original. **Processed** switches and stays.
3. To check one region quickly, tick **Preview area**, then move and resize its frame on the canvas.
4. Choose **Take snapshot** to keep the preview as a new image.

### Apply to an image

1. Check **Target Image**, and the mask on the target's view: white takes the result, black keeps the original.
2. Choose **Apply**, or press ++ctrl+enter++. A row in the column says how far the run has come, and the target is
   locked until it ends.
3. The view shows the result, its history gets one step named after the process, and the controls return to their
   defaults. A message at the foot of the centre offers **Undo**.

### Apply to a sequence

1. Make the sequence the target. Every frame is processed; to work on some of them, make a sequence of those frames
   first with [Extract Sequence](../processes/utility/extract-sequence.md).
2. Choose **Apply**. The run's row shows **Overall** for the frames and **Processing** for the frame on its way.
3. The sequence is replaced when every frame is done. **Cancel** stops the run and keeps the original.

### Leave a task and come back

Choose another place or task in the rail. The task stays as you left it, a run goes on, and its item brings it back.
While a run writes an image, the image's view names the task, with **Show** to return to it. A new image that a run
makes opens in a [workspace](../reference/glossary.md#workspace) and leaves you on the task.

### Float a task in a window

Choose **Float as window** in the task header. The page moves into a window of its own, with its run, its values and
its preview; **Attach** in its header brings it back. You can also pull the task's item out of the rail, and drag the
window back over the rail or the centre ([Floating and dragging](tasks-reference.md#floating-and-dragging)). The
window answers ++esc++, ++ctrl+enter++ and ++f1++, and closing it closes the task.

```figure
image: ../assets/images/interface/tasks-floating.webp
alt: >-
  A window of its own titled Gamma Correction and the image's name. It holds the whole task page: the header with
  Gamma Correction on the image, a check and "preview up to date", and the Attach and Close buttons at its right; the
  canvas at the left with the word PREVIEW over the stars and its floating toolbar near the foot; the history strip
  with a pending step; and at the right the column with the compact readout, Preview ticked, Preview area, Processed,
  Target Image and Channel, a histogram with its clipping shares, and Reset, Close and Apply at the foot.
caption: >-
  Gamma Correction floated in a window of its own, with Gamma at 0.50: the same page, with Attach in place of Float as
  window.
owed: true
```

### Start again or close

Choose **Reset** to return the process's controls to their defaults. Choose **Close**, or press ++esc++, to close the
task without applying. A task does not close during its run.

### Read about the process

Press ++f1++ anywhere on a task's page to open this guide at that process's page.

## Controls

### The task header

```controls
window: Task header
controls:
  - name: Task
    kind: readout
    default: ""
    effect: >-
      The target's thumbnail, the process's name, "on" and the target's full name, shortened in the middle when it
      does not fit, as in `Gamma Correction on M42`. A task with no single target names the process alone.
    note: >-
      A task that holds a job, as Image Integration does, names the job on three lines instead - what holds it, its
      name and its facts - with the disk it needs at the right and the job's own buttons, such as Undo, Redo and
      Execute.
  - name: State
    kind: readout
    default: ""
    effect: >-
      Says where the task stands: "preview up to date", with a check, while the preview shows the values; "applying -
      42 %" during a run; "applied 2.8 s ago" for a minute after a run succeeded; "failed" after a run failed.
    range: >-
      During a run a thin bar lies along the header's foot, as long as the progress, or sliding across while the run
      cannot tell how far it has come.
  - name: The process's menus
    kind: toolbutton
    default: ""
    effect: Opens the menus of a task that fills its page whole, each entry acting as it does in its menu.
    enabled: Shown only on such a page, when its process has menus, as Image Integration has.
  - name: Float as window
    kind: toolbutton
    default: ""
    effect: Moves the task's page into a window of its own. In a floating task it reads **Attach** and brings the page back to the centre.
    scope: The page only. The run, the values, the preview and the lock on the target stay as they are.
  - name: Close
    object: taskHeaderClose
    kind: toolbutton
    default: ""
    effect: Closes the task without applying, as **Close** in the footer does. Its tooltip reads "Close (Esc)".
    enabled: Disabled while a run works.
```

### The canvas

```controls
window: Canvas
controls:
  - name: Fit To Window
    kind: toolbutton
    default: ""
    effect: Fits the whole image into the canvas.
    note: ++ctrl+shift+f++ does the same with the canvas focused. The canvas fits the image when the task opens.
  - name: Zoom Out
    kind: toolbutton
    default: ""
    effect: Shows the image smaller at each press, a factor of 0.8, down to 1 percent.
    note: ++ctrl+minus++ does the same on a task's page.
  - name: Zoom
    kind: readout
    default: ""
    effect: The canvas's zoom in percent, between **Zoom Out** and **Zoom In**.
  - name: Zoom In
    kind: toolbutton
    default: ""
    effect: Shows the image a quarter larger at each press, a factor of 1.25, up to 6,400 percent.
    scope: The canvas only, apart from the histogram's zoom. The mouse gestures of an image's view work here too.
    note: ++ctrl+plus++ does the same on a task's page.
  - name: Zoom 100%
    kind: toolbutton
    default: ""
    effect: Shows one sample of the image on one pixel of the screen.
    note: ++ctrl+0++ does the same on a task's page.
  - name: View Channel
    kind: combo
    default: RGB
    options: [RGB, Red, Green, Blue, The other display channels of an image's view]
    effect: Chooses what the canvas displays. It changes nothing the process does.
    enabled: A grayscale image shows a disabled **Grayscale**.
    scope: The canvas, and the lines the histogram draws. With **Red**, **Green** or **Blue** the histogram draws that channel's line alone, and with any other choice all three.
  - name: Take snapshot
    kind: toolbutton
    default: ""
    effect: Opens a new image of the preview's result, named `<target> - <process> - SNAPSHOT`, as `M42 - Gamma Correction - SNAPSHOT`.
    range: It runs the process again over the whole image, at the image's own depth, with the values, the channel and the mask as they are. With a preview area, the new image is the area over the whole original.
    enabled: Disabled while a run works. ++ctrl+shift+k++ does the same with the canvas focused.
    scope: The target and its history are not changed. The new image's history starts with the process's step.
    persistence: Each press makes another image.
  - name: More
    kind: button
    default: ""
    effect: Opens the view's menu, as on an image's view in a workspace.
  - name: PREVIEW or ORIGINAL
    kind: readout
    default: PREVIEW
    effect: Says which image the canvas shows, in its top left corner.
    note: Image Blend, Image Compare and Star Reduction name their two images in their own words.
  - name: Waiting indicator
    kind: readout
    default: ""
    effect: Turns over the canvas while a preview has been running for more than about a third of a second.
    enabled: In most tasks.
  - name: The preview is off
    kind: readout
    default: ""
    effect: >-
      What the canvas says while it shows nothing: that **Preview** is off, or, in a task that chooses its images
      itself, "Choose the images to see the result" over the images still to choose, as "Still to choose: Star image".
```

Some tasks add the display mode to the canvas's toolbar
([Tasks that differ](tasks-reference.md#tasks-that-differ)):

```controls
window: The canvas's display mode
controls:
  - name: Display Mode
    kind: radio
    default: Linear
    options: [Linear, Autostretch]
    effect: Shows the preview as its samples are, in **Linear**, or stretched for the screen, in **Autostretch**. It changes nothing the process does.
    range: It opens in the mode of the target's view.
    scope: The canvas only. **Autostretch** uses the Autostretch settings of the target's view, and the stretch is measured on the target, so it does not move while you adjust.
  - name: Autostretch Settings
    kind: toolbutton
    default: ""
    effect: Opens the [Autostretch](../reference/glossary.md#autostretch) settings for the preview, with the values of the target's view.
    enabled: With **Display Mode** at **Autostretch**. ++ctrl+shift+a++ does the same.
    scope: The canvas's display only. The dialog has no **Apply Stretch** here.
```

### The compact readout

```controls
window: Compact readout
controls:
  - name: Readout · preview
    kind: readout
    default: ""
    effect: The readout's header, with the target's astrometry in one line at its right - "Solved", the pixel scale and the SIP terms - when the target has a plate solution.
    enabled: Shown in a task with a preview. A task whose canvas shows its target has none.
  - name: Histogram
    kind: readout
    default: ""
    effect: The preview's histogram, tagged "Preview", beside its mode, as `RGB · 8-bit`.
    scope: The preview, as **Apply** will write it.
  - name: Clip low and Clip high
    kind: readout
    default: "-"
    effect: The shares of the preview's samples at or below 0 and at or above 1.
  - name: Min, Max, Mean, Median and StdDev
    kind: readout
    default: "-"
    effect: The preview's statistics, each with its change against the original under it, as `Δ +0.0123`. A color image's are the mean of its three channels' figures.
    note: While the preview is measured again, or while you drag, the values are drawn faint.
```

### The column

```controls
window: Column header
controls:
  - name: Category and name
    kind: readout
    default: ""
    effect: The menu the process sits in, in capitals, over the process's name, whose tooltip says what the process does.
    note: A process an extension adds, whose code runs in a program of its own, shows "extension" after its name.
  - name: Close
    object: pageClose
    kind: toolbutton
    default: ""
    effect: Closes the task without applying, as **Close** in the footer does. Its tooltip reads "Close (Esc)".
    enabled: Disabled while a run works.
```

```controls
window: Toggles
controls:
  - name: Preview
    kind: check
    default: "On"
    effect: Shows the preview in the canvas. Off stops the preview's work, and the canvas says "The preview is off".
    enabled: In most tasks; [Tasks that differ](tasks-reference.md#tasks-that-differ) lists those without it. Profiled Starmask Recombination opens with it off. Disabled while a run works.
    tradeoff: Each change computes the preview again and counts its histogram again. On a large image, turn it off or draw a preview area while you adjust.
    scope: The task only. The preview holds its own copy of the result, and the target, its history and its unsaved state are not touched. With it off, the histogram counts the image as it is.
    persistence: Until the task closes. **Reset** leaves it as it is.
    note: Only one task shows a real-time preview of an image at a time. Turning a preview on, or opening a task with one, turns **Preview** off in the other tasks open on that image.
  - name: Preview area
    kind: check
    default: "Off"
    effect: Limits the preview's work to a frame you move and resize on the canvas. The area is processed as an image of its own and shown over the original.
    range: >-
      The frame starts in the middle of the image, at a size each task gives it, and stays inside the image. Its
      label names it, as "Preview area" or "Preview: Median Filter".
    enabled: Shown in the tasks whose preview offers an area; several open with it on, which the reference page lists with the size of their area. Disabled while a run works.
    tradeoff: A process that measures the image measures the area alone. The area carries the image's samples but not its header cards or plate solution, so a process that reads them sees none while it previews an area.
    scope: The preview only, with the mask cut to the same area. **Apply** works on the whole image, and a snapshot is the area over the whole original. While an area is drawn, a drag previews the area itself.
    persistence: Until the task closes, also while **Preview** is off. **Reset** leaves it as it is.
  - name: Processed
    kind: button
    default: "Off"
    effect: Switches the canvas between the result and the original, and stays there until you switch back; it reads **Original** while the canvas shows the original. Nothing is computed again.
    range: Holding ++space++, or the right mouse button, over the canvas shows the other image while you hold it. ++ctrl+shift+o++ switches as the button does.
    enabled: Disabled while a run works. Image Compare names its two images **Image 1** and **Image 2** here.
    scope: The canvas, and the histogram, which counts the original while the canvas shows it.
```

```controls
window: The column's body
controls:
  - name: Message strip
    kind: readout
    default: ""
    effect: Shows what the task has to report, under the toggles, as information, a warning or an error, sometimes with how far a step has come and a button at its end, such as Restart.
    enabled: Hidden until there is a message, and while a run's row takes its place.
    note: A failed preview shows "The preview failed" and the reason, once for each different reason. The next preview that lands, or turning **Preview** off, clears it. A failed run shows its error here and opens no message box.
  - name: Run
    kind: readout
    default: ""
    effect: In the message strip's place during a run, a turning arc, what the run does - "applying", or a stacking tool's own step - how far it has come when it can tell, and a thin bar. Under it, "The task keeps running if you leave this page."
  - name: Overall and Processing
    kind: readout
    default: ""
    effect: During a run over a sequence, two bars in the run's row - **Overall** for the frames done, **Processing** for the frame on its way - each with its share.
    range: A bar that has nothing to tell yet shows a slice that slides along it.
  - name: Target Image
    kind: combo
    default: The active image
    options: [The active image, Every other open image or sequence the task can work on]
    effect: Chooses the image or sequence that **Apply** changes. The histogram, the choices of **Channel**, the preview and the mask follow it.
    range: It lists the open views the task can work on, and follows them as they open, close, change or are renamed. The tooltip of each item gives its source file, size, channels, pixel type and memory.
    enabled: Disabled while a run works. A task without it stays on the image it opened on.
    tradeoff: Apply replaces the image chosen here. Check it, and the task header, before you apply.
    persistence: Until the task closes. **Reset** leaves it as it is. Choosing another target returns **Channel** to its first choice and moves a preview area to the middle of the new image.
    note: "When the target closes, a run on its way stops and writes nothing, and the task says \"The target image '<name>' has been closed. The process will be closed as well.\" and closes. When the target changes into an image the task cannot work on, it says \"The target image '<name>' has changed and isn't supported by this process any more. The process will be closed.\" and closes."
  - name: Channel
    kind: combo
    default: RGB
    options: [RGB, Red, Green, Blue, CIE L* (Lightness), CIE a*, CIE b*, CIE c*, CIE h*, CIE X, CIE Y (Luminance), CIE Z, HSL Hue, HSL Saturation, HSL Lightness, HSV Hue, HSV Saturation, HSV Value, YCbCr Luma, YCbCr Blue Chroma, YCbCr Red Chroma]
    effect: Chooses what the process works on. **RGB** gives it the three channels, each on its own. **Red**, **Green** or **Blue** gives it that channel and copies the other two. Every other choice converts the image to another color model, gives the process one component, and converts back to red, green and blue with the other two components as they were.
    range: "The choices work on these components. **CIE L* (Lightness)** is the lightness of L*a*b*, **CIE a*** its green to red axis and **CIE b*** its blue to yellow axis. **CIE c*** is the chroma and **CIE h*** the hue angle of L*c*h*. **CIE X**, **CIE Y (Luminance)** and **CIE Z** are the tristimulus values. **HSL Hue**, **HSL Saturation** and **HSL Lightness**, and **HSV Hue**, **HSV Saturation** and **HSV Value**, are those of the two models. **YCbCr Luma**, **YCbCr Blue Chroma** and **YCbCr Red Chroma** use the BT.601 weights at full range, with unrounded coefficients. Each component reaches the process between 0 and 1: lightness is divided by 100, chroma by 200, the CIE hue runs from -180 to 180 degrees, a* and b* are shifted by 128 and divided by 255, X, Y and Z are divided by those of the white, and the HSL and HSV hues are a share of a full turn. a*, b* and both chromas are neutral near one half."
    enabled: For an RGB image. A grayscale image shows a disabled **Grayscale**, and the process works on its one channel. Disabled while a run works.
    tradeoff: The same change on each of the three channels changes the ratios between them, which shifts colors. A component costs two conversions of the whole image and a working copy of it. The conversion back is not clamped, so a floating-point image keeps colors outside 0 to 1, and an integer image is cut at the ends of its type.
    scope: "The preview and Apply, on images and sequences, before the mask is blended. It does not choose the lines the histogram draws, and it is not the **View Channel** of the canvas's toolbar, which changes only what you see. The tasks with this list: Adjust Brightness/Contrast, Exposure Correction, Gamma Correction, GRAYCstoration, NL-Bayes, Patch PCA Denoise, Total Variation Denoise, Wavelet Denoise, Banding Reduction, Bilateral Filter, Cosmetic Correction, Gaussian Blur, Median Filter, Morphological Transformation, Noise Generator, CLAHE, Dark Structure Enhance, Local Contrast Enhancement, Local Histogram Equalization, Sharpen Alpha, Tighten Edges, Unsharp Mask and Multiscale Linear Transform. Curves Transformation has the same list without CIE X, CIE Y and CIE Z."
    persistence: Until the task closes. A finished Apply, another target and any other change of the target's image return it to **RGB**, and so does **Reset** in the tasks whose page says so.
    note: "**Red**, **Green** and **Blue** are worked in the image's own pixel type. A component is worked in a 32-bit floating-point plane, or 64-bit for a 64-bit image, so an integer image is rounded once, on the way back, and a process that changes nothing returns a 16-bit image within one code of where it was. The red, green and blue are converted as linear when the image is described as linear RGB, and as sRGB-encoded otherwise, with sRGB primaries and a D65 white, and the conversion back is the exact inverse of the conversion there. An image of sensor codes or of a physical quantity is read as fractions of its full scale. A pixel the image marks as invalid reaches the process as not-a-number and is put back as it was. Hue is treated as a line from 0 to 1, not as a circle. The header, the plate solution and the acquisition data are kept with every choice. The list has no HSI choices and no combined a*/b* display, which the View Channel of a view has."
```

The body scrolls, and a process's groups of controls are sections that open and close
([The column](tasks-reference.md#the-column)).

### The footer

```controls
window: The footer
controls:
  - name: Reset
    kind: button
    default: ""
    effect: Returns the process's controls to their defaults, and one preview of what they give follows.
    range: Which controls it returns is the process page's to say. It leaves the target, **Preview**, the preview area, the histogram toolbar's choices and the histogram's zoom as they are, except that Histogram Transformation's **Reset** restores the zoom.
    enabled: Disabled while a run works.
    scope: The task only. The image and its history are not touched.
    note: In most tasks its tooltip reads "Reset all parameters to their default values."
  - name: Close
    kind: button
    default: ""
    effect: Closes the task without applying. Its values are discarded, and the centre shows the place you were on before the task.
    range: ++esc++, **Close** in the column's header and in the task header, the task's rail menu and **Window > Close Task** do the same.
    enabled: Disabled while a run works, as every other way to close the task is.
    persistence: "A task keeps none of its values after it closes, except the stacking tools: Image Calibration, Image Registration, Image Stacking, Image Integration, Image Integration Lite and Image List Analyzer."
    note: Background Neutralization, Mask Drawing and Range Selection have **Cancel** in its place, which does the same. In most tasks its tooltip reads "Close this dialog without applying any changes."
  - name: Apply
    kind: button
    default: ""
    effect: Runs the process over the whole target, with the values, the channel, the mask and its inversion as they are when you press it. It replaces the image, or every frame of the sequence, and returns the controls to their defaults.
    range: A preview area does not limit it. A preview on its way is stopped first. ++ctrl+enter++, from either Enter key, and **Process > Apply** do the same for the task shown.
    enabled: In most tasks it is disabled when the task opens, until a value leaves its default; the process page says when. During a run **Cancel** takes its place, and **Target Image**, **Channel**, the toggles, **Reset**, **Close** and the process's own controls are disabled.
    tradeoff: The run needs a second copy of the image in memory, and a sequence needs the disk space of a second copy.
    scope: "The target, through the mask on its view. Some tasks first mix their result with the original by their **Amount**, as their pages say. The mask then blends the two: white takes the result, black keeps the original, gray mixes them, and **Invert Mask** swaps the two. The mask must have the target's width and height, and one channel, which blends every channel, or one for each of the target's channels. It counts while it is enabled, whether or not it is shown over the view. While another task works on the target, Apply shows \"Another process is executing for this image, please cancel that job first before running this process\", with \"sequence\" for a sequence, and does nothing."
    persistence: The image keeps the result, and its history gets one step named after the process, recorded before the view shows the result. A result the history cannot record is not shown. A run that fails or is stopped writes nothing, and the image and its history stay as they were. One preview of the new image follows.
    note: Some tasks have **OK** in its place, some write a new image instead, and some close when they are done; their pages say so. A change made while the run works does not reach it.
  - name: Cancel
    object: pageCancel
    kind: button
    default: ""
    effect: Shown in the place of **Apply** during a run. It asks the run to stop; the image keeps the state it had before Apply.
    range: The request is noticed at points inside the work, so the step on its way finishes first. Over a sequence, the frame being read or written finishes first.
    persistence: Nothing is published. A sequence's temporary copy is removed, and the image and its history stay as they were. A sequence is replaced, with its one history step, only when every frame has been read, processed and written, and a frame that fails ends the run as Cancel does.
```

### The histogram

The tasks of tone and color processes show a histogram among their controls, with a toolbar over it.

```controls
window: The histogram
controls:
  - name: Histogram
    kind: readout
    default: ""
    effect: Shows how the samples are spread between 0 and 1, one white line for a grayscale image and a red, a green and a blue one for RGB. With **Preview** on it is counted from the preview, with the mask blended in, and from the original while the canvas shows the original. With **Preview** off it is counted from the image as it is.
    range: In the bins of **Plot resolution**. A sample outside 0 to 1 is counted in the end bin nearest it, a sample that is not a number in none, and only the samples the image marks as valid are counted.
    enabled: In the tasks of tone and color processes. In most of them its lines follow the channel the canvas displays.
    tradeoff: It is counted again with every preview, which adds to the work of each change.
    scope: The task only. It never changes a sample.
    note: The wheel with ++ctrl++ held zooms about the pointer, 1.2 times in and 0.8 times out, and the wheel alone scrolls. Dragging with the middle button pans a plot larger than its frame. In Histogram Transformation and Generalized Hyperbolic Transformation a right click restores the zoom. The plot stays dark in the light theme. A strip under it runs from black to white, or to red, green or blue while the canvas displays that channel alone.
  - name: Clipping Low and Clipping High
    kind: readout
    default: ""
    effect: Give the shares of the counted samples at or below 0 and at or above 1, over all the channels together, with three decimals.
    range: They are counted from the samples, not from the bins, so **Plot resolution** does not change them, and a sample just inside either end is not counted. They are shares of the valid samples.
    enabled: Shown while **Show clipping info** is on.
    persistence: Counted again when new counts arrive. While **Show histogram** is off they keep their last numbers.
  - name: Coordinates
    kind: readout
    default: ""
    effect: >-
      While you hold the left button on the plot, shows the position under the pointer from 0 to 1, and the height
      there as a share of the tallest bin drawn, as `x: 0.250, y: R 0.120  G 0.300  B 0.110`, or one height for a
      grayscale image.
    enabled: On every histogram. In Histogram Transformation and Generalized Hyperbolic Transformation, away from the handles.
```

```controls
window: The histogram toolbar
controls:
  - name: Zoom in
    kind: toolbutton
    default: ""
    effect: Enlarges the plot 1.3 times in width and height. Scroll bars appear when it is larger than its frame.
    scope: The drawing only, in both histograms of a task that has two. It never changes the bins.
    persistence: Until the task closes, **Zoom reset** or a hidden and shown histogram. **Reset** leaves it, except in Histogram Transformation.
  - name: Zoom out
    kind: toolbutton
    default: ""
    effect: Reduces the plot to 0.7 times its width and height.
    tradeoff: A plot narrower than its frame can hide a narrow peak.
  - name: Zoom reset
    kind: toolbutton
    default: ""
    effect: Fits the plot to its frame and returns its scrolling to the start.
  - name: Show grid
    kind: toolbutton
    default: "On"
    effect: Shows or hides the grid of eight by eight divisions, in both histograms of a task that has two.
    persistence: Until the task closes. **Reset** leaves it. Hiding and showing the histogram draws the grid again, even when this button is off.
  - name: Show clipping info
    kind: toolbutton
    default: "On"
    effect: Shows or hides **Clipping Low** and **Clipping High** in the plot's corner.
    enabled: In every task with this toolbar except Histogram Transformation and Curves Transformation.
    persistence: Until the task closes. **Reset** leaves it.
  - name: Show transfer curve
    kind: toolbutton
    default: "On"
    effect: Shows or hides the curve the task's values define, over the histogram of the image.
    enabled: In Histogram Transformation. Generalized Hyperbolic Transformation has the same button as **Transfer curve**.
    tradeoff: Left on, it shows an extreme setting at once.
    persistence: Until the task closes. **Reset** leaves it.
  - name: Transfer curve
    kind: toolbutton
    default: "On"
    effect: Shows or hides the curve of Generalized Hyperbolic Transformation over the histogram of the image.
    enabled: In Generalized Hyperbolic Transformation.
    persistence: Until the task closes. **Reset** leaves it.
  - name: Show guide
    kind: toolbutton
    default: "On"
    effect: Shows or hides the dotted diagonal of Curves Transformation, which is the curve of an unchanged image.
    enabled: In Curves Transformation.
  - name: Absolute scales
    kind: toolbutton
    default: "Off"
    effect: On draws each displayed channel to its own peak, which makes a weak channel visible and hides how the channels compare. Off draws them against one maximum. The label suggests the opposite of what it does.
    enabled: In every task with this toolbar except Curves Transformation. Generalized Hyperbolic Transformation opens with it on. In that task and in Histogram Transformation it is disabled for a grayscale image.
    scope: The drawing only. It never changes the image.
    persistence: Until the task closes. **Reset** leaves it.
  - name: Draw histogram areas
    kind: toolbutton
    default: "Off"
    effect: Draws each channel as a translucent filled area instead of a line, with the colors added where they overlap.
    tradeoff: Overlapping channels can be easier to compare, and a dense area can hide a line under it.
    persistence: Until the task closes. **Reset** leaves it.
  - name: Show histogram
    kind: toolbutton
    default: "On"
    effect: Off clears the plot, restores its zoom and disables the other buttons and **Plot resolution**. On counts the preview, or the image, again.
    enabled: In every task with this toolbar except Histogram Transformation, Generalized Hyperbolic Transformation, Statistical Stretch and Image Compare. Curves Transformation has a button of its own with this name.
    tradeoff: Hiding it saves the histogram's counting, not the preview's work.
    persistence: Until the task closes. **Reset** leaves it.
  - name: Plot resolution
    kind: combo
    default: 8-bit (256)
    options: [6-bit (64), 7-bit (128), 8-bit (256), 9-bit (512), 10-bit (1024), 12-bit (4096), 14-bit (16K), 16-bit (64K), 100 levels, 1000 levels, 10000 levels]
    effect: Chooses how many bins the histogram counts, and asks for a new preview counted in them.
    range: "From 64 to 65,536 bins. Of n bins, bin i holds the values from i/(n-1) up to (i+1)/(n-1), and the last bin holds full scale alone, so a whole code on a bin boundary lands in its own bin. A plot with more bins than pixels across is summed to its width, and drawn again to fill it when the column is resized."
    enabled: In every task with this toolbar except Curves Transformation.
    tradeoff: More bins show finer structure and cost counting time, drawing time and memory, up to 65,536 counters a channel.
    scope: The histogram only. It never changes the process or the precision of the result. The **Clipped** fields of Histogram Transformation count whole bins at this resolution, and the clipping shares do not change with it.
    persistence: Until the task closes. **Reset** leaves it.
```

## When it goes wrong

"An existing process of this type is already running for the target image"
:   **Cause.** A task of the same process is already open on that image.
:   **Fix.** Choose that task in the rail, or close it first.

"Another process is executing for this image, please cancel that job first before running this process"
:   **Cause.** Another task is applying to the same image.
:   **Fix.** Wait for that run to end, or cancel it, then apply again.

"Error executing the process" in the message strip
:   **Cause.** The process refused the run, for the reason the message gives, and nothing was written.
:   **Fix.** Correct what the reason names, often a value or the memory limit, and apply again.

"The preview failed" in the message strip
:   **Cause.** The process refused the preview with the values you set, or the mask does not fit the image.
:   **Fix.** Change the value the reason names, or give the mask the image's size.

**Close** is disabled and ++esc++ does nothing
:   **Cause.** The task's run is on its way.
:   **Fix.** Wait for the run to end, or choose **Cancel**.

**Preview** turned itself off
:   **Cause.** Another task turned on a real-time preview of the same image.
:   **Fix.** Turn that task's **Preview** off, then tick **Preview** again here.

## Related

- [Tasks Reference](tasks-reference.md), for every message and the tasks that differ.
- [Rail and Places](rail-and-places.md), for a task's item in the rail.
- [Image Viewer](image-viewer.md), for the zoom, the channels and the
  [Autostretch](../reference/glossary.md#autostretch) the canvas shares.
- [Masks](masks.md), for the mask a run blends through.
- [Image History](image-history.md), to undo an Apply.
- [Settings](settings-dialog.md), for the preview's rate on the **Processing** page.
