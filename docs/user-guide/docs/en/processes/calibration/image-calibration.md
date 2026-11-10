---
kind: process
stage: calibrate
menu: Process > Preprocess > Image Calibration
summary: >-
  Subtracts a bias and a dark master from a list of light frames and divides them by a flat master, writing one
  calibrated file for each light, so that each frame records the sky rather than the sensor.
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
    Still image files of one size, pixel type and channel count, and one bias, one dark and one flat master for the
    whole list, each optional. No open image, sequence or mask is used.
  outputs: >-
    One calibrated file for each light, in the folder you choose, as FITS or TIFF of 32-bit or 64-bit floating point,
    or in the light's own format. The source files are not changed and nothing opens.
  state: >-
    Raw linear lights as the camera wrote them, grayscale, a color-filter mosaic or RGB, before the debayer,
    registration and stacking.
derived: false
rewritten: true
---
# Image Calibration

Image Calibration corrects a list of [light frames](../../reference/glossary.md#light-frame) with
[masters](../../reference/glossary.md#master) you made beforehand. It adds an optional
[pedestal](../../reference/glossary.md#pedestal), subtracts a bias master and a scaled dark master, and divides by a
normalized flat master, so that each frame records the sky rather than the sensor. It can also replace the
[hot pixels](../../reference/glossary.md#hot-pixel) and [cold pixels](../../reference/glossary.md#cold-pixel) the
master dark shows. It makes no masters, matches none to groups of lights, and does not
[debayer](../../reference/glossary.md#debayer), register or stack.

**Calibrate Images** writes one file for each light into a folder you choose and marks its row **Calibrated** or
**Failed**. The source files are not changed, and nothing opens in a view.

```figure
image: ../../assets/images/processes/calibration/image-calibration.webp
alt: >-
  The Image Calibration window. On the left, a preview of the first raw light of NGC 253, dark in the Linear display
  mode, with a scatter of stars, the faint galaxy across its middle and its statistics. On the right, Bias Master unchecked, Dark Master and Flat Master checked with the paths of the two masters,
  and the Output & Execution card with the output folder, Use source filename on, the suffix _cal, FITS
  (Recommended) and 32-bit float, over the Calibrate Images button. Across the bottom, the Light Frames table of 35
  raw lights, every row with the Status Calibrated, then the buttons Add Files, Add Folder, Remove Selected, two
  arrows, the counters and Clear All. Reset and Close at the bottom.
caption: >-
  Image Calibration after a run over the 35 lights of NGC 253 with the master dark and the master flat: every row
  reads Calibrated, and the preview shows the first light as it was recorded.
pins:
  - {n: 2, x: 11.3, y: 69.6}
  - {n: 3, x: 89.3, y: 69.6}
  - {n: 4, x: 70.7, y: 14.0}
  - {n: 5, x: 71.1, y: 21.6}
  - {n: 6, x: 70.7, y: 33.0}
  - {n: 7, x: 72.1, y: 44.4}
  - {n: 8, x: 65.7, y: 64.2}
```

## Before you start

- Make the masters first. The window takes one bias, one dark and one flat master for the whole list and builds
  none. Stack them with Image Stacking, or take the ones an Image Integration run left. Calibrate the flats before you
  stack them, by a [dark-flat](../../reference/glossary.md#dark-flat) or by the bias master: this window can do that
  pass too.
- Know what the dark master holds. The window subtracts the bias, then the dark. A dark stacked from raw darks holds
  the bias already, so use it with **Bias Master** off and **Dark scaling** as it is. Scale only a dark that is free
  of bias.
- List lights of one kind: one size, one [pixel type](../../reference/glossary.md#pixel-type) and one channel count.
  The masters need the same size and channels and, for a [CFA](../../reference/glossary.md#cfa) mosaic, the same
  [Bayer pattern](../../reference/glossary.md#bayer-pattern). Exposure, gain, offset, filter and temperature are
  shown, not compared.
- Calibrate a raw mosaic before you debayer it. The preview shows the selected light in color, but the run works on
  the mosaic and writes a mosaic.
- Choose an empty output folder apart from the raw frames. A file of the same name is replaced without a question.
- Keep **FITS (Recommended)** and **32-bit float**. **Original input format** rounds an integer light to its own
  codes, which cannot hold a value below zero, and a camera [RAW](../../reference/glossary.md#raw) file cannot be
  written back.

## Steps

1. **Open the window.** Choose **Process > Preprocess > Image Calibration**. It needs no open image, and its masters
   and output folder stay disabled until the list holds a file.
2. **List the lights.** Choose **Add Files** and select the lights, or **Add Folder** to take a folder's files by
   frame type and filter. The rows appear in date order, and the first light shows in the preview.
3. **Check the list.** Compare **Size**, **Exposure**, **ISO/Gain**, **CFA** and **Depth** down the rows. Remove
   every file that does not belong: each row is calibrated as a light.
4. **Choose the bias, or leave it off.** Check **Bias Master** and choose its **...** button only when the dark master
   is free of bias, or when you have no dark.
5. **Choose the dark.** Check **Dark Master**, choose its **...** button and select the master dark, or drop the file
   on its field. A dark of the lights' exposure needs no scaling.
6. **Choose the flat.** Check **Flat Master** and select the master flat. Keep **Normalize flat master
   automatically** on unless the flat is normalized already.
7. **Choose where the files go.** Choose the **...** beside **Output directory** and pick an empty folder. Keep the
   naming, the format and the precision unless you need others.
8. **Calibrate.** Choose **Calibrate Images**. A progress window counts the lights, and each row's **Status** turns
   to **Calibrated** or **Failed**.
9. **Check the result.** The strip over the controls reads "Image Calibration: Calibration executed successfully!".
   Open a calibrated file beside its raw light, both with [Autostretch](../../reference/glossary.md#autostretch), and
   compare the corners, the dust shadows and the hot pixels.

## Controls

What every process window shares, such as the message strip and the window's size, is in
[Process Windows](../../interface/process-windows.md). The preview is a still preview, whose toolbar and mouse work as
in the [Image Viewer](../../interface/image-viewer.md).

### Image Calibration

```controls
window: Image Calibration
controls:
  - name: Bias Master
    kind: check
    default: "Off"
    effect: Subtracts the bias master from every light, after the pedestal and before the dark.
    range: One file for the whole list, chosen with its **...** button or dropped on its path field. The field takes one local file and cannot be typed in.
    enabled: Once the list holds a file, and not while a run works. Checked with an empty path, it holds **Calibrate Images** back.
    tradeoff: A dark stacked from raw darks holds the bias already, and with both checked the bias is subtracted twice. A wrong bias shifts every light and prints its pattern into them.
    scope: Every sample of every light, and only the lights. It is not subtracted from the dark or the flat master. The master is read once a run, its size and channel count are checked against the first light, and its filter array against each light.
    persistence: The check and the path are kept for the next window. **Reset** clears both.
  - name: "..."
    object: biasFileButton
    kind: button
    default: ""
    effect: Opens a file dialog for one still image file, the bias master, and writes its path into the field.
    enabled: While **Bias Master** is checked, and not while a run works.
    persistence: The dialog starts in the folder you last chose a bias master from.
  - name: Dark Master
    kind: check
    default: "Off"
    effect: Subtracts the dark master from every light, multiplied by **Dark scaling** or by the factor dark optimization finds.
    range: One file for the whole list, chosen or dropped as the bias is.
    enabled: As **Bias Master**.
    tradeoff: A dark of another exposure, temperature, gain or offset subtracts too much or too little thermal signal. It is subtracted after the bias, so a dark that still holds the bias goes without **Bias Master**, unscaled.
    scope: Every light, read once a run and checked as the bias is. With cosmetic correction on, it also marks the pixels to replace.
    persistence: As **Bias Master**.
  - name: "..."
    object: darkFileButton
    kind: button
    default: ""
    effect: Opens a file dialog for the dark master and writes its path into the field.
    enabled: While **Dark Master** is checked, and not while a run works.
    persistence: The dialog starts in the folder you last chose a dark master from.
  - name: Enable dark optimization
    kind: check
    default: "Off"
    effect: Finds, for each light, the factor from 0 to 2 that leaves the least variance in its central 512 x 512 square once the scaled dark is subtracted, and uses it in place of **Dark scaling**.
    range: Only for a light of one channel, grayscale or an undebayered mosaic, at least 512 pixels on each side. An RGB or smaller light is calibrated with **Dark scaling**, and the Session Console says so for each one. The factor is found to within 0.001.
    enabled: Under **Advanced Dark Settings**, which opens closed, once the list holds a file and **Dark Master** is checked.
    tradeoff: The dark must be free of bias, with the bias master checked, or the bias is scaled with it. The factor found can differ from the ratio of the exposures, and it is shown and recorded nowhere, so compare a result with a fixed factor. Each light takes one more pass over its central square.
    scope: Each light on its own, before the flat.
    persistence: Kept for the next window. **Reset** turns it off.
  - name: Dark scaling
    kind: spinbox
    default: "1.00"
    widget: {min: 0.1, max: 10, step: 0.1, decimals: 2}
    effect: Multiplies the dark master before it is subtracted. Higher subtracts more thermal signal, lower less.
    enabled: Under **Advanced Dark Settings**, once the list holds a file and a dark master is chosen, with dark optimization on or off.
    tradeoff: Too high leaves the background too low, with negative values and dark holes at hot pixels. Too low leaves thermal pattern and hot pixels. Scale only a dark that is free of bias, by the ratio of the lights' exposure to the dark's.
    scope: Every light while dark optimization is off. With it on, the RGB and smaller lights only.
    persistence: Kept for the next window. **Reset** returns it to its default.
  - name: Enable cosmetic correction
    kind: check
    default: "Off"
    effect: Marks the cold and hot pixels of the dark master, once a run and in each of its channels, and replaces them in every light after the flat.
    range: With both **Cold sigma** and **Hot sigma** unchecked, nothing is replaced, and the run ends with a warning that says so.
    enabled: Under **Advanced Dark Settings**, once the list holds a file and a dark master is chosen.
    tradeoff: The dark is scanned once more and each light copied once more. Low thresholds replace compact real signal, such as small stars, along with the defects.
    scope: Every channel of every light. In a mosaic, a pixel is replaced from pixels of its own color.
    persistence: Kept for the next window. **Reset** turns it off.
  - name: Cold sigma
    object: coldSigmaCheck
    kind: check
    default: "On"
    effect: Looks for cold pixels. Off, none is marked.
    persistence: Kept for the next window. **Reset** turns it on.
  - name: Cold sigma
    object: coldSigmaValue
    kind: spinbox
    default: "3.000"
    widget: {min: 0, max: 99.99, step: 0.1, decimals: 3}
    effect: Sets how many standard deviations below its channel's median a dark sample must lie to be cold. Lower marks more pixels, higher fewer.
    range: A sample the hot threshold has already marked stays hot.
    tradeoff: Too low replaces ordinary pixels. Too high leaves cold defects.
    scope: Marked on the dark, replaced in every light, channel by channel.
    persistence: Kept for the next window. **Reset** returns it to its default.
  - name: Hot sigma
    object: hotSigmaCheck
    kind: check
    default: "On"
    effect: Looks for hot pixels. Off, none is marked.
    persistence: Kept for the next window. **Reset** turns it on.
  - name: Hot sigma
    object: hotSigmaValue
    kind: spinbox
    default: "3.000"
    widget: {min: 0, max: 99.99, step: 0.1, decimals: 3}
    effect: Sets how many standard deviations above its channel's median a dark sample must lie to be hot. Lower marks more pixels, higher fewer.
    range: Hot is decided first. Where the two thresholds meet or cross, as with both at 0 or over a dark without spread, a sample that meets both is hot. No threshold is cut to a range, so one beyond every sample marks nothing.
    tradeoff: Too low marks many bright samples. Too high leaves hot pixels.
    scope: Marked on the dark, replaced in every light, channel by channel.
    persistence: Kept for the next window. **Reset** returns it to its default.
  - name: Auto-estimate
    kind: button
    default: ""
    effect: Counts the cold and hot pixels the thresholds as they stand find in the dark master, summed over its channels, and shows "Detected N cold pixels and M hot pixels".
    range: It neither suggests nor changes a threshold. With both sigma boxes unchecked, it reports an error instead.
    enabled: Under **Advanced Dark Settings**, once the list holds a file and a dark master is chosen. Disabled while it counts, and **Calibrate Images** asks you to wait meanwhile.
    tradeoff: It reads and scans the dark once more, and the run scans it again.
    persistence: The count is kept only in the Session Console.
  - name: Flat Master
    kind: check
    default: "Off"
    effect: Divides every light by the flat master, normalized by its own central mean when **Normalize flat master automatically** is on.
    range: One file for the whole list, chosen or dropped as the bias is.
    enabled: As **Bias Master**.
    tradeoff: A flat of other optics, filter, focus or rotation prints its gradients, dust shadows and colors into every light. A flat that still holds its bias corrects too little.
    scope: Every light and channel, last. Read once a run and checked as the bias is. A zero in it leaves a sample that is not a number.
    persistence: As **Bias Master**.
  - name: "..."
    object: flatFileButton
    kind: button
    default: ""
    effect: Opens a file dialog for the flat master and writes its path into the field.
    enabled: While **Flat Master** is checked, and not while a run works.
    persistence: The dialog starts in the folder you last chose a flat master from.
  - name: Normalize flat master automatically
    kind: check
    default: "On"
    effect: Divides the flat by the mean of its central third before it divides the light, one mean for each channel, or for each filter site of a mosaic, so that the flat changes how the light is spread and not its level. Off divides by the flat as it is.
    enabled: Under **Advanced Flat Settings**, which opens closed, once the list holds a file and **Flat Master** is checked.
    tradeoff: A central third that is not typical of the flat, with a dust shadow in it for example, sets the wrong level. Off, a flat whose values are not near 1 changes the level of every light.
    scope: The flat division of every light.
    persistence: Kept for the next window. **Reset** turns it on.
  - name: Output directory
    object: outputDirectory
    kind: readout
    default: ""
    effect: Shows the folder the calibrated files are written to. It cannot be typed in.
    enabled: Once the list holds a file.
    persistence: Kept for the next window, also when the folder has gone since. A missing folder is created when the run starts, where the system allows it. **Reset** empties it.
  - name: "..."
    object: outputDirectoryButton
    kind: button
    default: ""
    effect: Opens a folder dialog to choose the output folder.
    range: It starts in the folder shown, else in the last output folder you chose, else in the folder of the first listed file.
    enabled: Once the list holds a file, and not while a run works.
    tradeoff: The folder needs room for every calibrated file. A file of the same name there is replaced without a question.
  - name: Use source filename
    kind: check
    default: "On"
    effect: Names each file after its light, followed by **Suffix**. Off names them `image_0000`, `image_0001` and so on by their row, counted from 0, followed by **Suffix**.
    tradeoff: Two lights of one name from two folders get one name, and the second replaces the first. Numbers avoid that within a run, but a later run replaces them.
    scope: Every file of the run.
    persistence: Kept for the next window. **Reset** turns it on.
  - name: Suffix
    kind: field
    default: "_cal"
    effect: Is added to each name before the extension, as in `light_cal.fits`.
    range: Any text, or none. It is neither checked nor cleaned.
    tradeoff: Without a suffix, an output folder that holds the lights replaces them with their calibrated files. A character or a path the system refuses makes every write fail.
    persistence: Kept for the next window. **Reset** returns it to its default.
  - name: Format
    kind: combo
    default: FITS (Recommended)
    options: [FITS (Recommended), TIFF, Original input format]
    effect: Chooses the container. **FITS (Recommended)** keeps a light's `.fit`, `.fts` or `.fits` extension, in any case, and writes `.fits` otherwise. **TIFF** writes `.tiff`. **Original input format** writes the light's own container, extension and pixel type.
    range: An X-Trans mosaic cannot be written as FITS, so choose TIFF for it. A mosaic in its own camera RAW, PNG or JPEG format cannot be written and fails.
    tradeoff: "**Original input format** rounds each value to the nearest code of an integer type, which keeps no value below zero or above full scale. Only the pixel type is stated, and the export settings of the application are not applied."
    persistence: Kept for the next window. A saved value the window does not know opens as FITS. **Reset** returns it to FITS.
  - name: Precision
    kind: combo
    default: 32-bit float
    options: [32-bit float, 64-bit double]
    effect: Chooses the pixel type of a FITS or TIFF file, and the precision the calibration works in.
    enabled: Disabled with **Original input format**, which works in 64-bit for a 64-bit floating-point or 32-bit integer light and in 32-bit for the others.
    tradeoff: "**64-bit double** doubles the files on disk and the memory of a run."
    persistence: Kept for the next window. **Reset** returns it to 32-bit.
  - name: Pedestal (16-bit codes)
    kind: spinbox
    default: "0.0"
    widget: {min: 0, max: 10000, step: 10, decimals: 1}
    effect: Adds a constant to every light before the bias and the dark are subtracted, so that fewer samples end below zero. It is read in 16-bit codes, so 655.4 is about a hundredth of full scale whatever the light's own depth.
    range: It is added as typed and not cut to the light's range.
    enabled: Under **Advanced Calibration Settings**, which opens closed, once the list holds a file. A value above 0 enables **Calibrate Images** without any master.
    tradeoff: It raises the level of every light. It cannot bring back values a file cut before. Leave it at 0 unless you know why the lights must stay above zero, such as an integer output or a next tool that refuses negative values.
    scope: Every sample of every light.
    persistence: Kept for the next window. **Reset** returns it to its default.
  - name: Calibrate Images
    kind: button
    default: ""
    effect: Calibrates every light of the list, in the list's order, writes one file for each and marks its row.
    range: It needs a checked master with a file, or a pedestal above 0. It asks for an output folder when none is chosen, and asks you to wait while files are still read or **Auto-estimate** counts.
    enabled: Once the list holds a file, while no checked master lacks its file. Disabled while a run works.
    tradeoff: Each light is read and written whole, one after the other, the arithmetic of each spread over the processor's threads. The window stays usable meanwhile. A stop keeps the files written and leaves none for the light in progress.
    scope: The list and the values as they are when you press it. A change made during the run does not reach it.
    note: Before you register, open a few files at 100 % and check their count, names, pixel type and mosaic, their black level, and the dust and defects left.
  - name: Light Frames
    kind: readout
    default: ""
    effect: Lists the files the run calibrates, one row each, under File, Date, Size, Filter, ISO/Gain, Offset, Exposure, CFA, Depth, Type and Status, read from each file's header. Choosing a row shows its file in the preview.
    range: Filter, ISO/Gain, Offset and Exposure read `-` when the header states none, and Exposure is in seconds. CFA reads Yes for a mosaic. Depth reads 8 bit, 16 bit, 32 bit int, 32 bit float or 64 bit float. Date is the creation time the header states, else the start of the exposure, else the time the file last changed, in UTC. Type reads Light on every row, whatever the file is. Status reads Pending, Calibrated or Failed.
    enabled: The list is neither reordered nor changed while a run works.
    tradeoff: The run compares only size, pixel type and channels. Exposure, gain, offset, filter and temperature are shown for you to compare, and a dark or a flat listed here is calibrated as a light.
    scope: The run calibrates every row, in the list's order.
    persistence: The list opens ordered by Date, oldest first. Clicking a column's title orders by it, newest or largest first, and again the other way. Size, Type and Status order nothing, and equal values keep the order of their dates. A row moved by hand ends the column's order. The list and its order are kept for the next window, and its files are read again when it opens. The statuses are not kept, and a run sets every row to Pending first.
    note: The full path of a file is its tooltip. Right-click a row for the row menu.
  - name: Add Files
    kind: button
    default: ""
    effect: Opens a file dialog for one or several still image files, and adds them to the list while their headers are read in the background.
    range: FITS, XISF, TIFF, PNG, JPEG and camera RAW files. A SER file, a video or another sequence is not offered, so extract its frames to still files first. A file already listed, by its full path, is passed over. A file that cannot be read is left out, and the others are added.
    enabled: Disabled while a run works.
    tradeoff: Many headers take time to read, and the window stays usable meanwhile. When not every file is added, the window says how many it added, passed over and could not read, with the first reason.
    persistence: The dialog starts in the folder you last added from.
  - name: Add Folder
    kind: button
    default: ""
    effect: Opens a folder dialog, then **Add Folder Options**, and adds the folder's image files that pass the options, as **Add Files** adds them.
    range: The same file types as **Add Files**. The folder is listed in the background.
    enabled: Disabled while a run works.
    tradeoff: A folder with its subfolders can bring in darks, flats or another night. Check the list before you calibrate.
    persistence: The folder dialog starts in the folder you last chose. The options start afresh each time.
  - name: Remove Selected
    kind: button
    default: ""
    effect: Removes the selected rows from the list. The files stay on disk.
    enabled: While the list holds a file. It does nothing while a run works.
  - name: Move frame up
    kind: button
    default: ""
    effect: Moves the selected row one place up, so that it is calibrated earlier.
    enabled: When the selected row can move up. It does nothing while a run works.
    tradeoff: The order decides which files are written before a stop or a failure. It changes no value.
  - name: Move frame down
    kind: button
    default: ""
    effect: Moves the selected row one place down, so that it is calibrated later.
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
    effect: Empties the list, its statuses and the preview, and stops the files still being read, none of which is added afterwards.
    enabled: While the list holds a file. It does nothing while a run works.
    tradeoff: The files stay on disk.
  - name: Close
    kind: button
    default: ""
    effect: Closes the window. Its values and its list are kept for the next window.
    range: ++esc++ and the window's own close button do the same.
    enabled: Also while a run works. Closing then stops the run, the files being read and the count of **Auto-estimate**, and the window closes at once.
    tradeoff: The files already written stay. The light in progress leaves no file, and the rows not reached are not calibrated.
  - name: Reset
    kind: button
    default: ""
    effect: Returns the masters, the corrections and the output to their defaults. Every master is unchecked with its path cleared, and the output folder is emptied.
    range: It leaves the list, its statuses and the preview as they are.
    enabled: Disabled while a run works.
    tradeoff: The values it clears are gone unless you noted them. No file is touched.
    persistence: The new values are kept when the window closes.
    note: Its tooltip reads "Reset calibration settings to their default values."
```

### The image preview

```controls
window: The image preview
controls:
  - name: Picture
    kind: readout
    default: ""
    effect: Shows the file of the selected row as it is recorded, never calibrated. A mosaic is demosaiced in color by the fast method, without a question.
    enabled: Empty until the list holds a file. A newer choice replaces a file still being read. A file that cannot be read shows nothing and is reported.
    persistence: Nothing of the preview, its zoom, channel, mode or overlay, is kept for the next window.
    note: The wheel zooms as the zoom settings of the Interface page say, a middle-button drag pans, and holding the left button opens the lens, as in an image view.
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
    effect: Shows one sample of the file on one pixel of the screen, to look at single defects.
  - name: View Channel
    kind: combo
    default: RGB
    options: [RGB, Red, Green, Blue, The other display channels of an image's view]
    effect: Chooses the channel or component the preview displays, from the same list as an image view, HSI and the a* and b* chrominance among them.
    enabled: Empty without a picture. A grayscale file shows a disabled **Grayscale**.
    scope: The preview only. A component can show defects otherwise than the channels do.
  - name: Display Mode
    kind: combo
    default: Linear
    options: [Linear, Autostretch]
    effect: Shows the samples as they are, or stretched for the screen to bring out faint structure.
    enabled: Once a file is shown.
    scope: The preview only. A stretched picture is not what the run writes.
  - name: Autostretch Settings
    kind: toolbutton
    default: ""
    effect: Opens the settings of the display stretch.
    enabled: With **Display Mode** at **Autostretch**, and not while its window is open.
    tradeoff: Strong settings exaggerate the background and its noise, on the screen only.
  - name: Show statistics
    kind: toolbutton
    default: "On"
    effect: Shows or hides the overlay of the picture's minimum, maximum, mean, median and standard deviation.
    scope: The preview only.
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
    tradeoff: A header without its type, or with an unusual one, can be left out or let in. **Add Files** and **All** pass every file.
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

### Calibration progress

```controls
window: Calibration progress
controls:
  - name: Progress
    kind: readout
    default: ""
    effect: Reads "Calibrating 35 images", with the number of the list, and moves from 0 to 100 percent as each light is done.
    enabled: Shown while a run works, under the title Image Calibration.
    note: A run that was stopped ends without a message.
  - name: Cancel
    kind: button
    default: ""
    effect: Asks the run to stop. The text reads "Canceling execution..." and the button is disabled.
    range: The light being read, calibrated or written stops at its next check and leaves no file.
    persistence: The files already written stay, and their rows read Calibrated. The rows not reached stay Pending.
```

## How it works

1. **Calibrate Images** takes the list and the values as they stand. It checks that the lights share one size, pixel
   type and channel count, then reads each checked master once and checks its size and channels against the first
   light.
2. With cosmetic correction on, it marks the dark's cold and hot pixels, once for the run.
3. Each light, in the list's order, is read as a
   [fraction of full scale](../../reference/glossary.md#fraction-of-full-scale), as a mosaic when it is one, and
   calibrated by equation (1) in one pass: the pedestal, the bias, the scaled dark, then the normalized flat.
4. The marked pixels are replaced, and the light is written whole in the chosen format, or not at all. Its row turns
   **Calibrated** or **Failed**, and the next light follows.
5. Each file keeps the light's [header cards](../../reference/glossary.md#header-card), its acquisition data and its
   filter array. Nothing is debayered, registered or opened, and no report is written.

### Model

An integer file's codes are divided by their largest value, 65,535 for 16-bit, and a floating-point file's samples
are taken as they are. With \(L\) a sample of the light, \(B\), \(D\) and \(F\) the samples of the bias, dark and flat
masters at the same place, \(p\) the **Pedestal (16-bit codes)**, \(k\) the dark's factor and \(\bar{F}_s\) the mean
of the flat over its central third for the sample's channel or filter site, the calibrated sample is

\[ C = \frac{L + p/65535 - B - k\,D}{F / \bar{F}_s} \tag{1} \]

A master left unchecked counts as 0 above the line and as 1 below it. With **Normalize flat master automatically**
off, \(\bar{F}_s = 1\). Each step is computed in double precision and kept in the working precision of **Precision**.
Nothing is cut, so a floating-point file keeps the values the subtractions take below zero and the division lifts
above 1.

The factor \(k\) is **Dark scaling**. With **Enable dark optimization** on, for a light of one channel and at least
512 pixels on each side, \(k\) is the value that leaves the least variance over the light's central square \(Q\) of
512 by 512 pixels:

\[ k = \operatorname*{arg\,min}_{0 \le k \le 2}\ \operatorname{Var}_{Q}\bigl(L + p/65535 - B - k\,D\bigr) \tag{2} \]

Cosmetic correction measures the median \(m\) and the population standard deviation \(\sigma\) of each channel of the
dark. With \(s_h\) and \(s_c\) the values of **Hot sigma** and **Cold sigma**, a dark sample \(x\) is hot when

\[ x \ge m + s_h\,\sigma \tag{3} \]

and otherwise cold when \(x \le m - s_c\,\sigma\). Each marked sample of a calibrated light becomes the median of its
neighbours, without itself and without a neighbour outside the frame: those within 1 pixel of a hot one and within 2
of a cold one. In a mosaic the neighbours are the sites of the same color, at twice those distances.

### Boundaries

- Masters are checked against the first light before any light is read: one of another size, then one of another
  channel count, or one that cannot be read, stops the run. A bias, dark or flat whose filter array differs from a
  light's fails that light.
- A zero in the flat leaves a sample that is not a number, which the file marks as invalid. Nothing fills it, and
  the flat has no floor. A flat whose central mean is zero, or not a number, fails every light.
- One set of masters serves every light. The window matches nothing to exposure, filter, temperature, camera or
  night.
- A light that fails is marked **Failed**, and the others go on. Its reason is in the Session Console.
- A stop, or closing the window, keeps the files written. The light in progress leaves no file, and the rows not
  reached stay **Pending**.

### Worked example

The data is one night of NGC 253 from a QHY183C color camera: 35 lights and 20 darks of 180 s, 40 flats and 30
[bias frames](../../reference/glossary.md#bias-frame), all 16-bit [FITS](../../reference/glossary.md#fits) mosaics of
5544 by 3684 pixels with `BAYERPAT = 'RGGB'`, gain 11 and offset 30. The masters are made first. In
[Image Stacking](../stacking-integration/image-stacking.md), as it opens, stack the 30 biases with **Name Template**
`master_bias.fit` and the 20 darks with `master_dark.fit`. The master dark holds the bias. Then list the 40 flats in
this window, check **Bias Master** alone with `master_bias.fit`, choose a folder `flats_cal` and calibrate them. Stack
those 40 files into `master_flat.fit`.

For the lights, choose **Clear All**, add the 35 lights, and uncheck **Bias Master**. Check **Dark Master** with
`master_dark.fit` and **Flat Master** with `master_flat.fit`, choose a folder `lights_cal`, and keep every other
value. **Calibrate Images** writes 35 files named after the lights with `_cal.fits` added. Each is a 32-bit mosaic of
about 82 MB, so the run needs about 2.9 GB, and the calibrated flats took 3.3 GB. The Session Console's line
`Image Calibration: 35 calibrated, 0 failed in` gives the run's time. No frame opens in a view during the example.

The master dark's median is about 0.02953, of which the bias holds 0.02935, so 180 s add about 12 codes. In the
corners, the master flat's green sites are about 0.90 of their central mean, and 0.906 at the top left. A green
background sample in the top-left corner of the first light reads about 0.0601, which is 3,936 codes. With \(p = 0\), no bias and \(k = 1\), equation (1)
gives \((0.0601 - 0.0295)/0.906 = 0.0338\). The median of the whole light falls from 0.0566 to 0.0281. Against the
middle of the frame, the corners gain about a tenth, which takes the vignetting away.
[Image Statistics](../../interface/image-statistics.md) gives such medians, and the
[FITS Header, EXIF, And Astrometry Metadata](../../interface/fits-header-viewer.md) window the cards each file kept.

To compare, open the first light and its calibrated file. Each asks "Do you want to automatically debayer it before
opening?": answer **Yes** to both. Show both whole, with Autostretch.

```figure
owed: true
images:
  - image: ../../assets/images/processes/calibration/image-calibration-before.webp
    alt: >-
      The whole raw light in color, NGC 253 across the middle, the corners of the frame darker than its centre, and a
      scatter of single colored dots over the background.
    label: Before
  - image: ../../assets/images/processes/calibration/image-calibration-after.webp
    alt: >-
      The same light calibrated, the background even from corner to corner and the colored dots gone.
    label: After
caption: >-
  The first light of NGC 253 before and after calibration with the master dark and the master flat, the whole frame
  in each view's Autostretch display mode.
```

## Starting points

| Situation | Start | Then adjust |
| --- | --- | --- |
| A dark stacked from raw darks, at the lights' exposure | The dark and the flat, **Bias Master** off | Cosmetic correction, when hot pixels remain. |
| A dark free of bias, or of another exposure | All three masters, **Dark scaling** at the ratio of the exposures | [Dark optimization](../../reference/glossary.md#dark-optimization) for one-channel lights, checked against the fixed factor. |
| No darks | The bias and the flat | Cosmetic Correction on the calibrated lights. |
| A long list, or new masters | One representative light, FITS at 32-bit, the optional corrections off | Each correction in turn, then the whole list. |

## When it goes wrong

!!! danger "Files are replaced without a question"
    A file of the same name in the output folder is replaced, and so is the first of two lights of one name from two
    folders. With an empty **Suffix** and the lights' own folder as output, the calibrated files replace the raw
    lights. Give each run an empty folder of its own.

!!! warning "The bias subtracted twice"
    A dark master stacked from raw darks holds the bias, as Image Integration's master dark does. With **Bias Master**
    checked as well, every light ends a bias too low, much of it below zero. Check **Bias Master** only beside a dark
    that is free of bias.

!!! danger "A floating-point light in ADU is not rescaled"
    A floating-point file is taken as it is. A light or master written in [ADU](../../reference/glossary.md#adu),
    with samples up to 65,535 rather than up to 1, is calibrated without a warning, 65,535 times off the scale of an
    integer master and of the pedestal. Convert such files to fractions of full scale first, or calibrate them only
    with masters in the same units and no pedestal.

!!! warning "One set of masters for every light"
    The list may mix filters, exposures or nights, and every light still gets the same masters. Calibrate each set
    in a run of its own, or let Image Integration match the masters.

**Calibrate Images** stays disabled
:   **Cause.** The list is empty, no master is checked and the pedestal is 0, or a checked master has no file.
:   **Fix.** Add the lights, then check each master you need and choose its file, or uncheck it.

"You need to define an output directory before executing image calibration"
:   **Cause.** No output folder is chosen.
:   **Fix.** Choose the **...** beside **Output directory** and pick a folder you can write to.

"Wait until the files being added and the cosmetic correction estimate have finished before calibrating."
:   **Cause.** Headers are still being read, or **Auto-estimate** is still counting.
:   **Fix.** Wait until the list stops growing and the count is shown, then choose **Calibrate Images** again.

"An error occurred when calibrating images:" with "Some images in the list have different size" or "different color depth"
:   **Cause.** The lights differ in width and height, or in pixel type or channel count.
:   **Fix.** Remove the odd files, or calibrate each kind in a run of its own.

"An error occurred when calibrating images:" with "Could NOT load calibration", "file size is different" or "file depth is different"
:   **Cause.** The master the message names cannot be read, or its size or channels differ from the first light's.
:   **Fix.** Check that the file opens in AfterNight, and choose a master made from frames of the lights' camera and
    size.

"Calibration did not produce any output images. First error:" and a reason
:   **Cause.** Every light failed, most often because a master's filter array differs from the lights' or the format
    cannot hold the lights.
:   **Fix.** With "must match the light's channels and CFA", choose masters made from undebayered frames of the same
    camera and pattern. With "cannot be written as", choose **FITS (Recommended)** or **TIFF**.

"Calibration completed with warnings." with "images succeeded. First error:"
:   **Cause.** Some lights failed and the others were written.
:   **Fix.** Read the **Failed** rows and the Session Console, correct those lights, and calibrate only them, so that
    the good files are not replaced.

"Some files were skipped"
:   **Cause.** Files could not be read when they were added, and the list holds only the others.
:   **Fix.** Read the first reason in the message and the Session Console for each file, then add the corrected files
    again.

The calibrated lights are too dark, or much of them below zero
:   **Cause.** The bias was subtracted twice, the dark is too strong for the lights, or **Dark scaling** is too high.
:   **Fix.** Uncheck **Bias Master** beside a dark that holds the bias, and return **Dark scaling** to its default.
    Check the masters' exposure, gain and temperature before you add a pedestal.

Bright corners, rings or pixels that are not a number after the flat
:   **Cause.** The flat does not match the optics, still holds its bias, or has samples near zero.
:   **Fix.** Make a flat of the same optics, calibrated before it was stacked, and look at the master itself. Compare
    **Normalize flat master automatically** on and off only after that.

"Cosmetic correction was on but neither the cold nor the hot sigma was checked, so no pixel was corrected."
:   **Cause.** Both sigma boxes were unchecked.
:   **Fix.** Check **Cold sigma**, **Hot sigma** or both, count with **Auto-estimate**, and run again.

Rows still read Pending after the run
:   **Cause.** The run was stopped, or the window closed, before it reached them.
:   **Fix.** Check the files written, remove their rows, and calibrate the rest.

## Related

- [Image Stacking](../stacking-integration/image-stacking.md), to make the bias, dark and flat masters this window
  takes.
- [Image Integration](../stacking-integration/image-integration.md), which makes the masters, matches them to the
  lights and calibrates, registers and stacks in one run.
- [Cosmetic Correction](cosmetic-correction.md), for the defects calibration leaves, or for lights without darks.
- [Debayer](debayer.md) and [Image Registration](../registration-alignment/image-registration.md), the steps after
  calibration.
- [Image Calibration Reference](image-calibration-reference.md), for every message, the files a run writes, and
  what too little and too much of each correction look like.
- [Process Windows](../../interface/process-windows.md), for what every process window shares.
