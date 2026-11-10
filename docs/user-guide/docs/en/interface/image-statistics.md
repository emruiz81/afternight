---
kind: interface
summary: >-
  Image Statistics measures each channel of an image, or of the frame a sequence shows: counts, level, spread,
  extremes and background noise, read from the stored samples and never from the display.
menu: Image > Information > Statistics
rewritten: true
---
# Image Statistics

Image Statistics measures the [active image](../reference/glossary.md#active-image), or the frame of a
[sequence](../reference/glossary.md#sequence) its view shows, one channel at a time. Use it to put numbers on the
level, the spread, the clipping and the noise of an image before and after a step. It reads each stored sample as a
[fraction of full scale](../reference/glossary.md#fraction-of-full-scale), never what the view displays, so the
[Autostretch](../reference/glossary.md#autostretch) display mode changes none of its figures. The window changes nothing either: not the samples, the header,
the [history](../reference/glossary.md#history), the [mask](../reference/glossary.md#mask) or a file.

```figure
owed: true
image: ../assets/images/interface/image-statistics.webp
alt: >-
  The Image Statistics window. Under the title bar, the heading Image Statistics and a line about per-channel
  statistics. A card titled Statistics Table holds a table with the columns Red, Green and Blue and a row for each
  figure, from Count (px), Count (%), Mean, Sigma and Median downward, each cell a number with seven decimals. Under the
  table, a box reading Normalized (0.0-1.0), an Unclipped check box, off, and a Refresh button. Close stands at the
  bottom right.
caption: >-
  Image Statistics on the linear Carina master at its defaults, Normalized (0.0-1.0) with Unclipped off.
pins:
  - {n: 1, x: 23.0, y: 3.0}
  - {n: 2, x: 11.0, y: 30.0}
  - {n: 3, x: 2.8, y: 85.0}
  - {n: 4, x: 47.0, y: 85.0}
  - {n: 5, x: 78.5, y: 85.0}
  - {n: 6, x: 80.5, y: 94.0}
```

## What you see

1. **The title.** It names the view the window measures, as `Statistics <RGB>`.
2. **The table.** One column for each channel and one row for each figure. A grayscale image has one column,
   **Gray**.
3. **The range box.** The units of the figures: fractions from 0 to 1, or codes of 8, 16 or 32 bits.
4. **Unclipped.** Whether the samples at either end of the range are counted.
5. **Refresh.** Measures the image again.
6. **Close.** Closes the window.

## What you can do

### Measure an image

1. Make the image the active view. For a sequence, pause the player on the frame you want to measure.
2. Choose **Image > Information > Statistics**, choose **Statistics** on the view's toolbar, or press
   ++ctrl+shift+t++ over the view. The window opens at once, and the table fills when the measurement is done.
3. Read **Median** and **MAD** for the background, which a few stars hardly move. **Min**, **Max** and the two counts
   show clipping, and **BG Noise** the noise between neighbouring pixels.

On the linear Carina [master](../reference/glossary.md#master), every channel's **Median** reads 0.02766 and its
**MAD** 0.00175.

### Count the samples at the ends

With **Unclipped** off, a sample at or below 0 or at or above 1 is left out. An integer image loses its lowest and its
highest code: an unsigned 16-bit image loses 0 and 65535 and keeps 1 and 65534. Turn **Unclipped** on to count every sample that
is a finite number. The window measures again, and **Count (px)** rises by the samples that were left out. On the
Carina master, green's **Min** then reads -0.1600173 and its **Max** 1.8526056.

Leave it off when a black border or saturated stars would decide the extremes. Turn it on to see how far a
floating-point image runs past 0 and 1.

### Read the figures in codes

Choose **8-bit (0-255)**, **16-bit (0-65K)** or **32-bit (0-4000M)** in the range box. The table is shown again in
those units at once, and nothing is measured again. At **16-bit (0-65K)**, the Carina master's red **Median** reads
1812.496. Keep **Normalized (0.0-1.0)** to compare images of different [pixel types](../reference/glossary.md#pixel-type).

### Compare before and after a step

Leave the window open while you work. It measures again after each change of the image, an Apply, an Undo or a new
frame, and shows the newest measurement. Press **Refresh** to measure again yourself. To learn what a save kept, save
the image, open the file again and measure its new view.

## Controls

### The window

```controls
window: Image Statistics
controls:
  - name: Title
    kind: readout
    default: ""
    effect: Names the view the window measures, as `Statistics <RGB>`, and follows a rename. A rename changes nothing else.
    enabled: The window opens from **Image > Information > Statistics**, from **Statistics** on the view's toolbar, or with ++ctrl+shift+t++ while the view has the focus. While it is open, that view's menu entry, button and shortcut are disabled. They come back when the window closes, and when the view's image loads or changes, which lets a second window open.
    tradeoff: The shortcut does nothing while the button sits in the toolbar's **More** menu. Use the menu then.
    persistence: Nothing of the window is kept. Each new window opens at **Normalized (0.0-1.0)** with **Unclipped** off.
    note: The window stays open beside your work, and closes with its view. It writes nothing - no image, file, header card, history step or clipboard text.
  - name: Numeric range
    kind: combo
    default: Normalized (0.0-1.0)
    options: [Normalized (0.0-1.0), 8-bit (0-255), 16-bit (0-65K), 32-bit (0-4000M)]
    effect: Shows the figures as fractions of full scale, or multiplied by 255, 65535 or 4294967295. **Variance** and **BWMV** are multiplied by the square, so a variance stays the square of the deviation beside it.
    range: Seven decimals in **Normalized (0.0-1.0)**, three in the others. No figure is held to the range, so the mean of a floating-point image above 1 shows above full scale.
    enabled: Always. The box has no label of its own.
    tradeoff: It only shows the last measurement in other units. It converts nothing and measures nothing again.
    scope: Every column and every figure. The two counts do not change.
  - name: Unclipped
    kind: check
    default: "Off"
    effect: Off counts only the samples strictly inside the range, above 0 and below 1. On counts every sample that is a finite number, the two ends included, and a floating-point sample below 0 or above 1.
    range: For an integer image the ends are its lowest and highest code, as 0 and 65535 in 16 bits, or -128 and 127 in signed 8 bits.
    tradeoff: On, a black border or saturated stars count, and can decide **Min**, **Max** and the spread. Neither state rejects outliers by their deviation or repairs a clipped sample.
    scope: Every channel. A sample that is not a finite number, or that the image marks as invalid, is left out either way.
    persistence: Each change measures the image again, in the background.
  - name: Refresh
    kind: button
    default: ""
    effect: Measures the image again, with **Unclipped** as it is.
    enabled: Always.
    tradeoff: A measurement reads every sample of every channel, and its time grows with the width, the height and the channel count. The median, **MAD** and **BWMV** rank the samples exactly - an 8- or 16-bit image, as a camera raw frame opens, from one read of each channel, a floating-point image from several. The work runs in the background and may use every thread the **Processing** page of the settings allows that is free, so the window stays responsive.
    note: The window also measures by itself when it opens, when **Unclipped** changes and when the view's image loads or changes, a new frame of a sequence among them. A measurement asked for while another is on its way replaces it, and the table shows only the newest one.
  - name: Close
    kind: button
    default: ""
    effect: Closes the window and enables the view's **Statistics** entries again.
    range: ++enter++ and the window's own close button do the same.
```

### The table

```controls
window: The table
controls:
  - name: Columns
    kind: readout
    default: ""
    effect: One column for each channel the image stores. **Gray** for one channel, then **Red**, **Green**, **Blue** and **Alpha**, and **Channel 5** and on for more.
    scope: Each channel is measured alone, over the whole image or frame. The view's **View Channel**, its display mode, its zoom, a mask and a selection change nothing here.
    note: A row can be selected, but the table has no copy and no export. It has no sum of squares either; the `ssqr` and `asqr` functions of Pixel Math give one.
  - name: Count (px)
    kind: readout
    default: ""
    effect: How many samples of the channel the figures are over.
    range: From 0 to the width times the height.
    tradeoff: A lower count says that samples were left out, at an end of the range, not finite or marked invalid. It does not say why, nor that the image was damaged.
    note: A channel that keeps no sample shows 0 here and in every figure.
  - name: Count (%)
    kind: readout
    default: ""
    effect: The count as a share of the image's pixels, with seven decimals in every range.
    note: It compares what was left out across images of different sizes.
  - name: Mean
    kind: readout
    default: ""
    effect: The arithmetic mean of the counted samples, their sum divided by **Count (px)**.
    tradeoff: Stars, gradients, clipping and outliers pull it.
  - name: Sigma
    kind: readout
    default: ""
    effect: The population standard deviation, the same number as **StdDev**. Both rows keep their places.
  - name: Median
    kind: readout
    default: ""
    effect: The middle sample, ranked exactly, or the mean of the two middle samples of an even count.
    tradeoff: A few stars or hot pixels hardly move it, but over a whole image it is not the background alone.
  - name: Min and Max
    kind: readout
    default: ""
    effect: The lowest and the highest counted sample.
    tradeoff: One pixel decides each. Read them with the counts, and with the clipping shares of Histogram Analysis.
    note: With **Unclipped** off they lie inside the range, so on a 16-bit image 0 and 65535 never appear.
  - name: StdDev
    kind: readout
    default: ""
    effect: The population standard deviation about the mean, the square root of **Variance**.
    tradeoff: Gradients, stars and structure raise it as well as noise.
  - name: AvgDev
    kind: readout
    default: ""
    effect: The mean distance of the samples from the mean.
    tradeoff: Outliers move it less than **StdDev** and more than **MAD**.
  - name: BWMV
    kind: readout
    default: ""
    effect: The biweight midvariance about the median, with a tuning constant of nine times **MAD**. It is 0 when **MAD** is 0.
    range: In squared units, multiplied by the square of the full scale outside **Normalized (0.0-1.0)**.
    tradeoff: Outliers hardly move it.
  - name: Variance
    kind: readout
    default: ""
    effect: The mean of the squared distances from the mean, **StdDev** squared apart from rounding.
    range: In squared units, as **BWMV**.
  - name: MAD
    kind: readout
    default: ""
    effect: The median of the distances of the samples from **Median**, ranked exactly.
    note: It is not multiplied by 1.4826. Multiply it by that to compare it with a standard deviation.
  - name: BG Noise
    kind: readout
    default: ""
    effect: The noise between neighbouring pixels. In each row, every counted sample is subtracted from the one before it. The standard deviation of those differences is taken after up to three passes that drop a difference five deviations or more from the row's mean. The median over the rows, divided by the square root of 2, is the figure.
    range: 0 when no row holds two differences. A sample left out is skipped, and the next counted sample is compared with the one before it.
    tradeoff: It follows the rows only. Structure, gradients, stars and noise that runs along the rows all raise it, so it is no full noise model and no sensor's figure. Compare it between images of the same kind.
```

## When it goes wrong

**Statistics** is greyed in the menu or on the toolbar
:   **Cause.** A Statistics window of this view is already open, or the active view has no image.
:   **Fix.** Bring the open window forward or close it, or make a view with an image active.

The table stays empty
:   **Cause.** The measurement failed, and a failed measurement leaves the table empty.
:   **Fix.** Press **Refresh**. For a floating-point image, turn **Unclipped** off, which leaves out every sample that
    is not a finite number.

**Count (%)** is below 100
:   **Cause.** Samples were left out: at an end of the range with **Unclipped** off, not finite, or marked invalid by
    the image, such as the border a registration leaves.
:   **Fix.** Turn **Unclipped** on to count the ends. Samples that are not finite or are marked invalid are never
    counted.

**Mean** and **Median** are far apart
:   **Cause.** Stars, gradients or clipped samples pull the mean, and hardly move the median.
:   **Fix.** Compare **Median** and **MAD** between images, and look at the shape in
    [Histogram Analysis](image-histogram.md).

**BG Noise** is higher than the camera's noise
:   **Cause.** Gradients, stars, structure and noise that runs along the rows all add to the differences between
    neighbouring pixels.
:   **Fix.** Compare it only between images of the same kind, such as two frames of one night.

The figures change while a sequence plays
:   **Cause.** The window measures the frame shown, again at every frame.
:   **Fix.** Pause the player on the frame to measure.

## Related

- [Histogram Analysis](image-histogram.md), for where the counted samples lie and how many sit at the ends.
- [Image and Sequence Summary](image-summary.md), for the size, pixel type and acquisition facts of the image.
- [Image Viewer](image-viewer.md), for the toolbar that opens the window and the display that does not change it.
- [Saving And Exporting](saving-and-exporting.md), to save an image and open it again before you measure what was
  kept.
- [Histogram Transformation](../processes/stretching/histogram-transformation.md), to
  [stretch](../reference/glossary.md#stretch) the image once every measurement that needs linear data is done.
- [Pixel Math](../processes/pixel-math/pixel-math.md), for figures the table does not have, such as the sum of squares.
