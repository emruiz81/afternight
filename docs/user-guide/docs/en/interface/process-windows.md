---
kind: interface
summary: >-
  What every process window shares: the image and channel it works on, the real-time preview and its toolbar, the
  histogram toolbar, Reset, Close and Apply, masks, and the progress of a run.
rewritten: true
---
# Process Windows

A command of the **Process** menu that has settings opens a process window over the
[active image](../reference/glossary.md#active-image). The window holds the process's own controls, the image it works
on, a [real-time preview](../reference/glossary.md#real-time-preview) of the result and the buttons that apply it. Some
windows, such as the stacking windows, choose their images themselves and open without one. A command without
settings runs at once: see [Non-Interactive Processes](../processes/utility/non-interactive-processes.md). This page
describes what the windows share. Each process page describes the process's own controls and links here for the rest.

```figure
image: ../assets/images/interface/process-windows.webp
alt: >-
  The Gamma Correction window. Under the title bar, the process name and a line that says what it does. On the left,
  a histogram with three colored peaks and two clipping shares in its corner, a row of toolbar buttons with a Plot
  resolution box, the Gamma box at 0.50 with its slider, then Target Image, Channel at RGB and the Preview check box.
  On the right, a toolbar of zoom buttons, a channel box, a switch, the Preview area check box and a camera button
  over the preview of a red nebula, with the word PREVIEW in its corner and a framed area in its middle. Reset, Close
  and Apply run along the bottom.
caption: >-
  Gamma Correction on a stretched master with Gamma at 0.50 and a preview area drawn: the parts every process window
  shares, around the process's own control.
pins:
  - {n: 1, x: 15.5, y: 7.5}
  - {n: 2, x: 40.0, y: 20.0}
  - {n: 3, x: 2.6, y: 48.5}
  - {n: 4, x: 2.6, y: 54.0}
  - {n: 5, x: 2.6, y: 79.4}
  - {n: 6, x: 2.6, y: 84.3}
  - {n: 7, x: 2.6, y: 88.9}
  - {n: 8, x: 45.6, y: 18.2}
  - {n: 9, x: 47.5, y: 55.0}
  - {n: 10, x: 82.4, y: 96.0}
```

## What you see

1. **The header.** The title names the process and, in angle brackets, the image it works on. Under it, a line says
   what the process does, and a message strip appears when the window has something to report.
2. **The histogram.** How the samples of the preview are spread, with the clipping shares in its corner. The windows
   of tone and color processes have one.
3. **The histogram toolbar.** Buttons and a **Plot resolution** box that change how the histogram is drawn, never
   the image.
4. **The process's controls.** What the process page describes.
5. **Target Image.** The image or [sequence](../reference/glossary.md#sequence) that **Apply** changes.
6. **Channel.** What the process works on in a color image: the three channels, one of them, or one component of
   another [color model](../reference/glossary.md#color-model). Only some windows have it.
7. **Preview.** Shows or hides the preview pane.
8. **The preview's toolbar.** Zoom, the channel and mode the preview is displayed in, the switch to the original,
   [**Preview area**](../reference/glossary.md#preview-area) and the snapshot. What does not fit the pane's width
   moves into a menu at its end.
9. **The preview.** The result as **Apply** would write it, with the [mask](../reference/glossary.md#mask) on the
   target's view blended in. A word in its corner says whether it shows the PREVIEW or the ORIGINAL, and a framed
   preview area limits the work to part of the image.
10. **Reset, Close and Apply.** They return the controls to their defaults, close the window without applying, and
    apply. Some windows have **OK** in place of **Apply**, or **Cancel** in place of **Close**.

## What you can do

### Open a window

Make the image or sequence the active view and choose the process from the **Process** menu. **Target Image** names
the view, and the preview shows the image unchanged until the first result lands. A window does not open twice on one
image, and some processes need the image to themselves. A process that cannot open is disabled in the menu, and its
tooltip says why.

### Preview and compare

1. Change a value. While you drag a slider, the preview follows at the rate set on the **Processing** page of the
   settings. When you let go, or hold still for a moment, one more preview shows what **Apply** would write.
2. Hold ++space++, or the right mouse button, over the preview to see the original, and let go to see the result.
   **Switch image** switches and stays.
3. To check one region quickly, tick **Preview area**, then move and resize its frame on the preview.
4. Choose **Take snapshot** to keep the preview as a new image.

### Apply to an image

1. Check **Target Image**. When the target's view carries a mask, check that it is enabled, and whether it is
   inverted: white takes the result, black keeps the original and gray mixes them.
2. Choose **Apply**. The window's controls are disabled while the run works.
3. The view shows the result, its [history](../reference/glossary.md#history) gets one step named after the process,
   and the controls return to their defaults.

### Apply to a sequence

1. Make the sequence the target. To process only some frames, select them in its player first.
2. Choose **Apply**. A progress window follows the frames.
3. The sequence in the view is replaced when every frame is done. **Cancel** stops the run and keeps the original.

### Start again or leave

Choose **Reset** to return the process's controls to their defaults. Choose **Close**, or press ++esc++, to leave
without applying. The next window of the same process opens at the size you left this one.

## Controls

### The window

```controls
window: The window
controls:
  - name: Title
    kind: readout
    default: ""
    effect: Names the process and, in angle brackets, its target, as in `Gamma Correction <M42>`. The header inside the window repeats the process's name over a line that says what the process does, in the windows that have one.
    enabled: It follows a rename of the target. A window that chooses its images itself names no target.
  - name: Message strip
    kind: readout
    default: ""
    effect: Shows what the window has to report, under the header, as information, a warning or an error, sometimes with a progress bar.
    enabled: Hidden until there is a message.
    note: A failed preview shows "The preview failed" and the reason, once for each different reason. The next preview that lands, or turning **Preview** off, clears it. A failed run shows its error here as well as in a message box.
  - name: Target Image
    kind: combo
    default: The active image
    options: [The active image, Every other open image or sequence the window can work on]
    effect: Chooses the image or sequence that **Apply** changes. The histogram, the choices of **Channel**, the preview and the mask follow it.
    range: It lists the open views the window can work on, and follows them as they open, close, change or are renamed. The tooltip of each item gives its source file, size, channels, pixel type and memory.
    enabled: Disabled while a run works. A window without it stays on the image it opened on.
    tradeoff: Apply replaces the image chosen here. Check it, and the window's title, before you apply.
    persistence: Until the window closes. **Reset** leaves it as it is. Choosing another target returns **Channel** to its first choice and moves a preview area to the middle of the new image.
    note: "When the target closes, a run on its way stops and writes nothing, and the window says \"The target image '<name>' has been closed. The process will be closed as well.\" and closes. When the target changes into an image the window cannot work on, it says \"The target image '<name>' has changed and isn't supported by this process any more. The process will be closed.\" and closes."
  - name: Channel
    kind: combo
    default: RGB
    options: [RGB, Red, Green, Blue, CIE L* (Lightness), CIE a*, CIE b*, CIE c*, CIE h*, CIE X, CIE Y (Luminance), CIE Z, HSL Hue, HSL Saturation, HSL Lightness, HSV Hue, HSV Saturation, HSV Value, YCbCr Luma, YCbCr Blue Chroma, YCbCr Red Chroma]
    effect: Chooses what the process works on. **RGB** gives it the three channels, each on its own. **Red**, **Green** or **Blue** gives it that channel and copies the other two. Every other choice converts the image to another color model, gives the process one component, and converts back to red, green and blue with the other two components as they were.
    range: "The choices work on these components. **CIE L* (Lightness)** is the lightness of L*a*b*, **CIE a*** its green to red axis and **CIE b*** its blue to yellow axis. **CIE c*** is the chroma and **CIE h*** the hue angle of L*c*h*. **CIE X**, **CIE Y (Luminance)** and **CIE Z** are the tristimulus values. **HSL Hue**, **HSL Saturation** and **HSL Lightness**, and **HSV Hue**, **HSV Saturation** and **HSV Value**, are those of the two models. **YCbCr Luma**, **YCbCr Blue Chroma** and **YCbCr Red Chroma** use the BT.601 weights at full range, with unrounded coefficients. Each component reaches the process between 0 and 1: lightness is divided by 100, chroma by 200, the CIE hue runs from -180 to 180 degrees, a* and b* are shifted by 128 and divided by 255, X, Y and Z are divided by those of the white, and the HSL and HSV hues are a share of a full turn. a*, b* and both chromas are neutral near one half."
    enabled: For an RGB image. A grayscale image shows a disabled **Grayscale**, and the process works on its one channel. Disabled while a run works.
    tradeoff: The same change on each of the three channels changes the ratios between them, which shifts colors. A component costs two conversions of the whole image and a working copy of it. The conversion back is not clamped, so a floating-point image keeps colors outside 0 to 1, and an integer image is cut at the ends of its type.
    scope: "The preview and Apply, on images and sequences, before the mask is blended. It does not choose the lines the histogram draws, and it is not the **View Channel** of the preview's toolbar, which changes only what you see. The windows with this list: Adjust Brightness/Contrast, Exposure Correction, Gamma Correction, GRAYCstoration, NL-Bayes, Patch PCA Denoise, Total Variation Denoise, Wavelet Denoise, Banding Reduction, Bilateral Filter, Cosmetic Correction, Gaussian Blur, Median Filter, Morphological Transformation, Noise Generator, CLAHE, Dark Structure Enhance, Local Contrast Enhancement, Local Histogram Equalization, Sharpen Alpha, Tighten Edges, Unsharp Mask and Multiscale Linear Transform. Curves Transformation has the same list without CIE X, CIE Y and CIE Z."
    persistence: Until the window closes. A finished Apply, another target and any other change of the target's image return it to **RGB**, and so does **Reset** in the windows whose page says so.
    note: "**Red**, **Green** and **Blue** are worked in the image's own pixel type. A component is worked in a 32-bit floating-point plane, or 64-bit for a 64-bit image, so an integer image is rounded once, on the way back, and a process that changes nothing returns a 16-bit image within one code of where it was. The red, green and blue are converted as linear when the image is described as linear RGB, and as sRGB-encoded otherwise, with sRGB primaries and a D65 white, and the conversion back is the exact inverse of the conversion there. An image of sensor codes or of a physical quantity is read as fractions of its full scale. A pixel the image marks as invalid reaches the process as not-a-number and is put back as it was. Hue is treated as a line from 0 to 1, not as a circle. The header, the plate solution and the acquisition data are kept with every choice. The list has no HSI choices and no combined a*/b* display, which the View Channel of a view has."
  - name: Preview
    kind: check
    default: "On"
    effect: Shows the preview pane beside the controls. Off stops the preview's work, removes the pane and shrinks the window to the controls. On brings the pane back at the size it had.
    enabled: In most windows it stays enabled while a run works. Profiled Starmask Recombination opens with it off.
    tradeoff: Each change computes the preview again and counts its histogram again. On a large image, turn it off or draw a preview area while you adjust.
    scope: The window only. The pane holds its own copy of the result, and the target, its history and its unsaved state are not touched. With it off, the histogram counts the image as it is.
    persistence: Until the window closes. **Reset** leaves it as it is.
    note: Only one window shows a real-time preview of an image at a time. Turning a preview on, or opening a window with one, turns **Preview** off in the other windows open on that image.
  - name: Reset
    kind: button
    default: ""
    effect: Returns the process's controls to their defaults, and one preview of what they give follows.
    range: Which controls it returns is the process page's to say. It leaves the target, **Preview**, the preview area, the histogram toolbar's choices and the histogram's zoom as they are, except that Histogram Transformation's **Reset** restores the zoom.
    enabled: Disabled while a run works.
    scope: The window only. The image and its history are not touched.
    note: In most windows its tooltip reads "Reset all parameters to their default values."
  - name: Close
    kind: button
    default: ""
    effect: Closes the window without applying. Its values are discarded.
    range: ++esc++ and the window's own close button do the same.
    enabled: Disabled while a run works in most windows. Closing the window another way during a run stops a sequence, a new image or a snapshot, and an Apply that shows a progress window, as **Cancel** would. An Apply to a single image without a progress window goes on to its end. The window disappears at once and closes when the run has ended, without holding up the application.
    persistence: "A window keeps none of its values after it closes, except the stacking windows: Image Calibration, Image Registration, Image Stacking, Image Integration, Image Integration Lite and Image List Analyzer. Its size, and whether it was maximized, are kept for the next window of the same process while **Remember window size and position** is on, on the Interface page of the settings, as it is unless you change it. A kept size is cut to the screen the window opens on."
    note: In most windows its tooltip reads "Close this dialog without applying any changes."
  - name: Apply
    kind: button
    default: ""
    effect: Runs the process over the whole target, with the values, the channel, the mask and its inversion as they are when you press it. It replaces the image, or every frame of the sequence, and returns the controls to their defaults.
    range: A preview area does not limit it. A preview on its way is stopped first.
    enabled: In most windows it is disabled when the window opens, until a value leaves its default; the process page says when. While the run works it is disabled, with **Target Image**, **Channel**, **Reset**, **Close** and the process's own controls.
    tradeoff: The run needs a second copy of the image in memory, and a sequence needs the disk space of a second copy. A single image shows no progress window in most windows, and its run cannot be cancelled once it starts.
    scope: "The target, through the mask on its view. Some windows first mix their result with the original by their **Amount**, as their pages say. The mask then blends the two: white takes the result, black keeps the original, gray mixes them, and **Invert Mask** swaps the two. The mask must have the target's width and height, and one channel, which blends every channel, or one for each of the target's channels. It counts while it is enabled, whether or not it is shown over the view. While another process works on the target, Apply shows \"Another process is executing for this image, please cancel that job first before running this process\", with \"sequence\" for a sequence, and does nothing."
    persistence: The image keeps the result, and its history gets one step named after the process, recorded before the view shows the result. A result the history cannot record is not shown. A run that fails or is stopped writes nothing, and the image and its history stay as they were. One preview of the new image follows.
    note: Some windows write a new image instead, and some close when they are done; their pages say so. A change made while the run works does not reach it.
  - name: OK
    kind: button
    default: ""
    effect: Runs the process as **Apply** does, in the windows that have it in place of **Apply**.
    note: Whether the window closes afterwards, and whether the result is a new image, is the process page's to say.
  - name: Cancel
    kind: button
    default: ""
    effect: Closes the window without applying, as **Close** does, in the windows that have it in place of **Close**.
    note: Background Neutralization, Mask Drawing and Range Selection have it.
```

### The preview

```controls
window: The preview pane
controls:
  - name: Fit To Window
    kind: toolbutton
    default: ""
    effect: Fits the whole image into the pane.
    note: ++ctrl+shift+f++ does the same. The pane fits the image when it opens.
  - name: Zoom In
    kind: toolbutton
    default: ""
    effect: Shows the preview larger, up to 6,400 percent.
    scope: The pane only, apart from the histogram's zoom. The mouse gestures of an image's view work here too.
  - name: Zoom Out
    kind: toolbutton
    default: ""
    effect: Shows the preview smaller, down to 1 percent.
  - name: Zoom 100%
    kind: toolbutton
    default: ""
    effect: Shows one sample of the image on one pixel of the screen.
  - name: View Channel
    kind: combo
    default: RGB
    options: [RGB, Red, Green, Blue, The other display channels of an image's view]
    effect: Chooses what the preview displays. It changes nothing the process does.
    enabled: A grayscale image shows a disabled **Grayscale**.
    scope: The pane, and the lines the histogram draws. With **Red**, **Green** or **Blue** the histogram draws that channel's line alone, and with any other choice all three.
  - name: Display Mode
    kind: combo
    default: The target view's mode
    options: [The target view's mode, Linear, Autostretch]
    effect: Shows the preview as its samples are, in **Linear**, or stretched for the screen, in **Autostretch**. It changes nothing the process does.
    enabled: Only in some windows, which the reference page lists.
    scope: The pane only. **Autostretch** uses the Autostretch settings of the target's view, and the stretch is measured on the target, so it does not move while you adjust.
  - name: Autostretch Settings
    kind: toolbutton
    default: ""
    effect: Opens the Autostretch settings for the preview, with the values of the target's view.
    enabled: With **Display Mode** at **Autostretch**. ++ctrl+shift+a++ does the same.
    scope: The pane's display only. The dialog has no **Apply Stretch** here.
  - name: Switch image
    kind: toolbutton
    default: ""
    effect: Switches the pane between the result and the original, and stays there until you switch back. Nothing is computed again.
    range: Holding ++space++, or the right mouse button, over the preview shows the other image while you hold it. ++ctrl+shift+o++ switches as the button does.
    scope: The pane, and the histogram, which counts the original while the pane shows it.
  - name: Preview area
    kind: check
    default: "Off"
    effect: Limits the preview's work to a frame you move and resize on the preview. The area is processed as an image of its own and shown over the original.
    range: >-
      The frame starts in the middle of the image, at a size each window gives it, and stays inside the image. Its
      label names it, as "Preview area" or "Preview: Median Filter".
    enabled: Several windows open with it on, which the reference page lists with the size of their area. Auto Gradient Removal, Background Extraction and Narrowband Palette Composer have none. Disabled while a run works.
    tradeoff: A process that measures the image measures the area alone. The area carries the image's samples but not its header cards or plate solution, so a process that reads them sees none while it previews an area.
    scope: The preview only, with the mask cut to the same area. **Apply** works on the whole image, and a snapshot is the area over the whole original. While an area is drawn, a drag previews the area itself.
    persistence: Until the window closes, also while **Preview** is off. **Reset** leaves it as it is.
  - name: Take snapshot
    kind: toolbutton
    default: ""
    effect: Opens a new image of the preview's result, named `<target> - <process> - SNAPSHOT`, as `M42 - Gamma Correction - SNAPSHOT`.
    range: It runs the process again over the whole image, at the image's own depth, with the values, the channel and the mask as they are. With a preview area, the new image is the area over the whole original.
    enabled: Disabled while a run works. ++ctrl+shift+k++ does the same.
    scope: The target and its history are not changed. The new image's history starts with the process's step.
    persistence: Each press makes another image.
  - name: PREVIEW or ORIGINAL
    kind: readout
    default: PREVIEW
    effect: Says which image the pane shows, in its top left corner.
    note: Image Blend, Image Compare and Star Reduction name their two images in their own words.
  - name: Waiting indicator
    kind: readout
    default: ""
    effect: Turns over the preview while a preview has been running for more than about a third of a second.
    enabled: In most windows.
```

### The histogram

```controls
window: The histogram
controls:
  - name: Histogram
    kind: readout
    default: ""
    effect: Shows how the samples are spread between 0 and 1, one white line for a grayscale image and a red, a green and a blue one for RGB. With **Preview** on it is counted from the preview, with the mask blended in, and from the original while the pane shows the original. With **Preview** off it is counted from the image as it is.
    range: In the bins of **Plot resolution**. A sample outside 0 to 1 is counted in the end bin nearest it, a sample that is not a number in none, and only the samples the image marks as valid are counted.
    enabled: In the windows of tone and color processes. In most of them its lines follow the channel the pane displays.
    tradeoff: It is counted again with every preview, which adds to the work of each change.
    scope: The window only. It never changes a sample.
    note: The wheel with ++ctrl++ held zooms about the pointer, 1.2 times in and 0.8 times out, and the wheel alone scrolls. Dragging with the middle button pans a plot larger than its frame. In Histogram Transformation and Generalized Hyperbolic Transformation a right click restores the zoom. The plot stays dark in the light theme. A strip under it runs from black to white, or to red, green or blue while the pane displays that channel alone.
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
    scope: The drawing only, in both histograms of a window that has two. It never changes the bins.
    persistence: Until the window closes, **Zoom reset** or a hidden and shown histogram. **Reset** leaves it, except in Histogram Transformation.
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
    effect: Shows or hides the grid of eight by eight divisions, in both histograms of a window that has two.
    persistence: Until the window closes. **Reset** leaves it. Hiding and showing the histogram draws the grid again, even when this button is off.
  - name: Show clipping info
    kind: toolbutton
    default: "On"
    effect: Shows or hides **Clipping Low** and **Clipping High** in the plot's corner.
    enabled: In every window with this toolbar except Histogram Transformation and Curves Transformation.
    persistence: Until the window closes. **Reset** leaves it.
  - name: Show transfer curve
    kind: toolbutton
    default: "On"
    effect: Shows or hides the curve the window's values define, over the histogram of the image.
    enabled: In Histogram Transformation. Generalized Hyperbolic Transformation has the same button as **Transfer curve**.
    tradeoff: Left on, it shows an extreme setting at once.
    persistence: Until the window closes. **Reset** leaves it.
  - name: Transfer curve
    kind: toolbutton
    default: "On"
    effect: Shows or hides the curve of Generalized Hyperbolic Transformation over the histogram of the image.
    enabled: In Generalized Hyperbolic Transformation.
    persistence: Until the window closes. **Reset** leaves it.
  - name: Show guide
    kind: toolbutton
    default: "On"
    effect: Shows or hides the dotted diagonal of Curves Transformation, which is the curve of an unchanged image.
    enabled: In Curves Transformation.
  - name: Absolute scales
    kind: toolbutton
    default: "Off"
    effect: On draws each displayed channel to its own peak, which makes a weak channel visible and hides how the channels compare. Off draws them against one maximum. The label suggests the opposite of what it does.
    enabled: In every window with this toolbar except Curves Transformation. Generalized Hyperbolic Transformation opens with it on. In that window and in Histogram Transformation it is disabled for a grayscale image.
    scope: The drawing only. It never changes the image.
    persistence: Until the window closes. **Reset** leaves it.
  - name: Draw histogram areas
    kind: toolbutton
    default: "Off"
    effect: Draws each channel as a translucent filled area instead of a line, with the colors added where they overlap.
    tradeoff: Overlapping channels can be easier to compare, and a dense area can hide a line under it.
    persistence: Until the window closes. **Reset** leaves it.
  - name: Show histogram
    kind: toolbutton
    default: "On"
    effect: Off clears the plot, restores its zoom and disables the other buttons and **Plot resolution**. On counts the preview, or the image, again.
    enabled: In every window with this toolbar except Histogram Transformation, Generalized Hyperbolic Transformation, Statistical Stretch and Image Compare. Curves Transformation has a button of its own with this name.
    tradeoff: Hiding it saves the histogram's counting, not the preview's work.
    persistence: Until the window closes. **Reset** leaves it.
  - name: Plot resolution
    kind: combo
    default: 8-bit (256)
    options: [6-bit (64), 7-bit (128), 8-bit (256), 9-bit (512), 10-bit (1024), 12-bit (4096), 14-bit (16K), 16-bit (64K), 100 levels, 1000 levels, 10000 levels]
    effect: Chooses how many bins the histogram counts, and asks for a new preview counted in them.
    range: "From 64 to 65,536 bins. Of n bins, bin i holds the values from i/(n-1) up to (i+1)/(n-1), and the last bin holds full scale alone, so a whole code on a bin boundary lands in its own bin. A plot with more bins than pixels across is summed to its width, and drawn again to fill it when the window is resized."
    enabled: In every window with this toolbar except Curves Transformation.
    tradeoff: More bins show finer structure and cost counting time, drawing time and memory, up to 65,536 counters a channel.
    scope: The histogram only. It never changes the process or the precision of the result. The **Clipped** fields of Histogram Transformation count whole bins at this resolution, and the clipping shares do not change with it.
    persistence: Until the window closes. **Reset** leaves it.
```

### Progress

```controls
window: Sequence progress
controls:
  - name: Progress
    kind: readout
    default: ""
    effect: Shows what the run does and how far it has come, from 0 to 100 percent. It reads "Preparing execution..." first and then, on a sequence, the frame being processed, as `Executing process in sequence. Frame 3/120`, one update a frame.
    range: Its title names the process and the image, as `Gamma Correction <M42>`. A window that also reports the work inside a frame shows a second bar.
    enabled: For every run on a sequence, and for a single image in the windows the reference page lists. It has no close button.
    tradeoff: A sequence is written again to a temporary file whose name starts with `SEQ_SWAP`, in the session's temporary folder, which needs the disk space of a second copy until the run ends.
    scope: The frames selected in the sequence's player, or every frame when none is selected, read with the view's automatic debayer setting. The player is disabled while the run works.
  - name: Cancel
    kind: button
    default: ""
    effect: Asks the run to stop. The text reads "Canceling execution..." and the button is disabled.
    range: The request is noticed before each frame and at points inside the work on one, so the frame being read or written finishes first.
    persistence: Nothing is published. The temporary sequence is removed, and the image and its history stay as they were. The sequence is replaced, with its one history step, only when every frame has been read, processed and written, and a frame that fails ends the run as Cancel does.
```

## When it goes wrong

The process is disabled in the menu: "An existing process of this type is already running for the target image"
:   **Cause.** A window of the same process is already open on that image, perhaps behind another window.
:   **Fix.** Use that window, or close it first.
    [Process Windows Reference](process-windows-reference.md) explains every other reason a process gives.

"Another process is executing for this image, please cancel that job first before running this process"
:   **Cause.** Another window is applying its process to the same image, and an image runs one process at a time.
:   **Fix.** Wait for that run to end, or cancel it, then choose **Apply** again.

"Error executing the process", with "An error occurred when running the process:" and a reason
:   **Cause.** The process refused the run, for the reason the message gives, and nothing was written.
:   **Fix.** Correct what the reason names, often a value or the memory limit on the **Processing** page of the
    settings, and apply again.

"The preview failed" over the controls
:   **Cause.** The process refused the preview with the values you set, or the mask does not fit the image.
:   **Fix.** Change the value the reason names, or give the mask the image's size. The strip clears when a preview
    lands.

The preview changes only part of the image
:   **Cause.** **Preview area** is on, as it is when some windows open.
:   **Fix.** Untick **Preview area** to preview the whole image. **Apply** works on the whole image either way.

**Preview** turned itself off
:   **Cause.** Another window turned on a real-time preview of the same image.
:   **Fix.** Close the other window, or turn its **Preview** off, then tick **Preview** again here.

## Related

- [Process Windows Reference](process-windows-reference.md), for every message, the windows that differ from the
  rest, and how the preview works while you drag.
- [Image Viewer](image-viewer.md), for the zoom, the display channels and the
  [Autostretch](../reference/glossary.md#autostretch) the preview pane shares.
- [Masks](masks.md), to make, attach, enable and invert the mask a run blends through.
- [Image History](image-history.md), to undo an Apply.
- [Sequences](sequences.md), for the player, its frame selection and the temporary files.
- [Settings](settings-dialog.md), for the preview's rate on the **Processing** page and the window sizes on the
  **Interface** page.
