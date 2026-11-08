# CFA Integer Resample

## Purpose and expected result

CFA Integer Resample changes an undebayered Bayer mosaic by an integer factor
without mixing the four CFA positions. It runs one filter-aware plan of the
image engine: Downsample combines only samples under the same filter, and
Upsample replicates each sample within its own filter's lattice. The result is a
mosaic of the same arrangement and phase.

The operation modifies the selected still image or replaces its sequence. It is
not ordinary interpolation and does not create additional sensor resolution.

## At a glance

| Item | Current behavior |
| --- | --- |
| Menu path | **Process > Geometry > CFA Integer Resample** |
| Enabled when | An image or sequence view is active. The target selector lists only views shown as a one-channel mosaic; a target that does not state a two-by-two mosaic is refused when **OK** is pressed. |
| Inputs | One undebayered RGGB/BGGR/GRBG/GBRG still or sequence. An image that states no filter array, a colour image and an X-Trans mosaic are refused by name. |
| Outputs | Same target/sequence at a CFA-preserving integer scale, same sample type, Bayer pattern and phase. No new view/file for stills. |
| Recommended image state | Calibrated linear CFA data before Debayer. |
| Preview support | None. Read-only geometry/memory estimates are the only pre-execution feedback. |
| Sequence support | Supported: every frame is resampled into a new sequence that replaces the view's, with progress and **Cancel**. |
| Mask support | Unsupported. |
| External dependencies | None. |

## Before you start

1. Verify CFA metadata. The process changes geometry in place; the change is one
   history step you can undo.
2. Prefer a factor that divides each half-resolution CFA plane: source width and
   height should be divisible by `2 × factor` for the displayed `W/factor` size
   to equal the actual downsampled result without discarded edges.
3. Check disk/RAM independently before large Upsample factors. A factor `f`
   multiplies pixel count by `f²`; readouts are capped by 100,000-pixel spin-box
   limits and can under-report larger computed dimensions.
4. Plan to plate-solve again after resampling if astrometry matters: the result
   drops the source's solution, because the four filters' samples sit at
   different places within each block, which one solution cannot describe.

## Procedure

1. Open **Process > Geometry > CFA Integer Resample** and verify **Target Image**.
2. Choose **Downsample** or **Upsample** and set **Resample Factor**.
3. For Downsample, choose Average, Median, Maximum, or Minimum.
4. Compare **Original Px**, **Target Px**, **Current Size**, and **New Size**.
   Calculate the exact result from the formula below when dimensions are
   odd/not divisible or the readout reaches 100,000.
5. Click **OK**. Sequence mode shows a progress window; still mode runs without
   one.
6. Confirm actual dimensions, CFA pattern and history before Debayer.

## Complete parameter and action reference

| UI label/action | Type and unit | Runtime default | Persisted-default exception | Valid values, step, or constraints | Effect of increasing, decreasing, or selecting | Dependencies and enable/visibility rules | Quality, artifact, runtime, memory, or disk trade-off | Scope: preview/apply, channel, mask, still/sequence | Persistence |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Downsample | Radio button | On | None | Mutually exclusive with Upsample | Reduces each filter's lattice by the factor using the selected statistic. | Enables Downsample Mode. | Reduces resolution/data; Average improves random-noise SNR, Median rejects outliers, extrema bias output. | Execute; still/sequence; no mask | Window lifetime; target change/Reset restores On. |
| Upsample | Radio button | Off | None | Mutually exclusive with Downsample | Replicates each sample within its filter's lattice by the factor on both axes. | Disables Downsample Mode. | Pixel count/memory/disk grow by `f²`; no new detail is created. | Execute; still/sequence | Window lifetime; target change/Reset restores Off. |
| Resample Factor | Integer multiplier/divisor | `2` | None | `2`–`100`; step `1`; a reduction additionally needs `2 × factor` no larger than the even width and height | Larger factors reduce/enlarge more strongly. Every whole factor is accepted and keeps the arrangement's phase, as the tooltip says. | Both directions. | Downsample discards incomplete blocks; Upsample cost grows quadratically and can exceed readout limits. | Execute | Window lifetime; target change/Reset restores `2`. |
| Downsample Mode | Selector | **Average** | None | **Average**, **Median**, **Maximum**, **Minimum** | Chooses the statistic over each `f×f` block of one filter's samples. | Enabled only for Downsample. | Average preserves mean; Median resists outliers; Maximum/Minimum select extremes and bias brightness/noise. Median is costlier. | Downsample only | Window lifetime; target change/Reset restores Average. |
| Original Px > Width | Read-only pixels | Target width | Live | `1`–`100000` display range | Shows current source width, clamped by widget maximum. | Refreshes on target/image update. | Can under-report sources wider than 100,000. | Readout only | Live. |
| Original Px > Height | Read-only pixels | Target height | Live | `1`–`100000` display range | Shows current source height. | Same. | Same cap. | Readout only | Live. |
| Target Px > Width | Read-only pixels | `floor(W/2)` for default UI formula `W/2` | Live | `1`–`100000` display range | UI displays integer `W/f` or `W×f`. The actual size follows the formula below. | Recalculates with direction/factor. | May differ for odd/non-divisible dimensions or clamp at 100,000. | Readout only | Live. |
| Target Px > Height | Read-only pixels | `floor(H/2)` for default | Live | `1`–`100000` display range | UI displays `H/f` or `H×f`. | Same. | Same discrepancy/cap. | Readout only | Live. |
| Current Size | Read-only estimate | Current image/sequence bytes in MB/GB | Live | Two decimals; uses current frame bytes × frame count | Estimates uncompressed image payload, not container overhead. | Sequence count included. | Does not represent working copies or compression. | Readout only | Live. |
| New Size | Read-only estimate | Computed target width×height×sample bytes×frames | Live | Two decimals | Estimates final uncompressed payload from displayed Target Px. | Recalculates with settings. | Can under-report when dimensions clamp and omits working memory. | Readout only | Live. |
| Target Image | CFA mono image/sequence selector | Launching view | None | Views shown as a one-channel mosaic | Changes source and resets direction/factor/mode. | Stays enabled during execution; a change does not affect the run already started. | Sequence length drives disk/time. | Execute | Current window only. |
| OK | Button | Enabled at factor 2 when displayed target differs | None | Valid two-by-two mosaic target | Checks the target, reads the dialog and resamples on a worker as an edit of the still image, or replaces the sequence; records one history step on success. | Disabled while running and when the displayed target equals the current size. | Can allocate very large outputs; still work has no progress/cancel window. | Execute | Window closes after success. |
| Reset | Button | — | None | Click action | Refreshes source/readouts and restores Downsample, factor 2, Average. | Disabled during execution. | Does not undo an already applied resample; use Image History. | Process window | No preset. |
| Close | Button | — | None | Click action | Closes without executing pending settings. | Disabled during execution. A window closed during a run stays hidden until it lands. | No change. | Process window | No persistence. |

## How the algorithm works

### Conceptual pipeline

1. Check that the target is a one-channel two-by-two mosaic.
2. If the width or height is odd, crop the last column and/or row first; the
   crop starts at the top-left pixel, so the phase is unchanged.
3. Run the engine's filter-aware integer plan: Downsample each filter's samples
   independently with the selected `f×f` statistic, or Upsample by replicating
   each sample within its filter's lattice.
4. Publish the still image or replace the sequence, as one history step.

### Model and equations

Let source dimensions be `W×H`, factor `f`, and per-filter lattice dimensions
`w=floor(W/2)`, `h=floor(H/2)`.

Actual Downsample output dimensions are

\[
W_d=2\left\lfloor\frac{w}{f}\right\rfloor,\qquad
H_d=2\left\lfloor\frac{h}{f}\right\rfloor.
\]

Actual Upsample dimensions are

\[
W_u=2wf,\qquad H_u=2hf.
\]

Thus an odd source edge is always lost, and Downsample also drops incomplete
`f×f` blocks of each filter's lattice. The UI's simpler `floor(W/f)` and `W×f`
readouts agree only for compatible even/divisible dimensions.

For Downsample Average, each output sample of one filter position is

\[
P'(x,y)=\frac{1}{f^2}\sum_{j=0}^{f-1}\sum_{i=0}^{f-1}P(fx+i,fy+j),
\]

where `P` contains only one CFA position. The mean is accumulated in 64-bit
float. Median takes the middle value, averaging the two middle values for an even
count; Maximum and Minimum take the largest and smallest valid sample. An integer
average or median is rounded to the nearest code, ties to even.

Upsample sets `P'(fx+i,fy+j)=P(x,y)` for all `i,j` in `0…f-1`. Each original
2×2 Bayer cell becomes a `2f×2f` region whose four sublattices retain their
respective original values. This is not ordinary whole-mosaic nearest neighbor.

### Boundary and failure behavior

- The target is checked when **OK** is pressed. An image that states no filter
  array is refused with `the image states no colour filter array`, a colour
  image with `a mosaic has one channel`, and an X-Trans mosaic as not a
  two-by-two arrangement, each under **Error running integer resample**.
- A reduction whose `2 × factor` exceeds the even width or height is refused
  when the run starts with `a CFA reduction factor leaves at least one complete
  block in every phase`. The UI does not preflight this.
- Size readouts clamp to 100,000 pixels and can diverge from actual output. The
  engine's memory budget refuses an allocation it cannot make, with nothing
  changed.
- A failure is reported in a message titled **Error running integer resample**
  that begins `An error occurred when executing the integer resample process:`
  followed by the reason, and in the log; the image and its history are left as
  they were. If Image History cannot keep the image the resample replaces, the
  history says so once, in a message titled **Image History**, this window says
  nothing more, and nothing changes.
- A sequence another run holds shows the busy warning. **Cancel** on a
  sequence's progress window stops the run quietly and leaves nothing in the
  temporary directory.

## Parameter interactions and starting points

| Combination | Conservative starting point | Too weak looks like | Too strong looks like | Adjustment order |
| --- | --- | --- | --- | --- |
| Factor + divisibility | Factor 2 with `W,H` divisible by 4 | Desired size reduction/enlargement not reached | Edge loss or huge allocation | Check `2f` divisibility -> exact formula -> memory -> execute |
| Downsample Mode + data goal | Average for SNR/mean; Median only for outliers | Noise/outliers remain | Max/Min biases brightness; Median removes legitimate extremes | Choose statistic by goal -> small test -> compare values |
| Upsample + later Debayer | Lowest factor required | Display remains coarse | Large replicated blocks waste resources without detail | Debayer native baseline -> choose required scale -> upsample once |
| Metadata + geometry | Solve again after resampling | No solution is available on the result | Assuming the original solution still applies | Resample -> plate-solve |

## Inputs, outputs, and metadata

- Still execution changes the selected view as one edit, preserves sample type
  (any of the eight AfterNight stores), Bayer pattern and phase, changes
  dimensions, and records the history step **CFA Integer Resample** with the
  description `Binning factor: <f>X - New size: <w>x<h> - Binning method:
  <mode>` or `Resample factor: <f>X - New size: <w>x<h>`. The size in that text
  is the dialog's **Target Px** readout.
- Sequence execution reads every frame of the sequence as recorded, whatever the
  player has selected, writes the resampled frames into a new sequence of the
  same container in AfterNight's temporary directory, publishes it only when
  every frame is written, shows it in the view, and records one history step.
- The result keeps the acquisition record and meaning and, among the header
  cards, the frame's observing context - its optics, site, pointing, camera
  settings, filter, object, frame type and date - and the light it holds, its
  `EXPTIME`, `TOTEXP`, `LIVETIME`, `STACKCNT` and `NCOMBINE` cards as it
  states them, which [Crop](../geometry/crop.md#inputs-outputs-and-metadata)
  lists. Its `XPIXSZ` and `YPIXSZ` are multiplied by the factor for Downsample
  and divided by it for Upsample - the exact factor, whatever edges the
  resample drops - so the plate scale the optics imply is still the result's.
  It drops the other header cards and the plate solution, and withdraws any
  output decision; its lineage names the engine's geometry operation and
  **CFA Integer Resample**. A sample every output was written from keeps the
  source's validity rule; a mask moves with its samples; a sample the step
  could not write is marked invalid in a mask.
- No auxiliary image/file is produced. Save/export remains a separate action.

## Performance

The engine reads each filter's samples in place and writes one result image; a
still run holds the source and the result, plus an even-cropped copy when the
source is odd. Average, Minimum and Maximum are linear in input pixels; Median
does order-statistic work per block. Upsample output/compute/disk scale with
`f²`. A sequence writes one new sequence. The displayed New Size omits working
memory and may clamp, so use the exact formula and free-space checks for large
sequences.

## Worked example

- Evidence mode: Source-and-focused-test-derived.
- Input condition/provenance and settings: a 16-bit RGGB mosaic cropped to
  23×15, resampled with the defaults (Downsample, factor 2, Average); then a
  32-bit float grey image with no filter array.
- Expected checkpoint: the odd edges go (22×14), each lattice of 11×7 reduces to
  5×3, and the result is a 10×6 RGGB mosaic whose first red sample is the
  average of the first four red samples within half a code. The grey image is
  refused under **Error running integer resample** naming the missing filter
  array, and its revision is unchanged.
- Inspect at 100%/statistics/history: verify actual dimensions, Bayer pattern,
  block/aggregate values and history.
- Limitations: no approved CFA still/sequence was executed through this process.

## Warnings and limitations

- This operation replaces the selected view's image or sequence; undo it from
  the history if needed.
- There is no preview, mask, or channel selector.
- Odd/non-divisible dimensions lose edges and can make Target Px and the history
  text inaccurate.
- Factors too large for the lattice fail only during execution.
- Upsample adds no resolution and can require `f²` storage; readouts can clamp at
  100,000 and omit working storage.
- The plate solution is dropped; solve the result again if needed.
- Average preserves mean intensity rather than summed flux; Median/Min/Max have
  different photometric/noise biases.
- Every integer 2–100 is accepted and the plan preserves positional phase; an
  odd factor is as valid as an even one.

## Troubleshooting

| Symptom or exact error | Verified cause/validation path | Recovery | Evidence source |
| --- | --- | --- | --- |
| Target absent from the selector | View is not shown as a one-channel mosaic. | Open undebayered data with correct Bayer metadata. | Target filter |
| `the image states no colour filter array` | The target states no Bayer pattern. | Reopen with correct RAW/CFA metadata. | `checkBayerMosaic` |
| `a CFA reduction factor leaves at least one complete block in every phase` | `2 × factor` exceeds the even width or height. | Use `f ≤ floor(W/2), floor(H/2)` and preferably dimensions divisible by `2f`. | engine CFA integer plan |
| Actual size differs from Target Px | UI uses `W/f` or `W×f`; the engine crops odd edges and discards incomplete blocks. | Use the exact formulas above; crop to compatible even dimensions first. | UI vs engine plan |
| New Size looks too small at extreme factor | Target dimension spin boxes cap at 100,000 and the estimate omits working memory. | Calculate externally and reduce factor before executing. | UI ranges/estimate code |
| No astrometric solution after Apply | The geometry change drops it. | Plate-solve the result. | geometry metadata policy |
| An **Image History** error, and nothing changed | Image History could not keep the image the resample replaces; it says so once and the window says nothing more. | Free memory and run again. | `GeometryProcessCase.AnEditTheHistoryCannotKeepIsSaidOnceAndChangesNothing` |
| The busy warning | Another run holds the sequence. | Wait for it to land and run again. | `runInPlace` |
| Result looks blocky after Upsample | Values are intentionally replicated; no interpolation/detail synthesis occurs. | Debayer at native size or use an appropriate post-debayer resampler for display geometry. | engine integer plan |

## Related workflow steps

Use [Image Calibration](image-calibration.md) before geometry changes. Compare
[Split CFA](split-cfa.md) and [Merge CFA](merge-cfa.md) for the positional model,
then [Debayer](debayer.md) afterward. Use ordinary
[Integer Resample](../geometry/integer-resample.md) only after Debayer when CFA
phase no longer needs preservation.

## References and provenance

- AfterNight's image engine and the geometry support unit define all current behavior; no external resampler is invoked.
- The focused window and unit tests cover the reducers, the odd-edge crop, the
  kept arrangement and the refusals.
