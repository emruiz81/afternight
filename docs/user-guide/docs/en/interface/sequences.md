---
kind: interface
summary: >-
  A sequence view shows a SER capture, a video or an image list one frame at a time, with a player under the picture.
  New Sequence gathers still images into a sequence.
menu: File > New Sequence...
rewritten: true
---
# Sequences

A [sequence](../reference/glossary.md#sequence) is a series of frames of one size that AfterNight opens in one view and
shows one frame at a time: a [SER](../reference/glossary.md#ser) capture, a video, or an image list, which is an
`.anseq` file that names image files in order. A player under the picture steps through the frames and plays them.
The view reads one frame at a time, so a long capture never has to fit in memory. Most processes work on every frame,
and **New Sequence** gathers still images into a sequence of their own.

```figure
owed: true
image: ../assets/images/interface/sequences.webp
alt: >-
  The AfterNight main window with one sequence view, its tab named jupiter_8bit. Under the tab, the view's toolbar:
  zoom buttons, a box reading RGB, a box reading Linear, and the history and analysis icons. Below, the disc of Jupiter
  on black, enlarged to the height of the view. Under the picture, a row of four buttons - play, stop, previous and
  next - a long slider with its handle at the left end, and a box reading 1/4965. The status bar reads SER SEQUENCE,
  448x448 (RGB), 1/4965, 82.46 fps, 8 bit unsigned integer and 2.78 GB.
caption: >-
  An 8-bit SER video of Jupiter in its view, on its first frame, with the player under the picture.
pins:
  - {n: 1, x: 11.0, y: 9.8}
  - {n: 2, x: 12.0, y: 50.0}
  - {n: 3, x: 2.5, y: 91.0}
  - {n: 4, x: 15.0, y: 91.0}
  - {n: 5, x: 96.5, y: 91.0}
  - {n: 6, x: 16.2, y: 97.6}
```

## What you see

1. **The tab.** The sequence's name, which is its file's name without the extension. A sequence made in AfterNight
   carries the asterisk of unsaved work until you save it.
2. **The picture.** The frame shown. The view's toolbar zooms it, shows one channel or applies a
   [display stretch](../reference/glossary.md#display-stretch) as it does for an image, and has no astrometry buttons
   for a sequence.
3. **The player's buttons.** **Play sequence**, **Stop sequence**, **Previous frame** and **Next frame**.
4. **The frame slider.** Drag it, or click its track, to show another frame.
5. **The frame box.** The frame shown and the count of frames, as `1/4965`. Type a number to go to that frame.
6. **The status bar.** The kind of sequence, such as `SER SEQUENCE`, the frame's size, the frame shown and the count,
   the frame rate, the [pixel type](../reference/glossary.md#pixel-type) and the memory of every frame together.

## What you can do

### Open a sequence

Choose **File > Open**, the toolbar's **Open** or ++ctrl+o++, and select a `.ser`, `.anseq` or video file. The view
opens on the first frame, fitted to the window, and keeps the file open until the view closes. The file is never
changed.

When the frames are a [CFA](../reference/glossary.md#cfa) mosaic, "Open CFA file" asks first. **Yes** makes the player
[debayer](../reference/glossary.md#debayer) each frame as it shows it, by the **Demosaic Algorithm** of the Raw Images
settings, and processes read the frames the same way. **No** shows the mosaic as recorded.
[Supported Formats](../getting-started/supported-formats.md) lists the containers and video codecs AfterNight reads.

### Play and step through the frames

- Choose **Play sequence** to play from the frame shown. The same button pauses, and playback stops at the last frame.
- Choose **Stop sequence** to stop and go back to the first frame.
- Choose **Previous frame** or **Next frame**, drag the slider or type in the frame box to show one frame. Each pauses
  playback.
- Hold the left button over the picture to read values with the lens of the [Image Viewer](image-viewer.md). Playback
  pauses while you hold it.

The player only shows frames. It ranks, aligns and stacks nothing, and the view has no way to keep the best frames of
a planetary video. Image Stacking and Image Integration take still files, not a sequence: save the sequence as an image
list (`.anseq`), which writes each frame as a [FITS](../reference/glossary.md#fits) file in a folder beside it, and add those files there.
[Planetary Imaging](../workflows/planetary-imaging.md) follows that path.

### Make a sequence from still images

1. Choose **File > New Sequence...**, or **New sequence** on the Dashboard. No image needs to be open. The window opens
   on **Main Settings**, with an empty table.
2. Choose **Add Files** and select two or more images of one size, pixel type and channel count. Their headers fill
   the table in the background.
3. Select a row to see its preview, histogram and summary, and move it with the arrows. The table's order is the order
   of the frames.
4. On **Advanced Settings**, choose the **Sequence Type**, then set **FPS** and **Sequence Name**.
5. Choose **Apply**. A progress window follows the frames, then the window closes and the new sequence opens in a view,
   unsaved, on its first frame.

```figure
owed: true
image: ../assets/images/interface/sequences-new-sequence.webp
alt: >-
  The New Sequence window on its Main Settings tab. On the left, a table of five FITS files with the columns Path,
  File, Date, Size, ISO/Gain, Offset, Exposure, CFA and Depth, its first row selected, and under it Add Files, Remove
  Selected, two arrow buttons, Current Frame 1, Total Frames 5 and Clear All. On the right, the preview of a dark frame
  of stars, a histogram with three peaks near its left edge, and the file's summary. Apply and Close at the bottom.
caption: >-
  New Sequence with five raw lights of NGC 253 added, the first one selected.
```

### Keep a sequence, or part of it

A sequence that New Sequence, Extract Sequence, a conversion or a process makes is a
[temporary sequence](../reference/glossary.md#temporary-sequence): a file in the session's temporary folder whose name
starts with `SEQ_SWAP`. It is lost with the session unless you save it with **File > Save as...** or export it with
**Image > Sequence > Export...**, as [Saving and Exporting](saving-and-exporting.md) describes.

- **Image > Sequence > Extract Sequence** copies a range of frames into a new sequence: see
  [Extract Sequence](../processes/utility/extract-sequence.md).
- **Image > Sequence > Extract Frame** opens the frame shown as an image, and **Convert** writes the sequence again as a
  SER or an image list: see [Non-Interactive Processes](../processes/utility/non-interactive-processes.md).

None of them changes the file the sequence came from.

### Process a sequence

Make the sequence the target of a process window and choose **Apply**. Every frame is processed, since the player
selects no frames. To process part of a capture, extract it first. The frames are read as the view shows them,
debayered when the view debayers its frames. The result replaces the sequence in the view, with one step of
[history](../reference/glossary.md#history), and [Process Windows](process-windows.md) describes the run.

## Controls

### The player

```controls
window: The player
controls:
  - name: Play sequence
    kind: toolbutton
    default: ""
    effect: Plays the frames from the one shown, at the sequence's rate. While it plays, the button shows a pause sign and pauses.
    range: The rate the status bar shows, rounded to a whole number of frames per second and held between 1 and 60. A sequence that states no rate plays at 1.
    enabled: With a sequence that holds a frame. The whole player is disabled while a process works on the sequence.
    tradeoff: Each frame is read in the background, shown when it lands, and only then is the next one asked for. A tick that comes while a frame is on its way is skipped, so a large frame, a video, a slow disk or the Autostretch display, which measures every frame, plays slower than the rate. Skipping shows fewer frames and removes none.
    persistence: Playback stops at the last frame and does not loop. **Play sequence** there starts again from the first frame.
    note: A view has no box for the rate. The player of Image List Analyzer has one.
  - name: Stop sequence
    kind: toolbutton
    default: ""
    effect: Stops playback and shows the first frame.
    enabled: Only while the sequence plays. A pause or a step disables it again.
  - name: Previous frame
    kind: toolbutton
    default: ""
    effect: Pauses playback and shows the frame before.
    enabled: Not on the first frame.
  - name: Next frame
    kind: toolbutton
    default: ""
    effect: Pauses playback and shows the next frame.
    enabled: Not on the last frame.
  - name: Frame slider
    kind: slider
    default: "1"
    widget: {min: 1, step: 1}
    effect: Pauses playback and shows the frame you drag it to. A click on the track jumps there.
    range: From 1 to the count of frames. Every control and message counts frames from 1.
    enabled: Disabled while a process works on the sequence. A sequence without frames shows 0.
    tradeoff: Only the frame last asked for is shown. A frame asked for while another is read replaces it, so a fast drag shows fewer frames than it passes.
    persistence: The view keeps the frame shown, which **Extract Frame** copies.
  - name: Frame box
    kind: spinbox
    default: "1"
    widget: {min: 1, step: 1}
    effect: Shows the frame and, after a slash, the count of frames. Type or step a number to show that frame.
    range: As the slider, which it follows.
```

### Opening a sequence

```controls
window: Opening a sequence
controls:
  - name: A SER file
    kind: action
    default: ""
    effect: Opens a `.ser` capture of gray, mosaic or color frames of 8 or 16 bits, and reads each frame by its position in the file. AfterNight reads SER and image lists itself, without FFmpeg.
    range: A recording that stopped before it wrote its frame count opens with the complete frames it holds. A cyan, yellow and magenta mosaic opens as gray samples.
    note: A SER has no field for a frame rate. It plays at the rate its timestamps give, one frame per mean interval between the earliest and the latest time it records, in whole ticks of 100 nanoseconds. With fewer than two distinct times it states no rate, the status bar shows `0.00 fps`, and the player plays at 1 frame per second.
  - name: An image list
    kind: action
    default: ""
    effect: Opens an `.anseq` file, a short XML list of image files in order, and decodes each frame from its file when it is shown.
    range: A path in the list is taken as written when it is absolute, and from the list's own folder otherwise. The rate is the list's own, read the same whatever the language settings of the computer.
    tradeoff: The list holds no pixels. Every file it names must stay where it is. The frames keep the pixel type of their files.
    note: A list is refused when AfterNight cannot read it - a version other than 1, elements nested more than 4,096 deep, or a frame path that is empty, not UTF-8, holds a NUL or is too long.
  - name: A video
    kind: action
    default: ""
    effect: Opens an `.avi`, `.mp4`, `.mov`, `.mkv`, `.webm`, `.mpg` or `.mpeg` file through FFmpeg, decoded on the processor, in the order its frames are shown. Sound is never read.
    range: Three things must agree. The extension makes AfterNight offer the file. AfterNight then admits only AVI, MP4 and MOV, Matroska and WebM, MPEG program and transport streams and NUT containers, with the codecs Supported Formats lists, even where FFmpeg could read another. Last, the stream itself must decode.
    tradeoff: Opening reads every packet header once and keeps an index, so a jump lands on exactly the frame asked for. No decoded frame is kept, so a step back decodes again from the keyframe before it and costs more than playing forward.
    note: A color stream is converted to RGB through the matrix and range it states, or as BT.601 at limited range when it states neither. Frames of 8 bits or fewer become 8-bit RGB, deeper ones 16-bit RGB. Gray, mosaic and RGB frames arrive as stored. A palette is looked up into RGB, and a palette left empty, as some AVI files hold, is read as gray. FFmpeg opens no other file and no network address for a video.
```

### New Sequence

```controls
window: Main Settings
controls:
  - name: The file table
    kind: readout
    default: ""
    effect: Lists the files of the new sequence, in its order, with the columns Path, File, Date, Size, ISO/Gain, Offset, Exposure, CFA and Depth. Selecting a row shows its file in the preview, the histogram and the summary.
    range: "**Date** is the creation time the header states, else the start of its exposure, in UTC as `2023-10-10 00:19:34.462`, else the file's modification time. **Size** reads as `5544x3684`. **ISO/Gain**, **Offset** and **Exposure** give a number or `-`, **CFA** `Yes` or `No`, and **Depth** `8 bit`, `16 bit`, `32 bit int`, `32 bit float` or `64 bit float`."
    tradeoff: ISO or gain, offset and exposure are shown, never compared. A click on a header sorts nothing. The list copies no file.
    persistence: Until the window closes. Each **File > New Sequence...** opens another window, with a list of its own.
    note: The window's header reads "Build a sequence from selected frames with configurable debayering, output type and frame rate." ++ctrl+n++, which the Dashboard names, is no shortcut of New Sequence.
  - name: Add Files
    kind: button
    default: ""
    effect: Opens **Select Files** and adds the image files you choose. Their headers are read in the background, without their pixels, and their rows come in batches of 50.
    range: A path listed already is passed over. Reading stops at the first file whose header cannot be read. The files before it are added, and "Error loading image metadata" names it.
    persistence: The file window opens in the folder New Sequence chose from last, else in the folder you last opened from.
  - name: Remove Selected
    kind: button
    default: ""
    effect: Takes the selected rows out of the list. The files stay on disk.
    enabled: With a file in the list.
  - name: Move frame up
    kind: button
    default: ""
    effect: Swaps the selected row with the one above it.
    enabled: With a row selected below the first.
  - name: Move frame down
    kind: button
    default: ""
    effect: Swaps the selected row with the one below it.
    enabled: With a row selected above the last.
  - name: "Current Frame:"
    kind: readout
    default: "0"
    effect: Shows the number of the selected row, counted from 1.
  - name: "Total Frames:"
    kind: readout
    default: "0"
    effect: Counts the files in the list.
  - name: Clear All
    kind: button
    default: ""
    effect: Empties the list and clears the preview, the histogram and the summary. The files stay on disk.
    enabled: With a file in the list.
  - name: Preview
    kind: readout
    default: ""
    effect: Shows the selected file with every channel at full scale and no stretch, a mosaic debayered by a fast method. Its quick zoom works once a file is shown.
    tradeoff: Each selection reads the whole file. A linear image looks dark here.
  - name: Histogram
    kind: readout
    default: ""
    effect: Shows how the selected file's samples spread, with its clipping shares once the list holds a file.
  - name: Summary
    kind: readout
    default: The image list is empty
    effect: Shows the selected file's report - the file, the image and the camera facts its header states.
```

```controls
window: Advanced Settings
controls:
  - name: Debayer CFA images
    kind: check
    default: "On"
    effect: Debayers mosaic files by the **Demosaic Algorithm** of the Raw Images settings. A **SER Sequence** stores them as color frames. An **Image List** keeps its files as they are, and its view debayers each frame as it shows it.
    scope: Files of one channel. Other files pass unchanged. It sits under **Input Settings**.
    tradeoff: A debayered SER holds three channels in place of the mosaic, which it no longer keeps. A list debayered as it plays costs time at every frame.
    persistence: Until the window closes, as every setting of the window.
  - name: Bayer Pattern
    kind: combo
    default: Auto
    options: [Auto, RGGB, BGGR, GBRG, GRBG]
    effect: "**Auto** debayers each file under the pattern its header states and leaves a file that states none as it is. A pattern debayers every file of one channel under it."
    enabled: Disabled when the window opens, although **Debayer CFA images** is on. Turn the check box off and on again to enable it.
    scope: A **SER Sequence** only. An **Image List** ignores it.
    tradeoff: A wrong pattern swaps the colors. An X-Trans file is refused under a pattern and debayered under its own with **Auto**.
  - name: Sequence Type
    kind: combo
    default: Image List
    options: [Image List, SER Sequence, FITS Sequence]
    effect: "**Image List** writes a small `.anseq` that names the files where they are, by their full paths. **SER Sequence** decodes every file into one SER file."
    range: A SER stores gray, mosaic or color frames, 8-bit frames as 8 bits and every other type as 16-bit unsigned integers, with each frame's exposure start, or unknown for a file that states none. **FITS Sequence** is shown and cannot be chosen, and no AVI is offered.
    scope: The sequence is written in the session's temporary folder, as `SEQ_SWAP<token>.anseq` or `.ser`. It sits under **Output Settings**.
    tradeoff: A list takes almost no space and loses a frame when its file moves. A SER stands alone and takes the space of every frame.
    note: A list states the first file's size, channels and mosaic, the rate and the time it was made. Each frame states the rest when it is read.
  - name: FPS
    kind: spinbox
    default: "1.00"
    widget: {min: 0.1, max: 200, step: 1, decimals: 2}
    effect: Sets the rate the new sequence plays and exports at. It adds and removes no frame.
    range: The player plays it rounded to a whole number from 1 to 60.
    persistence: A list stores it, with four significant digits. A SER stores no rate. Its view plays at the rate the frames' exposure starts give, and at this one only when they give none, while the view is open. Opened again, the file has only the rate of its exposure starts, or none.
  - name: Sequence Name
    kind: field
    default: NEW_SEQUENCE
    effect: Names the new view.
    range: Spaces at the ends are removed, an empty name reads `NEW_SEQUENCE`, and a name another view has gets `_1`, `_2` added.
  - name: Apply
    kind: button
    default: ""
    effect: Checks the list, writes the sequence and opens it in a new view, on its first frame and fitted to the window. The window then closes.
    range: Every file must have the first file's width and height, then its pixel type and channel count. Otherwise nothing is written, and a message says which differs.
    enabled: With two files or more in the list, while no sequence is being made.
    tradeoff: A SER reads and writes every file, which takes time and disk space. The free space is not checked first.
    persistence: The view is unsaved, and its history starts with `New sequence manually created from image list`. A list's view debayers its frames when **Debayer CFA images** is on.
    note: A failure shows "Error creating sequence" with "An exception occurred when creating sequence:" and the reason, and the window stays open with its list.
  - name: Close
    kind: button
    default: ""
    effect: Closes the window and discards the list.
    enabled: Disabled while a sequence is being made. Closing the window another way stops the reading, the preview and the run without waiting, and nothing is kept.
```

```controls
window: Create Sequence
controls:
  - name: Progress
    kind: readout
    default: ""
    effect: Reads "Creating SER Sequence <name>..." or "Creating an image sequence <name>..." and runs from 0 to 100 percent, a step for each frame of a SER. A list is written at once.
  - name: Cancel
    kind: button
    default: ""
    effect: Stops the run before the next frame. The SER is written privately and appears only when its last frame is in, so nothing is left.
    persistence: The New Sequence window stays open with its list, and no message is shown.
```

## When it goes wrong

"Some images in the list have different size"
:   **Cause.** A file's width or height differs from the first file's.
:   **Fix.** Remove that file, or crop or resize the files to one size before you make the sequence.

"Some images in the list have different color depth"
:   **Cause.** A file's pixel type or channel count differs from the first file's.
:   **Fix.** Convert the files to one pixel type, and all to gray or all to color, then add them again.

"Could NOT load image" and a path, in a box titled "Error loading image metadata"
:   **Cause.** That file's header could not be read, and the files after it in your choice were not added.
:   **Fix.** Leave the file out, or convert it, and add the remaining files again.

"Could NOT load file" when you open a sequence
:   **Cause.** The list is malformed or names a missing file, the video's container or codec is not one AfterNight
    admits, or the file is damaged.
:   **Fix.** Read the reason in the message. Make the list again with **File > New Sequence...**, or have the program
    that made the video write a copy as SER, or as H.264 in MP4.

A box titled "Sequence Player" while you play or step
:   **Cause.** A frame could not be read: a file the list names is gone or unreadable, or a video frame is damaged.
    Playback stops.
:   **Fix.** Put the file back where the list names it, or step past the frame.

A SER plays at 1 frame per second, and the status bar shows `0.00 fps`
:   **Cause.** A SER has no field for a rate, and its frames record fewer than two distinct times, as a SER converted
    from a video does.
:   **Fix.** Step through the frames, or export the sequence to AVI or a video at the rate you want.

## Related

- [Image Viewer](image-viewer.md), for the toolbar, the lens and the status bar around the player.
- [Saving and Exporting](saving-and-exporting.md), to keep a sequence as SER, an image list, AVI or a video.
- [Extract Sequence](../processes/utility/extract-sequence.md), to keep a range of frames.
- [Non-Interactive Processes](../processes/utility/non-interactive-processes.md), for **Extract Frame**, **Convert** and
  the commands that work on every frame.
- [Supported Formats](../getting-started/supported-formats.md), for the files and video codecs AfterNight opens.
- [Planetary Imaging](../workflows/planetary-imaging.md), for the work on a planetary video.

## Credits

- [SER format description, version 3b](https://free-astro.org/images/5/51/SER_Doc_V3b.pdf), for the SER container and
  its timestamp trailer.
- [FFmpeg](https://ffmpeg.org), by the FFmpeg developers, under the GNU LGPL 2.1 or later and, as built with its GPL
  encoders, the GNU GPL 2 or later. It decodes the video containers.
- [FFmpeg Formats Documentation](https://ffmpeg.org/ffmpeg-formats.html), for the video containers.
