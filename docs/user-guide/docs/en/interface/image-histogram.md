---
kind: interface
summary: >-
  Histogram Analysis draws how the stored samples of an image, or of the frame a sequence shows, spread from black to
  white, channel by channel, with the shares clipped at either end.
menu: Image > Information > Histogram
---
# Histogram Analysis

Histogram Analysis draws the histogram of the [active image](../reference/glossary.md#active-image), or of the frame
of a [sequence](../reference/glossary.md#sequence) its view shows: how many samples lie at each level, from black at
the left to white at the right. Use it to see clipping, how the channels sit against each other, where the tones
gather and what a step did to them. It counts the stored samples, never what the view displays, and it changes nothing
in the image, its header or its [history](../reference/glossary.md#history).

```figure
owed: true
image: ../assets/images/interface/image-histogram.webp
alt: >-
  The Histogram Analysis window. Under the title bar, the heading Histogram Analysis and a line about channel
  distributions. A card titled Histogram Plot and Controls holds a dark plot with a dotted grid, where red, green and
  blue filled areas rise together to sharp peaks a quarter of the way from the left and fall in a long low tail toward
  the right. Two lines in the top left corner read Clipping Low: 0.000% and Clipping High: 0.019%. A strip from black
  to white runs under the plot. Under it, a row of toolbar buttons, a box reading 8-bit (256), and a Channel box
  reading RGB. Reset and Close stand at the bottom right.
caption: >-
  Histogram Analysis of the Carina master after Histogram Transformation with Shadows and Midtones at 0.02, at its
  defaults.
pins:
  - {n: 1, x: 85.0, y: 40.0}
  - {n: 2, x: 10.0, y: 33.0}
  - {n: 3, x: 2.5, y: 84.0}
  - {n: 4, x: 55.0, y: 84.0}
  - {n: 5, x: 70.5, y: 84.0}
  - {n: 6, x: 72.5, y: 94.0}
```

## What you see

1. **The plot.** One white line or area for a grayscale image, a red, a green and a blue one for color. The height
   at each level is how many samples lie there, against the tallest peak drawn.
2. **The clipping shares.** The share of all the image's samples at or below black and at or above white.
3. **The toolbar.** Zoom, grid, clipping shares, scales and areas. It changes how the plot is drawn, never the counts.
4. **Plot resolution.** How many levels the samples are counted in.
5. **Channel.** Which lines the plot draws.
6. **Reset and Close.** **Reset** returns the window to the way it opened, and **Close** closes it.

## What you can do

### Open the histogram

Make the image the active view, or pause a sequence on the frame you want. Choose **Image > Information > Histogram**,
choose **Histogram** on the view's toolbar, or press ++ctrl+h++ over the view. The plot appears when the counting is
done, at **8-bit (256)**, with every channel drawn as a filled area.

### Read the levels and the clipping

The horizontal axis is the level as a [fraction of full scale](../reference/glossary.md#fraction-of-full-scale): 0 at
the left, 1 at the right, whatever the image's [pixel type](../reference/glossary.md#pixel-type). The strip under the
plot shows the level each place stands for. Read **Clipping Low** and **Clipping High** before a
[stretch](../reference/glossary.md#stretch). A share above zero says that samples sit at an end, not why: a black
border, a [mask](../reference/glossary.md#mask) written into the pixels, saturated stars and damage all count the
same. Hold the left button on the plot to read the level and the heights under the pointer.

On the Carina [master](../reference/glossary.md#master) stretched by Histogram Transformation with **Shadows** and
**Midtones** at 0.02, the peaks stand between 0.23 and 0.27, and the shares read 0.000% and 0.019%.

### Look at one channel

Choose **Red**, **Green** or **Blue** in **Channel** to draw that channel alone, and **RGB** to draw them together. The
clipping shares still count every channel. With **Absolute scales** off, the lines share one maximum, so you can
compare their heights. A weak channel is easier to see with it on, but the heights no longer compare.

### See a narrow peak

The samples of a [linear image](../reference/glossary.md#linear-image) gather in a few levels at the left. Choose a
finer **Plot resolution**, then zoom with the toolbar or with the wheel while you hold ++ctrl++, and drag with the
middle button to move along the plot. Start at **8-bit (256)**, and try **10-bit (1024)** or **12-bit (4096)** for
fine structure before **14-bit (16K)** or **16-bit (64K)**.

### Compare before and after a step

Leave the window open while you work: it counts the image again after each change. Keep the resolution, the channel
and **Absolute scales** the same for both readings. To learn what a save kept, save the image, open the file again
and open the histogram of its new view.

## Controls

The toolbar's zoom, grid, scales, areas and **Plot resolution** draw the plot as they do in every histogram of a
process window: see [Process Windows](process-windows.md). This window has no **Show histogram** and no transfer curve.
What is its own is below.

```controls
window: Histogram Analysis
controls:
  - name: Title
    kind: readout
    default: ""
    effect: Names the view the window counts, as `Histogram <RGB>`, and follows a rename. A rename changes nothing else.
    enabled: The window opens from **Image > Information > Histogram**, from **Histogram** on the view's toolbar, or with ++ctrl+h++ while the view has the focus. While it is open, that view's menu entry, button and shortcut are disabled. They come back when the window closes, and when the view's image loads or changes, which lets a second window open.
    tradeoff: The shortcut does nothing while the button sits in the toolbar's **More** menu. Use the menu then.
    persistence: Nothing of the window is kept. Each new window opens at **8-bit (256)** and **RGB**, with the grid, the clipping shares and the areas on, **Absolute scales** off and no zoom.
    note: The window stays open beside your work, and closes with its view. It writes nothing - no image, file, header card, history step or clipboard text, and the plot cannot be exported.
  - name: Plot
    kind: readout
    default: ""
    effect: Shows how many samples of each channel lie at each level, from 0 at the left to 1 at the right, as a line or a filled area.
    range: A sample is read as a fraction of full scale. A signed image is read from its lowest value to its highest, so in 16 bits -32768 is 0, 32767 is 1, and -1 and 0 both land in bin 127 of 256. A sample outside 0 to 1 is counted in the end bin nearest it.
    tradeoff: The height is a share of the tallest peak drawn, with no logarithmic scale. A plot with more bins than pixels across sums neighbouring bins, so what it shows depends on the window's width.
    scope: The whole image or frame, counted from the stored samples. The view's display mode, its **View Channel**, its zoom, a mask and a selection change nothing. A sequence is counted one frame at a time, never as a whole.
    persistence: Counted again, in the background, when the window opens, when **Plot resolution** changes and when the view's image loads or changes, a new frame of a sequence among them. A count asked for while another is on its way replaces it, and only the newest is drawn.
    note: The count reads every sample of every channel, and its time grows with the width, the height and the channel count, so change the resolution sparingly on a very large frame. In an 8- or 16-bit image, as a camera raw frame opens, each code the image can hold is placed in its bin once, rather than each sample. The counts take 8 bytes for each bin of each channel, are the same whatever the number of threads, and may use every thread the **Processing** page of the settings allows that is free. The window stays responsive meanwhile.
  - name: Clipping Low and Clipping High
    kind: readout
    default: ""
    effect: Give the share of samples at or below 0 and at or above 1, summed over every channel of the image, with three decimals.
    range: They are shares of the samples the image admits. A sample the image marks as invalid is left out. One that is not a number enters no bin and neither share, but counts among the samples they divide by.
    enabled: Shown while **Show clipping info** is on.
    scope: Every channel the image has, whatever **Channel** draws.
  - name: Draw histogram areas
    kind: toolbutton
    default: "On"
    effect: Draws each channel as a filled area. Off draws lines only, which show exact shapes and crossings more clearly.
    persistence: Until the window closes. **Reset** leaves it as it is.
  - name: Channel
    kind: combo
    default: RGB
    options: [RGB, Red, Green, Blue]
    effect: Chooses the lines the plot draws. **RGB** draws the three channels, and a color draws that channel alone, with the strip under the plot running from black to that color.
    enabled: For an image of three channels or more. Any other image shows a disabled **Grayscale**. An alpha channel is never drawn.
    tradeoff: One channel alone removes the overlap, and hides how it compares with the others.
    scope: The drawing only. The counts and the clipping shares are of every channel.
    persistence: Until the window closes. It returns to **RGB** when the view's image loads or changes, and with **Reset**.
  - name: Reset
    kind: button
    default: ""
    effect: Returns **Channel** to its first choice, **Plot resolution** to **8-bit (256)**, the zoom to fit, **Absolute scales** to off, and **Show clipping info** and **Show grid** to on.
    range: It leaves **Draw histogram areas** as it is.
    persistence: The image is counted again only when the resolution changes.
  - name: Close
    kind: button
    default: ""
    effect: Closes the window and enables the view's **Histogram** entries again.
    range: ++enter++ and the window's own close button do the same.
```

## When it goes wrong

**Histogram** is greyed in the menu or on the toolbar
:   **Cause.** A Histogram window of this view is already open, or the active view has no image.
:   **Fix.** Bring the open window forward or close it, or make a view with an image active.

One channel's line is much lower than the others
:   **Cause.** With **Absolute scales** off, every line is drawn against the tallest peak of all.
:   **Fix.** Choose that channel alone in **Channel**, or turn **Absolute scales** on and stop comparing heights.

The line looks like a comb, or too smooth
:   **Cause.** A fine resolution leaves empty bins between the levels the image holds, and a coarse one merges
    neighbouring levels.
:   **Fix.** Lower **Plot resolution** for a comb. Raise it, or zoom the plot, for a shape that is too smooth.

Clipping High stays above zero with one channel chosen
:   **Cause.** The shares count every channel of the image, whatever **Channel** draws.
:   **Fix.** Read each channel's **Min**, **Max** and counts in [Image Statistics](image-statistics.md).

**Reset** left the filled areas
:   **Cause.** **Reset** does not change **Draw histogram areas**.
:   **Fix.** Turn **Draw histogram areas** off yourself.

The plot changes while a sequence plays
:   **Cause.** The window counts the frame shown, again at every frame.
:   **Fix.** Pause the player on the frame to look at.

## Related

- [Image Statistics](image-statistics.md), for exact counts, levels, spreads and the noise of each channel.
- [Process Windows](process-windows.md), for the histogram toolbar and the plot's zoom, scales and resolution.
- [Image and Sequence Summary](image-summary.md), for the size, pixel type and acquisition facts of the image.
- [Histogram Transformation](../processes/stretching/histogram-transformation.md), to stretch the image with the
  histogram in view.
- [Saving And Exporting](saving-and-exporting.md), to save an image and open it again before you look at what a
  format kept.
- [Image Viewer](image-viewer.md), for the toolbar that opens the window and the display that does not change it.
