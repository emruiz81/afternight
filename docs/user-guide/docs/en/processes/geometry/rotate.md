# Rotate

## Purpose and expected result

Rotate turns an image, or every frame of a sequence, clockwise or
counter-clockwise about its centre by up to 180 degrees, optionally grows the
canvas to hold every turned pixel, and can crop the turned result. It is intended
for framing and orientation changes that cannot use the exact non-interpolating
90/180-degree Image-menu actions.

The result keeps the source sample representation, channel layout, acquisition
record and meaning, and, among the header cards read from the file, the frame's
observing context - its optics, site, pointing, camera settings, filter,
object, frame type and date - and the light it holds, its `EXPTIME`, `TOTEXP`,
`LIVETIME`, `STACKCNT` and `NCOMBINE` cards as it states them, but arbitrary
rotation resamples pixels, adds black border areas, drops the plate solution
and drops the other header cards. It does not preserve scientific flux or
increase resolution.

## At a glance

| Item | Current behavior |
| --- | --- |
| Menu path | **Process > Geometry > Rotate** |
| Enabled when | An image or sequence is active, Rotate is not already attached, and no other process is attached because Rotate requires exclusive access |
| Inputs | One still or sequence of any of the eight sample representations whose pixels are square; a colour-filter-array mosaic only after **Rotate CFA** is answered **Yes** |
| Outputs | Replaces the target with the turned image on its own or a grown canvas, optionally cropped, as one Image History step; a sequence is written whole into a new sequence of the same container (another video container is written as Matroska) |
| Recommended image state | Any state; prefer demosaiced data and perform scientific rotation only when geometry requires it |
| Preview support | Yes, but display-only: the viewer rotates its current display, stretch and mask overlay; the engine's interpolation and border are not computed until OK |
| Sequence support | Yes: every frame, whatever frames the player has selected, with one angle, canvas, interpolation and crop; progress and Cancel are shown |
| Mask support | No for output; an active mask may appear in the display-only rotated preview but is not applied |
| External dependencies | None; the rotation is the image engine's own |

## Before you start

- Duplicate important data. Apply replaces the target and resampling is not
  exactly reversible; the step can be undone in Image History.
- Use **Image > Fast Geometry > Rotate 90° Left/Right** or **Rotate 180°** when an
  exact orthogonal turn is sufficient. Those actions move every sample without
  interpolating and keep a mosaic's filters under their samples; this process
  still interpolates at those angles.
- Debayer a colour-filter-array mosaic before an arbitrary rotation. If you
  answer **Yes** to **Rotate CFA**, the mosaic is turned as one grey plane and the
  result states no filter array.
- Decide whether corners may be clipped. **Resize image** grows the output canvas
  to hold every turned pixel; it does not scale the image.
- Expect black triangles wherever the turned source does not cover the output.
  There is no fill-colour or edge-extension option.
- Rotate drops a plate solution, even at zero degrees with only the Crop group
  in use. Plan to plate-solve afterward; [Crop](crop.md), [Resize](resize.md)
  and [Integer Resample](integer-resample.md) carry the solution, so do them
  around the rotation as framing needs, and solve once at the end.
- Rotate needs square pixels and refuses an image that states another pixel
  aspect. A [Resize](resize.md) states the source's pixel aspect exactly, whatever
  its two axis scales, so a resized image with square pixels can be rotated.

## Procedure

1. Select the target and open **Process > Geometry > Rotate**. Confirm **Target
   Image**.
2. Set **Rotation Angle**. Positive is clockwise; negative is counter-clockwise.
   The main image viewer rotates its current display as a framing preview.
3. Check **Resize image** to keep the complete turned image. Leave it unchecked
   only when clipping against the original dimensions is intended.
4. Choose an interpolation. The first entry reads **Auto (Lanczos4)** and always
   chooses Lanczos4.
5. If needed, check **Crop**, then edit or drag the Pos/size overlay in
   coordinates of the turned canvas. Verify nonzero Width and Height.
6. Select **OK**. The dialog is read at that moment; changing a field afterwards
   does not reach the run. A still is turned on a worker without a progress
   window; a sequence shows progress and **Cancel**. Success closes Rotate and
   records `Rotated <angle> degrees - Interpolation: <name>` - with
   `Cropped area : X: <x> Y: <y> W:<w> H:<h>` before the interpolation when the
   Crop group is checked - in Image History, naming the interpolation that ran,
   never Auto.
7. Confirm geometry, borders, orientation, and clipped content at 100%.
   Plate-solve again before using coordinates.

## Complete parameter and action reference

| UI label/action | Type and unit | Runtime default | Persisted-default exception | Valid values, step, or constraints | Effect of increasing, decreasing, or selecting | Dependencies and enable/visibility rules | Quality, artifact, runtime, memory, or disk trade-off | Scope: preview/apply, channel, mask, still/sequence | Persistence |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Rotation Angle: | Double spin box plus slider, degrees | `0.00` | None | `-180.00..180.00`; spin step `1.00`; two decimals; slider is remapped to 1,000 positions (nominal `0.36°`, despite its `0.1°` tooltip) | Positive rotates clockwise; negative counter-clockwise | Updates display preview, canvas estimate/bounds, and OK | Non-orthogonal values interpolate and create border/clipping; repeated rotations compound loss | Display preview/OK; all channels; still/sequence | Window lifetime; Reset `0.00` |
| Resize image | Checkbox | Unchecked | None | Checked/unchecked | Checked grows the canvas to the smallest whole-pixel size holding every turned pixel; unchecked keeps original width/height | Affects preview widget size, crop maxima, and output canvas | Checked preserves source corners but adds memory/black borders; unchecked clips corners | Display preview/OK; still/sequence | Window lifetime; Reset unchecked |
| Interpolation | Combo box | **Auto (Lanczos4)** | None | Auto, **Nearest Neighbor**, **Bilinear**, **Bicubic**, **Pixel Area Relation**, **Lanczos4**, **Bit Exact Bilinear**, **Bit Exact Nearest Neighbor** | Auto always chooses Lanczos4; Pixel Area Relation and Bit Exact Bilinear sample bilinearly, Bit Exact Nearest Neighbor as nearest | Always visible; read when OK is pressed | Nearest is blocky; Bilinear smooths; Bicubic/Lanczos4 can ring; all non-nearest modes correlate samples | OK; each stored channel; still/sequence | Window lifetime; Reset Auto |
| Crop | Checkable group | Unchecked | None | Checked/unchecked | Checked enables a rectangle on the turned canvas and crops the turned image to the part of it the canvas holds | Children disabled while unchecked; overlay listener follows state | Can remove black borders or science content | Display overlay/OK; still/sequence | Window lifetime; Reset unchecked |
| Pos X | Integer, turned-canvas pixels | `0` | None | `0..canvas width`; step `1`; Width maximum becomes `canvas width-X` | Moves the crop's left edge right | Used only when Crop checked | Discards left output pixels | Crop after rotate; all channels | Window lifetime; Reset `0` |
| Pos Y | Integer, turned-canvas pixels | `0` | None | `0..canvas height`; step `1`; Height maximum becomes `canvas height-Y` | Moves the crop's top edge down | Used only when Crop checked | Discards top output pixels | Crop after rotate; all channels | Window lifetime; Reset `0` |
| Width | Integer, turned-canvas pixels | Source width | None | `0..canvas width-X`; step `1` | Sets cropped output width | Used only when Crop checked; zero is not blocked by the dialog | Output memory/runtime scale with area; zero is refused by the engine | Crop after rotate; all channels | Window lifetime; Reset source width |
| Height | Integer, turned-canvas pixels | Source height | None | `0..canvas height-Y`; step `1` | Sets cropped output height | Used only when Crop checked; zero is not blocked by the dialog | Same as Width | Crop after rotate; all channels | Window lifetime; Reset source height |
| Current Size: | Read-only estimate, MB/GB | Current sample bytes × sequence frame count | None | Two decimals; GB above 1024 MB | Reports approximate current sample volume | Recomputed on target updates | Excludes metadata/compression | Information only | Recomputed |
| New Size: | Read-only estimate, MB/GB | Current size at zero angle/no crop | None | Based on crop area, the preview's grown canvas, or source dimensions | Estimates output sample volume | Recomputed from angle/canvas/crop | The grown canvas is measured from the on-screen preview, so it can differ by a row or column from the result; excludes working planes, temporary sequence storage, metadata, and compression | Information only | Recomputed |
| Target Image | Image/sequence selector | Attached active target | None | Any open target allowed by exclusive launch/attachment gates | Reattaches and resets controls/preview for new target | Rotate disables preview on old target and enables it on new target | Wrong target is changed | Preview/OK; still/sequence | Target lifecycle |
| OK | Dialog action | Disabled at zero angle with Crop off/full | None | Enabled for nonzero angle or any checked Crop geometry change | Asks the **Rotate CFA** question for a mosaic, then reads the dialog and starts the rotation, then the optional crop, on a worker | OK/Close/Reset disable while it runs; the fields stay editable but no longer reach the run | Keeps the source for Image History; a sequence adds a temporary output sequence | Apply; all channels; mask not applied; still/sequence | Success closes and records history |
| Reset | Dialog action | Enabled | None | One action | Restores angle 0, source crop fields, Auto, Resize image off, and Crop off | Disabled during execution | No pixel change | Setup/display preview | Immediate |
| Close | Dialog action | Enabled | None | Button or title-bar close | Removes rotation preview and closes without a new operation | Button disables during execution; a title-bar close while a run is in flight hides the window, which closes once the run has landed; it does not stop the run | Nothing waits on the GUI thread | Dialog only | No settings saved |
| Cancel | Sequence-progress action | Not requested | None | Available only during sequence execution | Stops the run between frames | The progress window belongs to the run | A stopped run leaves the sequence and its history as they were and no temporary sequence behind; nothing is reported | Apply; sequence only | Per run |

## How the algorithm works

### Conceptual pipeline

1. On **OK**, ask the **Rotate CFA** question when the target is a mosaic the
   view does not demosaic, then read the angle, the canvas choice, the
   interpolation (Auto is Lanczos4) and the optional crop rectangle on the GUI
   thread.
2. Plan the rotation against the image's description: clockwise by the angle
   about the image's centre `((w - 1) / 2, (h - 1) / 2)`, the midpoint of the
   pixel centres, on the source's canvas or a grown one. The engine refuses a
   mosaic unless it is read as grey after **Yes**, and an image whose pixels are
   not square.
3. Sample every channel at each destination pixel through the chosen
   interpolation in Float32 working planes accumulated in Float64. A tap outside
   the source reads black (`0`).
4. Write the result back in the source representation through the default
   conversion (nearest code, saturating at the representation's range). A
   floating image in a normalized domain is held to `[0,1]`; one in a sensor-code
   or physical domain is left as computed.
5. If Crop is checked, keep the part of the rectangle the turned canvas holds,
   copying its samples exactly.
6. Restate the metadata under the geometry policy and record
   `an::transformImageGeometry`, detail `Rotate`, in the lineage.

### Model and equations

With centre `(cx, cy) = ((w - 1)/2, (h - 1)/2)`, clockwise angle `θ` and a
destination centre `(cx', cy')` at the middle of the output canvas, each
destination pixel reads the source at

`u = cx + (x - cx') cos θ + (y - cy') sin θ`,
`v = cy - (x - cx') sin θ + (y - cy') cos θ`,

in the image's x-right, y-down coordinates, through the interpolation's kernel,
with `0` for a tap outside the source. Nearest Neighbor and Bit Exact Nearest
Neighbor read the nearest pixel centre; Bilinear, Pixel Area Relation and Bit
Exact Bilinear the four surrounding centres; Bicubic a four-by-four neighbourhood
(`a = -0.75`); Lanczos4 an eight-by-eight radius-four window.

For a source `W×H` and angle `θ`, **Resize image** gives the smallest whole-pixel
canvas holding every turned pixel cell:

`ceil(|W cos θ| + |H sin θ|)` by `ceil(|W sin θ| + |H cos θ|)`,

grown equally on both sides of the centre. With Resize image off, output remains
`W×H`.

Rotation interpolation and the black border are not flux conserving. A grown
canvas avoids clipping the source, but resampling and the range hold can change
stored sums and a later crop can discard signal. Noise becomes spatially
correlated; rotation adds no independent samples, resolution, flux, or SNR.

### Boundary and failure behavior

The border is always black; Rotate exposes no alternate border mode or fill
value. Integer results saturate at their representation's range; a normalized
floating result is held to `[0,1]`; a sensor-code or physical-domain result
keeps its excursions. A sample the image's validity rule calls invalid prevents every
output whose kernel reaches it from being written, and those outputs are marked
invalid in the result's validity mask; under a rule that counts every sample
valid, a `NaN` takes part in the interpolation and can spread.

The optional crop keeps the part of the rectangle the turned canvas holds. The
dialog allows a zero Crop Width or Height, and a rectangle wholly outside the
canvas; the engine refuses both. Answering **No** to **Rotate CFA** starts
nothing. An engine refusal or an allocation it cannot make becomes **Error
running rotate**, with the text `An error occurred when executing the rotate
process:` followed by the reason, in a message and in the log; the image and its
history are left as they were and the window stays open. A still rotation cannot
be stopped once it starts. If another run holds a sequence view, Rotate shows the
view's busy warning titled **Rotate** and starts nothing. If Image History cannot
keep the image the rotation replaces, the history says so once, in a message
titled **Image History**, Rotate says nothing more, and the image and its history
are left as they were.

## Parameter interactions and starting points

| Combination | Conservative starting point | Too weak looks like | Too strong looks like | Adjustment order |
| --- | --- | --- | --- | --- |
| Small framing correction | Check Resize image; start with Bilinear or Auto/Lanczos4; Crop only after viewing borders | Residual tilt remains | Excessive interpolation and large black triangles | Set angle, choose canvas, compare kernels, then crop |
| Exact quarter/half turn | Prefer the Image-menu 90/180 action, not this process | Orientation remains wrong | Unnecessary interpolation | Use the exact action; use Rotate only if an arbitrary angle or crop is required |
| Remove empty corners | Resize image on, set angle, then check Crop and choose an inscribed region | Black borders remain | Source corners/field are removed | Finalize angle/canvas first, then X/Y/Width/Height |

## Inputs, outputs, and metadata

Rotate modifies the attached target and keeps its sample representation and
channel layout. It follows the geometry policy every geometry change shares
(Crop, Resize, both integer resamples and the **Image > Fast Geometry** turns
and mirrors): the acquisition record and meaning are kept, and the writer writes the
acquisition record when the image is saved; of the header cards read from the
file, the frame's observing context and integration cards, which
[Crop](crop.md#inputs-outputs-and-metadata) lists, are kept and the others are
dropped; an output decision is withdrawn; and the lineage records
`an::transformImageGeometry` with `Rotate`. Crop, Resize and Integer Resample
carry a plate solution moved with the pixels; Rotate drops it, together with the
card naming its solver, on a still image and on every frame of a sequence.

Validity: the black border counts as written, so where every output was written
the image's validity rule is kept and an explicit mask is turned with the
samples; where an invalid source sample prevented some output from being
written, the result states its validity as the mask the rotation wrote.

On a still image the result is one `Rotate` Image History step. On a sequence,
Rotate takes the view for the run, reads every frame - whatever the player has
selected, and demosaiced when the view demosaics them, by the method the
sequence states from the one RAW
[Demosaic Algorithm](../../interface/settings-dialog.md#raw-images-settings)
setting - writes each turned (and cropped) frame into a new temporary sequence of
the same container (a video in a container the product does not write becomes Matroska),
publishes it whole, and shows it in the view as one Image History step. Each
frame is stored as its container stores frames: SER at 8 or 16 unsigned bits,
AVI and video at 8 bits, and video at an even width and height, so an odd last
column or row is dropped from a video frame. A run that fails or is cancelled
leaves the sequence as it was and nothing in the temporary directory. No
other file or auxiliary image is written.

## Performance

Cost scales with output canvas pixels, channels, and kernel support. Resize image
can substantially enlarge the canvas near diagonal angles. Bicubic/Lanczos4
inspect wider neighbourhoods than Nearest/Bilinear. The optional crop makes
another image, the source is kept for Image History, and sequences add temporary
storage. Dialog estimates omit working planes, metadata, compression, and peak
memory.

## Worked example

- Evidence mode: Source-and-test-derived; not run on approved astronomical data.
- Input/settings: solved `1200×800` still, `Rotation Angle=+90°`,
  Resize image checked, Nearest Neighbor, Crop off.
- Expected checkpoint: content turns clockwise about the image's centre, the
  output is `800×1200`, no border is needed at a quarter turn, the plate solution
  is no longer present, and the history step reads
  `Rotated 90 degrees - Interpolation: Nearest Neighbor`.
- Inspect at 100%/statistics/history: orientation, edge samples, dimensions,
  absence of the astrometry overlay, and the Rotate history step.
- Limitations: synthetic tests establish the turn, the canvas and the metadata
  policy; they do not establish astronomical quality, photometry, production
  timing, or peak memory.

## Warnings and limitations

- Apply replaces the target and resampling is not reversible.
- The display preview rotates an existing on-screen picture, not engine output.
  Its smoothing, stretch and visible mask can differ from Apply; active masks are
  not applied. The crop fields' maxima and **New Size** come from that preview,
  so they can differ by a row or column from the grown canvas the engine makes;
  the crop keeps whatever part of the rectangle the canvas holds.
- The plate solution is dropped, including for zero-degree Crop-only use.
- Resize image grows the canvas; it does not rescale the source. Black corners
  remain.
- A newly enabled Crop can retain source-sized default fields even when Resize
  image created a larger canvas; verify the overlay rather than assuming full
  bounds.
- A still rotation has no Cancel; closing the window during a run hides it until
  the run lands but does not stop it.
- Arbitrary rotation changes flux sums and noise correlation and adds no SNR.

## Troubleshooting

| Symptom or exact error | Verified cause/validation path | Recovery | Evidence source |
| --- | --- | --- | --- |
| OK is disabled | Angle is zero and checked Crop does not change geometry | Set a nonzero angle or a changed valid crop | `RotateProcess::checkChangesPending()` |
| Corners are clipped | Resize image is unchecked | Check Resize image and rerun from the original | `RotationCanvas::Preserve` |
| Black triangles appear | Taps outside the turned source read black | Crop an inscribed rectangle or retain borders intentionally; no alternate fill exists | `rotationRequest` |
| Output contains `NaN` | Source `NaN` samples under a rule that counts every sample valid took part in the interpolation | Repair non-finite samples before Rotate and rerun from the original | sampling policy |
| **Error running rotate** with Crop checked | Crop Width or Height is zero, or the rectangle lies outside the turned canvas | Set a nonzero rectangle inside the canvas | engine crop plan |
| **Error running rotate** saying a rotation requires square source pixels | The image states a pixel aspect other than one | Rotate an image that states square pixels; a resize keeps the aspect its source states | `planRotation`; `GeometryProcessCase.AnImageResizedToAnySizeCanBeRotated` |
| **Error running rotate** otherwise | The engine refused the request or could not allocate the result | Read the message and the log, free memory, and retry | `GeometryInPlaceWindowTest.ARunTheEngineRefusesSaysWhyAndChangesNothing` |
| An **Image History** error, and the rotation is not applied | Image History could not keep the image the rotation replaces; it says so once, Rotate says nothing more, and nothing changes | Free memory and run Rotate again | `GeometryProcessCase.AnEditTheHistoryCannotKeepIsSaidOnceAndChangesNothing` |
| Astrometry overlay disappears | The geometry policy drops the plate solution of a rotation | Plate-solve the rotated result | `GeometryProcessCase.AStillImagesSolutionFollowsACropAResizeAndAResampleAndNotARotation` |
| **Rotate CFA** question | Target is a mosaic the view does not demosaic | Choose No and Debayer, or use an exact Image-menu turn when suitable | `RotateProcess::executeImageRotate()`; `GeometryProcessCase.AMosaicIsReadAsGreyOnlyWhenThePersonSaysSo` |

## Related workflow steps

Use [Crop](crop.md) when no interpolation is needed and [Resize](resize.md) when
only sampling dimensions change. For arbitrary-angle alignment of a batch, use
[Image Registration](../registration-alignment/image-registration.md) so transform
estimation and common geometry are handled together. Re-run
[Plate Solving](../registration-alignment/plate-solving.md) after Rotate.

## References and provenance

- AfterNight Rotate process, its display preview, the shared geometry support,
  the engine's geometry and sampling-kernel definitions, the sequence
  companions, and the focused tests.
- No external library is used by Rotate.
