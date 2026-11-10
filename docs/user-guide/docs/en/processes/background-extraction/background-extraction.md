---
kind: process
stage: color
menu: Process > Background Modelization > Background Extraction
summary: >-
  Fits a smooth background surface through sample points you choose, then subtracts it from the image or divides the
  image by it, to remove gradients.
data_in: linear
data_out: linear
derived: true
---
# Background Extraction

## Purpose and expected result

Background Extraction lets you choose background samples, fit a smooth surface,
and subtract or divide that surface from an image. It is intended for users who
want direct control over which regions define the sky background. A successful
Apply preserves dimensions, channel count, and sample type, modifies the target
or processed sequence, records a history step, and can open a separate gradient
model for a still image.

The fit cannot distinguish unwanted illumination from real faint structure.
Place samples only on representative background and inspect the model before
continuing to color calibration or stretching.

## At a glance

| Item | Current behavior |
| --- | --- |
| Menu path | **Process > Background Modelization > Background Extraction** |
| Enabled when | An image or sequence view is active. Background Extraction needs the view to itself: a launch is refused with a warning while any other process is attached to the image, while this process is already attached, or while an attached process holds exclusive access. The target selector can switch among compatible open views. |
| Inputs | One grayscale or color image (red, green, and blue, with or without alpha) in any integer or floating representation, or a sequence. At least one 25×25 sample enables Apply; a fit needs at least three well-spread samples. An image with more than four channels is refused. |
| Outputs | Corrected target image or processed sequence. **Output Background Model** additionally opens one same-size model view, `<target> - GRADIENT`, after a successful still-image Apply; preview and sequence runs open none. No file is written automatically. |
| Recommended image state | Calibrated, registered, stacked, cropped, and still linear. The process does not enforce linearity and does not mark its result nonlinear. |
| Preview support | On request. The preview pane shows the unprocessed target until you click **Preview**; it then shows the corrected image or the fitted model (**Preview Display**), computed by the same steps as Apply. The preview always shows the whole image, because the samples are positions over the whole image; no preview area is offered. Changing the samples, **Interpolation Method**, **Smoothing**, **Correction**, or the target marks the preview out of date until **Preview** is clicked again. |
| Sequence support | Supported by the standard process framework. One sample-position set is used for every frame; each frame's channel medians are measured from that frame and its own surface is fitted. The whole processed sequence is published, or nothing. Model-image output is not published for sequences. |
| Mask support | An enabled image-view mask does not affect sampling or fitting; the standard process framework blends the preview or Apply result with the source through it. Mask inversion is honored. The opened model view is not blended. |
| External dependencies | None for the user. The bundled engine computes the fits itself and runs the per-pixel work on oneTBB. |

## Before you start

1. Finish calibration, registration, stacking, and border cropping. Fixed black
   borders, stars, nebulae, galaxy halos, and dust can bias samples.
2. Keep the image linear for the normal deep-sky workflow. Duplicate it or be
   prepared to use Image History.
3. Disable an image-view mask while choosing samples. The mask limits only the
   final blend; it does not protect pixels from the estimator.
4. For a sequence, choose samples on a representative current frame. The same
   coordinates must describe background in every frame.

## Procedure

1. Open **Process > Background Modelization > Background Extraction** and verify
   **Target Image**.
2. Choose **Generate Samples** for a filtered grid, or use the preview: left-click
   to add a sample and right-click inside a displayed sample box to remove it.
   The grid is placed in the background; when it finishes it replaces the whole
   sample list, including samples you clicked meanwhile. Samples are fixed 25×25
   patches; a click whose patch would leave the image is not kept.
3. Inspect every sample location. Use **Clear Samples** and regenerate, or edit
   manually, until real objects and invalid borders are excluded.
4. Start with **RBF**, **Smoothing** `0.50`, and **Subtraction**. Keep **Add
   dither** off unless integer quantization is producing visible banding.
5. Keep **Enable preview** selected and click **Preview**. Until you do, the
   preview shows the unprocessed target. The preview always covers the whole
   image; there is no preview area. Switch **Preview Display** between
   **Corrected image** and **Gradient model**; a computed preview is fitted
   again for the other display.
6. If the model follows real structure, remove implicated samples, lower the
   grid tolerance, increase RBF smoothing, or compare a low-order polynomial.
   Click **Preview** after every sample or model change.
7. Optionally select **Output Background Model**, then click **Apply**. Confirm
   the history entry and, for a still image, the `<target> - GRADIENT` view.
8. Compare background statistics and color balance with matched display stretch
   before continuing.

## Complete parameter and action reference

| UI label/action | Type and unit | Runtime default | Persisted-default exception | Valid values, step, or constraints | Effect of increasing, decreasing, or selecting | Dependencies and enable/visibility rules | Quality, artifact, runtime, memory, or disk trade-off | Scope: preview/apply, channel, mask, still/sequence | Persistence |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Samples per Line | Integer grid count | `20` | None | `5`–`100`; step `1`. The grid is refused when Samples per Line × 25 pixels exceeds the image width less 2 pixels. | Raises or lowers the horizontal grid density; rows use the same spacing, as many as fit the image height. | Used only by **Generate Samples**; recorded in the history step. | More samples improve spatial coverage but raise fit cost and the chance of sampling objects. RBF accepts at most 1,024 samples, and one grid at most 4,096 sites. | Sample generation on target/current frame | Window lifetime; Apply and Reset restore `20`. |
| Grid Tolerance | Decimal MAD multiplier | `2.0` | None | `0.01`–`6.0`; step `0.1` | Higher values retain brighter grid samples; lower values reject them more aggressively. | Used only by **Generate Samples**; manual samples bypass this filter. Recorded in the history step. | Too low can leave too few samples; too high admits stars or extended signal. Rejection has no dark-side threshold beyond rejecting nonpositive medians. | Automatic sample generation | Window lifetime; Apply and Reset restore `2.0`. |
| Generate Samples | Button | — | None | Image at least 27 pixels in each dimension, and a row of boxes that fits its width | Places a centered grid over a color image's CIE lightness or over a grayscale image in the background and, when it finishes, replaces the sample list with the samples the rejection rule keeps. | Enabled while idle and no placement is on its way. Clear Samples, Reset, or a target change discards a placement still on its way. A refused grid shows a message and leaves the list as it was. | Placing reads a full-size lightness or normalized copy. A new list marks a computed preview out of date; click **Preview** before judging it. | Target/current sequence frame; all later preview/Apply operations | Samples are never saved; changing target, Reset, a successful Apply, or closing clears them. |
| Clear Samples | Button | Disabled with no samples | None | Click action | Removes all samples and overlays and discards a grid placement still on its way. | Enabled only while idle with at least one sample. | Disables Preview and Apply until samples are added again. | Current process window | No persistence. |
| Preview sample: left-click | Pointer action at image coordinates | — | None | The 25×25 patch around the click must lie entirely inside the image | Adds one sample position; its channel medians are measured whenever a preview or Apply runs. | Preview pane and sample listener must be active (**Enable preview**). | Manual control avoids objects but can create uneven coverage. | Sample set used by preview and Apply | No persistence. |
| Preview sample: right-click | Pointer action | — | None | Click inside a displayed sample box | Removes the held sample nearest the clicked box's center, which is that box's sample. | Preview pane and sample overlay must be active. | Sparse/uneven evidence can destabilize high-order or local fits. | Sample set used by preview and Apply | No persistence. |
| Interpolation Method | Selector | **RBF** | None | **Polynomial**, **RBF** | Chooses a polynomial least-squares surface or a thin-plate spline, both in normalized image coordinates. | **Degree Order** is enabled only for Polynomial; **Smoothing** only for RBF. A change marks a computed preview out of date. | RBF follows irregular smooth gradients but needs at least three samples not all on one line, accepts at most 1,024, and costs one term per sample per pixel. Polynomial is cheaper but can underfit local variation or overfit at high order. | Every channel independently; preview and Apply | Window lifetime; Apply/Reset restore **RBF**. |
| Correction | Selector | **Subtraction** | None | **Subtraction**, **Division** | Subtraction removes the surface and adds back one shared level, the mean of every sample median over every channel. Division divides by the surface and multiplies by each channel's own mean. | Always enabled while idle. A change marks a computed preview out of date. | Division can amplify noise where the fitted model is small; a model value below `0.000001` is divided as `0.000001`. Both modes hold results to `[0,1]`. | Every channel; mask post-blend; preview and Apply | Window lifetime; Apply/Reset restore **Subtraction**. |
| Smoothing | Decimal RBF regularization control | `0.50` | None | `0.00`–`1.00`; step `0.01` | Raises or lowers the RBF diagonal regularization exponentially; larger values produce a less exact fit that relaxes toward a plane. | Enabled only for RBF. A change marks a computed preview out of date. | Higher smoothing resists sample noise but can miss real background curvature; lower values follow every sample more closely. | RBF model; every channel | Window lifetime; Apply/Reset restore `0.50`. |
| Degree Order | Integer polynomial degree | `4` on first open | Reset does not assign this widget, so a user-selected order survives Reset and successful Apply even though other process controls return to defaults. | `1`–`4`; step `1`; the samples must determine all `3`, `6`, `10`, or `15` terms respectively | Higher orders add cross terms and curvature. | Enabled only for Polynomial. A change does not mark a computed preview out of date; click **Preview** to see it. | Higher orders require more well-spread samples and can bend through real structure. A degree the samples do not determine is refused by name and the image is left unchanged. | Polynomial model; every channel | Process-window lifetime, including Reset/Apply; reopening restores `4`. |
| Output Background Model | Checkbox | Off | None | Off/On | Requests a separate gradient view after successful still-image Apply. | No separate view is published for preview or sequence execution. | Adds one full-size image, plus single-precision model planes during the run. | Still-image Apply only; all channels; unmasked model | Window lifetime; Apply/Reset turn it off. |
| Add dither | Checkbox | Off | None | Off/On | Adds a deterministic dither `0.0001` wide and centered on zero (each normalized sample moves by less than `0.00005`), derived from the pixel's position, immediately before correction. It does not change sample measurement or the fitted model. | Always enabled while idle. A change does not mark a computed preview out of date. | Can reduce integer quantization banding but slightly perturbs corrected pixels. | Corrected result only; every channel; preview and Apply | Window lifetime; Apply/Reset turn it off. |
| Preview Display | Selector | **Corrected image** | None | **Corrected image**, **Gradient model** | Selects whether a computed preview shows the corrected image or the fitted model, held to `[0,1]`. Switching it after **Preview** fits the preview again for the other display. | Enabled only with **Enable preview**. Before **Preview** is clicked, the pane shows the unprocessed target whichever is selected. | Model inspection exposes overfitting without opening an extra view. An active mask blends the model preview with the source too. | Preview only | Window lifetime; Apply/Reset restore **Corrected image**. |
| Preview | Button | Disabled with no samples | None | One request at a time | Measures the samples, fits, and computes the corrected image or the model over the whole image. | Requires idle state, **Enable preview**, and at least one sample. | Runs the same full-resolution fit and correction as Apply. | Preview only | The computed preview lasts until the samples, Interpolation Method, Smoothing, Correction, or target change. |
| Enable preview | Checkbox | On | None | Off/On | Shows/hides the preview pane, which is also where samples are clicked and drawn. | Apply remains available when preview is off. | Turning it off removes click placement and the sample overlay; Generate Samples still works. | Preview presentation and manual sampling | Window lifetime; unlike most controls, Reset does not restore it. |
| Target Image | Image/sequence selector | Launching view | None | Compatible open views | Changes the target and clears samples; the preview shows the new target until **Preview** is clicked. | Disabled during execution. | A sequence reuses coordinates across frames; a large image increases memory/runtime. | Preview and Apply | Current window only. |
| Preview-pane viewer actions | Standard viewer actions | Viewer defaults | Viewer/session dependent | Fit/zoom/pan, channels, autostretch, and snapshot actions | Change presentation, not samples or fit parameters. | Available with preview. No preview area is offered: the preview always shows the whole image, because the samples are positions over the whole image. | Mismatched stretches can make identical data look different. | Preview presentation only | Viewer/session behavior. |
| Apply | Button | Disabled with no samples | None | Requires at least one sample and a valid target | Measures the samples on the image (on each frame of a sequence), fits, corrects, post-blends an active mask, updates the image or processes the sequence, records history, and may open the still-image model. A refused fit changes nothing and says why. | Disabled during execution. | Uses full resolution and a working copy of the image; showing or keeping the model adds one plane per channel. | Still or sequence; all channels | On success samples and most controls reset; Degree Order and preview checkbox retain their widget state. |
| Reset | Button | — | None | Click action | Clears samples, discards a grid placement on its way, marks the preview out of date, and restores declared process defaults except Degree Order and the preview checkbox state. | Disabled during execution. | Any computed preview must be asked for again. | Process window | No saved preset. |
| Close | Button | — | None | Click action | Closes the process window. | Disabled during execution. | Unapplied samples/settings are discarded. | Process window | No persistence. |

## How the algorithm works

### Conceptual pipeline

1. **Generate Samples** reads the current image in normalized units: integers
   as fractions of their full range (a signed image counted from its smallest
   value), floating values as stored. A color image is reduced to its CIE
   lightness L\*, scaled to `[0,1]`; a grayscale image is used directly.
2. The engine places a centered grid of 25×25 boxes: **Samples per Line** boxes
   spread across the width less a one-pixel margin on each side, then as many
   rows at the same spacing as fit the height, with the leftover height split
   between top and bottom. Each box's median is measured.
3. The grid keeps a site when its median is positive and at most
   `level + tolerance × MAD`, where `level` is the median of all site medians
   and MAD is the unscaled median absolute deviation of the site medians from
   that level. The kept sites become the sample list, which holds positions only.
4. At Preview or Apply, every sample's median is measured in each channel of
   the run's image (the target, or each frame of a sequence) in the same
   normalized units; pixels holding no number take no part. Each channel is
   fitted independently by Polynomial or RBF, in double precision.
5. The correction evaluates the surface at every pixel and holds each result to
   `[0,1]`; integer images are written back to the nearest code, ties to even.
   Pixels holding
   no number are left as they were. The image-view mask, if enabled, is blended
   afterward by the process framework.

### Model and equations

Both surfaces use coordinates normalized so that the first pixel center of each
axis is `-1` and the last is `1`:

\[
u=\frac{2x}{W-1}-1,\qquad v=\frac{2y}{H-1}-1.
\]

For polynomial order `d`, the model contains every two-dimensional monomial up
to that order. For example, order 2 is

\[
G_c(u,v)=a_0+a_1u+a_2v+a_3u^2+a_4uv+a_5v^2,
\]

where `G_c` is the fitted background for channel `c`. The coefficients are a
least-squares fit through every sample with equal weight. Orders 1–4 contain 3,
6, 10, and 15 coefficients; when the samples do not determine every term, the
run is refused and names the degree, the terms, and how many the samples
determine.

RBF is a thin-plate spline centered on the samples:

\[
G_c(u,v)=a_0+a_1u+a_2v+\sum_i w_i\,\phi(r_i),\qquad \phi(r)=r^2\ln(r),
\]

with `φ(0)=0` and `r_i` the distance from sample `i` in normalized coordinates.
The weights satisfy `Σw_i = Σw_i u_i = Σw_i v_i = 0`, so the constant and both
linear terms are constrained. The diagonal regularization is

\[
\lambda = \bar K\,10^{-4}\,10^{3(s-0.5)},
\]

where `s` is **Smoothing** and `K̄` is the mean row sum of the kernel matrix.
The system is solved by elimination with partial pivoting, and the surface is
evaluated at every pixel.

Let `I_c` be a normalized source channel, `G_c` its model, `μ_B` the mean of all
sample medians across every channel, and `μ_c` the source-channel mean over the
pixels that hold a number. With **Add dither**, each sample first becomes
`I_c + 0.0001·δ(x,y)`, where `δ` is a fixed value in `[-0.5, 0.5)` derived from
the pixel's position. The implemented corrections are

\[
O_{c,sub}=\operatorname{clip}_{[0,1]}(I_c-G_c+\mu_B)
\]

and

\[
O_{c,div}=\operatorname{clip}_{[0,1]}\left(\frac{I_c}{\max(G_c,10^{-6})}\mu_c\right).
\]

Subtraction therefore restores one shared level for every channel; Division
restores a separate mean for each channel.

### Boundary and failure behavior

- A clicked sample whose 25×25 patch would leave the image is not kept.
  **Generate Samples** is refused with a message, placing nothing and keeping
  the current list, when Samples per Line × 25 pixels is wider than the image
  less 2 pixels, when the grid would hold more than 4,096 sites, or when the
  image is smaller than 27 pixels in either dimension.
- A polynomial whose samples do not determine all of its terms is refused by
  name and the image is left unchanged.
- RBF is refused for fewer than three samples, for samples that all lie on one
  line, for a singular system, and for more than 1,024 samples.
- A sample box in which some channel holds no number at all stops the run with
  an error.
- Every stage honors cancellation. A stopped Apply publishes nothing, leaves the
  image and its history unchanged, opens no model view, and shows no error.
- When the engine cannot obtain working memory, the run publishes nothing and
  the error names the memory budget.
- All corrected and model samples are held to `[0,1]` in normalized units.
  Where the fitted model is at or below `0.000001`, Division divides by
  `0.000001`, which drives those pixels to the upper clip.
- Thin-plate distances are measured in coordinates normalized per axis, so on a
  non-square image a pixel along the longer axis counts for less than one along
  the shorter.

## Parameter interactions and starting points

| Combination | Conservative starting point | Too weak looks like | Too strong looks like | Adjustment order |
| --- | --- | --- | --- | --- |
| Samples per Line + Grid Tolerance | `20`, `2.0`, then manually remove object samples | Model misses local background variation | Samples cover stars/target structure and model reproduces it | Generate -> inspect locations -> adjust tolerance/density -> edit manually |
| RBF + Smoothing | RBF at `0.50` | Broad/local gradient remains | Model ignores legitimate curvature or becomes over-smoothed | Fix samples first -> adjust smoothing -> compare model |
| Polynomial + Degree Order | Order 1 or 2 with well-distributed samples | Plane/curvature remains | Surface bends through objects or edges | Ensure enough samples -> start order 1 -> increase only if model requires it |
| Subtraction + Division | Subtraction for additive sky glow | Multiplicative shading remains | Division amplifies noise/clips where model is weak | Identify error type -> inspect model -> compare corrections |
| Active mask + sample set | Fit with external mask disabled | Masked objects still bias the fit | Final blend has hard/local transitions | Build clean samples -> preview unmasked -> enable mask only for final spatial limitation |

Starting directions are source-derived; they are not measured presets for a
particular camera, target, or gradient.

## Inputs, outputs, and metadata

- A still-image Apply updates the selected target and adds a **Background
  Extraction** history step described, for example, as `Samples per Line: 5,
  Grid Tolerance: 2, Correction: Subtraction, Interpolation: RBF, Smoothing:
  0.5`; a Polynomial run records `Degree Order: n` in place of Smoothing. The
  grid values are recorded even when every sample was placed by hand.
- The corrected result retains source dimensions, channels, sample type, image
  identity, cards, acquisition record, and astrometric solution (WCS). It is not
  marked nonlinear, and its lineage gains an entry naming
  `an::applyImageBackground` with the step description. Numeric clipping can
  still change range.
- **Output Background Model** opens `<target> - GRADIENT` beside the target as a
  separate view with the target's size, channels, and sample type, its values
  held to `[0,1]`. It keeps the target's acquisition record, astrometric
  solution, and lineage, but none of its cards except processing-state cards
  marking it a nonlinear process output (`background-extraction-gradient`). Its
  first history step reads `BackgroundExtractionProcess: Image gradient created
  from <target>`. It is not mask-blended.
- Sequence execution writes a processed sequence through the standard sequence
  pipeline and then replaces or opens it according to the process framework
  choice. No separate model sequence is created.
- Samples, parameters, and diagnostics are not written to disk automatically.

## Performance

Sample measurement reads one 25×25 box per sample and channel. A polynomial fit
solves a small system of at most 15 coefficients and then evaluates a few terms
per pixel. An RBF fit solves a dense system of one row per sample plus three,
whose cost grows with the cube of the sample count, and then evaluates one term
per sample at every pixel of every channel; a dense RBF grid on a large image
can therefore dominate runtime. Preview runs the same full-resolution fit and
correction as Apply, and only when **Preview** is clicked.

The run works on a copy of the image, and an integer image also on a normalized
working copy. Showing or keeping the model adds one single-precision plane per
channel, and **Output Background Model** adds the new image.

Begin with the default grid and RBF smoothing. Use a representative separate
crop for cheap experimentation.

## Worked example

- Input condition/provenance and settings: A hypothetical calibrated linear
  master with an additive smooth gradient; RBF, 20 samples per line, tolerance
  2.0, smoothing 0.50, Subtraction, dither off.
- Expected checkpoint: Source inspection shows that Preview fits the same sample
  set as Apply and shows either the corrected image or the model, and that Apply
  records the chosen interpolation/correction settings. The focused tests show,
  on a synthetic grayscale gradient with one bright square, that the generated
  grid keeps no sample on the square and that the opened model follows the
  gradient (near 0.11 at the top-left corner, rising to the right and down).
  This does not prove visual flattening on a real image.
- Inspect at 100%/statistics/history: Check the Gradient model for stars or target
  structure, compare background regions with matched stretch/statistics, and
  verify the history step.
- Limitations: No approved project-owned gradient image was executed; no visual
  quality, ideal parameter, or production timing claim is made.

## Warnings and limitations

- Apply changes the target or produces/replaces a processed sequence. Keep a
  duplicate or use Image History.
- An image mask limits only the final blend and cannot exclude samples from the
  model. Remove contaminated samples explicitly.
- Automatic rejection is one-sided: it removes nonpositive and unusually bright
  patches, but not unusually dark positive patches.
- RGB fitting is per channel, while Subtraction restores a shared level across
  channels. Inspect color balance after correction.
- All outputs are held to the normalized range `[0,1]`; a floating image with
  values outside it loses them. Division floors the model at `0.000001`, so it
  remains unsafe where the fitted model approaches zero.
- RBF needs at least three samples not all on one line and accepts at most
  1,024; a polynomial needs samples that determine all of its terms. Otherwise
  the run is refused and nothing changes.
- The preview always shows the whole image and offers no preview area,
  because the samples are positions over the whole image. For cheap
  experiments, work on a representative separate crop.
- No reusable preset or sample-map file is saved. Most controls reset after
  Apply; Degree Order and preview checkbox state are exceptions in the current UI.

## Troubleshooting

| Symptom | Cause | Fix |
| --- | --- | --- |
| Apply and Preview remain disabled | The window holds no samples; Preview also needs **Enable preview**. | Generate samples or left-click valid background locations away from edges. |
| Generate Samples reports `this many boxes of this size do not fit across the picture` | Samples per Line × 25 pixels is wider than the image less 2 pixels. Nothing is placed. | Lower **Samples per Line** to at most (image width − 2) / 25. |
| Generate Samples produces none or very few | Every candidate had a nonpositive lightness median or sat above `level + tolerance × MAD`. | Use a positive linear representation, raise tolerance carefully, or place manual samples. |
| Error `a polynomial of degree … needs samples that determine its … terms` | The samples do not determine every term of the selected order. The run is refused and the image is unchanged. | Add well-distributed samples, lower Degree Order, or use RBF. |
| Error `the thin-plate system these sites describe is singular` | Too few RBF samples, samples all on one line, or two samples at one position. | Add samples spread across the image, or remove duplicates. |
| Error naming a thin-plate spline that `accepts fewer` sites | More than 1,024 samples with RBF. | Lower Samples per Line or use Polynomial. |
| Gradient model contains stars or target structure | Grid tolerance was too permissive or manual samples cover real signal. | Lower tolerance, reduce density, or remove/relocate samples; preview again. |
| Preview shows an older result or the unprocessed target | The preview is computed only when **Preview** is clicked, and a change of samples, Interpolation Method, Smoothing, Correction, or target marks it out of date. | Click **Preview**. |
| Division produces clipped, black, or white pixels | The model is zero/near zero or Division is the wrong physical correction. | Return to Subtraction or rebuild a strictly positive representative model. |
| Corrected preview changes when a mask is enabled | The estimator ignores the mask; the framework post-blends the result afterward. | Tune samples/model with the mask disabled, then enable it only to inspect the intended final blend. |
| Output Background Model does not open for a sequence or a preview | The model is kept only by a still-image Apply. | Apply to a representative still frame for model diagnosis; the process does not create a model sequence. |
| Error mentions the memory budget | The engine refused the run's working memory; nothing was published. | Close other images, or use a smaller crop or fewer samples. |
| Process was cancelled | The run observed the stop; nothing was published and the history is unchanged. | Reduce grid density or image size for prototyping, then rerun. |

## Related workflow steps

Complete [Image Calibration](../calibration/image-calibration.md),
[Image Registration](../registration-alignment/image-registration.md), and
[Image Stacking](../stacking-integration/image-stacking.md) first, then remove
invalid borders with [Crop](../geometry/crop.md). Compare the automatic,
all-pixel [Auto Gradient Removal](auto-gradient-removal.md) when constructing a
clean manual sample set is impractical. Continue to
[Spectrophotometric Color Calibration](../color-calibration/spectrophotometric-cc.md),
[Photometric Color Calibration](../color-calibration/photometric-cc.md), or
[Background Neutralization](../color-calibration/background-neutralization.md)
only after the background model is acceptable.

## Credits

- The sample-grid workflow follows the upstream Siril sample-grid implementation
  ([Siril `background_extraction.c` at the linked revision](https://gitlab.com/free-astro/siril/-/blob/ef048278814067d24178f01067870d8896d9633d/src/algos/background_extraction.c)):
  AfterNight's engine keeps its 25×25 sample box, its median-plus-tolerance
  grid rejection, and its RBF smoothing mapping. That code is derived from
  Siril's under its GPL-3.0-or-later licence, by Francois Meyer and team
  free-astro, and **About > About AfterNight** credits it.
