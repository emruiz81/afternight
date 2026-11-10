---
kind: reference
summary: >-
  Format by format, the pixel types AfterNight reads and writes, how a save converts what a format cannot hold, the
  compression it offers and the metadata a file keeps, for still images, sequences and video.
rewritten: true
---
# File Formats

This page gives, format by format, the [pixel types](glossary.md#pixel-type) AfterNight reads and writes, the
compression it offers, how a save converts what a format cannot hold, and the metadata a file keeps. Still images come
first, then [sequences](glossary.md#sequence) and video. [Supported Formats](../getting-started/supported-formats.md)
lists the extensions and helps you choose a format, [Saving and Exporting](../interface/saving-and-exporting.md)
describes the commands that write a file, and [Sequences](../interface/sequences.md) describes how a sequence opens.

## Still images

The extension decides the format, in upper or lower case, when a file is opened and when it is saved. A filter chosen
in a file window does not. The options of a save are those of **Settings > Export**, read when you confirm the save:
the save window has none of its own, and none applies to a sequence. [Settings](../interface/settings-dialog.md)
describes each option.

### FITS

Extensions
:   `.fits`, `.fit` and `.fts`: a [FITS](glossary.md#fits) file, read and written. The save window's filter lists
    `.fit` and `.fits`.

What opens
:   The first image of the file: the primary image, an image extension or a tile-compressed image. Grayscale or
    color, in 8, 16 or 32-bit integers, signed or unsigned, or in 32 or 64-bit floating point, and a Bayer mosaic its
    header states.

64-bit integers
:   An image of 64-bit integers opens as 64-bit floating point while every value lies within \(\pm 2^{53}\), which
    floating point holds exactly. Beyond, it is refused with "FITS integer exceeds Float64's guaranteed exact range;
    preserve raw codes or acknowledge lossy decoding".

What a save writes
:   Every pixel type at the FITS type of its width. Signed 8-bit and unsigned 16 and 32-bit samples are stored with
    the standard's offset, `BZERO`, so the full unsigned 32-bit range comes back exactly. Color is written as three
    planes, and a Bayer mosaic with its pattern.

Compression
:   None is written, and no setting offers one. The stored value `export/fitsFloatFormat` is not read: the image's
    own type decides.

[Header cards](glossary.md#header-card)
:   The cards the image was read with are written back, its `HISTORY` cards and world coordinates among them. The
    cards that describe the data are written new, a long [processing state](glossary.md#processing-state) continues
    over `CONTINUE` cards, and a keyword longer than eight characters is written as a `HIERARCH` card.

### XISF

What opens
:   An [XISF](glossary.md#xisf) file in grayscale or color, in 8, 16 or 32-bit unsigned integers or 32 or 64-bit
    floating point, and Bayer and X-Trans mosaics. 64-bit integer and complex samples are refused, and so is a file
    whose pixel data fail their checksum.

What a save writes
:   The same types. A signed image is written as the unsigned integers of its width. Bayer and X-Trans mosaics keep
    their filter array.

Compression
:   None on a new profile. The XISF **Default Compression:** offers zlib, LZ4, LZ4-HC and Zstandard, all lossless,
    written in pieces of 4 MiB that a reader decodes one at a time. Every XISF reader reads zlib and LZ4, and only
    readers of Revision 1 of the specification read Zstandard. Beside a compression, **Shuffle the bytes of each
    sample before compressing**, on by default, stores every sample's first byte, then its second, which makes
    16-bit and floating-point images smaller. An 8-bit image is never shuffled.

Checksum
:   **Checksum:** at **SHA-256** writes a checksum of the pixel data. AfterNight checks it when the file opens, and
    refuses a file whose pixels do not match. The hash costs time at every save and every open.

Properties and keywords
:   Every property is read under its own name and written back as the type it was: a number, a vector, a matrix, or
    text with line breaks, such as a record of processing written into the file. The FITS keywords are carried too.
    When the keywords state no [plate solution](glossary.md#plate-solution), the one the properties store is read.

Cards XISF cannot hold
:   A save is refused before anything is written, and names the card, when the image carries a FITS keyword longer
    than eight characters or with text outside printable ASCII: "a FITS card is not one an XISF FITSKeyword element
    can hold: its name is more than eight characters or its text is not printable ASCII". Such a keyword comes from a
    capture program's `HIERARCH` card, which a FITS save keeps. A property whose name is not an XISF identifier is
    refused the same way.

### TIFF

Extensions
:   `.tif` and `.tiff`.

What opens
:   Grayscale, or color with or without an alpha channel, in 8, 16 or 32-bit integers, signed or unsigned, or in 32
    or 64-bit floating point, and a mosaic its tags state. A color TIFF compressed as JPEG opens in red, green and
    blue.

What is refused
:   A palette, white-is-zero, CMYK or any other [color model](glossary.md#color-model) whose values are not gray
    levels or red, green and blue: "this image states a photometric interpretation whose samples are not what they
    appear to be, and this engine refuses it rather than reading them as grey levels". Associated alpha is refused
    too. A TIFF of 64-bit integers or half-precision floating point gives "the file holds samples outside the eight
    this engine processes".

What a save writes
:   Every pixel type with its kind, signed 8-bit included, and Bayer and X-Trans mosaics with their pattern, in strips
    of about a quarter of a megabyte. A file whose pixels pass 3,500 MiB is written in the BigTIFF form, which
    addresses more than 4 GiB.

Compression
:   None on a new profile. The TIFF **Default Compression:** offers LZW and ZIP/Deflate, which keep every value, and
    JPEG, which loses some, at a fixed quality of 95 that the JPEG **Quality:** does not change. JPEG takes only 8-bit
    samples: a deeper image is written with LZW, and the Session Console says so.

### PNG

What opens
:   8 or 16-bit grayscale or color, with or without an alpha channel. A palette image opens in its palette's colors.

What a save writes
:   8 and 16-bit unsigned integers. Signed 8 and 16-bit images are written as unsigned integers of their width, and
    every wider type as 16-bit.

Compression
:   Deflate, at the **Compression Level:** from 0 to 9, 6 on a new profile. A higher level writes a smaller file more
    slowly, and the pixels read back are the same.

### JPEG

Extensions
:   `.jpg`, `.jpeg` and `.jpe`. The save window's filter lists `.jpg` and `.jpeg`.

What opens
:   8-bit grayscale or color. 12-bit and CMYK JPEG are refused, and so is a file that ends before its last row.

What a save writes
:   8-bit grayscale or color, compressed with loss at the **Quality:** from 1 to 100, 95 on a new profile. Every
    other type is converted to 8 bits.

### Camera raw

Camera [RAW](glossary.md#raw) files are read and never written. Supported Formats lists the extensions by maker.

- The file is decoded by LibRaw, or by a private build of RawSpeed for the cameras where that decoder gives the same
  samples and metadata.
- It opens under the **Raw Images** settings in force when you asked to open it. A mosaic's white balance is applied to
  its values, a full-color frame's by the decoder, and **Auto-rotate based on EXIF orientation** turns either by the
  orientation the camera recorded.
- A Bayer or X-Trans mosaic, or a full-color frame such as a linear DNG, opens as such. For a mosaic, the
  **Open CFA file** question decides whether it is turned into color: Supported Formats says how an X-Trans mosaic is.
- A four-color sensor, such as CMYG or CYGM, and a camera model the decoder does not know are refused.
- The camera's record is read where the file holds one. Save the processed image as FITS, XISF or TIFF, and keep the
  raw files.

## How a save converts pixel types

When the format holds the image's pixel type, the image is written in it. Otherwise it is written in the narrowest
type of the format that is at least as wide, or in the format's widest type when none is. Each value keeps its share
of full scale: it is computed in double precision and rounded to the nearest code, a value halfway between two codes
going to the even one. A floating-point image of physical values is converted as the
[fraction of full scale](glossary.md#fraction-of-full-scale) the viewer shows.

FITS and TIFF write every type as it is. The other formats write:

| Pixel type of the image | XISF | PNG | JPEG | [SER](glossary.md#ser) |
| --- | --- | --- | --- | --- |
| 8-bit unsigned | 8-bit unsigned | 8-bit unsigned | 8-bit | 8-bit unsigned |
| 8-bit signed | 8-bit unsigned | 8-bit unsigned | 8-bit | 8-bit unsigned |
| 16-bit unsigned | 16-bit unsigned | 16-bit unsigned | 8-bit | 16-bit unsigned |
| 16-bit signed | 16-bit unsigned | 16-bit unsigned | 8-bit | 16-bit unsigned |
| 32-bit unsigned | 32-bit unsigned | 16-bit unsigned | 8-bit | 16-bit unsigned |
| 32-bit signed | 32-bit unsigned | 16-bit unsigned | 8-bit | 16-bit unsigned |
| 32-bit floating point | 32-bit floating point | 16-bit unsigned | 8-bit | 16-bit unsigned |
| 64-bit floating point | 64-bit floating point | 16-bit unsigned | 8-bit | 16-bit unsigned |

AVI and the video formats write 8-bit unsigned frames, and an image list writes every frame in the first frame's type.

A mosaic keeps its filter pattern only where the format can state it. A save to another format is refused before
anything is written:

| Mosaic | Written as | Refused for |
| --- | --- | --- |
| Bayer | FITS, XISF, TIFF, SER and an image list | PNG and JPEG: "a colour filter array mosaic cannot be written as PNG or JPEG" |
| X-Trans | XISF and TIFF | FITS, PNG and JPEG: "an X-Trans mosaic cannot be written as FITS, PNG or JPEG" |

FITS states a filter pattern in one two-by-two card, which cannot describe an X-Trans array. AVI and the video formats
write a mosaic's values as gray, without its pattern. [Debayer](glossary.md#debayer) the mosaic first for color.

## Metadata

The [EXIF](glossary.md#exif) record
:   TIFF and JPEG carry the part of it their format allows, PNG a smaller part, with the image's other header cards,
    in the same write, while **Embed EXIF metadata in exported files** is on, as it is on a new profile. A field the
    metadata library cannot write is left out while the others are written. The record is not dated with the time of
    the save, and a FITS `GAIN` card is not written as the EXIF gain control. FITS and XISF carry their header
    whatever the setting says.

XMP
:   An XMP packet inside a TIFF, PNG or JPEG file is not read.

The processing [history](glossary.md#history)
:   No file carries it. **Embed processing history in FITS header** writes nothing, whatever it says. The `HISTORY`
    cards a FITS file was read with are carried as they are.

The plate solution
:   FITS and XISF carry the world coordinates the image holds. A process that turns or resizes the image has already
    dropped the cards that no longer fit, so no stale coordinates are written.

Orientation and alpha
:   Nothing is turned or removed when a file opens. A JPEG's or TIFF's recorded orientation is kept in its metadata and
    not applied, and only a camera raw file is turned, by its setting. An alpha channel opens with the image.

Sequences
:   A SER keeps each frame's exposure start, and an image list each frame's header cards: the exposure, the filter,
    the date and the world coordinates. AVI and the videos keep no astronomical metadata. A container's own metadata
    is not read into the image's history.

## Sequences and video

A sequence is saved frame by frame: every frame is read from the sequence's file and written into the container the
extension names, so a save under another extension converts the sequence. A save to a video container decodes and
codes every frame again, and is never a copy of the file. **Image > Sequence > Export...** writes SER, AVI, MP4 or
WebM with choices of its own - the coding, the rate, a debayer, a [stretch](glossary.md#stretch) and a resize - which
[Saving and Exporting](../interface/saving-and-exporting.md#export-a-sequence) describes.

### SER

What opens
:   Gray frames, Bayer mosaics in RGGB, GRBG, GBRG or BGGR, and RGB or BGR frames, of 8 or 16 bits, read by
    AfterNight itself. A recording that stopped before it wrote its frame count opens with its complete frames. A cyan, yellow and magenta
    mosaic opens as one gray plane of its values.

Frame rate
:   A SER has no field for a rate. It plays at one frame per mean interval between the earliest and the latest time its
    timestamp trailer records, in whole ticks of 100 nanoseconds.

What a save writes
:   Gray, Bayer mosaic or RGB frames, 8-bit frames as 8-bit unsigned and every other type as 16-bit unsigned. A mosaic
    stays a mosaic. Each frame's exposure start goes into the timestamp trailer, as unknown for a frame that states
    none, and the header's recording date is written as unknown.

### Image list

An image list, `.anseq`, is a short XML file that names image files in order. It holds no pixels.

What opens
:   The files the list names, each decoded when it is shown, in the pixel type of its file. A path in the list is
    taken as written when it is absolute, and from the list's own folder otherwise.

What it states
:   The first frame's width, height, channel count and whether it is a mosaic, the time it was made, the rate with
    four significant digits, written the same whatever the computer's language, and the frames' paths.

What a save writes
:   The list, and a folder beside it named `<name>-frames` that holds each frame as a FITS file,
    `<name>-frames/<name>-<index>.fits`, the index counted from 0: `M42-frames/M42-0.fits` is the first frame of
    `M42.anseq`. The list names each frame relative to its own folder, so the list and its folder move together.
    Every frame is written in the first frame's type, with its header cards.

**New Sequence**
:   An image list that **File > New Sequence...** makes names its files where they are, by their absolute paths. It
    works only while those files stay where they are.

Limits
:   A list is refused when its version is not 1, its elements nest more than 4,096 deep, or a frame path is empty, is
    not UTF-8, holds a NUL or is too long. A list holds at most 1,048,576 frames and its file at most 128 MiB.

### Reading a video

What opens
:   `.avi`, `.mp4`, `.mov`, `.mkv`, `.webm`, `.mpg` and `.mpeg` files, read by FFmpeg on the processor. AfterNight
    admits only the AVI, MP4 and MOV, Matroska and WebM, MPEG program and transport stream and NUT containers, and
    the codecs Supported Formats lists, even where FFmpeg could read another. Sound is never read.

How a frame arrives
:   Gray, mosaic and RGB frames arrive as the stream stores them. A color stream in luma and chroma is converted to
    RGB through the matrix and the range it states, or as BT.601 at limited range when it states neither. A stream of
    8 bits or fewer becomes 8-bit RGB, a wider one 16-bit RGB. A palette is looked up into RGB, and a palette left
    empty, as some AVI files hold, is read as gray.

Order
:   The frames come in the order a player shows them. Opening reads every packet header once, so a jump lands on
    exactly the frame asked for. FFmpeg opens no other file and no network address for a video.

### AVI

AfterNight writes AVI with its own writer. FFmpeg only reads it.

What a save writes
:   Uncompressed 8-bit frames: gray through a gray palette, or 24-bit color. Every deeper type is converted to 8 bits.
    The frames are stored the right way up, every byte that pads a row is zero, and the rate is stored as its exact
    fraction.

Size
:   A frame is at most 32,767 pixels wide and tall. The file is written in sections of at most 1 GiB, behind an index
    whose room is reserved when the save starts. A recording that would need more sections than the index holds is
    refused, not continued in a second file.

### MP4, MOV, MKV and WebM

FFmpeg writes these containers, through the software encoders x264 for H.264, x265 for H.265 and libvpx for VP9.

What a save writes
:   An `.mp4` or `.mov` file is an MP4 container and an `.mkv` file a Matroska one, both in H.264, and a `.webm` file
    is VP9. **Export Sequence** writes MP4 in H.264 or H.265, or WebM in VP9.

Frames
:   8-bit frames. Color is coded through the BT.709 matrix at limited range, and a gray frame stays gray. An odd width
    or height loses its last column or row, keeping the top left.

Quality
:   One fixed quality: a constant rate factor of 26 for H.264, 29 for H.265 and 32 for VP9. Each encoder works on one
    thread. Every coding loses detail, so a video is a copy to watch, never a [master](glossary.md#master).

MPG and MPEG
:   AfterNight writes no MPEG stream. A sequence read from an `.mpg` or `.mpeg` file is saved under another extension.

## What a save does with the file

- **The image is written, not its display.** The [Autostretch](glossary.md#autostretch) display, the channel shown,
  the zoom, overlays, annotations and [masks](glossary.md#mask) are not written. A process you applied is.
- **The image is written as it was when you confirmed the save.** An edit made while it is written reaches only the
  next save, and closing the view does not change the file.
- **A file is written whole or not at all.** A still image is written beside its destination and moved into place only
  when it is complete, and a sequence appears only when its last frame is in. A save that fails or is stopped leaves
  nothing at the destination, and an existing file as it was.
- **Replacing a file keeps no backup of it.** Save a result under a new name when the old file matters.
- **AfterNight does not read a file again to check it.** Open what you wrote before you delete its source:
  [Saving and Exporting](../interface/saving-and-exporting.md#check-what-you-wrote) says what to look at.
- **Every save works in the background**, behind a progress window, and you can go on working.

## What a format costs

- FITS, and XISF and TIFF without compression, are large but lose nothing. LZW, ZIP/Deflate, the XISF compressions
  and the PNG levels cost time when the file is written and opened, and change nothing in the pixels.
- JPEG, H.264, H.265 and VP9 make small files at the cost of detail and processor time. No size or time is promised
  for a setting: it depends on the image.
- An image list writes one FITS file for each frame and a list. Keep them together, with room for them all.
- The work of a sequence grows with its frames times their pixels. An export's stretch measures every frame, its
  resize needs memory for each frame, and an uncompressed AVI takes much disk space and disk speed.

## Troubleshooting

"the product writes FITS, XISF, TIFF, PNG and JPEG files only"
:   **Cause.** The name of a still image has no extension AfterNight writes, whatever filter the save window shows.
:   **Fix.** Save again with `.fits`, `.xisf`, `.tif`, `.png` or `.jpg` at the end of the name.

A sequence save to `.mpg` or `.mpeg` is refused
:   **Cause.** AfterNight writes no MPEG stream.
:   **Fix.** Save it as `.ser`, `.anseq`, `.avi`, `.mp4`, `.mkv` or `.webm`, or export it.

The saved file has fewer bits than the image
:   **Cause.** The format does not hold the image's pixel type, and the save converted it.
:   **Fix.** Keep the master as FITS, XISF or TIFF, and make a PNG or JPEG copy beside it.

A TIFF was written with LZW instead of JPEG
:   **Cause.** The image is deeper than 8 bits, and JPEG compression takes only 8-bit samples. The Session Console
    says so.
:   **Fix.** Keep LZW or ZIP/Deflate for a deep image, or save an 8-bit copy on purpose.

An EXIF field is missing from a TIFF, PNG or JPEG
:   **Cause.** The format does not allow the field, the field was in an XMP packet, the metadata library could not
    write it, or **Embed EXIF metadata in exported files** was off.
:   **Fix.** Keep the original file, and save astronomical fields in FITS or XISF.

A FITS file has no new `HISTORY` cards
:   **Cause.** No save writes the processing history.
:   **Fix.** Keep the record of your steps apart, and read the cards the file already had in the FITS Header window.

An XISF save is refused, naming a card
:   **Cause.** The image carries a FITS keyword longer than eight characters, a `HIERARCH` card, or text outside
    printable ASCII, which XISF cannot hold.
:   **Fix.** Save it as FITS, which keeps such a card.

An image list opens only on the computer that made it
:   **Cause.** **New Sequence** named its files by their absolute paths, or the list's frames folder was not moved
    with it.
:   **Fix.** Move the list together with its files and its frames folder, or make the list again from the files where
    they are now.

A saved image list's frames have another pixel type than some of the files
:   **Cause.** A save writes every frame in the first frame's type.
:   **Fix.** Put first the frame whose type the list should keep, or save the frames one by one.

An exported video is one pixel narrower or lower
:   **Cause.** A video stores an even width and height, and the last odd column or row is cut away.
:   **Fix.** Give the frames an even width and height, or resize them in the export.

"this image states a photometric interpretation whose samples are not what they appear to be"
:   **Cause.** The TIFF is a palette, white-is-zero, CMYK or another color model AfterNight refuses.
:   **Fix.** Convert it to a grayscale or RGB TIFF in the program that made it.

"FITS integer exceeds Float64's guaranteed exact range"
:   **Cause.** The 64-bit integer FITS holds a value beyond \(\pm 2^{53}\).
:   **Fix.** Convert it to a 32-bit integer or floating-point FITS with another program.

"the file holds samples outside the eight this engine processes"
:   **Cause.** The file stores 64-bit integers or half-precision floating point, such as some TIFF files.
:   **Fix.** Convert it to a 32-bit integer or floating-point file with another program.

A JPEG or TIFF opens turned on its side
:   **Cause.** AfterNight does not apply the orientation such a file records.
:   **Fix.** Turn the image with a geometry process.

A video does not open although a player shows it
:   **Cause.** Its container or codec is not one AfterNight admits.
:   **Fix.** Have the program that made it write a copy as SER, or as H.264 in MP4.

An X-Trans mosaic is refused for a FITS save
:   **Cause.** FITS states a filter pattern in one two-by-two card, which cannot describe an X-Trans array.
:   **Fix.** Save it as XISF or TIFF, or debayer it first.

## Credits

- [FITS Standard 4.0](https://fits.gsfc.nasa.gov/fits_standard.html), by the IAU FITS Working Group.
- [XISF 1.0 Specification](https://pixinsight.com/doc/docs/XISF-1.0-spec/XISF-1.0-spec.html), by Pleiades
  Astrophoto.
- [TIFF 6.0 specification](https://libtiff.gitlab.io/libtiff/specification/index.html) and
  [TIFF/JPEG Technical Note 2](https://libtiff.gitlab.io/libtiff/specification/technote2.html), as LibTIFF hosts them.
- [PNG Specification, Second Edition](https://www.libpng.org/pub/png/spec/iso/index-object.html).
- [Independent JPEG Group](https://ijg.org/) and
  [The JPEG Still Picture Compression Standard](https://www.ijg.org/files/Wallace.JPEG.pdf), by Gregory K. Wallace.
- [FFmpeg Formats Documentation](https://ffmpeg.org/ffmpeg-formats.html).
- [SER format description, version 3b](https://free-astro.org/images/5/51/SER_Doc_V3b.pdf).
