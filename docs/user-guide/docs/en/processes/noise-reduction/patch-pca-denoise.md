---
kind: process
stage: noise
menu: Process > Noise Reduction > Patch PCA Denoise Filter
summary: >-
  Reduces noise by projecting groups of similar patches onto their principal components and dropping the weakest ones.
data_in: linear
derived: true
---
# Patch PCA Denoise

## Purpose and expected result

Patch PCA Denoise reduces noise with a local, patch-based principal-component
filter. At regularly sampled reference positions it compares every patch in a
square lookup neighborhood with the reference patch, estimates a local
second-moment matrix, removes eigen-components below an automatically estimated
noise threshold, reconstructs all neighboring patches, and overlap-averages the
results.

The expected result is a same-size, same-type still or replacement sequence with
weaker random noise. Repeated local structure can help define strong components,
but the current implementation does not rank or reject patches by similarity:
**Lookup Size includes every position in the window**. Strong thresholds can
remove faint stars or texture; large neighborhoods can mix unrelated structure;
and sparse sampling can leave uneven or even uncovered output regions.

## At a glance

| Item | Current behavior |
| --- | --- |
| Menu path | **Process > Noise Reduction > Patch PCA Denoise Filter** |
| Enabled when | An image or sequence is active, the same process is not already attached to it, and no attached process holds exclusive access |
| Inputs | One grayscale or RGB still or sequence; a still CFA mosaic is treated as one grayscale sample plane unless debayered first; an optional active mask must match target geometry |
| Outputs | Destructively replaces a still or, after successful temporary processing, a sequence; **Take snapshot** creates an auxiliary still; dimensions, channels, and source pixel type are retained unless a selected component reconstructs RGB; no file is written directly |
| Recommended image state | Usually a finite, normalized linear master after calibration, registration, stacking, gradient correction, and color calibration but before stretching |
| Preview support | Asynchronous RT preview with the same captured parameters, channel conversion, and mask blend as Apply; selected-area preview and display-only autostretch are available |
| Sequence support | Supported through the generic temporary replacement-sequence path with per-frame processing, progress, and cooperative cancellation; configured CFA auto-debayer can produce RGB frames |
| Mask support | Supported after denoising for preview, still Apply, and sequence Apply; white applies, black protects, gray blends, and **Invert Mask** reverses weights |
| External dependencies | Bundled G'MIC 3.5.4 command `denoise_patchpca`, run through AfterNight's optional G'MIC adapter; no separate model, catalog, network service, or installation. In a build without G'MIC, Preview and Apply are disabled and their tooltip says why |
| Non-finite samples | An image holding a NaN or infinite sample is refused before G'MIC sees it, and left unchanged |

## Before you start

- Finish calibration, registration, stacking, gradient correction, and color
  calibration first. The lookup window uses every local patch, so walking noise,
  registration defects, gradients, hot pixels, and compression texture enter the
  local model rather than being identified as outliers.
- Use finite samples normalized to `[0,1]`. An image holding a NaN or infinite
  sample - such as the empty border of a registered frame - is refused with
  "the image holds a sample that is not a finite number, which G'MIC cannot
  process" and left unchanged. Integer inputs are converted into a float32
  G'MIC buffer as fractions of full scale; float inputs are processed in place
  and double inputs are reduced to float32 precision. Output is always clamped
  to `[0,1]` before conversion back to the original type, so negative or
  above-one float data is clipped even at Strength `0`.
- This process needs G'MIC, which AfterNight normally includes. In a build
  without it, **Preview** and **Apply** are disabled and their tooltip reads
  "This build of AfterNight carries no G'MIC library, so this process cannot
  run."
- Treat the live defaults as `4 / 7 / 11 / 7`, not the dormant builder values
  `1.8 / 7 / 8 / 7` or G'MIC's omitted-argument defaults. AfterNight explicitly
  supplies all four values and its constructor calls `initialize()` after the
  controls are connected.
- Tune on a representative **Preview area** containing background, fine stars,
  and a real edge. Patch Size and Lookup Size can increase work by orders of
  magnitude, while lower Spatial Sampling values make reference centers denser.
- Duplicate the target or verify Image History. Apply replaces a still only
  after success; a sequence is processed into temporary storage and committed
  only after every frame succeeds.

## Procedure

1. Select a finite normalized master and open **Process > Noise Reduction >
   Patch PCA Denoise Filter**. Confirm **Target Image**, select **RGB** or one
   deliberate component, and leave **Preview** selected.
2. Begin with live defaults: **Strength 4**, **Patch Size 7**, **Lookup Size 11**,
   and **Spatial Sampling 7**. Use a Preview area before running the full frame.
3. Judge at **Zoom 100%** under one Display Mode. Toggle **Switch image** and
   inspect smooth background, faint stars, edges, and each color channel.
4. Tune **Strength** first. Lower it if faint structure disappears; raise it only
   while residual random noise falls without flat or blocky patches.
5. Change **Patch Size** or **Lookup Size** one at a time. Larger values make the
   model less local and dramatically more expensive. Reduce **Spatial Sampling**
   only when denser estimation materially improves coverage or consistency.
6. Verify the full-frame preview, mask state, target, and Channel before selecting
   **Apply**. For a sequence, monitor frame progress; Cancel preserves the
   original and removes temporary output.

## Complete parameter and action reference

| UI label/action | Type and unit | Runtime default | Persisted-default exception | Valid values, step, or constraints | Effect of increasing, decreasing, or selecting | Dependencies and enable/visibility rules | Quality, artifact, runtime, memory, or disk trade-off | Scope: preview/apply, channel, mask, still/sequence | Persistence |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Strength | Double value plus slider, dimensionless noise multiplier | `4.00` | Builder starts at `1.80`, but the live process constructor immediately sets `4.00`; G'MIC omitted-argument default `1.8` is never used | `0.00..16.00`; two decimals; spin step `0.01`; slider has 1000 positions across the range | Sets the cutoff `Strength × estimated noise`; higher values retain fewer leading PCA components, while `0` retains all returned components | Always enabled while idle; noise is estimated automatically and is not exposed | Higher values smooth more and can erase faint structure; the slider resolves about `0.016`, coarser than the spin step | Preview/Apply; selected channel/component; still/sequence; before mask | Captured per execution; Reset and successful Apply return `4.00` |
| Patch Size | Integer value plus slider, pixels per side | `7` | None | `2..21`, step `1`; even sizes use asymmetric left/right extents | Sets each `N×N` patch and PCA dimension `N²`; larger patches model broader structure | Always enabled while idle; no validation against image dimensions | Covariance storage/work grows with `N⁴` and eigendecomposition approximately with `N⁶`; large values can mix detail and become extremely slow | Preview/Apply; selected channel/component; still/sequence | Captured per execution; Reset and successful Apply return `7` |
| Lookup Size | Integer value plus slider, patch positions per side | `11` | Builder starts at `8`, but the live process constructor immediately sets `11` | `2..21`, step `1`; all `L²` offsets are used, including the reference region | Expands or contracts the local window used to build the matrix and reconstruct neighboring patches; it does **not** select only similar patches | Always enabled while idle; no distance threshold or group-size control | Work grows approximately with `L²`; larger windows mix less-local structure and increase aggregate writes | Preview/Apply; selected channel/component; still/sequence | Captured per execution; Reset and successful Apply return `11` |
| Spatial Sampling | Integer value plus slider, reference-center stride in x and y | `7` | G'MIC omitted-argument default is `5`, but AfterNight explicitly supplies `7` | `1..16`, step `1`; a center runs only where both coordinates are divisible by this value | Higher values skip more centers and run faster; lower values produce denser estimates | Always enabled while idle; not automatically limited by Patch/Lookup Size | Approximate center count falls as `1/S²`; overly sparse support can produce uneven weights or uncovered black pixels, especially when `S > Patch Size + Lookup Size - 1` or at the far image edges | Preview/Apply; selected channel/component; still/sequence | Captured per execution; Reset and successful Apply return `7` |
| Target Image | Image selector | Attached/active target | None | Open eligible image or sequence | Changes source, Channel list, Preview, active mask, and Apply destination | Active image required; duplicate/exclusive gates apply | Wrong target is destructively replaced after success | Preview/Apply source and active mask | Target lifecycle; Reset does not change it |
| Channel | Combo box | **RGB** for RGB; disabled **Grayscale** for mono | None | **RGB**, **Red**, **Green**, **Blue**, **CIE L* (Lightness)**, **CIE a***, **CIE b***, **CIE c***, **CIE h***, **CIE X**, **CIE Y (Luminance)**, **CIE Z**, **HSL Hue**, **HSL Saturation**, **HSL Lightness**, **HSV Hue**, **HSV Saturation**, **HSV Value**, **YCbCr Luma**, **YCbCr Blue Chroma**, **YCbCr Red Chroma** | RGB sends all stored channels to G'MIC, which filters each channel independently with one shared noise threshold; a stored/component choice filters one plane and reconstructs/copies the others | Component choices require RGB; mono selector disabled | Independent RGB decisions can change color around weak detail; component conversions add buffers and may clip or shift color | Preview/Apply; active mask post-blend; still/sequence | Captured by current selection; Reset does not restore RGB |
| Preview | Checkbox | Checked | None | Checked/unchecked | Enables/disables asynchronous full-image or selected-area execution | Process idle | Runs the complete G'MIC filter; repeated large-patch previews can be very expensive | Preview only; mask-aware; current still/current displayed sequence frame | Window lifetime; Reset does not change it |
| Preview pane | Image viewer | Current result | None | Pan with scroll bars/dragging; toolbar actions below | Shows the mask-aware result without modifying the target | Exists only with Preview | Holds source/result and display buffers in addition to G'MIC workspaces | Preview only | Window lifetime |
| Fit To Window | Preview-toolbar action | Preview fitted when first displayed | None | Click or **Ctrl+Shift+F** | Fits the whole image uniformly in the viewport | Preview image required | Good overview; resampling can hide sparse-grid or patch artifacts | Preview display only | Viewer lifetime |
| Zoom In | Preview-toolbar action | Current scale | None | Click; `1.25×` relative zoom within viewer limits | Enlarges around viewport center | Preview image required | Improves local inspection; more panning | Preview display only | Viewer lifetime |
| Zoom Out | Preview-toolbar action | Current scale | None | Click; `0.8×` relative zoom within viewer limits | Reduces around viewport center | Preview image required | More context, less fine detail | Preview display only | Viewer lifetime |
| Zoom 100% | Preview-toolbar action | Current scale | None | Click; absolute scale `1.0` | Shows the viewer's one-pixel scale | Preview image required | Preferred for judging residual noise, black gaps, patch texture, and stars | Preview display only | Viewer lifetime |
| View Channel | Preview-toolbar combo box | **RGB** for RGB; disabled **Grayscale** for mono | None | **RGB**, **Red**, **Green**, **Blue**, **CIE L* (Lightness)**, **CIE a***, **CIE b***, **CIE a*=R / b*=G (Chrominance)**, **CIE c***, **CIE h***, **CIE X**, **CIE Y (Luminance)**, **CIE Z**, **HSL Hue**, **HSL Saturation**, **HSL Lightness**, **HSV Hue**, **HSV Saturation**, **HSV Value**, **HSI Hue**, **HSI Saturation**, **HSI Intensity**, **YCbCr Luma**, **YCbCr Blue Chroma**, **YCbCr Red Chroma** | Selects only the displayed stored/derived component | Separate from process Channel; RGB target required for components | Can reveal channel artifacts without changing pixels | Preview display only | Viewer lifetime; not in process history |
| Display Mode | Preview-toolbar combo box | Inherits target **Linear** or **Autostretch** when preview is created | None | **Linear**, **Autostretch** | Changes display only; Autostretch uses cached full-target source statistics | Autostretch enables settings action | Aggressive display stretch can exaggerate residual noise/artifacts | Preview display only | Preview-view state; not in process history |
| Autostretch Settings | Preview-toolbar dialog action | Disabled in Linear mode; inherits target-view settings | Shared-view values load when opened | Opens Target Background, Shadows Clipping, linked channels, neutralization/color calibration, saturation/background, brightness, contrast, banner, Reset, and Close; **Apply Stretch** is hidden | Tunes preview display only | Autostretch mode only; disabled while dialog open | Can change visual judgment without changing denoise | Preview display only | Viewer-scoped; not in Patch PCA parameters |
| Switch image | Preview-toolbar action | Denoised preview shown | None | Click; **Space**; right-click; **Ctrl+Shift+O** | Toggles result/original | Preview required | Comparison only; no recomputation | Preview display; still/current frame | Preview-window lifetime |
| Preview area | Preview-toolbar checkbox | Unchecked | None | Checked/unchecked; move/resize overlay | Processes a selected area and composites it over the original preview | Preview required | Safest way to reduce tuning cost; full Apply remains full-frame | Preview and Take snapshot; mask cropped consistently | Preview-window lifetime; Reset unchanged |
| Take snapshot | Preview-toolbar action | No snapshot | None | Click or **Ctrl+Shift+K** | Re-executes settings and opens `<target> - Patch PCA Denoise - SNAPSHOT` | Preview required | Adds a complete execution and auxiliary image; display stretch is not baked in | New still; target unchanged; area/mask honored | Each action creates a separate image view |
| Apply | Button/action | Enabled | None | One action | Captures four parameters and selected Channel, then processes the full still or every sequence frame | Target must lock; controls disabled during commit | Full-frame local matrices and eigensolves dominate; component conversion, mask, sequence encode, and temporary storage add cost | Destructive still/sequence replacement after success | Records partial history, then calls Reset; Target, Channel, Preview, and preview toolbar remain |
| Reset | Button/action | Enabled while idle | None | One action | Restores Strength `4`, Patch Size `7`, Lookup Size `11`, and Spatial Sampling `7` | Does not reset Target, Channel, Preview, mask, or preview toolbar | Checked Preview schedules another full execution | Configuration only | Immediate; also invoked after successful Apply |
| Close | Button/action | Enabled while idle | None | One action | Closes without applying current settings | Disabled during Apply | Active preview cancellation is requested and reaches the running command through G'MIC's abort flag; pending result discarded | Window/execution | No process settings persist after destruction |
| Sequence progress / Cancel | Shared per-process/frame progress and Cancel | Created for sequence Apply | None | Cooperative Cancel through G'MIC's abort flag and between frames; frame/task text updates during temporary processing; G'MIC reports no progress within a frame | Sequence target only | Decode/encode, temporary disk, and serial per-frame Patch PCA work add cost | Sequence Apply; captured parameters, Channel, and mask apply to every frame | Cancel removes temporary output; success replaces sequence, records one history step, and resets parameters |

## How the algorithm works

### Float bridge and automatic noise estimate

AfterNight runs the command through its G'MIC adapter, one G'MIC session per
run, and hands G'MIC 3.5.4 the planar samples as single-precision fractions of
full scale: a tightly packed float image in place, anything else - integer or
double input, or padded rows - as a working copy. Before the call it checks that
every sample is finite and refuses the image otherwise. The command's answer is
held to `[0,1]` and converted back to the image's type, rounded to nearest and
held to the type's range. The command estimates one noise scale before
processing channels:

\[
\hat\sigma=\frac{1.4826}{\sqrt{20}}
\operatorname{median}\left(
  \left|\Delta I-\operatorname{median}(\Delta I)\right|
\right),
\]

where `ΔI` is G'MIC's 2-D Laplacian. For an RGB image the image statistics cover
all stored channel samples, so the same scalar threshold is used while each
channel is filtered independently. The process exposes no manual noise value.
Real edges and undersampled stars can influence this robust estimate.

### Lookup neighborhood and PCA cutoff

Let Patch Size be `N`, Lookup Size be `L`, Spatial Sampling be `S`, and
`d=N²`. A reference center is evaluated only when both `x mod S` and `y mod S`
are zero. Its reference patch is the vector `X`. For every one of the `L²`
lookup offsets, the neighboring patch `Y_i` contributes

\[
M=\frac{1}{L^2}\sum_{i=1}^{L^2}(Y_i-X)(Y_i-X)^T.
\]

This is an uncentered second-moment matrix relative to `X`, not a conventional
mean-centered covariance and not a similarity-selected patch group. The code
eigendecomposes `M`, takes `lambda_j=sqrt(abs(mu_j))`, and retains the leading
returned components while

\[
\lambda_j \ge \text{Strength}\,\hat\sigma.
\]

All remaining coefficients are set exactly to zero. Each lookup patch is
reconstructed as

\[
\hat Y_i=X+QKQ^T(Y_i-X),
\]

where `K` is one for retained components and zero otherwise. Strength `0` keeps
every returned component, but the float bridge, overlap arithmetic, output
clamp, and type conversion can still change pixels.

### Aggregation, color, and boundaries

Every reconstructed lookup patch is drawn into a full-frame accumulation image
through a Gaussian patch mask. A separate weight image receives the same mask;
the final output divides accumulated samples by `max(weight,0.01)`. Patch reads
outside the image use Neumann/replicated boundaries, while writes outside the
image are clipped.

For RGB, the G'MIC formula runs in parallel over pixels and channels. It builds a
separate `N² × N²` matrix and cutoff reconstruction for each channel; there is
no joint color covariance. A shared noise threshold does not prevent different
components being retained in R, G, and B, so weak colored structure can shift.
A selected CIE/HSL/HSV/YCbCr component is converted, filtered as one plane, and
reconstructed through the shared Channel path. A derived component's
reconstruction saturates an integer image at its type's range and keeps a
floating image's values outside `[0, 1]`.

The weight floor does not restore source pixels. If reference centers and their
lookup patches do not cover a location, its accumulated value remains zero and
the output can be black there. Interior support from one center spans
`Patch Size + Lookup Size - 1` pixels per axis; therefore Spatial Sampling above
that span guarantees interior gaps, and right/bottom edge remainders can be
uncovered sooner. The live `7/11/7` geometry covers the ordinary interior, but
extreme allowed combinations require a full-frame check.

## Parameter interactions and starting points

- **Strength × estimated noise** is the only shrinkage threshold. A noisy or
  edge-dominated estimate changes every local cutoff.
- **Patch Size** changes both spatial support and matrix dimension. Going from
  `7` to `14` doubles patch width but makes the dense matrix 16 times larger and
  raises cubic eigensolver work by roughly 64 times before lookup work.
- **Lookup Size** does not make a similarity search more selective. Increasing
  it includes every additional local offset and can blend unrelated features.
- **Spatial Sampling** is a two-axis stride. Moving from `7` to `1` can create
  about 49 times as many reference centers away from boundaries.
- **Patch/Lookup/Sampling coverage** must be checked together. Fast sparse
  settings can trade consistent overlap for grid texture or zero-weight gaps.
- **Channel** changes the data model. RGB is three independent filters with one
  shared noise estimate; a derived component adds conversion/reconstruction and
  may affect color even though only one component was selected.

## Inputs, outputs, and metadata

- Output dimensions and source pixel type are retained. G'MIC works in float32,
  final samples are clamped to `[0,1]`, and integer output is rounded.
- The result keeps the source image's header cards and metadata.
- The process does not update linear/nonlinear processing-state provenance. Do
  not infer state only from the fact that denoising was applied.
- Still Image History records Strength, Patch Size, Lookup Size, Spatial
  Sampling, and, for a colour image, ` in channel: <channel>`.
- A still CFA mosaic is treated as one stored sample plane unless debayered
  first. For a sequence, shared preprocessing may auto-debayer configured CFA
  frames and the replacement sequence can therefore be RGB.
- Sequence frames use one captured parameter set, are processed to temporary
  storage, and replace the original sequence only after success. Cancel/error
  removes the temporary output.
- **Take snapshot** creates a still from the current preview scope. It does not
  change the target or bake the preview's display autostretch into pixel values.

## Performance

For image width `W`, height `H`, channels `C`, patch side `N`, lookup side `L`,
and sampling stride `S`, the approximate number of reference centers is
`WH/S²` per channel. At each center the dominant work is about
`O(L²N⁴ + N⁶)`: `L²` outer products and projections of `N²`-element vectors,
plus a dense eigendecomposition of the `N² × N²` matrix. These are scaling
guides, not measured timings. G'MIC's `*` formula evaluates in parallel, so CPU
core count and memory bandwidth also matter.

G'MIC holds the float input/result, a full-frame per-channel aggregation image,
a full-frame weight image, and per-thread patch, matrix, eigensystem, and
projection vectors. AfterNight also holds destination, preview, component, and
mask buffers as needed. Sequence mode adds decode/encode buffers and temporary
disk roughly comparable to an output sequence. No approved astronomical image
was timed and peak memory was not instrumented; use a Preview area and
avoid changing multiple size parameters upward together.

## Worked example

These source-and-calculator examples explain thresholds and scaling. They were
not executed on an approved astronomical dataset and make no quality claim.

Suppose the Laplacian estimator returns `sigma=0.020`. At live Strength `4`, the
component cutoff is

```text
cutoff = 4 × 0.020 = 0.080
```

If the leading returned component standard deviations are `0.120`, `0.070`,
and `0.030`, only the first remains. Strength `2` lowers the cutoff to `0.040`
and retains the first two; Strength `0` retains all returned components.

For default geometry, the matrix dimension is `7²=49`, it contains `49²=2401`
values, and each sampled center examines `11²=121` lookup patches. Changing only
Patch Size to `14` makes the dimension `196` and matrix contain `38,416` values.
Changing only Spatial Sampling from `7` to `1` can make reference centers about
49 times denser. These ratios omit parallel overhead, boundaries, previews, and
I/O.

## Warnings and limitations

- The UI builder's starting values `1.8/7/8/7` are not the live process's: the
  constructor, Reset, and post-Apply values are `4/7/11/7`.
- Lookup Size is labeled as a similar-patch search window, but the bundled
  command uses every one of its `L²` positions; it performs no distance ranking
  or rejection.
- Shrinkage is a hard cutoff. Strong values can remove faint stars, thin
  filaments, and low-contrast texture abruptly or create flat/patchy regions.
- RGB channels are filtered independently with one shared estimated noise value.
  Weak color detail can shift, and derived-component round trips can alter
  color and, in an integer image, clip at its type's range (a floating image
  keeps values outside `[0, 1]`).
- Spatial Sampling can create grid inconsistency or black zero-weight gaps for
  sparse parameter combinations and far edges. Always inspect a full-frame
  result at 100% after tuning on an area.
- Even patch and lookup sizes have asymmetric integer extents. Oversized windows
  are accepted even when larger than the image and rely on replicated boundary
  samples.
- Float/double values outside `[0,1]` are clamped on output; double input is
  processed at float32 precision; integer output is requantized.
- An image holding a NaN or infinite sample is refused by name and left
  unchanged; repair or crop non-finite samples first.
- Without G'MIC in the build, the process cannot run: Preview and Apply stay
  disabled.
- The Windows and Linux builds give slightly different results for the same
  image and settings: G'MIC's script weighs its patches with the platform's own
  mathematical library, and a last-bit difference there can change how many
  components a patch keeps. Each build gives the same result every time; do not
  compare a Windows result with a Linux one sample by sample.
- Apply automatically resets all four algorithm parameters after success. Target,
  Channel, Preview, mask, and preview-toolbar state remain.
- Cancellation is cooperative through G'MIC's abort flag. A costly internal
  matrix/eigen operation may delay the visible response; sequence Cancel is also
  checked between frames.

## Troubleshooting

| Symptom | Cause | Fix |
| --- | --- | --- |
| Dialog starts at `4/7/11/7`, not `1.8/7/8/7` | Live constructor overwrites builder values | Use the live values; Reset returns them too |
| Large black spots or a grid appear | Spatial Sampling leaves zero-weight pixels | Reduce Spatial Sampling; enlarge support carefully; inspect the full frame at 100% |
| Fine stars disappear abruptly | Strength threshold retains too few PCA components | Lower Strength and compare under the same stretch |
| Increasing Lookup Size changes structure rather than finding better matches | Every lookup position is included | Reduce Lookup Size around mixed structures; it is not a similarity-ranked group |
| Runtime jumps after a modest Patch Size change | Matrix dimension is Patch Size squared and eigensolve is dense | Return toward `7`; tune on Preview area |
| Colors shift | Per-channel PCA decisions or a component conversion round trip | Compare RGB and a deliberate luminance component on a duplicate; inspect View Channel |
| Negative highlights or values above one are clipped | The G'MIC result is held to `[0,1]` | Normalize a working copy before denoising |
| Error: the image holds a sample that is not a finite number | The image has a NaN or infinite sample, which G'MIC cannot process | Repair or crop the non-finite samples, then retry; the image was left unchanged |
| Preview and Apply are disabled; tooltip says the build carries no G'MIC library | This build was made without G'MIC | Use a build that includes G'MIC, or another denoiser |
| Preview differs from Apply | Preview area, mask, target/channel, or display stretch changed | Match all controls and use one Display Mode; Apply always processes the full target |
| Cancel appears delayed | G'MIC has not reached an abort check inside costly work | Wait for the current operation; use smaller patches/lookup or a smaller Preview area next time |

## Credits

- [G'MIC `denoise_patchpca` reference](https://www.gmic.eu/reference/denoise_patchpca.html)
  lists the command's four arguments and upstream omitted-argument defaults.
  AfterNight bundles G'MIC 3.5.4 and supplies all four arguments itself.
- [G'MIC mathematical expressions](https://gmic.eu/reference/mathematical_expressions.html)
  defines image median `ic`, matrix `eig()`, and the parallel `*` evaluation used
  by the bundled command.
- [G'MIC PCA reference](https://gmic.eu/reference/pca.html) describes the general
  eigenvector projection/reconstruction convention. Patch PCA Denoise uses its
  own local hard-cutoff implementation rather than invoking that command.
- [G'MIC project and JOSS citation](https://github.com/GreycLab/gmic) identifies
  the upstream framework, authorship, and primary project publication.

## Related workflow steps

- [NL-Bayes](nl-bayes.md) uses similarity-ranked patch groups and two Bayesian
  stages when a more explicitly non-local model is appropriate.
- [Wavelet Denoise](wavelet-denoise.md) exposes direct scale-by-scale control
  without a dense local eigensolve.
- [Total Variation Denoise](total-variation-denoise.md) uses iterative gradient
  regularization and has different edge/staircasing trade-offs.
- [Process Windows](../../interface/process-windows.md), [Image Viewer](../../interface/image-viewer.md),
  [Masks](../../interface/masks.md), and [Image History](../../interface/image-history.md)
  explain the shared controls and verification workflow.
