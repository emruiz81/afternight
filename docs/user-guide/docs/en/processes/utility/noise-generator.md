---
kind: process
stage: any
menu: Process > Utilities > Noise Generator
summary: >-
  Adds synthetic random noise to an image, to test denoisers, masks and other noise-sensitive processing.
data_in: either
---
# Noise Generator

## Purpose and expected result

Noise Generator adds synthetic random noise to a working image so developers and
advanced users can test denoisers, masks, rejection behavior, and other
noise-sensitive processing. It is a test-data tool, not a simulation of a full
camera noise model: it does not combine calibrated read noise, shot noise, dark
current, gain, and quantization from acquisition metadata.

The result is a same-size replacement image or sequence with one of seven
noise selections - six distinct noise models drawn by AfterNight's own engine,
plus **LocalVar**, which is Gaussian noise of variance `0.01` - applied to RGB,
one stored channel, or one converted component. The dialog draws a random seed
when it opens and holds it for every preview, snapshot, and Apply, so the
preview shows the noise Apply adds. After an Apply completes, the dialog draws
a new seed, so a second Apply adds new noise. There is no seed control, and the
seed is not recorded.

## At a glance

| Item | Current behavior |
| --- | --- |
| Menu path | **Process > Utilities > Noise Generator** |
| Enabled when | An image or sequence is active, another Noise Generator is not already attached to it, and no attached process holds exclusive access |
| Inputs | One grayscale or RGB still/sequence and optional matching active mask; configured CFA sequence frames can be auto-debayered before processing |
| Outputs | Replaces the still or, after successful temporary processing, the sequence; **Take snapshot** creates an auxiliary still; no file or seed record is written |
| Recommended image state | A duplicate or synthetic finite normalized test image; linear data is easiest to interpret, but the process accepts the current stored state |
| Preview support | Asynchronous full-image or movable-area preview, original/result switching, snapshot, display channels, and display-only autostretch; every recomputation draws from the dialog's held seed, so the same settings show the same noise |
| Sequence support | Yes; noise mode, numeric values, Channel, and seed are captured once when Apply starts; each frame draws its own noise from the seed shifted by its frame index; temporary output replaces the sequence only after success |
| Mask support | Yes; the active mask blends the generated result with the original after noise generation |
| External dependencies | None; the AfterNight engine's own noise generator. No Python interpreter, scikit-image, global lock, or network access |

## Before you start

- Duplicate valuable data. Successful Apply replaces the target, and the seed is
  not recorded, so the generated noise cannot be subtracted later.
- Decide what is being tested. **Gaussian** and **Speckle** expose Mean/Var;
  impulse modes expose Amount; **Poisson** is data-dependent; **LocalVar** is
  Gaussian noise at a fixed variance of `0.01`.
- Use normalized finite data. Mean, Var, and Amount are fractions of full
  scale: an integer image's values are restated in its own codes (Mean times the
  type's span, Var times its square). Generated output is clipped to the image's
  full scale - `[0,1]` for a float image, the type's range for an integer one -
  and a pepper sample takes the lowest value of that range.
- For RGB, decide between **RGB** and a component. RGB generates independent
  random values for R, G, and B samples; impulse noise therefore need not replace
  all three samples at the same pixel.
- The full-frame Preview shows the noise the next Apply adds: the dialog holds
  one seed until an Apply completes. Changing a setting changes the noise
  computed from that seed; reopening the dialog or completing an Apply draws a
  new seed.

## Procedure

1. Activate a duplicate or synthetic target and open **Process > Utilities >
   Noise Generator**. Confirm **Target Image**, **Channel**, and mask state.
2. Keep **Preview** checked and select **Noise Type**. Use the enabled numeric
   controls listed below; controls that remain enabled after a mode switch can be
   ignored when the backend does not consume them.
3. For Gaussian/Speckle, choose Mean and Var. For Salt/Pepper/Salt & Pepper,
   choose Amount and, for the combined mode, Salt vs Pepper. Poisson and LocalVar
   have no effective numeric control in this UI.
4. Inspect the preview at **Zoom 100%**. Confirm the range and spatial
   character. Use **Switch image** to compare the current draw with the
   original.
5. Uncheck **Preview area** for a full-frame distribution check. If using a mask,
   confirm its enabled/inverted state and geometry.
6. Select **Apply**. The full-frame result is the noise the preview showed; the
   dialog then draws a new seed for the next run. For a sequence, monitor
   progress; Cancel preserves the original.

## Complete parameter and action reference

| UI label/action | Type and unit | Runtime default | Persisted-default exception | Valid values, step, or constraints | Effect of increasing, decreasing, or selecting | Dependencies and enable/visibility rules | Quality, artifact, runtime, memory, or disk trade-off | Scope: preview/apply, channel, mask, still/sequence | Persistence |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Noise Type | Combo box | **Gaussian** | None | **Gaussian**, **LocalVar**, **Poisson**, **Salt**, **Pepper**, **Salt & Pepper**, **Speckle** | Selects the distribution and which numeric arguments are consumed | Disabled during Apply; mode changes update numeric enablement, but Salt/Pepper/Salt & Pepper do not explicitly disable Mean/Var and Speckle does not explicitly disable Salt vs Pepper, so stale enabled controls can remain after some transitions | Every mode draws per sample from the held seed; changing type recomputes the preview | Preview/Apply; selected channel/component; before mask; still/sequence | Captured per execution; Reset and successful Apply restore Gaussian |
| Mean | Double value plus slider, additive-noise mean as a fraction of full scale (Speckle: relative multiplier mean) | `0.0000` | None | `0.0000..0.5000`; configured single-step hint `0.005`, but adaptive decimal stepping makes arrow/keyboard increments vary with magnitude; slider has 1001 positions | Raises the Gaussian additive offset or Speckle multiplier mean | Intended for Gaussian and Speckle; ignored by every other backend mode even if stale-enabled after a mode transition | Positive means brighten Gaussian output and bias Speckle in proportion to source; clipping increases near the upper bound | Preview/Apply; selected channel/component; before mask; still/sequence | Captured per execution; Reset/successful Apply return `0` |
| Var | Double value plus slider, variance | `0.0020` | None | `0.0000..0.5000`; configured single-step hint `0.001`, but adaptive decimal stepping makes arrow/keyboard increments vary with magnitude; standard deviation is `sqrt(Var)` | Widens Gaussian/Speckle random variation | Intended for Gaussian and Speckle; ignored by LocalVar and every other backend mode even if stale-enabled | Larger variance increases clipping and obscures structure; `0` removes random spread but Mean can still change samples | Preview/Apply; selected channel/component; before mask; still/sequence | Captured per execution; Reset/successful Apply return `0.002` |
| Amount | Double value plus slider, replacement probability per selected array sample | `0.0200` | None | `0.0000..0.5000`; configured single-step hint `0.001`, but adaptive decimal stepping makes arrow/keyboard increments vary with magnitude | Raises the probability that Salt/Pepper/Salt & Pepper replaces a sample | Enabled for the three impulse modes; ignored by Gaussian, LocalVar, Poisson, and Speckle | More replacements destroy more source samples; RGB draws independently per channel sample | Preview/Apply; selected channel/component; before mask; still/sequence | Captured per execution; Reset/successful Apply return `0.02` |
| Salt vs Pepper | Double value plus slider, salt fraction among selected impulse replacements | `0.50` | None | `0.00..1.00`; configured single-step hint `0.01`, but adaptive decimal stepping makes arrow/keyboard increments vary with magnitude; `0` all pepper, `1` all salt | Raises the share replaced by the top of the range; lowers the share replaced by its bottom | Consumed only by Salt & Pepper; may remain stale-enabled when switching from Salt & Pepper to Speckle, where it is ignored | Extreme values make one-sided impulse tests; does not change total Amount | Preview/Apply; selected channel/component; before mask; still/sequence | Captured per execution; Reset/successful Apply return `0.5` |
| Target Image | Image selector | Attached/active target | None | Open eligible still or sequence | Changes source, Channel list, Preview, active mask, and Apply destination | Active target required; duplicate/exclusive launch gates apply | A wrong target is destructively replaced after success | Preview/Apply source and active mask | Target lifetime; Reset does not change it |
| Channel | Combo box | **RGB** for RGB; disabled **Grayscale** for mono | None | **RGB**, **Red**, **Green**, **Blue**, CIE L*a*b*/L*c*h*/XYZ, HSL, HSV, and YCbCr components exposed by the shared selector | RGB draws noise independently for every stored channel sample; a component draws on one converted plane and reconstructs RGB | Components require RGB; disabled during Apply; the selection is captured when Apply starts | Component conversion adds buffers/quantization/gamut risk | Preview/Apply; active mask post-blend; still/sequence | Dialog-local; Reset/successful Apply restore RGB on RGB targets |
| Preview | Checkbox | Checked | None | Checked/unchecked | Enables/disables asynchronous full-image or selected-area random generation | Process idle | Every recomputation draws from the dialog's held seed | Preview only; mask-aware; still/current displayed sequence frame | Window lifetime; Reset unchanged |
| Preview pane | Image viewer | Current generated result | None | Pan/drag and toolbar actions below | Shows the current mask-aware realization without modifying target | Exists only with Preview | Holds source/result/display buffers | Preview only | Window lifetime |
| Fit To Window | Preview-toolbar action | Fitted initially | None | Click or **Ctrl+Shift+F** | Fits the whole image | Preview image required | Useful for broad distribution; resampling hides individual impulses | Preview display only | Viewer lifetime |
| Zoom In | Preview-toolbar action | Current scale | None | Click; `1.25×` relative zoom | Enlarges around viewport center | Preview image required | Improves sample inspection; increases panning | Preview display only | Viewer lifetime |
| Zoom Out | Preview-toolbar action | Current scale | None | Click; `0.8×` relative zoom | Reduces around viewport center | Preview image required | Adds context but hides fine grain | Preview display only | Viewer lifetime |
| Zoom 100% | Preview-toolbar action | Current scale | None | Click; absolute scale `1.0` | Shows one-pixel display scale | Preview image required | Preferred for impulse locations and channel-wise grain | Preview display only | Viewer lifetime |
| View Channel | Preview-toolbar combo box | **RGB** for RGB; disabled **Grayscale** for mono | None | Stored/derived RGB, CIE, HSL, HSV, HSI, and YCbCr display components | Selects only displayed component | Separate from process Channel; components require RGB | Helps reveal independent per-channel draws without changing data | Preview display only | Viewer lifetime |
| Display Mode | Preview-toolbar combo box | Inherits target Linear/Autostretch | None | **Linear**, **Autostretch** | Changes display only; Autostretch takes the image's exact median and MAD and rounds each display byte to the nearest value | Autostretch enables settings action | Stretch can exaggerate low-amplitude noise | Preview display only | Preview-view state |
| Autostretch Settings | Preview-toolbar dialog action | Disabled in Linear mode; inherits target-view settings | Shared-view values load when opened | Opens preview autostretch controls; **Apply Stretch** hidden | Tunes display only | Autostretch mode only | Changes judgment, never generated samples | Preview display only | Viewer-scoped |
| Switch image | Preview-toolbar action | Generated result shown | None | Click; **Space**; right-click; **Ctrl+Shift+O** | Toggles current result/original | Preview required | Comparison only; no recomputation | Preview display | Preview lifetime |
| Preview area | Preview-toolbar checkbox | Checked with initial `1200×1200` area | None | Checked/unchecked; move/resize overlay | Generates noise only for the selected area and composites over original preview | Preview required | Reduces tuning cost; the area is drawn on its own, so judge the distribution there and the exact samples in a full-frame preview | Preview/snapshot; mask cropped consistently; Apply always full-frame | Preview lifetime |
| Take snapshot | Preview-toolbar action | No snapshot | None | Click or **Ctrl+Shift+K** | Re-executes with the dialog's held seed and opens `<target> - Noise Generator - SNAPSHOT` | Preview required | Adds a complete execution and auxiliary still; it draws the same noise as the preview of the same scope | New still; target unchanged; area/mask honored | Each action creates a view; does not draw a new seed |
| Apply | Button/action | Enabled | None | One action | Captures noise type, numeric values, Channel, and seed, locks the target, and draws the full target from the held seed | Target must lock; process controls are disabled during Apply; another Apply is ignored while running | Component conversion, masks, sequence encoding, and temporary storage add cost | Destructive still/sequence replacement after success | History records Noise Type and, for a colour image, the Channel label, not numeric values or seed; settings then Reset and a new seed is drawn |
| Reset | Button/action | Enabled while idle | None | One action | Restores Gaussian, Mean `0`, Var `0.002`, Amount `0.02`, Salt vs Pepper `0.5`, and RGB for RGB targets | Does not change Target, Preview, mask, preview toolbar, or the held seed; Preview checked recomputes | Cannot restore a seed an earlier Apply used | Configuration only | Immediate; also invoked after successful Apply |
| Close | Button/action | Enabled while idle | None | One action | Closes without applying current settings | Disabled during Apply | Requests preview cancellation, which the generator observes | Window/execution | Settings and the held seed are discarded |
| Sequence progress / Cancel | Shared per-process/frame progress and Cancel | Created for sequence Apply | None | Cooperative Cancel; the generator observes it while drawing a frame | Sequence target only | A frame stops promptly on Cancel | Each frame draws its own noise from the seed shifted by its frame index; captured mode, numerics, and Channel are stable | Cancel removes temporary output; success replaces sequence and records one history step |

## How the algorithm works

### Conceptual pipeline

1. The selected stored image or converted component is handed to AfterNight's
   engine in its stored units. Mean and Var are restated in those units for an
   integer type; the clip range and the salt and pepper values are the type's
   full scale (`[0,1]` for float).
2. The engine draws one random value or replacement decision per sample. Every
   draw is keyed on the sample's coordinate and the run's seed rather than taken
   from a generator that advances, so the same seed gives the same noise at any
   thread count. A sequence frame uses the seed shifted by a fixed multiple of
   its frame index.
3. The result is clipped to the full-scale range and written back in the source
   pixel type.
4. AfterNight reconstructs RGB for a component path, keeps the source metadata,
   then applies the active mask blend.

### Model and equations

Let `I(p)` be a selected sample as a fraction of full scale, `m` be **Mean**,
`v` be **Var**, `a` be **Amount**, `q` be **Salt vs Pepper**, and `N(m,v)` be an
independent Gaussian draw with mean `m` and variance `v`.

- **Gaussian:** `O(p) = I(p) + N(m,v)`.
- **LocalVar:** `O(p) = I(p) + N(0,0.01)` at every element - the Gaussian model
  at a fixed variance of `0.01`. Mean/Var are disabled/ignored for this mode.
- **Speckle:** `O(p) = I(p) + I(p)N(m,v)`.
- **Salt:** each element is replaced by the top of the range (`1`) with
  probability `a`.
- **Pepper:** each element is replaced by the bottom of the range (`0`) with
  probability `a`.
- **Salt & Pepper:** replacement occurs with probability `a`; a replaced element
  becomes the top with conditional probability `q`, otherwise the bottom.
- **Poisson:** each sample is redrawn as `Poisson(I(p)s)/s` with a rate scale
  `s` fixed by the sample type: `256` for an 8-bit integer image and `65,536`
  for every other type, so the noise depends on the sample value and not on the
  rest of the picture. A sample at or below zero becomes zero.

All array elements are independent draws under these models. In an interleaved
RGB array, `p` identifies a channel sample, not an RGB triplet. The active mask
is applied later by the shared process path:

\[
F(p)=(1-M(p))I(p)+M(p)O_c(p),
\]

where `M` is the effective normalized mask (or its inverse) and `O_c` is the
clipped, type-restored generated result.

### Reproducibility, boundaries, and failure behavior

- The dialog draws its seed from the system's entropy source and the clock when
  it opens, holds it for every preview, snapshot, and Apply, and draws a new one
  after an Apply completes. Equal settings, input, and seed give the same noise
  at any thread count; the seed is not stored with the image, so a later session
  cannot reproduce it.
- Var is variance, not standard deviation. The Gaussian sigma is `sqrt(Var)`.
- Clipping occurs before mask blending. Out-of-range random values can be
  flattened irreversibly; integer conversion then rounds them.
- NaN/Inf are not validated and stay non-finite.
- Generator failures are reported as an error; the target is unchanged.
- The generator observes Cancel while it runs, and no global lock serializes it
  with other processes.

## Parameter interactions and starting points

| Combination | Conservative starting point | Too weak looks like | Too strong looks like | Adjustment order |
| --- | --- | --- | --- | --- |
| Gaussian Mean × Var | Mean `0`, Var `0.002` on a duplicate | Distribution is indistinguishable at the chosen display scale | Broad clipping and strong brightness dispersion | Keep Mean `0`; raise Var; add Mean only for an intentional bias |
| Speckle Mean × Var | Mean `0`, low Var | Bright structures show little signal-dependent variation | Bright structures fragment/clip much more than dark background | Set Mean, then raise Var while comparing bright/dark regions |
| Amount × Salt vs Pepper | Amount `0.02`, ratio `0.5` | Too few impulses to stress the tested operation | Source structure is dominated by replacements | Set total Amount, then tune the salt/pepper split |
| RGB × component | Compare RGB with one deliberate luminance component | One plane does not stress color-channel handling | Independent RGB impulses create colored samples | Choose channel semantics first, then distribution/strength |

These are test-design starting points, not measurements of astronomical
quality. Reopen the dialog, or Apply to a fresh duplicate after a completed
Apply, to get further independent draws when evaluating statistical behavior.

## Inputs, outputs, and metadata

- Dimensions normally match input. Channel count normally matches, except that
  configured CFA sequence frames can be auto-debayered to RGB before generation.
- Output keeps the source pixel type. Noise is drawn in the stored units and
  integer data is rounded; RGB/component reconstruction can add further
  rounding/gamut clipping.
- Apply replaces a still only after a successful result. Preview and snapshot do
  not modify it. A snapshot is the held seed's draw in a separate still view.
- The result keeps the source image's header cards and metadata. Image History
  records **Noise Type** and, for a colour image, ` in channel: <channel>`; it
  does not record Mean, Var, Amount, Salt vs Pepper, or the seed.
- No explicit linear/nonlinear state or simulated-camera provenance is added.
- Sequence Apply writes temporary output and draws separately per frame from the
  held seed shifted by the frame index. Success replaces the sequence;
  cancellation removes temporary output. No seed manifest, noise map, report, or
  standalone file is created.

## Performance

- Generation scales with selected pixel count and channel count and runs on the
  threads the operation is granted; RGB triples the sample work relative to mono.
- Poisson draws take a bounded number of uniform draws per sample whatever the
  rate, so they cost more than the other modes but cannot run unbounded.
- No global lock serializes the generator with other processes.
- Preview area reduces tuning work. No production timing or peak-memory
  measurement was run.

## Worked example

- **Input condition and settings:** Constant finite mono image at `0.5`, Gaussian,
  Mean `0`, Var `0.002`, no mask.
- **Expected checkpoint:** A generated result differs from the constant source,
  is clipped to `[0,1]`, and has an intended standard deviation of
  `sqrt(0.002) ≈ 0.0447` before finite-sample variation/clipping.
- **Reproducibility checkpoint:** Trigger two full-frame previews, or a
  full-frame Preview then Apply. With the same settings the sample values match,
  because both draw from the dialog's held seed; a second Apply after the first
  completes draws different noise.
- **Inspect at 100%/history:** Confirm grain on the selected process Channel and
  verify history records type/channel only.
- **Limitations:** No claim is made for exact sample statistics, visual realism,
  production runtime, sequence output, or memory without an approved execution.

## Warnings and limitations

- There is no seed control or seed history. The held seed reproduces noise only
  within one dialog session, until an Apply completes.
- This is not a calibrated sensor model and does not read gain, exposure,
  temperature, read-noise, dark-current, or camera quantization metadata.
- LocalVar is not spatially varying; it is Gaussian noise of constant variance
  `0.01`. The visible Mean/Var values do not affect it.
- Mode switches can leave ignored controls enabled: Gaussian to Salt/Pepper/Salt
  & Pepper can leave Mean/Var enabled; Salt & Pepper to Speckle can leave Salt vs
  Pepper enabled. Backend routing, not enabled appearance, determines effect.
- RGB randomness is per channel sample. Salt/pepper can produce colored impulses.
- Output clips and returns to source type; clipping and integer rounding are
  irreversible.

## Troubleshooting

| Symptom | Cause | Fix |
| --- | --- | --- |
| Apply does not match a Preview area | The area is drawn on its own | Compare a full-frame preview with Apply |
| A second Apply adds different noise | The dialog draws a new seed after each completed Apply | Expected; a completed Apply's seed cannot be reused, so duplicate the noisy result when identical noise is needed elsewhere |
| Mean/Var appears enabled but has no effect | Stale control enablement after switching to an impulse mode; the generator ignores the values | Use only the mode-consumed controls in this page; Reset before changing modes if clarity is needed |
| LocalVar looks like uniform-strength Gaussian noise | LocalVar is Gaussian noise of constant variance `0.01` | Use Gaussian for a tunable uniform variance; use another tool for a spatial variance map |
| RGB salt/pepper contains colored pixels | Replacement decisions are drawn for every channel sample independently | Select a luminance/component channel if impulses should share one reconstructed plane |
| Highlights/shadows flatten | Generated values are clipped to full scale before type conversion/mask blend | Reduce Mean/Var/Amount and work on a duplicate |
| Result is unchanged | Gaussian/Speckle Var `0` with Mean `0`, impulse Amount `0`, or mask protection | Check mode-consumed values and mask/inversion |
| An error is reported | Invalid data or insufficient memory | Verify target data; retry on a duplicate |

## Related workflow steps

Use this process to build controlled stress inputs before comparing
[Wavelet Denoise](../noise-reduction/wavelet-denoise.md),
[Total Variation Denoise](../noise-reduction/total-variation-denoise.md), or
stacking rejection. Use [Pixel Math](../pixel-math/pixel-math.md) when a formula
with a stated seed is required, and [Image History](../../interface/image-history.md)
to compare or revert destructive Apply. Save test fixtures through
[Saving and Exporting](../../interface/saving-and-exporting.md), noting that this
process does not preserve its seed for later recreation.

## Credits

- [scikit-image 0.26.0 `random_noise`](https://scikit-image.org/docs/stable/api/skimage.util.html#skimage.util.random_noise) — the noise modes and parameters whose distributions the engine's generator follows; this process does not use scikit-image or Python.
