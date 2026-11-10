---
kind: process
stage: color
menu: Process > Background Modelization > Auto Gradient Removal
summary: >-
  Estimates the large-scale background without a sample grid, then subtracts it from a linear master or divides the
  master by it, to remove gradients.
data_in: linear
data_out: linear
derived: true
---
# Auto Gradient Removal

## Purpose and expected result

Auto Gradient Removal estimates an uneven large-scale background without a
sample grid, then subtracts or divides that model from a still image. It is
intended for people correcting gradients in a calibrated, registered, linear
master before color calibration or stretching. A successful application leaves
the selected image at the same dimensions, channel count, and sample type with
a flatter background. Optional diagnostic views expose the fitted gradient and
the internally detected bright-structure protection mask.

This process cannot decide whether a broad feature is unwanted illumination or
real astronomical signal. Inspect the model before applying a strong
correction, especially around extended nebulae, galaxy halos, and mosaics.

## At a glance

| Item | Current behavior |
| --- | --- |
| Menu path | **Process > Background Modelization > Auto Gradient Removal** |
| Enabled when | A still-image view is active and no other process window is attached to it: the process needs exclusive access to its target. The menu action is disabled for a sequence view. The target selector can switch among other open still images. |
| Inputs | One grayscale or color still image in any integer or floating sample type. Integer samples are worked on as fractions of their code range. No sample grid or reference image is required. |
| Outputs | The selected target is updated only after a successful Apply. Optional `<target> - AGR Gradient` and `<target> - AGR Protection Mask` views open beside it. No file is written automatically. |
| Recommended image state | A calibrated, registered, linear master before color calibration and stretch. The process does not enforce linearity and does not record the corrected image as nonlinear. |
| Preview support | On request. The preview shows the unchanged target until **Preview** is clicked, then the corrected image, gradient model, or protection mask of the whole image. One preview run makes all three, so changing **Preview display** afterwards shows each at once. Any setting change returns it to the target. Preview-area selection is disabled. |
| Sequence support | Unsupported. A run on a sequence is refused with an error saying the process works on a still image. |
| Mask support | The model is always estimated without the image-view mask. An enabled mask, including inversion, is blended into every computed preview display and into the corrected target on Apply. The gradient and protection views created by Apply are not masked. |
| External dependencies | None for the user. The background estimate is built into AfterNight. |

## Before you start

1. Finish calibration, registration, and stacking, then remove invalid stacking
   borders. A border can be mistaken for a background feature.
2. Keep the master linear if gradient correction is part of a normal deep-sky
   workflow. Duplicate the image or confirm that Image History can restore the
   previous state.
3. If the image contains broad real signal, plan to inspect **Gradient model**
   and **Protection mask**. Structure protection detects positive residuals; it
   is not an astronomical object classifier.
4. Disable an active image mask while inspecting **Gradient model** and
   **Protection mask**. The mask never changes the fitted model, but every
   computed preview is blended with it, so a masked diagnostic display mixes the
   source with the model or mask.

## Procedure

1. Select the still image and open **Process > Background Modelization > Auto
   Gradient Removal**. Confirm the intended image under **Target image**.
2. Leave the multiscale defaults for the first pass: **Scale** `5.0`,
   **Smoothness** `1.0`, **Downsample** `4`, and **Protect bright structures**
   enabled.
3. Keep **Enable preview** selected. The preview pane shows the unchanged target
   until you click **Preview**; the button then computes the full-image result
   once and stays disabled until a setting changes. Changing a model,
   protection, or correction setting, the target, or clicking **Reset** returns
   the preview to the target and enables the button again. For consistent
   diagnostic views, keep the target's image-view mask disabled while tuning.
4. Set **Preview display** to **Gradient model**. Once a preview has been
   computed, switching the display shows the new display at once: the preview
   run made the corrected image, the model and the protection mask together,
   so nothing is computed again. The model
   should contain the unwanted broad illumination pattern, not detailed stars
   or the main outline of the target.
5. Set **Preview display** to **Protection mask** to inspect excluded positive
   structure. This choice is available only while **Protect bright structures**
   is enabled.
6. Return to **Corrected image**. If necessary, adjust one control group, click
   **Preview** again, and compare with the same display stretch.
7. Select either optional output checkbox if you want a diagnostic view after
   application, then click **Apply**. A progress dialog shows the run; its
   **Cancel** button stops it without changing anything. On success, the target
   updates, an **Auto Gradient Removal** history step is recorded, requested
   diagnostics open as separate unmasked views, and the process controls retain
   the selected values for another Apply. If the image is too small for the
   chosen **Downsample**, a warning names the factor used instead. Use **Reset**
   or reopen the process to restore defaults.
8. Inspect the corrected target and model at matched zoom/stretch. Continue to
   color calibration only when real large-scale signal has not been removed.

## Complete parameter and action reference

| UI label/action | Type and unit | Runtime default | Persisted-default exception | Valid values, step, or constraints | Effect of increasing, decreasing, or selecting | Dependencies and enable/visibility rules | Quality, artifact, runtime, memory, or disk trade-off | Scope: preview/apply, channel, mask, still/sequence | Persistence |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Scale | Decimal; percent of the downsampled image's shorter dimension | `5.0` | None | `1.0`–`10.0`; step `0.5`. A Scale whose working radius would exceed 1,024 pixels is refused, which can happen only at Downsample `1` or `2` on very large frames. | Sets the base radius. Larger values produce broader models and expand the scale used to detect protected structure; smaller values follow finer variation. | Always enabled when idle. In polynomial mode it still affects protection and final smoothing, although it does not set polynomial degree. | Too small can absorb real detail; too large can miss compact gradients. The low-pass cost does not grow with the radius. | Full image; every channel independently; preview and Apply | Window lifetime; Apply retains the value; Reset or reopening restores `5.0`. |
| Smoothness | Decimal radius multiplier | `1.0` | None | `0.0`–`3.0`; step `0.1` | `0` skips final model smoothing. Higher values apply three additional box-filter passes with radius `Scale radius × Smoothness`. | Applies to both model types after iterative fitting. | Higher values suppress model texture but can underfit faster background changes. | Full image; every channel; preview and Apply | Window lifetime; Apply retains the value; Reset or reopening restores `1.0`. |
| Downsample | Selector; linear reduction factor | `4` | None | `1`, `2`, `4`, or `8` | Higher factors estimate the model on fewer area-averaged pixels; `1` works at full resolution. The model is enlarged to the original size by bilinear interpolation. | Always enabled when idle. A frame shorter than the factor on either side is worked at the largest smaller factor that fits, and Apply warns of it. | Higher factors are faster and use less working memory but can miss smaller background changes. Factor `1` is the most expensive. | Model estimation for all channels; corrected and diagnostic outputs remain full size | Window lifetime; Apply retains the value; Reset or reopening restores `4`. |
| Simplified polynomial model | Checkbox | Off (multiscale model) | None | Off/On | On replaces the inpainted low-pass surface with a total-degree polynomial least-squares surface. | Enables **Polynomial degree**. Scale still influences protection and final smoothing. | Useful for smooth global planes/curvature; a high degree can follow real structure or become poorly constrained. | Full image; every channel; preview and Apply | Window lifetime; Apply retains the choice; Reset or reopening turns it off. |
| Polynomial degree | Integer polynomial total degree | `2` | None | `1`–`6`; step `1` | Adds terms whose powers satisfy `i + j ≤ degree`; degree 1 is planar, while higher degrees permit more curvature. | Enabled only when **Simplified polynomial model** is on. | Higher degree adds coefficients and can overfit; degree 6 has 28 terms. | Simplified model only; every channel; preview and Apply | Window lifetime; Apply retains the value; Reset or reopening restores `2`. |
| Protect bright structures | Checkbox | On | None | Off/On | Excludes detected positive residual structures from every later fit, including when the minimum-retention fallback rebuilds the accepted pixels. | Enables **Threshold**, **Amount**, protection-mask preview, and protection-mask output. Turning it off clears a selected/requested protection mask. | Helps keep bright extended signal out of the model; excessive protection leaves less background evidence, and a fit left with none is refused. | Internal fit protection; every channel; preview and Apply | Window lifetime; Apply retains the choice; Reset or reopening turns it on. |
| Threshold | Normalized sample-value difference | `0.050` | None | `0.000`–`1.000`; step `0.005` | A residual must exceed this value above the residuals' robust center to seed structure protection. Lower values protect more pixels; higher values protect fewer. | Enabled only with **Protect bright structures**. | Too low can reject ordinary background variation; too high can allow bright nebulosity or halos into the model. | Internal protection per channel; preview and Apply | Window lifetime; Apply retains the value; Reset or reopening restores `0.050`. |
| Amount | Normalized protection expansion strength | `0.50` | None | `0.00`–`1.00`; step `0.05` | Higher values use a larger growth radius and a lower cutoff, producing broader protected regions. | Enabled only with **Protect bright structures**. | Too little can leave bright outskirts in the fit; too much can leave too little background evidence. | Internal protection per channel; preview and Apply | Window lifetime; Apply retains the value; Reset or reopening restores `0.50`. |
| Mode | Selector | **Subtraction** | None | **Subtraction** or **Division** | Subtraction removes an additive model while restoring its median level. Division removes a multiplicative model and rescales by its median. | Always enabled when idle. | Division can amplify noise where the model is small; either mode clips values outside `[0,1]`. | Corrected image; every channel; preview and Apply | Window lifetime; Apply retains the choice; Reset or reopening restores **Subtraction**. |
| Preview display | Selector | **Corrected image** | None | **Corrected image**, **Gradient model**, or **Protection mask** | Chooses what a computed preview shows. Once a preview has been computed, switching displays shows the new display at once, without computing again. It does not change Apply output. | Requires **Enable preview**. **Protection mask** is disabled when structure protection is off. Every computed display is blended with an active image-view mask. | Diagnostic displays help find overfitting without extra output views; disable the image-view mask for consistent comparisons. A computed preview holds every display's image, up to three full-size images, until a setting, the target or its mask changes or the preview is turned off. | Preview only; all channels as produced | Window lifetime; Apply retains the choice; Reset or reopening restores **Corrected image**. |
| Preview | Button | Ready while no preview has been computed for the current settings | None | Click action | Runs the Apply's computation over the whole image once, making the corrected image, the gradient model and, with protection, the protection mask, and shows the selected display. Until it is clicked, and after any setting or target change, the preview shows the target. | Requires **Enable preview**, idle controls, and no preview already computed for the current settings. | Full-image estimation can be expensive; only the latest requested preview is shown. | Preview only; still image only | No persistence. |
| Create gradient model image | Checkbox | Off | None | Off/On | Requests a separate unmasked gradient-model view after successful Apply. | Does not control preview availability. | Adds a full-size image in the target's sample type, drawn from the estimate the correction uses. | Apply only; all channels; still image only | Window lifetime; Apply retains the choice; Reset or reopening turns it off. |
| Create protection mask image | Checkbox | Off | None | Off/On | Requests a separate unmasked binary protection-mask view after successful Apply. | Enabled and honored only with **Protect bright structures**. | Adds a full-size image in the target's sample type. For color input the mask has one independently estimated channel per source channel. | Apply only; still image only | Window lifetime; Apply retains the choice; disabling protection clears it; Reset or reopening turns it off. |
| Enable preview | Checkbox | On | None | Off/On | Shows the preview pane. Turning it off closes the pane, discards the computed preview, and disables **Preview display** and **Preview** without changing Apply settings. | Preview controls require it; Apply does not. | Turning it off avoids preview computation while editing expensive settings. | Preview only | Window lifetime; returns on when the process is reopened. |
| Target image | Image selector | Image used to launch the process | None | Open still-image views only | Changes the source and destination target. Changing the target returns the preview to the target until **Preview** is clicked again. | Sequence targets are not offered. | A large target increases time and memory. | Preview and Apply; still image only | Current window selection only. |
| Preview-pane viewer actions | Viewer toolbar actions | Viewer defaults | Viewer-setting dependent | Fit/zoom/pan, display channel, autostretch, and snapshot actions supplied by the standard RT preview | Change only preview presentation or save a preview snapshot; they do not change the model parameters. In autostretch the target and the corrected image are stretched through the target's statistics, so the original shown by right-clicking and the corrected image compare under one stretch; a computed gradient model or protection mask is stretched by its own statistics, which show the model's broad pattern rather than a flat grey. | Available with the preview pane; preview-area selection is deliberately unavailable. | Display stretch can make two identical data results look different; compare with matching view settings. | Preview presentation only | Viewer/session behavior; see [Process Windows](../../interface/process-windows.md). |
| Apply | Button | Enabled while idle | None | Requires a valid still target | Runs the full process with a cancellable progress dialog, blends an active view mask into the corrected target, updates the target, records history, and opens requested unmasked diagnostics. | Disabled during execution. | Uses full target resolution for correction and output; cancellation or failure publishes nothing. | Apply only; still image only | Current control values remain for another Apply; Reset or reopening restores defaults. |
| Reset | Button | — | None | Click action | Restores every process-specific runtime default and returns the preview to the target. | Disabled during execution. | Any preview shown after reset must be requested again before judging the default result. | Process window | No persistence. |
| Close | Button | — | None | Click action | Closes the process window. | Disabled during execution. | Unapplied parameter changes are discarded. | Process window | No persistence. |

## How the algorithm works

### Conceptual pipeline

1. Samples are read in normalized units: an unsigned integer as a fraction of
   its largest code, a signed integer counted from its smallest code, and a
   floating sample as it is. Integer images are worked on as a floating copy
   (double precision for 32-bit integers) and narrowed back at the end.
2. Each channel is estimated independently. It is area-averaged by
   **Downsample** into a working image; a partial block on the right or bottom
   averages only the pixels it covers. The working image is held in single
   precision; the polynomial fit and the box-filter running sums use double
   precision.
3. The selected model is first fitted to every working pixel. Each later round
   measures residuals, keeps the pixels inside a robust interval, removes any
   protected structure, and refits. Iteration stops when the accepted pixels
   settle or after 20 rounds.
4. The model receives optional final smoothing. The correction then reads it at
   every source pixel by bilinear interpolation and subtracts or divides it
   channel by channel, putting back the model's median level.
5. Corrected values are clamped to `[0,1]` and, for an integer image, narrowed
   to the nearest code. If an image-view mask is enabled, the process framework
   blends the original and corrected images only after modeling.

### Model and equations

For a downsampled working image of width `W` and height `H` (the source
dimensions divided by **Downsample**, rounded up), the base model radius is

\[
r = \max\left(1,\operatorname{round}\left(\frac{s}{100}\min(W,H)\right)\right),
\]

where `s` is **Scale** and `round` rounds to the nearest integer, halves away
from zero. Thus Scale is a percentage of the shorter working dimension, not a
source-image pixel radius.

At each robust-fit round, residuals are `R = I - G`, where `I` is the working
channel and `G` is the current model. The center and spread are measured over
the residuals of the pixels the previous round accepted (every pixel in the
first round):

\[
m = \operatorname{median}(R),\qquad
\sigma = 1.4826\operatorname{median}(|R-m|) + 10^{-12}.
\]

A pixel remains eligible when `m - 4σ ≤ R ≤ m + 2σ`. The tighter bright-side
limit preferentially rejects stars and bright structures. The retention floor
is the greater of 16 pixels and 2% of the working pixels. If robust and
structure rejection leaves fewer pixels, the accepted set is rebuilt from the
pixels whose residual is at or below the percentile that floor marks, less any
pixel structure protection has excluded, so the fallback never re-admits
protected structure. From the second round on, iteration stops when fewer than
`10⁻⁴` of the working pixels change between accepted and rejected, with a hard
limit of 20 rounds.

With **Protect bright structures**, pixels whose residual exceeds `m` by more
than **Threshold** seed a mask. Two low-pass passes grow those seeds with radius

\[
r_p = \max(1,\operatorname{round}(r(0.5+a))),
\]

where `a` is **Amount**. A grown value above `(1-a)/2 + 0.001` becomes
protected. Larger Amount therefore both widens the filter and lowers its
cutoff. A protected pixel stays excluded from every later fit. The union of
detections across rounds becomes the optional protection-mask output.

The default multiscale model initializes rejected holes with the accepted-pixel
mean. It performs ten fill iterations; each iteration applies three box-filter
passes along both axes and then restores accepted pixels. One final three-pass
low-pass produces the surface. Image borders use clamped-edge sampling.

The simplified model fits

\[
G(x,y)=\sum_{i+j\le d} c_{ij}x^i y^j,
\]

where `x` and `y` are normalized to `[-1,1]` across the working image, `d` is
**Polynomial degree**, and the coefficients `cᵢⱼ` minimize least-squares error
over accepted pixels. The normal equations are solved in double precision; a
term the accepted pixels do not determine is dropped and set to zero, and a
fit they determine no surface for is refused.

When **Smoothness** is greater than zero, the final model receives three more
box-filter passes with radius `max(1, round(r × Smoothness))`. Each working
pixel stands for the center of the block it averaged, and the model is read at
a source pixel by bilinear interpolation between those block centers; beyond
the outermost centers the edge value is held.

Let `L` be the median of the working model for the channel. The two correction
modes are

\[
O_{sub}=\operatorname{clip}_{[0,1]}(I-G+L)
\]

and

\[
O_{div}=\operatorname{clip}_{[0,1]}\left(\frac{I}{\max(G,10^{-6})}L\right).
\]

Subtraction preserves the model's median level while removing an additive
surface. Division normalizes a multiplicative surface; the `10⁻⁶` floor avoids
division by zero. These equations operate independently on every channel.

### Boundary and failure behavior

- Box filters repeat edge pixels outside the image. A partial right or bottom
  block is averaged over the pixels it covers, and the enlargement holds the
  edge value beyond the outermost block centers.
- A pixel holding no number (not-a-number or an infinity) takes no part in the
  estimate and is left as it was in the corrected image; a block holding no
  usable pixel takes the channel's mean. Floating samples outside `[0,1]` take
  part in the estimate as they are, and the corrected values are clamped.
- An integer result is narrowed to the nearest code, ties to even, saturated at
  the type's range.
- A polynomial fit the accepted pixels determine no surface for, structure
  protection that leaves no working pixel to fit, a Scale whose working radius
  would exceed 1,024 pixels, and a run the session's memory budget cannot hold
  are refused with an error. Nothing is published and the image is left as it
  was.
- A frame shorter than **Downsample** on either side is worked at the largest
  smaller factor that fits, and Apply warns of it.
- Cancellation is checked at round, pass, and row-block boundaries. The
  destination and optional outputs are published only after success; a stopped
  Apply changes nothing, records no history, and opens no output.

## Parameter interactions and starting points

| Combination | Conservative starting point | Too weak looks like | Too strong looks like | Adjustment order |
| --- | --- | --- | --- | --- |
| Scale + Smoothness + Downsample | `5.0`, `1.0`, `4` | Broad illumination remains in the corrected preview or model misses curvature. | Model contains smaller astronomical structure; corrected image loses broad real signal. | Inspect the model, adjust Scale first, then Smoothness; lower Downsample only if the model cannot represent the needed scale. |
| Threshold + Amount | Keep protection on at `0.050` and `0.50`. | Bright halos or extended objects appear in the model. | Protection mask covers much of the background and the gradient is underfit. | Adjust Threshold first; then change Amount to grow or contract already detected regions. |
| Multiscale + polynomial | Start with multiscale. Use degree 1–2 only when the unwanted pattern is demonstrably a smooth plane or curvature. | Polynomial leaves localized or irregular gradients. | High-degree polynomial bends around real structures or sparse accepted regions. | Compare Gradient model previews; increase polynomial degree only after confirming a lower degree cannot follow the background. |
| Subtraction + Division | Start with Subtraction for additive sky glow; use Division only for a multiplicative field. | Chosen correction leaves a pattern consistent with the other error type. | Division amplifies dark-region noise; either mode clips shadows/highlights. | Diagnose the physical pattern, inspect the model, then compare modes with identical estimator settings. |
| Active image mask + structure protection | Tune and inspect structure protection with the external mask disabled; enable it only before the final Apply when correction should be spatially limited. | Expected protected region still influences the fitted model because the external mask was assumed to be a fitting mask. | A computed diagnostic preview is blended with the source, or hard transitions appear in the corrected preview/target. | Disable the external mask, fit and inspect the unmasked diagnostics, then enable it and compute a fresh **Corrected image** preview before Apply. |

These are source-backed adjustment directions, not measured presets for a
particular camera or target.

## Inputs, outputs, and metadata

- Full Apply updates one selected still image in place after a successful run
  and adds an Image History entry such as `Scale: 5.00, Smoothness: 1.00,
  Downsample: 4, Correction: Subtraction, Model: Multiscale, Structure
  protection: enabled (threshold: 0.050, amount: 0.50)`. The simplified model
  reads `Model: Simplified polynomial, Degree: 2`, and disabled protection reads
  `Structure protection: disabled`.
- The corrected image keeps source dimensions, channel count, sample type,
  header cards, acquisition record, and recorded linear/channel-basis state; it
  is not recorded as nonlinear. Its processing lineage gains an entry naming
  `an::removeImageAutoBackground` with the same settings. Geometry and WCS are
  not changed. Numeric samples are nevertheless clamped to normalized range
  during correction.
- **Create gradient model image** opens `<target> - AGR Gradient` after Apply.
  **Create protection mask image** opens `<target> - AGR Protection Mask` when
  protection is enabled. Diagnostics have the target's dimensions, channels, and
  sample type, with values held to `[0,1]`. They keep the target's acquisition
  record, astrometric solution and lineage but none of its header cards; the only cards
  they carry mark them as nonlinear process outputs. They are not blended with
  the target's image-view mask, and their history starts with a step saying
  which image they were created from.
- A color target produces independent color-channel models and mask planes; the
  diagnostic protection image is not collapsed to one grayscale union. Each
  protected working pixel covers its whole **Downsample** block in the mask.
- A computed preview makes all three displays from one estimate, regardless
  of the two Apply-output checkboxes, and keeps them while it is shown. No
  temporary or output file is written automatically.

## Performance

Model fitting dominates runtime. Cost grows with the working pixel count set by
**Downsample**, channel count, the number of robust rounds, protection work,
and polynomial term count. The box low-pass is a running sum, so its cost does
not grow with **Scale** or **Smoothness**. Channels are estimated one at a
time in a few single-precision working rasters of the downsampled size; the
correction reads the model as it writes and never builds a full-size model.
Apply works on a full-size copy of the image and estimates the background
once; each requested output adds a full-size image drawn from that estimate. A
computed preview estimates once too and keeps the corrected image, the model
and, with protection, the mask - up to three full-size images beside the
target - so switching **Preview display** computes nothing. The work runs in
parallel on the threads the operation is given.

Use **Downsample** `4` or `8` for initial comparisons. Use `2` or `1` only when
the model preview shows that coarse estimation misses relevant variation. Since
preview-area selection is disabled, a crop selected in the viewer cannot be
used as a cheap estimator preview; make a separate cropped image if you need a
representative low-cost experiment, while remembering that a crop cannot prove
the model for the full field.

## Worked example

- Input condition/provenance and settings: The process test suite builds a
  300 × 220 grayscale floating image whose background rises to the right and
  downwards from `0.1` (`0.1 + 0.0004x + 0.0002y`) with a bright 60 × 60 square
  at `0.9`, opens the window over it, and applies the defaults with both output
  checkboxes selected. Separate record cases apply the defaults and a
  **Simplified polynomial model** / **Division** / Downsample `2` setting to
  small grayscale and color floating star scenes and compare the results with
  their behavior records.
- Expected checkpoint: The Apply completes once with no error and opens
  `target - AGR Gradient` and `target - AGR Protection Mask`. The preview shows
  the unchanged image until **Preview** is clicked and the corrected image
  afterwards. The record cases hold the corrected image to its record within
  `5×10⁻⁴` for the default subtraction, and within a ratio of `3×10⁻²` for the
  polynomial division.
- Inspect at 100%/statistics/history: On your own image, inspect the Gradient
  model for target structure, compare corner/background statistics with the
  same display stretch, and confirm the history entry before continuing.
- Limitations: This example was not executed in the current GUI with approved
  project-owned astrophotography data. It supports algorithm behavior only and
  makes no visual-quality or timing claim for real data.

## Warnings and limitations

- Apply changes the selected target. Keep a duplicate or use Image History when
  comparing models.
- Correct gradients before color calibration and nonlinear stretching in the
  usual linear workflow. The process accepts nonlinear input but does not
  reinterpret its controls for display-encoded data.
- All corrected values are clipped to `[0,1]`, including floating samples above
  `1`. Division near a weak model can amplify noise before clipping.
- The automatic estimator may confuse extended real signal with the background.
  Structure protection handles positive residuals only and cannot identify all
  astrophysical morphology.
- An active image-view mask does not protect pixels from the estimator. It is
  blended into every computed preview display and into the corrected target on
  Apply, while Apply-created diagnostic views are unmasked; disable the
  image-view mask while comparing model diagnostics.
- Per-channel selection and sequences are unsupported. RGB channels can receive
  different surfaces, so inspect color balance after correction.
- Full-image preview deliberately disables area selection to keep the preview
  model consistent with full Apply. It is computed only when **Preview** is
  clicked and can be slow on large images; switching **Preview display**
  afterwards is immediate.
- Parameters are not saved as a reusable process preset. Successful Apply keeps
  the current control values in the open process window; Reset or reopening the
  process restores runtime defaults.

## Troubleshooting

| Symptom | Cause | Fix |
| --- | --- | --- |
| Process action is unavailable, or its tooltip says `This process supports still images only, not sequences.` | Registry and launch guards require an active still image and exclusive access to it; a run on a sequence is also refused. | Extract/open a still frame, or close other process windows attached to the image. |
| Preview shows the unchanged image | No preview has been computed for the current settings: the preview shows the target until **Preview** is clicked, and any setting or target change returns it there. | Keep **Enable preview** on and click **Preview**; wait for the request to finish. |
| A model or protection preview looks like a mix of the image and the model | An enabled image-view mask is blended into every computed preview display. | Disable the image-view mask and click **Preview** again before comparing diagnostics. Re-enable it only to inspect the final corrected blend. |
| **Protection mask** cannot be selected, or output checkbox clears | **Protect bright structures** is off. Its dependent preview/output controls are disabled and a selected protection display or output is cleared. | Enable **Protect bright structures**, then click **Preview**. |
| Gradient contains stars, nebula, or a galaxy halo | Scale/protection/model choices allowed real positive or smaller-scale structure into the fit. | Inspect the protection mask; lower Threshold or raise Amount conservatively; increase Scale or compare the multiscale model. |
| Corrected image has clipped shadows/highlights or noisy dark regions | Correction is clamped; Division uses a `10⁻⁶` model floor and can strongly scale weak-background pixels. | Try Subtraction when the gradient is additive, reduce estimator aggressiveness, and inspect matched statistics. |
| Warning says `The image is too small to be downsampled by N; it was downsampled by M.` | The selected factor exceeds the shorter dimension of a very small target. | Use a smaller factor. The run has already continued with the largest factor that fits. |
| Apply reports that the accepted samples determine no background surface, or that structure protection leaves no working sample to fit | Robust and protection rejection left the polynomial nothing it could fit, or protection covered every working pixel. Nothing is changed. | Lower protection (raise Threshold, lower Amount), lower the degree, or use the multiscale model. |
| Apply reports that the model radius is larger than the working picture can usefully carry | **Scale** at a low **Downsample** implies a working radius above 1,024 pixels on a very large frame. | Raise Downsample or lower Scale. |
| Apply reports the memory budget, or is cancelled | The session's memory budget cannot hold the run, or **Cancel** was clicked. Nothing is published either way. | Raise Downsample, close other images, or rerun; cancellation needs no recovery. |
| Requested diagnostic does not open | The Apply did not succeed, or protection output is unavailable because protection is off. | Confirm the corresponding checkbox and protection state, rerun Apply, and read the error dialog/session log. |

## Related workflow steps

Use [Image Registration](../registration-alignment/image-registration.md) and
[Image Stacking](../stacking-integration/image-stacking.md) before this process,
then remove invalid borders with [Crop](../geometry/crop.md). For manual
sample-grid control, compare [Background Extraction](background-extraction.md).
After a satisfactory linear correction, continue to
[Spectrophotometric Color Calibration](../color-calibration/spectrophotometric-cc.md),
[Photometric Color Calibration](../color-calibration/photometric-cc.md), or
[Background Neutralization](../color-calibration/background-neutralization.md)
as appropriate.

## Credits

- The native implementation is based on
  [Siril AutoGradientRemoval 1.0.0 at the linked commit](https://gitlab.com/free-astro/siril-scripts/-/blob/ca21c1425b14eeaff2f94e315e683240e28b86ce/processing/AutoGradientRemoval.py),
  authored by Cyril Richard in 2026 and declared
  [GPL-3.0-or-later](https://spdx.org/licenses/GPL-3.0-or-later.html) at file
  level.
