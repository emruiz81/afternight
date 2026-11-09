# File Formats

## Purpose, scope, audience, prerequisites, expected result, and authority

This is the authoritative guide to AfterNight's current file-format support,
sample precision, compression, and metadata behavior. It covers the standard
application open/save paths and Sequence Export. It does not promise support
for every camera model, every stream codec inside a recognized video container,
or every optional feature in the underlying format standards.

Use this reference to decide which artifact is a processing master, an external-
editor handoff, a display copy, or a sequence delivery.

## Reachability or applicability

- Open files with **File > Open...**, the Open toolbar action, recent files, or
  supported drag-and-drop paths.
- Save the active still or same-family sequence with **File > Save as...** or
  **Image > Actions > Save File...**. These are aliases and always prompt.
- Render a sequence to SER, raw AVI, MP4, or WebM with **Image > Sequence >
  Export...**.
- Configure initial/default still behavior under **Settings > General** and
  **Settings > Export**. The standard save dialog does not expose compression,
  quality, precision, or metadata controls itself.

The engine dispatches still codecs by filename extension. Sequence opening
dispatches `.ser` and `.anseq` natively and the allowed video suffixes through
FFmpeg. A recognized suffix is only the first gate: AfterNight admits only the
video containers (AVI, MOV/MP4, Matroska/WebM, MPEG program and transport
streams, NUT) and video decoders on its own lists, even when the installed
FFmpeg could decode more, and the decoder must still understand the file's
actual stream. A stream that states BT.709 is read as BT.709, and one that
states nothing as BT.601 at limited range. A video decodes on the processor
unless a device is named.

## Complete reference matrix

### Still-image compatibility, precision, and use

| Item/exact label | Type/unit/category | Default/current value | Valid values/constraints | Meaning/effect | Dependencies/applicability | Trade-off/side effect | Scope | Persistence/metadata |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| FITS (`.fit`, `.fits`, `.fts`) | Astronomy still | Standard save supported; dialog offers `.fit/.fits` | Mono or RGB; signed/unsigned 8/16/32-bit integer and 32/64-bit floating samples, using matching fixed-width FITS sample types. A `BITPIX = 64` integer image opens as 64-bit floating samples while every stored value is within ±2^53, and is refused otherwise | Preserves signed 8-bit and full-range unsigned 32-bit samples plus FITS-style astronomy metadata | OpenFITS; extension selects codec | No user-facing FITS compression; geometry changes can stale WCS/SIP | Open/save | Rewrites structural fields; carries FITS header/CFA/WCS/SIP where present; no save writes a processing history |
| XISF (`.xisf`) | Astronomy still | Standard save supported | Mono/RGB; native unsigned 8/16/32 and float 32/64. A signed image is written as the unsigned representation of its width. A Bayer or an X-Trans mosaic keeps its filter array | High-precision astronomy/PixInsight interchange | OpenXISF | Uncompressed unless **Settings > Export** names lossless zlib, LZ4, LZ4-HC or Zstandard; Zstandard is read only by readers of Revision 1 of the specification | Open/save | Carries FITS-like keywords and structured AfterNight color-calibration provenance; does not use generic EXIF preference |
| TIFF (`.tif`, `.tiff`) | Interchange still | Clean-profile default save format; compression None | Active 8/16/32-bit signed/unsigned or 32/64-bit floating sample type, including signed 8-bit reload. A palette, white-is-zero, separated-ink or subsampled-chroma TIFF is refused by name; a 64-bit integer or half-precision float TIFF is not opened | Preserves active sample width/type for external editing | LibTIFF; reader needs matching sample/compression support | Potentially large; JPEG compression is lossy and only used for 8-bit samples | Open/save | Best-effort allowed EXIF subset when enabled; astronomy metadata is not authoritative |
| PNG (`.png`) | Lossless display still | Compression level 6 | Unsigned 8/16 native; signed 8/16 convert to matching unsigned; 32-bit integer/float convert to unsigned 16; a colour filter array mosaic is refused. A palette PNG opens in its table's colours, and an alpha channel is kept | Lossless encoding of the converted samples | libpng | Precision conversion can clip/quantize relative to the processing master | Open/save | Best-effort small EXIF subset when enabled; no general astronomy header |
| JPEG (`.jpg`, `.jpeg`, `.jpe`) | Lossy display still | Quality 95 | Mono/RGB unsigned 8 in file; every other input converts to unsigned 8; a colour filter array mosaic is refused. A camera's recorded orientation is kept in the metadata, not applied | Small sharing/preview copy | libjpeg-turbo | Lossy, precision-reducing, unsuitable as sole processing master | Open/save | Best-effort allowed EXIF subset when enabled; no astronomy header authority |
| Camera RAW | Acquisition still | Input only | `.nef`, `.crw`, `.cr2`, `.cr3`, `.dng`, `.arw`, `.sr2`, `.srf`, `.dcr`, `.k25`, `.kdc`, `.erf`, `.mrw`, `.orf`, `.pef`, `.raf`, `.raw`, `.x3f` | Decodes camera data according to the RAW settings in effect when the open was asked for: a mosaic's white balance applied to its codes, a full-colour frame's by the decoder, and auto-rotate turning either by the camera's recorded orientation. A four-colour (CMYG, CYGM) sensor is refused; an X-Trans (Fujifilm) mosaic opens in colour, demosaiced by the X-Trans counterpart of the RAW setting's method (the fast X-Trans estimate for **Bilinear**, three-pass Markesteijn in camera RGB for every other), or as its mosaic when the debayer question is answered **No** | Bundled LibRaw and supported camera/variant | Decoding choices can change color/orientation; cannot save back to RAW | Open only | Source camera metadata is read where available; save processed output separately |

### Compression and save-option behavior

| Item/exact label | Type/unit/category | Default/current value | Valid values/constraints | Meaning/effect | Dependencies/applicability | Trade-off/side effect | Scope | Persistence/metadata |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| TIFF compression | Encoding option | None | None; LZW; ZIP/Deflate; JPEG | Selects TIFF storage codec | Standard TIFF save | None/LZW/ZIP preserve decoded samples; JPEG is lossy | All later TIFF saves | `export/tiffCompression` |
| TIFF JPEG fallback | Compatibility rule | Automatic | JPEG requested with non-8-bit samples | Uses LZW instead of JPEG | TIFF save only | Output is lossless but not the requested compression; no dialog warning, but the session console says so | One TIFF save | Not separately persisted |
| JPEG quality | Integer | 95 | 1-100, the Export page's range | Controls lossy JPEG and 8-bit JPEG-in-TIFF quality | JPEG save; 8-bit TIFF JPEG | Higher usually trades size for fidelity; no project-specific ratio is guaranteed | All later JPEG/JPEG-TIFF saves | `export/jpegQuality` |
| PNG compression | Integer level | 6 | 0-9, the Export page's range | Controls DEFLATE effort | PNG save | Higher can reduce size and increase write time; decoded pixels are unchanged | All later PNG saves | `export/pngCompression` |
| FITS float format | Hidden/stored preference | `float32` | Stored string | No effect in standard current save | No visible Export control; options builder does not read it | Do not infer output precision from this key; active pixel type controls FITS output | None in current path | `export/fitsFloatFormat` |
| XISF compression | Encoding option | None | None; zlib; LZ4; LZ4-HC; Zstandard | Selects the codec of the pixel data, written in pieces of 4 MiB a reader decodes one at a time | XISF save | Every choice is lossless; zlib and LZ4 are read by every XISF reader, Zstandard only by readers of Revision 1 of the specification | All later XISF saves | `export/xisfCompression` |
| XISF byte shuffling | Boolean | On | Off/On, offered beside a compression | Stores every sample's first byte, then its second, before compressing | Compressed XISF save; 8-bit samples are not shuffled | Usually smaller 16-bit and floating-point files; decoded pixels are unchanged | All later compressed XISF saves | `export/xisfByteShuffle` |
| XISF checksum | Selector | None | None; SHA-256 | Writes a checksum of the pixel data, verified when the file is read | XISF save | A file whose pixels fail their checksum is refused when it is opened; hashing costs time on save and open | All later XISF saves | `export/xisfChecksum` |
| Embed EXIF | Boolean | On | Off/On | A TIFF/PNG/JPEG save carries the camera's EXIF record and the image's other header cards, within the format's allowed subset, in the same write | Not used for FITS/XISF, which always carry the header | An EXIF field the metadata library cannot write is skipped while the others are written, so save success does not prove every field was kept | Later display/interchange saves | `export/embedExif` |
| Embed processing history in FITS header | Boolean | Off | Off/On | Writes nothing in this release, and the Export page says so | FITS save | Enabling it adds no history; the cards the image was read with, `HISTORY` among them, are carried as they are | None | `export/embedHistory`; existing header HISTORY is separate |
| Sequence Export: Debayer CFA | Boolean | Off on every dialog open | Off/On; applicable to CFA source frames | Converts CFA mosaics to RGB before encoding exported frames | Sequence Export only; the source sequence supplies the demosaic method | Output is no longer a CFA mosaic and can be larger; leave Off when retaining native mosaic data | All frames in one export operation | Not persisted; snapshotted and locked before the worker starts |

### Sequence and video compatibility

| Item/exact label | Type/unit/category | Default/current value | Valid values/constraints | Meaning/effect | Dependencies/applicability | Trade-off/side effect | Scope | Persistence/metadata |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| SER (`.ser`) | Native sequence | Open, same-family save, export | Opens mono, Bayer RGGB/GRBG/GBRG/BGGR, RGB and BGR in unsigned 8/16 bits; a recording that stopped before its frame count was written opens with its complete frames; a CMY mosaic (YCMY/YMCY/MYYC) opens as one grey plane of its codes; the frame rate is the one its timestamp trailer gives - one frame per mean interval between the trailer's earliest and latest instants, in whole 100 ns ticks | Native planetary/lucky-imaging sequence | Engine SER reader/writer | Written frames are mono, Bayer mosaic or RGB: unsigned and signed 8-bit frames as unsigned 8, every other representation as unsigned 16 | Whole sequence | Writes the geometry, colour layout and frames, and each frame's exposure start in the timestamp trailer (an unknown entry for a frame that states none); the header's recording date is written as unknown; automatic debayer can create RGB copy |
| AfterNight Image List (`.anseq`) | Native sequence manifest | Open and same-family save | XML manifest naming its frames: **New Sequence** names each file where it is, by absolute path; a save writes every frame as `<stem>-frames/<stem>-<i>.fits` beside the manifest and names it relative to the manifest's directory. A manifest named without a directory finds its frames beside itself; a manifest whose version is not 1, that nests deeper than 4,096 elements, or that names a frame path that is not UTF-8, holds a NUL or is too long is refused | Lightweight/lazy ordered frame sequence | Referenced files must remain readable | Not self-contained; a saved frame is a FITS file with its header cards, in the first frame's representation | Whole sequence | Stores the first frame's width, height, channel count and whether it is a mosaic, the creation time, the frame rate (four significant digits, written the same in every locale), and the frame paths |
| AVI (`.avi`) | Video container | Open through FFmpeg; same-container save; raw AVI export | Input stream must decode; a raw AVI is written from decoded mono or RGB frames converted to unsigned 8, a mosaic as grey, stored upright with the exact frame-rate fraction, every row-padding byte zero, in sections of at most 1 GiB | Video input or very large uncompressed delivery | FFmpeg for input; native AVI writer for output | Raw AVI is large; no astronomy metadata archive; a recording that would need more 1 GiB sections than the index reserves is refused | Whole sequence | Standard save proposes `.avi` and writes a raw AVI frame by frame |
| MP4 (`.mp4`) | Video container | Open; same-container save; H.264/H.265 export | Admitted decoder for input; frames written in 8 bits, a mosaic as grey, colour through BT.709, encoded with one thread; an odd width or height is cropped by one column or row | Compact delivery video | FFmpeg; the software encoders libx264 and libx265 | Lossy; metadata/history not preserved | Whole sequence | Standard save proposes `.mp4` and writes H.264; export stores selected FPS and persists format/codec |
| WebM (`.webm`) | Video container | Open; same-container save; VP9 export | Admitted decoder for input; frames written as for MP4, an odd width or height cropped by one | Compact VP9 delivery video | FFmpeg; the software encoder libvpx-vp9 | Lossy; not a science master | Whole sequence | Standard save proposes `.webm` and writes VP9; export stores selected FPS and persists its selector |
| MOV/MKV/MPG/MPEG | Video input containers | Open; same-container save for `.mov` and `.mkv` | `.mov`, `.mkv`, `.mpg`, `.mpeg`; actual stream must decode | Input/inspection/extraction; a `.mov` save is written as an MP4 container under that name and a `.mkv` save as Matroska, both H.264 | FFmpeg | No matching general export choice; transcode through supported Export output. A `.mpg`/`.mpeg` save is refused, because AfterNight writes no MPEG stream | Whole sequence | Standard save proposes the recognized source suffix; source container metadata is not promoted into AfterNight image history |

## How to use this reference

1. Identify whether the output is a processing master, interchange copy,
   display image, sequence archive, or delivery video.
2. Find the format row and confirm its precision conversion, metadata, and
   dependency constraints before saving.
3. Configure global still options in Settings if necessary. For a sequence
   delivery, use Sequence Export and review FPS/autostretch/resize explicitly.
4. Write to a new path when the old file matters. A still save replaces an
   existing file only once the new one is complete, and creates no backup.
5. Reopen the result. For stills, check dimensions, channels, pixel type, and
   required FITS/XISF/EXIF fields. For sequences, check container/family, frame
   count, dimensions, sample depth, FPS, first frame, and last frame.

## Implementation behavior and model

### Extension dispatch and display versus data

Still codec selection uses the final filename suffix, case-insensitively. A
selected file-dialog filter does not override a conflicting suffix. The
standard still save passes the underlying image to the codec; viewer
autostretch, channel selection, masks, annotations, zoom, and overlays are not
flattened. An applied processing operation changes the underlying image and is
therefore saved.

On load nothing is removed or turned: an image with an alpha channel opens with
it, and no still codec applies an orientation - a JPEG's or TIFF's recorded
orientation is kept in its metadata, not applied to the pixels. Only a camera
RAW frame is turned, by **Auto-rotate**. FITS/XISF read their own headers; the
raster formats read EXIF through Exiv2, except an embedded XMP packet, which is
not read. A file whose samples are none of the eight representations AfterNight
processes - a 64-bit integer or half-precision float TIFF, for one - is refused
with **the file holds samples outside the eight this engine processes**.

### Numeric conversion

FITS and TIFF are the broadest standard save paths for retaining the active
sample type. FITS uses the signed-byte convention for signed 8-bit samples and
fixed-width signed/unsigned transfers for 32-bit samples; focused tests include
the full unsigned 32-bit range. TIFF reloads the stored signedness at every
supported width, including signed 8-bit. XISF natively maps AfterNight's
unsigned and floating types; a signed image is written as the unsigned
representation of its width. PNG accepts only unsigned 8/16 and converts
unsupported types to those depths. JPEG always converts to unsigned 8. These
conversions operate on the actual numeric image, not on a display rendering, at
corresponding full scales to the nearest value; floating data in physical units
is converted as a fraction of full scale; a value exactly halfway between two
codes goes to the even one. A Bayer (two-by-two) mosaic is saved only as FITS,
XISF, or TIFF, which record the filter array; a PNG or JPEG save of one is
refused before anything is written. An X-Trans mosaic is saved only as XISF or
TIFF: FITS states a mosaic's filters in one two-by-two pattern card, so a FITS,
PNG or JPEG save of one is refused before anything is written, the message
naming XISF and TIFF.

SER creation accepts unsigned 8/16 natively. Signed 8 converts to unsigned 8;
signed 16 and types above unsigned 16 convert to unsigned 16. Raw AVI also
converts every frame to unsigned 8. Video encoders consume decoded frames
converted to unsigned 8 and produce delivery encodings; they are not
precision-preserving masters. Raw AVI and video write a mosaic frame as grey,
while SER keeps it a mosaic.

### Sequence representation and export geometry

Standard sequence Save reads every frame of the source and writes it, through
the same writer Sequence Export uses, into the container the destination's
extension names: SER, an image list, raw AVI, or MP4, Matroska or WebM video.
Nothing is published until the last frame is in, so a failed or stopped save
leaves nothing at the destination. Image List creates a new manifest and a FITS
frame directory. No container is a byte copy of the source.

Sequence Export snapshots its settings before starting the worker and decodes
each source frame. A CFA source remains mosaic by default; **Debayer CFA** can
request RGB frames, demosaiced by the method the sequence states. Export can
then stretch each frame as **Apply Stretch** does, through the presentation the
view shows, and use Lanczos4 to resize. Fit preserves aspect inside a black
canvas; Fill preserves aspect and center-crops; Stretch changes aspect; Center
adds black canvas without scaling. A video frame of an odd width or height is
cropped by one column or row, and no other size rule is applied. Frames are
stored upright, and a failed export leaves nothing at its path.

## Inputs, outputs, persistence, and metadata

The standard save input is the active view's underlying still or sequence. A
successful save adds the chosen destination to recent files and clears the
standard view's pending-change marker, but it does not change the image/
sequence object's source filename or attach the view to the new destination.
There is no separate plain Save/write-back action.

Still output naming begins with the view's unique name in the shared last-open
directory. The clean-profile default filter is TIFF. The filename extension
chooses the codec. Sequence same-family filters depend on whether the active
view is SER, image list, or video. Video save preserves a recognized source
suffix and otherwise falls back to `.mp4`; Sequence Export normalizes the path
to the selected writer's `.ser`, `.avi`, `.mp4`, or `.webm` suffix. `.anseq`
output is incomplete without its sibling frame directory.

FITS and XISF carry the header cards the image was read with while
regenerating structural fields. An XISF file's properties are read whole under
their own names and written back as the types they were - a number, a vector, a
matrix - text with line breaks, such as a processing history written into the
file, included; when an XISF file's FITS keywords state no plate solution, the
solution its properties store is read. TIFF/PNG/JPEG carry a format-specific
allowed EXIF subset when Embed EXIF is on, as part of the same write; the save
does not stamp the EXIF record with the time of the write or write a FITS `GAIN`
card as `Exif.Photo.GainControl`. No file carries an image's processing history.

A still save is written beside its destination and moved into place only when it
is complete, so a failed or stopped save leaves nothing and an existing
destination as it was. Normal sequence cancellation removes expected partial
output and does not clear the view's pending-change marker or add the cancelled
path to recent files.

## Performance and resource implications

- FITS and uncompressed XISF/TIFF can be large but avoid display-format
  precision loss. TIFF LZW/ZIP and PNG add lossless compression work.
- JPEG and H.264/H.265/VP9 trade fidelity and CPU for smaller delivery output.
  The guide makes no fixed compression-ratio or time claim because image
  content, dimensions, encoder, and platform dominate.
- Image-list saving writes many FITS files and an XML list. Plan for directory
  entry overhead, free space, and the need to keep all files together.
- Sequence work scales with decoded pixels multiplied by frames. Autostretch
  adds statistics/transforms per frame; Lanczos4 resize adds temporary buffers;
  raw AVI has particularly high disk bandwidth/space demand.
- Standard still save and sequence work run on background workers. A still save
  writes the image as it was when the save was confirmed; editing or closing the
  view meanwhile does not change the file.

## Worked lookup example

- Evidence mode: Source-and-test-derived.
- Question, lookup path, verified conclusion, and revision/platform/data
  limitation: "Which output should retain a float RGB processing master and
  which should be sent for web sharing?" The still matrix shows FITS, TIFF, and
  XISF retain 32-bit floating samples;
  PNG converts float to unsigned 16 and JPEG converts to unsigned 8 with lossy
  encoding. Therefore save and reopen a FITS/XISF/TIFF master first, then make a
  separate PNG/JPEG delivery copy. Codec round-trip and metadata tests support
  the implementation model, but this source-derived lookup does not prove a
  particular external editor, file-size ratio, EXIF round trip, or visual-
  quality threshold.

## Warnings and limitations

- A recognized extension is not proof that the file contents are valid or that
  the deployed FFmpeg/LibRaw build supports a particular stream/camera.
- Output is not automatically read back or checksummed. A success notification
  proves only that the surfaced writer path completed without a reported error.
- A still save is whole or absent, but it still replaces an existing file once
  complete. Use a new path for irreplaceable data.
- Standard video sequence Save re-encodes every frame in 8 bits and is lossy.
  A `.mpg`/`.mpeg` destination is refused. Retain the original.
- An image list's saved frames are FITS files in the first frame's
  representation; a frame of a deeper representation is narrowed to it.
- An X-Trans mosaic cannot be saved as FITS; save it as XISF or TIFF, or
  demosaic it first.
- JPEG, raw AVI, and video encoders are not science-master formats. PNG also
  reduces float/32-bit integer data to unsigned 16.
- TIFF JPEG falls back to LZW above 8 bits, as the session console says. XISF
  is written uncompressed unless **Settings > Export** names a compression. FITS has no exposed compression control in the
  standard path.
- An XISF save is refused, before anything is written and naming the card, when
  the image carries a FITS keyword longer than eight characters (the HIERARCH
  convention some capture programs use) or with text outside printable ASCII,
  or a property whose name is not an XISF identifier. An image refused only for
  a long keyword can still be saved as FITS, which writes it as a HIERARCH card.
- The processing-history checkbox writes nothing; no file carries an image's
  processing history.
- Metadata presence is not metadata validity. Crop, rotate, resize, alignment,
  and other geometry operations can invalidate WCS/SIP even when keywords remain.
- `.anseq` is a manifest, not a single-file archive. Moving/deleting its frame
  directory or referenced source images breaks it.

## Troubleshooting

| Symptom/question | Verified interpretation or cause | Resolution/next page | Evidence source |
| --- | --- | --- | --- |
| Which format keeps float samples? | FITS, TIFF, and XISF map 32/64-bit float; PNG/JPEG do not. | Keep an astronomy-native/interchange master, then make delivery copies. | FITS/TIFF/XISF/PNG/JPEG codec sources and round-trip tests |
| Save rejects a filename | Extension is missing/unsupported even if a broad filter was selected; a still save names only FITS, XISF, TIFF, PNG and JPEG. | Use a recognized still extension; see [Saving And Exporting](../interface/saving-and-exporting.md). | `FilePolicies.cpp`; `CodecFormat.cpp` |
| TIFF used LZW instead of JPEG | The written samples were not 8-bit; the session console says so. | Keep LZW/ZIP for lossless high-depth output or deliberately make an 8-bit delivery copy. | `FilePolicies.cpp` |
| FITS has no new HISTORY cards | No save writes a processing history. | Preserve provenance separately and inspect existing header records. | `FilePolicies.cpp`; `ExportSettingsPage.cpp` |
| EXIF field disappeared | Field was outside the format's allowed subset, an embedded XMP packet, a field the metadata library could not write, or Embed EXIF was off. | Retain source metadata; verify after reopening; use FITS/XISF for astronomy fields. | `CodecMetadataPolicy.h`; `ExifCards.cpp` |
| `.anseq` works only on the original machine/path | Manifest frame paths or sibling directory are missing; **New Sequence** names its files by absolute path. | Move the whole artifact or rebuild the list from available frames. | `ImageListManifest.h`; `NewSequenceControl.cpp` |
| A saved image list's frames have another sample type than some sources | A save writes every frame in the first frame's representation, with its header cards. | Make the first frame the one whose representation the list should keep, or save the frames individually. | `SequenceContainers.cpp`; `SequenceContainersTest.AnImageListWrittenKeepsEachFramesCards` |
| Video opens but export dimensions change | An odd width or height is cropped by one column or row, or the chosen resize preset applies. | Use an even width and height; verify output. | `SequenceContainers.cpp`; `ExportSequenceControl.cpp` |
| Video sequence Save is refused for a `.mpg` or `.mpeg` file | AfterNight writes no MPEG stream. | Save with an `.mp4`, `.mkv` or `.webm` name, or use [Sequence Export](../interface/saving-and-exporting.md#export-a-sequence) to a new file. | `SequenceContainers.cpp`; `SequenceCompanions.cpp` |
| A TIFF does not open, naming its photometric interpretation | Palette, white-is-zero, separated-ink and subsampled-chroma TIFFs are refused. | Convert it to an RGB or grey TIFF with another tool. | `TiffCodec.h` |
| A 64-bit integer FITS does not open | A stored value lies beyond ±2^53. | Convert it to a 32-bit or floating FITS with another tool. | `FitsCodec.h` |
| A JPEG or TIFF opens unrotated | No still codec applies an orientation. | Turn it with the view's rotate commands or a geometry process. | `JpegCodec.h`; `TiffCodec.h`; `ImageViewFiles.cpp` |
| A video does not open although another player shows it | Its container or decoder is outside AfterNight's admission lists. | Convert it to a listed container and codec with another tool. | `FfmpegSequence.h`; `SequenceContainers.cpp` |
| An X-Trans mosaic is refused for a FITS save, naming XISF or TIFF | FITS states a mosaic's filters in one two-by-two pattern card, which cannot describe an X-Trans array. | Save as XISF or TIFF, or demosaic the mosaic first. | `FilePolicies.cpp`; `FilePoliciesTest.AnXTransMosaicIsRefusedForFitsWhichStatesATwoByTwoPatternOnly` |

## Related pages

- [Supported Formats](../getting-started/supported-formats.md) is the concise
  compatibility list; this page remains authoritative for precision,
  compression, and metadata details.
- [Saving And Exporting](../interface/saving-and-exporting.md) documents exact
  actions, controls, naming, overwrite, progress, cancellation, and verification.
- [Sequences](../interface/sequences.md) covers sequence creation, playback,
  extraction, conversion, and temporary state.
- [Settings](../interface/settings-dialog.md) documents persistent defaults and
  their current limitations.
- [FITS Header, EXIF, And Astrometry Metadata](../interface/fits-header-viewer.md) and [Image
  Summary](../interface/image-summary.md) and
  [Image Statistics](../interface/image-statistics.md) help verify reopened output.
- [Troubleshooting And Diagnostics](troubleshooting.md) routes open, save,
  metadata, sequence, and external-tool symptoms to the specific remedies.

## References and provenance

- [FITS Standard 4.0](https://fits.gsfc.nasa.gov/fits_standard.html), official
  IAU FITS Working Group reference, rechecked 2026-07-20.
- [XISF 1.0 Specification](https://pixinsight.com/doc/docs/XISF-1.0-spec/XISF-1.0-spec.html),
  PixInsight primary specification, reviewed on 2026-07-19.
- [TIFF specification index](https://libtiff.gitlab.io/libtiff/specification/index.html)
  and [TIFF/JPEG Technical Note 2](https://libtiff.gitlab.io/libtiff/specification/technote2.html),
  primary LibTIFF-hosted references, accessed 2026-07-19.
- [PNG Specification, Second Edition](https://www.libpng.org/pub/png/spec/iso/index-object.html),
  primary specification, accessed 2026-07-19.
- [Independent JPEG Group](https://ijg.org/) and
  [JPEG Still Picture Compression Standard](https://www.ijg.org/files/Wallace.JPEG.pdf),
  primary implementation/technical references, accessed 2026-07-19.
- [FFmpeg Formats Documentation](https://ffmpeg.org/ffmpeg-formats.html), primary
  libavformat documentation, rechecked 2026-07-20.
- [SER format description, version 3b](https://free-astro.org/images/5/51/SER_Doc_V3b.pdf),
  primary format description, reviewed on 2026-07-19.
- AfterNight source and tests, reconciled 2026-10-18.

<!-- No screenshot is required for this reference matrix. -->
