# Integer Resample

## Purpose and expected result

Integer Resample changes image dimensions by a whole-number factor.
**Downsample** combines each factor-by-factor block of each channel with a named
reducer; **Upsample** copies each source sample into a constant square block
without interpolation. Use it when exact replication or a named block reducer is
more important than arbitrary output dimensions.

The result keeps the sample representation, channel layout, acquisition record
and meaning. A plate solution the image's header cards state is carried, scaled
by the factor, so no new plate solve is needed, and so are the frame's observing
context - its optics, site, pointing, camera settings, filter, object, frame
type and date, with its pixel size restated for the new pixels - and the light
it holds, its `EXPTIME`, `TOTEXP`, `LIVETIME`, `STACKCNT` and `NCOMBINE` cards
as it states them; the other header cards read from the file are dropped.
Neither direction is a flux-conserving scientific binning model, and neither
adds information or SNR.

## At a glance

| Item | Current behavior |
| --- | --- |
| Menu path | **Process > Geometry > Integer Resample** |
| Enabled when | An image or sequence is active, Integer Resample is not already attached, and no attached process requires exclusive access |
| Inputs | One still or sequence of any of the eight sample representations; factor `2..100`; a colour-filter-array mosaic only after **Integer Resample CFA** is answered **Yes** |
| Outputs | Replaces the target at floor-divided Downsample dimensions or factor-multiplied Upsample dimensions as one Image History step; a sequence is written whole into a new sequence of the same container (another video container is written as Matroska) |
| Recommended image state | Any state; use calibrated linear data for noise reduction and a duplicate for display/pixel-art enlargement |
| Preview support | No processed preview; dimensions and sample estimates update before OK |
| Sequence support | Yes: every frame, whatever frames the player has selected, with one direction, factor and reducer; progress and Cancel are shown |
| Mask support | No; active masks are not applied |
| External dependencies | None |

## Before you start

- Duplicate valuable data. Downsample discards spatial detail and can discard
  trailing rows/columns; Upsample multiplies storage without adding information.
  The step can be undone in Image History.
- Ensure the factor is no larger than either dimension in Downsample. The UI
  permits `2..100` without enforcing the image-size limit; the engine refuses an
  oversized factor.
- Prefer dimensions exactly divisible by the factor. Otherwise output dimensions
  are floored and the trailing rows and columns that cannot fill a whole block are
  discarded.
- For a colour-filter-array mosaic, use
  [CFA Integer Resample](../calibration/cfa-integer-resample.md), or Debayer
  first. If you answer **Yes** to **Integer Resample CFA**, the mosaic is
  resampled as one grey plane, mixing its colour positions, and the result states
  no filter array.
- Integer Resample carries a plate solution stated in the image's header cards,
  scaled by the factor, with the card naming its solver beside it.
- If the sequence viewer demosaics the frames it shows, the frames are
  demosaiced before they are resampled, nothing is asked, and the new sequence
  holds colour frames.

## Procedure

1. Select the target and open **Process > Geometry > Integer Resample**. Confirm
   **Original Px**, **Current Size**, and **Target Image**.
2. Choose **Downsample** or **Upsample**, then set **Resample Factor**.
   Confirm the computed **Target Px** and **New Size**.
3. For Downsample, select **Average**, **Median**, **Maximum**, or **Minimum**.
   This control disables in Upsample because replication has no reducer.
4. Confirm both source dimensions are divisible by the factor when complete edge
   coverage matters. For Downsample, also confirm the factor is no larger than the
   smaller dimension.
5. Select **OK**. The dialog is read at that moment; changing a field afterwards
   does not reach the run. A still is resampled on a worker without a progress
   window; a sequence shows progress and **Cancel**. Success closes the window and
   adds an `Integer Resample` history step: `Binning factor: <f>X - New size:
   <w>x<h> - Binning method: <reducer>` or `Resample factor: <f>X - New size:
   <w>x<h>`.
6. Confirm exact dimensions. For Downsample, remeasure background noise, star
   profiles, extrema, and photometric sums. For Upsample, verify that each source
   pixel became a constant `factor × factor` block.

## Complete parameter and action reference

| UI label/action | Type and unit | Runtime default | Persisted-default exception | Valid values, step, or constraints | Effect of increasing, decreasing, or selecting | Dependencies and enable/visibility rules | Quality, artifact, runtime, memory, or disk trade-off | Scope: preview/apply, channel, mask, still/sequence | Persistence |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Resample Factor | Integer multiplier/divisor | `2` | None | `2..100`; step `1`; Downsample engine also requires factor no larger than either source axis | Larger factors shrink more in Downsample or enlarge more in Upsample | Recomputes readouts/size and OK | Downsample loses more detail; Upsample memory grows by factor squared | OK; all channels; mask not applied; still/sequence | Window lifetime; Reset `2` |
| Downsample Mode | Combo box | **Average** | None | **Average**, **Median**, **Maximum**, **Minimum** | Chooses the reducer for each source block | Enabled only with Downsample | Average smooths; Median rejects isolated outliers; Maximum biases bright; Minimum biases dark | Downsample only; each channel independently | Window lifetime; Reset Average |
| Downsample | Radio button | Selected | None | Mutually exclusive with Upsample | Uses floor-divided output dimensions and the selected reducer | Enables Downsample Mode | Reduces samples/work but irreversibly merges/discards information | OK; all channels; still/sequence | Window lifetime; Reset selects |
| Upsample | Radio button | Unselected | None | Mutually exclusive with Downsample | Replicates every sample into an `N×N` block | Disables Downsample Mode | Adds blockiness and factor-squared storage with no new detail | OK; all channels; still/sequence | Window lifetime; Reset clears |
| Width / Original Px | Read-only integer, pixels | Source width | None | A readout of any size | Reports source width | Updates on target/image change | Information only | Setup | Recomputed |
| Height / Original Px | Read-only integer, pixels | Source height | None | A readout of any size | Reports source height | Updates on target/image change | Information only | Setup | Recomputed |
| Width / Target Px | Read-only integer, pixels | `floor(source width/2)` | None | Downsample is floor division, Upsample is source width × factor, at any size | Reports computed width | Not editable; a Downsample factor larger than the width shows `1` | The readout, the history text and the size estimate are the size the run makes, except a Downsample factor larger than a side, which the run refuses | Setup/OK | Recomputed |
| Height / Target Px | Read-only integer, pixels | `floor(source height/2)` | None | Same rules as Width | Reports computed height | Not editable; UI clamping can differ from actual engine result | Same limitation as Width | Setup/OK | Recomputed |
| Current Size: | Read-only estimate, MB/GB | Current sample bytes × sequence frame count | None | Two decimals; GB above 1024 MB | Reports approximate current sample volume | Recomputed on target updates | Excludes metadata/compression | Information only | Recomputed |
| New Size: | Read-only estimate, MB/GB | Quarter current area at default Downsample 2 | None | Uses the clamped Target Px readouts × channels × bytes per sample × frame count | Estimates output sample volume | Recomputed with factor/direction and when the target's image changes | Can underreport very large Upsample output; excludes the source kept for history, temporary sequence storage and compression | Information only | Recomputed |
| Target Image | Image/sequence selector | Attached active target | None | Any open target accepted by generic attachment gates | Reattaches and resets factor/direction/mode | Cannot attach where exclusive access conflicts | Wrong target is resampled | Setup/OK; still/sequence | Target lifecycle |
| OK | Dialog action | Enabled when computed readouts differ from current dimensions | None | Asks the **Integer Resample CFA** question for a mosaic first | Reads the dialog and starts the resample on a worker | OK/Close/Reset disable while it runs; the fields stay editable but no longer reach the run | Keeps the source for Image History; a sequence adds a temporary output sequence | Apply; all channels; mask not applied | Success closes and records history |
| Reset | Dialog action | Enabled | None | One action | Restores factor 2, Downsample, and Average | Disabled during execution | No pixel change | Setup | Immediate |
| Close | Dialog action | Enabled | None | Button or title-bar close | Closes without new processing | Button disables during execution; a title-bar close while a run is in flight hides the window, which closes once the run has landed; it does not stop the run | Nothing waits on the GUI thread | Dialog only | No settings saved |
| Cancel | Sequence-progress action | Not requested | None | Available during sequence execution | Stops the run between frames | The progress window belongs to the run | A stopped run leaves the sequence and its history as they were and no temporary sequence behind; nothing is reported | Apply; sequence only | Per run |

## How the algorithm works

### Conceptual pipeline

1. On **OK**, ask the **Integer Resample CFA** question when the target is a
   mosaic the view does not demosaic, then read the factor, the direction and the
   reducer on the GUI thread.
2. Plan the resample against the image's description. A mosaic is planned as one
   grey plane, its filter array forgotten, only after **Yes**.
3. For Downsample, compute `Wout=floor(Win/f)`, `Hout=floor(Hin/f)` and combine
   each `f×f` block starting at `(x·f, y·f)` with the reducer, per channel.
   Samples to the right of `Wout·f` or below `Hout·f` are discarded.
4. For Upsample, copy each source sample exactly into an `f×f` block, per
   channel.
5. Write the result in the source representation, restate the metadata under the
   geometry policy, and record `an::transformImageGeometry`, detail
   `Integer Resample`, in the lineage.

### Model and equations

For a Downsample block `B` of the samples the image's validity rule calls valid:

- Average: `O_c(x,y) = (1/|B|) Σ_(i,j in B) I_c(i,j)`, accumulated in double.
- Median: the middle sample, or the mean of the two middle samples for an even
  count.
- Maximum: `max B`.
- Minimum: `min B`.

An integer result is rounded to the nearest code, ties to even. A block whose
samples are all invalid is left unwritten and marked invalid in the result's
validity mask. Under a rule that counts every sample valid, a `NaN` or infinity
takes part in the reducer and can make its block's answer non-finite; clean or
replace non-finite samples first when deterministic statistics are required.

Upsample is exact replication:

`O_c(fx+i, fy+j) = I_c(x,y)` for `0 <= i,j < f`.

Average preserves a local mean/surface-brightness convention, not the sum of
stored samples. Under independent equal-noise assumptions an `n`-sample Average
reduces standard deviation of the mean by about `sqrt(n)` and can improve
per-output-sample SNR for a uniform signal, but real resampling, structure, and
correlated noise violate that simple model. Median/Maximum/Minimum have different
nonlinear noise biases. Upsample repeats both signal and noise; it creates no
independent samples or SNR, while the stored sum grows by `f²`.

### Boundary and failure behavior

Downsample refuses a factor larger than either source dimension. The UI prevents
zero/factor-one values but not a factor greater than a small image. Trailing rows
and columns that cannot fill a block are discarded; there is no partial-edge bin
or fill. Upsample refuses a result too large for the engine's limits; Target Px
shows the size it asked for.

Answering **No** to **Integer Resample CFA** starts nothing. An engine refusal or
an allocation it cannot make becomes **Error running integer resample**, with the
text `An error occurred when executing the integer resample process:` followed by
the reason, in a message and in the log; the image and its history are left as
they were and the window stays open. A still resample cannot be stopped once it
starts. If another run holds a sequence view, Integer Resample shows the view's
busy warning titled **Integer Resample** and starts nothing. If Image History
cannot keep the image the resample replaces, the history says so once, in a
message titled **Image History**, Integer Resample says nothing more, and the
image and its history are left as they were.

## Parameter interactions and starting points

| Combination | Conservative starting point | Too weak looks like | Too strong looks like | Adjustment order |
| --- | --- | --- | --- | --- |
| Noise reduction by Average | Start at factor 2 on dimensions divisible by 2 | Noise reduction/storage saving is insufficient | Stars and small structures merge; sampling becomes coarse | Choose factor, verify divisibility, inspect profiles/noise, then commit |
| Outlier-resistant reduction | Factor 2 Median on a duplicate | Isolated defects remain | Small stars/peaks are suppressed and nonlinear bias dominates | Choose factor, compare Average/Median, then inspect rejected structures |
| Exact enlargement | Upsample factor 2 only when a block grid is required | Destination still undersized | Obvious blocks and excessive memory with no added resolution | Set destination need, choose smallest factor, verify displayed versus actual dimensions |

## Inputs, outputs, and metadata

Integer Resample modifies the attached target and keeps its sample representation,
channel layout and pixel aspect. It follows the geometry policy every geometry
change shares (Crop, Resize, Rotate, CFA Integer Resample and the
**Image > Fast Geometry** turns and mirrors): the acquisition record and meaning are
kept, and the writer writes the acquisition record when the image is saved; every
header card read from the file is dropped except a plate solution, the
`PLTSOLVR` card naming its solver and the frame's observing context and
integration cards, which [Crop](crop.md#inputs-outputs-and-metadata) lists; an
output decision is withdrawn; and the lineage records
`an::transformImageGeometry` with `Integer Resample`.

Plate solution: a solution the image's header cards state is carried with its
scale divided by the factor for Downsample or multiplied by it for Upsample, its
reference pixel moved with the grid, so every result pixel points where its
block pointed. The same move is applied to every frame of a sequence; the new
sequence keeps what its container stores. Rotate, CFA Integer Resample and the
turns and mirrors drop the solution.

Pixel size: the observing context's `XPIXSZ` and `YPIXSZ` are multiplied by
the factor for Downsample and divided by it for Upsample - the exact factor,
whatever trailing pixels a Downsample discards - so the plate scale the optics
imply is still the result's: a Downsample by 2 of a frame of 2.315 µm pixels
states 4.63, and an Upsample by 2 states 1.1575.

Validity: where every output was written, the image's validity rule is kept and
an explicit mask is resampled with the samples; where a block had no valid
sample, the result states its validity as the mask the resample wrote.

On a still image the result is one `Integer Resample` Image History step. On a
sequence, Integer Resample takes the view for the run, reads every frame -
whatever the player has selected, and demosaiced when the view demosaics them, by
the method the sequence states from the one RAW
[Demosaic Algorithm](../../interface/settings-dialog.md#raw-images-settings)
setting - writes each resampled frame into a new temporary sequence of the same
container (a video in a container the product does not write becomes Matroska),
publishes it whole, and shows it in the view as one Image History step. Each
frame is stored as its container stores frames: SER at 8 or 16 unsigned bits,
AVI and video at 8 bits, and video at an even width and height, so an odd last
column or row is dropped from a video frame. A run that fails or is cancelled
leaves the sequence as it was and nothing in the temporary directory. No
other file or auxiliary image is created.

## Performance

Downsample work is proportional to the input samples included. Upsample writes
`f²` values per source sample, so output memory, work, and temporary sequence
storage grow quadratically with factor. The source is kept for Image History
beside the result. Dialog estimates omit working memory, metadata and
compression, and can underreport oversized Upsample output due to readout
clamping.

## Worked example

- Evidence mode: Source-and-test-derived; not run on approved astronomical data.
- Input/settings: `6×4` single-channel values, Downsample, factor `2`, Average.
- Expected checkpoint: output is `3×2`; each sample is the arithmetic mean of its
  corresponding `2×2` source block, rounded to the nearest code for an integer
  image. With input dimensions divisible by 2, no edge sample is discarded. The
  history step reads `Binning factor: 2X - New size: 3x2 - Binning method:
  Average`.
- Upsample comparison: factor `2` produces `12×8` and every original value appears
  as one constant `2×2` block.
- A `13×8` image at factor `3` gives `4×2`: the thirteenth column and the last two
  rows are discarded.
- Inspect at 100%/statistics/history: exact block values, dimensions, background
  standard deviation, extrema, the carried plate solution of a solved image, and
  history parameters.
- Limitations: synthetic tests prove reducer/replication behavior; this does not
  establish real-data quality, flux accuracy, production timing, or peak memory.

## Warnings and limitations

- Apply replaces the target; Downsample information cannot be reconstructed.
- Active masks and display/channel selection do not limit processing.
- Non-divisible dimensions discard trailing pixels; choose divisible geometry.
- Average/Median/Maximum/Minimum do not preserve integrated stored-value sums.
- Upsample copies values exactly but inflates sum/storage by factor squared and
  adds no detail or SNR.
- A mosaic resampled after **Yes** mixes its colour positions into one grey plane.
- Only the plate solution, the card naming its solver and the frame's observing
  context and integration cards survive among the header cards; any other card
  read from the file is dropped.
- A large Upsample can exhaust memory: Target Px and the size estimate show what
  it asks for. A Downsample factor larger than a side shows `1` and is refused.
- If the target's dimensions change while the window is open, **Original Px**,
  **Target Px**, both size estimates, the OK state and the history text follow
  the new dimensions.
- A still resample has no Cancel; closing the window during a run hides it until
  the run lands but does not stop it.

## Troubleshooting

| Symptom or exact error | Verified cause/validation path | Recovery | Evidence source |
| --- | --- | --- | --- |
| **Error running integer resample** naming a complete source block | Downsample factor exceeds width or height | Reduce factor to at most the smaller dimension | engine integer resample plan |
| Output edge is missing | Source dimension was not divisible and trailing samples were discarded | Crop to divisible dimensions first or choose another factor | `GeometryUnits` incomplete-block case |
| Downsample result is darker/brighter | Minimum/Maximum was selected, or integrated sum was compared to an average/median result | Choose intended statistic and compare local means rather than raw sums | reducers |
| Output contains `NaN` or an unexpected non-finite statistic | A block contained `NaN`/infinity under a rule that counts every sample valid | Repair non-finite samples before resampling and rerun from the original | reducer validity rule |
| **Integer Resample CFA** question | Target is a mosaic the view does not demosaic | Choose No and use CFA Integer Resample or Debayer first | `GeometryProcessCase.AMosaicIsReadAsGreyOnlyWhenThePersonSaysSo` |
| Astrometry overlay disappears | The image's header cards stated no plate solution, so there was none to carry | Plate-solve the result | `GeometryProcessCase.AStillImagesSolutionFollowsACropAResizeAndAResampleAndNotARotation` |
| **Error running integer resample** otherwise | The engine refused the request or could not allocate the result | Read the message and the log, use a safe factor/direction, free memory, and retry | `GeometryInPlaceWindowTest.ARunTheEngineRefusesSaysWhyAndChangesNothing` |
| An **Image History** error, and the resample is not applied | Image History could not keep the image the resample replaces; it says so once, Integer Resample says nothing more, and nothing changes | Free memory and run Integer Resample again | `GeometryProcessCase.AnEditTheHistoryCannotKeepIsSaidOnceAndChangesNothing` |

## Related workflow steps

Use [Crop](crop.md) first to make dimensions divisible without interpolating.
Use [Resize](resize.md) for arbitrary dimensions and kernels. Use
[CFA Integer Resample](../calibration/cfa-integer-resample.md) for mosaic-aware
integer operations. Integer Resample carries a plate solution;
[Plate Solving](../registration-alignment/plate-solving.md) restores one after a
rotation or a CFA Integer Resample.

## References and provenance

- AfterNight Integer Resample process, the shared geometry support, the
  engine's geometry plans, the sequence companions, and the focused tests.
- No external algorithm or runtime is used by these block operations.
