# Merge CFA

## Purpose and expected result

Merge CFA interleaves four same-size grayscale positional planes into a new
single-channel Bayer mosaic. It is the inverse of **Split CFA Channels** for the
retained even rectangle when CFA0–CFA3 remain in positional order and have not
been resampled. The four inputs remain unchanged; one new still image or
sequence named **CFA MERGED** is created.

The selected Bayer pattern labels the output metadata. It does not reorder the
four selectors by color, so select positional CFA0, CFA1, CFA2, and CFA3—not
Red, Green, Green, Blue unless that happens to match the chosen pattern.

## At a glance

| Item | Current behavior |
| --- | --- |
| Menu path | **Process > Color Spaces > Merge CFA** |
| Enabled when | Always reachable as a top-level process; **OK** enables once none of the four grayscale selectors reads `<--NO IMAGE-->`. |
| Inputs | Four one-channel still images, or four one-channel sequences with equal frame count and size. |
| Outputs | One new mono CFA image/sequence at twice each input width and height, in the highest of the four sample types, stating the selected pattern. Inputs are not modified. |
| Recommended image state | Four calibrated linear positional planes produced by Split CFA Channels, with identical crop/geometry. |
| Preview support | None. |
| Sequence support | Supported when all four inputs are sequences; they are joined frame by frame into a new sequence, with progress and **Cancel**. |
| Mask support | Unsupported. |
| External dependencies | None. |

## Before you start

1. Use four positional planes from **Split CFA Channels**. Keep their CFA0–CFA3
   order; pattern names describe colors at positions but do not change placement.
2. Ensure identical width, height, crop, alignment, and frame count. Do not use
   derived Ha, Green, or OIII products as substitutes.
3. For a mosaic cropped at an odd offset before splitting, remember that the
   merged mosaic states the chosen pattern at zero phase: choose the pattern
   the planes' positions actually hold.

## Procedure

1. Open **Process > Color Spaces > Merge CFA**.
2. Choose the intended **CFA Pattern**. Use the pattern of the original mosaic,
   not the order in which colors happen to be listed.
3. Select the positional **CFA0**, **CFA1**, **CFA2**, and **CFA3** views.
4. Verify tooltips/dimensions and that all four are either stills or sequences.
5. Click **OK**. For sequences, monitor the progress window; **Cancel** stops
   the run.
6. Confirm the new **CFA MERGED** view is mono, twice the plane dimensions, and
   carries the intended CFA pattern before Debayer or CFA-aware processing.

## Complete parameter and action reference

| UI label/action | Type and unit | Runtime default | Persisted-default exception | Valid values, step, or constraints | Effect of increasing, decreasing, or selecting | Dependencies and enable/visibility rules | Quality, artifact, runtime, memory, or disk trade-off | Scope: preview/apply, channel, mask, still/sequence | Persistence |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| CFA Pattern | Selector | **RGGB** | None | **RGGB**, **BGGR**, **GBRG**, **GRBG** | Sets the arrangement the output states, at zero phase; it does not reorder CFA0–CFA3 samples. | Always enabled. | Wrong choice makes later Debayer interpret colors at wrong positions. | Output metadata; still/sequence | Window lifetime; reopening restores RGGB. |
| CFA0 | Grayscale view selector | `<--NO IMAGE-->` | None | Any currently open one-channel still/sequence | Supplies `(even x, even y)` output pixels and the output metadata. | OK needs all four nonempty. | Wrong positional plane is irreversibly interleaved into the new output. A sequence output takes CFA0's container. | Execute only | Current window selection. |
| CFA1 | Grayscale view selector | `<--NO IMAGE-->` | None | Any open one-channel view matching modality/dimensions | Supplies `(odd x, even y)` pixels. | Same modality and dimensions required at execution. | Same positional/compatibility risks. | Execute only | Current window selection. |
| CFA2 | Grayscale view selector | `<--NO IMAGE-->` | None | Any open one-channel view matching modality/dimensions | Supplies `(even x, odd y)` pixels. | Same modality and dimensions required at execution. | Same positional/compatibility risks. | Execute only | Current window selection. |
| CFA3 | Grayscale view selector | `<--NO IMAGE-->` | None | Any open one-channel view matching modality/dimensions | Supplies `(odd x, odd y)` pixels. | Same modality and dimensions required at execution. | Same positional/compatibility risks. | Execute only | Current window selection. |
| OK | Button | Disabled | None | Four populated selectors; execution additionally requires all still or all sequence, equal dimensions, and equal sequence frame counts | Reads the selections, joins the planes on a worker, opens the output beside CFA0's view, and closes on success. | Disabled until selectors are nonempty and while running. | New image doubles each dimension; a sequence output writes one new sequence file. | Execute; no preview/mask | No persistence. |
| Close | Button | — | None | Click action | Closes without output. | Disabled during execution; a window closed by other means during a run stops the run. | Inputs remain unchanged. | Process window | No persistence. |

## How the algorithm works

### Conceptual pipeline

1. The process confirms all inputs are stills or all are sequences, then checks
   equal dimensions; sequences must also have equal frame count.
2. Each plane that is not already in the highest of the four sample types is
   converted to it at corresponding full scales, to the nearest value. Sample
   types rank from 8-bit signed, 8-bit unsigned, 16-bit signed, 16-bit
   unsigned, 32-bit signed, 32-bit unsigned to 32-bit float and 64-bit float.
3. Every input pixel `(x,y)` is placed into a 2×2 output cell: CFA0 top-left,
   CFA1 top-right, CFA2 bottom-left, CFA3 bottom-right.
4. The output takes CFA0's metadata and states the selected pattern at zero
   phase. A new still/sequence view is opened; sources are untouched.

### Model and equations

For input planes `P₀…P₃` of size `W×H`, output `M` has size `2W×2H`:

\[
\begin{aligned}
M(2x,2y)&=P_0(x,y),\\
M(2x+1,2y)&=P_1(x,y),\\
M(2x,2y+1)&=P_2(x,y),\\
M(2x+1,2y+1)&=P_3(x,y).
\end{aligned}
\]

The pattern selector changes how downstream software interprets those positions:

| Pattern | CFA0 | CFA1 | CFA2 | CFA3 |
| --- | --- | --- | --- | --- |
| RGGB | Red | Green1 | Green2 | Blue |
| BGGR | Blue | Green1 | Green2 | Red |
| GRBG | Green1 | Red | Blue | Green2 |
| GBRG | Green1 | Blue | Red | Green2 |

No interpolation, normalization, averaging, clipping, or color conversion is
performed beyond the conversion to the highest sample type. Only these four
two-by-two arrangements can be stated; a four-colour or X-Trans layout cannot
be merged.

### Boundary and failure behavior

- **OK** checks nonempty selectors, consistent still/sequence modality, equal
  dimensions, and equal sequence frame count. It does not require distinct views,
  so the same plane can be selected multiple times.
- Mixed sample types are converted to the highest for stills and, frame by
  frame, for sequences.
- Each sequence frame is joined only when all four frames have one width and
  height; a frame that does not stops the run with `the four planes must have
  one width and height`.
- A plane whose valid samples are stated in a mask is refused with `an image
  whose valid samples are stated in a mask cannot be merged`.
- A refusal or failure is reported in a message titled **Error running Merge
  CFA** that begins `An error occurred when executing the Merge CFA process:`
  followed by the reason, for example `All the images must have the same size`,
  and in the log.
- **Cancel** on the sequence progress window, or closing the window, stops the
  run quietly; nothing is opened and the sequence it was writing is removed. A
  merged sequence no view comes to show - the window was closed after the join
  finished but before its result landed, or no view could be made - is removed
  from the temporary directory when the run lands, so nothing is left behind. A
  still merge shows no progress window.

## Parameter interactions and starting points

| Combination | Conservative starting point | Too weak looks like | Too strong looks like | Adjustment order |
| --- | --- | --- | --- | --- |
| Pattern + positional order | Use original pattern and Split-produced CFA0–3 names | Debayer colors/geometry are wrong | Reordering by color creates a different mosaic | Confirm positions -> confirm original pattern -> merge -> Debayer-check |
| Plane edits + lossless merge | No resampling/crop/value edits for a round trip | Intended edits absent | Different geometry or derived planes make merge non-invertible | Preserve originals -> edit copies identically -> verify dimensions/type |
| Sequence compatibility | Same split operation/source sequence for all four | Frame mismatch stops the run | Mixed types are promoted, which changes the stored codes of the lower types | Compare count -> dimensions -> type -> small test sequence |

## Inputs, outputs, and metadata

- Four `W×H` mono stills produce one `2W×2H` mono still. Four matching `N`-frame
  sequences produce one `N`-frame sequence of doubled dimensions, written in
  CFA0's sequence container in AfterNight's temporary directory and published
  only when every frame is written; a merged sequence no view comes to show is
  removed again when the run lands.
- Output is named **CFA MERGED** and opens beside CFA0's view. Its history step
  is `MergeCFA: created '<pattern>' pattern from image channels CFA0:'<a>'
  CFA1:'<b>' CFA2:'<c> CFA3:'<d>'` (`sequence channels` for sequences); the
  text has no closing quote after the CFA2 name. The source views remain
  unchanged and receive no history entry.
- Output uses the highest input sample type, any of the eight AfterNight stores.
- Output metadata comes from CFA0: its acquisition record, meaning and header
  cards are kept, the output states no plate solution, because a plane's
  solution does not describe the doubled grid, and it states the chosen
  arrangement at zero phase. CFA0's world-coordinate cards and the
  solver's `PLTSOLVR` card are dropped with the solution; plate-solve the merged
  mosaic if you need one. Metadata from CFA1–CFA3 is not reconciled.
  Its lineage names the engine's block join and the pattern.
- No file is written for still output.

## Performance

Interleaving is linear in output pixels. A still merge holds a converted copy of
each plane whose type differs plus a four-plane working image and the output,
four times a plane's pixel count. Sequence cost scales with frames and writes one
doubled-dimension sequence. Test a few frames first.

## Worked example

- Evidence mode: Focused window tests.
- Input condition/provenance and settings: a 16-bit RGGB mosaic is split into
  four planes, opened as views `cfa0` to `cfa3`, and merged under RGGB; one plane
  converted to 32-bit float is merged under BGGR; a plane of another size and a
  sequence among images are tried; two-frame plane sequences made by Split CFA
  are merged.
- Expected checkpoint: `CFA MERGED` with the history text above states RGGB; the
  mixed merge states BGGR and is 32-bit float; the mismatched size and the mixed
  modality are refused by name; both frames of the merged sequence equal the
  original mosaic exactly.
- Inspect at 100%/statistics/history: compare pixel values/geometry and verify CFA
  metadata; there is no source history step because a new view is created.
- Limitations: tests do not establish real RAW color correctness or production
  disk/runtime.

## Warnings and limitations

- Pattern selection labels metadata; it never reorders selectors.
- Use positional split planes only. Ha, Green, OIII, debayered color, aligned, or
  differently cropped images are not lossless merge inputs.
- Duplicate selections are allowed and silently duplicate a plane.
- Mixed sample types are promoted to the highest one.
- Only CFA0 metadata survives; no plate solution is stated, and no
  world-coordinate card is kept.
- There is no preview, target selector, mask, Apply, or Reset; execution uses OK.

## Troubleshooting

| Symptom or exact error | Verified cause/validation path | Recovery | Evidence source |
| --- | --- | --- | --- |
| OK remains disabled | One or more selectors reads `<--NO IMAGE-->`. | Select four open grayscale views. | `validateSelectedImages()` |
| `All the selected images must be either sequences or images` | Mixed still/sequence inputs. | Use four stills or four sequences. | Execute validation |
| `All the selected sequences must have the same number of frames` | Frame counts differ. | Recreate planes from the same source/range. | Execute validation |
| `All the images must have the same size` / `All the sequences must have the same size` | Current dimensions differ. | Apply identical crop/resample or use original split planes. | Execute validation |
| `the four planes must have one width and height` | A later sequence frame differs in size. | Recreate the plane sequences from one source. | `joinMosaicPlanes` |
| `an image whose valid samples are stated in a mask cannot be merged` | A plane carries a validity mask. | Use planes without one. | `joinMosaicPlanes` |
| Debayer colors are wrong | Wrong pattern or color-based rather than positional selector order. | Restore CFA0–3 positional order and original Bayer pattern. | Interleave/pattern source |
| Output has no astrometric solution | The merge states no solution and drops CFA0's world-coordinate cards. | Plate-solve the output. | join metadata effect; `GeometryProcessCase.SplitAndMergeCfaCarryNoWorldCoordinateCards` |
| `Missing images to assemble the CFA` | A selected view closed, or no longer shows an image, when OK was pressed. | Select four open grayscale views again. | Execute validation; `GeometryProcessCase.MergeCfaWithAPlaneMissingSaysSo` |

## Related workflow steps

Create positional inputs with [Split CFA](split-cfa.md). Use
[CFA Integer Resample](cfa-integer-resample.md) before splitting when a
CFA-preserving size change is required, and [Debayer](debayer.md) after merge to
validate pattern interpretation. For ordinary calibration, retain the original
mosaic instead of a split/merge round trip.

## References and provenance

- AfterNight's image engine defines the positional join; the window cases verify
  exact round trips for stills and sequences.
- Current UI validation/output behavior comes from AfterNight's source; no external CFA engine is used.
