# Wavelet Denoise

<!-- MEDIA_SLOT: UG-MEDIA-061 -->

## Purpose and expected result

Wavelet Denoise reduces low-amplitude structure by decomposing the image into
decimated Haar-wavelet coefficients, soft-thresholding them relative to a robust
noise estimate, reconstructing the image, and averaging several randomly shifted
repetitions. It is useful when you want direct scale control rather than a
patch-trained or learned denoiser.

The expected result is a same-size, same-type still or sequence with less visible
fine noise. It is not a detail-layer editor: one Threshold is applied across the
entire transformed coefficient array, including the coarsest approximation.
Excessive Threshold or Scales can therefore suppress real structure, create block
or boundary artifacts, and alter broad levels. Every execution draws the same
fixed-seed set of cycle-spinning shifts, so the same input and settings give
the same result.

## At a glance

| Item | Current behavior |
| --- | --- |
| Menu path | **Process > Noise Reduction > Wavelet Denoise Filter** |
| Enabled when | An image or sequence is active, the same process is not already attached to it, and no attached process holds exclusive access |
| Inputs | One intended normalized grayscale or RGB still or sequence; a still CFA mosaic is treated as grayscale samples unless debayered first; an optional active mask must match target geometry |
| Outputs | Destructively replaces a still or, after successful temporary processing, a sequence; **Take snapshot** creates an auxiliary still; dimensions and pixel type are retained; component modes reconstruct RGB; no file is written directly |
| Recommended image state | Usually linear or early nonlinear, after calibration/registration/stacking and before a strong stretch amplifies noise; valid on nonlinear data when judged in that state |
| Preview support | Asynchronous RT preview using the same G'MIC command and Amount/mask blend as Apply; a selectable preview area and display-only autostretch are available |
| Sequence support | Supported by the generic temporary replacement-sequence path with per-frame processing, progress, and cooperative cancellation; configured CFA auto-debayer can produce RGB frames |
| Mask support | Supported for preview, still Apply, and sequence Apply by post-filter blending; white applies, black protects, gray blends, and **Invert Mask** reverses weights |
| External dependencies | Bundled G'MIC 3.5.4 command `denoise_haar`, run through AfterNight's optional G'MIC adapter; no separate installation, model, catalog, or network service. In a build without G'MIC, Preview and Apply are disabled and their tooltip says why |
| Non-finite samples | An image holding a NaN or infinite sample is refused before G'MIC sees it, and left unchanged |

## Before you start

- Finish calibration, registration, stacking, gradient correction, and color
  calibration first. Wavelet thresholding cannot distinguish faint signal from
  coefficients at the estimated noise scale.
- Work on normalized finite samples. The image is handed to G'MIC as float
  fractions of full scale and the result is clamped to `[0,1]`; out-of-range
  float data is therefore clipped even with a weak filter. An image holding a
  NaN or infinite sample is refused with "the image holds a sample that is not
  a finite number, which G'MIC cannot process" and left unchanged.
- This process needs G'MIC, which AfterNight normally includes. In a build
  without it, **Preview** and **Apply** are disabled and their tooltip reads
  "This build of AfterNight carries no G'MIC library, so this process cannot
  run."
- Start with RGB only if the same absolute coefficient threshold is appropriate
  for every channel. For chroma-heavy noise, compare a lightness/chroma component
  on a duplicate and inspect reconstructed color.
- Do not treat **Iterations** as convergence. It is the number of independent
  random cycle-spinning reconstructions averaged together. The filter has no
  convergence tolerance or “converged” status.
- Keep Scales proportionate to the target. G'MIC temporarily extends image
  dimensions to multiples required by the Haar depth; high scales can add large
  work buffers and stronger edge sensitivity.
- Duplicate a still or rely on a verified Image History snapshot. Apply replaces
  the target only after successful completion; sequence Apply uses temporary disk
  storage and commits only after every frame succeeds.

## Procedure

1. Select a calibrated still or sequence and open **Process > Noise Reduction >
   Wavelet Denoise Filter**. Confirm **Target Image**, choose **RGB** or a deliberate
   component, and keep **Preview** selected.
2. Leave **Threshold: 1.40**, **Scales: 6**, **Iterations: 10**, and **Amount:
   1.00** for the first preview. Use **Preview area** around both smooth background
   and real fine structure; the area affects Preview and snapshots, not Apply.
3. Lower Threshold if stars, filaments, texture, or local contrast soften. Raise
   it only until random grain falls without making small features waxy or blocky.
4. Reduce Scales if larger structures or boundaries move. Raise Scales only when
   noise remains at broader spatial support and the memory/padding cost is
   acceptable.
5. Use Iterations to average more shifted reconstructions, not to demand more
   smoothing. Use Amount for the final source/result blend; values below `1`
   restore original signal and noise uniformly.
6. Toggle **Switch image** with the same Linear/Autostretch display mode and inspect
   at 100%. Select **Apply** only after checking edges, stars, color, and a smooth
   background. For a sequence, monitor progress; Cancel preserves the original.

## Complete parameter and action reference

| UI label/action | Type and unit | Runtime default | Persisted-default exception | Valid values, step, or constraints | Effect of increasing, decreasing, or selecting | Dependencies and enable/visibility rules | Quality, artifact, runtime, memory, or disk trade-off | Scope: preview/apply, channel, mask, still/sequence | Persistence |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Threshold | Double value plus slider, estimated-noise multiplier | `1.40` | None | `0.00..10.00`; two decimals; spin step `0.01`; slider linear over 1,000 positions | Sets soft threshold `T = Threshold × sigma`; larger values zero/shrink more coefficients | Enabled while idle; Apply remains enabled at `0` | Higher values remove more noise and real low-amplitude detail; very high values can flatten levels because all coefficients are thresholded | Preview/Apply; selected channel/component; before Amount and mask; still/sequence | Captured per execution; Reset returns `1.40`; successful Apply retains the current value |
| Scales | Integer value plus slider, Haar levels | `6` | None | `2..10`, step `1`; actual level is capped by image dimensions | Adds coarser Haar levels and expands the largest affected support | Enabled while idle; no Auto (`0`) choice is exposed | More levels add transform work, can increase temporary padding sharply, and expose broader structure/boundaries to thresholding | Preview/Apply; selected channel/component; still/sequence | Captured per execution; Reset returns `6`; successful Apply retains the current value |
| Iterations | Integer value plus slider, cycle-spinning repetitions | `10` | The label/tooltip says denoising iterations, but the bridge passes it as G'MIC `cycle_spinning` | `1..32`, step `1` | Averages more independent randomly shifted denoise/reconstruction results; it is not an optimization loop or convergence count | Enabled while idle | Cost scales approximately linearly; more cycles reduce shift dependence but do not guarantee a stronger or identical result | Preview/Apply; selected channel/component; still/sequence | Captured per execution; Reset returns `10`; successful Apply retains the current value; random shifts are not persisted |
| Amount | Double value plus slider, blend fraction | `1.00` | None | `0.00..1.00`; two decimals; spin step `0.01`; slider linear over 1,000 positions | Blends `source × (1-Amount) + denoised × Amount`; `0` restores source, `1` uses full denoised result | Enabled while idle; Apply remains enabled at `0` | Lower values recover signal and noise together; adds one full-image blend whenever not `1` | Preview/Apply after channel reconstruction and before mask; still/sequence | Captured per execution; Reset returns `1.00`; successful Apply retains the current value |
| Target Image | Image selector | Attached/active target | None | Open eligible image or sequence | Changes source, Channel list, Preview, mask, and Apply destination | Active image required; duplicate/exclusive gates apply | Wrong target is destructively replaced after success | Preview/Apply source and active mask | Target lifecycle; Reset does not change it |
| Channel | Combo box | **RGB** for RGB; disabled **Grayscale** for mono | None | **RGB**, **Red**, **Green**, **Blue**, **CIE L* (Lightness)**, **CIE a***, **CIE b***, **CIE c***, **CIE h***, **CIE X**, **CIE Y (Luminance)**, **CIE Z**, **HSL Hue**, **HSL Saturation**, **HSL Lightness**, **HSV Hue**, **HSV Saturation**, **HSV Value**, **YCbCr Luma**, **YCbCr Blue Chroma**, **YCbCr Red Chroma** | RGB sends all stored channels to G'MIC with one shared noise estimate; stored/component choices filter one plane and reconstruct/copy the others | Component choices require RGB; mono selector disabled | RGB channels are thresholded independently but share one absolute threshold; converted modes add buffers, quantization, and gamut/clipping | Preview/Apply; active mask post-blend; still/sequence | Captured by current selection; Reset does not restore RGB; successful Apply retains the selection |
| Preview | Checkbox | Checked | None | Checked/unchecked | Enables/disables asynchronous full-image or selected-area execution | Process idle | Runs the full G'MIC filter; repeated previews cost time | Preview only; mask-aware; current still/current displayed sequence frame | Window lifetime; Reset does not change it |
| Preview pane | Image viewer | Current result | None | Pan with scroll bars/dragging; toolbar actions are listed separately below | Shows the mask-aware result without modifying target | Exists only with Preview | Holds source/result and display buffers | Preview only | Window lifetime |
| Fit To Window | Preview-toolbar action | Preview is fitted when its result is first displayed | None | Click or **Ctrl+Shift+F** | Sets one uniform scale that fits the whole image inside the viewport | Exists only with Preview and a displayed image | Best overview; small details may be resampled or hidden | Preview display only; no effect on Apply/snapshot pixels | Viewer lifetime; later zoom actions replace the fitted scale |
| Zoom In | Preview-toolbar action | Current fitted/display scale | None | Click; each activation requests `1.25×` relative zoom within viewer limits | Enlarges the image around the viewport center | Exists only with Preview and a displayed image | Improves local inspection but increases panning and can expose display interpolation | Preview display only | Viewer lifetime |
| Zoom Out | Preview-toolbar action | Current fitted/display scale | None | Click; each activation requests `0.8×` relative zoom within viewer limits | Reduces the image around the viewport center | Exists only with Preview and a displayed image | Shows more context while hiding fine residuals | Preview display only | Viewer lifetime |
| Zoom 100% | Preview-toolbar action | Current fitted/display scale | None | Click; sets absolute scale `1.0` | Displays one image pixel at the viewer's 100% scale | Exists only with Preview and a displayed image | Preferred for judging noise, stars, and artifacts; large images require panning | Preview display only | Viewer lifetime |
| View Channel | Preview-toolbar combo box | **RGB** for RGB; disabled **Grayscale** for mono | None | **RGB**, **Red**, **Green**, **Blue**, **CIE L* (Lightness)**, **CIE a***, **CIE b***, **CIE a*=R / b*=G (Chrominance)**, **CIE c***, **CIE h***, **CIE X/Y/Z**, **HSL Hue/Saturation/Lightness**, **HSV Hue/Saturation/Value**, **HSI Hue/Saturation/Intensity**, and **YCbCr Luma/Blue Chroma/Red Chroma** | Chooses only which stored or derived component the preview viewer displays | Distinct from process **Channel**; RGB target required for component choices; mono is disabled | Derived display modes add conversion work and can exaggerate or hide artifacts without changing pixels | Preview display only; no effect on execution, Apply, masks, or snapshots | Preserved while the viewer rebuilds the same channel list; not in process history |
| Display Mode | Preview-toolbar combo box | Inherits target **Linear** or **Autostretch** mode when preview is created | None | **Linear**, **Autostretch** | Changes display only; Autostretch uses cached full-target source statistics | Exists only with Preview; Autostretch enables **Autostretch Settings** | Can reveal faint residuals or exaggerate noise; never changes output samples | Preview display only; no effect on Apply/snapshot | Preview-view state; not in process history |
| Autostretch Settings | Preview-toolbar dialog action | Disabled in Linear mode; inherits current target-view settings | Shared-view values load when opened | Opens Target Background, Shadows Clipping, linked channels, neutralization/color calibration, saturation/background, brightness, contrast, banner, Reset, and Close; **Apply Stretch** is hidden | Tunes only preview display | Autostretch mode only; disabled while its dialog is open | Aggressive display settings can misrepresent denoise strength/color | Preview display only | Viewer-scoped; not captured in Wavelet parameters |
| Switch image | Preview-toolbar action | Denoised preview shown | None | Click; **Space**; right-click; **Ctrl+Shift+O** | Toggles between current result and original target | Exists only with Preview | Comparison only; no recomputation | Preview display; still/current displayed sequence frame | Preview-window lifetime |
| Preview area | Preview-toolbar checkbox | Unchecked | None | Checked/unchecked; move/resize overlay | Processes a selected area and composites it over the original full preview | Exists only with Preview | Reduces transform work while tuning; final Apply remains full-frame | Preview and Take snapshot only; mask cropped consistently | Preview-window lifetime; Reset does not change it |
| Take snapshot | Preview-toolbar action | No snapshot | None | Click or **Ctrl+Shift+K** | Re-executes current settings and opens `<target> - Wavelet Denoise - SNAPSHOT` | Exists only with Preview | Allocates/processes an auxiliary still; display autostretch is not baked in | New still; target unchanged; selected area/mask honored | Each action creates a separate image view |
| Apply | Button/action | Enabled | None | One action, including Threshold/Amount `0` | Captures all four values and processes selected channel for still or every sequence frame | Target must lock; controls disabled during commit | G'MIC runs serially per call; high scales/cycles and conversions dominate; mask/Amount add blends | Destructive still/sequence replacement after success | Records partial history and retains the current dialog controls after success |
| Reset | Button/action | Enabled while idle | None | One action | Restores Threshold `1.40`, Scales `6`, Iterations `10`, Amount `1.00` | Does not change Target, Channel, Preview, preview toolbar, or display | No output change; checked Preview schedules another execution | Configuration only | Immediate |
| Close | Button/action | Enabled while idle | None | One action | Closes without applying current settings | Disabled during Apply | Active preview cancellation reaches the running command through G'MIC's abort flag; pending preview discarded | Window/execution | No process settings persist after destruction |
| Sequence progress / Cancel | Shared indeterminate per-process/frame progress and Cancel | Created automatically for sequence Apply | None | Cooperative Cancel; frame text updates through temporary sequence processing | Writes processed frames to a temporary sequence and replaces target only after success | Sequence target only | Decode/encode, temporary disk, and repeated G'MIC execution add cost; cancellation can interrupt the active G'MIC command | Sequence Apply; selected channel, Amount, and mask apply to every frame | Cancel removes temporary output; success replaces sequence and records one history step |

## How the algorithm works

### Conceptual pipeline

1. The selected stored image or converted component is checked for non-finite
   samples and handed to G'MIC through AfterNight's adapter, one G'MIC session
   per run, as planar single-precision fractions of full scale: a tightly packed
   float image in place, anything else as a working copy.
2. G'MIC estimates one noise standard deviation, chooses the requested Haar depth
   capped by image size, and temporarily extends dimensions to multiples of
   `2^(scales+1)` using no interpolation and a zero (Dirichlet) boundary.
3. For every cycle, G'MIC periodically shifts the working image by a random
   integer displacement, drawn from a generator AfterNight seeds with the same
   fixed value on every call, performs the multiscale Haar transform, soft-thresholds
   coefficients, performs the inverse transform, undoes the shift, and accumulates
   the reconstruction. The accumulated image is divided by the cycle count and
   restored to original dimensions.
4. AfterNight clamps every output sample to `[0,1]`, converts back to the source
   pixel type (rounded to nearest and held to the type's range), then
   reconstructs the chosen color component if necessary, applies Amount, and
   applies the active mask. The result keeps the source image's metadata. A
   derived component's reconstruction saturates an integer image at its type's
   range and keeps a floating image's values outside `[0, 1]`.

### Noise estimate, transform, and reconstruction

For input `f`, the vendored `std_noise` command forms a discrete Laplacian `L(f)`,
subtracts its median, and estimates

\[
\hat\sigma = \frac{1.4826\;\operatorname{median}(|L(f)-
\operatorname{median}(L(f))|)}{\sqrt{20}}
\]

for a 2D image. The factor is a robust Gaussian-noise scale estimate for this
Laplacian stencil. One scalar estimate is used for the whole image supplied to
the command; RGB therefore shares it across channels.

Let `H_s` be the `s`-level separable Haar analysis, `H_s^{-1}` its inverse, and
`c=H_s(f)` a coefficient. With UI Threshold `k`, G'MIC uses `T=k sigma` and the
soft shrinkage

\[
S_T(c)=\operatorname{sign}(c)\max(|c|-T,0).
\]

Unlike per-layer wavelet tools, this command thresholds the complete transformed
image; no separate strength exists for each scale or for the coarsest residual.
Each Haar level halves the current spatial approximation and produces horizontal,
vertical, and diagonal detail coefficients. Inverse Haar synthesis reconstructs
the original padded grid before dimensions are restored.

### Cycle spinning and blend

For `N=Iterations`, cycle `j` draws a random periodic shift `D_j`. The full
filtered result is

\[
d=\frac{1}{N}\sum_{j=1}^{N}D_j^{-1}H_s^{-1}
\left(S_T(H_s(D_j f))\right).
\]

Shifting before a decimated Haar transform changes which samples meet at block
boundaries. Undoing and averaging the shifts reduces translation/block artifacts.
The generator is seeded with the same fixed value on every call, so the same
input and settings give the same result on every run. There is no iterative
convergence or tolerance.

After channel reconstruction, Amount `a` produces

\[
b=(1-a)f+a d,\qquad 0\le a\le1.
\]

An active mask then blends `f` and `b`; mask inversion reverses its weights.

### Boundary and failure behavior

- The actual scale count is `min(requested, floor(log2(min(width,height))-1),
  32)`. Very small images may not support the UI's minimum two levels and can
  fail in the G'MIC command.
- Dimension extension uses a zero boundary and no interpolation. High scales or
  non-multiple dimensions can make outer pixels differ from the interior and can
  increase memory substantially. Cycle shifts themselves use periodic boundaries.
- Every G'MIC result is clamped to normalized range before conversion. Integer
  output is rounded to the source type; double input has only float working
  precision. An image holding NaN/Inf is refused before the command runs.
- A G'MIC command error is reported with G'MIC's message. The source is not
  committed on failure.
- G'MIC receives a live cancellation flag, so Cancel can abort inside the command.
  Sequence cancellation also removes the temporary output before target replace.

## Parameter interactions and starting points

| Combination | Conservative starting point | Too weak looks like | Too strong looks like | Adjustment order |
| --- | --- | --- | --- | --- |
| Threshold + Scales | Threshold `1.0..1.4`, Scales `4..6` on a representative area | Fine grain or broader mottling remains | Stars/filaments soften, background becomes blocky, broad levels move | Choose needed scale support first, then raise Threshold slowly |
| Iterations + preview repeatability | `10`; use fewer only for faster coarse tuning | More shift-dependent/block texture | No extra “convergence”; only cost rises | Tune Threshold/Scales with moderate cycles, then increase cycles if shift artifacts matter |
| Amount + strong threshold | Keep Amount `1` while diagnosing, then back off toward `0.5..0.8` | Denoise is visually diluted | Full result appears waxy or loses weak structure | Tune filter at full Amount, then restore source with Amount |
| RGB + unequal channel noise | Compare RGB with a deliberate luminance/chroma component on a duplicate | Noisiest channel remains | Cleaner channels lose detail or reconstructed color shifts (and, in an integer image, clips) | Inspect channels, choose component, then retune Threshold |
| Scales + image dimensions | Stay below scales that cause large padding jumps | Broad-scale noise remains | Edge/padding artifacts and memory/time jump | Reduce Scales or use a representative crop |
| Mask + Amount | Validate global result, then enable a feathered mask | Target area remains noisy | Mask boundary or over-smoothed protected feature becomes visible | Tune unmasked, reduce Amount, then refine mask |

These are conservative source-derived starting ranges, not measured quality
recommendations for a particular camera or dataset.

## Inputs, outputs, and metadata

- A still produces one modified still with original width, height, channel count,
  and pixel type unless a converted component reconstructs RGB. G'MIC works in
  float and clamps to `[0,1]`; double precision is not retained internally.
- The result keeps the source image's header cards, astrometric solution, and
  other metadata for every Channel choice. No processing-state flag is changed,
  so a linear or nonlinear label is inherited rather than newly characterized.
- Image History records **Wavelet Denoise**, Threshold, Scales, Iterations, Amount,
  and, for a colour image, ` in channel: <channel>`. It does not record the random shifts, mask, preview
  area, display stretch, G'MIC version, clamp, or actual dimension-capped scale.
- A snapshot creates an auxiliary still and leaves the target unchanged. Apply
  writes no user-selected file directly.
- Sequence Apply captures one parameter snapshot for all frames, processes into a
  temporary sequence, honors configured CFA auto-debayer on read, and replaces
  the source only after success. Cancellation removes the temporary output.

## Performance

The dominant cost grows with cycle count, scale count, channels, and padded pixel
count. Each cycle performs a forward/inverse multiscale Haar transform plus shifts
and accumulation. AfterNight runs the command as one G'MIC call per run, not split
across threads, so different sequence frames are expensive even when general
sequence infrastructure has parallel stages.

High Scales can round each dimension up to a large `2^(scales+1)` multiple. For
example, scale `10` requires 2,048-sample multiples, so memory may jump rather than
grow smoothly. RGB, component conversion, Amount, masks, Preview presentation,
and snapshots add full-image buffers/passes. Use Preview area and fewer cycles for
coarse tuning, then restore the intended cycles for the final comparison. No
production timing was measured.

## Worked example

- **Evidence mode:** Source-and-calculator-derived; not executed on
  project-owned astronomical data.
- **Input condition:** Assume the robust estimate is `sigma=0.020`, Threshold
  `1.40`, Scales `6`, Iterations `10`, and Amount `0.50`.
- **Coefficient checkpoint:** `T=1.40×0.020=0.028`. A coefficient `0.020` becomes
  `0`; a coefficient `0.050` becomes `0.022`; a coefficient `-0.050` becomes
  `-0.022` in every cycle before reconstruction.
- **Blend checkpoint:** If one reconstructed pixel is `d=0.36` and source is
  `f=0.40`, Amount `0.50` yields `0.38` before mask blending.
- **Inspect in real use:** 100% smooth background, smallest stars/filaments,
  high-contrast edges, all image boundaries, RGB balance, and Image History.
- **Limitations:** The numbers demonstrate the current shrink/blend equations
  only. They do not predict the robust estimate, random shifts, reconstructed
  pixels, artifacts, quality, or runtime for real data.

## Warnings and limitations

!!! warning "Iterations is not convergence"
    **Iterations** is cycle-spinning count, not convergence. Preview, snapshot,
    and Apply run the command again with the same fixed-seed shifts, so the same
    input and settings give the same result.

- The UI exposes one threshold for all scales and the coarse approximation; this
  is not a layer-by-layer wavelet denoise control.
- G'MIC uses one robust noise estimate for the supplied image. RGB channels are
  transformed independently with the same absolute coefficient threshold.
- More Iterations does not inherently mean more smoothing despite the current
  tooltip. It averages more shifted reconstructions at approximately linear cost.
- High Scales can affect large structures, force large zero-boundary padding, and
  produce block/ringing/edge behavior. Haar is discontinuous and particularly
  sensitive to sharp steps; cycle spinning reduces but does not remove this risk.
- All output is clamped to `[0,1]` and processed at float precision. Out-of-range
  float data and double-precision details are not preserved.
- An image holding NaN or infinite samples is refused; without G'MIC in the
  build, the process cannot run and Preview and Apply stay disabled.
- Component filtering can distort circular hue/opponent encodings and, in an
  integer image, clip reconstructed RGB at its type's range (a floating image
  keeps values outside `[0, 1]`). RGB thresholding can also shift color when channel
  noise/structure differs.
- Denoising is destructive after Apply and cannot recover clipped/calibration
  information. It does not update the image's linear/nonlinear processing state.

## Troubleshooting

| Symptom or exact error | Verified cause/validation path | Recovery | Evidence source |
| --- | --- | --- | --- |
| Increasing Iterations mostly slows processing | It is a fixed averaging count, not a convergence or repeated-strength loop | Tune Threshold/Scales/Amount for strength; use Iterations for shift averaging | `WaveletDenoiseProcess.cpp` (the `denoise_haar` command); G'MIC `cycle_spinning` |
| Broad structure or mean level changes | Soft threshold applies to the complete Haar coefficient array, including coarse approximation | Reduce Threshold and/or Scales; restore detail with lower Amount | vendored `threshold` and `denoise_haar` commands |
| Block patterns or damaged edges | Decimated Haar blocks, temporary zero-boundary dimension extension, and finite cycle sampling | Reduce Scales/Threshold, add cycles, use a feathered mask, or compare another denoiser | vendored Haar/resize/shift commands |
| Output clips out-of-range float samples | Every G'MIC result is clamped before restoring source type | Normalize/crop range before filtering and preserve a duplicate | `GmicProcessSupport.cpp` |
| Error: the image holds a sample that is not a finite number | The image has a NaN or infinite sample, which G'MIC cannot process | Repair or crop the non-finite samples, then retry; the image was left unchanged | `GmicProcessSupport.cpp` |
| Preview and Apply are disabled; tooltip says the build carries no G'MIC library | This build was made without G'MIC | Use a build that includes G'MIC, or another denoiser | `GmicProcessSupport.cpp` |
| A G'MIC error on a tiny target | Requested/UI-minimum Haar depth can exceed the dimension-derived supported depth | Use a larger image/crop or fewer supported scales where possible | vendored `denoise_haar`; Haar validation |
| Cancelled sequence leaves original unchanged | Temporary output is removed and commit is skipped | Expected; restart Apply when ready | shared sequence executor; G'MIC adapter abort flag |

## Related workflow steps

Finish [Image Stacking](../stacking-integration/image-stacking.md),
[Background Extraction](../background-extraction/background-extraction.md), and
[Photometric Color Calibration](../color-calibration/photometric-cc.md) or
[Spectrophotometric Color Calibration](../color-calibration/spectrophotometric-cc.md)
before linear denoise. Compare the variational model in
[Total Variation Denoise](total-variation-denoise.md), the learned external model
in [DeepSNR](deepsnr.md), and the later patch methods in
[NL-Bayes](nl-bayes.md) or [Patch PCA Denoise](patch-pca-denoise.md). Continue to
[Histogram Transformation](../stretching/histogram-transformation.md) or another
controlled stretch, then verify [Image History](../../interface/image-history.md),
[Image Statistics](../../interface/image-statistics.md), and critical metadata.

## References and provenance

- [G'MIC `denoise_haar` reference](https://gmic.eu/reference/denoise_haar.html) —
  upstream command contract for Haar thresholding, scale selection, and cycle
  spinning. AfterNight bundles G'MIC 3.5.4; the exact vendored command is authoritative for this build.
- [G'MIC Haar analysis and synthesis tutorial](https://gmic.eu/oldtutorial/wavelet-analysis-and-synthesis-in-gmic.html)
  — upstream background for the separable Haar transform.
- AfterNight and vendored G'MIC project sources —
  authoritative for the robust estimate, soft threshold, random shifts,
  dimension/boundary behavior, float/clamp bridge, channels, Amount/masks,
  preview, metadata, history, cancellation, and sequences.
