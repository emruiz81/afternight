---
kind: process
stage: any
menu:
  - Image > Actions
  - Image > Extract Channel
  - Image > Convert Format
  - Image > Fast Geometry
  - Image > Mask
  - Image > Sequence
  - Process > Adjustments
summary: >-
  The actions of the Image and Process menus that run at once, without a parameter window, and what each one does.
---
# Non-Interactive Processes

## Purpose and expected result

This page is the reference for actions that run immediately from **Image** or
**Process** without a parameter window, plus the Sequence actions that hand off
to a dedicated dialog. Use it to answer three questions before clicking: whether
the active view is eligible, whether the source is changed, and what happens to
precision and metadata.

Most entries start a background job. A still-image extraction or precision
conversion opens a new view and leaves the source unchanged. Fast geometry,
grayscale/RGB conversion, and Negative Transformation replace the active view's
current data. Sequence extraction and precision conversion create new temporary
sequences; fast geometry, grayscale/RGB, and negative replace the active
sequence with a processed temporary sequence. Save any result that must outlive
the session.

## At a glance

| Item | Current behavior |
| --- | --- |
| Menu path | **Image > Actions**, **Image > Extract Channel**, **Image > Convert Format**, **Image > Fast Geometry**, **Image > Mask**, **Image > Sequence**, and **Process > Adjustments** |
| Enabled when | An active view contains an image. Color-component extraction and Convert to Grayscale require RGB; Convert to RGB requires monochrome. Sequence-only actions require a sequence. Registry launch guards can also reject a busy or incompatible view. |
| Inputs | Active still image or sequence; an optional assigned mask for Negative Transformation; a second compatible image view when selecting a mask |
| Outputs | New still/sequence views for duplication, frame/channel extraction, and precision conversion; replacement view data for geometry, grayscale/RGB, negative, and sequence-container conversion; mask association/display-state changes; dialog/file output for Extract Sequence and Export |
| Recommended image state | Channel extraction and precision conversion are data-management operations. Negative is a nonlinear inversion. Grayscale and perceptual color-space components should be chosen according to the intended workflow, not as a substitute for calibration. Geometry is valid in either state but invalidates WCS/SIP. |
| Preview support | None. Selecting a reachable immediate action starts it; use Duplicate when a reversible comparison is needed. |
| Sequence support | Yes, where identified below. Sequence jobs process every frame and show progress; the current Extract Frame action produces one still. |
| Mask support | Negative Transformation honors the active mask. Mask-menu actions manage the association and display state. Channel extraction, precision conversion, geometry, grayscale/RGB, duplication, frame extraction, and container conversion do not blend through the active mask. |
| External dependencies | None beyond AfterNight's built-in engine and codecs. Export and Extract Sequence use their separately documented dialogs. |

## Before you start

1. Select the intended view immediately before choosing an action; every action
   resolves the current active view.
2. Save important work before an in-place/replacement operation. Image History
   records successful processing steps, but temporary sequence backing files are
   session artifacts rather than durable exports.
3. Check the view type. The whole **Image > Sequence** menu is disabled for a
   still. Color extraction and Convert to Grayscale are disabled for monochrome;
   Convert to RGB is disabled for RGB.
4. Expect all Fast Geometry actions to remove the WCS/SIP solution, because the
   pixel coordinate system changes, and every header card but the frame's
   observing context - its optics, site, pointing, camera settings, filter,
   object, frame type and date - and its integration cards, the `EXPTIME`,
   `TOTEXP`, `LIVETIME`, `STACKCNT` and `NCOMBINE` it states.

## Procedure

1. Activate the source view and open the submenu named in the matrix.
2. Confirm that the exact action is enabled. A disabled item indicates an
   incompatible color, pixel, sequence, or mask state; it is not a preview state.
3. For an output-producing immediate action, select it once. Sequence jobs show a
   progress dialog; still-image jobs may complete without one.
4. Wait for the new view or the active-view update. Do not start another exclusive
   sequence operation while the view is locked.
5. Verify the new view name, dimensions, channel count, pixel type, and Image
   History entry. For geometry, also confirm that WCS/SIP is no longer present.
6. Save or export the result if it must persist beyond the current session.

## Complete parameter and action reference

There are no numeric parameters or persisted presets. “Registered” means the
menu action is dispatched through `ProcessRegistryBuiltins`; “direct” means
`MenuActionManager` connects it directly to a coordinator. The following tables
cover all 67 reachable actions in this family and the one declared-but-unreachable
Automatic MTF action.

This canonical inventory supplies the frozen-template fields; the grouped
matrices immediately below supply each action's exact route, output, precision,
metadata, and state consequences.

| UI label/action | Type and unit | Runtime default | Persisted-default exception | Valid values, step, or constraints | Effect of increasing, decreasing, or selecting | Dependencies and enable/visibility rules | Quality, artifact, runtime, memory, or disk trade-off | Scope: preview/apply, channel, mask, still/sequence | Persistence |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Duplicate | Immediate action | N/A | None | Active view | See grouped matrix | Active image | See grouped matrix | Full still/sequence | New view/history |
| Extract Red Channel | Immediate action | N/A | None | RGB | See grouped matrix | RGB only | One output | R; still/sequence | New view/sequence |
| Extract Green Channel | Immediate action | N/A | None | RGB | See grouped matrix | RGB only | One output | G; still/sequence | New view/sequence |
| Extract Blue Channel | Immediate action | N/A | None | RGB | See grouped matrix | RGB only | One output | B; still/sequence | New view/sequence |
| Extract RGB Channels | Immediate action | N/A | None | RGB | See grouped matrix | RGB only | Three outputs | RGB; still/sequence | Three views/sequences |
| Extract CIE L* | Immediate action | N/A | None | RGB | See grouped matrix | RGB only | Conversion + output | L*; still/sequence | New view/sequence |
| Extract CIE a* | Immediate action | N/A | None | RGB | See grouped matrix | RGB only | Conversion + output | a*; still/sequence | New view/sequence |
| Extract CIE b* | Immediate action | N/A | None | RGB | See grouped matrix | RGB only | Conversion + output | b*; still/sequence | New view/sequence |
| Extract CIE c* | Immediate action | N/A | None | RGB | See grouped matrix | RGB only | Conversion + output | c*; still/sequence | New view/sequence |
| Extract CIE h* | Immediate action | N/A | None | RGB | See grouped matrix | RGB only | Conversion + output | h*; still/sequence | New view/sequence |
| Extract CIE a*=R / b*=G | Immediate action | N/A | None | RGB | See grouped matrix | RGB only | One encoded output | a*/b*; still/sequence | New view/sequence |
| Extract CIE L*a*b* | Immediate action | N/A | None | RGB | See grouped matrix | RGB only | Three outputs | Lab; still/sequence | Three views/sequences |
| Extract CIE L*c*h* | Immediate action | N/A | None | RGB | See grouped matrix | RGB only | Three outputs | LCh; still/sequence | Three views/sequences |
| Extract CIE X | Immediate action | N/A | None | RGB | See grouped matrix | RGB only | Conversion + output | X; still/sequence | New view/sequence |
| Extract CIE Y | Immediate action | N/A | None | RGB | See grouped matrix | RGB only | Conversion + output | Y; still/sequence | New view/sequence |
| Extract CIE Z | Immediate action | N/A | None | RGB | See grouped matrix | RGB only | Conversion + output | Z; still/sequence | New view/sequence |
| Extract CIE XYZ | Immediate action | N/A | None | RGB | See grouped matrix | RGB only | Three outputs | XYZ; still/sequence | Three views/sequences |
| Extract HSL Hue | Immediate action | N/A | None | RGB | See grouped matrix | RGB only | Conversion + output | H; still/sequence | New view/sequence |
| Extract HSL Saturation | Immediate action | N/A | None | RGB | See grouped matrix | RGB only | Conversion + output | S; still/sequence | New view/sequence |
| Extract HSL Lightness | Immediate action | N/A | None | RGB | See grouped matrix | RGB only | Conversion + output | L; still/sequence | New view/sequence |
| Extract HSL | Immediate action | N/A | None | RGB | See grouped matrix | RGB only | Three outputs | HSL; still/sequence | Three views/sequences |
| Extract HSV Hue | Immediate action | N/A | None | RGB | See grouped matrix | RGB only | Conversion + output | H; still/sequence | New view/sequence |
| Extract HSV Saturation | Immediate action | N/A | None | RGB | See grouped matrix | RGB only | Conversion + output | S; still/sequence | New view/sequence |
| Extract HSV Value | Immediate action | N/A | None | RGB | See grouped matrix | RGB only | Conversion + output | V; still/sequence | New view/sequence |
| Extract HSV | Immediate action | N/A | None | RGB | See grouped matrix | RGB only | Three outputs | HSV; still/sequence | Three views/sequences |
| Extract HSI Hue | Immediate action | N/A | None | RGB | See grouped matrix | RGB only | Conversion + output | H; still/sequence | New view/sequence |
| Extract HSI Saturation | Immediate action | N/A | None | RGB | See grouped matrix | RGB only | Conversion + output | S; still/sequence | New view/sequence |
| Extract HSI Intensity | Immediate action | N/A | None | RGB | See grouped matrix | RGB only | Conversion + output | I; still/sequence | New view/sequence |
| Extract HSI | Immediate action | N/A | None | RGB | See grouped matrix | RGB only | Three outputs | HSI; still/sequence | Three views/sequences |
| Extract YCbCr Luma | Immediate action | N/A | None | RGB | See grouped matrix | RGB only | Conversion + output | Y; still/sequence | New view/sequence |
| Extract YCbCr Blue Chroma | Immediate action | N/A | None | RGB | See grouped matrix | RGB only | Conversion + output | Cb; still/sequence | New view/sequence |
| Extract YCbCr Red Chroma | Immediate action | N/A | None | RGB | See grouped matrix | RGB only | Conversion + output | Cr; still/sequence | New view/sequence |
| Extract YCbCr | Immediate action | N/A | None | RGB | See grouped matrix | RGB only | Three outputs | YCbCr; still/sequence | Three views/sequences |
| Convert to RGB | Immediate action | N/A | None | Monochrome | See grouped matrix | Mono only | Triples samples | Full still/sequence | Replaces view/history |
| Convert to Grayscale | Immediate action | N/A | None | RGB | See grouped matrix | RGB only | Reduces samples | Full still/sequence | Replaces view/history |
| Negative Transformation | Immediate action | N/A | None | Active image | See grouped matrix | Active image | Full transform | Still/sequence; mask-aware | Replaces view/history |
| Automatic MTF Transform | Unavailable action | N/A | None | No route | No effect | Not connected | None | Unavailable | None |
| Convert to 8 bit | Immediate action | N/A | None | Different type | See grouped matrix | Active image | Quantization | Full still/sequence | New view/sequence |
| Convert to signed 16 bit | Immediate action | N/A | None | Different type | See grouped matrix | Not SER/video | Range conversion | Full still/sequence | New view/sequence |
| Convert to unsigned 16 bit | Immediate action | N/A | None | Different type | See grouped matrix | Active image | Range conversion | Full still/sequence | New view/sequence |
| Convert to signed 32 bit | Immediate action | N/A | None | Different type | See grouped matrix | Not SER/video | Range conversion | Full still/sequence | New view/sequence |
| Convert to unsigned 32 bit | Immediate action | N/A | None | Different type | See grouped matrix | Not SER/video | Range conversion | Full still/sequence | New view/sequence |
| Convert to 32 bit float | Immediate action | N/A | None | Different type | See grouped matrix | Not SER/video | More storage | Full still/sequence | New view/sequence |
| Convert to 64 bit float | Immediate action | N/A | None | Different type | See grouped matrix | Not SER/video | Highest storage | Full still/sequence | New view/sequence |
| Horizontal Mirror | Immediate action | N/A | None | Active image | See grouped matrix | Active image | Strips WCS/SIP and cards | Full still/sequence | Replaces view/history |
| Vertical Mirror | Immediate action | N/A | None | Active image | See grouped matrix | Active image | Strips WCS/SIP and cards | Full still/sequence | Replaces view/history |
| Rotate 180º | Immediate action | N/A | None | Active image | See grouped matrix | Active image | Strips WCS/SIP and cards | Full still/sequence | Replaces view/history |
| Rotate 90º Left | Immediate action | N/A | None | Active image | See grouped matrix | Active image | Swaps dimensions; WCS/SIP and cards lost | Full still/sequence | Replaces view/history |
| Rotate 90º Right | Immediate action | N/A | None | Active image | See grouped matrix | Active image | Swaps dimensions; WCS/SIP and cards lost | Full still/sequence | Replaces view/history |
| Convert to SER | Sequence action | N/A | None | Non-SER sequence | See grouped matrix | Sequence only | Codec limits/full disk output | Full sequence | Temporary replacement |
| Convert to Image List | Sequence action | N/A | None | Non-list sequence | See grouped matrix | Sequence only | Full disk output | Full sequence | Temporary replacement |
| Convert to FITS | Unavailable action | N/A | None | Disabled | No effect | Sequence state disables | None | Unavailable | None |
| Extract Frame | Sequence action | N/A | None | Sequence | See grouped matrix | Sequence only | One still allocation | Current frame | New still/history |
| Extract Sequence | Dialog action | UI state | Dialog lifecycle | Sequence | Opens dialog | Sequence only | Selected disk output | Full sequence dialog | User-selected output |
| Export... | Dialog action | UI state | Export lifecycle | Sequence | Opens dialog | Sequence only | Codec-dependent | Full sequence dialog | User-selected files |
| Select Mask | Dialog action | View state | View only | Compatible mask | Opens selector | Active image | No pixel rewrite | Mask association | Session state |
| Remove Mask | Immediate action | View state | View only | Attached mask | Detaches mask | Masked image | No pixel rewrite | Mask association | Session state |
| Invert Mask | Checked action | View state | View only | Boolean | Toggles interpretation | Masked image | No pixel rewrite | Mask state | Session state |
| Enable Mask | Checked action | View state | View only | Boolean | Toggles use | Masked image | No pixel rewrite | Mask state | Session state |
| Show Mask | Checked action | View state | View only | Boolean | Toggles visibility | Masked image | Display only | Mask display | Session state |
| Overlay Red | Exclusive action | View mode | View only | Eight modes | Selects mode | Masked image | Display only | Mask display | Session state |
| Overlay Green | Exclusive action | View mode | View only | Eight modes | Selects mode | Masked image | Display only | Mask display | Session state |
| Overlay Blue | Exclusive action | View mode | View only | Eight modes | Selects mode | Masked image | Display only | Mask display | Session state |
| Overlay Yellow | Exclusive action | View mode | View only | Eight modes | Selects mode | Masked image | Display only | Mask display | Session state |
| Overlay Magenta | Exclusive action | View mode | View only | Eight modes | Selects mode | Masked image | Display only | Mask display | Session state |
| Overlay Cyan | Exclusive action | View mode | View only | Eight modes | Selects mode | Masked image | Display only | Mask display | Session state |
| Replace | Exclusive action | View mode | View only | Eight modes | Selects mode | Masked image | Display only | Mask display | Session state |
| Multiply | Exclusive action | View mode | View only | Eight modes | Selects mode | Masked image | Display only | Mask display | Session state |

### View, channel, and color-space actions

| UI label/action | Menu path and wiring | Enabled when | Output and source effect | Precision and metadata consequence |
| --- | --- | --- | --- | --- |
| Duplicate | **Image > Actions**; direct | Active image | Still: new deep-copy view. Sequence: new view reopened from the same backing sequence file. Source unchanged. | Still keeps image metadata and astrometry/star profile but clears disk-file identity and filename. Sequence preserves the auto-debayer and pending-change flags; reopening the backing file is not an independent disk copy. |
| Extract Red Channel | **Image > Extract Channel**; registered | RGB | One new monochrome R view/sequence; source unchanged. | Keeps source pixel type and dimensions; copied sample values are independent. Metadata is copied by channel extraction. |
| Extract Green Channel | Same; registered | RGB | One new monochrome G view/sequence; source unchanged. | Same as Red. |
| Extract Blue Channel | Same; registered | RGB | One new monochrome B view/sequence; source unchanged. | Same as Red. |
| Extract RGB Channels | Same; registered | RGB | Three new monochrome R, G, and B views/sequences; source unchanged. | Same source pixel type and dimensions for all three; metadata copied. |
| Extract CIE L* | Same; registered | RGB | One L* component view/sequence. | RGB is read as fractions of full scale - linear when the image says so, sRGB-encoded otherwise - and converted to CIE L*a*b*; L* is a 32-bit floating image (64-bit for a 64-bit source) in the unit interval, whatever the source's pixel type. The component keeps the acquisition record, the WCS/SIP solution and, of the header cards, only the frame's observing context and integration cards, for a still and a sequence frame alike. |
| Extract CIE a* | Same; registered | RGB | One a* component view/sequence. | Same Lab conversion, floating encoding, and metadata behavior as CIE L*. |
| Extract CIE b* | Same; registered | RGB | One b* component view/sequence. | Same Lab conversion, floating encoding, and metadata behavior as CIE L*. |
| Extract CIE c* | Same; registered | RGB | One LCh chroma component view/sequence. | RGB→Lab→LCh with the same floating encoding and metadata behavior as CIE L*. |
| Extract CIE h* | Same; registered | RGB | One LCh hue component view/sequence. | Hue is a fraction of a turn in the unit interval, not an angle in degrees; same floating encoding and metadata behavior as CIE L*. |
| Extract CIE a*=R / b*=G | Same; registered | RGB | One three-channel view/sequence with a* in R, b* in G, and B cleared. | A floating colour image of a* and b* as CIE L* encodes them, blue cleared; this is a visualization/transport encoding, not an RGB color image. Metadata as CIE L*. |
| Extract CIE L*a*b* | Same; registered | RGB | Three monochrome L*, a*, and b* outputs. | Three floating components, each as CIE L* encodes and describes it. |
| Extract CIE L*c*h* | Same; registered | RGB | Three monochrome L*, c*, and h* outputs. | Three floating components, each as CIE L* encodes and describes it. |
| Extract CIE X | Same; registered | RGB | One CIE X component output. | Engine RGB→XYZ conversion, each tristimulus value divided by the white's so the white is one; floating encoding and metadata as CIE L*. |
| Extract CIE Y | Same; registered | RGB | One CIE Y component output. | Same conversion, precision, and metadata behavior as CIE X. |
| Extract CIE Z | Same; registered | RGB | One CIE Z component output. | Same conversion, precision, and metadata behavior as CIE X. |
| Extract CIE XYZ | Same; registered | RGB | Three monochrome X, Y, and Z outputs. | Same floating encoding and component metadata behavior for all outputs. |
| Extract HSL Hue | Same; registered | RGB | One HSL hue output. | Component is a 32-bit floating plane in the unit interval, 64-bit for a 64-bit source; metadata as CIE L*. |
| Extract HSL Saturation | Same; registered | RGB | One HSL saturation output. | Same floating encoding and metadata behavior as HSL Hue. |
| Extract HSL Lightness | Same; registered | RGB | One HSL lightness output. | Same floating encoding and metadata behavior as HSL Hue. |
| Extract HSL | Same; registered | RGB | Three monochrome H, S, and L outputs. | Same floating encoding and component metadata behavior for all outputs. |
| Extract HSV Hue | Same; registered | RGB | One HSV hue output. | Component is a 32-bit floating plane in the unit interval, 64-bit for a 64-bit source; metadata as CIE L*. |
| Extract HSV Saturation | Same; registered | RGB | One HSV saturation output. | Same floating encoding and metadata behavior as HSV Hue. |
| Extract HSV Value | Same; registered | RGB | One HSV value output. | Same floating encoding and metadata behavior as HSV Hue. |
| Extract HSV | Same; registered | RGB | Three monochrome H, S, and V outputs. | Same floating encoding and component metadata behavior for all outputs. |
| Extract HSI Hue | Same; registered | RGB | One HSI hue output. | Component is a 32-bit floating plane in the unit interval, 64-bit for a 64-bit source; metadata as CIE L*. |
| Extract HSI Saturation | Same; registered | RGB | One HSI saturation output. | Same floating encoding and metadata behavior as HSI Hue. |
| Extract HSI Intensity | Same; registered | RGB | One HSI intensity output. | Same floating encoding and metadata behavior as HSI Hue. |
| Extract HSI | Same; registered | RGB | Three monochrome H, S, and I outputs. | Same floating encoding and component metadata behavior for all outputs. |
| Extract YCbCr Luma | Same; registered | RGB | One Y (luma) output. | Component is a 32-bit floating plane in the unit interval, 64-bit for a 64-bit source; metadata as CIE L*. |
| Extract YCbCr Blue Chroma | Same; registered | RGB | One Cb output. | Same floating encoding and metadata behavior as YCbCr Luma. |
| Extract YCbCr Red Chroma | Same; registered | RGB | One Cr output. | Same floating encoding and metadata behavior as YCbCr Luma. |
| Extract YCbCr | Same; registered | RGB | Three monochrome Y, Cb, and Cr outputs. | Same floating encoding and component metadata behavior for all outputs. |
| Convert to RGB | **Process > Adjustments**; direct | Monochrome | Replaces the active still/sequence with three identical channels. | Pixel type is unchanged. The result keeps the image's acquisition record, WCS/SIP solution, header cards and processing state; an explicit validity mask is shared by the three channels. CFA sequences may be auto-debayered first when that view option is enabled. |
| Convert to Grayscale | Same; direct | RGB | Replaces the active still/sequence with one channel, using `0.2989R + 0.5870G + 0.1140B`. | Pixel type is unchanged; the weighted sum is rounded to the nearest code. The result keeps the image's acquisition record, WCS/SIP solution, header cards and processing state. |
| Negative Transformation | Same; direct | Active image | Replaces each selected sample with its complement: one minus a floating sample, the pixel-type maximum minus an unsigned one, and the sum of the minimum and maximum minus a signed one. Still/sequence data changes. | Pixel type, dimensions, and metadata are retained; a CFA sequence may become RGB first when auto-debayer is enabled. Active-mask blending is supported. The history state is marked nonlinear. |
| Automatic MTF Transform | Declared action only; no menu insertion, registry descriptor, or connection | Never reachable in the current UI | No runnable action. | No current precision or metadata consequence. Do not confuse the display auto-stretch with a committed Automatic MTF process. |

For sequence channel extraction, the sequence processor requests CFA debayering
before RGB/component extraction. Still-image extraction uses the already displayed
RGB data. Color-space component images are normalized encodings for processing and
inspection; they do not carry a unit such as degrees in their stored sample values.
A component is a 32-bit floating image whatever the source's pixel type, so a
16-bit image's lightness is not rounded to 16-bit codes. A sequence conversion,
unlike an extraction, keeps a mosaic frame as a mosaic.

### Pixel-type conversion actions

| UI label/action | Menu path and wiring | Enabled when | Output and source effect | Precision and metadata consequence |
| --- | --- | --- | --- | --- |
| Convert to 8 bit | **Image > Convert Format**; registered | Active type is not 8-bit | New view/sequence; source unchanged. | Normalizes the source type to unsigned 8-bit. Metadata is copied. Quantization is irreversible. |
| Convert to signed 16 bit | Same; registered | Active type differs; disabled for SER/video sequences | New signed-16 view/sequence; source unchanged. | Normalized range conversion; metadata copied. |
| Convert to unsigned 16 bit | Same; registered | Active type differs | New unsigned-16 view/sequence; source unchanged. | Normalized range conversion; metadata copied. SER-compatible high-precision integer choice. |
| Convert to signed 32 bit | Same; registered | Active type differs; disabled for SER/video sequences | New signed-32 view/sequence; source unchanged. | Normalized range conversion; metadata copied. |
| Convert to unsigned 32 bit | Same; registered | Active type differs; disabled for SER/video sequences | New unsigned-32 view/sequence; source unchanged. | Normalized range conversion; metadata copied. |
| Convert to 32 bit float | Same; registered | Active type differs; disabled for SER/video sequences | New float32 view/sequence; source unchanged. | Normalized sample values are represented in `[0,1]`; metadata copied. Avoids integer quantization but cannot restore detail already lost. |
| Convert to 64 bit float | Same; registered | Active type differs; disabled for SER/video sequences | New float64 view/sequence; source unchanged. | Normalized sample values are represented in `[0,1]`; metadata copied. Doubles storage relative to float32 without creating new source precision. |

The generic conversion path maps a source sample through its normalized range and
then into the destination range. For signed integer types, the full signed range
maps to `[0,1]`; floating-point storage already uses normalized samples. The
mapping is computed in double precision and rounded to the nearest code.
Converting to fewer integer levels
quantizes values, while converting to a wider type only provides headroom for later
processing. Converting to the image's own type makes an exact copy. A sequence
conversion to a type its container cannot store - a SER beyond 16-bit unsigned, a
video beyond 8-bit - is refused by name; use Convert to Image List first.

### Fast Geometry actions

| UI label/action | Menu path and wiring | Enabled when | Output and source effect | Precision and metadata consequence |
| --- | --- | --- | --- | --- |
| Horizontal Mirror | **Image > Fast Geometry**; direct | Active image | Reverses each row. A still image is the view's own edit; a sequence is replaced by a new temporary sequence with every frame mirrored. History: **Image Horizontal Mirror** for a still, **Sequence Horizontal Mirror** for a sequence. | Every sample is copied exactly, in every pixel type; channels kept. The acquisition record, the image's meaning and, of the header cards, the frame's observing context and integration cards are kept; the WCS/SIP solution is invalidated, the other header cards are dropped and any output decision is withdrawn. |
| Vertical Mirror | Same; direct | Active image | Reverses row order. History: **Image Vertical Mirror** / **Sequence Vertical Mirror**. | Same as Horizontal Mirror. |
| Rotate 180º | Same; direct | Active image | Reverses both axes; width/height unchanged. History: **Image Rotation** / **Sequence Rotation**, described "180 degrees". | Same as Horizontal Mirror. |
| Rotate 90º Left | Same; direct | Active image | Turns a quarter turn counter-clockwise; width and height swap. History: **Image Rotation** / **Sequence Rotation**, described "90 degrees left". | Same as Horizontal Mirror; a non-square pixel aspect is inverted with the axes. |
| Rotate 90º Right | Same; direct | Active image | Turns a quarter turn clockwise; width and height swap. History: **Image Rotation** / **Sequence Rotation**, described "90 degrees right". | Same as Rotate 90º Left. |

A still image's mirror or turn is an edit of its view: it is computed on a
worker, recorded as one Image History step before the view shows it, and
undone by that step. Edits of one view run one at a time in the order they
were asked for, so pressing a turn twice turns twice. When the image had a
solution, the Session Console notes that it was removed and that a plate solve
restores it.

Over a sequence, the action takes the view, turns or mirrors every frame of the
sequence as recorded, writes the frames into a new sequence of the same
container in the session's temporary directory, and shows it in the view as one
history step once the whole sequence is written. A progress window titled
**Flip sequence** (mirrors) or **Sequence Rotation** (turns) shows the frames
done, and its Cancel stops the run, leaving the view as it was and nothing in
the temporary directory. A mosaic frame is turned without being demosaiced: its
filter pattern is restated so each sample stays under the filter it was
measured through, and the view still demosaics the frames for display when it
did before. If another run already holds the sequence, a warning titled
**Rotate Sequence** says "Another process is executing for this sequence,
please cancel that job first before running this process" and nothing changes.
A sequence run that cannot finish says why in a message titled as its progress
window, "Could not turn the sequence '*name*': ...", and in the Session
Console.

Each mirror and turn moves an explicit validity mask with its samples; any
other validity rule is kept as stated. Nothing is interpolated: every sample is
copied exactly. A turn drops the solution and every header card but the
frame's observing context and integration cards, which every geometry change
keeps.

### Sequence actions

| UI label/action | Menu path and wiring | Enabled when | Output and source effect | Precision and metadata consequence |
| --- | --- | --- | --- | --- |
| Convert to SER | **Image > Sequence > Convert**; registered | Sequence that is not already SER | Replaces the active view with a new temporary SER sequence; original backing input is not overwritten. | Frames are constrained to SER-supported mono/RGB 8- or 16-bit storage; higher/signed/float inputs are converted to unsigned 16-bit. Per-frame metadata is not a general SER payload. FPS is copied before destination finalization. |
| Convert to Image List | Same; registered | Sequence that is not already an image list | Replaces the active view with a temporary image-list sequence; original input is not overwritten. | Writes a list of frame files through the image-list/FITS path, preserving the processed image type and frame metadata supported by the codec. FPS is copied before finalization. |
| Convert to FITS | Same; registered | Disabled by current action-state code for every sequence | No reachable output. If reached, the coordinator converts nothing and reports “Converting to a FITS sequence is not available. Convert to Image List writes every frame of the sequence as a FITS file.” for every sequence. | No current precision or metadata consequence; use **Convert to Image List** when FITS frame files are required. |
| Extract Frame | **Image > Sequence**; registered non-interactive | Sequence | Copies the currently displayed frame into one new still named with `FRAME_<current frame>`; sequence unchanged. | Pixel type, pixels, and copied image metadata are retained in the new view. |
| Extract Sequence | **Image > Sequence**; registered single-image dialog | Sequence | Opens the contiguous-range extraction dialog and writes a new sequence in the source's own container: SER, an Image List (`.anseq`), AVI, MP4, or Matroska (`.mkv`) for any other video. | See [Extract Sequence](extract-sequence.md) for inclusive range semantics, metadata, FPS, cancellation, and cleanup. |
| Export... | **Image > Sequence**; registered image operation | Sequence | Opens the Export Sequence dialog; it is not immediate after the dialog opens. | Output codec determines precision and metadata. See [Saving and Exporting](../../interface/saving-and-exporting.md). |

Convert to SER/Image List processes every frame without applying the view's CFA
auto-debayer option. Both jobs reject converting to the sequence's existing type,
lock the source view, clean incomplete temporary output on failure/cancellation,
and publish the replacement only after successful finalization.

### Mask actions

| UI label/action | Menu path and wiring | Enabled when | Output and source effect | Precision and metadata consequence |
| --- | --- | --- | --- | --- |
| Select Mask | **Image > Mask**; registered single-image | Active image | Opens the mask selector and associates a compatible image chosen there. | Does not alter target samples or metadata. Compatibility and mask behavior are covered in [Masks](../../interface/masks.md). |
| Remove Mask | Same; registered single-image | Active image with a mask | Removes the association. | Target and mask samples/metadata are unchanged. |
| Invert Mask | Same; direct checked action | Active image with a mask | Toggles interpretation of mask weights. | Display/process state only; no sample or metadata rewrite. |
| Enable Mask | Same; direct checked action | Active image with a mask | Enables/disables the mask for mask-aware processing. | State only; disabling does not detach the mask. |
| Show Mask | Same; direct checked action | Active image with a mask | Shows/hides mask rendering over the target. | Display state only. |
| Overlay Red | **Image > Mask > Rendering Mode**; direct checked action | Active image with a mask | Selects red overlay. | Display state only; mutually exclusive with the other seven rendering modes. |
| Overlay Green | Same; direct checked action | Active image with a mask | Selects green overlay. | Display state only; mutually exclusive. |
| Overlay Blue | Same; direct checked action | Active image with a mask | Selects blue overlay. | Display state only; mutually exclusive. |
| Overlay Yellow | Same; direct checked action | Active image with a mask | Selects yellow overlay. | Display state only; mutually exclusive. |
| Overlay Magenta | Same; direct checked action | Active image with a mask | Selects magenta overlay. | Display state only; mutually exclusive. |
| Overlay Cyan | Same; direct checked action | Active image with a mask | Selects cyan overlay. | Display state only; mutually exclusive. |
| Replace | Same; direct checked action | Active image with a mask | Displays the mask in replace mode. | Display state only; mutually exclusive. |
| Multiply | Same; direct checked action | Active image with a mask | Displays target multiplied by the mask. | Display state only; mutually exclusive. |

## How the algorithm works

### Conceptual pipeline

1. Menu/action-state code checks the active view, image/color/sequence type, mask
   association, and registry launch guard.
2. The action either changes lightweight mask state immediately or starts a
   detached worker. Sequence workers acquire an exclusive view lock and create a
   temporary destination when output frames are required.
3. Engine code copies/extracts channels, converts color or sample ranges,
   reindexes geometry, or applies the negative transform. Most jobs run their
   per-pixel/per-frame loops in parallel inside the engine.
4. A successful job creates a new view or publishes replacement data, adds Image
   History, refreshes the display, releases the lock, and closes progress.

### Model and equations

Type conversion uses normalized intensity `n`. Unsigned integer samples use
`n = x / max(T)`; signed integer samples map the complete signed range to
`[0,1]`; float and double samples already represent `n`. The destination is the
corresponding denormalization into its range, computed in double precision. Integer
destinations round to the nearest of a finite set of codes.

Grayscale conversion is:

`Y = 0.2989 R + 0.5870 G + 0.1140 B`

with the result rounded to the nearest code of the unchanged pixel type. Convert to
RGB sets `R = G = B = Y`. Negative Transformation is the complement in normalized
units: `y = 1 - x` for a floating sample, `y = max(T) - x` for an unsigned type and
`y = min(T) + max(T) - x` for a signed one (`-1 - x` for 16 bits), for every
processed sample of pixel type `T`, with the normal active-mask blend applied afterward.
Mirrors and right-angle rotations only move samples, each destination sample one
source sample copied bit for bit; they do not interpolate.

### Boundary and failure behavior

- Registry guards reject a missing active image, incompatible launch state, or a
  view already occupied by an exclusive process. Sequence jobs separately warn
  when their view lock cannot be acquired.
- Sequence progress can cancel engine processing. Every job that cannot finish -
  an engine refusal, an image of the wrong kind, a view that cannot be opened, a
  sequence that cannot be read or written - says why in a message titled as the
  action and in the Session Console, and leaves no temporary output; only a
  cancellation is quiet.
- Type-conversion functions assume normal engine sample ranges. Reducing bit
  depth quantizes; it does not dither. Right-angle geometry has no boundary fill
  because every source sample maps to exactly one destination sample.
- Select Mask shows “No selected image to process” if invoked without a target;
  Remove Mask is a no-op if no mask is attached.

## Parameter interactions and starting points

| Combination | Conservative starting point | Too weak looks like | Too strong looks like | Adjustment order |
| --- | --- | --- | --- | --- |
| Channel extraction + later recombination | Keep the source type; use RGB channels for literal component separation. | Wrong component selected or expected output count missing. | Early 8-bit conversion shows banding/quantization in later edits. | Extract first, inspect names/counts, then convert precision only for a required destination. |
| Color-space component + integer storage | A component is already float32; convert it to an integer type only for a destination that needs one. | Converting a component to 8 bits collapses subtle edits to the same code. | Float64 increases memory/disk use without recovering source detail. | Choose component, process in float, then convert only at final output. |
| Geometry + astrometry | Duplicate or save first; plan to plate-solve again. | Coordinates are gone after the turn, and header cards outside the frame's observing context and integration cards are no longer written when the result is saved. | Not applicable; transforms are exact. | Geometry, verify orientation, then obtain a new WCS/SIP solution. |
| Negative + mask | Verify Enable Mask, Show Mask, and rendering mode before running. | Intended protected/affected region is reversed or unchanged. | Full-strength inversion in the wrong region. | Select mask, inspect/invert state, enable, then run Negative once. |
| Sequence container conversion | Use Image List for broadly preserved frame type/metadata; use SER for compatible compact video-style data. | Destination is the same type and the action is disabled/rejected. | SER forces unsupported high/signed/float precision to 16-bit unsigned. | Inspect type/channels/FPS, choose destination, wait for finalization, verify first/last frame. |

## Inputs, outputs, and metadata

New still outputs receive generated names such as channel/component suffixes,
`CONV_*`, or `FRAME_*`; the UI disambiguates duplicate view names. A new view is
unsaved until explicitly saved. Still Duplicate clears the source file identity
so saving cannot silently overwrite the original.

Raw R/G/B channel extraction and all type-conversion outputs preserve dimensions
and copy image metadata, header cards included. A color-space component is a
measurement of the image: it keeps the acquisition record, the WCS/SIP solution
and, of the header cards, only the frame's observing context and integration
cards, for a still and a sequence frame alike. Grayscale/RGB conversion keeps
the image's metadata. Geometry keeps the acquisition record, the image's
meaning and the frame's observing context and integration cards, invalidates
the WCS/SIP solution and drops the other header cards.
Negative retains metadata because it replaces/redefines pixels in the active image. Mask state
actions never rewrite samples or metadata.

Sequence processing uses temporary disk storage. Channel and type conversions
open new sequence views and leave the source view unchanged. Geometry,
grayscale/RGB, negative, and container conversion update the active view to a new
processed sequence while leaving the original backing input file untouched.

## Performance

Cost is linear in pixel count for a still and approximately linear in
`frame count × pixels per frame` for a sequence. Multi-output component actions
write three full outputs. RGB conversion triples monochrome pixel storage;
grayscale reduces three channels to one. Float64 uses twice the sample memory and
disk of float32, and four times unsigned 16-bit. Temporary sequence operations
need enough free space for the complete destination before the view is published.

Prototype a long sequence with [Extract Sequence](extract-sequence.md) if runtime
or storage is uncertain. Do not close the application or remove source media while
a detached sequence job is active.

## Worked example

- Evidence mode: Source-and-test-derived.
- Input condition/provenance and settings: An RGB sequence with known first and
  last frames; choose **Image > Extract Channel > Extract RGB Channels**. No
  project-owned dataset was executed.
- Expected checkpoint: Three new sequence views are created for R, G, and B; each
  is monochrome, retains the source dimensions/pixel type, and the source sequence
  remains unchanged. The sequence processor requests CFA debayering before
  extraction when applicable.
- Inspect at 100%/statistics/history: Compare corresponding first/last frames,
  verify the output pixel type and Image History lineage, and confirm the original
  RGB sequence is still open.
- Limitations: The checkpoint is derived from current source plus focused engine
  and process-framework tests, not an executed visual dataset; no visual-quality
  claim is made.

## Warnings and limitations

- **Convert to FITS** is currently disabled and has no working conversion path.
- **Automatic MTF Transform** is not reachable; display auto-stretch is separate
  and does not commit pixel data.
- Fast Geometry discards WCS/SIP and every header card but the frame's
  observing context and integration cards; grayscale/RGB conversion keeps the
  image's metadata. Re-solve geometry-dependent data after a mirror or
  rotation.
- When Image History cannot keep the image a still mirror or turn replaces,
  the history says so once, in a message titled **Image History**, and the image
  and its history are left as they were. A failure of the view's own - the
  mirror or turn itself could not be computed, or the view could not show a
  result the history had recorded - leaves the image as it was and is only
  written to the Session Console; no message is shown. This is a known open
  defect.
- Negative Transformation is nonlinear and destructive to the active view data.
  Its result depends on mask inversion/enabled state at execution time.
- Color-space components are 32-bit floating encodings in the unit interval. Hue and
  chroma samples are not stored as human-readable degrees or physical units.
- Sequence Duplicate reopens the same backing file; it is another view, not a
  second on-disk copy. Save/export when independent durable data is required.
- A failed job says why. If no new output appears and no message was shown, the job
  was cancelled; inspect the Session Console for the stop.

## Troubleshooting

| Symptom or exact error | Verified cause/validation path | Recovery | Evidence source |
| --- | --- | --- | --- |
| Extract Channel is disabled | Active image is not RGB. | Select an RGB view, or use Convert to RGB for a monochrome image before extracting. | `ActionStateCoordinator::setEnableImageColorActions` |
| A precision-conversion item is disabled | The image already has that type, or the selected SER/video sequence cannot store the requested type. | Inspect the active type; choose a compatible destination or Convert to Image List. | `ActionStateCoordinator::updateActiveImageActions` |
| Convert to FITS is disabled | Current action-state code always disables it for sequences. | Use Convert to Image List, which writes the supported image-list/FITS frame path. | `ActionStateCoordinator::updateActiveImageActions`; `ImageOperationsCoordinator::convertToFITSSeq` |
| Coordinates no longer solve after mirror/rotate | The operation intentionally invalidates WCS/SIP and drops every header card but the frame's observing context and integration cards. | Plate-solve the transformed result again. | `ImageViewControl::orientImage`; `orientViewImage`; `ImageTurnsTest` |
| A still mirror or turn did nothing, with an **Image History** message | Image History could not keep the image the edit replaces; it says so once and nothing changes. | Free memory or close other views, and retry. | `ImageHistoryTracker.cpp`; `GeometryProcessCase.AnEditTheHistoryCannotKeepIsSaidOnceAndChangesNothing` |
| A still mirror or turn did nothing and no message appeared | A failure of the view's own: the turn could not be computed, or the view could not show a result the history had recorded; the reason is only in the Session Console. | Read the Session Console, free memory or close other views, and retry. | `ImageViewControl::startNextEdit` |
| "Another process is executing for this sequence, please cancel that job first before running this process" after a mirror or turn | Another run holds the sequence view. | Wait for or cancel that run, then retry. | `NonInteractiveProcesses.cpp` (`replaceSequence`); `ImageTurnsTest` |
| “Could NOT extract frame ... because it is not a sequence” | Extract Frame was invoked for a still. | Activate a sequence and navigate to the wanted frame first. | `NonInteractiveProcesses::extractFrame` |
| “No selected image to process” | A mask action resolved no active view. | Activate the intended target and retry. | `ProcessLauncher::executeSelectMask`; `MaskOverlayCoordinator::activeControlOrNotify` |
| Rendering mode will not uncheck | The selected mask rendering action is forced checked so exactly one mode remains active. | Select another rendering mode rather than trying to clear the current one. | `MaskOverlayCoordinator::applyOverlayMode` |
| Sequence job ends with no output | The view lock failed (a warning says so), the job was cancelled, or a failure ended it - which a message titled as the action names. | Wait for other processing, read the message, verify source access and free space, retry on a short extracted sequence, and inspect Session Console. | `NonInteractiveProcesses.cpp`; `ChannelSequenceTest`; `ImageOperationTest` |

## Related workflow steps

- [Extract Sequence](extract-sequence.md) documents contiguous-range extraction into a
  new sequence.
- [Masks](../../interface/masks.md) covers mask compatibility, weights, inversion,
  and how processing consumes the selected mask.
- [Saving and Exporting](../../interface/saving-and-exporting.md) covers durable
  files, codec limits, and the Export Sequence dialog.
- [Image History](../../interface/image-history.md) covers lineage, undo, and the
  difference between a new view and a modified active view.
- [File Formats](../../reference/file-formats.md) is the codec capability matrix
  for choosing SER, FITS/image-list, or another saved format.

## References and provenance

- AfterNight action inventory and exact labels: `AfterNightAppUiBuilder.h`,
  `MenuActionManager.cpp`, `ProcessRegistryBuiltins.cpp`.
- Active-view, type, sequence, and mask availability: `ActionStateCoordinator.cpp`.
- Immediate-operation behavior and temporary-output cleanup:
  `NonInteractiveProcesses.cpp`, `ProcessCommonUtils.h`, `ChannelExtractor.cpp`,
  `ImageOperationsCoordinator.cpp`, and `MaskOverlayCoordinator.cpp`.
- Pixel conversion, grayscale/RGB equations, negative transform, and metadata
  defaults: `ChannelActions.cpp` over the engine's `ImageConversion.h`,
  `SampleConversion.h`, `ChannelScope.h`, `ColorTransform.h`, `Geometry.h` and
  `PointwiseProgramBuilder.h`.
- Sequence-container coercion, FPS finalization, and image-list publication:
  `SequenceContainers.cpp` over the engine's `SerSequence.h`,
  `ImageListManifest.h` and `SequenceOutput.h`.
- Verification provenance: the focused GoogleTest sources.
