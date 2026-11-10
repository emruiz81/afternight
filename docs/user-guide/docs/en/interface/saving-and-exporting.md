---
kind: interface
summary: >-
  Save as writes the active image or sequence to a file whose extension chooses the format. Export Sequence writes a
  sequence as SER, AVI, MP4 or WebM, debayered, stretched or resized if you ask.
menu:
  - File > Save as...
  - Image > Sequence > Export...
rewritten: true
---
# Saving and Exporting

**File > Save as...** writes the [active image](../reference/glossary.md#active-image) or
[sequence](../reference/glossary.md#sequence) to a file you name. The extension you type chooses the format, and only
the image is written, never what the view merely shows. **Image > Sequence > Export...** writes a sequence again for
sharing, as [SER](../reference/glossary.md#ser), uncompressed AVI, MP4 or WebM, with its frames debayered, stretched
or resized if you ask. Both write a file whole or not at all, and replace an existing file only once the new one is
complete. [Supported Formats](../getting-started/supported-formats.md) compares what each format keeps.

```figure
owed: true
image: ../assets/images/interface/saving-and-exporting.webp
alt: >-
  The Export Sequence dialog. In a dark card: Output Format reading MP4 and Codec reading h264 on the first row; Output
  File Name ending in saturn.mp4, a button with three dots and FPS at 34.26 on the second; Autostretch and Debayer CFA,
  both ticked, on the third. Below, the Resize group, unticked, with a box reading Preserve Ratio, two greyed boxes at
  448 and a greyed box reading Fit. Reset at the bottom left, OK and Cancel at the bottom right.
caption: >-
  Export Sequence for a 16-bit SER video of Saturn, set to write a debayered and stretched H.264 video.
pins:
  - {n: 1, x: 4.0, y: 25.7}
  - {n: 2, x: 4.0, y: 34.0}
  - {n: 3, x: 95.0, y: 34.0}
  - {n: 4, x: 4.0, y: 41.7}
  - {n: 5, x: 8.0, y: 48.0}
  - {n: 6, x: 52.0, y: 89.0}
```

## What you see

1. **Output Format and Codec.** The kind of file the export writes, and the coding of a video.
2. **Output File Name.** The file the export writes, which the **...** button chooses.
3. **FPS.** The frame rate written into an AVI or a video.
4. **Autostretch and Debayer CFA.** Two changes to every frame: the
   [display stretch](../reference/glossary.md#display-stretch) of the sequence's view, and color from a mosaic.
5. **Resize.** A new size for every frame, with its preset and how a frame fills it.
6. **Reset, OK and Cancel.** They return the size to the frame's own, start the export, and close the dialog.

## What you can do

### Save an image

1. Click the image's view, then choose **File > Save as...**, ++ctrl+shift+s++, the save button of the toolbar or
   **Image > Actions > Save File...**. All four do the same: no command writes back to the file without asking.
2. The window proposes the view's name in the folder you last opened from or saved to. Type the name with the
   extension of the format you want: `.fits`, `.xisf`, `.tif`, `.png` or `.jpg` for
   [FITS](../reference/glossary.md#fits), [XISF](../reference/glossary.md#xisf), TIFF, PNG or JPEG.
3. Choose **Save**. A **Save File** window follows the write, and you can go on working meanwhile.
4. When it closes, the file is written. The view takes it as its own file and loses the asterisk of unsaved work,
   unless you changed the image while it was written.

The [Autostretch](../reference/glossary.md#autostretch) display, the channel shown, the zoom, overlays and
[masks](../reference/glossary.md#mask) are never written. To keep a stretch, apply it with a process first. The
options of each format are on the **Export** page of [Settings](settings-dialog.md).

### Save a sequence

Choose the same commands with a sequence's view active. The window proposes the sequence's own type: `.ser`, `.anseq`,
or the video's extension. Every frame is read from the sequence's file and written into the container the extension
names, so another extension converts the sequence.

A frame is written as the file holds it, whatever the view shows. A mosaic stays a mosaic in SER and in an image list,
and is written as its gray samples in AVI or a video. [Debayer](../reference/glossary.md#debayer) the sequence with a
process first for color frames, or use **Debayer CFA** of the export.

### Export a sequence

1. Make the sequence's view active and choose **Image > Sequence > Export...**.
2. Choose the **Output Format** and, for MP4, the **Codec**.
3. Choose **...** and name the file. **OK** becomes available.
4. Set **FPS**. Turn on **Debayer CFA**, **Autostretch** or **Resize** only for a copy to watch, not to process.
5. Choose **OK**. A progress window follows the frames, then "Sequence exported successfully!" appears and the dialog
   closes.

The sequence in the view, and its unsaved state, do not change.

### Check what you wrote

AfterNight does not open a file again to check it. Before you delete a [master](../reference/glossary.md#master) or a
source, open the file you wrote. Read its size, channels and [pixel type](../reference/glossary.md#pixel-type) in the
status bar and [Image Summary](image-summary.md), and its [header cards](../reference/glossary.md#header-card) in the
FITS Header window. For a sequence, check the frame count, the rate and the first and last frames, and for a list,
that its frames folder is beside it.

## Controls

### Export Sequence

```controls
window: Export Sequence
controls:
  - name: Output Format
    kind: combo
    default: SER
    options: [SER, AVI (RAW), MP4]
    effect: "**SER** writes a SER file, **AVI (RAW)** an uncompressed AVI, and **MP4** a compressed video in the coding **Codec** names."
    range: SER keeps 8-bit frames in 8 bits and stores every other pixel type as 16-bit unsigned integers, a mosaic as a mosaic, and each frame's exposure start. AVI and the videos store 8-bit frames, a mosaic as its gray samples. Only gray, mosaic and color frames can be written.
    tradeoff: SER suits a sequence you will process further. AVI keeps every 8-bit sample but is large. A video is small and plays almost anywhere, but loses detail. No format keeps the image history, and AVI and the videos keep no astronomical metadata.
    persistence: The dialog opens on the format, codec and **Resolution** it last closed with, kept between sessions among the process settings as `__EXPORT_SEQUENCE__`. SER on a new profile.
    note: AfterNight writes SER and AVI itself, and FFmpeg writes MP4 and WebM. None of them uses the network.
  - name: Codec
    kind: combo
    default: h264
    options: [h264, h265, WEBM]
    effect: "**h264** and **h265** write an MP4 file coded in H.264 or H.265, and **WEBM** a WebM file coded in VP9."
    enabled: With **Output Format** at **MP4**.
    range: A change of format or codec changes the extension of a file already named to `.ser`, `.avi`, `.mp4` or `.webm`.
    tradeoff: All three lose detail, and coding takes processor time for every frame. The encoders, x264, x265 and libvpx, work on one thread at one fixed quality, and code color through the BT.709 matrix at limited range.
  - name: Output File Name
    kind: field
    default: ""
    effect: Shows the file the export writes. It cannot be typed into.
    enabled: "**OK** stays disabled until it names a file."
    persistence: The name is not kept for the next export.
  - name: "..."
    kind: button
    default: ""
    effect: Opens **Save Sequence...** to choose the file, with the extension of the format chosen. The file window asks before it replaces a file.
    persistence: It opens in the folder of the file named already, else in the folder an export chose last, kept between sessions as `__EXPORT_SEQUENCE_CONTROL__`.
  - name: FPS
    kind: spinbox
    default: The sequence's rate
    widget: {min: 1, max: 60, step: 0.3, decimals: 2}
    effect: Sets the rate written into an AVI or a video. Frames are written as they are, none added, removed or merged.
    range: It opens at the rate the sequence states, held between 1 and 60, or at 1 when it states none. AVI and the videos store the exact fraction, a whole rate or thousandths.
    persistence: Not kept. A SER has no field for a rate and keeps each frame's exposure start instead, so this rate does not reach it.
  - name: Autostretch
    kind: check
    default: "Off"
    effect: Stretches every frame as **Apply Stretch** does, with the Autostretch settings of the sequence's view as they were when the dialog opened, their saturation, brightness and contrast included. Each frame is measured for its own stretch.
    scope: Every frame, after **Debayer CFA** and before **Resize**. A save never stretches.
    tradeoff: The file holds stretched samples, for watching and not for measuring. Measuring every frame adds time.
    persistence: Off each time the dialog opens.
  - name: Debayer CFA
    kind: check
    default: "Off"
    effect: Debayers every mosaic frame, by the demosaic method of the sequence's view, so that the file holds color frames.
    enabled: Shown only when the sequence's frames are a mosaic.
    tradeoff: The file then holds three channels in place of the mosaic, which cannot be had back from it. Off, a SER keeps the mosaic, and AVI and the videos store its samples as gray.
    persistence: Off each time the dialog opens.
  - name: Resize
    kind: check
    default: "Off"
    effect: Resizes every frame to **Width** and **Height** with a Lanczos-4 kernel. Off keeps the frames' own size.
    range: A video needs an even width and height. An odd last column or row is cut away, keeping the top left, with **Resize** on or off.
    tradeoff: Resizing adds time and memory for every frame. Enlarging adds no detail, and a resized frame no longer fits its plate solution.
    persistence: Off each time the dialog opens.
  - name: Resolution
    kind: combo
    default: Preserve Ratio
    options: [Preserve Ratio, Free Ratio, Full HD, 4K, 8K]
    effect: "**Preserve Ratio** keeps **Width** and **Height** in the frame's proportion, **Free Ratio** sets them apart, and **Full HD**, **4K** and **8K** ask for 1920 x 1080, 3840 x 2160 and 7680 x 4320."
    range: Choosing **Preserve Ratio** or **Free Ratio** returns **Width** and **Height** to the frame's size.
    enabled: With **Resize** on.
    persistence: Kept with the format and the codec.
  - name: Width
    kind: spinbox
    default: The frame's width
    widget: {min: 4, step: 1}
    effect: Sets the width of the frames written. At **Preserve Ratio**, **Height** follows.
    range: Up to twice the frame's width, or 7680 when that is more, so that no preset is cut.
    enabled: With **Resize** on, at **Preserve Ratio** or **Free Ratio**.
  - name: Height
    kind: spinbox
    default: The frame's height
    widget: {min: 4, step: 1}
    effect: Sets the height of the frames written. At **Preserve Ratio**, **Width** follows.
    range: Up to twice the frame's height, or 4320 when that is more.
    enabled: With **Resize** on, at **Preserve Ratio** or **Free Ratio**.
  - name: Fill mode
    kind: combo
    default: Fit
    options: [Fit, Fill, Stretch, Center]
    effect: "Places each frame in the size asked. **Fit** scales it to fit inside and pads the rest with black, **Fill** scales it to cover the whole and crops what overflows, **Stretch** scales width and height apart, and **Center** keeps the frame's size and centers it, padded or cropped."
    enabled: With **Full HD**, **4K** or **8K**. At **Preserve Ratio** or **Free Ratio** it is disabled but still applies as it reads, so a **Free Ratio** of another proportion is padded with black under **Fit**.
    tradeoff: "**Fit** leaves black bars, **Fill** and **Center** can crop, and **Stretch** distorts."
    persistence: Not kept for the next export.
  - name: Reset
    kind: button
    default: ""
    effect: Returns **Resolution** to **Preserve Ratio**, **Width** and **Height** to the frame's size, and turns **Resize** off.
    range: It leaves the format, the codec, the file, **FPS**, **Autostretch**, **Debayer CFA** and **Fill mode** as they are.
  - name: OK
    kind: button
    default: ""
    effect: Exports every frame of the sequence, read from its file, then debayered, stretched and resized as asked. A progress window follows the frames, and the dialog's controls are disabled while it works.
    range: On success, "Sequence exported successfully!" appears and the dialog closes. On failure, "Export Sequence Error" gives "Could not export sequence:" and the reason, and the dialog stays open.
    tradeoff: The time grows with the frames, their size and each change asked, and an uncompressed AVI can be very large. Try a short part made with Extract Sequence first.
    persistence: The file appears at its name only when its last frame is written. A stopped or failed export leaves nothing there, and an earlier file of that name as it was.
    note: An AVI that would need more index room than its writer reserves is refused, not continued in a second file. A frame the encoder refuses fails the export with its reason.
  - name: Cancel
    kind: button
    default: ""
    effect: Closes the dialog without writing.
    range: While an export works, closing the dialog stops it, and the dialog closes when the export has ended.
    persistence: Closing the dialog in any way keeps the format, the codec and **Resolution**.
```

```controls
window: Exporting Sequence...
controls:
  - name: Progress
    kind: readout
    default: ""
    effect: Reads "Exporting sequence '<path>'..." and runs from 0 to 100 percent, a step for each frame written.
  - name: Cancel
    kind: button
    default: ""
    effect: Stops the export before the next frame. Nothing is written, and the Export Sequence dialog stays open with its settings, without a message.
```

### Save as

```controls
window: Save as
controls:
  - name: File name
    kind: field
    default: The view's name
    effect: Names the file. Its extension decides the format, in upper or lower case, whatever the file type shows.
    range: For an image, `.fit`, `.fits` or `.fts`, `.xisf`, `.tif` or `.tiff`, `.png`, and `.jpg`, `.jpeg` or `.jpe`. For a sequence, `.ser`, `.anseq`, `.avi`, `.mp4`, `.mov`, `.mkv` or `.webm`. Any other extension is refused before anything is written.
    persistence: The folder becomes the one the next open and save start in, kept between sessions.
    note: The window is titled `Save image <name> as...`, also for a sequence. It is your system's file window unless **Use system file browser** is off in the Interface settings.
  - name: File type
    kind: combo
    default: "TIFF files (*.tif *.tiff)"
    options: ["All supported formats (*.png *.jpg *.jpeg *.tif *.tiff *.fit *.fits *.xisf)", "PNG files (*.png)", "JPEG files (*.jpg *.jpeg)", "TIFF files (*.tif *.tiff)", "FITS files (*.fit *.fits)", "XISF files (*.xisf)"]
    effect: Chooses the format the window proposes and the files it lists. For an image, it opens on the **Default Save Format** of the General settings, TIFF on a new profile.
    range: A sequence gets its own type alone - `SER sequence (*.ser)`, `AfterNight sequence (*.anseq)`, or `Video sequence` with the video's extension, `.mp4` for an extension AfterNight does not know - beside all the sequence types together. No camera raw type is offered.
    tradeoff: It does not decide the format, the extension does. A type chosen in the window is forgotten when it closes.
  - name: Save
    kind: button
    default: ""
    effect: Starts the save in the background, behind a **Save File** window, and you can go on working.
    enabled: The save commands are disabled while no image or sequence view is active. Reached without one, a box titled "File save error" says "No selected image to save".
    range: What is written is decided now - the image as it is, with the **Settings > Export** options as they are. A later edit or setting reaches only the next save. A mosaic is refused for a format that cannot state its filter pattern, before anything is written.
    tradeoff: The window asks before it replaces a file. Replacing keeps no backup.
    persistence: A file written joins the recent files. When the view still shows what was written, it takes the file as its own and loses its asterisk. An edit made meanwhile keeps the asterisk, because the view no longer shows what is in the file.
    note: A format that does not hold the image's pixel type gets a converted copy, computed in double precision at the same share of full scale and rounded to the nearest code, ties to even. An image of physical values in floating point is converted as the fraction of full scale the viewer shows.
  - name: Cancel
    kind: button
    default: ""
    effect: Closes the window. Nothing is written.
```

```controls
window: Save File
controls:
  - name: Progress
    kind: readout
    default: ""
    effect: Reads "Saving image '<name>' to file '<path>'...", with "Saving sequence" for a sequence, over a moving bar.
    tradeoff: An image is converted, compressed and written whole, and a sequence frame by frame, so a large image or a long sequence takes time and disk.
  - name: Cancel
    kind: button
    default: ""
    effect: Stops the save.
    range: An image may be written whole before the stop is noticed. A stopped or failed save leaves nothing at the destination, and an existing file as it was.
    persistence: A stopped save shows no message, adds no recent file and leaves the asterisk.
```

### What each format keeps

```controls
window: What each format keeps
controls:
  - name: FITS
    kind: action
    default: ""
    effect: Writes every pixel type at the FITS sample type of its width, color as three planes, and a Bayer mosaic with its pattern. An X-Trans mosaic is refused.
    range: The header cards the image was read with are written back, its world coordinates and HISTORY cards among them. The structural cards are written new, and a long processing state is continued over `CONTINUE` cards. A turn or a resize has already dropped the cards that no longer fit, so no stale coordinates are written.
    tradeoff: No compression is offered.
    note: The value `export/fitsFloatFormat`, which the settings keep, is not read.
  - name: XISF
    kind: action
    default: ""
    effect: Writes 8, 16 and 32-bit unsigned integers and 32 and 64-bit floating point, a signed image as the unsigned integers of its width, and Bayer and X-Trans mosaics.
    range: The header cards and the XISF properties are written back, each property with its type. The compression, byte shuffling and checksum are those of **Settings > Export**, none by default.
  - name: TIFF
    kind: action
    default: ""
    effect: Writes every pixel type with its kind, signed 8-bit included, and Bayer and X-Trans mosaics, in strips of about a quarter of a megabyte.
    range: Uncompressed, or LZW, ZIP/Deflate or JPEG as **Settings > Export** says. JPEG is used only for 8-bit samples. A deeper image is written with LZW, and the Session Console says so.
  - name: PNG
    kind: action
    default: ""
    effect: Writes 8 and 16-bit unsigned integers. A signed 8 or 16-bit image is written as unsigned of its width, and every wider type as 16-bit.
    range: Its compression level, from 0 to 9, changes the size and the time, never the pixels. A mosaic is refused.
  - name: JPEG
    kind: action
    default: ""
    effect: Writes 8-bit gray or color, compressed with loss, at the quality of **Settings > Export**.
    range: Every other pixel type is converted to 8 bits. A mosaic is refused.
  - name: SER
    kind: action
    default: ""
    effect: Writes the frames of a sequence as its file holds them, 8-bit frames in 8 bits and every other type as 16-bit unsigned integers. A mosaic stays a mosaic.
    range: Each frame's exposure start goes into the timestamp trailer, as unknown for a frame that states none. A SER has no field for a rate. Opened again, it plays at the rate the timestamps give, or at 1 frame per second when they give none, as for frames that came from a video.
  - name: Image list
    kind: action
    default: ""
    effect: Writes an `.anseq` list and a folder beside it, `<name>-frames`, with each frame as a FITS file, `<name>-0.fits`, `<name>-1.fits` and so on, carrying its header cards.
    range: Every frame takes the first frame's pixel type, so a frame of another type is converted to it. The list names its frames from its own folder, so the list and its frames folder move together. It states the first frame's size and the rate.
    tradeoff: One file a frame takes disk space and time. Keep the list and the folder as one.
  - name: AVI
    kind: action
    default: ""
    effect: Writes uncompressed 8-bit frames, gray through a gray palette or 24-bit color, stored the right way up, with the rate as an exact fraction.
    range: A deeper frame is converted to 8 bits, and a mosaic is written as its gray samples.
  - name: MP4, MOV and MKV
    kind: action
    default: ""
    effect: Writes 8-bit frames coded in H.264, in an MP4 container for `.mp4` and `.mov` and in a Matroska one for `.mkv`. A save of a video is never a copy of its file.
    range: The width and the height are made even, and a mosaic is written as its gray samples. Sound is never written.
  - name: WebM
    kind: action
    default: ""
    effect: Writes 8-bit frames coded in VP9.
    range: As MP4. AfterNight writes no MPG or MPEG file, so a video read from one is saved under another extension.
```

## When it goes wrong

"Could NOT save file" with "the product writes FITS, XISF, TIFF, PNG and JPEG files only"
:   **Cause.** The file name of an image has no extension AfterNight writes.
:   **Fix.** Save again with `.fits`, `.xisf`, `.tif`, `.png` or `.jpg` at the end of the name.

"no sequence container the product writes is named by" and the path
:   **Cause.** A sequence was saved under an extension AfterNight does not write, such as `.mpg` or `.mpeg`, which the
    window proposes for a video read from one.
:   **Fix.** Save it as `.ser`, `.anseq`, `.avi`, `.mp4`, `.mkv` or `.webm`, or export it.

"a colour filter array mosaic cannot be written as PNG or JPEG", or "an X-Trans mosaic cannot be written as FITS, PNG or JPEG"
:   **Cause.** The image is a mosaic not yet debayered, and the format cannot state its filter pattern.
:   **Fix.** Save a Bayer mosaic as FITS, XISF or TIFF and an X-Trans mosaic as XISF or TIFF, or debayer it first.

The view keeps its asterisk after a save
:   **Cause.** The image changed while it was written, so the file holds the image as it was before the change.
:   **Fix.** Save again once the edit is done.

"Export Sequence Error" with "Could not export sequence:" and a reason
:   **Cause.** A frame could not be read, converted or coded, or the file could not be written. Nothing was written.
:   **Fix.** Correct what the reason names, often the free space or a frame a video encoder refuses, and choose **OK**
    again.

An exported video is one pixel narrower, or has black bars
:   **Cause.** A video stores an even width and height, and **Fit** pads a frame of another proportion with black.
:   **Fix.** Keep the frame's proportion with **Preserve Ratio**, or choose **Fill** or **Stretch** for a preset, with
    an even width and height. Export again to a new file.

## Related

- [Supported Formats](../getting-started/supported-formats.md), to choose the format for a master, a copy or a video.
- [File Formats](../reference/file-formats.md), for each format's precision, compression and metadata in depth.
- [Sequences](sequences.md), to open, make and convert the sequences you save and export.
- [Settings](settings-dialog.md), for **Default Save Format** and the options of the **Export** page.
- [Image Summary](image-summary.md), to read the size and pixel type of a file you wrote.
- [FITS Header, EXIF, And Astrometry Metadata](fits-header-viewer.md), to read the header a save wrote.

## Credits

- [FITS Standard 4.0](https://fits.gsfc.nasa.gov/fits_standard.html), by the IAU FITS Working Group.
- [XISF 1.0 Specification](https://pixinsight.com/doc/docs/XISF-1.0-spec/XISF-1.0-spec.html), by Pleiades
  Astrophoto.
- [TIFF 6.0 specification](https://libtiff.gitlab.io/libtiff/specification/index.html) and
  [TIFF/JPEG Technical Note 2](https://libtiff.gitlab.io/libtiff/specification/technote2.html), as LibTIFF hosts them.
- [PNG Specification, Second Edition](https://www.libpng.org/pub/png/spec/iso/index-object.html).
- [Independent JPEG Group](https://ijg.org/) and the
  [JPEG Still Picture Compression Standard](https://www.ijg.org/files/Wallace.JPEG.pdf).
- [SER format description, version 3b](https://free-astro.org/images/5/51/SER_Doc_V3b.pdf), for the SER container and
  its timestamp trailer.
- [FFmpeg](https://ffmpeg.org), by the FFmpeg developers, under the GNU LGPL 2.1 or later and, as built with its GPL
  encoders, the GNU GPL 2 or later, writes the MP4, Matroska and WebM files, as the
  [FFmpeg Formats Documentation](https://ffmpeg.org/ffmpeg-formats.html) describes them.
- [x264](https://code.videolan.org/videolan/x264) and [x265](https://bitbucket.org/multicoreware/x265_git/), by their
  project authors, under GPL-2.0-or-later, code H.264 and H.265, and
  [libvpx](https://chromium.googlesource.com/webm/libvpx/), by the WebM project authors, under BSD-3-Clause, codes VP9.
