---
kind: interface
summary: >-
  Image and Sequence Summary reports what an image or a sequence and its file state - size, channels, pixel type,
  mosaic, camera, exposure and dates - and copies the report as text.
menu: Image > Information > Summary
---
# Image and Sequence Summary

The summary gathers the facts of the image a view shows, and of the file behind it, into one report you can read and
copy: its size and format, then what its header says of the camera and the exposure. A [SER](../reference/glossary.md#ser)
video or an image list gets a report of the whole [sequence](../reference/glossary.md#sequence) instead. The report is built from
what AfterNight holds in memory and from what the file system says of the file. It never reads the pixels, and it
changes nothing.

```figure
owed: true
image: ../assets/images/interface/image-summary.webp
alt: >-
  The Image Summary Viewer window. Under the title bar, the heading Image and Sequence Summary and a line about
  acquisition metadata. A card titled Summary Report holds a scrolled list of facts under section titles: the end of
  Image General Info, with the resolution 5544x3684, one channel, 16 bits unsigned integer, Grayscale, the CFA pattern
  RGGB and 38.96 MB, then Image Detailed Info, with the camera QHY183C, Gain 11, Offset 30, the date 2023-10-10
  00:19:34, 180 sec, 21 C and N.I.N.A. as the software. Copy to Clipboard and Close stand at the bottom right.
caption: >-
  The summary of a duplicate of a raw NGC 253 light frame, scrolled to its acquisition facts.
pins:
  - {n: 1, x: 65.0, y: 3.0}
  - {n: 2, x: 23.0, y: 23.5}
  - {n: 3, x: 32.0, y: 50.0}
  - {n: 4, x: 62.0, y: 94.0}
```

## What you see

1. **The title.** It names the view the report describes, as `Image Summary Viewer <M42>`.
2. **The report.** Sections of facts, one under the other. Scroll for the sections below.
3. **A section.** Its title, then one line for each fact, the label at the left and the value at the right. Select a
   value with the mouse to copy it alone.
4. **Copy to Clipboard and Close.** Copy the whole report as text, and close the window.

## What you can do

### Read an image's facts

1. Make the view active. For a sequence other than a SER or an image list, pause the player on the frame you want.
2. Choose **Image > Information > Summary**, choose **Summary** on the view's toolbar, or press ++ctrl+shift+m++ over
   the view.
3. Check **Image General Info** first: the size, the channels, the [pixel type](../reference/glossary.md#pixel-type),
   the mosaic and the memory. Then read **Image Detailed Info**, which says what the file states of the camera and the
   exposure.

A value shown as `-` means that nothing states it: AfterNight guesses no camera default. The acquisition facts are what
the file claims, not measurements. On the raw NGC 253 [light frame](../reference/glossary.md#light-frame), **Camera
Model** reads QHY183C, **Gain** 11, **Offset** 30 and **Exposure Time** 180 sec.

### Read the dates right

**Image Creation Date** is the date the file states, read as UTC and shown to the second. A
[FITS](../reference/glossary.md#fits) file that states its local time in `DATE-LOC` and no `DATE` has that local time
shown as written, with no offset. The NGC 253 frame reads 2023-10-10 00:19:34, while its `DATE-OBS` card says 03:19:34
UTC. Open the FITS header to read every date card.

### Read a sequence's facts

Open the summary of a SER or image-list view. **Sequence General Info** gives the frame size, **Number of Frames**,
**Frames per Second** and **Sequence creation date**. A video of another kind, such as an AVI or MP4 file, gets the
report of the frame shown, not of the sequence.

### Copy the report

Choose **Copy to Clipboard**. The whole report, as the window last showed it, goes to the clipboard as plain text, in
place of what was there. No message confirms it. The report names your folders and your camera: read it before you
paste it anywhere public.

### Keep the file facts meaningful

Save the image to a place of your own first. An image never saved, or kept in AfterNight's temporary storage, shows
`-` for every fact of **File Info**. The size and the times of a file are asked of the file system each time the
report is built. The image itself is not read again, so the report is no check that the file opens.

## Controls

### The window

```controls
window: Image and Sequence Summary
controls:
  - name: Title
    kind: readout
    default: ""
    effect: Names the view the report describes, as `Image Summary Viewer <M42>`. A rename changes the title and the name the report starts with.
    enabled: The window opens from **Image > Information > Summary**, from **Summary** on the view's toolbar, or with ++ctrl+shift+m++ while the view has the focus. While it is open, the menu entry is disabled until the menus are refreshed, as when another view becomes active. The button and the shortcut stay enabled, and each opens another window.
    tradeoff: The shortcut does nothing while the button sits in the toolbar's **More** menu. Use the menu then.
    persistence: Nothing of the window is kept.
    note: It needs no network, no catalog and no other program. It writes no image, file, header card or history step.
  - name: Summary Report
    kind: readout
    default: ""
    effect: Shows the report in sections, each a title and a line for each fact. A line without a colon shows as a note under its section, and a report without sections as one section named **Summary**.
    range: Built again when the view's image loads or changes, a new frame of a sequence among them, and when the view is renamed. There is no refresh button.
    enabled: '"No summary information available." stands in its place when the report is empty, as when the window opens while an edit holds the image.'
    tradeoff: The window never waits for an edit. While one holds the image, it keeps the report it shows and builds it again at the next change. A playing sequence builds it again at every frame, and a SER's or an image list's facts stay the same, so close the window or pause the player when you do not need it.
    note: Hovering over the view's tab or title bar shows the same report as a tooltip.
  - name: Copy to Clipboard
    kind: button
    default: ""
    effect: Puts the whole report, as last shown, on the clipboard as plain text, without its formatting.
    tradeoff: It replaces the clipboard's text and shows no confirmation. It does nothing when the system's clipboard cannot be reached.
  - name: Close
    kind: button
    default: ""
    effect: Closes the window and enables the menu's **Summary** again.
    range: ++enter++ and the window's own close button do the same.
```

### An image's report

```controls
window: An image's report
controls:
  - name: Image Name
    kind: readout
    default: ""
    effect: The view's name, under its own section, **Overview**.
  - name: File Info
    kind: readout
    default: ""
    effect: "**File Name**, the file's full path. **File Size** in MB of 1,048,576 bytes. **File Creation Time** and **File Modification Time**, the file system's, in local time to the millisecond. **File Codec**, the format the file's extension names: FITS, XISF, RAW, PNG, JPEG or TIFF."
    range: Every fact reads `-` for an image never saved, for one kept in AfterNight's temporary storage, and for a path that names no file. **File Codec** also reads `-` for an extension AfterNight does not know.
    tradeoff: The path shows your folders when you share a copy of the report.
  - name: Image General Info
    kind: readout
    default: ""
    effect: "**Image Resolution (Width x Height)**, such as `5544x3684`. **Image Channels**. **Image Format**, the pixel type, such as `16 bits (unsigned integer)` or `32 bits (float)`. **Image Pixel Format**, what the channels are: `Grayscale`, `RGB`, `RGBA` or the color model they state. **CFA pattern**, the four filters at the image's top left corner, such as `RGGB`, or `X-Trans`, or `-` for an image that is no mosaic. **Image Size (in memory)** in MB."
  - name: Image Detailed Info
    kind: readout
    default: ""
    effect: "**Camera Model**. **ISO Speed**, or **Gain** for a file that states a gain and no ISO speed. **Offset**. **Image Creation Date**. **Exposure Time** in seconds. **Sensor Temperature** in degrees Celsius. **Software Used**."
    range: A number is written in its fewest digits, to six significant ones, so a FITS card's `300.000` reads `300 sec`. An exposure under a second that is a whole fraction of one reads as a camera writes it, such as `1/250 sec`.
    scope: The facts come from the image's acquisition record and from its header cards - the camera's `Exif.Image.Model` or `INSTRUME`, the exposure's `Exif.Photo.ExposureTime`, `EXPOSURE` or `EXPTIME`, `CCD-TEMP` for the temperature, and `Exif.Image.Software`, `SOFTWARE` or `SWCREATE`.
    note: "**Image Creation Date** is `Exif.Photo.DateTimeDigitized`, else `DATE`, else `DATE-LOC`, else the start of the exposure, `Exif.Photo.DateTimeOriginal` or `DATE-OBS`. Each is read as UTC and shown to the second."
```

### A sequence's report

```controls
window: A sequence's report
controls:
  - name: Sequence Name
    kind: readout
    default: ""
    effect: The view's name, under **Overview**.
    enabled: For a SER video and an image list. Any other sequence gets the image report of the frame shown.
  - name: File Info
    kind: readout
    default: ""
    effect: "**File Name**, **File Size** in MB, or in GB from 1,024 MB, the two file times and **Sequence Type**: `SER sequence` or `Image list sequence`."
    range: An image list shows **Total files size**, the sizes of every file it names added up when the list was opened, and `-` when one of them could not be measured. Its two times are those of the `.anseq` file itself.
  - name: Sequence General Info
    kind: readout
    default: ""
    effect: "**Resolution (Width x Height)**, **Channels**, **CFA pattern**, **Number of Frames**, **Frames per Second** with two decimals, **Pixel Format**, the pixel type, **Frame size (in memory)** in MB or GB, and **Sequence creation date**."
    range: A 640 x 480 16-bit frame reads `0.59 MB`. A SER's rate is the one its frame timestamps give, and an image list's the rate it states. A fact nothing states reads `-`, the size and the rate included.
    scope: For an image list opened from its file, the frame facts describe its first frame, read when the list was opened. A list AfterNight has just written states the frames it wrote. The report lists neither the files nor the facts of each frame.
    note: "**Sequence creation date** is when a SER's header says its recording began, or its first frame's timestamp when the header states none, in UTC. For an image list, it is the creation time its file states. Either reads `-` when nothing states it."
```

## When it goes wrong

**Summary** is greyed in the **Information** menu
:   **Cause.** A Summary window of this view is open, or the active view has no image.
:   **Fix.** Use the open window, or choose **Summary** on the view's toolbar, which opens another one.

Every fact of **File Info** reads `-`
:   **Cause.** The image was never saved, is kept in temporary storage, or its file is gone.
:   **Fix.** Save it to a place of your own, then open the summary again.

A video shows image fields instead of the sequence's
:   **Cause.** Only a SER video and an image list get a sequence report.
:   **Fix.** Read the frame count and the rate in the status bar, or convert the video to a SER or an image list.

**Image Creation Date** is hours away from the night you shot
:   **Cause.** The file states a local time in `DATE-LOC`, which is read as UTC with no offset.
:   **Fix.** Read `DATE-OBS`, the start of the exposure in UTC, in the FITS header.

The report did not change while an edit ran
:   **Cause.** The window never waits for an edit, and keeps the report it shows until the edit is done.
:   **Fix.** Wait for the edit to finish. The report is built again when the image changes.

A card or a field you expected is missing
:   **Cause.** The report shows a fixed set of facts. It has no statistics, no histogram, no
    [history](../reference/glossary.md#history), no [plate solution](../reference/glossary.md#plate-solution) and no
    full list of the header's cards.
:   **Fix.** Open [FITS Header, EXIF, And Astrometry Metadata](fits-header-viewer.md) for every card and the solution.

## Related

- [FITS Header, EXIF, And Astrometry Metadata](fits-header-viewer.md), for every card, every camera field and the plate
  solution.
- [Image Statistics](image-statistics.md), for the level, the spread and the noise of each channel.
- [Histogram Analysis](image-histogram.md), for how the samples spread and how many are clipped.
- [Sequences](sequences.md), for the player, the kinds of sequence and their conversions.
- [Saving And Exporting](saving-and-exporting.md), to save the image to a file whose facts the report can show.
- [Image Viewer](image-viewer.md), for the toolbar that opens the window and the status bar's short summary.
