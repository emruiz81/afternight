---
kind: reference
summary: >-
  The files AfterNight opens and the files it writes, by format and extension, with what each keeps of an image and
  where in the menus each is written.
---
# Supported Formats

This page answers two questions: can AfterNight open my files, and what can it write? The first table lists every
kind of file AfterNight opens, the second every kind it writes, by format. [File Formats](../reference/file-formats.md)
gives each format's precision, compression and metadata in more depth.

## Files AfterNight opens

Open a file with **File > Open**, ++ctrl+o++, the Dashboard's **Open file**, a recent file, or a drop of files on the
Dashboard. The file's extension, in upper or lower case, decides how it is read. The **Open File** dialog shows only
these extensions: it has no filter for all files.

| Format | Extensions | What opens | What is refused |
| --- | --- | --- | --- |
| [FITS](../reference/glossary.md#fits) | `.fit`, `.fits`, `.fts` | The first image of the file: the primary image, an image extension or a tile-compressed image. Grayscale or color, in 8, 16 or 32-bit integers, signed or unsigned, or 32 or 64-bit floating point. A Bayer mosaic its header states. | A 64-bit integer image with a value beyond \(2^{53}\), which 64-bit floating point cannot hold exactly. Within that, it opens as 64-bit floating point. |
| [XISF](../reference/glossary.md#xisf) | `.xisf` | Grayscale or color, in 8, 16 or 32-bit unsigned integers or 32 or 64-bit floating point. A Bayer or X-Trans mosaic. | 64-bit integer and complex samples. A file whose pixel data fail their checksum. |
| TIFF | `.tif`, `.tiff` | Grayscale, or color with or without alpha, in 8, 16 or 32-bit integers, signed or unsigned, or 32 or 64-bit floating point. A mosaic it states. A color TIFF compressed as JPEG. | Palette, white-is-zero, CMYK and other [color models](../reference/glossary.md#color-model) whose values are not gray levels or red, green and blue. Associated alpha. 64-bit integer and half-precision floating point. |
| PNG | `.png` | 8 or 16-bit grayscale or color, with or without alpha. A palette image opens in its palette's colors. | - |
| JPEG | `.jpg`, `.jpeg`, `.jpe` | 8-bit grayscale or color. | 12-bit and CMYK JPEG. A file that ends before its last row. |
| Camera [RAW](../reference/glossary.md#raw) | `.cr2`, `.cr3`, `.crw` (Canon), `.nef` (Nikon), `.arw`, `.sr2`, `.srf` (Sony), `.dng` (Adobe), `.raf` (Fujifilm), `.orf` (Olympus), `.pef` (Pentax), `.raw` (Panasonic), `.x3f` (Sigma), `.dcr`, `.k25`, `.kdc` (Kodak), `.erf` (Epson), `.mrw` (Minolta) | The sensor's data as a Bayer or X-Trans mosaic, or in color for a full-color raw such as a linear DNG. | A four-color sensor, such as CMYG or CYGM. A camera model the decoder does not know. |
| [SER](../reference/glossary.md#ser) | `.ser` | A recording in grayscale, as a Bayer mosaic or in color, in 8 or 16 bits. A recording that stopped before it wrote its frame count opens with its complete frames. A CMY mosaic opens as one gray plane of its values. | - |
| AfterNight [sequence](../reference/glossary.md#sequence) | `.anseq` | The image files the list names, in order, as one sequence. | A list whose files are missing. |
| Video | `.avi`, `.mp4`, `.mov`, `.mkv`, `.webm`, `.mpg`, `.mpeg` | The video frames, decoded by FFmpeg, from a stream in one of these codecs: uncompressed, FFV1, HuffYUV, Ut Video, Motion JPEG, lossless JPEG, H.264, H.265, MPEG-4, MPEG-1, MPEG-2, VP8, VP9, AV1, ProRes, DNxHD, DV, Cinepak, RLE, Video 1, v210 and r210. | A stream in any other codec. Sound is never read. |

A selection in the dialog that holds a file of any other extension is refused whole: "AfterNight can only open
supported image, RAW, and video files." Dropped files of other extensions are ignored. A recent file that has moved or
gone fails to open, and leaves the list.

A file whose extension the table lists can still fail to open: it may be damaged, or hold a variant the format allows
and AfterNight does not read. An image whose samples are none of the eight types AfterNight works in is refused with
"the file holds samples outside the eight this engine processes".

### What opening does to an image

- **Nothing is written.** Opening reads the file into memory, and the file stays as it is.
- **A camera raw file** is read under the **Raw Images** settings in force when you asked to open it: its white
  balance, and its turn by the orientation the camera recorded in its [EXIF](../reference/glossary.md#exif) record
  when **Auto-rotate based on EXIF orientation** is on.
  Under **Auto White Balance**, the balance is measured from the frame itself. AfterNight reads it with LibRaw, or with
  a private build of RawSpeed for the cameras where that decoder gives the same samples and metadata. A full-color raw
  opens with each color in its own plane.
- **Only a camera raw file is turned.** A JPEG's or TIFF's recorded orientation is kept in its metadata and not
  applied. Nothing is removed either: an alpha channel opens with the image.
- **A mosaic asks a question.** For any [CFA](../reference/glossary.md#cfa) mosaic, a box titled **Open CFA file**
  asks whether to [debayer](../reference/glossary.md#debayer) it. **Yes** turns it into color with the **Demosaic Algorithm** of the **Raw Images** settings. An X-Trans mosaic,
  from a Fujifilm camera, is turned into color in the camera's colors by the X-Trans form of that method: the fast
  X-Trans estimate for **Bilinear**, three passes of Markesteijn's method for every other. **No** opens the mosaic as
  recorded.
- **An XISF file keeps all it states.** Every property is read under its own name and kept with its type, such as a
  number, a vector or a matrix. Text with line breaks is kept too, such as a record of processing steps written into
  the file. When its FITS keywords state no [plate solution](../reference/glossary.md#plate-solution), a solution
  stored in its properties is read, and the image opens solved.

## Files AfterNight writes

**File > Save as...**, ++ctrl+shift+s++, writes the [active image](../reference/glossary.md#active-image) or
sequence to a file you name. **Image > Actions >
Save File...** does the same. **Image > Sequence > Export...** writes a sequence through the **Export Sequence**
dialog, and **Image > Information > Annotate Image...** exports an annotated picture.

| Format | Extensions | Written by | What it keeps |
| --- | --- | --- | --- |
| FITS | `.fit`, `.fits` | Save as, for an image | Every sample type, and a Bayer mosaic. The [header cards](../reference/glossary.md#header-card) the image was read with. |
| XISF | `.xisf` | Save as, for an image | 8, 16 and 32-bit unsigned integers and 32 and 64-bit floating point. A signed image is written as the unsigned integers of its width. Bayer and X-Trans mosaics. Its properties, written back with their types, and its FITS keywords. |
| TIFF | `.tif`, `.tiff` | Save as, for an image. Annotate Image. | Every sample type, and Bayer and X-Trans mosaics. Part of the EXIF record, when it is embedded. |
| PNG | `.png` | Save as, for an image. Annotate Image. | 8 and 16-bit unsigned integers. Signed 8 and 16-bit images are written as unsigned of their width, and every wider type as 16-bit. A small part of the EXIF record, when it is embedded. |
| JPEG | `.jpg`, `.jpeg` | Save as, for an image. Annotate Image, without a transparent background. | 8-bit samples, compressed with loss. Part of the EXIF record, when it is embedded. |
| SER | `.ser` | Save as, for a sequence. Export Sequence. | Grayscale, Bayer mosaic or color frames in 8 or 16 bits, and each frame's capture time. |
| AfterNight sequence | `.anseq` | Save as, for a sequence | The list, and one FITS file for each frame in a folder named after it, `<name>-frames`, beside it. |
| AVI | `.avi` | Save as, for a sequence. Export Sequence, as **AVI (RAW)**. | Uncompressed 8-bit frames. A mosaic is written as gray. |
| MP4, MOV and MKV | `.mp4`, `.mov`, `.mkv` | Save as, for a sequence. Export Sequence writes MP4. | 8-bit frames as H.264, or H.265 from Export Sequence, compressed with loss. A mosaic is written as gray. |
| WebM | `.webm` | Save as, for a sequence. Export Sequence. | 8-bit frames as VP9, compressed with loss. A mosaic is written as gray. |

AfterNight writes no camera raw file and no MPG or MPEG video. A sequence saved under any extension the table does
not list for sequences, `.mpg` and `.mpeg` among them, is refused before anything is written. Some processes write files of their own, such as the [masters](../reference/glossary.md#master) of Image Integration: their
pages say which.

### What saving does to an image

- **The extension you type decides the format,** in upper or lower case, whatever filter the dialog shows. A still
  image saved under any other extension is refused with "the product writes FITS, XISF, TIFF, PNG and JPEG files
  only". A new profile proposes TIFF, which **Default Save Format** in **Settings > General** changes. The dialog
  proposes the view's name, in the folder you last opened from or saved to.
- **The image is written, not its display.** The [**Autostretch**](../reference/glossary.md#autostretch) display,
  the channel shown, the zoom, overlays, annotations and [masks](../reference/glossary.md#mask) are not saved. A process you applied is.
- **A type the format does not hold is converted** to the narrowest type of the format that holds it, or to the
  widest when none does, at the same share of full scale and rounded to the nearest code.
- **A mosaic keeps its filter pattern only where the format can state it.** A Bayer mosaic is written as FITS, XISF
  or TIFF. An X-Trans mosaic is written as XISF or TIFF only, because FITS states a mosaic in one two-by-two pattern.
  A save of a mosaic to any other format is refused before anything is written: debayer it first.
- **The options of a format** are those of **Settings > Export**: the compression of TIFF and XISF, the quality of
  JPEG, the compression level of PNG, an XISF checksum and the metadata. The save dialog has none of its own.
- **FITS and XISF carry the image's header cards.** TIFF, PNG and JPEG carry the part of the EXIF record their format
  allows, when **Embed EXIF metadata in exported files** is on, as it is on a new profile. A field the metadata
  library cannot write is left out while the others are written. The EXIF date is not the time of the save, and a
  FITS `GAIN` card is not written as the EXIF gain control. No file carries the image's processing [history](../reference/glossary.md#history), whatever
  **Embed processing history in FITS header** says.
- **A sequence is written frame by frame,** read from its source and written into the container the extension names.
  A save to a video container decodes and encodes every frame again: it is never a copy of the source file. A
  `.mov` file is written as an MP4 container under that name, and an `.mkv` file as Matroska.
- **An existing file is replaced only when the new one is complete.** A save that fails or is stopped leaves nothing
  at the destination, and an existing file as it was. Replacing a file keeps no backup of it.
- **A save that succeeds** adds the file to the recent files. When the view still shows what was written, the view
  takes the file as its own and its pending changes are cleared. AfterNight does not open the file again to check
  it.

**Export Sequence** writes its own choices: the **Output Format**, **SER**, **AVI (RAW)** or **MP4**, the **Codec** of
a video, **h264**, **h265** or **WEBM**, which writes a WebM file of VP9, and the **FPS**. It keeps the format and the
codec for the next export. Its **Debayer CFA**,
**Autostretch** and **Resize** change the frames it writes, so an export is a copy to share, not a master to process.
While it runs, its settings are locked, and its progress window can stop it. [Saving and Exporting](../interface/saving-and-exporting.md) describes the dialog.

**Image > Sequence > Convert** changes an open sequence into SER or an AfterNight sequence, in AfterNight's temporary
files. **Save as** then writes it where you choose.

## Choosing a format

| To keep | Choose | Because |
| --- | --- | --- |
| A master you will go on processing | FITS or XISF | They keep floating-point samples and the astronomical header. |
| A copy for another image editor | TIFF | It keeps the sample type. |
| A picture to show, without loss | PNG | It is lossless, at 8 or 16 bits. Keep the master as well. |
| A small picture to share | JPEG | It is small, at 8 bits with loss. Keep the master as well. |
| Frames not yet debayered | FITS, XISF or TIFF; XISF or TIFF for X-Trans | They state the filter pattern. |
| A planetary recording | SER | It keeps 16-bit frames, mosaics and each frame's time. |
| A list of frames to open again | AfterNight sequence | It is small. Keep its frame files with it. |
| A video to share | MP4 or WebM, from Export Sequence | It is small, at 8 bits with loss: never a master. |

Keep the files your camera wrote, and save each result under a new name. Lossless and uncompressed files of a large
image or of many frames take much disk space.

## When it goes wrong

A file is listed in the Open File dialog but does not open
:   **Cause.** Its extension is one AfterNight reads, and its content is not: a damaged file, a four-color camera
    sensor or a video stream in another codec.
:   **Fix.** Read the reason in the "File open error" box, and check the format's row above. Keep the original, and
    try a copy converted by the program that made it.

The Save as dialog offers no camera raw format
:   **Cause.** AfterNight reads camera raw files and never writes them.
:   **Fix.** Save your work as FITS, XISF or TIFF, and keep the raw files.

The saved file has fewer bits than the image
:   **Cause.** The format does not hold the image's sample type, and the save converted it.
:   **Fix.** Save the master as FITS, XISF or TIFF, under a new name.

The saved image looks dark, or lacks an overlay the view showed
:   **Cause.** A save writes the image, not its display.
:   **Fix.** Apply the [stretch](../reference/glossary.md#stretch) with a process before you save, or save the annotated picture from Annotate Image.

Image > Sequence > Export... is disabled
:   **Cause.** The active view holds an image, not a sequence.
:   **Fix.** Make a sequence's view active, then choose it again.

A sequence save to `.mpg` or `.mpeg` is refused
:   **Cause.** AfterNight writes no MPEG stream.
:   **Fix.** Save as SER, AVI, an AfterNight sequence, MP4, MOV, MKV or WebM, or export the sequence.
