# Resize

## Purpose and expected result

Resize changes the pixel-grid dimensions of one image or every frame in a
sequence. Use it to create a delivery size, reduce later processing cost, or
intentionally change sampling. Unlike Crop, Resize computes new sample values; it
cannot reveal detail or signal absent from the source.

The result keeps the source sample representation, channel layout, pixel
aspect, acquisition record and meaning. A plate solution the image's header
cards state is carried, scaled with the pixels, so no new plate solve is needed,
and so are the frame's observing context - its optics, site, pointing, camera
settings, filter, object, frame type and date, with its pixel size restated for
the new pixels - and the light it holds, its `EXPTIME`, `TOTEXP`, `LIVETIME`,
`STACKCNT` and `NCOMBINE` cards as it states them; the other header cards read
from the file are dropped. Interpolation changes noise correlation, sharpness,
extrema, and photometric sums.

## At a glance

| Item | Current behavior |
| --- | --- |
| Menu path | **Process > Geometry > Resize** |
| Enabled when | An image or sequence is active, Resize is not already attached, and no attached process requires exclusive access |
| Inputs | One still or sequence of any of the eight sample representations and any channel layout; a colour-filter-array mosaic only after **Resize CFA** is answered **Yes** |
| Outputs | Replaces the target at `1..100000` pixels per axis as one Image History step; a sequence is written whole into a new sequence of the same container (another video container is written as Matroska), which the view then shows |
| Recommended image state | Any state; prefer calibrated/registered linear data for scientific resampling and a duplicate for display-size experimentation |
| Preview support | No processed preview; dimension and memory estimates update before OK |
| Sequence support | Yes: every frame, whatever frames the player has selected, with one size and interpolation; progress and Cancel are shown |
| Mask support | No; active masks are not applied |
| External dependencies | None; the resize is the image engine's own |

## Before you start

- Duplicate valuable data. **OK** replaces the target and interpolation is not
  exactly reversible; the step can be undone in Image History.
- Leave **Keep aspect ratio** checked unless geometric distortion is deliberate.
  A non-uniform resize changes object shapes and pixel scale differently by axis;
  the result is a new grid of pixels of the source's shape.
- Choose output dimensions from the actual downstream requirement. Upsampling
  increases files and computation without adding spatial information or SNR.
- For a colour-filter-array mosaic, use
  [CFA Integer Resample](../calibration/cfa-integer-resample.md) when the mosaic
  must remain a mosaic, or [Debayer](../calibration/debayer.md) first. If you
  answer **Yes** to the **Resize CFA** question, the mosaic is resized as one grey
  plane and the result states no filter array.
- Resize carries a plate solution stated in the image's header cards, with its
  scale changed by the resize and the card naming its solver beside it. A
  distortion model is kept, rescaled, while the two axis scales agree within a
  thousandth - as they do for a resize with **Keep aspect ratio** checked whose
  truncated Target Px values are a thousand pixels or more - and a resize whose
  scales differ by more keeps the linear solution and drops the distortion model.
- If the sequence viewer demosaics the frames it shows, the frames are
  demosaiced before they are resized, nothing is asked, and the new sequence holds
  colour frames.

## Procedure

1. Select the target and open **Process > Geometry > Resize**. Confirm **Original
   Px**, **Current Size**, and **Target Image**.
2. Leave **Keep aspect ratio** checked and set either **Target Px** dimension or
   its **% Adjust** field. Confirm the linked axis and **New Size**. Uncheck the
   box only for an intentional per-axis scale.
3. Choose an interpolation. The first entry reads **Auto (Lanczos4 to enlarge,
   Pixel Area Relation to reduce)**: it chooses Lanczos4 when either Target Px
   value is larger than the original and Pixel Area Relation otherwise.
4. If the target is a mosaic and the **Resize CFA** question appears, normally
   choose **No**, then Debayer or use the CFA-specific resampler.
5. Select **OK**. The dialog is read at that moment; changing a field afterwards
   does not reach the run. A still is resized on a worker without a progress
   window. A sequence shows progress and **Cancel**. Success closes Resize and
   records `Previous size: <w>x<h> - New size: <w>x<h> - Interpolation: <name>`
   in Image History, naming the interpolation that ran, never Auto.
6. Confirm output dimensions and inspect stars/high-contrast edges at 100%. On
   solved data, the carried solution stays in use (when the axis scales differ
   by more than a thousandth and the field had a distortion model, plate-solve
   again for the best fit); on quantitative data,
   remeasure background noise and flux rather than carrying pre-resize values.

## Complete parameter and action reference

| UI label/action | Type and unit | Runtime default | Persisted-default exception | Valid values, step, or constraints | Effect of increasing, decreasing, or selecting | Dependencies and enable/visibility rules | Quality, artifact, runtime, memory, or disk trade-off | Scope: preview/apply, channel, mask, still/sequence | Persistence |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Width / Original Px | Read-only integer, pixels | Source width | None | `1..100000` display range | Reports current source width | Updates on target/image change | Information only | Setup; still/sequence | Recomputed |
| Height / Original Px | Read-only integer, pixels | Source height | None | `1..100000` display range | Reports current source height | Updates on target/image change | Information only | Setup; still/sequence | Recomputed |
| Width / Target Px | Integer, pixels | Source width | None | `1..100000`; step `1` | Sets output width and updates Width % | With aspect lock, also sets Height from the same percentage; integer conversion truncates the linked result | More pixels increase memory/runtime; upsampling adds no information | OK; all channels; mask not applied; still/sequence | Window lifetime; Reset/target change restores source width |
| Height / Target Px | Integer, pixels | Source height | None | `1..100000`; step `1` | Sets output height and updates Height % | With aspect lock, also sets Width from the same percentage | Same as Width; unequal scaling distorts geometry | OK; all channels; mask not applied; still/sequence | Window lifetime; Reset/target change restores source height |
| Width / % Adjust | Double percentage | `100.00` | None | `1.00..1000.00`; default step `1.00`; two decimals | Sets width as source width times percentage | With aspect lock, also sets Height and Height % | Values above 100 enlarge storage/work without new detail | OK; still/sequence | Window lifetime; Reset/target change `100.00` |
| Height / % Adjust | Double percentage | `100.00` | None | `1.00..1000.00`; default step `1.00`; two decimals | Sets height as source height times percentage | With aspect lock, also sets Width and Width % | Unequal percentages distort object shapes | OK; still/sequence | Window lifetime; Reset/target change `100.00` |
| Keep aspect ratio | Checkbox | Checked | None | Checked/unchecked | Links both axes to one percentage | When newly checked, the process keeps the percentage of the currently larger Target Px dimension and adjusts the other axis | Prevents geometric stretching; integer dimensions can leave a tiny per-axis scale difference | Setup/OK; still/sequence | Window lifetime; Reset checks it |
| Interpolation | Combo box | **Auto (Lanczos4 to enlarge, Pixel Area Relation to reduce)** | None | Auto, **Nearest Neighbor**, **Bilinear**, **Bicubic**, **Pixel Area Relation**, **Lanczos4**, **Bit Exact Bilinear**, **Bit Exact Nearest Neighbor** | Selects the sampling kernel; Auto chooses Lanczos4 when either Target Px value is larger than the original, otherwise Pixel Area Relation | Always visible; read when OK is pressed | Nearest is blocky; Bilinear/Bicubic/Lanczos4 smooth across neighborhoods; Bicubic/Lanczos4 can ring and are guarded against dark rings; Pixel Area Relation suits reduction; the Bit Exact entries follow fixed-point rules | OK; each channel; still/sequence | Window lifetime; Reset returns Auto |
| Current Size: | Read-only estimate, MB/GB | Current sample bytes; multiplied by frame count for sequence | None | Two decimals; GB above 1024 MB | Reports approximate current sample volume | Recomputed on target update | Excludes metadata/compression | Information only | Recomputed |
| New Size: | Read-only estimate, MB/GB | Same as current at 100% | None | `Target width × Target height × channels × bytes per sample`; frame-count multiplier for sequence | Reports approximate output sample volume | Updates with target dimensions | Excludes working planes, the source kept for history, temporary sequence storage, metadata, and compression | Information only | Recomputed |
| Target Image | Image/sequence selector | Attached active target | None | Any open target accepted by generic image-process support | Reattaches and resets all dimensions/options for the new target | Cannot attach where an exclusive process conflicts | Wrong target is replaced | Setup/OK; still/sequence | Target lifecycle |
| OK | Dialog action | Disabled at unchanged dimensions | None | Enabled when either Target Px axis differs | Asks the **Resize CFA** question for a mosaic, then reads the dialog and starts the resize on a worker | OK/Close/Reset disable while it runs; the fields stay editable but no longer reach the run | Keeps the source for Image History; a sequence adds a temporary output sequence | Apply; all channels; mask not applied; still/sequence | Success closes and records history |
| Reset | Dialog action | Enabled | None | One action | Restores source dimensions, 100%, aspect lock, and Auto | Disabled during execution | No pixel change | Setup | Immediate |
| Close | Dialog action | Enabled | None | Button or title-bar close | Closes without starting a new resize | Button disables during execution; a title-bar close while a run is in flight hides the window, which closes once the run has landed; it does not stop the run | Nothing waits on the GUI thread | Dialog only | No settings saved |
| Cancel | Sequence-progress action | Not requested | None | Available only during a sequence run | Stops the run between frames | The progress window belongs to the run | A stopped run leaves the sequence and its history as they were and no temporary sequence behind; nothing is reported | Apply; sequence only | Per run |

## How the algorithm works

### Conceptual pipeline

1. On **OK**, ask the **Resize CFA** question when the target is a mosaic the
   view does not demosaic, then read the target size and the interpolation on
   the GUI thread, resolving Auto: Lanczos4 when either Target Px value is larger
   than the original, otherwise Pixel Area Relation.
2. Plan the resize against the image's description. A mosaic is planned as one
   grey plane, its filter array forgotten, only after **Yes**.
3. Sample every channel at each destination pixel through the chosen
   interpolation, in Float32 working planes accumulated in Float64, with the edge
   sample repeated for taps beyond the border.
4. For Bicubic and Lanczos4, apply the dark-ringing guard described below.
5. Write the result back in the source representation through the default
   conversion (nearest code, saturating at the representation's range). A
   floating image in a normalized domain is held to `[0,1]`; one in a sensor-code
   or physical domain is left as computed.
6. Restate the metadata under the geometry policy and record
   `an::transformImageGeometry`, detail `Resize`, in the lineage.

### Model and equations

Let `sx = Win/Wout` and `sy = Hin/Hout`. Each destination sample is a weighted sum
of source samples around a source coordinate:

`O_c(x,y) = Σ_i Σ_j I_c(i,j) Kx(i - u(x)) Ky(j - v(y))`.

Every interpolation except Nearest Neighbor uses the half-pixel convention,
`u(x) = (x + 0.5)·sx - 0.5` and likewise `v(y)`. The kernels:

- **Nearest Neighbor**: one donor at the asymmetric index `floor(x·sx)`.
- **Bilinear**: the four surrounding samples with linear weights.
- **Bicubic**: a four-by-four neighbourhood with the cubic kernel `a = -0.75`.
- **Pixel Area Relation**: when neither axis grows, the area average of each
  destination cell's source footprint; when either axis grows, a bilinear sample.
- **Lanczos4**: an eight-by-eight neighbourhood with a radius-four Lanczos
  window.
- **Bit Exact Bilinear**: the fixed-point linear coefficients and rounding of
  OpenCV's `INTER_LINEAR_EXACT` rule for 8-bit, unsigned 16-bit and signed
  16-bit samples; ordinary Bilinear for the other representations.
- **Bit Exact Nearest Neighbor**: the half-pixel nearest rule evaluated in a
  16-bit fixed-point coordinate, as OpenCV's `INTER_NEAREST_EXACT` evaluates it,
  copying a stored value exactly.

**Dark-ringing guard.** For Bicubic and Lanczos4, each output is compared with a
guide: the area average of its source footprint when neither axis grows, a
bilinear sample otherwise. An output below `0.98 ×` its guide is replaced by the
guide, together with its four neighbours. The guard suppresses dark rims beside
bright stars; it does not limit overshoot.

The result states the source's pixel aspect exactly, whatever the rounding of
its two axis scales: a resize that scales one axis more than the other makes a
new grid of pixels of the source's shape rather than stating stretched pixels,
which Rotate refuses. A resized image with square
pixels can therefore always be rotated.

Resize is not a flux-conserving photometric operation. Pixel Area Relation
reduction generally preserves local average/surface brightness, not the sum of
stored pixel values. Upsampling distributes correlated estimates over more
pixels; it adds neither independent samples nor SNR. Downsampling can reduce
per-pixel noise variation through averaging, but introduces correlation and
changes the measurement aperture in pixels. Remeasure rather than assuming an
SNR or flux scaling.

### Boundary and failure behavior

The UI prevents zero dimensions and caps each Target Px axis at 100000. The
engine creates exactly the requested canvas; Resize has no fill control or extra
border. Integer results saturate at their representation's range. A normalized
floating result is held to `[0,1]`, so finite excursions and infinities outside
it are clipped; a sensor-code or physical-domain result keeps them.

A sample the image's validity rule calls invalid - a `NaN` under a rule that
calls non-finite samples invalid, or a sample an explicit mask clears - prevents
every output whose kernel reaches it from being written; those outputs are marked
invalid in the result's validity mask. Under a rule that counts every sample
valid, a `NaN` takes part in the interpolation and can spread to its neighbours.

Answering **No** to the **Resize CFA** question starts nothing. The engine's
refusal of the request or an allocation it cannot make becomes **Error running
resize**, with
the text `An error occurred when executing the resize process:` followed by the
reason, in a message and in the log; the image and its history are left as they
were and the window stays open. A still resize cannot be stopped once it starts.
If another run holds a sequence view, Resize shows the view's busy warning titled
**Resize** and starts nothing. If Image History cannot keep the image the resize
replaces, the history says so once, in a message titled **Image History**, Resize
says nothing more, and the image and its history are left as they were.

## Parameter interactions and starting points

| Combination | Conservative starting point | Too weak looks like | Too strong looks like | Adjustment order |
| --- | --- | --- | --- | --- |
| Downsampling | Keep aspect ratio; set final dimensions; start with Auto (Pixel Area Relation) | File/work remain larger than required | Stars and fine structures merge or alias | Choose required size, compare Area with Bilinear/Lanczos at 100%, then commit |
| Upsampling | Keep aspect ratio; use only a downstream-required size; Auto chooses Lanczos4 | Output still undersized for the destination | Larger soft/ringing pixels and excessive storage with no added detail | Fix destination requirement, choose size, inspect ringing, then compare Bicubic/Bilinear |
| Non-uniform resize | Leave locked unless correcting a known geometric scale error | Intended distortion remains | Stars/objects stretch, and a distortion model of the plate solution is dropped | Establish axis scales externally, unlock, set axes, then verify shape |

## Inputs, outputs, and metadata

Resize modifies one target and keeps its sample representation and channel
layout. It follows the geometry policy every geometry change shares (Crop,
Rotate, both integer resamples and the **Image > Fast Geometry** turns and
mirrors):
the acquisition record and meaning are kept, and the writer writes the
acquisition record when the image is saved; every header card read from the file
(FITS cards, Exif) is dropped except a plate solution, the `PLTSOLVR` card
naming its solver and the frame's observing context and integration cards,
which [Crop](crop.md#inputs-outputs-and-metadata) lists; an output decision is
withdrawn; and the lineage records `an::transformImageGeometry` with `Resize`.
The result's description states the new dimensions and the source's pixel
aspect.

Pixel size: the observing context's `XPIXSZ` and `YPIXSZ` are divided by the
resize's scale along each axis, so the plate scale the optics imply is still
the result's: a 50% resize of a frame of 2.315 µm pixels states 4.63.

Plate solution: a solution the image's header cards state is carried with its
scale matrix multiplied by the axis scales and its reference pixel moved with
the grid, so every result pixel points where the source position it came from
pointed. A distortion model is carried while the two axis scales agree within a
thousandth of the larger: its coefficients are rescaled by the mean of the two
scales and its reference pixel moves with the grid on each axis. A resize whose
scales differ by more keeps the linear solution and drops a distortion model it
no longer describes. The same move is
applied to every frame of a sequence; the new sequence keeps what its container
stores. Rotate, a filter-array integer resample and the turns and mirrors drop
the solution.

Validity: where every output was written, the image's validity rule is kept, and
an explicit mask is resampled with the samples; where an invalid source sample
prevented some output from being written, the result states its validity as the
mask the resize wrote.

A mosaic resized after **Yes** is one grey plane with no filter array. On a still
image the result is one `Resize` Image History step. On a sequence, Resize takes
the view for the run, reads every frame - whatever the player has selected, and
demosaiced when the view demosaics them, by the method the sequence states from
the one RAW
[Demosaic Algorithm](../../interface/settings-dialog.md#raw-images-settings)
setting - writes each resized frame into a new temporary sequence of the same
container (a video in a container the product does not write becomes Matroska),
publishes it whole, and shows it in the view as one Image History step. Each
frame is stored as its container stores frames: SER at 8 or 16 unsigned bits,
AVI and video at 8 bits, and video at an even width and height, so an odd last
column or row is dropped from a video frame. A run that fails or is cancelled
leaves the sequence as it was and nothing in the temporary directory. No
standalone image file or auxiliary view is written.

## Performance

Work and memory increase with output pixels, channels, and kernel support.
Nearest/Bilinear are generally cheaper than Bicubic/Lanczos4, which also compute
the guard's guide. The working planes and the result coexist with the source,
which is kept for Image History. Sequence processing adds temporary output
storage. The two dialog estimates count samples only and do not predict peak
memory, compressed file size, or execution time.

## Worked example

- Evidence mode: Source-and-test-derived; not executed on approved astronomical data.
- Input and settings: a solved `6000×4000` image, Keep aspect ratio checked,
  Width Target Px `3000`, Auto.
- Expected checkpoint: Height becomes `2000`, Auto chooses Pixel Area Relation,
  output is `3000×2000`, axis scales are `0.5`, the result states square pixels,
  the plate solution is carried at twice the angular size per pixel, and the
  history step reads
  `Previous size: 6000x4000 - New size: 3000x2000 - Interpolation: Pixel Area Relation`.
- Inspect at 100%/statistics/history: dimensions, star profiles/edges, background
  noise, the astrometry state, and the interpolation named in the Resize history
  step.
- Limitations: synthetic tests establish geometry, kernels, and the metadata
  policy; this does not establish real-image sharpness, flux accuracy, production
  timing, or memory.

## Warnings and limitations

- Resize replaces the target and interpolation is not exactly reversible.
- Active masks and display/channel choices do not limit processing.
- A mosaic resized after **Yes** mixes its colour positions into one grey plane.
- Auto is only a direction rule: if either axis grows it chooses Lanczos4, even
  when the other axis shrinks; otherwise it chooses Pixel Area Relation.
- Percent-to-pixel conversion truncates to integer dimensions, so the displayed
  linked percentages and exact `sx/sy` can differ slightly; the result's pixels
  keep the source's shape, and the carried solution follows the exact scales.
- A resize whose two axis scales differ by more than a thousandth drops a
  distortion model from the carried plate solution. A locked aspect ratio whose
  truncated dimensions are a thousand pixels or more on each side never does; a
  smaller result can, by a pixel's truncation. Plate-solve again when a
  distorted field needs it.
- A normalized floating result is held to `[0,1]` and an integer result to its
  representation's range; ringing or saturated samples can lose recoverable
  range.
- A still resize has no Cancel; closing the window during a run hides it until
  the run lands but does not stop it.
- Resampling changes noise correlation and stored-value sums; it does not create
  flux, resolution, or SNR.

## Troubleshooting

| Symptom or exact error | Verified cause/validation path | Recovery | Evidence source |
| --- | --- | --- | --- |
| OK is disabled | Both Target Px values equal current geometry | Change at least one axis | `ResizeProcess::checkChangesPending()` |
| Linked dimension is off by one | Percentage-to-integer conversion and source aspect do not divide exactly | Enter the exact required Target Px; accept the nearest integer aspect or unlock deliberately | Resize value handlers |
| **Resize CFA** question | Target is a mosaic the view does not demosaic | Choose No, then Debayer or use CFA Integer Resample | `ResizeProcess::executeImageResize()`; `GeometryProcessCase.AMosaicIsReadAsGreyOnlyWhenThePersonSaysSo` |
| Fine detail looks blocky | Nearest enlargement or excessive scale | Compare Bilinear, Bicubic, or Lanczos4 and avoid unnecessary enlargement | interpolation mapping |
| Output contains `NaN` | Source `NaN` samples under a rule that counts every sample valid took part in the interpolation | Repair non-finite samples before Resize and rerun from the original | sampling policy |
| Dark rims replaced by smoother values or clipped peaks | The dark-ringing guard replaced a ring with its guide, or the result was held to its range | Compare Bilinear/Area, reduce scale, and work from an unclipped source | `GeometryUnits.ANormalizedFloatingResultIsHeldToTheUnitInterval` |
| Astrometry overlay disappears after resize | The image's header cards stated no plate solution, so there was none to carry | Plate-solve the resized image | `GeometryProcessCase.AStillImagesSolutionFollowsACropAResizeAndAResampleAndNotARotation` |
| **Error running resize** | The engine refused the request or could not allocate the result | Read the message and the log, choose supported dimensions, free memory, and retry | `GeometryInPlaceWindowTest.ARunTheEngineRefusesSaysWhyAndChangesNothing` |
| An **Image History** error, and the resize is not applied | Image History could not keep the image the resize replaces; it says so once, Resize says nothing more, and nothing changes | Free memory and run Resize again | `GeometryProcessCase.AnEditTheHistoryCannotKeepIsSaidOnceAndChangesNothing` |

## Related workflow steps

Use [Crop](crop.md) first when unwanted borders should be removed without
interpolation. Use [Integer Resample](integer-resample.md) for exact integer
pixel replication and [Rotate](rotate.md) for orientation. For CFA-preserving
integer operations, use
[CFA Integer Resample](../calibration/cfa-integer-resample.md). Resize carries a
plate solution; [Plate Solving](../registration-alignment/plate-solving.md)
restores one after a rotation or a filter-array resample.

## References and provenance

- AfterNight Resize process, the shared geometry support, the engine's
  geometry and sampling-kernel definitions, the sequence companions, and the
  focused tests.
- No external library is used by Resize.
