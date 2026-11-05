# Banding Reduction

## Purpose and expected result

Banding Reduction measures the median of every row and adds one constant
correction to every sample in that row. With **Vertical Banding** selected, it
measures and corrects every column instead, which makes the correction constant
down each column.

The expected result is a same-size, same-type still or replacement sequence in
which row- or column-wide additive offsets are closer to one another. This is
not a frequency-domain band-stop filter, surface fit, or smoothed trend model.
It can remove genuine large-scale signal whose line medians change with
position, and it cannot fully correct bands whose value varies along a line.

## At a glance

| Item | Current behavior |
| --- | --- |
| Menu path | **Process > Filters > Banding Reduction Filter** |
| Enabled when | An image or sequence is active, the same process is not already attached to it, and no attached process holds exclusive access |
| Inputs | One grayscale or RGB still or sequence; a still CFA mosaic is treated as one grayscale plane unless debayered first; optional active mask must match target geometry |
| Outputs | Destructively replaces a still or, after successful temporary processing, a sequence; **Take snapshot** creates an auxiliary still; dimensions, channel count, and source pixel type are retained; no file is written directly |
| Correction model | Per-row or per-column median offsets, measured for each channel on its own; corrections are nonnegative additions scaled by Amount, held to the type's range for an integer image |
| Highlight option | Excludes high samples from the line-median estimator; it does **not** protect highlight pixels from the resulting line-wide correction |
| Recommended image state | Finite, normalized linear data after calibration, before stretching; first verify that the apparent bands are fixed row/column offsets rather than gradients, walking noise, or residual calibration structure |
| Preview | Asynchronous full-image or movable-area preview, original/result switching, snapshot, display-channel selection, and display-only autostretch |
| Sequence support | Yes; one captured parameter/channel/mask state is applied frame by frame to temporary storage before replacement |
| Mask support | Yes; an active mask blends the corrected result with the original and is the only current way to protect selected output pixels |
| External dependency | None; the AfterNight engine's own banding suppression and exact median, with no separate user installation or network access |

## Before you start

- Use this process only for bands aligned with image rows or columns. Diagonal,
  curved, periodic-within-line, or frame-to-frame moving patterns need a
  different model.
- Know where NaN and infinity samples are, such as the empty borders of a
  registered frame. They are treated as missing: left out of every median and
  left where they are.
- Inspect a duplicate under one display stretch. Include blank background,
  nebulosity, bright stars, and image edges in the Preview area.
- Leave **Protect From Highlights** off for an initial comparison. If bright
  objects bias line medians, enable it with a positive Sigma and verify that the
  line-wide correction does not clip those same bright pixels.
- Do not assume the filter preserves a broad gradient. Any real change in line
  median is interpreted as banding.

## Procedure

1. Select the target and open **Process > Filters > Banding
   Reduction Filter**. Confirm **Target Image**, choose **RGB** or a deliberate
   component, and leave **Preview** selected.
2. Leave **Vertical Banding** clear for horizontal stripes (one correction per
   row). Select it for vertical stripes (one correction per column).
3. Start at **Amount 1.00**. Toggle **Switch image** and inspect background and
   large real structures at **Zoom 100%**.
4. If bright stars or cores distort line medians, select **Protect From
   Highlights** and start with **Sigma 1.00**. Increasing Sigma lowers the
   rejection threshold and excludes more high samples; it is inverse to the
   usual “larger sigma protects fewer samples” expectation.
5. Reduce Amount when the correction is useful but too strong. Values above
   `1` extrapolate past equalized line medians and can reverse/overcorrect the
   band pattern.
6. Check the full frame, mask, target, Channel, and orientation before **Apply**.
   For a sequence, Cancel stops the filter between groups of lines.

## Complete parameter and action reference

| UI label/action | Type and unit | Runtime default | Persisted-default exception | Valid values, step, or constraints | Effect of increasing, decreasing, or selecting | Dependencies and enable/visibility rules | Quality, artifact, runtime, memory, or disk trade-off | Scope: preview/apply, channel, mask, still/sequence | Persistence |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Amount | Double value plus slider, correction multiplier | `1.00` | None; process settings are not persisted across dialog instances | `0.00..4.00`; spin step `0.05`; slider resolution `0.001` | Multiplies each nonnegative line correction inside the engine; `0` adds nothing but still measures every line | Eligible target; no additional enable/visibility dependency | `1` equalizes unprotected line medians in every processed channel; values above `1` overshoot and can invert bands; Preview/Apply, selected Channel, still/sequence; Reset/Apply return `1.00` | Preview and Apply; selected Channel and mask; still or sequence | Dialog-local; Reset and successful Apply restore the documented runtime default |
| Protect From Highlights | Checkbox | Off | None; process settings are not persisted across dialog instances | Checked/unchecked | When checked, line samples at or above `global_median + 1.4826 × global_MAD / Sigma` are excluded before the line median | Eligible target; no additional enable/visibility dependency | Estimator-only protection: every pixel in the line still receives the correction; enables Sigma while idle; Reset/Apply clear it | Preview and Apply; selected Channel and mask; still or sequence | Dialog-local; Reset and successful Apply restore the documented runtime default |
| Sigma | Double value plus slider, inverse rejection divisor | `1.00` | None; process settings are not persisted across dialog instances | `0.00..5.00`; spin step `0.05`; slider resolution `0.001` | Larger values lower the high-sample threshold toward the global median and reject more samples; smaller positive values raise it | Enabled only while Protect From Highlights is checked and the process is idle | Enabled only with Protect From Highlights; `0` turns the protection off, so no sample is rejected; Reset/Apply return `1.00` and disable it | Preview and Apply; selected Channel and mask; still or sequence | Dialog-local; Reset and successful Apply restore the documented runtime default |
| Vertical Banding | Checkbox | Off | None; process settings are not persisted across dialog instances | Checked/unchecked | Off computes one correction per row for horizontal bands; on computes one correction per column | Eligible target; no additional enable/visibility dependency | Changes which real gradients are flattened; Reset/Apply turn it off | Preview and Apply; selected Channel and mask; still or sequence | Dialog-local; Reset and successful Apply restore the documented runtime default |
| Target Image | Image selector | Attached/active target | None; process settings are not persisted across dialog instances | Open eligible image or sequence | Changes source, Channel list, Preview, active mask, and Apply destination | At least one eligible image or sequence; launch requires an active image | Wrong target is destructively replaced after success; Reset retains target | Preview and Apply destination; still or sequence | Dialog-local; Reset retains the current selection/state |
| Channel | Combo box | **RGB** for RGB; disabled **Grayscale** for mono | None; process settings are not persisted across dialog instances | **RGB**, **Red**, **Green**, **Blue**, **CIE L* (Lightness)**, **CIE a***, **CIE b***, **CIE c***, **CIE h***, **CIE X**, **CIE Y (Luminance)**, **CIE Z**, **HSL Hue**, **HSL Saturation**, **HSL Lightness**, **HSV Hue**, **HSV Saturation**, **HSV Value**, **YCbCr Luma**, **YCbCr Blue Chroma**, **YCbCr Red Chroma** | RGB corrects each stored channel by its own line statistics and its own baseline; a component filters one plane and reconstructs/copies the others | Target channel count; mono targets force and disable Grayscale | Per-channel corrections can still lift channels by different amounts where their bands differ; a single component corrects one plane only; Reset retains selection | Preview and Apply; selected component; still or sequence | Dialog-local; Reset retains the current selection/state |
| Preview | Checkbox | Checked | None; process settings are not persisted across dialog instances | Checked/unchecked | Enables asynchronous full-image or selected-area execution | Eligible target; work is scheduled only while checked | Runs the full engine path; selected area reduces tuning work; Reset retains state | Preview only; still or sequence source | Dialog-local; Reset retains the current selection/state |
| Preview pane | Image viewer | Current result | None; process settings are not persisted across dialog instances | Pan/scroll; toolbar actions below | Shows mask-aware result without modifying target | Preview execution or original display buffer | Holds source/result/display and filter work buffers; Preview only | Preview/display only | Preview-view lifetime only |
| Fit To Window | Preview-toolbar action | Fitted initially | None; process settings are not persisted across dialog instances | Click or **Ctrl+Shift+F** | Fits whole image uniformly | Preview pane | Display only; resampling can hide residual bands; viewer lifetime | Preview/display only | Preview-view lifetime only |
| Zoom In | Preview-toolbar action | Current scale | None; process settings are not persisted across dialog instances | Click; `1.25×` relative zoom | Enlarges around viewport center | Preview pane | Display only; viewer lifetime | Preview/display only | Preview-view lifetime only |
| Zoom Out | Preview-toolbar action | Current scale | None; process settings are not persisted across dialog instances | Click; `0.8×` relative zoom | Reduces around viewport center | Preview pane | Display only; viewer lifetime | Preview/display only | Preview-view lifetime only |
| Zoom 100% | Preview-toolbar action | Current scale | None; process settings are not persisted across dialog instances | Click; absolute scale `1.0` | Shows one-pixel viewer scale | Preview pane | Preferred for residual bands, clipping, and line discontinuities; display only | Preview/display only | Preview-view lifetime only |
| View Channel | Preview-toolbar combo box | **RGB** for RGB; disabled **Grayscale** for mono | None; process settings are not persisted across dialog instances | Stored RGB plus CIE Lab/LCh/XYZ, HSL, HSV, HSI, and YCbCr display components | Selects only displayed component | Preview pane and target channel count | Separate from process Channel; useful for finding channel-specific residuals; display only | Preview/display only | Preview-view lifetime only |
| Display Mode | Preview-toolbar combo box | Inherits target **Linear** or **Autostretch** | None; process settings are not persisted across dialog instances | **Linear**, **Autostretch** | Changes display only; Autostretch uses cached full-target source statistics | Preview pane | Aggressive stretch can reveal small residual offsets but does not change pixels | Preview/display only | Preview-view lifetime only |
| Autostretch Settings | Preview-toolbar dialog action | Disabled in Linear mode; inherits target-view settings | None; process settings are not persisted across dialog instances | Opens display stretch controls; **Apply Stretch** is hidden | Tunes preview display only | Preview pane; enabled only when Display Mode is Autostretch | Viewer-scoped, not Banding Reduction parameters | Preview/display only | Preview-view lifetime only |
| Switch image | Preview-toolbar action | Corrected result shown | None; process settings are not persisted across dialog instances | Click; **Space**; right-click; **Ctrl+Shift+O** | Toggles result/original | Preview pane | Comparison only; no recomputation; preview lifetime | Preview/display only | Preview-view lifetime only |
| Preview area | Preview-toolbar checkbox | Checked with initial `700×700` area | None; process settings are not persisted across dialog instances | Checked/unchecked; move/resize overlay | Checked processes selected area and composites it over original preview; unchecked processes full image | Checked Preview and eligible target | A cropped area estimates medians from the crop, so Preview can differ from full-frame Apply even with identical settings; full Apply always uses full target | Preview/display only | Preview-view lifetime only |
| Take snapshot | Preview-toolbar action | No snapshot | None; process settings are not persisted across dialog instances | Click or **Ctrl+Shift+K** | Re-executes settings and opens `<target> - Banding Reduction - SNAPSHOT` | Checked Preview and eligible target | Adds complete execution and auxiliary still; selected-area statistics/mask honored; target unchanged | Auxiliary still from preview scope; selected Channel and mask | Preview-view lifetime only |
| Apply | Button/action | Enabled while idle, including Amount/Sigma `0` | None; process settings are not persisted across dialog instances | One action | Captures Amount, highlight option, Sigma, orientation, Channel, and mask, then processes full still or every sequence frame | Eligible target and idle process | Controls lock during commit; destructive replacement after success; records history; successful Apply resets all four process settings | Full-target Apply; selected Channel and mask; still or sequence | Successful Apply resets all four process settings |
| Reset | Button/action | Enabled while idle | None; process settings are not persisted across dialog instances | One action | Restores Amount/Sigma `1`, clears both checkboxes, and disables Sigma | Idle process | Does not reset Target, Channel, Preview, mask, or preview toolbar; checked Preview schedules execution | Open process dialog only | Immediate dialog-local reset; no cross-dialog persistence |
| Close | Button/action | Enabled while idle | None; process settings are not persisted across dialog instances | One action | Closes without applying current settings | Idle process | Disabled during Apply; requests preview cancellation, which reaches the engine between groups of lines; no process settings persist after destruction | Open process dialog only | Unapplied process settings are discarded |
| Sequence progress / Cancel | Shared per-frame progress and Cancel | Created for sequence Apply | None; process settings are not persisted across dialog instances | Cooperative Cancel, checked between groups of lines | Applies one captured setting/channel/mask state to every frame in temporary storage | Sequence target during Apply | The filter reports no progress within a frame, but a frame stops promptly on Cancel; Cancel/error removes temporary output, success replaces sequence and records one history step | Sequence Apply only | Execution snapshot only; no cross-dialog persistence |

## How the algorithm works

AfterNight works on each processed channel on its own, in the channel's stored
units. For channel `c` it computes the channel median `b_c` - and, with
highlight protection, the median absolute deviation `MAD_c` - then the median
`m_cj` of every row `j`. It forms

\[
d_{cj}=b_c-m_{cj},\qquad
d_{c,min}=\min_{j}(d_{cj}),\qquad
q_{cj}=A(d_{cj}-d_{c,min}),
\]

where `A` is Amount, and adds the constant `q_cj` to every sample in that row.
Because `d_c,min` is taken within each channel, every correction is
nonnegative and one channel's banding does not change another channel's
correction.

For each channel, `d_c,min=b_c-max_j(m_cj)`, so

\[
q_j=A(\max_k m_k-m_j).
\]

At Amount `1`, all row medians therefore move to the brightest original row
median, not to the global median. The algorithm never subtracts a positive
offset from a bright band. Amount between `0` and `1` moves darker line medians
toward the brightest; Amount above `1` overshoots them.

### Highlight-estimator rejection

With **Protect From Highlights**, samples at or above

\[
t_c=b_c+\frac{1.4826\,MAD_c}{\text{Sigma}}
\]

are removed from each row before its median is calculated. Increasing Sigma
lowers `t_c` and removes more of the upper tail. This option only changes
`m_cj`. The resulting correction remains constant across the entire line and
is added to excluded bright pixels too.

A row from which protection removes every sample has no median: it takes no
part in `d_c,min` and is left unchanged. Sigma `0` turns the protection off, so
every finite sample is measured.

A NaN or infinite sample in an image that marks every sample valid is treated as
missing: it is left out of the channel and row medians, and it is not changed.
An image that carries its own validity rule or mask is read under that rule.

### Orientation, color, and clipping

With Vertical Banding off, each row receives one constant. With it on, each
column is measured and receives one constant; no rotation is involved.

In RGB, every channel is measured and corrected by its own figures: its own
median, its own row medians, and its own baseline `d_c,min`. At Amount `1` each
channel's rows move to that channel's brightest row median. A channel that is
banded more than another is therefore lifted more, which is what equalizes it.
Processing a single luminance or color component corrects that one plane, with
the usual conversion/reconstruction cost. A derived component's reconstruction
saturates an integer image at its type's range and keeps a floating image's
values outside `[0, 1]`.

The scaled correction is added in single precision and written back in the
source type: an integer image is rounded and held to its type's range, a float
image is not clipped, and a 64-bit float image is processed at single
precision. The active mask is blended after this complete operation.

## Parameter interactions and starting points

- **Orientation × real gradient** determines what gets flattened. A sky or
  illumination gradient across rows/columns is indistinguishable from banding
  to this model.
- **Amount × line-median spread** sets correction magnitude. Amount `1` reaches
  the brightest line median in each channel; higher values overshoot and, in an
  integer image, can clip highlights at the type's maximum.
- **Protect From Highlights × Sigma** is inverse-threshold control. Increasing
  Sigma rejects more high samples; it does not feather or protect those pixels.
- **Preview area × full Apply** changes the estimator population. Crop medians
  need not match full-frame medians, so always check a full-image preview.
- **RGB × per-channel correction** equalizes each channel's rows separately.
  Prefer a deliberate luminance/component path if line offsets are achromatic
  and you want only one plane corrected.
- **Mask × highlight protection** serves different purposes. Protection changes
  line estimates; a mask controls which output pixels receive the final result.

## Inputs, outputs, and metadata

- Output dimensions, channel count, and sample type are retained. Every type is
  processed in its stored units in a single-precision working plane.
- Integer output is rounded and held to the type's range; float output is not
  clipped; double input loses precision in the single-precision working path.
- The result keeps the source image's header cards and metadata. Banding
  Reduction does not update linear/nonlinear processing-state provenance.
- Image History records Amount, conditionally `Inv Sigma`, Vertical Banding, and,
  for a colour image, ` in channel: <channel>`. It does not record the Protect
  From Highlights label separately, although presence of `Inv Sigma` implies it.
- A still CFA mosaic is processed as one plane. Sequence preprocessing may
  auto-debayer configured CFA frames before replacement.
- Sequence frames use one captured parameter/channel/mask state. Success replaces
  the original sequence; Cancel/error removes temporary output.
- **Take snapshot** creates a still from current preview scope without changing
  the target or baking display autostretch into pixels.

## Performance

For width `W`, height `H`, and processed channels `C`, the filter computes
exact channel statistics and a median for each line, with typical work
proportional to `W × H × C`. Lines are measured in parallel, a group at a time,
each in a line buffer reused within the group, and the correction is added in
parallel by rows on the threads the operation is granted.

Memory includes single-precision working planes for a channel, the line
buffers, one offset per line, the destination, and preview/component/mask
buffers. Sequences also need temporary disk roughly comparable to an output
sequence. No approved banded dataset has been run and no production time or
peak memory has been measured.

## Worked example

These normalized scalar examples follow current source equations. They were not
run through an approved astronomical image and make no visual-quality claim.

```text
row medians: 0.10, 0.12, 0.08
brightest row median: 0.12
raw nonnegative corrections: 0.02, 0.00, 0.04

Amount 1.00 -> corrected medians: 0.12, 0.12, 0.12
Amount 0.50 -> corrected medians: 0.11, 0.12, 0.10
Amount 2.00 -> corrected medians: 0.14, 0.12, 0.16 (overshoot)
```

For global median `0.20` and raw MAD `0.01`, the normalized robust scale is
`1.4826 × 0.01 = 0.014826`. Sigma `1` rejects samples at or above `0.214826`;
Sigma `2` lowers the threshold to `0.207413` and rejects more samples.

## Warnings and limitations

- The model corrects one constant per row/column. It does not estimate a smooth
  trend, periodic spectrum, diagonal orientation, or within-line variation.
- It raises darker line medians toward the brightest; it never subtracts bright
  bands. Amount above `1` overshoots and can reverse the pattern.
- Real gradients, nebula, galaxy light, vignetting, amp glow, and large stars can
  change line medians and be flattened or imprinted into the correction.
- Protect From Highlights is estimator rejection, not output-pixel protection.
  Bright samples still receive the line correction and may clip; use a mask for
  actual spatial protection.
- Sigma is an inverse divisor, not a Gaussian smoothing radius or standard
  deviation. Increasing it rejects more, and `0` has degenerate behavior.
- A line whose every sample protection rejects is left uncorrected, which can
  leave an isolated band on flat/degenerate data with a high Sigma.
- RGB channels are corrected by their own figures, so a channel banded more
  than another is lifted more and the color balance can shift.
- NaN and infinity are treated as missing: left out of every median and left in
  place.
- Float samples are not clipped; integer samples are held to their type's range.
  Amount `0` still measures every line.
- Successful Apply resets all four process settings but retains Target, Channel,
  Preview, mask, and preview-toolbar state.

## Troubleshooting

| Symptom | Likely cause | Check or correction | Evidence |
| --- | --- | --- | --- |
| Horizontal bands remain | Vertical Banding is selected or offsets vary within rows | Clear Vertical Banding; if residual varies along a row, use a different model | Row-median path |
| Vertical bands remain | Default row orientation is active | Select Vertical Banding | Column-median path |
| Gradient or nebula is flattened | Genuine line-median change is treated as banding | Reduce Amount, use a mask, or do not use this process on that target | Per-line constant model |
| Highlights still change with protection | Option excludes estimator samples only | Protect output pixels with a mask and reduce Amount | Rejected-median then line-wide add |
| More highlights are rejected when Sigma increases | Threshold divides by Sigma | Lower Sigma to raise threshold; keep it above zero | `median + 1.4826 MAD / Sigma` |
| A band remains on flat data with protection | Protection rejected every sample of that line, so it was left uncorrected | Lower Sigma or disable protection; inspect full-frame Preview | Unmeasured-line rule |
| Color balance shifts | Each channel's rows are lifted by that channel's own banding | Try CIE Y/Lightness or process channels deliberately; compare chroma | Per-channel correction |
| Preview differs from Apply | Selected-area medians differ from full-image medians | Uncheck Preview area and recompute before Apply | Scope-dependent estimator |
| Amount `0` remains slow | Line estimation still runs | Disable Preview/close process when no effect is wanted | Engine order |
| NaN or infinity survives | Non-finite samples are treated as missing and left in place | Repair them first if the result must be finite | Missing-sample rule |

## References and provenance

- [GNU Scientific Library statistics reference](https://www.gnu.org/software/gsl/doc/html/statistics.html)
  defines the sorted-data median and normalized median-absolute-deviation that
  the highlight-rejection path computes; the engine computes them exactly
  itself.
- AfterNight's engine banding suppression and the shared Channel/sequence path
  are authoritative for the exact correction equation, orientation, threshold,
  per-channel baseline, missing-sample rule, cancellation, and output behavior.

## Related workflow steps

- [Cosmetic Correction](../calibration/cosmetic-correction.md) targets localized
  hot/cold defects rather than whole-line offsets.
- [Background Extraction](../background-extraction/background-extraction.md)
  models two-dimensional gradients from samples instead of forcing line medians
  to match.
- [Auto Gradient Removal](../background-extraction/auto-gradient-removal.md)
  estimates a broad two-dimensional background with structure protection.
- [Median Filter](median-filter.md) replaces local neighborhood outliers and is
  not a row/column offset estimator.
- [Process Windows](../../interface/process-windows.md), [Image Viewer](../../interface/image-viewer.md),
  [Masks](../../interface/masks.md), and [Image History](../../interface/image-history.md)
  explain the shared controls and verification workflow.
