---
kind: process
stage: noise
menu: Process > Noise Reduction > Non-Local Bayes Filter
summary: >-
  Reduces noise by grouping similar nearby patches, estimating a statistical model for each group and combining two
  Bayesian estimates.
data_in: linear
derived: true
---
# NL-Bayes

## Purpose and expected result

NL-Bayes reduces noise by finding nearby patches with similar structure,
estimating a local Gaussian model for each patch group, and aggregating two
Bayesian estimates. It is intended for a calibrated master whose repeated
textures, stars, and edges provide useful non-local matches. Optional
homogeneous-area shortcuts, repeated strengthened/subtractive passes, an
Anscombe transform for shot-noise-like data, and a DA3D refinement change the
estimator and its cost.

The expected result is a same-size, same-type still or replacement sequence with
less noise while repeated structures remain recognizable. This is an expensive,
adaptive process: an incorrect Sigma changes patch sizes, group counts, search
windows, covariance shrinkage, and artifacts together. Excessive filtering can
make backgrounds patchy or waxy, repeat nearby structure, shift color, or soften
small stars.

## At a glance

| Item | Current behavior |
| --- | --- |
| Menu path | **Process > Noise Reduction > Non-Local Bayes Filter** |
| Enabled when | An image or sequence is active, the same process is not already attached to it, and no attached process holds exclusive access |
| Inputs | One grayscale or RGB still or sequence; a still CFA mosaic is treated as one grayscale sample plane unless debayered first; an optional active mask must match target geometry |
| Outputs | Destructively replaces a still or, after successful temporary processing, a sequence; **Take snapshot** creates an auxiliary still; dimensions, channels, and pixel type are retained unless a selected component reconstructs RGB; no file is written directly |
| Recommended image state | Usually linear, after calibration/registration/stacking and gradient/color correction but before stretching; normalize deliberately when manual Sigma or Anscombe assumptions are used |
| Preview support | Asynchronous RT preview with the same captured parameters, channel conversion, Amount, and mask blend as Apply; selected-area preview and display-only autostretch are available |
| Sequence support | Supported through the generic temporary replacement-sequence path with per-frame processing, progress, and cooperative cancellation; configured CFA auto-debayer can produce RGB frames |
| Progress and cancellation | Progress advances as each pass over the frame completes (two per SOS iteration, plus one for DA3D); Cancel is checked between tiles and iterations, and a cancelled run writes nothing |
| Mask support | Supported after denoising for preview, still Apply, and sequence Apply; white applies, black protects, gray blends, and **Invert Mask** reverses weights |
| External dependencies | None; the AfterNight engine's own patch denoiser, carried over from the published NL-Bayes and DA3D implementations; no separate installation, model, catalog, or network service |

## Before you start

- Finish calibration, registration, stacking, gradient correction, and color
  calibration first. Patch matching can preserve or repeat residual walking
  noise, registration defects, gradients, or compression artifacts.
- Use a finite master whose samples lie between zero and full scale. The engine
  states Sigma on a `[0,255]` tuning scale whose `255` is the image's full scale
  (`1` for a float image, the type's span for an integer one), and holds the
  result inside `[0, full scale]`: a negative sample, such as one left by a
  background subtraction, comes back as `0`, and a float sample above `1` comes
  back as `1`. Integer output is rounded to its original type.
- Start with **Automatic Sigma Estimation** unless you have measured Sigma in
  those `[0,255]` tuning units. A normalized standard deviation `s` corresponds
  to approximately `255s`; a normalized `0.02` is about `5.1`.
- Treat **Anscombe Variance-Stabilizing Transform** as a specialized option, not
  a general “better denoise” switch. The current path assumes unit-gain pure
  Poisson data after internal rescaling; it does not use camera gain, offset, or
  read-noise metadata.
- Duplicate the target or verify Image History. Apply replaces the still only
  after success; a sequence is processed into temporary storage and committed
  only after every frame succeeds.

## Procedure

1. Select a calibrated master and open **Process > Noise Reduction > Non-Local
   Bayes Filter**. Confirm **Target Image**, choose **RGB** or a deliberate
   component, and leave **Preview** selected.
2. Begin with **Automatic Sigma Estimation**, **1° Step Homogeneous Area** on,
   **2° Step Homogeneous Area** off, DA3D off, Anscombe off, **SOS Iterations:
   1**, **SOS Rho: 0.80**, and **Amount: 1.00**.
3. Use **Preview area** around both smooth background and fine stars or filaments.
   Check patch repetition, blotching, edge changes, and color at 100%.
4. If automatic strength is unsuitable, clear **Automatic Sigma Estimation** and
   tune Sigma in `[0,255]` units. Lower it when detail softens; raise it only
   while random noise falls without structured artifacts.
5. Compare the two homogeneous-area stages one at a time. Enable multiple SOS
   passes only after a stable single pass; lower Rho or the pass count if later
   passes become unstable. Use DA3D only as a separately inspected final
   refinement. Use Anscombe only for a compatible shot-noise case.
6. Use Amount to blend original signal back. Toggle **Switch image** under the
   same display stretch, inspect all channels and boundaries, then select
   **Apply**. Progress advances as each pass over the frame completes. For a
   sequence, monitor progress; Cancel preserves the original.

## Complete parameter and action reference

| UI label/action | Type and unit | Runtime default | Persisted-default exception | Valid values, step, or constraints | Effect of increasing, decreasing, or selecting | Dependencies and enable/visibility rules | Quality, artifact, runtime, memory, or disk trade-off | Scope: preview/apply, channel, mask, still/sequence | Persistence |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Sigma | Double value plus slider, noise standard deviation in `[0,255]` tuning units, where `255` is full scale | `5.0000` | None | `0.0010..50.0000`; four decimals; spin step `0.1`; the slider maps 1,000 intervals across the range, about `0.0500` per interval | Manual Gaussian-noise scale; also selects hidden patch/group/search profiles and covariance shrinkage | Disabled while Automatic Sigma is checked; under Anscombe the NL-Bayes pass re-estimates transformed noise, so this value is ignored there; DA3D still receives this numeric value | Higher values usually smooth more and use different/larger hidden neighborhoods; wrong values can erase detail or leave noise | Preview/Apply; selected channel/component; still/sequence | Captured per execution; Reset and successful Apply return `5` |
| Automatic Sigma Estimation | Checkbox | Checked | None | Checked/unchecked | Uses the clipped row-difference estimate for each NL-Bayes/SOS input instead of Sigma | Disables Sigma control; does not make DA3D use the estimate—the current DA3D call still uses the hidden Sigma value | Adapts per frame/pass but can be biased by horizontal texture, small dimensions, non-finite values, or nonstationary noise | Preview/Apply; current selected image/component; re-estimated per sequence frame | Captured per execution; Reset and successful Apply check it |
| 1° Step Homogeneous Area | Checkbox | Checked on a newly opened process | Reset does not restore it | Checked/unchecked | In stage 1, replaces a low-variance patch group with its per-channel mean instead of inverting its covariance | Independent of stage 2 option | Can accelerate/flatten smooth regions; may remove faint low-contrast structure classified as homogeneous | Preview/Apply; selected channel/component; still/sequence | Captured per execution; Reset preserves current state; successful Apply retains it |
| 2° Step Homogeneous Area | Checkbox | Unchecked | Reset does not restore it | Checked/unchecked | Applies the same shortcut in stage 2, testing noisy-group variance and filling the basic group with per-channel means | Independent of stage 1 option | Stronger smooth-area simplification can reduce noise and covariance work but increase flat patches or lost faint signal | Preview/Apply; selected channel/component; still/sequence | Captured per execution; Reset preserves current state; successful Apply retains it |
| Data Adaptive Dual Domain Denoise (DA3D) | Checkbox | Unchecked | Reset does not restore it | Checked/unchecked | Runs the DA3D refinement after NL-Bayes, using the original working image as noisy input and NL-Bayes result as guide | Disabled and execution-forced off under Anscombe; current Automatic Sigma does not feed DA3D—its sigma is the disabled/manual Sigma value (`5` initially) | Adds large adaptive Fourier-patch buffers and time; can refine detail/staircasing but can introduce separate frequency/patch artifacts | Preview/Apply; selected channel/component; still/sequence; before Amount/mask | Captured per execution; Reset preserves current state; successful Apply retains it |
| Anscombe Variance-Stabilizing Transform | Checkbox | Unchecked | Reset does not restore it | Checked/unchecked | Applies a unit-gain forward square-root transform before NL-Bayes and a closed-form inverse afterward | Disables DA3D and SOS controls; execution forces DA3D off and SOS to `1`; transformed noise is re-estimated even if Automatic Sigma is off | Intended for Poisson-like shot noise; model mismatch, low-count inverse bias, and clamping can change levels/detail | Preview/Apply; selected channel/component; still/sequence | Captured per execution; Reset preserves current state; successful Apply retains it |
| SOS Iterations | Integer spin box, passes | `1` | None | `1..8`, step `1`; eight is the most the engine runs | Repeats a strengthened/subtractive NL-Bayes pass; automatic noise is re-estimated for each input | Disabled under Anscombe; execution clamps to at least `1` | Cost and full-image buffers scale with passes; later passes can amplify artifacts or diverge, though a 1% noise-increase guard keeps the last converging result | Preview/Apply; selected channel/component; still/sequence | Captured per execution; Reset and successful Apply return `1` |
| SOS Rho | Double value plus slider, mixing fraction | `0.80` | None | `0.01..0.99`; two decimals; spin step `0.05`; effective slider/spin resolution `0.01` | Weights prior output in the next strengthened input; values near `1` amplify the subtractive correction | Disabled under Anscombe; irrelevant when SOS Iterations is `1`, though still recorded | Higher values can change faster but make division by `1-rho` large and increase divergence/artifact risk | Preview/Apply; selected channel/component; still/sequence | Captured per execution; Reset and successful Apply return `0.80` |
| Amount | Double value plus slider, blend fraction | `1.00` | None | `0.00..1.00`; two decimals; spin step `0.05`; effective slider/spin resolution `0.01` | Blends `source × (1-Amount) + denoised × Amount` | Enabled while idle; `0` still runs the complete algorithm before restoring source | Lower values recover signal and noise together; non-unit values add a full-image blend | Preview/Apply after channel reconstruction and before mask; still/sequence | Captured per execution; Reset and successful Apply return `1` |
| Target Image | Image selector | Attached/active target | None | Open eligible image or sequence | Changes source, Channel list, Preview, active mask, and Apply destination | Active image required; duplicate/exclusive gates apply | Wrong target is destructively replaced after success | Preview/Apply source and active mask | Target lifecycle; Reset does not change it |
| Channel | Combo box | **RGB** for RGB; disabled **Grayscale** for mono | None | **RGB**, **Red**, **Green**, **Blue**, **CIE L* (Lightness)**, **CIE a***, **CIE b***, **CIE c***, **CIE h***, **CIE X**, **CIE Y (Luminance)**, **CIE Z**, **HSL Hue**, **HSL Saturation**, **HSL Lightness**, **HSV Hue**, **HSV Saturation**, **HSV Value**, **YCbCr Luma**, **YCbCr Blue Chroma**, **YCbCr Red Chroma** | RGB runs the three-channel opponent/joint model; a stored/component choice runs the mono profile and reconstructs/copies other components | Component choices require RGB; mono selector disabled | Component conversions add buffers and can shift/clip color; RGB shares one averaged automatic Sigma and joint second-stage covariance | Preview/Apply; active mask post-blend; still/sequence | Captured by current selection; Reset does not restore RGB; successful Apply retains selection |
| Preview | Checkbox | Checked | None | Checked/unchecked | Enables/disables asynchronous full-image or selected-area execution | Process idle | Runs the full patch search; repeated previews can be expensive | Preview only; mask-aware; current still/current displayed sequence frame | Window lifetime; Reset does not change it |
| Preview pane | Image viewer | Current result | None | Pan with scroll bars/dragging; toolbar actions below | Shows mask-aware result without modifying target | Exists only with Preview | Holds source/result and display buffers in addition to algorithm workspaces | Preview only | Window lifetime |
| Fit To Window | Preview-toolbar action | Preview fitted when first displayed | None | Click or **Ctrl+Shift+F** | Fits the whole image uniformly in the viewport | Preview image required | Good overview; resampling can hide fine artifacts | Preview display only | Viewer lifetime |
| Zoom In | Preview-toolbar action | Current scale | None | Click; `1.25×` relative zoom within viewer limits | Enlarges around viewport center | Preview image required | Improves local inspection; more panning | Preview display only | Viewer lifetime |
| Zoom Out | Preview-toolbar action | Current scale | None | Click; `0.8×` relative zoom within viewer limits | Reduces around viewport center | Preview image required | More context, less fine detail | Preview display only | Viewer lifetime |
| Zoom 100% | Preview-toolbar action | Current scale | None | Click; absolute scale `1.0` | Shows the viewer's one-pixel scale | Preview image required | Preferred for judging residual noise, patch artifacts, and stars | Preview display only | Viewer lifetime |
| View Channel | Preview-toolbar combo box | **RGB** for RGB; disabled **Grayscale** for mono | None | **RGB**, **Red**, **Green**, **Blue**, **CIE L* (Lightness)**, **CIE a***, **CIE b***, **CIE a*=R / b*=G (Chrominance)**, **CIE c***, **CIE h***, **CIE X**, **CIE Y (Luminance)**, **CIE Z**, **HSL Hue**, **HSL Saturation**, **HSL Lightness**, **HSV Hue**, **HSV Saturation**, **HSV Value**, **HSI Hue**, **HSI Saturation**, **HSI Intensity**, **YCbCr Luma**, **YCbCr Blue Chroma**, **YCbCr Red Chroma** | Selects only the displayed stored/derived component | Separate from process Channel; RGB target required for components | Can reveal or hide channel artifacts without changing pixels | Preview display only | Viewer lifetime; not in process history |
| Display Mode | Preview-toolbar combo box | Inherits target **Linear** or **Autostretch** when preview is created | None | **Linear**, **Autostretch** | Changes display only; Autostretch uses cached full-target source statistics | Autostretch enables settings action | Aggressive display stretch can exaggerate noise/artifacts | Preview display only | Preview-view state; not in process history |
| Autostretch Settings | Preview-toolbar dialog action | Disabled in Linear mode; inherits target-view settings | Shared-view values load when opened | Opens Target Background, Shadows Clipping, linked channels, neutralization/color calibration, saturation/background, brightness, contrast, banner, Reset, and Close; **Apply Stretch** is hidden | Tunes preview display only | Autostretch mode only; disabled while dialog open | Can change visual judgment without changing denoise | Preview display only | Viewer-scoped; not in NL-Bayes parameters |
| Switch image | Preview-toolbar action | Denoised preview shown | None | Click; **Space**; right-click; **Ctrl+Shift+O** | Toggles result/original | Preview required | Comparison only; no recomputation | Preview display; still/current frame | Preview-window lifetime |
| Preview area | Preview-toolbar checkbox | Unchecked | None | Checked/unchecked; move/resize overlay | Processes a selected area and composites it over original preview | Preview required | Reduces tuning cost; full Apply remains full-frame | Preview and Take snapshot; mask cropped consistently | Preview-window lifetime; Reset unchanged |
| Take snapshot | Preview-toolbar action | No snapshot | None | Click or **Ctrl+Shift+K** | Re-executes settings and opens `<target> - Non-Local Bayes - SNAPSHOT` | Preview required | Adds a full execution and auxiliary image; display stretch not baked in | New still; target unchanged; area/mask honored | Each action creates a separate image view |
| Apply | Button/action | Enabled | None | One action, including Amount `0` | Captures all parameters and processes the selected channel for a still or every sequence frame | Target must lock; controls disabled during commit | Patch search/covariance work dominates; SOS/DA3D/components/masks add time and memory | Destructive still/sequence replacement after success | Records partial history, then calls Reset; optional modes, Target, Channel, Preview, and preview-toolbar state remain |
| Reset | Button/action | Enabled while idle | None | One action | Restores Sigma `5`, Automatic Sigma on, SOS `1`, Rho `0.8`, and Amount `1` | Does not reset either Homogeneous Area checkbox, DA3D, Anscombe, Target, Channel, Preview, or preview toolbar | Checked Preview schedules another execution | Configuration only | Immediate; also invoked after successful Apply |
| Close | Button/action | Enabled while idle | None | One action | Closes without applying current settings | Disabled during Apply | Active preview cancellation is requested and reaches the denoiser between tiles; pending result discarded | Window/execution | No settings persist after destruction |
| Sequence progress / Cancel | Shared per-process/frame progress and Cancel | Created for sequence Apply | None | Cooperative Cancel, checked between tiles and iterations; frame/task text updates during temporary processing | Sequence target only | Decode/encode, temporary disk, and per-frame patch work add cost | Sequence Apply; captured parameters, channel, Amount, and mask apply to every frame | Cancel removes temporary output; success replaces sequence, records one history step, and calls Reset |

## How the algorithm works

### Conceptual pipeline

1. AfterNight reads the selected stored image or component into planar
   single-precision samples scaled so that full scale is `255`. Automatic Sigma
   measures horizontal row differences.
2. Stage 1 converts RGB to an orthonormal luminance/opponent space, selects the
   closest patches by luminance distance, estimates a separate covariance per
   channel, filters every group, and overlap-averages patches into a basic image.
3. Stage 2 finds patches using all channels of that basic image, estimates one
   joint color covariance, filters the corresponding noisy patches, and
   overlap-averages the final estimate. Optional homogeneous-area shortcuts use
   group means instead of the covariance estimate.
4. SOS can feed a strengthened/subtractive result through the two stages again.
   Anscombe wraps the first/only pass; DA3D can refine the final estimate. The
   engine holds the result inside `[0,255]`, scales it back, and writes the
   source type; AfterNight then applies Amount, component reconstruction, mask
   blending, and metadata copying.

### Automatic Sigma and hidden patch profile

For channel `c`, row `r`, and column `x`, the estimator forms horizontal
differences

\[
\Delta_{c,r,x}=v_{c,r,x}-v_{c,r,x-1}.
\]

Each row is clipped three times at five population standard deviations around
its current mean. If `s_{c,r}` is the surviving row standard deviation, the
reported working-scale estimate is

\[
\hat\sigma=\frac{1}{C\sqrt 2}\sum_{c=1}^{C}
\operatorname{median}_r(s_{c,r}),
\]

where `C` is channel count. This is not a median-absolute-deviation estimator.
It is robust to some row outliers but can include horizontal real structure and
averages unequal RGB channel noise into one value.

Sigma also selects a hidden implementation profile. Mono stage-1 patches are
`5×5` below Sigma `30`, then `7×7`; RGB stage-1 patches are `3×3`, `5×5`, or
`7×7` across thresholds `20` and `50`. Stage 2 has its own thresholds. The
number of kept patches, reference-patch stride, odd search-window width, overlap
boundary, and covariance regularization are derived from that profile rather
than exposed separately.

### Patch groups and two Bayesian estimates

For a reference patch, stage 1 computes squared Euclidean distance in the first
opponent (luminance) plane throughout its search window and keeps exactly the
profile's `N` closest positions. Let centered patch vectors be `y_i-m` and their
sample covariance be `C_y`. The current estimate is

\[
\hat x_i=m+\left(I-\beta\sigma^2 C_y^{-1}\right)(y_i-m),
\]

where `beta` is the hidden profile regularizer. If matrix inversion fails, the
centered noisy group is retained before its mean is restored.

Stage 2 measures full multi-channel distance on the basic estimate and keeps all
search-window patches below the larger of a fixed profile threshold and the
`N`th distance. With basic-image covariance `C_b`, basic mean `m_b`, and centered
noisy patch `y_i-m_b`, it computes

\[
\hat x_i=m_b+\left[I-\beta\sigma^2
  (C_b+\beta\sigma^2I)^{-1}\right](y_i-m_b).
\]

All estimated patches contribute with equal overlap counts. Pixels with no
estimate fall back to the noisy image in stage 1 and the basic image in stage 2.
The frame is processed whole, in tiles whose size follows from the image's
extent and the stage's boundary - never from the number of processor cores - so
the result is identical however many threads run it. Tiles use reflected
overlap boundaries and discard their overlap when reassembled.

For RGB, the first stage uses an orthonormal luminance and two opponent channels;
similar positions are chosen from luminance but all three channel covariances are
filtered separately. The basic image is converted back to RGB before stage 2,
whose matching and covariance are joint across all three channels. A selected
single component instead uses the mono profile.

### Homogeneous areas, SOS, Anscombe, DA3D, and Amount

The homogeneous shortcut compares patch-group sample variance with
`1.05 sigma^2` (with the stage-1 channel sum scaled consistently). Below that
threshold, the group is replaced by its per-channel mean. This avoids a matrix
inverse but deliberately removes within-group variation.

Let original working image be `f`, the current SOS state be `q_k`, and UI Rho be
`rho`. Before pass `k`, the implementation supplies

\[
u_k=\rho q_k+(1-\rho)f
\]

to NL-Bayes. Between non-final passes, denoised result `d_k` becomes

\[
q_{k+1}=\frac{d_k-\rho u_k}{1-\rho}.
\]

Automatic Sigma is re-measured on `u_k`. If its measured noise is more than 1%
above the previous pass, iteration stops and retains the previous pre-subtraction
denoised result. Rho near `1` magnifies the subtraction denominator. At most
eight passes run; the dialog offers no more.

Anscombe rescales the data, applies `z=2 sqrt(max(x+3/8,0))`, re-estimates noise,
runs one NL-Bayes pass, and applies the closed-form low-count inverse.
The current call supplies zero Gaussian mean/read noise and unit gain, so the
generalized function is used as a pure Poisson model. DA3D instead uses the
original noisy image and NL-Bayes guide for shape/data-adaptive Fourier shrinkage
with large reflected patches; the current call uses the engine's defaults and the
numeric Sigma control.

Finally, for source `f`, denoised result `d`, and Amount `a`, AfterNight computes

\[
b=(1-a)f+ad,\qquad 0\le a\le1,
\]

then applies the active mask as another source/result blend.

### Boundary and failure behavior

- The denoiser accepts only one, three, or four planes. The normal process
  paths provide mono or RGB. A frame narrower or shorter than one search window
  at the noise level the estimator runs at is refused with an error naming the
  search window, and the image is left as it was; DA3D additionally needs a
  frame at least one of its transforms across.
- Outer/tile overlap is reflected. Uncovered patches fall back to the noisy or
  basic image; estimated overlaps are averaged with equal integer counts.
- Every type is processed at single precision on the `[0,255]` tuning scale and
  held inside `[0, full scale]`: negative samples become `0`, and float samples
  above `1` become `1`. Integer output is rounded; double input does not keep
  double precision. Out-of-range data is therefore not lossless.
- A NaN or infinity is passed to the estimator. With Automatic Sigma it can make
  the measured noise level non-finite, and the run is then refused; repair
  non-finite samples first rather than relying on the result.
- Cancellation is checked between tiles, SOS passes, and DA3D work, and between
  sequence frames. A cancelled or failed run writes nothing: a still is not
  committed, and sequence temporary output is removed before commit.

## Parameter interactions and starting points

| Combination | Conservative starting point | Too weak looks like | Too strong looks like | Adjustment order |
| --- | --- | --- | --- | --- |
| Automatic Sigma + Amount | Automatic on, Amount `1` for diagnosis | Random grain remains after a conservative estimate | Small stars/texture soften or background becomes patchy | Validate estimate/result, tune manual Sigma only if needed, then reduce Amount |
| Sigma + hidden patch profile | Stay near measured noise; cross profile thresholds cautiously | Patches remain noisy and covariance shrinkage is weak | Abrupt change in texture/detail near Sigma thresholds | Tune on one representative area and re-check after crossing `20`, `30`, or `50` |
| Homogeneous stages | Stage 1 on, stage 2 off | Smooth areas retain grain | Faint nebulosity flattens or smooth areas become unnaturally uniform | Compare stage 1, then stage 2 separately, then adjust Sigma/Amount |
| SOS Iterations + Rho | `1`; if repeating, try `2` at `0.5..0.8` | Single-pass residual remains | Noise/structure grows, guard stops early, or subtractive halos appear | Establish one pass, add one pass, then change Rho away from `1` |
| Anscombe + Sigma/DA3D/SOS | Anscombe off unless shot-noise model is justified | Signal-dependent shot noise remains | Levels/bias/detail change under model mismatch | Normalize/validate noise model, enable Anscombe, then retune Amount; DA3D/SOS are unavailable |
| DA3D + Automatic Sigma | DA3D off; if enabled, set Sigma deliberately even when Auto is on | NL-Bayes artifacts or residuals remain | Fourier/patch artifacts, color shifts, or excessive smoothing | Choose explicit Sigma, validate NL-Bayes, then toggle DA3D and reduce Amount if needed |
| RGB + unequal channel noise | Compare RGB and a luminance/chroma component on a duplicate | Noisiest channel remains | Cleaner channels lose detail or reconstructed color shifts (and, in an integer image, clips) | Inspect channels, choose mode, re-estimate/tune Sigma, then apply mask |
| Mask + Amount | Validate global result, then use a feathered mask | Intended area remains noisy | Mask boundary or protected structure looks detached | Tune unmasked, reduce Amount, then refine mask |

These are source-derived starting points, not measured recommendations for a
particular camera or dataset.

## Inputs, outputs, and metadata

- A still produces one modified still of the same width, height, channels, and
  pixel type unless component reconstruction yields RGB. The engine works in
  single precision; integer output is rounded and double input does not retain
  double working precision.
- The result keeps the source image's header cards and metadata. No
  linear/nonlinear processing-state flag is changed, so the existing state is
  inherited rather than newly characterized.
- Image History records **Non-Local Bayes**, Sigma or `auto`, both homogeneous
  flags, DA3D, Anscombe, effective SOS/Rho, Amount, and, for a colour image,
  ` in channel: <channel>`. It does
  not record measured Sigma, hidden patch profile, DA3D defaults, mask, preview
  area, display stretch, early SOS stopping, input offset, or engine revision.
- **Take snapshot** creates an auxiliary still and leaves the target unchanged.
  Apply writes no user-selected file directly.
- Sequence Apply captures one immutable parameter snapshot for all frames but
  automatic Sigma is measured independently per frame/pass. It writes a
  temporary replacement sequence, applies configured CFA auto-debayer on read,
  and commits only after success. Cancellation removes temporary output.

## Performance

Patch distance searches, covariance construction/inversion, aggregation, and
full-image working copies dominate. Cost grows with pixels, channels, Sigma's
hidden patch/group/search profile, SOS pass count, and DA3D. RGB stage 2 inverts
a joint covariance whose dimension is `patch_width² × channels`; larger patches
therefore increase matrix work and per-thread scratch nonlinearly.

The engine divides each stage into reflected-overlap tiles fixed by the image's
size - a tile's interior is at least four boundaries or 256 samples across - and
processes them on the threads the operation is granted. It takes its working
memory from the session before the first patch: multiple full single-precision
images, tile buffers, aggregation weights, and patch/covariance buffers; SOS and
the RT preview add more full-frame storage. DA3D adds noisy/guide/output,
weight-map, Fourier-patch, and tiling buffers. A run that would exceed the
session's memory budget is refused before it starts. Use Preview area, one SOS
pass, DA3D off, and a representative duplicate for tuning. No production timing
or peak-memory measurement was made.

## Worked example

- **Input condition:** Suppose a normalized mono crop has median clipped row
  difference standard deviation `7.21/255`. Automatic Sigma reports about
  `7.21/sqrt(2)=5.10` tuning units. Use SOS `2`, Rho `0.8`, Amount `0.5`.
- **Noise checkpoint:** This estimate selects the mono stage-1 `5×5` profile and
  supplies Sigma `5.10` to covariance shrinkage. It does not predict the actual
  patch matches or output.
- **SOS checkpoint:** If source sample `f=100` and first-pass denoised sample is
  `d_0=96`, the subtractive state is `q_1=(96-0.8×100)/0.2=80`, so the next input
  is `u_1=0.8×80+0.2×100=84`. The complete image, not this isolated sample,
  determines the noise guard and next denoised result.
- **Amount checkpoint:** If final denoised sample is `d=94`, Amount `0.5` produces
  `0.5×100+0.5×94=97` before mask blending.
- **Inspect in real use:** Smooth background, faint filaments, smallest stars,
  repeated texture, edges/tile seams, every RGB channel, Image History, and
  metadata at 100%.
- **Limitations:** These values demonstrate current scale, SOS input, and blend
  equations only. They do not claim a visual result, quality improvement,
  convergence, runtime, or memory use on real data.

## Warnings and limitations

!!! warning "Automatic Sigma and optional refinements are not one shared model"
    Automatic Sigma controls the NL-Bayes pass, but current DA3D receives the
    numeric Sigma control even while it is disabled. Anscombe re-estimates noise
    after its transform and therefore ignores manual Sigma for NL-Bayes. Record
    these mode choices when reproducing a result.

- **Reset** does not reset either homogeneous-area checkbox, DA3D, or Anscombe.
  Reopen the process for constructor defaults or clear those options manually.
- Automatic Sigma uses clipped horizontal differences, not a camera noise model
  or robust multiscale estimator. Real horizontal texture and unequal channels
  can bias it; sequence frames can choose different hidden profiles.
- Sigma thresholds change patch dimensions, group counts, and search windows
  discontinuously. A small Sigma adjustment can therefore change more than
  shrinkage strength.
- Anscombe assumes a fixed unit-gain Poisson model and forces one SOS pass with
  DA3D off. It does not incorporate sensor gain/read noise/offset metadata.
- DA3D uses the engine's default 64-pixel Fourier patches and the numeric Sigma
  control. Its current bridge has no separate radius/threshold/profile controls.
- The first RGB stage groups by an opponent luminance and the second uses joint
  RGB covariance. This can preserve shared structure but can also transfer or
  shift color when noise/structure differs between channels.
- Component filtering can distort circular hue/opponent encodings, clip an
  integer image's reconstructed RGB at its type's range (a floating image's
  keeps values outside `[0, 1]`), and increase buffers. CFA should
  normally be debayered before a color-aware denoise.
- The process does not update linear/nonlinear state. Strong denoise is
  destructive after Apply and cannot recover clipped or calibration information.

## Troubleshooting

| Symptom | Cause | Fix |
| --- | --- | --- |
| Sigma cannot be edited | Automatic Sigma is checked | Clear **Automatic Sigma Estimation**; remember the value remains DA3D's sigma even while disabled |
| DA3D or SOS controls are disabled | Anscombe is checked and execution forces DA3D off/SOS `1` | Clear Anscombe for those modes |
| Manual Sigma appears ineffective under Anscombe | The transformed input noise is always re-estimated before NL-Bayes | Tune Amount or clear Anscombe; do not use Sigma as an Anscombe strength control |
| DA3D result is unexpectedly strong with Auto Sigma | DA3D uses numeric Sigma, initially `5`, not measured automatic Sigma | Clear Auto, set Sigma deliberately, compare DA3D off/on, then reduce Amount |
| Reset leaves optional modes checked | Reset initializes only Sigma, Auto, SOS, Rho, and Amount | Clear Homogeneous Area/DA3D/Anscombe manually or reopen process |
| Background becomes flat/patchy | Homogeneous shortcut, high Sigma/profile, repeated SOS, or DA3D classified/suppressed real variation | Disable stage 2 then stage 1, reduce Sigma/SOS, turn off DA3D, lower Amount |
| Color shifts or fine chroma disappears | Shared automatic Sigma, opponent/joint RGB model, or component reconstruction affects channels differently | Compare RGB and luminance/chroma paths on a duplicate; reduce Sigma/Amount |
| Error says the noise level needs a frame at least one search window across | The frame or Preview area is narrower than the search window the Sigma selects | Use a larger target or Preview area, or a lower Sigma; the image is left unchanged |
| SOS Iterations stops at `8` | Eight strengthen-and-subtract passes is the engine's limit | Expected; more passes would mostly add time |
| Negative background becomes zero, or float highlights above `1` are capped | The result is held inside `[0, full scale]` | Rescale or offset the image into `[0,1]` before denoising |
| Non-finite input is refused or produces unreliable output | Noise estimation reads NaN/Inf | Repair NaN/Inf first |
| Cancelled sequence leaves original unchanged | Temporary output is removed and commit skipped | Expected; restart Apply when ready |

## Related workflow steps

Finish [Image Stacking](../stacking-integration/image-stacking.md),
[Background Extraction](../background-extraction/background-extraction.md), and
[Photometric Color Calibration](../color-calibration/photometric-cc.md) or
[Spectrophotometric Color Calibration](../color-calibration/spectrophotometric-cc.md)
before linear denoise. Compare direct scale control in
[Wavelet Denoise](wavelet-denoise.md), a gradient-penalty model in
[Total Variation Denoise](total-variation-denoise.md), an all-neighbor local
eigenspace method in [Patch PCA Denoise](patch-pca-denoise.md), or the separately
installed learned model in [DeepSNR](deepsnr.md). Continue to
[Histogram Transformation](../stretching/histogram-transformation.md), then
verify [Image History](../../interface/image-history.md),
[Image Statistics](../../interface/image-statistics.md), and critical metadata.

## Credits

- [Lebrun, Buades, and Morel, “Implementation of the Non-Local Bayes Image
  Denoising Algorithm”](https://www.ipol.im/pub/art/2013/16/) — primary
  algorithm article, detailed implementation, and source provenance for patch
  grouping and two Gaussian estimates.
- [Romano and Elad, “Boosting of Image Denoising Algorithms”](https://epubs.siam.org/doi/10.1137/140990978)
  — primary SOS method background. AfterNight implements the exact recurrence
  and divergence guard documented above.
- [Pierazzo and Facciolo, “Data Adaptive Dual Domain Denoising: a Method to Boost State of the Art Denoising Algorithms”](https://www.ipol.im/pub/art/2017/203/)
  — primary DA3D algorithm and implementation provenance.
- [Mäkitalo and Foi, “A Closed-Form Approximation of the Exact Unbiased Inverse of the Anscombe Variance-Stabilizing Transformation”](https://doi.org/10.1109/TIP.2011.2121085)
  — primary inverse-transform reference.
- The engine's patch estimator is adapted from Marc Lebrun's implementation
  published with the first article, under GPL-3.0-or-later;
  **About > About AfterNight** credits it.
