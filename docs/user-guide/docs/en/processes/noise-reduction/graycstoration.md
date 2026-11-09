---
kind: process
stage: noise
menu: Process > Noise Reduction > GRAYCstoration Filter
summary: >-
  Smooths along the image's own structure with G'MIC's anisotropic diffusion, reducing noise while keeping edges and
  filaments.
data_in: linear
derived: true
---
# GRAYCstoration

## Purpose and expected result

GRAYCstoration performs edge-directed smoothing through the bundled G'MIC
`smooth` command. It estimates a shared diffusion-tensor field from local image
structure, follows that field along curved paths, and averages samples along
those paths. Compared with an isotropic blur, it can smooth more strongly along
coherent edges than across them.

The expected result is a same-size, same-type still or replacement sequence with
weaker fine variation and better retention of coherent edges than a similarly
strong ordinary blur. It does not recover detail. Strong or repeated settings
can erase small stars, bend or extend thin structures, create flowing/smeared
texture, and shift color.

## At a glance

| Item | Current behavior |
| --- | --- |
| Menu path | **Process > Noise Reduction > GRAYCstoration Filter** |
| Enabled when | An image or sequence is active, the same process is not already attached to it, and no attached process holds exclusive access |
| Inputs | One grayscale or RGB still or sequence; a still CFA mosaic is treated as one grayscale plane unless debayered first; an optional active mask must match target geometry |
| Outputs | Destructively replaces a still or, after successful temporary processing, a sequence; **Take snapshot** creates an auxiliary still; dimensions, channel count, and source pixel type are retained; no file is written directly |
| Recommended image state | Usually finite, normalized linear data after calibration, registration, stacking, gradient correction, and color calibration; judge any small-star loss at 100% scale |
| Preview | Asynchronous full-image or movable-area preview, original/result switching, snapshot, display-channel selection, and display-only autostretch |
| Sequence support | Yes; one captured parameter/channel/mask state is applied frame by frame to temporary storage before replacement |
| Mask support | Yes; the active mask blends the result with the original after Amount blending |
| External dependency | Bundled G'MIC/CImg runtime, run through AfterNight's optional G'MIC adapter; no separate installation or network access. In a build without G'MIC, Preview and Apply are disabled and their tooltip says why |
| Non-finite samples | An image holding a NaN or infinite sample is refused before G'MIC sees it, and left unchanged |

## Before you start

- Repair NaN and infinity before processing. An image holding a sample that is
  not a finite number is refused with "the image holds a sample that is not a
  finite number, which G'MIC cannot process" and left unchanged.
- This process needs G'MIC, which AfterNight normally includes. In a build
  without it, **Preview** and **Apply** are disabled and their tooltip reads
  "This build of AfterNight carries no G'MIC library, so this process cannot
  run."
- Prefer normalized data. The filtered result is clamped to `[0,1]`, including
  for float and double input. **Amount** `1` replaces the source with that
  clamped result; partial Amount blends it with the original samples, and
  Amount `0` restores the original samples after G'MIC has run.
- Include smooth background, faint stars, thin filaments, a bright edge, and a
  corner in the Preview area. Compare at **Zoom 100%** under one Display Mode.
- Duplicate the target or verify Image History. Apply replaces the target only
  after the whole still operation or temporary sequence succeeds.
- Start with one iteration. Each additional iteration rebuilds the tensor field
  from the previous result and runs the full smoothing operation again.

## Procedure

1. Select a finite, normalized target and open **Process > Noise Reduction >
   GRAYCstoration Filter**. Confirm **Target Image**, choose **RGB** or a
   deliberate component, and leave **Preview** selected.
2. Keep **Iterations 1**, **Amplitude 60**, and **Amount 1.00** initially. Use
   **Sharpness** and **Anisotropy** to control how strongly coherent edges steer
   smoothing.
3. Tune **Noise Scale** for the feature scale used to estimate gradients and
   **Regularity** for the spatial coherence of the tensor field. Inspect both
   fine stars and broad structure after each change.
4. Leave **Spatial Step Size 0.80**, **Angular Step Size 30**, **Precision 2.00**,
   **Nearest Neighbor**, and **Fast Approximation** selected until the structural
   behavior is acceptable. These are numerical quality/cost controls, not extra
   strength controls.
5. Toggle **Switch image** and inspect at **Zoom 100%**. Reduce Amplitude,
   Iterations, or Amount if stars shrink or texture begins to flow along edges.
6. Check the full frame, mask, target, and Channel before selecting **Apply**.
   For a sequence, monitor frame progress; Cancel reaches the running G'MIC
   command through its abort flag, preserves the original, and removes
   temporary output.

## Complete parameter and action reference

| UI label/action | Type and unit | Runtime default | Persisted-default exception | Valid values, step, or constraints | Effect of increasing, decreasing, or selecting | Dependencies and enable/visibility rules | Quality, artifact, runtime, memory, or disk trade-off | Scope: preview/apply, channel, mask, still/sequence | Persistence |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Iterations | Integer value plus slider, complete passes | `1` | None; process settings are not persisted across dialog instances | `1..10`; step `1` | Repeats the complete `smooth` command; every pass estimates a new tensor field from the previous output | Eligible target; no additional enable/visibility dependency | Cost is approximately linear in Iterations; repeated passes can grow flat or flowing regions; Preview/Apply, selected Channel, still/sequence; Reset and successful Apply return `1` | Preview and Apply; selected Channel and mask; still or sequence | Dialog-local; Reset and successful Apply restore the documented runtime default |
| Amplitude | Integer value plus slider, G'MIC smoothing amplitude | `60` | None; process settings are not persisted across dialog instances | `0..100`; step `1` | Controls streamline integration scale through `sqrt(2 × Amplitude)`; `0` makes each G'MIC pass an identity average | Eligible target; no additional enable/visibility dependency | Larger values integrate farther and cost more; Amount/mask are applied later; Reset and successful Apply return `60` | Preview and Apply; selected Channel and mask; still or sequence | Dialog-local; Reset and successful Apply restore the documented runtime default |
| Sharpness | Double value plus slider | `0.70` | None; process settings are not persisted across dialog instances | `0.00..2.50`; spin step `0.01`; slider resolution `0.001` | Raises tensor attenuation with local structure strength; larger values suppress diffusion more strongly around detected variation | Eligible target; no additional enable/visibility dependency | Very high values can preserve noise/fragment smoothing; `0` is internally floored to `1e-5`, nearly removing structure-based attenuation; Reset/Apply return `0.70` | Preview and Apply; selected Channel and mask; still or sequence | Dialog-local; Reset and successful Apply restore the documented runtime default |
| Noise Scale | Double value plus slider, pixels in tensor estimation | `0.60` | None; process settings are not persisted across dialog instances | `0.00..3.00`; spin step `0.05`; slider resolution `0.001` | G'MIC's `alpha`: Gaussian preblur before gradients and global normalization | Eligible target; no additional enable/visibility dependency | Larger values stop very fine variation from steering the tensor but can also ignore small real structure; Reset/Apply return `0.60` | Preview and Apply; selected Channel and mask; still or sequence | Dialog-local; Reset and successful Apply restore the documented runtime default |
| Regularity | Double value plus slider, pixels in tensor estimation | `1.10` | None; process settings are not persisted across dialog instances | `0.00..3.00`; spin step `0.05`; slider resolution `0.001` | G'MIC's `sigma`: Gaussian blur of the structure-tensor field | Eligible target; no additional enable/visibility dependency | Larger values make orientation vary more smoothly over space and can carry direction across nearby features; Reset/Apply return `1.10` | Preview and Apply; selected Channel and mask; still or sequence | Dialog-local; Reset and successful Apply restore the documented runtime default |
| Spatial Step Size | Double value plus slider, pixels along integration paths | `0.80` | None; process settings are not persisted across dialog instances | `0.10..1.00`; spin step `0.01`; slider resolution `0.001` | G'MIC's `dl`: distance advanced per streamline sample after tensor normalization | Eligible target; no additional enable/visibility dependency | Smaller steps sample paths more densely and cost more; large steps are coarser and can miss curvature/detail; this positive range avoids the separate explicit-Laplacian mode; Reset/Apply return `0.80` | Preview and Apply; selected Channel and mask; still or sequence | Dialog-local; Reset and successful Apply restore the documented runtime default |
| Anisotropy | Double value plus slider | `0.30` | None; process settings are not persisted across dialog instances | `0.00..1.00`; spin step `0.05`; slider resolution `0.001` | Separates the tensor eigenvalue attenuation; `0` is locally isotropic, while values toward `1` strongly suppress cross-structure motion relative to along-structure motion | Eligible target; no additional enable/visibility dependency | High values can create directional streaks or extend thin features; Reset/Apply return `0.30` | Preview and Apply; selected Channel and mask; still or sequence | Dialog-local; Reset and successful Apply restore the documented runtime default |
| Amount | Double value plus slider, blend fraction | `1.00` | None; process settings are not persisted across dialog instances | `0.00..1.00`; spin step `0.01`; slider resolution `0.001` | Shared post-filter blend: `original × (1-Amount) + smoothed × Amount` | Eligible target; no additional enable/visibility dependency | Amount `0` restores the original samples, including out-of-range float/double values, but the full G'MIC call and filtered-result clamp still occur before blending; partial Amount mixes the original with the clamped result; active mask blends afterward; Reset/Apply return `1.00` | Preview and Apply; selected Channel and mask; still or sequence | Dialog-local; Reset and successful Apply restore the documented runtime default |
| Precision | Double value plus slider, Gaussian support multiplier | `2.00` | None; process settings are not persisted across dialog instances | `0.10..10.00`; spin step `0.10`; slider resolution `0.001` | G'MIC's `gauss_prec`: integration length is `Precision × local_sigma` | Eligible target; no additional enable/visibility dependency | Larger values extend paths and increase work; exact weighting captures more Gaussian tail, while Fast Approximation adds more uniform samples; Reset/Apply return `2.00` | Preview and Apply; selected Channel and mask; still or sequence | Dialog-local; Reset and successful Apply restore the documented runtime default |
| Angular Step Size | Integer value plus slider, degrees | `30` | None; process settings are not persisted across dialog instances | `1..90`; step `1` | Samples orientations around 360 degrees at this spacing | Eligible target; no additional enable/visibility dependency | Smaller values use more directions and cost roughly in proportion to `360 / step`; larger values are coarser and can show directional bias; Reset/Apply return `30` | Preview and Apply; selected Channel and mask; still or sequence | Dialog-local; Reset and successful Apply restore the documented runtime default |
| Interpolation | Combo box | **Nearest Neighbor** | None; process settings are not persisted across dialog instances | **Nearest Neighbor**, **Linear**, **Runge-Kutta** | Chooses path/sample evaluation: nearest samples; bilinear field/image samples; or midpoint second-order Runge-Kutta path integration with linear image samples | Eligible target; no additional enable/visibility dependency | Linear and Runge-Kutta reduce blocky path stepping at extra cost; does not change output type; Reset/Apply return Nearest Neighbor | Preview and Apply; selected Channel and mask; still or sequence | Dialog-local; Reset and successful Apply restore the documented runtime default |
| Fast Approximation | Checkbox | Checked in the live process | Raw UI builder starts unchecked; live process initialization sets checked; no cross-dialog persistence | Checked/unchecked; the bare UI-builder test observes unchecked before process initialization | Checked uses equal weights for accepted path samples; unchecked uses `exp(-distance² / (2 sigma²))` Gaussian weights | Eligible target; no additional enable/visibility dependency | This changes the numerical result as well as speed; unchecked emphasizes nearer samples and evaluates exponentials; Reset/Apply check it | Preview and Apply; selected Channel and mask; still or sequence | Dialog-local; Reset and successful Apply restore the documented runtime default |
| Target Image | Image selector | Attached/active target | None; process settings are not persisted across dialog instances | Open eligible image or sequence | Changes source, Channel list, Preview, active mask, and Apply destination | At least one eligible image or sequence; launch requires an active image | Wrong target is destructively replaced after success; Reset retains target | Preview and Apply destination; still or sequence | Dialog-local; Reset retains the current selection/state |
| Channel | Combo box | **RGB** for RGB; disabled **Grayscale** for mono | None; process settings are not persisted across dialog instances | **RGB**, **Red**, **Green**, **Blue**, **CIE L* (Lightness)**, **CIE a***, **CIE b***, **CIE c***, **CIE h***, **CIE X**, **CIE Y (Luminance)**, **CIE Z**, **HSL Hue**, **HSL Saturation**, **HSL Lightness**, **HSV Hue**, **HSV Saturation**, **HSV Value**, **YCbCr Luma**, **YCbCr Blue Chroma**, **YCbCr Red Chroma** | RGB builds one structure tensor from all stored channels and smooths channel values along the shared paths; a component filters one plane and reconstructs/copies the others | Target channel count; mono targets force and disable Grayscale | Component conversions add buffers and can alter color; captured per execution; Reset retains selection | Preview and Apply; selected component; still or sequence | Dialog-local; Reset retains the current selection/state |
| Preview | Checkbox | Checked | None; process settings are not persisted across dialog instances | Checked/unchecked | Enables asynchronous full-image or selected-area execution | Eligible target; work is scheduled only while checked | Runs the complete G'MIC command; selected area reduces tuning work; Reset retains state | Preview only; still or sequence source | Dialog-local; Reset retains the current selection/state |
| Preview pane | Image viewer | Current result | None; process settings are not persisted across dialog instances | Pan/scroll; toolbar actions below | Shows mask-aware result without modifying target | Preview execution or original display buffer | Holds source/result/display buffers plus G'MIC/tensor workspaces; Preview only | Preview/display only | Preview-view lifetime only |
| Fit To Window | Preview-toolbar action | Fitted initially | None; process settings are not persisted across dialog instances | Click or **Ctrl+Shift+F** | Fits whole image uniformly | Preview pane | Display only; resampling can hide fine loss; viewer lifetime | Preview/display only | Preview-view lifetime only |
| Zoom In | Preview-toolbar action | Current scale | None; process settings are not persisted across dialog instances | Click; `1.25×` relative zoom | Enlarges around viewport center | Preview pane | Display only; viewer lifetime | Preview/display only | Preview-view lifetime only |
| Zoom Out | Preview-toolbar action | Current scale | None; process settings are not persisted across dialog instances | Click; `0.8×` relative zoom | Reduces around viewport center | Preview pane | Display only; viewer lifetime | Preview/display only | Preview-view lifetime only |
| Zoom 100% | Preview-toolbar action | Current scale | None; process settings are not persisted across dialog instances | Click; absolute scale `1.0` | Shows one-pixel viewer scale | Preview pane | Preferred for stars, streaking, and texture; display only | Preview/display only | Preview-view lifetime only |
| View Channel | Preview-toolbar combo box | **RGB** for RGB; disabled **Grayscale** for mono | None; process settings are not persisted across dialog instances | Stored RGB plus CIE Lab/LCh/XYZ, HSL, HSV, HSI, and YCbCr display components | Selects only the displayed component | Preview pane and target channel count | Separate from process Channel; display only; viewer lifetime | Preview/display only | Preview-view lifetime only |
| Display Mode | Preview-toolbar combo box | Inherits target **Linear** or **Autostretch** | None; process settings are not persisted across dialog instances | **Linear**, **Autostretch** | Changes display only; Autostretch uses cached full-target source statistics | Preview pane | A strong stretch can exaggerate residual noise/streaks; never changes pixels | Preview/display only | Preview-view lifetime only |
| Autostretch Settings | Preview-toolbar dialog action | Disabled in Linear mode; inherits target-view settings | None; process settings are not persisted across dialog instances | Opens display stretch controls; **Apply Stretch** is hidden | Tunes preview display only | Preview pane; enabled only when Display Mode is Autostretch | Viewer-scoped, not GRAYCstoration parameters | Preview/display only | Preview-view lifetime only |
| Switch image | Preview-toolbar action | Smoothed result shown | None; process settings are not persisted across dialog instances | Click; **Space**; right-click; **Ctrl+Shift+O** | Toggles result/original | Preview pane | Comparison only; no recomputation; preview lifetime | Preview/display only | Preview-view lifetime only |
| Preview area | Preview-toolbar checkbox | Checked with initial `500×500` area | None; process settings are not persisted across dialog instances | Checked/unchecked; move/resize overlay | Checked processes the selected area and composites it over the original preview; unchecked processes full image | Checked Preview and eligible target | Preview and snapshot scope; full Apply is always full target; Reset retains state | Preview/display only | Preview-view lifetime only |
| Take snapshot | Preview-toolbar action | No snapshot | None; process settings are not persisted across dialog instances | Click or **Ctrl+Shift+K** | Re-executes settings and opens `<target> - GRAYCstoration - SNAPSHOT` | Checked Preview and eligible target | Adds a complete G'MIC execution and auxiliary still; area/mask honored; target unchanged | Auxiliary still from preview scope; selected Channel and mask | Preview-view lifetime only |
| Apply | Button/action | Enabled while idle, including Amount/Amplitude `0` | None; process settings are not persisted across dialog instances | One action | Captures all numerical settings and Amount, then processes the full still or every sequence frame | Eligible target and idle process | Controls lock during commit; destructive replacement after success; records history but omits Fast Approximation; successful Apply resets every numerical setting, Interpolation, and Fast Approximation | Full-target Apply; selected Channel and mask; still or sequence | Successful Apply resets all 12 process settings |
| Reset | Button/action | Enabled while idle | None; process settings are not persisted across dialog instances | One action | Restores the 12 process settings to live defaults | Idle process | Does not reset Target, Channel, Preview, mask, or preview toolbar; checked Preview schedules execution | Open process dialog only | Immediate dialog-local reset; no cross-dialog persistence |
| Close | Button/action | Enabled while idle | None; process settings are not persisted across dialog instances | One action | Closes without applying current settings | Idle process | Disabled during Apply; requests active preview cancellation; no process settings persist after destruction | Open process dialog only | Unapplied process settings are discarded |
| Sequence progress / Cancel | Shared per-frame progress and Cancel | Created for sequence Apply | None; process settings are not persisted across dialog instances | Cooperative Cancel | Applies one captured setting/channel/mask state to every frame in temporary storage | Sequence target during Apply | G'MIC receives an abort flag and polls internally; Cancel/error removes temporary output, success replaces sequence and records one history step | Sequence Apply only | Execution snapshot only; no cross-dialog persistence |

## How the algorithm works

AfterNight checks the selected image or component for non-finite samples and
sends it through its G'MIC adapter, one G'MIC session per run, to the bundled
`smooth` command as planar float32 fractions of full scale - a tightly packed
float image in place, anything else as a working copy. With **Iterations**
above `1` the command is `repeat <Iterations> { smooth ... }` in that one call.
G'MIC first blurs a copy by **Noise Scale**,
normalizes that copy to `0..255`, computes a channel-coupled structure tensor,
and blurs the tensor field by **Regularity**. For local nonnegative tensor
eigenvalues `lambda1 <= lambda2`, Sharpness `s`, and Anisotropy `a`, the bundled
CImg code constructs square-root tensor strengths proportional to

\[
n_1=(1+\lambda_1+\lambda_2)^{-s/2},\qquad
n_2=(1+\lambda_1+\lambda_2)^{-s/[2(1-a)]}.
\]

At `a=0`, the two strengths match. As `a` approaches `1`, the second strength
falls much faster around detected structure, making smoothing strongly
directional. Sharpness is floored internally at `10^-5`, so displayed `0` is
nearly structure-independent rather than a singular value.

For each angular direction, CImg follows the tensor field from each pixel. With
local tensor-vector magnitude `n` and Amplitude `A`, it uses a local smoothing
scale

\[
\sigma_p=n\sqrt{2A}
\]

and follows the path up to `Precision × sigma_p`, advancing by **Spatial Step
Size**. The exact mode weights a sample at path distance `l` by
`exp(-l²/(2 sigma_p²))`; **Fast Approximation** assigns equal weight to every
accepted sample. Results are averaged over angular directions. Paths stop at the
image boundary, so edge support is truncated rather than padded or reflected.

Each `smooth` pass clips its result to that pass's finite input
minimum/maximum; AfterNight then clamps the command's result to `[0,1]` and
converts it back to the original pixel type, rounded to nearest and held to the
type's range. Thus the filtered leg does not create new
extrema for valid normalized input. The later Amount blend restores the
original samples at `0`, fully substitutes the clamped result at `1`, and can
retain some original out-of-range contribution between those endpoints. The
active mask is applied after the Amount blend.

### Stability, channels, and repeated passes

The exposed positive Angular Step Size selects CImg's line-integral-convolution
path, not its separate explicit oriented-Laplacian iteration mode. The result is
a normalized average bounded by the current input range; there is no exposed
PDE time step with a classical CFL limit. Numerical stability does not mean
artifact-free output: coarse spatial/angular sampling can bias paths, and strong
directionality can smear or extend structure.

RGB input contributes all channels to one structure tensor. All three values
then travel along the same spatial paths, which couples edge geometry across
color while still averaging each channel's samples separately. A selected
derived component uses one-plane tensor estimation and is reconstructed through
the shared Channel path. A derived component's reconstruction saturates an
integer image at its type's range and keeps a floating image's values outside
`[0, 1]`.

Iterations greater than one repeat the complete `smooth` pass inside one G'MIC
call, including tensor re-estimation and its range clip. They are not one
equivalent higher Amplitude pass and can progressively change the geometry that
steers later passes.

## Parameter interactions and starting points

- **Amplitude × Precision / Spatial Step Size × 360 / Angular Step Size** is the
  dominant sampling-cost relationship. Tensor magnitude changes it locally.
- **Noise Scale × Regularity** separates the pre-gradient scale from the spatial
  smoothing of the resulting orientation field. Raising both can make broad
  structures dominate small stars and texture.
- **Sharpness × Anisotropy** controls attenuation and directionality. High
  Sharpness with high Anisotropy can protect major edges while turning weaker
  structures into streaks.
- **Interpolation × Spatial Step Size** controls curved-path accuracy. A coarse
  step with Nearest Neighbor is fastest and most likely to show stepping.
- **Fast Approximation × Precision** changes more than runtime. Extra Precision
  adds equally weighted samples in fast mode but progressively smaller Gaussian
  weights in exact mode.
- **Iterations × Amount** is recursive filtering followed by one final blend.
  Amount does not soften each iteration and Amount `0` does not skip G'MIC.
- **Channel × mask** sets the operation order: filter/reconstruct, Amount blend,
  then active-mask blend.

## Inputs, outputs, and metadata

- Output dimensions and channel count are retained. All source types work through
  float32; conversion back preserves the source type but rounds integer data and
  reduces double precision.
- Every filtered G'MIC output is clamped to `[0,1]`. Full Amount replaces
  negative, HDR, or otherwise out-of-range source samples with that result;
  partial Amount blends the clamped result with the original samples.
- The result keeps the source image's header cards and metadata. GRAYCstoration
  does not update linear/nonlinear processing-state provenance.
- Image History records Interpolation, Iterations, Amplitude, Sharpness,
  Anisotropy, Noise Scale, Regularity, both step sizes, Precision, Amount, and,
  for a colour image, ` in channel: <channel>`. It omits **Fast Approximation**.
- A still CFA mosaic is processed as one plane. Sequence preprocessing may
  auto-debayer configured CFA frames before replacement.
- Sequence frames use one captured parameter/channel/mask state. Success replaces
  the original sequence; Cancel/error removes temporary output.
- **Take snapshot** creates a still from current preview scope without changing
  the target or baking display autostretch into pixels.

## Performance

For image width `W`, height `H`, channels `C`, and Iterations `I`, tensor
construction is proportional to `W × H × C × I`. Line integration adds a
data-dependent number of directions and path samples. Lower Angular Step Size,
lower Spatial Step Size, higher Precision, higher Amplitude, and disabling Fast
Approximation all increase cost. CImg uses OpenMP where enabled and the process
can occupy multiple CPU cores.

Memory includes the float image, blurred/tensor fields, line-integration
accumulators, destination and preview/component/mask buffers. Sequences also
need temporary disk roughly comparable to an output sequence. No approved
astronomical workload has been timed.

## Worked example

This support estimate follows current source equations. It is not an executed
image-quality or timing result. In a locally flat region where tensor-vector
magnitude is approximately `n=1`, defaults give

```text
local sigma = sqrt(2 × 60) = 10.95 pixels
path length = Precision 2 × 10.95 = 21.91 pixels
samples per direction ≈ ceil(21.91 / 0.80) = 28
directions at 30 degrees = 360 / 30 = 12
upper local sample count ≈ 28 × 12 = 336 per pixel and pass
```

Actual tensor magnitudes shorten paths around structure, boundaries truncate
them, and implementation overhead is not represented. At Amount `0.25`, a
normalized original value `0.20` and smoothed value `0.12` become
`0.20 × 0.75 + 0.12 × 0.25 = 0.18` before mask blending.

## Warnings and limitations

- GRAYCstoration is a structure-directed smoother, not a noise-model estimator.
  It does not infer shot/read noise or guarantee optimal denoising.
- Fine stars and real texture can steer the tensor as if they were edges or be
  averaged away as noise, depending on Noise Scale and Regularity.
- High Anisotropy, Amplitude, Precision, or Iterations can create curved streaks,
  extend thin structures, or produce a painted appearance.
- Coarse spatial/angular sampling and Nearest Neighbor can show directional or
  stair-step artifacts; more accurate settings cost substantially more.
- Boundary paths are truncated. Edge pixels use less/asymmetric support and can
  differ from the interior.
- RGB shares geometry across channels but averages channel values separately;
  color noise and strong single-channel edges can influence all tensor paths.
- The filtered result is clamped to `[0,1]`, even for float/double data.
  Amount `0` restores the original samples afterward; full or partial Amount
  can still alter out-of-range data, so repair or rescale it before processing.
- An image holding NaN or infinite samples is refused by name and left
  unchanged; without G'MIC in the build, the process cannot run and Preview and
  Apply stay disabled.
- Successful Apply resets all 12 process settings but retains Target, Channel,
  Preview, mask, and preview-toolbar state.
- History omits Fast Approximation, so it is not a complete reproducibility
  record for the current implementation.

## Troubleshooting

| Symptom | Likely cause | Check or correction | Evidence |
| --- | --- | --- | --- |
| Fine stars disappear | Scale/amplitude treats them as removable variation | Reduce Amplitude/Iterations; lower Noise Scale or Regularity; blend with Amount or a mask | Tensor construction and repeated-pass path |
| Texture becomes curved or painted | Directional paths dominate local detail | Reduce Anisotropy, Precision, Amplitude, or Iterations | Tensor eigenvalue and LIC paths |
| Result looks blocky or directionally biased | Path/angular sampling is coarse | Try Linear or Runge-Kutta, lower Spatial/Angular Step Size | CImg integration branches |
| Border differs from interior | Paths stop when they leave the image | Inspect full frame and protect/crop borders if necessary | LIC boundary test |
| Amount `0` is unchanged but still slow | G'MIC and the filtered-result clamp run before the shared Amount blend restores the original samples | Close or disable Preview when no processing is wanted | G'MIC run and modulation order |
| Error: the image holds a sample that is not a finite number | The image has a NaN or infinite sample, which G'MIC cannot process | Repair or crop the non-finite samples, then retry; the image was left unchanged | `GmicProcessSupport.cpp` |
| Preview and Apply are disabled; tooltip says the build carries no G'MIC library | This build was made without G'MIC | Use a build that includes G'MIC, or another denoiser | `GmicProcessSupport.cpp` |
| Fast Approximation result changes | It replaces Gaussian distance weights with equal sample weights | Disable it for Gaussian-weighted integration; expect more cost | CImg weighting branch |
| Colors smear together | Shared RGB tensor paths follow dominant multichannel structure | Try a deliberate luminance component and inspect chroma channels | Vector structure tensor and Channel dispatcher |
| Reset changes Fast Approximation to checked | Live process initialization overrides the raw UI-builder checkbox state | Treat checked as the runtime default | `initialize()` after UI construction |
| Cancel appears delayed | G'MIC cancellation is cooperative inside a large multi-core pass | Wait for the current polling point; use a smaller Preview area next time | G'MIC adapter abort flag |

## References and provenance

- [G'MIC `smooth` command reference](https://gmic.eu/reference/smooth.html)
  defines the anisotropic-smoothing arguments, interpolation choices, and
  upstream defaults AfterNight passes explicitly.
- [David Tschumperlé, “Fast Anisotropic Smoothing of Multi-Valued Images using Curvature-Preserving PDE's”](https://tschumperle.users.greyc.fr/publications/tschumperle_ijcv06.pdf)
  is the primary publication for the curvature-preserving, line-integral
  formulation behind this smoothing family.
- AfterNight's `GRAYCstorationProcess.cpp` and `GmicProcessSupport.cpp`, the
  G'MIC adapter, bundled `gmic.cpp`, and bundled `CImg.h` are authoritative for
  the exact script, float/clamp hand-off, tensor equations, integration,
  boundaries, and cancellation behavior shipped with this build.

## Related workflow steps

- [Wavelet Denoise](wavelet-denoise.md) separates detail by Haar scale rather
  than following a tensor field.
- [Total Variation Denoise](total-variation-denoise.md) minimizes a TV objective
  and can staircase instead of producing directional LIC streaks.
- [Bilateral Filter](bilateral-filter.md) uses local spatial/range weights with a
  fixed neighborhood rather than curvature-following paths.
- [Gaussian Blur](gaussian-blur.md) is an isotropic linear filter useful when
  edge direction should not steer smoothing.
- [Process Windows](../../interface/process-windows.md), [Image Viewer](../../interface/image-viewer.md),
  [Masks](../../interface/masks.md), and [Image History](../../interface/image-history.md)
  explain the shared controls and verification workflow.
