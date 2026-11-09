---
kind: process
stage: finish
menu: Process > Intensity Transformations > Local Histogram Equalization
summary: >-
  Remaps each pixel through the histogram of its own neighborhood, raising local contrast, with G'MIC.
data_in: nonlinear
data_out: nonlinear
derived: true
---
# Local Histogram Equalization

## Purpose and expected result

Local Histogram Equalization remaps each pixel through a cumulative distribution
function (CDF) built from that pixel's own neighborhood. Unlike CLAHE, it does
not divide the image into tiles, clip histogram bins, or interpolate between
tile mappings. Every pixel receives a separately computed local mapping.

The expected result is a same-size still or replacement sequence with stronger
local tonal separation. The shipped process always uses G'MIC's hidden **Soft**
mode and, for default RGB, operates on Lab lightness. It can reveal faint
structure, but it can also amplify noise, create local halos, and produce an
overprocessed soft-light appearance.

## At a glance

| Item | Current behavior |
| --- | --- |
| Menu path | **Process > Intensity Transformations > Local Histogram Equalization** |
| Enabled when | An image or sequence is active, the same process is not already attached to it, and no attached process holds exclusive access |
| Inputs | One grayscale or RGB still or sequence; optional active mask with matching geometry; a CFA still remains one grayscale mosaic plane, while a CFA sequence is auto-debayered to RGB first when its view has sequence auto-debayer enabled |
| Outputs | Destructively replaces the still or, after successful temporary processing, the sequence; **Take snapshot** creates an auxiliary still; no file is written directly |
| Recommended image state | Finite normalized nonlinear working copy after noise reduction; local CDF remapping is normally a post-stretch contrast operation |
| Preview support | Asynchronous movable-area preview, original/result switching, snapshot, display-channel selection, and separate source/result histogram; no execution autostretch |
| Sequence support | Yes; numeric/checkbox parameters and process Channel are captured once when Apply starts, CFA frames can be auto-debayered first, and temporary output replaces the sequence only after success |
| Mask support | Yes; active mask blends the Amount-mixed result with the original after local equalization |
| External dependencies | Bundled G'MIC 3.5.4/cgmic standard library, run through AfterNight's optional G'MIC adapter; no separate installation, model, file, or network access. In a build without G'MIC, Preview and Apply are disabled and their tooltip says why |
| Non-finite samples | An image holding a NaN or infinite sample is refused before G'MIC sees it, and left unchanged |
| Processing state | Apply records the result as nonlinear under the operation `local-histogram-equalization`; a preview records nothing |

## Before you start

- Denoise before local equalization. A local CDF expands small neighborhood
  differences whether they are structure, shot noise, compression, or defects.
- Debayer CFA stills before color work. For CFA sequences, confirm the view's
  sequence auto-debayer setting: enabled frames enter as RGB; disabled frames
  remain a grayscale mosaic that can exaggerate the sensor pattern.
- Duplicate the image or confirm Image History is available. Apply replaces the
  target only after the full operation succeeds.
- Use finite normalized data. G'MIC works in float32 and the result is clamped
  to `[0,1]`; out-of-range values and double precision are not preserved. An
  image holding a NaN or infinite sample is refused with "the image holds a
  sample that is not a finite number, which G'MIC cannot process" and left
  unchanged.
- This process needs G'MIC, which AfterNight normally includes. In a build
  without it, **Preview** and **Apply** are disabled and their tooltip reads
  "This build of AfterNight carries no G'MIC library, so this process cannot
  run."
- Start with default RGB if Lab-lightness enhancement is intended. Selecting a
  different process Channel first changes the component supplied to G'MIC.
- Preview smooth background, a bright star edge, and faint structure at 100%.
  Disable Preview area before Apply to check broad context and frame borders.

## Procedure

1. Open **Process > Intensity Transformations > Local Histogram Equalization**
   and confirm Target Image and Channel.
2. Start at Strength `75%`, Radius `4`, Sigma `100`, Regularization `8`, Reduce
   Halos selected, and Amount `1`.
3. Choose Radius for neighborhood scale. Increase only while the desired
   structure benefits more than stars, gradients, and smooth background.
4. Tune Sigma with Reduce Halos selected. Lower values restrict the CDF toward
   center-like intensities; higher values admit a broader intensity range.
5. Adjust Regularization to smooth an unstable or mottled equalization map.
   Recheck fine structure because strong bilateral smoothing can suppress it.
6. Reduce Strength to weaken the internal Soft blend; reduce Amount last to
   crossfade the entire completed filter result toward the source.
7. Toggle Switch image and inspect noise, halos, clipping, and color at Zoom
   100%. Confirm mask/inversion, then Apply.

## Complete parameter and action reference

| UI label/action | Type and unit | Runtime default | Persisted-default exception | Valid values, step, or constraints | Effect of increasing, decreasing, or selecting | Dependencies and enable/visibility rules | Quality, artifact, runtime, memory, or disk trade-off | Scope: preview/apply, channel, mask, still/sequence | Persistence |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Strength (%) | Double value plus slider, percent opacity of fixed Soft blend | `75.00` | None | `0.00..100.00`; spin default step `1`; slider maps `0..1000` to tenths | Raises opacity of G'MIC Soft blend between source and locally equalized layer; `0%` is source, `100%` is full Soft blend—not the raw CDF layer | Hidden Mode is always Soft (`2`); captured for execution | High values can look crunchy/flattened and amplify noise; does not avoid CDF cost at `0` | Preview/Apply; selected process Channel; before final Amount/mask; still/sequence | Captured; Reset/success restore `75` |
| Radius | Integer value plus slider, pixels | `4` | None | `1..16`; step `1`; square neighborhood is `(2r+1)×(2r+1)` | Enlarges the local sample window from `3×3` to `33×33` | Captured for execution | Larger windows include broader structures and sharply increase per-pixel work; can create broader halos | Preview/Apply; selected Channel; before blending/mask | Captured; Reset/success restore `4` |
| Sigma | Double value plus slider, 512-bin intensity-distance scale | `100.00` | None | `0.00..256.00`; spin step `0.05` adaptive; slider maps `0..2560` to tenths; script uses `0.1+Sigma` | With Reduce Halos on, higher Sigma admits more intensity differences into the local histogram; lower Sigma favors center-like levels. With it off, the complementary weighting reverses that relationship | Interacts fundamentally with Reduce Halos; captured | Very low selected Sigma can reduce support to nearly identical bins; very high values approach broad weighting. Off mode can ignore same-level samples and emphasize dissimilar neighbors | Preview/Apply; local CDF; still/sequence | Captured; Reset/success restore `100` |
| Regularization | Double value plus slider, G'MIC bilateral spatial/range scale | `8.00` | None | `0.00..32.00`; spin step `0.05` adaptive; slider maps `0..320` to tenths; `0` disables | If nonzero, smooths the equalized result using source norm as guide, spatial sigma `Regularization`, and range sigma `2+Regularization` in G'MIC's working convention | Applied after CDF normalization and before Soft blend | Higher values can reduce mottling/halos but blur the mapping and cost a bilateral pass; `0` preserves raw local-map variation | Preview/Apply; selected Channel; still/sequence | Captured; Reset/success restore `8` |
| Reduce Halos | Checkbox | Selected | None | Selected/unselected | Selected uses `exp(-(difference/(0.1+Sigma))²)` and favors intensities similar to the center. Unselected uses `1-exp(...)`, gives zero weight to identical values, and increasingly favors dissimilar levels | Changes meaning of Sigma; captured | Unselected can produce aggressive/unstable local mappings; selected is the conservative path but does not guarantee halo-free output | Preview/Apply; local CDF; still/sequence | Captured; Reset/success select it |
| Amount | Double value plus slider, final normalized blend | `1.00` | None | `0.00..1.00`; spin step `0.05` adaptive; slider maps `0..1000` to thousandths | Linearly crossfades source and complete fixed-Soft LHE result; `0` is source, `1` is full result | Independent of Strength; captured | Reduces all visible effects but does not undo internal clipping or save CDF/bilateral work | Preview/Apply; reconstructed Channel; before mask; still/sequence | Captured; Reset/success restore `1` |
| Histogram plot | Interactive source/result histogram | Source initially, preview result after execution | None | Mouse wheel/drag and toolbar actions below | Displays distribution and clipping diagnostics only | Updated from target/preview; separate from preview display | High bin counts add analysis/rendering cost; spatial halos are not visible in a histogram | Display only | Window lifetime |
| Zoom in | Histogram-toolbar action | Current histogram range | None | Click | Narrows plot range | Histogram shown | Diagnostic only | Histogram display only | Window lifetime |
| Zoom out | Histogram-toolbar action | Current histogram range | None | Click | Widens plot range | Histogram shown | Diagnostic only | Histogram display only | Window lifetime |
| Zoom reset | Histogram-toolbar action | Full histogram range | None | Click | Restores full plot range | Histogram shown | Diagnostic only | Histogram display only | Window lifetime |
| Show grid | Checkable histogram action | Checked | None | Checked/unchecked | Shows/hides plot grid | Histogram shown | Display only | Histogram display only | Window lifetime |
| Show clipping info | Checkable histogram action | Checked | None | Checked/unchecked | Shows/hides endpoint clipping readouts | Histogram shown | Binned diagnostic, not spatial-quality evidence | Histogram display only | Window lifetime |
| Absolute scales | Checkable histogram action | Unchecked | None | Checked/unchecked | Uses shared absolute vertical scale instead of local per-channel normalization | Histogram shown | A low-population channel can become hard to see | Histogram display only | Window lifetime |
| Draw histogram areas | Checkable histogram action | Unchecked | None | Checked/unchecked | Fills plotted channel areas | Histogram shown | Overlap can obscure detail | Histogram display only | Window lifetime |
| Show histogram | Checkable histogram action | Checked | None | Checked/unchecked | Hides/clears graph and disables toolbar, or reloads source/preview | Always available while idle | Hiding saves graph work only | Display only | Window lifetime |
| Plot resolution | Combo box, histogram bins | **8-bit (256)** | None | **6-bit (64)**, **7-bit (128)**, **8-bit (256)**, **9-bit (512)**, **10-bit (1024)**, **12-bit (4096)**, **14-bit (16K)**, **16-bit (64K)**, **100 levels**, **1000 levels**, **10000 levels** | Changes diagnostic binning only | Disabled when histogram hidden | More bins cost memory/rendering; output always uses internal 512-bin local CDF regardless | Histogram display only | Window lifetime |
| Target Image | Image selector | Attached/active target | None | Open eligible image or sequence | Changes source, Channel list, histogram, Preview, mask, and destination | Active target required; duplicate/exclusive launch gates apply; disabled during Apply | A wrong selection before Apply replaces the wrong target | Preview/Apply source and mask | Target lifetime; Reset unchanged |
| Channel | Combo box | **RGB** for RGB; disabled **Grayscale** for mono | None | **RGB**, **Red**, **Green**, **Blue**, **CIE L* (Lightness)**, **CIE a***, **CIE b***, **CIE c***, **CIE h***, **CIE X**, **CIE Y (Luminance)**, **CIE Z**, **HSL Hue**, **HSL Saturation**, **HSL Lightness**, **HSV Hue**, **HSV Saturation**, **HSV Value**, **YCbCr Luma**, **YCbCr Blue Chroma**, **YCbCr Red Chroma** | Default RGB is internally converted by G'MIC and equalized only in Lab lightness. A selected component is extracted first and supplied as grayscale, then reconstructed | RGB required for components; disabled during Apply; the selection is captured when Apply starts | Lab-lightness default better preserves chroma than independent RGB; alternate component round trips add buffers and possible gamut clipping | Preview/Apply; active mask post-blend; still/sequence | Dialog-local; Reset/success restore RGB |
| Preview | Checkbox | Checked | None | Checked/unchecked | Enables/disables asynchronous full/area execution and preview pane | Process idle | Recomputes full per-pixel CDF/bilateral after changes | Preview only; mask-aware; still/current displayed frame | Window lifetime; Reset unchanged |
| Preview pane | Image viewer | Filtered result in Linear mode; centered `600×600` area when possible | Execution autostretch is unavailable | Pan/scroll and toolbar actions below | Shows mask-aware result without target modification | Exists only with Preview | Holds source/result/display plus G'MIC buffers | Preview only | Window lifetime |
| Fit To Window | Preview-toolbar action | Preview fitted initially | None | Click or **Ctrl+Shift+F** | Fits whole image | Preview required | Broad context; hides fine noise | Preview display only | Viewer lifetime |
| Zoom In | Preview-toolbar action | Current scale | None | Click; `1.25×` | Enlarges around center | Preview required | Improves local inspection | Preview display only | Viewer lifetime |
| Zoom Out | Preview-toolbar action | Current scale | None | Click; `0.8×` | Reduces view | Preview required | More context, less fine detail | Preview display only | Viewer lifetime |
| Zoom 100% | Preview-toolbar action | Current scale | None | Click; scale `1.0` | One-pixel viewer scale | Preview required | Preferred for noise/halos | Preview display only | Viewer lifetime |
| View Channel | Preview-toolbar combo box | **RGB** for RGB; disabled **Grayscale** for mono | None | Stored/derived RGB, CIE, HSL, HSV, HSI, YCbCr display components | Selects display component only | Separate from process Channel | Diagnoses color changes without altering samples | Preview display only | Viewer lifetime |
| Switch image | Preview-toolbar action | Result shown | None | Click; **Space**; right-click; **Ctrl+Shift+O** | Toggles result/original | Preview required | Comparison only | Preview display | Preview lifetime |
| Preview area | Preview-toolbar checkbox | Checked with centered `600×600` area when possible | None | Checked/unchecked; move/resize | Processes area and composites it over original preview | Preview required | Major tuning speedup; can hide broad context/border behavior | Preview/snapshot; mask cropped consistently | Preview lifetime |
| Take snapshot | Preview-toolbar action | No snapshot | None | Click or **Ctrl+Shift+K** | Re-executes and opens `<target> - Local Histogram Equalization - SNAPSHOT` | Preview required | Adds costly execution and auxiliary still | New still; target unchanged; area/mask honored | New view per action |
| Apply | Button/action | Enabled | None | One action | Captures numeric/checkbox values, requests a processing lock on the current target, executes the full still/sequence, then applies Amount/mask and commits on success | Target, Channel, and process controls lock during Apply | Per-pixel CDF, optional bilateral, conversions, mask, and sequence encoding are expensive | Destructive still/sequence replacement after success; the result is recorded nonlinear | History records six parameters and, for a colour image, the selected Channel text; then Reset |
| Reset | Button/action | Enabled while idle | None | One action | Restores `75/4/100/8/selected/1` and RGB | Leaves Target, Preview, histogram, mask, and preview toolbar; disabled during Apply | Checked Preview schedules another expensive execution | Configuration only | Immediate; also after successful Apply |
| Close | Button/action | Enabled while idle | None | One action | Closes without Apply | Disabled during Apply | G'MIC cancellation may wait for next abort check | Window/execution | Settings discarded |
| Sequence progress / Cancel | Shared indeterminate process/frame progress and Cancel | Created for sequence Apply | None | Cooperative Cancel | Optionally auto-debayers configured CFA frames, writes a temporary sequence, and promotes it only after success | Sequence only | Captured parameters and Channel stay fixed for every frame; high temporary disk/CPU cost | Full sequence; Amount/mask per frame | Cancel removes temporary output; success records one history step and resets |

## How the algorithm works

### Conceptual pipeline

1. The shared process path extracts a selected component when needed, checks it
   for non-finite samples, and sends it through AfterNight's G'MIC adapter, one
   G'MIC session per run, as float32 planar fractions of full scale. For default RGB, the filter's fixed channel
   code `16` selects Lab lightness internally.
2. The bundled G'MIC command `fx_equalize_local_histograms` normalizes that
   working channel to integer bins `0..511`. At every pixel, it scans a square Radius neighborhood using Neumann
   boundary extension and builds a weighted 512-bin histogram.
3. It converts the histogram to a normalized CDF and maps the center sample
   through that CDF. It then normalizes the resulting local-map image to
   `0..255`.
4. If Regularization is nonzero, it applies a guided bilateral smoothing pass
   to the local-map result.
5. The command combines source and map with G'MIC **Soft light** at Strength
   opacity. AfterNight clamps the result to `[0,1]` and converts it back to the
   source type. On Apply it then records the result as nonlinear in the
   processing-state cards (`ANLIN` false, `ANOP` `local-histogram-equalization`),
   keeping every other card, before applying final Amount and mask; a preview
   records nothing.

### Local weighted CDF

Let `q(p)` be the center sample quantized to one of 512 bins, and let
`N_r(p)` be its `(2r+1)×(2r+1)` neighborhood. For neighbor `u`, define intensity
difference `Δ=|q(u)-q(p)|` and `s=0.1+Sigma`.

With Reduce Halos selected, the histogram weight is

\[
w(\Delta)=\exp[-(\Delta/s)^2].
\]

Without it, the script uses the complement

\[
w(\Delta)=1-\exp[-(\Delta/s)^2].
\]

The weighted histogram and CDF are

\[
h_p(k)=\sum_{u\in N_r(p)} w(|q(u)-q(p)|)\,[q(u)=k],
\]

\[
C_p(k)=\frac{\sum_{j=0}^{k}h_p(j)}{\max(10^{-5},\sum_{j=0}^{511}h_p(j))}.
\]

The local-map sample is `E(p)=C_p(q(p))`. Selected weighting favors levels near
the center; unselected weighting gives identical levels zero weight and favors
increasingly dissimilar levels. This is an intensity-distance Gaussian, not a
spatial Gaussian. Spatial inclusion is the hard square Radius window.

### Two separate strength stages

The hidden process mode is always G'MIC **Soft light**. Strength `s_t` is its
opacity between the original and locally mapped layer; it is not a linear mix
with the raw CDF layer. Let the resulting G'MIC output be `F`. AfterNight then
uses a separate linear Amount `a`:

\[
M(p)=(1-a)I(p)+aF(p).
\]

With mask weight `m(p)`, final output is

\[
O(p)=(1-m(p))I(p)+m(p)M(p).
\]

Strength `0%` and Amount `0` are both identity endpoints but neither bypasses
the expensive local-CDF call. Clipping/normalization inside G'MIC occurs before
Amount and mask, so later blending cannot restore lost range.

### Regularization, color, and precision

- For nonzero Regularization `λ`, the script applies bilateral smoothing with
  source norm as guide, spatial sigma `λ`, and range sigma `2+λ` in the G'MIC
  working convention. This smooths the map rather than changing CDF support.
- Default RGB uses Lab lightness and preserves G'MIC's reconstructed chroma.
  Selecting Red/Green/Blue or another process component extracts that scalar
  component before the fixed internal lightness selection is invoked. A derived
  component's reconstruction saturates an integer image at its type's range and
  keeps a floating image's values outside `[0, 1]`.
- The G'MIC hand-off is float32. Integers normalize before processing; double
  narrows; all output clamps to `[0,1]` and returns to the original storage
  type, rounded to nearest.
- Apply records the result as nonlinear under the operation
  `local-histogram-equalization`.

## Parameter interactions and starting points

| Combination | Conservative starting point | Too weak looks like | Too strong looks like | Adjustment order |
| --- | --- | --- | --- | --- |
| Radius × structure | Radius `4` on 100% preview | Structure remains compressed | Broad halos/flattening and long runtime | Radius first |
| Sigma × Reduce Halos | Selected, Sigma `100` | Very low Sigma uses only center-like levels | Very high Sigma admits broad level population; unselected can become aggressive | Keep selected; tune Sigma second |
| Regularization × noise | `8`, compare `0` only on a duplicate | Map remains mottled/noisy | Fine local separation is smoothed away | Sigma, then Regularization |
| Strength × Amount | Strength `75%`, Amount `1` | Both low obscure whether CDF settings work | High Strength creates Soft-light artifacts even if Amount later hides them | Tune Strength, lower Amount last |
| RGB × component | Default RGB/Lab lightness | Luminance separation is modest | Alternate chroma/hue component produces gamut/color artifacts | Choose process Channel before CDF controls |
| Preview area × Radius | Area for tuning, full frame before Apply | Small area misses context | Full result surprises at borders/large structures | Tune locally, then full preview |

These are source-derived checkpoints, not executed visual-quality claims.

## Inputs, outputs, and metadata

- Output dimensions, channel count, pixel format, and storage type match the
  source still/frame. Integer results quantize; float/double still clamp and
  double passes through float32.
- Still Apply modifies only after success. Take snapshot creates a new still;
  histogram/display settings are not baked into samples.
- The result keeps the source image's header cards and metadata, and Apply
  records its processing state as nonlinear under the operation
  `local-histogram-equalization`, with a lineage entry naming the process. A
  preview records nothing. Image History uses the process name.
- History records Strength, Radius, Sigma, Regularization, Amount, Reduce Halos,
  and, for a colour image, ` in channel: <channel>`. It omits fixed Soft mode,
  internal Lab-lightness code, mask/inversion, G'MIC version, and CDF bins.
- Successful Apply resets process parameters/RGB. Target, Preview, histogram,
  mask, and preview toolbar remain.
- Sequence Apply promotes a temporary sequence only after success; Cancel
  removes temporary output. There is no direct output path/name control.
- CFA sequence frames are auto-debayered before filtering when sequence
  auto-debayer is enabled; otherwise the filter receives the mosaic plane.

## Performance

- Per pixel, the script scans `(2r+1)²` samples and cumulatively scans 512 bins.
  A useful model is `O(width × height × ((2r+1)² + 512))` per processed scalar
  channel, plus color conversion, normalization, and blending.
- Radius grows the neighborhood from 9 to 1089 samples, so it is the dominant
  user-controlled cost. The fixed CDF scan remains large even at Radius `1`.
- Nonzero Regularization adds a full guided bilateral pass. Default RGB also
  adds Lab conversion/reconstruction.
- Memory includes G'MIC float images, local command/list intermediates,
  destination, color conversion, mask, preview, histogram, and temporary
  sequence storage.
- Preview area is the safest prototype path. Strength `0`, Amount `0`, and
  unchecked Preview do not provide an algorithmic fast path for Apply.
- Focused tests illustrate cost even on synthetic small/medium fixtures: the
  four-test filter took 57.166 seconds in Release and 105.196 seconds in Debug on
  the recorded machine. These are validation timings, not production benchmarks.
- No approved astronomical timing or peak-memory measurement was made.

## Worked example

- **Evidence mode:** Source-and-equation-derived; not executed on approved
  astronomical data.
- **Input/settings:** Finite normalized nonlinear image, default RGB/Lab
  lightness, Strength `75%`, Radius `4`, Sigma `100`, Regularization `8`, Reduce
  Halos selected, Amount `1`.
- **CDF checkpoint:** If a local weighted histogram has total weight 10 and
  cumulative weight 7 at the center bin, its local-map value before global
  normalization/blending is `0.7`.
- **Endpoint checkpoint:** Strength `0%` returns source at the G'MIC blend stage;
  Amount `0` returns source at the AfterNight stage.
- **Inspect:** At 100%, check smooth background noise, star rims, mottling,
  border behavior, hue/chroma stability, clipping bins, and recorded history.
- **Limitations:** No visual improvement, executed sequence output, production
  timing, or memory result is claimed.

## Warnings and limitations

- Local Histogram Equalization is not CLAHE: no tiles, clip limit, or tile
  interpolation exist. It avoids tile seams by computing a CDF at every pixel,
  at much higher cost.
- The only process mode is hidden Soft. Raw and Hard remain disabled because the
  process source records them as broken for 16-bit images.
- Sigma measures intensity difference in 512-bin space, not spatial distance.
  Radius alone defines the square spatial neighborhood.
- Turning Reduce Halos off does more than remove protection: it complements the
  weighting, gives identical values zero weight, and favors dissimilar levels.
- Default RGB operates on Lab lightness, not independently on R/G/B. Alternate
  process components can alter color/gamut during reconstruction.
- Local equalization can amplify noise, ringing, hot pixels, compression, and
  gradient residuals. Regularization and final blending do not make bad input
  safe.
- Every result is clamped to `[0,1]`; out-of-range float data and double
  precision are not preserved. An image holding NaN or infinity is refused by
  name; without G'MIC in the build, the process cannot run and Preview and Apply
  stay disabled.
- History uses the process name but omits the hidden mode/internal channel
  details.

## Troubleshooting

| Symptom or exact error | Verified cause/validation path | Recovery | Evidence source |
| --- | --- | --- | --- |
| Result looks unlike CLAHE | This process computes per-pixel weighted CDFs and Soft blend; no tiles/clip limit | Use CLAHE when tiled contrast-limited behavior is required | Bundled G'MIC command |
| Smooth background becomes mottled | Neighborhood CDF expands small differences/noise | Denoise first; reduce Strength/Radius; increase Regularization cautiously | CDF/cost model |
| Halos appear with Reduce Halos selected | Similarity weighting reduces but does not eliminate local CDF and Soft-light edge effects | Reduce Strength/Radius; inspect full frame; use mask | Weighted CDF and Soft blend |
| Turning Reduce Halos off changes result drastically | Weight becomes `1-exp(...)`, not unweighted histogram | Re-enable it; retune Sigma only if the complementary behavior is intentional | Exact script equation |
| Sigma seems opposite with protection off | Complementary weighting favors larger intensity differences | Re-enable Reduce Halos or reason from the off-mode equation | Exact script equation |
| Colors shift | Non-default component reconstruction or extreme Lab-lightness mapping clips/gamut shifts | Return process Channel to RGB; reduce Strength/Amount | Channel pipeline |
| Process is very slow | Per-pixel window plus 512-bin CDF, optional bilateral, and preview repeats | Use small Preview area, lower Radius, pause changes | Complexity model |
| Preview differs from Apply | Preview area, Channel, mask, or display conditions changed | Match controls, disable area | Shared execution paths |
| The image is marked nonlinear after Apply | Apply records local equalization as a nonlinear operation | Expected; a later linear-only process will see the recorded state | Processing-state record |
| Error: the image holds a sample that is not a finite number | The image has a NaN or infinite sample, which G'MIC cannot process | Repair or crop the non-finite samples, then retry; the image was left unchanged | `GmicProcessSupport.cpp` |
| Preview and Apply are disabled; tooltip says the build carries no G'MIC library | This build was made without G'MIC | Use a build that includes G'MIC, or CLAHE | `GmicProcessSupport.cpp` |
| A G'MIC error is reported | G'MIC failure, invalid data, or memory exhaustion | Verify finite normalized data; retry smaller duplicate | Process error path |

## Related workflow steps

Complete [noise reduction](../noise-reduction/wavelet-denoise.md) before local
equalization. Compare [Local Contrast Enhancement](local-contrast-enhancement.md)
for a high-radius residual model, and [CLAHE](clahe.md) for tiled CDFs with a
clip limit and interpolation. Use [Masks](../../interface/masks.md) to protect
smooth background and [Image History](../../interface/image-history.md) to
compare or revert Apply.

## References and provenance

- [G'MIC `blend` reference](https://gmic.eu/reference/blend) — official Soft
  light and opacity command contract; accessed 2026-07-14.
- [G'MIC `bilateral` reference](https://gmic.eu/reference/bilateral.html) —
  official guide/spatial/range sigma contract; accessed 2026-07-14.
- [G'MIC `apply_channels` reference](https://gmic.eu/reference/apply_channels.html)
  — official channel-selection and 0–255 working convention; accessed
  2026-07-14.
- [David Tschumperlé's 2018 G'MIC implementation note](https://discuss.pixls.us/t/feature-request-contrast-limited-adaptive-histogram-equalization-clahe/6573/5)
  — primary author statement that this local-histogram filter is not CLAHE and
  source listing for the original controls.
- Bundled G'MIC 3.5.4 `gmic_stdlib.gmic` and AfterNight sources/tests are authoritative for the shipped CDF, fixed mode/internal
  channel, controls, G'MIC hand-off, masks, state/history, preview, and sequences.
