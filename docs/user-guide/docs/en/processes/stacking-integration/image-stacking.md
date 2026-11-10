---
kind: process
stage: stack
menu: Process > Preprocess > Image Stacking
summary: >-
  Combines a list of frames into one floating-point master for each group of them, with optional normalization,
  weighting and rejection: the calibration masters, or the stack of calibrated, registered lights.
data_in: linear
data_out: linear
destructive: false
preview: none
sequences: false
masks: none
external: none
glance:
  enabled: >-
    Always. The window opens without an image and works on the files you list in it.
  inputs: >-
    Still image files, grouped by filter and, if you ask, by night and exposure. Within a group every frame has the
    first frame's size, pixel type, channel count and filter array. No open image, sequence or mask is used.
  outputs: >-
    One 32-bit or 64-bit floating-point master for each group, in the folder you choose, with optional rejection maps
    beside it, each of which can open in a view. The source files are not changed.
  state: >-
    Raw bias or dark frames, flat frames calibrated by the bias, or calibrated and registered linear lights.
derived: true
rewritten: true
---
# Image Stacking

Image Stacking combines a list of files into one [master](../../reference/glossary.md#master) for each group of them:
a bias, dark or flat master for calibration, or the stack of [light frames](../../reference/glossary.md#light-frame)
that are already calibrated and registered. It can bring the frames to one level, weigh them, and leave out the
samples that disagree with the rest by [rejection](../../reference/glossary.md#rejection) before it averages, sums or
takes the median of what is left. It neither calibrates nor registers.

**Stack Images** writes one floating-point file for each group into the folder you choose, with optional rejection
maps beside it, and can open each in a view. The source files are not changed.

```figure
image: ../../assets/images/processes/stacking-integration/image-stacking.webp
alt: >-
  The Image Stacking window. Under its header, a warning strip that begins Stacking completed with warnings. On the
  left, the preview of the first bias of NGC 253, black in the Linear display mode, with its statistics: Min 1090,
  Max 3023, Median 1896. On the right, the Stacking Settings card with Method Average, Normalization None, Weighting None (Equal), a disabled Scale
  Estimator at IKSS, Comet Mode Stars and the folded Rejection Settings, then the Output card with the Name Template
  master_bias.fit, the path of the masters folder in Directory, Open after stacking unchecked and the Stack Images
  button. Across the bottom, the Frames table, showing the first two of the 30 biases, with Current Image 1 and Total
  Images 30, the Frame Groups
  section with Filter checked, and the folded Advanced section. Reset and Close at the bottom.
caption: >-
  Image Stacking after stacking the 30 biases of NGC 253 into master_bias.fit, with every other value as the window
  opens.
pins:
  - {n: 2, x: 8.0, y: 64.1}
  - {n: 3, x: 82.1, y: 67.5}
  - {n: 4, x: 30.8, y: 86.2}
  - {n: 5, x: 70.5, y: 13.6}
  - {n: 6, x: 73.5, y: 25.9}
  - {n: 8, x: 82.1, y: 34.5}
  - {n: 10, x: 88.9, y: 40.4}
  - {n: 11, x: 79.1, y: 50.5}
```

## Before you start

- Calibrate and register the lights first. The window stacks each pixel where it lies in the files and reads no
  alignment, so frames that do not line up give a soft or doubled master without a message. Biases, darks and flats
  need no registration.
- List frames that belong in one master. Within a group every frame needs the first frame's size,
  [pixel type](../../reference/glossary.md#pixel-type), channel count and [CFA](../../reference/glossary.md#cfa)
  layout, or the group is refused. Exposure, gain, offset and temperature are shown, not compared.
- Stack linear frames. Normalization and rejection read the frames' levels and spread as the sensor recorded them.
- Keep the masters of a color camera as mosaics: Image Calibration needs a bias, dark or flat master with the lights'
  [Bayer pattern](../../reference/glossary.md#bayer-pattern). Leave **Debayer CFA** off for them.
- Choose a folder with room for every master and map. A file of the same name there is replaced without a question.
- **FWHM**, **SNR** and **Stars** weighting, and **Comet** mode, read the `registration_metrics.db` that Image
  Registration writes in its output folder. Choose that folder as the output.

## Steps

1. **Open the window.** Choose **Process > Preprocess > Image Stacking**. It needs no open image, and it opens with
   the values and the list it had when it last closed.
2. **List the frames.** Choose **Add Files** and select the frames, or **Add Folder** to take a folder's files by
   frame type and filter. The rows appear in date order, and the selected file shows in the preview.
3. **Check the list.** Compare **Size**, **Exposure**, **CFA** and **Depth** down the rows, and remove every file
   that does not belong. The order of the rows changes nothing in the stack.
4. **Check the grouping.** Under **Frame Groups**, leave **Filter** on to make one master for each filter. Turn on
   **Session** or **Exposure** only to make a master for each night or each exposure.
5. **Choose the combination.** Keep **Method** at **Average**, which takes a rejection and weights. Choose
   **Median** for a few frames, and the other methods only for their special uses.
6. **Set the rejection.** Open **Rejection Settings**. Keep **Winsorized Sigma Clipping** as it is for three frames
   or more, and lower **Sigma High** only when trails or [hot pixels](../../reference/glossary.md#hot-pixel)
   remain.
7. **Bring the frames to one level.** Leave **Normalization** at **None** for biases and darks. Choose
   **Multiplicative** for flats of different brightness, **Additive With Scaling** for lights, and **Weighting** for
   lights of uneven quality.
8. **Name the output.** Type the file name in **Name Template**, with `$filter` where each group's filter goes. Its
   extension chooses the format.
9. **Choose the folder.** Choose **Browse** and pick a folder. Its path appears in **Directory**.
10. **Choose what the run keeps.** Turn **Open after stacking** off to write the files only. Check **64-bit** and
    **Sum exposure times** only when you need them.
11. **Stack.** Choose **Stack Images**. A progress window names each group as it is stacked, and the window stays
    usable meanwhile.
12. **Check the result.** The progress window closes, and the Session Console lists `Image Stacking: saved` with the
    path of each file. Open the master at 100 % and compare its level and noise with a single frame's.

## Controls

What every process window shares, such as the message strip and the window's size, is in
[Process Windows](../../interface/process-windows.md). This window keeps its values and its list of files for the
next window. Its preview is a still preview, whose toolbar and mouse work as in the
[Image Viewer](../../interface/image-viewer.md).

### Stacking Settings

```controls
window: Stacking Settings
controls:
  - name: Method
    kind: combo
    default: Average
    options: [Average, Sum, Maximum, Minimum, Median, Auto-Adaptive Weighted Average]
    effect: Chooses how the samples of a pixel become its value. **Average** takes their mean, after the rejection and with the weights. **Sum** adds them. **Maximum** and **Minimum** keep the largest or the smallest. **Median** takes their exact median. **Auto-Adaptive Weighted Average** weighs each sample by its distance from the running mean, in five passes.
    range: Only **Average** takes a rejection and weights. Any other method disables **Rejection Settings** and ignores **Weighting**. **Sum** divides every sum by the largest, so the brightest pixel is 1, and writes the sums as they are, with a warning, when none is positive.
    tradeoff: "**Median**, **Auto-Adaptive Weighted Average** and every rejection hold the samples of every frame for a band of rows at once, which costs memory and time. **Maximum** and **Minimum** keep the extremes, trails and hot pixels among them. **Sum** keeps the ratios between pixels, not the totals: the integration time is in **Sum exposure times**."
    scope: Every channel of every group.
    persistence: Kept for the next window. **Reset** returns it to **Average**.
    note: "The auto-adaptive mean is made for samples between 0 and 1. The tooltip names each method's use, such as \"Median: robust to outliers but lower SNR.\""
  - name: Normalization
    kind: combo
    default: None
    options: [None, Additive, Multiplicative, Additive With Scaling, Multiplicative With Scaling, Local]
    effect: Brings each frame to the group's first frame before the samples are combined. **None** combines them as they are. **Additive** adds an offset so that the medians agree, **Multiplicative** multiplies so that they agree, and the two **With Scaling** modes first match the spread that **Scale Estimator** measures. **Local** fits a smooth surface to each frame instead, set in **Configure Local Normalization...**.
    range: The first frame is the first of the group in the order of filter, session, exposure and file path, whatever the order of the list. A multiplicative mode is refused when that frame's median is zero.
    tradeoff: Each mode measures every frame once more, and **Local** fits a grid besides. A mode that does not suit the frames changes the background and the contrast between them.
    scope: Each channel of each frame, before the rejection judges it. Exact zeros and samples that are not numbers are left out of the measures.
    persistence: Kept for the next window. **Reset** returns it to **None**.
  - name: Weighting
    kind: combo
    default: None (Equal)
    options: [None (Equal), FWHM, Noise, SNR, Stars, Exposure]
    effect: Gives each frame a weight in the average. **None (Equal)** weighs every frame alike. **FWHM**, **SNR** and **Stars** read the figures Image Registration recorded for each frame. **Noise** measures each frame's background noise when the stack starts and weighs it by one over its square. **Exposure** weighs each frame by the exposure its header states.
    range: Only **Average** reads it, with or without a rejection, and **Comet** mode stacks with equal weights. When no frame has the figure, the stack uses equal weights and says why. When only some frames have it, the stack is refused.
    tradeoff: "**Noise** reads every frame once more before the stack. **FWHM**, **SNR** and **Stars** find their figures only in the `registration_metrics.db` of **Directory**."
    scope: One weight for each frame, the same in every channel.
    persistence: Kept for the next window. **Reset** returns it to **None (Equal)**.
    note: "**FWHM**, **Noise** and **Stars** are the weights Image Integration offers as Weighted FWHM, Noise and Number of stars. **SNR** and **Exposure** are this window's own. The tooltip says of **Noise** \"weight by each frame's measured background noise.\""
  - name: Scale Estimator
    kind: combo
    default: IKSS
    options: [IKSS, MAD]
    effect: Chooses how the two **With Scaling** modes measure a frame's spread. **IKSS** takes a robust spread of the samples within six deviations of the median. **MAD** takes the [median absolute deviation](../../reference/glossary.md#median-absolute-deviation).
    enabled: With **Additive With Scaling** or **Multiplicative With Scaling** only, since no other mode reads it.
    tradeoff: "**IKSS** resists stars and defects better and takes longer. **MAD** is faster."
    persistence: Kept for the next window. **Reset** returns it to **IKSS**.
  - name: Comet Mode
    kind: combo
    default: Stars
    options: [Stars, Comet]
    effect: "**Stars** stacks the listed files. **Comet** stacks, for each listed file, the copy aligned on the comet that Image Registration's comet registration wrote, as `registration_metrics.db` in **Directory** names it."
    enabled: Disabled while a run works.
    tradeoff: "**Comet** stacks with equal weights. It refuses the stack when the database has no active comet run that names a copy for every frame, rather than stack the files themselves."
    persistence: Kept for the next window. **Reset** returns it to **Stars**.
  - name: Weighting warning
    kind: readout
    default: ""
    effect: Says before you stack that the weighting chosen cannot weigh the frames, such as "Registration metrics were not found in this output directory, so FWHM weighting is falling back to equal weights.", or which frame lacks its figure.
    enabled: Shown for **FWHM**, **SNR** and **Stars** once a folder is chosen, and for **Exposure** at once, when a frame lacks the figure. **Noise** says so only when the run ends.
  - name: Configure Local Normalization...
    kind: button
    default: ""
    effect: Opens **Local Normalization Settings**, where the grid and estimators of the local fit are set.
    enabled: Shown only with **Normalization** at **Local**, under the note "Local normalization works best with aligned/registered frames.".
    tradeoff: A finer grid follows more of each frame's background and can print its noise into the stack.
    persistence: The values you accept are kept for the next window. **Reset** returns them to the **Standard (Grid 4)** preset.
  - name: Rejection Method
    kind: combo
    default: Winsorized Sigma Clipping
    options: [None, Sigma Clipping, MAD Clipping, Median Sigma, Winsorized Sigma Clipping, Percentile, GESDT, Linear Fit]
    effect: Chooses which samples **Average** leaves out. The four sigma methods leave out the samples too far from the median in deviations, **Linear Fit** those too far from a line through the sorted samples, **Percentile** those too far from the median in fractions of it, and **GESDT** the extremes a statistical test singles out. **None** keeps every sample.
    range: Every method but **None** needs three frames or more in a group, and a smaller group is refused when the stack starts. Choosing a method sets the two numbers below to its own defaults.
    enabled: Under **Rejection Settings**, which opens folded, with **Method** at **Average** only.
    tradeoff: Every method holds all the samples of a band of rows, and some iterate. Thresholds set too low leave out real signal, too high leave trails and hot pixels.
    scope: Each pixel of each channel, after the normalization.
    persistence: Kept for the next window. **Reset** returns it to **Winsorized Sigma Clipping**.
  - name: Sigma Low
    kind: spinbox
    default: "3.000"
    widget: {min: 0.001, max: 1000, step: 0.1, decimals: 3}
    effect: Sets how far below the centre a sample may lie and stay. Lower leaves out more dark samples, higher fewer.
    range: "Its label and meaning follow **Rejection Method**, in deviations for the sigma methods. It reads **Linear Low**, in residuals of the line, for **Linear Fit**, set to 5 when you choose it. It reads **Low Fraction of Median** for **Percentile**, set to 0.2, which means 20 % of the median and not a percentile. It reads **ESD Outliers** for **GESDT**, the largest share of samples tested, set to 0.30 and kept below 1."
    enabled: With a rejection other than **None**.
    tradeoff: Too low removes dark real signal and leaves too few samples. Too high leaves dark defects, such as cold pixels.
    persistence: Kept for the next window. **Reset** returns it to its default.
  - name: Sigma High
    kind: spinbox
    default: "3.000"
    widget: {min: 0.001, max: 1000, step: 0.1, decimals: 3}
    effect: Sets how far above the centre a sample may lie and stay. Lower leaves out more bright samples, higher fewer.
    range: "**Linear High** for **Linear Fit**, set to 4. **High Fraction of Median** for **Percentile**, set to 0.1. **ESD Significance** for **GESDT**, the test's significance, set to 0.05 and kept below 1."
    enabled: With a rejection other than **None**.
    tradeoff: Too low removes star cores and bright signal. Too high leaves satellite trails, cosmic rays and hot pixels.
    persistence: Kept for the next window. **Reset** returns it to its default.
  - name: Save rejection maps
    kind: check
    default: "Off"
    effect: Writes, beside each master, a map of the share of the group's frames left out at each pixel and channel, from 0 to 1.
    enabled: With **Method** at **Average** and a rejection other than **None**.
    tradeoff: One or two more files of the master's size, in 32-bit floating point, and more memory while the run works.
    scope: Every group. A map shows how often samples were left out, not that a frame is bad, so compare it with the frames.
    persistence: Kept for the next window. **Reset** turns it off.
  - name: Merge rejection maps
    kind: check
    default: "On"
    effect: On writes one map, `<master>_rej_l+h`, of the samples left out below and above. Off writes two, `_rej_l` and `_rej_h`, which keep the side.
    enabled: With **Save rejection maps** checked.
    persistence: Kept for the next window. **Reset** turns it on.
  - name: Rejection maps
    kind: readout
    default: Rejection maps are disabled.
    effect: Says where the first group's maps go, as "Rejection maps:" and their path, with `_rej_[l|h]` when they are not merged.
    enabled: With **Save rejection maps** checked, it reads "Choose an output directory to save rejection maps." until **Directory** holds a folder, and "Rejection maps will be written next to the stacked outputs." until the list holds a file.
```

### Output

```controls
window: Output
controls:
  - name: Name Template
    kind: field
    default: stacked_$filter.fit
    effect: Names each group's master. `$filter` becomes the group's filter, `$session` its date, as `2023-10-10`, `$exposure` its exposure, as `180s`, and `$group` the group's name. With frames of the filter L, the default gives `stacked_L.fit`.
    range: "`.fit`, `.fits` or `.fts` writes FITS, `.tif` or `.tiff` TIFF, `.xisf` XISF, `.png` PNG and `.jpg` or `.jpeg` JPEG. Any other extension, or none, becomes `.fit`. Spaces and characters a file name cannot hold become underscores, and doubled, leading and trailing underscores are removed. A group without a filter puts `group` in place of `$filter`, so the default gives `stacked_group.fit`, whatever the tooltip says."
    enabled: Empty, it holds **Stack Images** back.
    tradeoff: A PNG or JPEG file holds no floating-point samples and no mosaic, so the run stacks and then fails to write. A file of the same name in the folder is replaced without a question.
    scope: Every group. When two groups would get one name, whatever its case, the second gets its group's name added, then a number.
    persistence: Kept for the next window. **Reset** returns it to its default.
  - name: Directory
    kind: readout
    default: ""
    effect: Shows the folder the masters and maps are written to, which is also where **FWHM**, **SNR**, **Stars** and **Comet** read `registration_metrics.db`. It cannot be typed in.
    range: A missing folder is created when the run writes.
    persistence: Kept for the next window. **Reset** leaves it as it is.
  - name: Browse
    kind: button
    default: ""
    effect: Opens a folder dialog to choose **Directory**.
    range: It starts in the folder shown, else in the folder of the first listed file.
    tradeoff: Choosing another folder can change what the weighting finds, and the warning above says so.
  - name: 64-bit
    kind: check
    default: "Off"
    effect: Writes each master in 64-bit floating point. Off writes 32-bit, whatever the frames' own pixel type.
    tradeoff: Doubles the master on disk and in memory. It cannot add precision the frames did not have. Rejection maps stay 32-bit.
    persistence: Kept for the next window. **Reset** turns it off.
  - name: Open after stacking
    kind: check
    default: "On"
    effect: Opens each file the run writes in a view of its own, named after the file and fitted to it, the master first and then its maps. Off writes the files only.
    tradeoff: Many groups and maps open many views, which take memory. It changes nothing on disk.
    persistence: Kept for the next window. **Reset** turns it on.
    note: The history of a master's view starts with a step such as "Stacked image created from 30 frame(s) using Average".
  - name: Sum exposure times
    kind: check
    default: "Off"
    effect: "On writes the sum of the frames' exposures as the master's `EXPTIME`, a frame that states none counted at the first frame's. Off writes the first frame's exposure."
    range: No `EXPTIME` is written when the frames state no exposure.
    tradeoff: It changes the header only, not a sample.
    persistence: Kept for the next window. **Reset** turns it off.
  - name: Stack Images
    kind: button
    default: ""
    effect: Stacks every group in turn, in the background, and writes its maps and then its master into **Directory**.
    range: It takes the list, the groups and the values as they are when you press it, and a change made during the run does not reach it. Pressed while headers are still being read, or while the weighting warning is being worked out, it does nothing.
    enabled: Once the list holds a file, **Directory** a folder and **Name Template** a name. Disabled while a run works, with **Add Files**, **Add Folder**, **Comet Mode** and **Reset**. The other controls stay enabled, and the list cannot be changed.
    tradeoff: Its time grows with the pixels, the frames and the channels. A group that fails stops the groups after it, and the files of the groups before it stay.
    note: A run that ends with something to say shows "Stacking completed with warnings." in the strip under the header, each warning after its group's name, eight at most. A run that ends well says nothing more.
```

### Frames

```controls
window: Frames
controls:
  - name: Frames
    kind: readout
    default: ""
    effect: Lists the files to stack, one row each, under File, Date, Size, Filter, ISO/Gain, Offset, Exposure, CFA and Depth, read from each file's header. Choosing a row shows its file in the preview.
    range: Filter, ISO/Gain, Offset and Exposure read `-` when the header states none, and Exposure is in seconds. CFA reads Yes for a mosaic. Depth reads 8 bit, 16 bit, 32 bit int, 32 bit float or 64 bit float. Date is the creation time the header states, else the start of the exposure, else the time the file last changed, in UTC.
    enabled: The list is neither reordered nor changed while a run works.
    tradeoff: The order of the rows decides nothing. The groups sort their frames by filter, session, exposure and file path, and the first of each group is the one the others are normalized to.
    persistence: The list opens ordered by Date, oldest first. Clicking a column's title orders by it, newest or largest first, and again the other way. Size orders nothing. The list and its order are kept for the next window, and its files are read again when it opens.
    note: The full path of a file is its tooltip. Right-click a row for the row menu.
  - name: Add Files
    kind: button
    default: ""
    effect: Opens a file dialog for one or several still image files, and adds them to the list while their headers are read in the background.
    range: FITS, XISF, TIFF, PNG, JPEG and camera RAW files. A SER file, a video or another sequence is not offered, so extract its frames to still files first. A file already listed is passed over. A file that cannot be read is left out, and the others are added.
    enabled: Disabled while a run works.
    tradeoff: Many headers take time to read, and the window stays usable meanwhile. When not every file is added, the window says how many it added, passed over and could not read, with the first reason.
    persistence: The dialog starts in the folder you last added from.
  - name: Add Folder
    kind: button
    default: ""
    effect: Opens a folder dialog, then **Add Folder Options**, and adds the folder's image files that pass the options, as **Add Files** adds them.
    range: The same file types as **Add Files**. The folder is listed in the background.
    enabled: Disabled while a run works.
    tradeoff: A folder with its subfolders can bring in other frame types or another night. Check the list before you stack.
    persistence: The folder dialog starts in the folder you last added from. The options start afresh each time.
  - name: Remove Selected
    kind: button
    default: ""
    effect: Removes the selected rows from the list. The files stay on disk.
    enabled: While the list holds a file. It does nothing while a run works.
  - name: Move frame up
    kind: button
    default: ""
    effect: Moves the selected row one place up.
    enabled: When the selected row can move up. It does nothing while a run works.
    tradeoff: It changes the list only, not the stack or which frame comes first in a group.
  - name: Move frame down
    kind: button
    default: ""
    effect: Moves the selected row one place down.
    enabled: When the selected row can move down. It does nothing while a run works.
  - name: Current Image
    kind: readout
    default: "0"
    effect: Shows the number of the selected row, counted from 1, or 0 when none is selected.
  - name: Total Images
    kind: readout
    default: "0"
    effect: Shows how many files the list holds.
  - name: Clear All
    kind: button
    default: ""
    effect: Empties the list and the preview, and stops the files still being read, none of which is added afterwards.
    enabled: While the list holds a file. It does nothing while a run works.
    tradeoff: The files stay on disk.
```

### Frame Groups

```controls
window: Frame Groups
controls:
  - name: Filter
    kind: check
    default: "On"
    effect: Makes a group, and a master, for each filter the frames' headers state. Off puts every filter together.
    range: A frame whose filter is empty, `-` or `N/A` belongs to the group `Broadband`, which is stacked like any other.
    tradeoff: A header that spells one filter two ways makes two groups.
    scope: The groups are made again at once.
    persistence: Kept for the next window. **Reset** turns it on.
  - name: Session
    kind: check
    default: "Off"
    effect: Makes a group for each night, by the date in UTC on which each frame was taken.
    range: A frame whose header states no time takes the date its file last changed, else the name of its folder.
    tradeoff: Frames of one night that cross midnight in UTC, or whose dates are wrong, split into two groups.
    persistence: Kept for the next window. **Reset** turns it off.
  - name: Exposure
    kind: check
    default: "Off"
    effect: Makes a group for each exposure, with frames within **Tolerance** of the group's mean exposure together.
    tradeoff: Many exposures make many small groups, and a group of fewer than three frames cannot take a rejection.
    persistence: Kept for the next window. **Reset** turns it off.
  - name: Tolerance
    kind: spinbox
    default: "5.0"
    widget: {min: 0, max: 100, step: 1, decimals: 1}
    effect: Sets how far, in percent of the longer of the two, a frame's exposure may differ from the group's mean exposure and join it. Higher merges more exposures, lower splits them.
    range: A frame that states no exposure joins only a group of frames that state none.
    enabled: With **Exposure** checked.
    tradeoff: Too high mixes exposures that differ in signal, too low splits one exposure that varies slightly.
    persistence: Kept for the next window. **Reset** returns it to its default.
  - name: Frame Groups
    kind: readout
    default: ""
    effect: Lists the stacks the run will make, one row each, under Group, Frames, Filter, Session, Total Exposure and Output Name. It cannot be edited.
    range: The group's name joins its filter, date and exposure, or reads All Frames with every key off. Filter and Session read Mixed when the group holds several. Total Exposure is the sum of the frames' exposures, in seconds, or `-`.
    enabled: The window gives the table no height of its own, so its rows do not show. The run stacks every group all the same.
    scope: It is made again when the list, a key, **Tolerance** or **Name Template** changes.
```

### Advanced

```controls
window: Advanced
controls:
  - name: Limit Memory Usage
    kind: check
    default: "On"
    effect: Holds the stack's working memory under **Max Memory**. Off leaves only the application's own memory limit.
    enabled: Under **Advanced**, which opens folded.
    tradeoff: A stack that does not fit is read in bands of rows, each file once for every band, and the run then ends with a warning that says so. Memory outside the stack, such as the files being decoded and the views, is not counted.
    persistence: Kept for the next window. **Reset** turns it on.
  - name: Max Memory
    kind: spinbox
    default: "1024"
    widget: {min: 256, max: 8184, step: 1}
    effect: Sets the most memory the stack may hold, in megabytes. Higher reads each file fewer times, lower more.
    range: A limit too small for the master and one frame is refused, with the sizes it needed.
    enabled: With **Limit Memory Usage** checked.
    tradeoff: A rejection or a median holds every frame's samples for its band of rows, about 0.67 MB a row for 30 frames 5544 pixels wide.
    persistence: Kept for the next window. **Reset** returns it to its default.
  - name: Debayer CFA
    kind: check
    default: "Off"
    effect: Debayers a master that is still a mosaic after it is stacked, so it is written in color. It does not debayer the frames before the stack.
    range: It changes nothing for frames that are not mosaics.
    tradeoff: A debayered master no longer calibrates a mosaic, and frames registered without regard to their filter array make a master no debayer repairs.
    persistence: Kept for the next window. **Reset** turns it off.
  - name: Pattern
    kind: combo
    default: Auto
    options: [Auto, RGGB, BGGR, GBRG, GRBG]
    effect: Chooses the filter array the debayer reads. **Auto** takes the one the frames state.
    enabled: With **Debayer CFA** checked.
    tradeoff: A wrong pattern gives wrong colors. A two-by-two pattern over an X-Trans mosaic is refused before anything is written.
    persistence: Kept for the next window. **Reset** returns it to **Auto**.
  - name: Method
    kind: combo
    default: RCD
    options: [Bilinear, VNG4, AHD, AMaZE, LMMSE, RCD]
    effect: Chooses the debayer method, as [Debayer](../calibration/debayer.md) describes them.
    enabled: With **Debayer CFA** checked.
    tradeoff: "**AMaZE** and **LMMSE** take the longest. No method brings back detail the stack lost."
    persistence: Kept for the next window. A method the window does not offer opens as **RCD**. **Reset** returns it to **RCD**.
    range: DCB, HPHD and IGV are not offered here.
    note: A color pixel estimated from a pixel no frame reached is marked invalid.
```

### Reset and Close

```controls
window: Image Stacking
controls:
  - name: Close
    kind: button
    default: ""
    effect: Closes the window and keeps its values and its list for the next window.
    range: ++esc++ and the window's own close button do the same.
    enabled: Also while a run works. Closing then stops the run and the files being read, and the window closes at once.
    tradeoff: The files of the groups already stacked stay. The group in progress leaves no file.
  - name: Reset
    kind: button
    default: ""
    effect: Returns the stacking settings, the local normalization settings, the grouping, the output options, the memory and the debayer to their defaults.
    range: It leaves the list, the preview and **Directory** as they are.
    enabled: Disabled while a run works.
    tradeoff: No file is touched.
```

### The image preview

```controls
window: The image preview
controls:
  - name: Picture
    kind: readout
    default: ""
    effect: Shows the file of the selected row as it is recorded. A mosaic is debayered in color by the fast method, without a question. It never shows the stack.
    enabled: Empty until the list holds a file. A file that cannot be read shows nothing and is reported.
    persistence: Nothing of the preview is kept for the next window.
    note: The wheel zooms, a middle-button drag pans, and holding the left button opens the lens, as in an image view.
  - name: Fit To Window
    kind: toolbutton
    default: ""
    effect: Fits the whole picture into the preview, as each newly chosen file is fitted.
  - name: Zoom In
    kind: toolbutton
    default: ""
    effect: Shows the picture larger, as in an image view.
  - name: Zoom Out
    kind: toolbutton
    default: ""
    effect: Shows the picture smaller, as in an image view.
  - name: Zoom 100%
    kind: toolbutton
    default: ""
    effect: Shows one sample of the file on one pixel of the screen.
  - name: View Channel
    kind: combo
    default: RGB
    options: [RGB, Red, Green, Blue, The other display channels of an image's view]
    effect: Chooses the channel or component the preview displays.
    enabled: Empty without a picture. A grayscale file shows a disabled **Grayscale**.
  - name: Display Mode
    kind: combo
    default: Linear
    options: [Linear, Autostretch]
    effect: Shows the samples as they are, or stretched for the screen.
    scope: The preview only. A stretched picture is not what the run reads.
  - name: Autostretch Settings
    kind: toolbutton
    default: ""
    effect: Opens the settings of the display stretch.
    enabled: With **Display Mode** at **Autostretch**.
  - name: Show statistics
    kind: toolbutton
    default: "On"
    effect: Shows or hides the overlay of the picture's minimum, maximum, mean, median and standard deviation.
```

### Add Folder Options

```controls
window: Add Folder Options
controls:
  - name: Include subfolders (recursive)
    kind: check
    default: "On"
    effect: Adds the files of the folder's subfolders too. Off adds only the folder's own.
    tradeoff: A wide tree takes longer to list and can mix nights and frame types.
  - name: Frame Type
    kind: combo
    default: All
    options: [All, Light, Dark, Flat, Bias, Dark Flat]
    effect: Adds only the files whose header states that frame type. **All** adds every file.
    range: "**Light** takes a type naming LIGHT, **Dark** one naming DARK and not FLAT, **Flat** one naming FLAT and not DARK, **Dark Flat** one naming both, and **Bias** one naming BIAS or OFFSET. A file that states no type counts as a light."
    tradeoff: A header without its type, or with an unusual one, can be left out or let in.
  - name: Filter Name
    kind: field
    default: ""
    effect: Adds only the files whose filter has this name, compared without regard to case or to spaces at its ends. Empty adds every filter.
    tradeoff: A file whose header states no filter, or spells it otherwise, is left out.
    note: The empty field suggests names such as Ha, L, R, G and B.
  - name: OK
    kind: button
    default: ""
    effect: Lists the folder with these options and adds what passes.
  - name: Cancel
    kind: button
    default: ""
    effect: Adds nothing.
```

### The row menu

```controls
window: The row menu
controls:
  - name: Move Up
    kind: action
    default: ""
    effect: Moves the row one place up.
    enabled: Unless the row is the first. The menu is not offered while a run works.
  - name: Move Down
    kind: action
    default: ""
    effect: Moves the row one place down.
    enabled: Unless the row is the last.
  - name: Remove
    kind: action
    default: ""
    effect: Removes the selected rows from the list. The files stay on disk.
  - name: Delete From Disk
    kind: action
    default: ""
    effect: Asks "This will delete the selected file from the disk permanently. Are you sure you want to proceed?", then removes the row and deletes its file.
    range: The file of the row you clicked only.
    tradeoff: The file is gone for good. When it cannot be deleted, the row is gone all the same and the window says "Could not delete" with the path.
```

### Local Normalization Settings

```controls
window: Local Normalization Settings
controls:
  - name: Note
    kind: readout
    default: ""
    effect: Reads "This stack fits each frame's local surfaces and scale against its reference frame" and says that the reference strategy, the evaluation, the PSF flux scale and the hot-pixel and noise filters, which Image Integration offers in this dialog, do not apply here.
  - name: Preset
    kind: combo
    default: Standard (Grid 4)
    options: [Quick (Grid 2), Standard (Grid 4), Fine (Grid 8), Custom]
    effect: Sets **Grid Size** to 2, 4 or 8 and the other values to their defaults. A change of any value reads **Custom**.
    tradeoff: A finer grid follows the frame more closely and prints more of its noise.
  - name: Grid Size
    kind: spinbox
    default: "4"
    widget: {min: 1, max: 32}
    effect: Sets how many cells divide each side of the frame. Higher follows smaller changes of the background, lower only broad ones.
    range: The fit has one node more than cells on each side, which the label beside it counts, as "(5x5 = 25 nodes)".
    tradeoff: More cells cost more work and can print structure and noise into the stack.
  - name: Interpolation
    kind: combo
    default: Bicubic
    options: [Bilinear, Bicubic]
    effect: Chooses how the surface is drawn between the nodes.
    tradeoff: "**Bicubic** is smoother and takes longer. **Bilinear** can show the edges of the cells."
  - name: BG Smoothing
    kind: spinbox
    default: "1"
    widget: {min: 0, max: 10}
    effect: Smooths the background nodes over this many neighbours. 0 does not smooth.
    tradeoff: More smoothing calms noisy nodes and misses small changes.
  - name: Mode
    kind: combo
    default: Linked RGB
    options: [Linked RGB, Per-Channel]
    effect: "**Linked RGB** finds one scale for the three channels, **Per-Channel** one for each."
    tradeoff: "**Linked RGB** keeps the colors steadier. **Per-Channel** can suit narrowband and changes the ratios between the channels."
  - name: Low Clip
    kind: spinbox
    default: "0.000045"
    widget: {min: 0, max: 1, step: 0.00001, decimals: 6}
    effect: Replaces samples below this value with the local background estimate, to prevent artifacts.
    tradeoff: Too high replaces faint real signal.
  - name: High Clip
    kind: spinbox
    default: "0.850"
    widget: {min: 0, max: 1, step: 0.01, decimals: 3}
    effect: Leaves samples above this value out of the local measures, to keep saturated stars out.
    tradeoff: Too low leaves out much of the signal, too high lets saturation in.
  - name: Enable Advanced Options
    kind: check
    default: "Off"
    effect: Enables the options below. It changes no value itself.
    persistence: It opens off each time.
  - name: Estimator
    kind: combo
    default: Sigma Clipped Median
    options: [Percentile, Sigma Clipped Median, Sigma Clipped Mode]
    effect: Chooses how each node's background is measured.
    enabled: With **Enable Advanced Options** checked.
    tradeoff: The sigma-clipped estimators resist bright structure and take longer.
  - name: Percentile
    kind: spinbox
    default: "0.250"
    widget: {min: 0, max: 1, step: 0.01, decimals: 3}
    effect: Sets the share of samples below the background with **Estimator** at **Percentile**. Lower follows the darker background.
    enabled: With **Estimator** at **Percentile**.
  - name: Sigma Clip K
    kind: spinbox
    default: "3.00"
    widget: {min: 0.1, max: 10, step: 0.1, decimals: 2}
    effect: Sets the clipping threshold of the sigma-clipped estimators, in deviations. Lower clips more.
    range: The stack reads a value below 0.5 as 0.5.
    enabled: With a sigma-clipped **Estimator**.
  - name: Sigma Iterations
    kind: spinbox
    default: "3"
    widget: {min: 1, max: 20}
    effect: Sets how many times the clipping repeats at most.
    range: The stack reads a value above 10 as 10.
    enabled: With a sigma-clipped **Estimator**.
    tradeoff: More passes cost more and can steady a node a star spoils.
  - name: Use local scale k(x,y)
    kind: check
    default: "Off"
    effect: Fits a scale that varies over the frame, besides the background, to follow a transparency that changes across it.
    enabled: With **Enable Advanced Options** checked.
    tradeoff: The dialog marks it experimental. It can correct uneven transparency or print noise into the stack.
  - name: Smoothing
    kind: spinbox
    default: "1"
    widget: {min: 0, max: 10}
    effect: Smooths the scale's grid over this many neighbours.
    enabled: With **Use local scale k(x,y)** checked.
    tradeoff: More smoothing prints less noise and follows the frame less.
  - name: Reset to Defaults
    kind: button
    default: ""
    effect: Returns every value to the **Standard (Grid 4)** preset.
  - name: Cancel
    kind: button
    default: ""
    effect: Closes the dialog and keeps the values it opened with.
  - name: OK
    kind: button
    default: ""
    effect: Keeps the values for the stack. Nothing is computed until **Stack Images**.
    persistence: The window keeps the values this dialog shows for the next window, and none of the ones it hides.
```

### Stacking progress

```controls
window: Stacking progress
controls:
  - name: Progress
    kind: readout
    default: ""
    effect: Names the group being stacked, as "Stacking group 'Broadband' (30 frames), 1 of 1", and moves from 0 to 100 percent over all the groups.
    enabled: Shown while a run works, under the title Image Stacking.
  - name: Cancel
    kind: button
    default: ""
    effect: Asks the run to stop. The text reads "Canceling execution..." and the button is disabled.
    range: The group in progress stops at its next stop point and leaves no file. No later group starts.
    persistence: The files of the groups already stacked stay. A stopped run ends without a message.
```

## How it works

1. **Stack Images** takes the list and the values as they stand. The files are sorted by filter, session, exposure
   and path, grouped by the keys of **Group by**, and each group gets its file name.
2. Each group's headers are read, and every frame is held to the first frame's size, pixel type, channels and filter
   array. **Comet** mode first replaces each file with its copy aligned on the comet.
3. The frames are read as [fractions of full scale](../../reference/glossary.md#fraction-of-full-scale). An exact
   zero, such as the border a registration leaves, and a sample that is not a number mean nothing in every frame.
   Each frame is normalized to the group's first frame and given its weight.
4. At each pixel and channel, the samples that mean something are judged by the rejection and combined by
   **Method** into a 32-bit or 64-bit floating-point master. A mosaic master is debayered when **Debayer CFA** asks.
5. The maps and then the master are written whole into **Directory**, and opened when **Open after stacking** asks.
   The next group follows.

### Model

For one pixel of one channel, let \(x_i\) be the sample of frame \(i\) of a group. With **Normalization** at
**Additive With Scaling**, each frame is first brought to the group's first frame \(r\). With \(l\) the median of a
frame and \(s\) its spread by **Scale Estimator**:

\[ x_i' = \frac{s_r}{s_i}\,\bigl(x_i - l_i\bigr) + l_r \tag{1} \]

The other modes are in [Image Stacking Reference](image-stacking-reference.md). A sigma method then judges the
samples. With \(m\) the median and \(\sigma\) the standard deviation of the samples still kept, \(k_L\) the value of
**Sigma Low** and \(k_H\) that of **Sigma High**, a sample is left out when

\[ x_i < m - k_L\,\sigma \quad \text{or} \quad x_i > m + k_H\,\sigma \tag{2} \]

The test is repeated on what is left until it leaves out nothing more. **Winsorized Sigma Clipping** takes
\(\sigma\) from a copy of the samples held within \(m \pm 1.5\,\sigma\) and multiplied by 1.134, again and again
until it moves by less than 0.0005 of itself.

With \(S\) the samples kept and \(w_i\) the weight of frame \(i\), which is 1 under **None (Equal)**, **Average**
writes

\[ \bar{x} = \frac{\sum_{i \in S} w_i\,x_i}{\sum_{i \in S} w_i} \tag{3} \]

When \(n\) samples of equal weight carry independent noise of one deviation \(\sigma_1\), the average's noise is

\[ \sigma_{\bar{x}} = \frac{\sigma_1}{\sqrt{n}} \tag{4} \]

so four frames halve the noise. What every frame shares, such as the fixed pattern of a bias, is not divided: it is
what a master keeps. Nothing clips the result, so a master can hold values below 0 and above 1.

### Boundaries

- Every rejection needs three frames or more in a group. A group of two with a rejection is refused when the stack
  starts.
- A group whose frames differ in size, pixel type, channel count or filter array is refused, the message naming the
  group. So is a mosaic beside a frame that states none.
- An exact zero and a sample that is not a number are neither combined nor judged in any frame, so a real sample of
  exactly zero is left out too. A pixel no frame reaches is written as 0 and marked invalid.
- No alignment is read. Frames that do not line up stack without a message into a soft or doubled master.
- A stop, or closing the window, keeps the files of the groups already stacked. The group in progress leaves no
  file, and a group that fails stops the ones after it.

### Worked example

The frames are the [calibration frames](../../reference/glossary.md#calibration-frame) of one night of NGC 253 from
a QHY183C color camera: 30 [bias frames](../../reference/glossary.md#bias-frame), 20
[dark frames](../../reference/glossary.md#dark-frame) of 180 s and 40
[flat frames](../../reference/glossary.md#flat-frame). All are 16-bit [FITS](../../reference/glossary.md#fits) mosaics
of 5544 by 3684 pixels with `BAYERPAT = 'RGGB'`, and none states a filter. They make the masters that
[Image Calibration](../calibration/image-calibration.md) takes.

Open the window and choose **Add Files** with the 30 biases. They form one group, `Broadband`. Type
`master_bias.fit` in **Name Template**, choose **Browse** and a folder `masters`, turn **Open after stacking** off,
and keep every other value. **Stack Images** writes `master_bias.fit`, a 32-bit mosaic of about 82 MB, whose
`EXPTIME` is the first bias's 0.00025 s. The Session Console's line `Image Stacking: finished in` gives the run's
time.

With **Max Memory** at its default, the 30 frames do not fit at once, so the window reads them in bands of rows. The
strip under the header then reads "Stacking completed with warnings." with "Whole-frame reads are multiplied by the
tile count". The master is the same either way. With **Max Memory** at 4096, each file of these three stacks is read
once.

In the middle 1,100 by 820 pixels, one bias has a median of 0.0291 and a standard deviation of 0.00047, which is 31
[sensor codes](../../reference/glossary.md#sensor-code). Equation (4) predicts \(0.00047/\sqrt{30} = 0.000086\) for
the master. Its deviation there is 0.000099, about 6.5 codes, and the rest is the pattern every bias repeats, about
3.3 codes. The master's median is 0.02935. The rejection leaves out about 0.7 % of the samples: with **Save
rejection maps** on, the merged map's mean there is about 0.007.

Then choose **Clear All**, add the 20 darks, type `master_dark.fit` and stack them the same way. Its median is about
0.0295, of which the bias holds 0.02935. Calibrate the 40 flats by the master bias in Image Calibration, into a
folder `flats_cal`, and stack those 40 files into `master_flat.fit`. They come in two exposures, 0.39 s and 4.35 s,
at about 0.49 and 0.41 in the middle. Choose **Normalization** **Multiplicative**, so that each is brought to the
level of the first, a 0.39 s flat, before the rejection judges it.

To compare, open the first bias and `master_bias.fit`, and answer **No** when each asks whether to
[debayer](../../reference/glossary.md#debayer) it. Apply [Histogram Transformation](../stretching/histogram-transformation.md)
to each with **Shadows** at 0.0270 and **Highlights** at 0.0320, so that both are spread over one range, and show both
at 100 % in the middle.

```figure
images:
  - image: ../../assets/images/processes/stacking-integration/image-stacking-before.webp
    alt: >-
      One bias at 100 percent, a dark gray covered with coarse grain from pixel to pixel and a few single white
      pixels.
    label: Before
  - image: ../../assets/images/processes/stacking-integration/image-stacking-after.webp
    alt: >-
      The master bias at 100 percent, a lighter, even gray almost free of grain, with a few single light and dark
      pixels left.
    label: After
caption: >-
  One bias of NGC 253 and the master of 30, the middle of the frame at 100 % in the Linear display mode, both after
  one Histogram Transformation from 0.0270 to 0.0320.
```

## Starting points

| Situation | Start | Then adjust |
| --- | --- | --- |
| Bias or dark frames | **Average** with **Winsorized Sigma Clipping**, **Normalization** at **None** | **Max Memory** up for many frames. |
| Flat frames calibrated by the bias | **Normalization** at **Multiplicative** | Keep each filter in its own group with **Filter** on. |
| Calibrated, registered lights | **Additive With Scaling**, **IKSS** and **Winsorized Sigma Clipping** | **Save rejection maps** on, then **Sigma High** down if trails remain. A weighting once the registration's figures are at hand. |
| Fewer than ten frames | **Median**, or **Average** with **None** for two frames | More frames before tighter thresholds. |

## When it goes wrong

!!! danger "Files are replaced without a question"
    A master or map whose name is already in **Directory** is replaced, and every name follows from **Name
    Template**. Give each kind of master its own name, and keep the files you need elsewhere.

!!! warning "Misaligned frames stack without a message"
    The window reads no registration. Lights that do not line up give soft or doubled stars, and the run reports
    nothing. Register them with Image Registration first.

!!! warning "A debayered master does not calibrate a mosaic"
    With **Debayer CFA** on, a bias, dark or flat master is written in color, and Image Calibration refuses it beside
    mosaic lights. Stack calibration masters with **Debayer CFA** off.

**Stack Images** stays disabled
:   **Cause.** The list is empty, **Directory** holds no folder or **Name Template** is empty, or a run works.
:   **Fix.** Add the frames, choose **Browse** and a folder, and type a name.

"Stacking completed with warnings." with "Whole-frame reads are multiplied by the tile count"
:   **Cause.** The stack needed more memory than **Max Memory**, so each file was read once for each band of rows.
:   **Fix.** Nothing is wrong with the master. Raise **Max Memory** under **Advanced** to make the next run faster.

"Stacking completed with warnings." with "Sum not normalized"
:   **Cause.** No pixel's sum was positive, so **Sum** wrote the sums as they are.
:   **Fix.** Check that the frames hold signal above zero, or choose **Average**.

"Registration metrics were not found in this output directory, so FWHM weighting is falling back to equal weights."
:   **Cause.** **Directory** holds no `registration_metrics.db`, or the frames have no figures in it.
:   **Fix.** Choose Image Registration's output folder as **Directory**, or choose **None (Equal)** or **Exposure**.

"Error stacking images" with "on some frames and not on frame"
:   **Cause.** Only some frames have the figure the weighting reads: an exposure, a registration's figures or a
    measurable noise.
:   **Fix.** Remove the frames the weighting warning names, add their figures, or choose **None (Equal)**.

"Error stacking images" with "RejectionNeedsMoreFrames"
:   **Cause.** A group with a rejection holds fewer than three frames.
:   **Fix.** Add frames, turn a grouping key off, or set **Rejection Method** to **None**.

"Error stacking images" with "frames with mismatched dimensions", "mismatched color depth" or "mismatched filter arrays"
:   **Cause.** The group the message names mixes sizes, pixel types, channel counts or filter arrays.
:   **Fix.** Remove the odd frames, or give them a group of their own.

"Error stacking images" with "Standalone comet stacking requires"
:   **Cause.** **Comet** mode found no active comet run in the `registration_metrics.db` of **Directory**.
:   **Fix.** Run the comet registration and choose its output folder, or set **Comet Mode** to **Stars**.

"Some files were skipped"
:   **Cause.** Files could not be read when they were added, and the list holds only the others.
:   **Fix.** Read the first reason in the message and the Session Console, then add the corrected files again.

Satellite trails or hot pixels remain in the master
:   **Cause.** No rejection, thresholds set too high, or too few frames for the rejection to judge.
:   **Fix.** Use **Average** with a rejection, save the rejection maps, and lower **Sigma High** a step at a time.

More groups, or other file names, than you expected
:   **Cause.** The frames' filter, date or exposure differ from what you thought, or two groups had one name.
:   **Fix.** Compare **Filter**, **Date** and **Exposure** in the list, and turn the grouping keys you do not need off.

## Related

- [Image Calibration](../calibration/image-calibration.md), which takes the bias, dark and flat masters this window
  makes.
- [Image Registration](../registration-alignment/image-registration.md), to align lights before the stack and to
  record the figures that **FWHM**, **SNR** and **Stars** read.
- [Image Integration](image-integration.md) and [Image Integration Lite](image-integration-lite.md), which calibrate,
  register and stack in one run, with [drizzle](../../reference/glossary.md#drizzle) and reports.
- [Image Stacking Reference](image-stacking-reference.md), for every rejection, normalization and weighting rule, the
  names and files a run writes, the memory, and every message.
- [Image Statistics](../../interface/image-statistics.md), for the level and noise of a master and of a frame.
- [Process Windows](../../interface/process-windows.md), for what every process window shares.

## Credits

- The auto-adaptive weighted average is adapted from `AutoAdaptiveWeightedAverage` of
  [DeepSkyStacker](https://github.com/deepskystacker/DSS), by David C. Partridge with Michael A. Covington and Simon
  C. Smith, after the 1989 lectures of Peter B. Stetson on stellar photometry with CCDs. It is distributed under the
  BSD 3-Clause License of Luc Coiffier and the DeepSkyStacker contributors, whose notice **About > About AfterNight**
  reproduces.
- The rejection methods follow the rules of the [Siril stacking
  documentation](https://siril.readthedocs.io/en/stable/preprocessing/stacking.html).
- B. Rosner, [Percentage Points for a Generalized ESD Many-Outlier
  Procedure](https://doi.org/10.1080/00401706.1983.10487848), Technometrics 25 (1983), 165-172, the test **GESDT**
  runs.
- The critical values of **GESDT** come from the Student-t distribution of the
  [GNU Scientific Library](https://www.gnu.org/software/gsl/), under the GPL-3.0-or-later.
- The deviation of **MAD Clipping** is found with a histogram search after a method of Ingo Weyrich, re-derived
  without its text.
