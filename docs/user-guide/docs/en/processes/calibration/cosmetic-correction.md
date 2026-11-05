# Cosmetic Correction

## Purpose and expected result

Cosmetic Correction replaces isolated bright or dark outliers with a local
median, then blends that corrected result with the source by **Amount**. Use it
for residual single-pixel or very small sensor defects after calibration, not
for broad dust, amp glow, trails, or clusters of real detail. A successful Apply
keeps image geometry and sample type, updates the selected target or sequence,
and records the settings in Image History.

This standalone filter detects defects from the target itself. It is distinct
from Image Calibration's dark-frame-derived cosmetic map.

## At a glance

| Item | Current behavior |
| --- | --- |
| Menu path | **Process > Filters > Cosmetic Correction Filter** |
| Enabled when | An image or sequence view is active. |
| Inputs | One grayscale or RGB image/sequence; optional selected color/luminance/chrominance channel; optional image-view mask. |
| Outputs | Corrected target image or processed sequence with unchanged dimensions and sample type. No map or file is created. |
| Recommended image state | Calibrated linear light/master with residual isolated defects. It also accepts nonlinear images. |
| Preview support | Live full-image RT preview with standard viewer/autostretch controls and optional preview-area selection. Parameter changes schedule refresh. |
| Sequence support | Supported frame by frame through the standard sequence pipeline. |
| Mask support | Detection runs without the image-view mask; the standard framework post-blends the corrected result through an enabled/inverted mask. |
| External dependencies | None; the AfterNight engine's own impulse rejection over its order-statistic median. |
| Non-finite samples | A NaN or infinite sample is treated as missing: it is left out of every median and left where it is. |

## Before you start

1. Calibrate with a representative dark/bad-pixel workflow first. Use this
   filter only for residual isolated defects.
2. Inspect at 100% zoom. Stars, undersampled star cores, cosmic rays, and tiny
   high-contrast detail can resemble hot pixels.
3. Duplicate the image or rely on Image History. Disable an external mask while
   tuning because it does not change detection.
4. On CFA data, avoid kernels/thresholds that replace real mosaic samples. When
   possible, correct defects during calibration before Debayer.

## Procedure

1. Open the target and choose **Process > Filters > Cosmetic Correction
   Filter**. Verify **Target Image** and **Channel**.
2. Enable **Hot Pixels**, **Cold Pixels**, or both. Apply remains disabled when
   both groups are off.
3. Start with **Kernel Size** `3x3`, thresholds `0.50`, and **Amount** `1.00`.
4. Keep **Preview** on. Inspect known defects and nearby stars at 100% zoom.
5. Lower a threshold only until the intended outliers change. Increase the
   kernel only when the immediate neighborhood is itself contaminated.
6. Reduce **Amount** if full median replacement looks too abrupt.
7. Click **Apply**, then verify the history entry and inspect ordinary small
   stars/detail for collateral replacement.

## Complete parameter and action reference

| UI label/action | Type and unit | Runtime default | Persisted-default exception | Valid values, step, or constraints | Effect of increasing, decreasing, or selecting | Dependencies and enable/visibility rules | Quality, artifact, runtime, memory, or disk trade-off | Scope: preview/apply, channel, mask, still/sequence | Persistence |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Hot Pixels | Checkable group | On (Qt checkable-group default) | Reset does not change it | Off/On | Enables replacement of pixels brighter than their local median by more than the selected relative threshold. | Apply requires Hot or Cold enabled. | Can remove bright defects but may replace undersampled stars/highlights. | Selected channel(s), preview/Apply, still/sequence | Window lifetime; survives Reset/Apply; reopening restores On. |
| Hot Pixels > Threshold | Normalized relative ratio | `0.50` | None | `0.00`–`1.00`; step `0.05` | Lower detects more bright deviations; higher requires a larger difference. At `1.00` the hot test is turned off and replaces nothing. | Hot group controls whether it is used; widget itself remains enabled while idle. | Low values remove detail; high values miss defects. | Hot detection only | Window lifetime; Apply/Reset restore `0.50`. |
| Cold Pixels | Checkable group | On (Qt checkable-group default) | Reset does not change it | Off/On | Enables replacement of pixels darker than their local median by more than the selected relative threshold. | Apply requires Hot or Cold enabled. | Can remove dead pixels but may alter dark fine structure. | Selected channel(s), preview/Apply, still/sequence | Window lifetime; survives Reset/Apply; reopening restores On. |
| Cold Pixels > Threshold | Normalized relative ratio | `0.50` | None | `0.00`–`1.00`; step `0.05` | Lower detects more dark deviations; higher requires a larger difference. At `1.00` the cold test is turned off and replaces nothing. | Cold group controls whether it is used; widget itself remains enabled while idle. | Low values remove detail; high values miss defects. A pixel whose local median is exactly zero has no relative deviation and is left unchanged. | Cold detection only | Window lifetime; Apply/Reset restore `0.50`. |
| Kernel Size | Selector; square pixels | **3x3** | None | **3x3**, **5x5**, **7x7**, **9x9**, **11x11**, **13x13**, **15x15** | Larger kernels estimate the replacement from a wider neighborhood. | Always enabled while idle. | Larger kernels cost more and can replace defects with a median from unrelated structure. | Median reference for selected channel(s) | Window lifetime; Apply/Reset restore **3x3**. |
| Amount | Normalized blend factor | `1.00` | None | `0.00`–`1.00`; step `0.05` | `0` returns the source after framework blending; `1` uses full detected-pixel replacement; intermediate values interpolate source and corrected output everywhere, but unchanged pixels remain identical. | Always enabled while idle. | Lower softens correction; higher gives complete median substitution. | Post-filter modulation before mask; preview/Apply/still/sequence | Window lifetime; Apply/Reset restore `1.00`. |
| Target Image | Image/sequence selector | Launching view | None | Compatible open views | Chooses the processed view and refreshes preview state. | Disabled during execution. | Large images/sequences increase cost. | Preview and Apply | Current window only. |
| Channel | Selector | **RGB** for color; **Grayscale** and disabled for mono | None | RGB, Red, Green, Blue, CIE L*/a*/b*/c*/h*, CIE X/Y/Z, HSL H/S/L, HSV H/S/V, YCbCr luma/chroma | Filters all RGB channels or one selected component; converted color-space components are recomposed afterward. | Color targets only. | Component filtering can change color or gamut on recomposition; RGB costs up to three median passes. | Selected component only; still/sequence | Current window; target-dependent. |
| Preview | Checkbox | On | Reset does not change it | Off/On | Shows/hides live preview and gates scheduled refresh. | Apply remains available. | Disable to avoid repeated median work while tuning large kernels. | Preview only | Window lifetime; survives Reset/Apply. |
| Apply | Button | Enabled because Hot and Cold start on | None | Requires at least one group enabled and a valid target | Runs correction, Amount blend, optional mask blend, commits target/sequence, and records thresholds/kernel/amount/channel. | Disabled while executing. | Full image/sequence cost; the engine observes Cancel between groups of rows. | Apply; still/sequence | Successful Apply resets thresholds/kernel/amount, not group/Preview choices. |
| Reset | Button | — | None | Click action | Restores thresholds `0.50`, Amount `1.00`, and Kernel `3x3`. | Does not restore Hot/Cold/Preview check states. | Schedules preview if Preview remains on. | Process window | No preset. |
| Close | Button | — | None | Click action | Closes without applying pending changes. | Disabled during execution. | Pending settings are discarded. | Process window | No persistence. |

## How the algorithm works

### Conceptual pipeline

1. The engine computes a reference median image of the selected image/component
   with the chosen odd kernel, using its order-statistic filter in the image's
   stored units.
2. Hot detection compares each original pixel with its median and replaces
   qualifying bright pixels. Cold detection compares each original pixel with
   the same median and replaces qualifying dark pixels. Every pixel neither test
   names is copied across exactly.
3. The process framework blends source and filtered result by **Amount**, then
   post-blends through any enabled image mask.
4. A selected non-RGB component is converted, filtered, and recomposed; sequence
   frames repeat the same operation.

### Model and equations

Let `v` be a source pixel, `m` the local median (the center pixel is included),
and `t_h`, `t_c` the thresholds. A hot pixel is replaced when

\[
v>m \quad\text{and}\quad \frac{v-m}{|v|}>t_h.
\]

A cold pixel is replaced when

\[
v<m \quad\text{and}\quad \frac{m-v}{|m|}>t_c.
\]

A pixel of exactly zero (hot test) or with a median of exactly zero (cold test)
has no relative deviation and is left unchanged. A threshold of `1.00` turns its
test off.

The filtered value is `m`; otherwise it remains `v`. The final Amount blend is

\[
O=(1-a)I+aF,
\]

where `a` is **Amount**, `I` is the source, and `F` is the fully filtered image.
An enabled image-view mask is applied after this blend.

The median is ranked in single precision from the image's stored values and
written in the source type, so every sample type keeps its type. 32-bit integer
and 64-bit float medians are rounded to single precision.

### Boundary and failure behavior

- Edge neighborhoods are clipped to available pixels; they are not wrapped.
- Every channel/component is processed independently. Hot and Cold correction
  both compare the original pixel with the same median reference.
- A NaN or infinite sample, in an image that marks every sample valid, is
  treated as missing: it is left out of every median window and left in place.
- The engine observes Cancel between groups of rows, so a preview, still, or
  sequence frame stops promptly; it reports no progress within a frame.
- UI ranges prevent invalid even/zero kernels. Very small images with a kernel
  larger than their dimensions use clipped neighborhoods and are not a useful
  defect-detection baseline.

## Parameter interactions and starting points

| Combination | Conservative starting point | Too weak looks like | Too strong looks like | Adjustment order |
| --- | --- | --- | --- | --- |
| Hot/Cold Threshold | Enable only the needed class at `0.50` | Known defects remain | Stars/fine bright or dark detail are replaced | Inspect at 100% -> lower one threshold slowly -> check ordinary detail |
| Kernel Size + Threshold | `3x3`, then tune threshold | Contaminated immediate neighbors yield a poor median | Wide kernel imports unrelated structure and costs more | Fix threshold first -> enlarge kernel only for clustered neighbors |
| Amount + detection | Tune detection at `1.00`, then lower Amount if needed | Residual defect remains | Corrected pixel looks locally abrupt | Detection first -> Amount second |
| Channel + CFA/color | RGB/Grayscale unless a component-specific defect is proven | Defect remains in an unprocessed component | Recomposition changes color around high-contrast detail | Choose data-safe channel -> preview -> inspect color |

## Inputs, outputs, and metadata

- Still Apply updates one target and adds a **Cosmetic Correction** history step.
  Sequence Apply produces/replaces a processed sequence through the standard
  temporary-sequence transaction.
- Dimensions, channel count, sample type, geometry, WCS, header cards, and
  source metadata are preserved. No defect map, count, report, or file is
  emitted.
- An external image mask and Amount affect only the final blend; neither changes
  which pixels meet the detector equations.

## Performance

Cost grows approximately with pixel count, channels/components, sequence length,
and kernel area. The median pass holds an additional full median image; preview
adds another rendered result. Start with `3x3`, one representative frame/crop,
and only the needed defect class. Cancel stops the work promptly, but no
progress is reported within a frame.

## Worked example

- Evidence mode: Source-and-test-derived.
- Input condition/provenance and settings: Programmatic UI coverage verifies
  thresholds `0.50`, Amount `1.00`, preview on, and the standard buttons. Engine
  source defines `3x3` median replacement with both groups initially on.
- Expected checkpoint: A synthetic pixel meeting the hot/cold equation is
  replaced by its local median at Amount `1`; unchanged pixels remain unchanged.
- Inspect at 100%/statistics/history: Check known defects and similarly sized
  stars/detail, then verify threshold/kernel/Amount/channel in history.
- Limitations: No approved sensor data was executed and no true/false
  detection rates, visual quality, or production performance were measured.

## Warnings and limitations

- Low thresholds and large kernels can erase undersampled stars and fine detail.
- This filter does not learn a stable sensor defect map from a dark frame; use
  [Image Calibration](image-calibration.md) for that workflow.
- On mosaiced CFA data, ordinary square-neighborhood medians do not preserve CFA
  color-plane geometry. Prefer calibration-stage correction before Debayer.
- Image masks and Amount do not affect detection, only the final blend.
- No defect count/map is exposed, and no progress is reported within a frame.
- NaN and infinity are treated as missing and stay in the result.
- Reset/Apply do not restore the Hot, Cold, or Preview check states.

## Troubleshooting

| Symptom or exact error | Verified cause/validation path | Recovery | Evidence source |
| --- | --- | --- | --- |
| Apply is disabled | Both Hot Pixels and Cold Pixels are off. | Enable at least one group. | `settingsUpdatedImpl()` |
| Known defect remains | Threshold is too high, the local median is similar, Amount is below 1, or the selected Channel excludes it. | Verify channel/group, use `3x3`, lower only the relevant threshold, then check Amount. | Detector equations; framework modulation |
| Small stars/details disappear | Threshold is too low or kernel too wide. | Raise threshold, return to `3x3`, or protect the area with a final-blend mask after validating detection. | Median/detector source |
| Preview changes only inside a mask | The mask is post-blending the corrected result. | Disable the mask while tuning; re-enable it for final spatial limitation. | `ImageRTPreviewControl.cpp` |
| NaN or infinity survives correction | Non-finite samples are treated as missing and left in place. | Repair them first if the result must be finite. | Missing-sample rule |
| Color shifts around corrected pixels | A derived color component was filtered/recomposed or RGB channels corrected different pixels. | Use RGB/Grayscale where appropriate and inspect channels before Apply. | Channel preprocessing/recomposition source |

## Related workflow steps

Use [Image Calibration](image-calibration.md) first when dark-frame evidence is
available. On CFA data, keep the correction before [Debayer](debayer.md), then
continue with [Image Registration](../registration-alignment/image-registration.md)
and stacking. For broad impulsive noise rather than isolated defects, compare
[Median Filter](../noise-reduction/median-filter.md) cautiously.

## References and provenance

- The standalone process uses the AfterNight engine's impulse rejection over its
  order-statistic median; it does not invoke an external tool.
- AfterNight's source and focused tests are the authority for current
  labels, defaults, equations, channel behavior, and limitations.
