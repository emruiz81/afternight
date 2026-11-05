# Crop

<!-- MEDIA_SLOT: UG-MEDIA-092 -->

## Purpose and expected result

Crop replaces one image, or every frame of a sequence, with the rectangular
pixel region defined by a zero-based top-left position and a width and height.
Use it to remove stacking borders, isolate a region, or reduce later memory and
runtime without interpolating the retained samples.

The result has the selected dimensions, the same sample representation and
channel layout, and exactly the source samples inside the rectangle. Crop keeps
the image's acquisition record and meaning, and carries a plate solution its
header cards state with the pixels: the reference pixel moves by the crop's
origin, so every retained pixel still points where it pointed and no new plate
solve is needed. The other header cards read from the file are dropped.

## At a glance

| Item | Current behavior |
| --- | --- |
| Menu path | **Process > Geometry > Crop** |
| Enabled when | An image or sequence is active, Crop is not already attached to it, and no other process is attached because Crop requires exclusive access |
| Inputs | One open still or sequence of any of the eight sample representations and any channel layout, a colour-filter-array mosaic included; the rectangle must lie inside the current frame |
| Outputs | Replaces the target with the selected rectangle as one Image History step; a sequence is written whole into a new sequence of the same container (another video container is written as Matroska), which the view then shows |
| Recommended image state | Any linear or nonlinear state; for registered stacks, choose one common rectangle before downstream geometry-sensitive work |
| Preview support | No processed preview; the image viewer shows and moves the crop rectangle before OK |
| Sequence support | Yes: every frame, whatever frames the player has selected, with one rectangle; progress and Cancel are shown |
| Mask support | No; an active image mask is not applied |
| External dependencies | None |

## Before you start

- Duplicate irreplaceable data or confirm that Image History provides the
  recovery you need. **OK** replaces the target rather than creating a new view;
  the step can be undone.
- Coordinates are zero-based: `(0,0)` is the top-left source pixel. The selected
  rectangle is `[X, X+Width)` by `[Y, Y+Height)`.
- Use the same valid rectangle for every sequence frame. The process does not
  find per-frame borders or align frames.
- A colour-filter-array mosaic can be cropped at any position. Crop asks nothing:
  the result states the filter arrangement that starts at the new origin, so an
  odd X or Y turns RGGB into GRBG, GBRG or BGGR and the image says so.
- Crop carries a plate solution with the pixels. The retained pixels have not
  moved on the sky, and the solution stated in the image's header cards is
  shifted by the crop's origin, with the card naming the solver beside it.
- If the sequence viewer demosaics the frames it shows, the frames are
  demosaiced before they are cropped, and the new sequence holds colour frames.

## Procedure

1. Select the target and open **Process > Geometry > Crop**. Confirm **Target
   Image** and the original **Current Size** estimate.
2. Enter **Pos X**, **Pos Y**, **Width**, and **Height**, or select **New
   Selection** and drag a rectangle in the image viewer. Confirm that the overlay
   encloses the intended pixels and **New Size** updates.
3. Select **OK**. The rectangle is read from the dialog at that moment; changing
   a field afterwards does not reach the run. A still is cropped on a worker
   without a progress window; a sequence shows progress and **Cancel**.
4. Success closes the Crop window and adds a `Crop` Image History step whose
   description is `Cropped area : X: <x> Y: <y> W:<width> H:<height>`.
5. Confirm the new dimensions. For a solved image, the astrometry overlay and
   coordinate readout use the carried solution; no new plate solve is needed.

## Complete parameter and action reference

| UI label/action | Type and unit | Runtime default | Persisted-default exception | Valid values, step, or constraints | Effect of increasing, decreasing, or selecting | Dependencies and enable/visibility rules | Quality, artifact, runtime, memory, or disk trade-off | Scope: preview/apply, channel, mask, still/sequence | Persistence |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Pos X | Integer, pixels | `0` | None | `0..source width`; step `1`; Width is limited to `source width - X` | Moves the left edge right as it increases | Changing it tightens Width's maximum and moves the overlay | No interpolation; discarded left pixels cannot contribute later | Rectangle/OK; all channels; still/sequence | Window lifetime; Reset/target change returns `0` |
| Pos Y | Integer, pixels | `0` | None | `0..source height`; step `1`; Height is limited to `source height - Y` | Moves the top edge down as it increases | Changing it tightens Height's maximum and moves the overlay | No interpolation; discarded top pixels cannot contribute later | Rectangle/OK; all channels; still/sequence | Window lifetime; Reset/target change returns `0` |
| Width | Integer, pixels | Source width | None | `0..source width - X`; step `1`; OK requires greater than `0` | Changes output width from the fixed left edge | Updates overlay, New Size, and OK state | Runtime/output memory scale linearly with width | Rectangle/OK; all channels; still/sequence | Window lifetime; Reset/target change uses full width |
| Height | Integer, pixels | Source height | None | `0..source height - Y`; step `1`; OK requires greater than `0` | Changes output height from the fixed top edge | Updates overlay, New Size, and OK state | Runtime/output memory scale linearly with height | Rectangle/OK; all channels; still/sequence | Window lifetime; Reset/target change uses full height |
| Current Size: | Read-only estimate, MB/GB | Current target bytes; multiplied by frame count for a sequence | None | Two decimal places; switches to GB above 1024 MB | Reports approximate stored sample volume before crop | Recomputed on image/target update | Excludes temporary overhead and codec compression | Information only; still/sequence | Recomputed |
| New Size: | Read-only estimate, MB/GB | Same as Current Size for the full-frame rectangle | None | `Width × Height × channels × bytes per sample`; multiplied by sequence frame count | Reports approximate output sample volume | Recomputed from Width/Height | Excludes the source kept for history, metadata, temporary sequence storage, and codec compression | Information only; still/sequence | Recomputed |
| New Selection | Checkable button and image-view drag | Unchecked | None | Click, then drag a rectangle; click again to cancel | Clears fields while drawing, then transfers the rectangle to all four fields | Uses the attached target viewer; one-click move is enabled after selection | Faster than transcribing coordinates; pixels are not processed yet | Rectangle only; still/current sequence frame | Window lifetime; Reset cancels it |
| Target Image | Image/sequence selector | Attached active target | None | Any open target supported by the generic image-process list | Reattaches Crop and resets the rectangle to that target's full dimensions | Crop has exclusive access on its attached target | A wrong target will be cropped | Rectangle/OK; all channels; still/sequence | Target lifecycle; Reset does not change target |
| OK | Dialog action | Disabled for the full image and for zero Width/Height | None | Enabled only for a nonempty rectangle different from the full image | Reads the rectangle and starts the crop on a worker | OK/Close/Reset disable while it runs; the fields stay editable but no longer reach the run | Keeps the source for Image History; a sequence adds a temporary output sequence | Apply; all channels; mask not applied; still/sequence | Success closes window and records history |
| Reset | Dialog action | Enabled | None | One action | Cancels New Selection and restores the full-frame rectangle | Disabled during execution | No pixel change | Setup only; still/sequence | Immediate; no saved settings exist |
| Close | Dialog action | Enabled | None | Button or title-bar close | Closes without starting a new crop | Button disables during execution; a title-bar close while a run is in flight hides the window, which closes once the run has landed; it does not stop the run | Nothing waits on the GUI thread | Dialog only | No settings saved |
| Cancel | Sequence-progress action | Not requested | None | Available only while a sequence is running | Stops the run between frames | The progress window belongs to the run, not to the Crop window | A stopped run leaves the sequence as it was, writes nothing into Image History and leaves no temporary sequence behind; nothing is reported | Apply; sequence only | Per run |

## How the algorithm works

### Conceptual pipeline

1. On **OK**, read X, Y, Width and Height from the dialog on the GUI thread.
   The worker reads only that value.
2. Plan the crop against the image's description. The engine refuses an empty
   rectangle and one that begins outside the image or overhangs its right or
   bottom edge.
3. Copy every retained sample of every channel, unchanged, into a new image of
   the same representation. For a mosaic, restate the filter arrangement at the
   new origin.
4. Restate the metadata under the geometry policy below and record the change in
   the image's lineage as `an::transformImageGeometry`, detail `Crop`.
5. For a still image, the result is an edit of the view: Image History records
   the step before the view shows it. For a sequence, see Inputs, outputs, and
   metadata.

### Model and equations

For source sample plane `I`, crop origin `(x0,y0)`, and output coordinates
`0 <= x < W`, `0 <= y < H`, each channel is copied exactly:

`O_c(x,y) = I_c(x+x0, y+y0)`.

There is no kernel, interpolation, averaging, fill region, clipping, or
representation change. Floating-point `NaN` and positive/negative infinity values
are copied unchanged with every other retained sample.

For a two-by-two mosaic the filter at output `(x,y)` is the filter at source
`(x+x0, y+y0)`: the result's arrangement is the source's shifted by the origin,
reduced over its two-pixel repeat. A six-by-six tabulated arrangement is carried
the same way over its six-pixel repeat.

Cropping does not alter per-pixel signal, noise, or SNR. It removes all signal
and noise outside the rectangle; an object's integrated flux is unchanged only
if its complete measured aperture and background region remain inside.

### Boundary and failure behavior

The UI prevents Width/Height from extending beyond the current target and keeps
OK disabled for a zero or unchanged full-frame rectangle. The engine refuses an
empty or out-of-bounds rectangle and a result it cannot allocate; Crop then
reports **Error running crop** with the text `An error occurred when executing the
crop process:` followed by the reason, writes the same to the log, leaves the
image and its history as they were, and keeps the window open.

No fill pixels are created. Crop has no mask path and no processed preview. A
still crop cannot be stopped once it starts. If another run already holds a
sequence view, Crop shows the view's busy warning titled **Crop** and starts
nothing. If Image History cannot keep the image the crop replaces, the history
says so once, in a message titled **Image History**, Crop says nothing more, and
the image and its history are left as they were.

## Parameter interactions and starting points

| Combination | Conservative starting point | Too weak looks like | Too strong looks like | Adjustment order |
| --- | --- | --- | --- | --- |
| Registered stack borders | Start at the first column/row valid across the complete stack and end before the opposite invalid border | Residual black/registration edge remains | Useful field or target halo is removed | Inspect all four borders, set X/Y, then Width/Height |
| CFA crop | Any rectangle; even X and Y keep the original arrangement name | Extra border remains by one or two pixels | An odd Width or Height leaves an incomplete two-by-two cell at the right or bottom edge | Choose the origin first, then the size; check the stated arrangement afterwards |
| Solved image | Retain the science field and use the smallest necessary offset | Excess blank area remains | Field needed later, such as reference stars for a fresh plate solve, is lost | Choose geometry, crop, then confirm the carried solution on the overlay |

## Inputs, outputs, and metadata

Crop modifies the attached target and keeps its sample representation (any of
the eight), channel layout and colour-filter arrangement, restated at the new
origin.

One policy decides what a geometry change does to an image's metadata, shared by
Crop, Resize, Rotate, both integer resamples and the **Image > Fast Geometry**
turns and mirrors:

- the acquisition record (camera, exposure, filter, object, dates and the rest)
  and the image's meaning (colour, numeric domain, transfer function, processing
  state) are kept, and the writer writes the acquisition record when the image
  is saved;
- a plate solution stated in the image's header cards is carried by a crop, a
  resize and an integer resample, moved with the pixels, together with the
  `PLTSOLVR` card naming its solver; a rotation, a filter-array integer resample
  and the turns and mirrors drop it;
- every other header card read from the file (FITS cards, Exif) is dropped;
- an output decision made for the image is withdrawn;
- the lineage records `an::transformImageGeometry` with the process name.

For Crop the solution's reference pixel moves by `(-X, -Y)`; its scale and
orientation, and any distortion model, are unchanged. The same move is applied to
every frame of a sequence; the new sequence keeps what its container stores.

Validity: Crop writes every result sample from a source sample, so the image's
validity rule is kept; an explicit validity mask is cropped with the samples.

On a still image the result is one `Crop` Image History step. On a sequence,
Crop takes the view for the run, reads every frame of the sequence - every one,
whatever the player has selected, and demosaiced when the view demosaics them,
by the method the sequence states from the one RAW
[Demosaic Algorithm](../../interface/settings-dialog.md#raw-images-settings)
setting - writes each cropped frame into a new temporary sequence of the same
container (a video in a container the product does not write becomes Matroska),
publishes it whole, and shows it in the view, on the frame the view showed, as one
Image History step. Each frame is stored as its container stores frames: SER at 8
or 16 unsigned bits, AVI and video at 8 bits, and video at an even width and
height, so an odd last column or row of a cropped video frame is dropped. A run
that fails or is cancelled leaves the sequence as it was and nothing in the
temporary directory. No other file, report, or auxiliary view is written.

## Performance

Work is proportional to `Width × Height × channels × bytes-per-sample` and is a
copy of stored values. A still crop keeps the source image for Image History
beside the result. A sequence additionally needs the temporary output sequence;
the estimates shown in the dialog exclude that overhead and any codec behavior.

## Worked example

- Evidence mode: Source-and-test-derived; not run on approved astronomical data.
- Input condition and settings: a `1024×768` solved image, crop
  `X=100`, `Y=200`, `Width=824`, `Height=568`.
- Expected checkpoint: output dimensions are `824×568`; original pixel
  `(600,500)` becomes `(500,300)` with exactly the same sample value and points
  at the same sky position; the acquisition record is unchanged, the plate
  solution's reference pixel has moved by `(-100,-200)`, and the history step
  reads `Cropped area : X: 100 Y: 200 W:824 H:568`.
- Inspect at 100%/statistics/history: compare a retained pixel value, the
  dimensions, the `Crop` history step, and the astrometry state.
- Limitations: focused synthetic tests prove copying, the step text and the
  metadata policy; this is not evidence of real-data framing quality, timing, or
  memory.

## Warnings and limitations

- Apply replaces the target; Crop does not create a duplicate.
- Active masks and channel selections do not limit the crop.
- The same rectangle is applied to every sequence frame without registration or
  per-frame validity analysis.
- Only the plate solution and the card naming its solver survive among the
  header cards; any other card read from the file is dropped.
- A still crop has no Cancel; closing the window during a run hides it until the
  run lands but does not stop it.
- Crop preserves retained samples but can remove flux or background area needed
  by later photometry, stacking normalization, or noise measurement.

## Troubleshooting

| Symptom or exact error | Verified cause/validation path | Recovery | Evidence source |
| --- | --- | --- | --- |
| OK is disabled | Rectangle is empty or identical to the full target | Set nonzero Width/Height and change at least one edge | `CropProcess::checkChangesPending()` |
| Width or Height will not increase | Maximum is the remaining extent after Pos X/Pos Y | Reduce the corresponding position or choose a smaller output | Crop value-change handlers |
| A cropped mosaic reports a different Bayer pattern | X or Y was odd; the result states the arrangement that starts at the new origin | Expected; the filters stay under their samples | `GeometryProcessCase.ACropKeepsTheFilterUnderEverySample` |
| **Error running crop** | The engine refused the rectangle or could not allocate the result | Read the message and the log, reset, enter an in-bounds rectangle, free memory, and retry | `GeometryInPlaceWindowTest.ARunTheEngineRefusesSaysWhyAndChangesNothing` |
| An **Image History** error, and the crop is not applied | Image History could not keep the image the crop replaces; it says so once and nothing changes | Free memory and run Crop again | `GeometryProcessCase.AnEditTheHistoryCannotKeepIsSaidOnceAndChangesNothing` |
| Astrometry overlay disappears after Crop | The image's header cards stated no plate solution, so there was none to carry | Plate-solve the cropped image | `GeometryProcessCase.AStillImagesSolutionFollowsACropAResizeAndAResampleAndNotARotation` |
| Busy warning titled **Crop** | Another run holds the sequence view | Wait for it to finish, then run again | `runInPlace` busy path |
| Sequence does not change after Cancel | A stopped run publishes nothing | Run again with the intended rectangle | `GeometryWindowTest.AStoppedRunPublishesNothing` |

## Related workflow steps

Crop registered/calibrated frames to a common valid footprint before
[Image Stacking](../stacking-integration/image-stacking.md), or crop an integrated
master before expensive filters. Use [Resize](resize.md) for a sampled dimension
change and [Rotate](rotate.md) for arbitrary orientation. Restore coordinates
with [Plate Solving](../registration-alignment/plate-solving.md) after a
rotation, a turn or a mirror, which drop the solution.

## References and provenance

- AfterNight Crop process, the shared geometry support, the engine's
  geometry plans, and the focused tests are the
  implementation sources.
- No external library is used by Crop; retained samples are copied directly.
