---
kind: process
stage: stack
menu: Process > Utilities > Image List Analyzer
summary: >-
  Lists image files with the facts their headers give, shows and plays them one after another with each file's
  histogram and statistics, and exports the list as one SER, AVI, MP4 or WebM file.
data_in: either
data_out: either
destructive: false
preview: static
sequences: false
masks: none
external: none
glance:
  enabled: >-
    Always. The window opens without an image and chooses its files itself. Its list actions wait for a first file,
    and each choice of the menu opens another window.
  inputs: >-
    Still image files of the formats AfterNight opens, chosen one by one or from a folder. No open image, no mask and
    no sequence file.
  outputs: >-
    Nothing for looking: the files are never changed. Export Sequence writes one SER, AVI, MP4 or WebM file of every
    listed file, in the table's order, and Delete From Disk deletes one file. The proxies for playback are temporary.
  state: >-
    The lights of a session, raw or calibrated, before registration and stacking, to look through, put in order and
    thin out by hand.
derived: true
---
# Image List Analyzer

Image List Analyzer lists a set of image files and shows them one after another, so that you can look through a
session before you stack it. The table gives each file's header facts and sorts by them. The preview shows the selected
file, or plays the list as a [sequence](../../reference/glossary.md#sequence) at the rate you set, with the file's
histogram and statistics beside it.

The window changes no file and measures no stars. You thin the list out by hand. **Export Sequence** writes the files
that are left, in the table's order, as one [SER](../../reference/glossary.md#ser), AVI, MP4 or WebM file.

```figure
owed: true
image: ../../assets/images/processes/stacking-integration/image-list-analyzer.webp
alt: >-
  The Image List Analyzer window. On the left, the preview of a color frame of a galaxy among stars, with a card of
  statistics in its corner and a player under it. On the right, the summary of the file, a histogram with three narrow
  peaks near its left edge, the Optimize For Fast Play button, the status Images Already Optimized and the Export
  Sequence button. Below, a table of files with the columns File, Date, Size, Filter, ISO/Gain, Offset, Exposure, CFA
  and Depth, its first row selected, then Add Files, Add Folder, Remove Selected, Current Image 1, Total Images 35 and
  Clear All. Reset and Close run along the bottom.
caption: >-
  Image List Analyzer with 35 raw color-filter lights of NGC 253, in Date order and optimized for playback, showing the
  first of them.
pins:
  - {n: 2, x: 2.3, y: 92.1}
  - {n: 3, x: 2.3, y: 67.1}
  - {n: 4, x: 35.0, y: 11.7}
  - {n: 6, x: 69.5, y: 55.5}
  - {n: 8, x: 69.5, y: 62.6}
```

## Before you start

- To choose frames by their stars, [FWHM](../../reference/glossary.md#fwhm),
  [SNR](../../reference/glossary.md#snr) and eccentricity, register them in
  [Image Registration](../registration-alignment/image-registration.md), whose table lists those figures.
- Leave the files where they are while the window is open. The preview, **Open In Editor**, **Export Sequence** and
  **Delete From Disk** all work on the files at their paths.
- A list you play or export should hold files of one size, channel count and
  [pixel type](../../reference/glossary.md#pixel-type). Nothing compares them: the list is described by its first
  file, and a file that differs fails when it is played or exported.
- Choose the **Demosaic Algorithm** on the **Raw Images** page of the settings first. The preview, the proxies and the
  export [debayer](../../reference/glossary.md#debayer) color-filter frames with it.
- **Optimize For Fast Play** writes a full-size, 8-bit color copy of every file to the session's temporary folder.
  Check that the folder has room for them.

## Steps

1. **Open the window.** Choose **Process > Utilities > Image List Analyzer**. No image needs to be open. The summary
   reads "The image list is empty" and the status under **Optimize For Fast Play** reads "No Optimization Needed".
2. **Add the files.** Choose **Add Files** and select them, or choose **Add Folder**, pick a folder and set
   **Add Folder Options**. The table fills as the headers are read, in **Date** order, and the first row is selected.
3. **Put the list in order.** Click a column's header to sort by it, and click it again to reverse the order. The
   preview plays the files, and an export writes them, in the order the table shows.
4. **Look at a file.** Select a row, or step with the player under the preview. The summary shows the file's header at
   once, and the histogram and the statistics card follow when the file has been measured.
5. **Leave files out.** Select a row and choose **Remove Selected**, or right-click it for **Remove** or
   **Delete From Disk**. **Total Images** counts the files that are left.
6. **Prepare fast playback.** With **Display Mode** at **Autostretch**, choose **Optimize For Fast Play**. A progress
   window follows the files, then the status reads "Images Already Optimized".
7. **Play the list.** Type a rate in the player's **FPS** box and press its play button. The table's selection follows
   the frame shown.
8. **Export the list.** Choose **Export Sequence**, choose the format and the file in its dialog, then **OK**. A
   progress window follows the frames, and "Sequence exported successfully!" says when the file is written.

## Controls

The preview's zoom and channel work as in [Process Windows](../../interface/process-windows.md), which also says what
++esc++ and the window's kept size do. This window has no **Apply**: it changes no file, and its runs are the
optimization and the export.

```controls
window: The preview
controls:
  - name: Preview
    kind: readout
    default: ""
    effect: >-
      Shows the selected file as an image view reads it, a color-filter mosaic debayered by the **Demosaic Algorithm**
      of the settings. It plays the list from a temporary image list of the files, or of their proxies.
    range: >-
      The picture is fitted to the pane when a list is first shown, and again when the frames change size. Otherwise
      the zoom you choose is kept.
    enabled: Empty until the list holds a file.
    tradeoff: >-
      A frame is read in full when it is shown. A large camera raw or FITS file, or a file on a network drive, makes
      stepping and playing slower.
    scope: The window only. Nothing it shows changes a file or ranks one.
  - name: Display Mode
    kind: combo
    default: Autostretch
    options: [Linear, Autostretch]
    effect: >-
      **Linear** shows each file's samples as they are. **Autostretch** stretches each frame for the screen as it is
      shown, or shows its proxy, which is stretched already.
    range: >-
      The proxies play only in **Autostretch** with the **Autostretch Settings** they were written with, and are shown
      as they are, without a second stretch. **Linear**, or other settings, shows the files themselves, so the picture
      is always in the mode this box names. Back in the proxies' display, they play again without being written again.
    enabled: Empty until a file is shown.
    scope: The preview and the status under **Optimize For Fast Play**. It changes no file.
  - name: Autostretch Settings
    kind: toolbutton
    default: ""
    effect: >-
      Opens the stretch settings of the preview. **Optimize For Fast Play**, and the **Autostretch** of an export, take
      the settings shown here.
    enabled: With **Display Mode** at **Autostretch**.
    tradeoff: A change leaves the proxies of the old settings behind, and the files themselves play until you optimize again.
  - name: Show statistics
    kind: toolbutton
    default: "On"
    effect: >-
      Shows or hides a card over the preview with the file's **Min**, **Max**, **Mean**, **Median** and **StdDev**, to
      four decimals, in the file's own units. For a color frame **Mean**, **Median** and **StdDev** are means over its
      channels, and **Min** and **Max** the extremes.
    range: >-
      Each file is measured once, from the file itself and never from its proxy, and the card is shown again whenever
      its frame is, during playback too. Nothing shows while the file is being measured.
    tradeoff: >-
      A file is measured after its frame is shown, one file at a time, so measuring never holds up a frame. A file that
      cannot be measured shows no card.
    scope: The preview only.
  - name: Play sequence
    kind: toolbutton
    default: ""
    effect: Plays the list from the frame shown at the rate of **FPS**, and pauses it at a second press. The table's selection follows each frame.
    range: >-
      Playback stops at the last frame. Pressed on the last frame, it starts again from the first.
    enabled: >-
      With a file in the list, in either display mode: the files themselves play without proxies, which only make
      **Autostretch** faster. Disabled while **Optimize For Fast Play** works.
    tradeoff: >-
      The rate is a request. A frame on its way is never skipped: playback waits for it, so large frames or files on a
      slow disk play slower than asked. No frame is made up between two.
  - name: Stop sequence
    kind: toolbutton
    default: ""
    effect: Stops playback and returns to the first frame.
    enabled: While the list plays.
  - name: Previous frame
    kind: toolbutton
    default: ""
    effect: Shows the frame before, and selects its row.
  - name: Next frame
    kind: toolbutton
    default: ""
    effect: Shows the frame after, and selects its row.
  - name: Frame
    kind: spinbox
    default: "0"
    widget: {min: 0, step: 1}
    effect: Shows a frame by its number, typed in the box or set with the slider beside it, and selects its row.
    range: From 1 to the number of files once the list holds one, shown as `1/35`.
  - name: FPS
    kind: spinbox
    default: "1"
    widget: {min: 1, max: 60, step: 1}
    effect: Sets the playback rate in frames per second. A change while the list plays applies at once.
    persistence: >-
      The window keeps it when files are added, removed, sorted, optimized or cleared, and for the next window, also
      in a later session. **Reset** returns it to 1. **Export Sequence** opens with it.
    note: Its tooltip reads "Frames per second".
```

```controls
window: Image List Analyzer
controls:
  - name: Image summary
    object: imageSummary
    kind: readout
    default: The image list is empty
    effect: >-
      Shows the selected file's name, then its **File Info**: path, size, creation and modification times and codec.
      Then its **Image General Info**: resolution, channels, sample format, pixel format, CFA pattern and size in
      memory. Then its **Image Detailed Info**: camera, ISO or gain, offset, creation date, exposure time, sensor
      temperature and software.
    range: Read from the header when the file was added, and shown at once when the row changes. A value the file does not give reads `-`.
    tradeoff: A header can lack a value or state a wrong one. Nothing here measures quality.
  - name: Histogram
    kind: readout
    default: ""
    effect: >-
      Shows the counts of the file whose frame the preview shows, in 256 bins from 0 to 1: one line for a grayscale
      file, three for a color or debayered one.
    range: >-
      Counted once for each file, from the file itself and never from its proxy, with its statistics, and shown again
      whenever its frame is. Nothing shows while the file is being measured.
    enabled: The clipping shares in its corner appear once the list holds a file.
    tradeoff: It shows how the samples spread, not how good the frame is.
    note: It has no toolbar. Its zoom, panning and coordinates work as in the histogram of a process window.
  - name: Optimize For Fast Play
    kind: button
    default: ""
    effect: >-
      Writes a proxy of every listed file that has none for the preview's present **Autostretch Settings**: the file
      read as the preview reads it, debayered by the **Demosaic Algorithm** of the settings, stretched by those
      settings and written as an 8-bit FITS file at full size. A proxy written with other settings is replaced, and its
      file deleted.
    range: >-
      Every file gets one, FITS files too. Each file read is also measured for the histogram and the statistics card,
      unless it was measured already. A file added afterwards has no proxy until you press it again.
    enabled: >-
      When a listed file has no proxy for the present settings and no optimization works. While it works, the table,
      **Add Files**, **Add Folder**, **Remove Selected**, **Clear All**, **Export Sequence** and the player are
      disabled.
    tradeoff: >-
      It takes processor time, memory and temporary disk: a proxy holds three 8-bit channels at the file's size, about
      61 MB for a frame of 20 megapixels. Proxies are not reduced in size.
    scope: >-
      Playback in **Autostretch** with these settings only. **Linear** never plays the proxies, and **Export Sequence**
      never reads them.
    persistence: >-
      The proxies are temporary, in a folder `ImageListAnalyzer_<id>` of the session's temporary folder, one folder for
      each set of settings. Each is named after its file with 16 hexadecimal digits of a hash of the file's path, so
      that files of one name in two folders keep apart. A proxy goes with its row, with **Clear All** and when the
      window closes, and never joins the recent files.
  - name: Optimization status
    object: showInfoLabel
    kind: readout
    default: No Optimization Needed
    effect: >-
      Says whether **Autostretch** plays proxies. "Images Already Optimized" means every file has a proxy of the present
      settings. "Images Not Optimized" means the files themselves play, each stretched as it is shown, also when the
      proxies hold other settings. "No Optimization Needed" stands for an empty list and for **Linear**.
    range: Its tooltip says why. It reports the proxies only, not whether playback reaches the rate you set.
  - name: Export Sequence
    kind: button
    default: ""
    effect: >-
      Opens the Export Sequence dialog over every listed file, in the table's order, whichever row is selected.
    range: It reads the original files, through a new temporary image list, never their proxies. It cannot export an `.anseq` image list.
    enabled: With a file in the list, while no optimization works.
    note: When the temporary list cannot be opened, a message titled "Export Sequence" gives the reason.
  - name: Add Files
    kind: button
    default: ""
    effect: >-
      Opens **Select Files** and adds the files you choose. Only their headers are read, in the background, so the
      window stays free. The pixels are read when a frame is shown, measured or optimized.
    range: >-
      A path listed already is passed over. A path that is no file, or a file whose header cannot be read, is skipped,
      and the log names it.
    tradeoff: Reading headers takes longer with many files and on slow or network storage.
    persistence: The dialog opens in the folder that **Add Files** or **Add Folder** of an Image List Analyzer used last. The list lasts until the window closes.
    note: >-
      When an import does not add every file, a message says how many it added, passed over and could not read:
      "Some files were passed over" when it passed some over, "Some files were skipped", with the first reason, when it
      could not read some.
  - name: Add Folder
    kind: button
    default: ""
    effect: Opens **Select Folder**, then **Add Folder Options**, and adds the folder's still images that pass the options.
    range: >-
      It takes the files whose extension is one of the still-image formats AfterNight opens, in the subfolders too
      when asked, then reads their headers and keeps those that pass. The folder is listed before the import starts,
      and the window waits while a large tree is listed.
    persistence: The folder is remembered for the next time. The options apply to this import only.
    note: A folder without such files shows "No Files Found" with "No supported image files found in the selected folder."
  - name: Remove Selected
    kind: button
    default: ""
    effect: Takes the selected row out of the list, with its proxy. The file stays on disk.
    enabled: With a file in the list, while no optimization works.
    tradeoff: There is no undo. Add the file again to bring it back.
  - name: Current Image
    kind: readout
    default: "0"
    effect: Shows the number of the selected row, counted from 1.
  - name: Total Images
    kind: readout
    default: "0"
    effect: Shows how many files the list holds.
  - name: Clear All
    kind: button
    default: ""
    effect: >-
      Empties the list at once, without asking: the table, the preview, the measurements and every proxy. An import
      under way stops, and none of its files is added afterwards.
    range: The files stay on disk. The table's order returns to **Date**, from the earliest.
    enabled: With a file in the list, while no optimization works.
    tradeoff: There is no undo.
  - name: Close
    kind: button
    default: ""
    effect: >-
      Closes the window at once. An import, a measurement or an optimization under way stops, and the window waits for
      none of them.
    range: >-
      The list, its order, the measurements and the proxies are discarded. An Export Sequence dialog the window opened
      closes with it, and its export stops without leaving a file.
    enabled: Also while the optimization works.
    persistence: The playback rate is kept for the next window.
  - name: Reset
    kind: button
    default: ""
    effect: Returns the playback rate to 1 frame per second, the window's one setting.
    range: It leaves the list, its order, the selection and the preview's display as they are.
```

```controls
window: The file table
controls:
  - name: File
    kind: readout
    default: ""
    effect: Shows the file's name. Its tooltip gives the full path.
    range: Sorted by the name, with capital letters before small ones.
  - name: Date
    kind: readout
    default: ""
    effect: >-
      Shows the time the header gives for the file's creation, else the start of its exposure, else the file's
      modification time, as `yyyy-MM-dd hh:mm:ss.zzz`.
    range: >-
      Times are given in UTC. A creation time read from a `DATE-LOC` card, the local time of the capture computer, is
      shown as it is written.
    tradeoff: A file whose header gives no time is placed by its modification time, which may be long after the capture.
  - name: Size
    kind: readout
    default: ""
    effect: Shows the width and the height, as `5544x3684`.
    range: A click on its header orders nothing.
  - name: Filter
    kind: readout
    default: ""
    effect: Shows the filter the header names, or `-`.
    range: Sorted by its text, with capital letters before small ones.
  - name: ISO/Gain
    kind: readout
    default: ""
    effect: Shows the ISO speed or the camera's gain, or `-`.
    range: Sorted by number. `-` comes before every number.
  - name: Offset
    kind: readout
    default: ""
    effect: Shows the camera's offset, or `-`.
    range: Sorted by number.
  - name: Exposure
    kind: readout
    default: ""
    effect: Shows the exposure in seconds, or `-`.
    range: Sorted by number.
  - name: CFA
    kind: readout
    default: ""
    effect: Shows `Yes` for a color-filter mosaic and `No` otherwise.
    range: Sorted with `No` before `Yes`.
  - name: Depth
    kind: readout
    default: ""
    effect: Shows `8 bit`, `16 bit`, `32 bit int`, `32 bit float` or `64 bit float`. Signed and unsigned integers share a label.
    range: Sorted by pixel type.
  - name: Column headers
    kind: action
    default: ""
    effect: >-
      A click on a header orders the list by its column, from the largest at the first click, and another click
      reverses the order.
    range: >-
      Rows that tie stay in **Date** order, from the earliest. No header shows which column the list is ordered by: a
      new list is in **Date** order, from the earliest.
    enabled: Not while the optimization works.
    scope: The preview, playback and an export follow the order. The selection stays on its file.
    persistence: Until the window closes. **Clear All** returns it to **Date**.
  - name: Rows
    kind: action
    default: ""
    effect: >-
      A click selects one row, and the preview, the summary and the histogram show its file. The arrow keys move the
      selection.
    range: >-
      One row at a time. There is no check box, rating or mark that keeps or rejects a frame, and no selection is
      saved.
    persistence: The first row is selected when a list is first filled.
```

```controls
window: The table's menu
controls:
  - name: Open In Editor
    kind: action
    default: ""
    effect: Opens the row's file in an image view of its own.
    scope: The view is a plain view of the file. It knows nothing of the list.
  - name: Remove
    kind: action
    default: ""
    effect: Takes the row out of the list, as **Remove Selected** does. The file stays on disk.
  - name: Delete From Disk
    kind: action
    default: ""
    effect: >-
      Asks "This will delete the selected file from the disk permanently. Are you sure you want to proceed?", and on
      **Yes** takes the row out of the list, then deletes the file.
    range: '**No** is the default button.'
    tradeoff: >-
      The file is gone for good, with no recycle bin. When it cannot be deleted, "Could not delete '<path>' from the
      disk." says so, and the row stays out of the list.
    enabled: The menu does not open while the optimization works.
```

```controls
window: Add Folder Options
controls:
  - name: Include subfolders (recursive)
    kind: check
    default: "On"
    effect: Takes the images of every folder inside the one you chose too. Off takes the folder's own files only.
    tradeoff: A large tree takes longer to list, and can bring in files that do not belong.
  - name: Frame Type
    kind: combo
    default: All
    options: [All, Light, Dark, Flat, Bias, Dark Flat]
    effect: >-
      Keeps the files whose header names that type, read without regard to case. **Light** keeps a type that contains
      LIGHT, **Dark** one with DARK and no FLAT, **Flat** one with FLAT and no DARK, **Dark Flat** one with both and
      **Bias** one with BIAS or OFFSET.
    range: A file whose header names no frame type counts as a light.
    tradeoff: A type the header misspells keeps the file out. Choose **All** when you are not sure of the headers.
  - name: Filter Name
    kind: field
    default: ""
    effect: >-
      Keeps the files whose filter equals the text, without regard to case or to spaces around it. Empty keeps every
      filter.
    range: '`Ha` does not match `Ha 7nm`, and a file whose header names no filter never matches a text.'
    note: The empty box shows `e.g. Ha, L, R, G, B`.
  - name: OK
    kind: button
    default: ""
    effect: Lists the folder and starts the import.
    persistence: The three options apply to this import only, and open at their defaults next time.
  - name: Cancel
    kind: button
    default: ""
    effect: Adds nothing.
```

```controls
window: Optimization progress
controls:
  - name: Progress
    kind: readout
    default: ""
    effect: Titled "Optimizing image list", it reads "Optimizing N images" and runs from 0 to 100 percent, a step for each file.
  - name: Cancel
    kind: button
    default: ""
    effect: Stops the optimization before the next file. The text reads "Canceling execution...".
    range: >-
      The proxies finished so far are kept and serve the next optimization, but the list is not optimized, so
      **Autostretch** plays the files themselves.
    note: >-
      A file that fails is logged and the others go on. At the end, "Image optimization incomplete" says how many
      failed, with the first reason. A cancel shows no message.
```

```controls
window: Export Sequence dialog
controls:
  - name: Output Format
    kind: combo
    default: SER
    options: [SER, AVI (RAW), MP4]
    effect: >-
      **SER** writes a SER file, **AVI (RAW)** an uncompressed AVI, and **MP4** a compressed video in the format
      **Codec** names.
    range: >-
      SER keeps 8-bit frames in 8 bits and stores every other pixel type as 16-bit unsigned integers, a mosaic as a
      mosaic, and each frame's capture time. AVI and the videos store 8-bit frames, a mosaic as gray. Only grayscale,
      mosaic and RGB frames can be written.
    tradeoff: >-
      SER suits a sequence to process further. AVI keeps every 8-bit sample but is large. A video is small and plays
      anywhere, but loses detail and keeps no header cards. No format keeps the history.
    persistence: The dialog opens on the last format, codec and **Resolution** used.
  - name: Codec
    kind: combo
    default: h264
    options: [h264, h265, WEBM]
    effect: Chooses the video's coding. **h264** and **h265** write an MP4 file, **WEBM** a WebM file coded in VP9.
    enabled: With **Output Format** at **MP4**.
    range: A change of format or codec changes the file name's extension to `.ser`, `.avi`, `.mp4` or `.webm`.
    tradeoff: All three lose detail, and coding takes processor time for every frame.
  - name: Output File Name
    kind: field
    default: ""
    effect: Shows the file the export writes, which **...** chooses. It cannot be typed into.
    enabled: '**OK** stays disabled until it names a file.'
    persistence: The folder is remembered for the next export, not the name.
  - name: "..."
    kind: button
    default: ""
    effect: Opens **Save Sequence...** to choose the file, with the extension of the format chosen.
  - name: FPS
    kind: spinbox
    default: "1.00"
    widget: {min: 1, max: 60, step: 0.3, decimals: 2}
    effect: Sets the rate written into an AVI or a video. Frames are written as they are, none added or merged.
    range: A SER has no field for a rate. It keeps each frame's capture time instead.
    persistence: >-
      It opens at the rate of the player's **FPS**, 1 unless you changed it. The dialog keeps no rate of its own.
  - name: Autostretch
    kind: check
    default: "Off"
    effect: >-
      Stretches each frame with the preview's **Autostretch Settings** as they were when you chose **Export
      Sequence**, their saturation, brightness and contrast included. Each frame is measured for its own stretch.
    tradeoff: The file holds stretched samples, for watching, not for measuring. The files on disk do not change.
    persistence: Off each time the dialog opens.
  - name: Debayer CFA
    kind: check
    default: "Off"
    effect: Debayers each mosaic frame by the **Demosaic Algorithm** of the settings, so that the file holds color frames.
    enabled: Shown only when the list's first file is a mosaic.
    tradeoff: The file then holds three channels in place of the mosaic, which cannot be undone in it.
    persistence: Off each time the dialog opens.
  - name: Resize
    kind: check
    default: "Off"
    effect: Resizes each frame to **Width** and **Height** with Lanczos interpolation. Off keeps the frames' own size.
    range: >-
      MP4 and WebM need an even width and height. An odd last column or row is cut away, keeping the top left, with
      **Resize** on or off.
    tradeoff: Resizing adds time to every frame.
    persistence: Off each time the dialog opens.
  - name: Resolution
    kind: combo
    default: Preserve Ratio
    options: [Preserve Ratio, Free Ratio, Full HD, 4K, 8K]
    effect: >-
      **Preserve Ratio** keeps **Width** and **Height** in the first frame's proportion, **Free Ratio** sets them
      apart, and **Full HD**, **4K** and **8K** ask for 1920 x 1080, 3840 x 2160 and 7680 x 4320.
    range: Choosing **Preserve Ratio** or **Free Ratio** returns **Width** and **Height** to the first frame's size.
    enabled: With **Resize** on.
  - name: Width
    kind: spinbox
    default: The first frame's width
    widget: {min: 4, step: 1}
    effect: Sets the width of the frames written.
    range: Up to twice the first frame's width, or 7680 when that is more.
    enabled: With **Resize** on, and **Preserve Ratio** or **Free Ratio**.
    tradeoff: With **Free Ratio**, a proportion other than the frame's leaves black bars under **Fit**, and distorts the picture only under **Stretch**.
  - name: Height
    kind: spinbox
    default: The first frame's height
    widget: {min: 4, step: 1}
    effect: Sets the height of the frames written.
    range: Up to twice the first frame's height, or 4320 when that is more.
    enabled: With **Resize** on, and **Preserve Ratio** or **Free Ratio**.
  - name: Fill mode
    kind: combo
    default: Fit
    options: [Fit, Fill, Stretch, Center]
    effect: >-
      Places each frame in the size of **Full HD**, **4K** or **8K**. **Fit** scales it to fit inside and pads the rest
      with black, **Fill** scales it to cover the whole and crops what overflows, **Stretch** scales width and height
      apart, and **Center** keeps the frame's size and centers it, padded or cropped.
    enabled: With **Full HD**, **4K** or **8K**.
    tradeoff: '**Fit** leaves black bars, **Fill** and **Center** can crop, and **Stretch** distorts.'
  - name: OK
    kind: button
    default: ""
    effect: >-
      Exports every listed file, debayered, stretched and resized as asked, in that order. A progress window titled
      "Exporting Sequence..." follows the frames, and the dialog's controls are disabled while it works.
    range: >-
      On success, "Sequence exported successfully!" appears and the dialog closes. On failure, "Export Sequence Error"
      gives "Could not export sequence:" and the reason, and the dialog stays open.
    tradeoff: >-
      The time grows with the frames, their size and each change asked. Autostretch measures every frame, and the
      video codings cost processor time.
    persistence: The file appears at its name only when its last frame is written. A stopped or failed export leaves no file.
  - name: Reset
    kind: button
    default: ""
    effect: Returns **Resolution** to **Preserve Ratio**, **Width** and **Height** to the first frame's size, and turns **Resize** off.
    range: It leaves the format, the codec, the file, **FPS**, **Autostretch** and **Debayer CFA** as they are.
  - name: Cancel
    kind: button
    default: ""
    effect: Closes the dialog without writing.
    range: >-
      While an export works, the progress window's **Cancel** stops it. Closing the dialog stops it too, and the dialog
      closes when the export has ended.
```

## How it works

1. An import reads the header of each new file in the background, without its pixels, and adds the files that pass
   in the order of the table's sorted column.
2. The table and the preview share one list. The window writes it as a temporary image list of the files, or of their
   proxies, which the preview plays as a sequence described by its first file. Every sort, addition or removal writes
   it again.
3. The first time a file's frame is shown, the file is read and measured in the background, one file at a time, for the
   histogram and the statistics card. The result is kept while the file is listed.
4. **Optimize For Fast Play** writes, for each file, a proxy that is the file as the
   [Autostretch](../../reference/glossary.md#autostretch) display shows it, so that playback neither debayers nor
   [stretches](../../reference/glossary.md#stretch). A proxy serves only the settings it was written with.
5. **Export Sequence** reads the original files in the table's order, changes each frame as the dialog asks, and adds
   it to the file, which appears only when the last frame is in.

### Boundaries

- No measure of quality: no stars, FWHM, noise figure or rank, no threshold, no rating or flag, no group. Sorting by
  header facts and removing rows by hand are the only ways to choose files.
- Still files only. A SER, AVI or video file is no input, and the export writes no image list.
- No handoff: the list, its order and its removals do not reach Image Integration, which does not read the export
  either. Add the files you keep there yourself.
- No header is written, and no list, order or rating is saved beside the files.

### Worked example

The 35 lights of NGC 253 used here are raw color-filter frames of 180 s from a QHY183C, recorded in two runs of one
night. Choose **Add Files** and select all 35. The table puts them in **Date** order, from `2023-10-10 00:19:34.462`
to `2023-10-10 02:29:26.716`: the local times of the headers' `DATE-LOC` cards, as in the file names. Every row
reads `5544x3684`, `-`, `11`, `30`, `180`, `Yes` and `16 bit`. Only **File** and **Date** tell these files apart, and
a sort by any other column falls back on the date.

The first row is selected. The summary names the camera and the capture software, and the preview shows the frame
debayered and stretched. The histogram shows three narrow peaks near 0.05, the green one a little to the right, near
0.06: the sky background of a linear frame. The statistics card's **Median** reads about 3,500, in the file's own
16-bit units.

Choose **Optimize For Fast Play**. It writes 35 proxies of about 61 MB each, 2.1 GB of temporary disk, and the status
then reads "Images Already Optimized". Type 5 in the player's **FPS** box and play the list.

To keep the night as a short film, choose **Export Sequence**. Choose **MP4** and **h264**, tick **Autostretch**,
**Debayer CFA** and **Resize**, choose **Full HD** and **Fit**, and leave **FPS** at 5. Each frame is debayered,
stretched and scaled to 1625 x 1080 pixels between black bars of 147 and 148 pixels, and the video lasts 7 seconds.

## Starting points

| Situation | Start | Then adjust |
| --- | --- | --- |
| A folder of mixed frames | **Add Folder** with **Frame Type** at **All** and **Filter Name** empty | Sort by **Filter** or **Exposure**, and remove what does not belong. A strict option keeps out files whose header is wrong. |
| Finding the capture order | **Date**, from the earliest | Compare a doubtful row's times in the summary, and sort by **File** where the names hold the time. |
| Looking through a night quickly | **Optimize For Fast Play**, then play at 5 to 10 FPS | Step frame by frame where something changes. Without proxies, look at single frames. |
| A first export | **SER**, with **Autostretch**, **Debayer CFA** and **Resize** off | Turn on one change at a time, and keep the original files. |

## When it goes wrong

!!! danger "Delete From Disk cannot be undone"
    It deletes the file itself, with no recycle bin. Use **Remove** to take a frame out of the list and leave the file.

!!! warning "The list is not kept"
    Closing the window discards the list, its order and the proxies at once. Add the files you keep to the stacking
    window, or export them, before you close it.

!!! warning "An export is for watching"
    An MP4, a WebM or a stretched export loses samples, [header cards](../../reference/glossary.md#header-card) and
    [history](../../reference/glossary.md#history). Stack the original files, never the export.

"Some files were skipped", "Some files were passed over", or files of a folder missing from the table
:   **Cause.** Their extension is not a still format, **Include subfolders (recursive)** was off, **Frame Type** or
    **Filter Name** kept them out, they were listed already, or their header could not be read.
:   **Fix.** Import again with **All**, an empty **Filter Name** and subfolders on. The message and the log name the
    files that could not be read. The window cannot correct a header.

The Date order does not match the capture order
:   **Cause.** Some headers give no time, so their files show their modification time, or a header gives a local time.
:   **Fix.** Compare the times in the summary, and sort by **File** when the names hold the time.

Playback in Autostretch is slow, and the status reads "Images Not Optimized"
:   **Cause.** A file has no proxy for the present **Autostretch Settings**: it was added after the optimization, the
    settings changed, or its proxy failed.
:   **Fix.** Choose **Optimize For Fast Play**. When "Image optimization incomplete" appears, read it and the log, and
    remove the files that fail.

The preview looks different after the optimization
:   **Cause.** **Autostretch** now shows the 8-bit proxies, while **Linear** shows the files.
:   **Fix.** Judge the samples in the file itself: right-click its row and choose **Open In Editor**.

The export holds files you meant to leave out
:   **Cause.** **Export Sequence** writes every row of the table, not the selected one.
:   **Fix.** Remove those rows first, and check **Total Images** before you export.

The exported frames are brighter, in color or cropped
:   **Cause.** **Autostretch**, **Debayer CFA** or **Resize** was on, or a video cut an odd last column or row.
:   **Fix.** Export again with them off, and give a video an even width and height.

The preview or the export fails on one file
:   **Cause.** Its size, channel count or pixel type differs from the first file's, which describes the list.
:   **Fix.** Remove it, or make a list of files that match.

"Could not delete '<path>' from the disk."
:   **Cause.** The file could not be deleted, and its row was already taken out of the list.
:   **Fix.** Look for the file in your file manager, and delete it there if you still mean to.

## Related

- [Image Registration](../registration-alignment/image-registration.md), for each frame's stars, FWHM, SNR and
  eccentricity, and to leave weak frames out.
- [Image Integration](image-integration.md) and [Image Integration Lite](image-integration-lite.md), to stack the
  files you keep, added there from their folder.
- [Sequences](../../interface/sequences.md), for the player and image lists.
- [Saving and Exporting](../../interface/saving-and-exporting.md) and [File Formats](../../reference/file-formats.md),
  for the Export Sequence dialog a sequence's view opens too, and the formats it writes.
- [Image Statistics](../../interface/image-statistics.md) and [Histogram Analysis](../../interface/image-histogram.md),
  for exact figures of a file opened with **Open In Editor**.
- [Process Windows](../../interface/process-windows.md), for the preview's zoom, ++esc++ and the window's kept size.

## Credits

- Color-filter frames are debayered by methods adapted from [RawTherapee](https://github.com/Beep6581/RawTherapee)'s
  demosaic code as [librtprocess](https://github.com/CarVac/librtprocess) packages it, under GPL-3.0-or-later.
  **About > About AfterNight** credits it.
- [FFmpeg](https://ffmpeg.org), by the FFmpeg developers, under the GNU LGPL 2.1 or later and, as built with its GPL
  encoders, the GNU GPL 2 or later, writes the MP4 and WebM files.
- [x264](https://code.videolan.org/videolan/x264) and [x265](https://bitbucket.org/multicoreware/x265_git/), by their
  project authors, under GPL-2.0-or-later, code H.264 and H.265, and [libvpx](https://chromium.googlesource.com/webm/libvpx/),
  by the WebM project authors, under BSD-3-Clause, codes VP9.
- [SER format description, version 3b](https://free-astro.org/images/5/51/SER_Doc_V3b.pdf), for the SER container and
  its timestamp trailer.
