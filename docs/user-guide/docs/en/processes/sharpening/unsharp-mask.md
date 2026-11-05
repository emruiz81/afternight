# Unsharp Mask

## Purpose and expected result

Unsharp Mask enhances local transitions by subtracting a Gaussian-blurred copy
from the source and adding a scaled version of that detail back. It runs on
AfterNight's own engine as its detail filter, with the same equation, exact
Gaussian, and reflecting edge as scikit-image's `unsharp_mask`. It is a stable
linear sharpening model before range clipping,
not deconvolution and not a way to recover information that blur or
undersampling removed.

The expected result is a same-size image or replacement sequence with stronger
transitions at the chosen scale. Channel count normally matches the input, but
configured CFA sequence frames can be auto-debayered to RGB before filtering
and temporary replacement. Positive **Amount** sharpens; `0` is an identity;
`-1` returns the Gaussian-blurred image; values below `-1` reverse the extracted
detail more strongly. High positive values can amplify noise, sharpen
compression defects, and create bright/dark halos around stars and
high-contrast edges.

## At a glance

| Item | Current behavior |
| --- | --- |
| Menu path | **Process > Details > Unsharp Mask** |
| Enabled when | An image or sequence is active, the same process is not already attached to it, and no attached process holds exclusive access |
| Inputs | One grayscale or RGB still or sequence; a still CFA mosaic is treated as one grayscale sample plane unless debayered first; sequence auto-debayer can convert configured CFA frames to RGB before filtering; an optional active mask must match target geometry |
| Outputs | Destructively replaces the still or, after successful temporary processing, the sequence; **Take snapshot** creates an auxiliary still; no file is written directly |
| Recommended image state | Usually a finite, normalized nonlinear working copy for final-scale detail; linear use is possible when noise is controlled and scale/amount are tested deliberately |
| Preview support | Asynchronous full-image or movable-area preview, original/result switching, snapshot, display-channel selection, and display-only autostretch |
| Sequence support | Yes; Radius, Amount, and Channel are captured once when Apply starts and used for every frame; temporary output replaces the sequence only after success |
| Mask support | Yes; the active mask blends the clipped/reconstructed filter result with the original after filtering |
| External dependencies | None; the AfterNight engine's own detail filter. No Python interpreter, scikit-image, global lock, separate installation, or network access |

## Before you start

- Calibrate, register, stack, remove gradients, and reduce objectionable noise
  before final sharpening. Unsharp Mask boosts every transition, including noise
  and residual calibration defects.
- Debayer CFA mosaics before color sharpening. A CFA still is exposed as one
  grayscale plane, so sharpening it can couple the sensor pattern into detail.
  For a sequence, the target view's auto-debayer setting can instead convert
  configured CFA frames to RGB automatically before filtering and replacement.
- Duplicate the target or confirm Image History is available. Apply replaces the
  target after a successful full operation.
- Use finite, normalized samples. The process does not sanitize NaN or infinity;
  the Gaussian spreads non-finite values into their neighbours.
- Decide whether RGB or a luminance/lightness component matches the goal. The
  RGB path sharpens R, G, and B independently and can create color fringes.
- Compare at **Zoom 100%** under one display mode. Include small stars, a bright
  edge, smooth background, and fine low-contrast structure in the preview area.

## Procedure

1. Select the target and open **Process > Details > Unsharp Mask**. Confirm
   **Target Image**, select **RGB** or one deliberate component, and keep
   **Preview** checked.
2. Start with **Radius 1.0** and **Amount 1.0**. Use **Preview area** around a
   representative edge and background region to limit tuning work.
3. Set Radius for the transition width. Small radii emphasize fine pixels and
   noise; larger radii create broader bright/dark edge shoulders.
4. Adjust Amount after Radius. Toggle **Switch image** and inspect at **Zoom
   100%** for star rims, dark troughs, grain, and color fringes.
5. If needed, select a luminance/lightness Channel or enable a protective mask.
   Recheck the full-frame Preview because a local crop can miss broad halos.
6. Confirm target, Channel, mask/inversion, and full-frame behavior, then select
   **Apply**. For a sequence, monitor frame progress; Cancel stops the filter
   between rows and preserves the original sequence.

## Complete parameter and action reference

| UI label/action | Type and unit | Runtime default | Persisted-default exception | Valid values, step, or constraints | Effect of increasing, decreasing, or selecting | Dependencies and enable/visibility rules | Quality, artifact, runtime, memory, or disk trade-off | Scope: preview/apply, channel, mask, still/sequence | Persistence |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Radius | Double value plus slider, Gaussian sigma in pixels | `1.00` | None | `0.00..10.00`; spin step `0.10` with adaptive stepping; slider has 1000 positions | Widens the Gaussian blur and therefore the scale of the extracted detail; `0` leaves the image unchanged | Enabled while idle; disabled during Apply | Small values emphasize fine noise; large values create broad halos and require wider convolution support | Preview/Apply; selected channel/component; still/sequence; before mask | Captured per execution; Reset and successful Apply return `1.00` |
| Amount | Double value plus slider, detail multiplier | `1.00` | None | `-10.00..10.00`; spin step `0.10` with adaptive stepping; slider has 2001 positions | Positive values add detail; `0` returns the source; `-1` returns the Gaussian blur; more-negative values reverse/overshoot detail before clipping | Enabled while idle; disabled during Apply | Large positive values amplify noise/halos; negative values smooth at `-1` but can create reversed halos beyond it; filter cost is unchanged at identity endpoints | Preview/Apply; selected channel/component; still/sequence; before mask | Captured per execution; Reset and successful Apply return `1.00` |
| Target Image | Image selector | Attached/active target | None | Open eligible image or sequence | Changes source, Channel list, Preview, active mask, and Apply destination | Active image required; duplicate/exclusive launch gates apply | A wrong target is destructively replaced after success | Preview/Apply source and active mask | Target lifetime; Reset does not change it |
| Channel | Combo box | **RGB** for RGB; disabled **Grayscale** for mono | None | **RGB**, **Red**, **Green**, **Blue**, **CIE L* (Lightness)**, **CIE a***, **CIE b***, **CIE c***, **CIE h***, **CIE X**, **CIE Y (Luminance)**, **CIE Z**, **HSL Hue**, **HSL Saturation**, **HSL Lightness**, **HSV Hue**, **HSV Saturation**, **HSV Value**, **YCbCr Luma**, **YCbCr Blue Chroma**, **YCbCr Red Chroma** | RGB sharpens each stored channel independently; a component filters one plane and reconstructs/copies the others | Component choices require RGB; mono selector is disabled; disabled during Apply; the selection is captured when Apply starts | Independent RGB can create color fringes; component round trips add buffers, quantization for integer data, and possible gamut clipping | Preview/Apply; active mask post-blend; still/sequence | Dialog-local; Reset and successful Apply restore RGB on RGB targets |
| Preview | Checkbox | Checked | None | Checked/unchecked | Enables/disables asynchronous full-image or selected-area execution | Process idle | Runs the complete filter; frequent changes allocate Gaussian work buffers | Preview only; mask-aware; still/current displayed sequence frame | Window lifetime; Reset does not change it |
| Preview pane | Image viewer | Filtered result | None | Pan with scroll bars or dragging; toolbar actions below | Shows the mask-aware result without modifying the target | Exists only with Preview | Holds source/result/display buffers in addition to filter buffers | Preview only | Window lifetime |
| Fit To Window | Preview-toolbar action | Preview fitted when first displayed | None | Click or **Ctrl+Shift+F** | Fits the whole image uniformly in the viewport | Preview image required | Good for broad halos; resampling can hide fine noise/ringing | Preview display only | Viewer lifetime |
| Zoom In | Preview-toolbar action | Current scale | None | Click; `1.25×` relative zoom within viewer limits | Enlarges around viewport center | Preview image required | Improves local inspection; increases panning | Preview display only | Viewer lifetime |
| Zoom Out | Preview-toolbar action | Current scale | None | Click; `0.8×` relative zoom within viewer limits | Reduces around viewport center | Preview image required | Shows more context but hides fine artifacts | Preview display only | Viewer lifetime |
| Zoom 100% | Preview-toolbar action | Current scale | None | Click; absolute scale `1.0` | Shows the viewer's one-pixel scale | Preview image required | Preferred for noise, small-star halos, and color fringes | Preview display only | Viewer lifetime |
| View Channel | Preview-toolbar combo box | **RGB** for RGB; disabled **Grayscale** for mono | None | Stored and derived RGB/CIE/HSL/HSV/HSI/YCbCr display components | Selects only the displayed component | Separate from process Channel; RGB target required for components | Helps diagnose color fringes without changing pixels | Preview display only | Viewer lifetime; not in process history |
| Display Mode | Preview-toolbar combo box | Inherits target **Linear** or **Autostretch** when preview is created | None | **Linear**, **Autostretch** | Changes display only; Autostretch uses prepared target/result statistics | Autostretch enables settings action | A strong stretch can exaggerate background grain and halos | Preview display only | Preview-view state; not in process history |
| Autostretch Settings | Preview-toolbar dialog action | Disabled in Linear mode; inherits target-view settings | Shared-view values load when opened | Opens the preview autostretch controls; **Apply Stretch** is hidden | Tunes preview display only | Autostretch mode only; disabled while its dialog is open | Changes visual judgment, never filter samples | Preview display only | Viewer-scoped; not an Unsharp Mask parameter |
| Switch image | Preview-toolbar action | Filtered preview shown | None | Click; **Space**; right-click; **Ctrl+Shift+O** | Toggles result/original | Preview required | Comparison only; no recomputation | Preview display; still/current frame | Preview-window lifetime |
| Preview area | Preview-toolbar checkbox | Checked with an initial `1200×1200` area when the preview is created | None | Checked/unchecked; move/resize overlay | Processes only the area and composites it over the original preview; uncheck for full-image preview | Preview required | Reduces tuning cost but can hide wider contextual halos; Apply is always full-frame | Preview and Take snapshot; mask is cropped consistently | Preview-window lifetime; Reset unchanged |
| Take snapshot | Preview-toolbar action | No snapshot | None | Click or **Ctrl+Shift+K** | Re-executes settings and opens `<target> - Unsharp Mask - SNAPSHOT` | Preview required | Adds a complete execution and auxiliary image; display stretch is not baked in | New still; target unchanged; area/mask honored | Each action creates a separate image view |
| Apply | Button/action | Enabled | None | One action | Captures Radius, Amount, and Channel, locks the target, and filters the full still or every sequence frame | Target must lock; target, Channel, and process controls are disabled during Apply; another Apply is ignored while execution is in progress | Component conversion, mask, sequence encode, and temporary storage add cost | Destructive still/sequence replacement after success | Records Radius/Amount and, for a colour image, the Channel label in history, then resets Radius/Amount and RGB selection |
| Reset | Button/action | Enabled while idle | None | One action | Restores Radius `1`, Amount `1`, and RGB on an RGB target | Does not change Target, Preview, mask, or preview toolbar; disabled during Apply | Checked Preview schedules another complete execution | Configuration only | Immediate; also invoked after successful Apply |
| Close | Button/action | Enabled while idle | None | One action | Closes without applying current settings | Disabled during Apply | Requests preview cancellation, which reaches the filter between rows | Window/execution | Process settings do not persist after destruction |
| Sequence progress / Cancel | Shared per-process/frame progress and Cancel | Created for sequence Apply | None | Cooperative Cancel; frame/task text updates around temporary processing | Sequence target only | The filter checks for Cancel between rows and tiles, so a frame stops promptly | Captured Radius, Amount, and Channel are used for every frame | Cancel removes temporary output; success replaces sequence, records one history step, and resets settings |

## How the algorithm works

### Conceptual pipeline

1. AfterNight's engine reads the selected image or component in its stored
   units, one channel at a time, in single precision with double-precision sums.
2. It applies an exact Gaussian with standard deviation Radius, a reflecting
   boundary that repeats the edge sample (as SciPy's `reflect` mode does), and
   support truncated at four sigma.
3. It subtracts the blur from the source, multiplies that detail by Amount, adds
   it to the source, and holds the result to full scale: `[0,1]` for a float
   image, the type's range for an integer one. A negative Amount is computed the
   same way over a single-precision working copy of fractions of full scale.
4. The shared Channel dispatcher reconstructs RGB when needed, keeps the source
   pixel type and metadata, and blends the active mask after filtering.

### Model and equations

Let `I(p)` be the selected sample at pixel `p`, let `G_r * I` be convolution
with a normalized Gaussian of standard deviation `r = Radius`, and let
`a = Amount`. The extracted high-frequency residual is

\[
D(p)=I(p)-(G_r*I)(p),
\]

and the unsharp result before range clipping is

\[
U(p)=I(p)+aD(p)=(1+a)I(p)-a(G_r*I)(p).
\]

This makes the endpoints predictable:

- `r = 0` gives `G_0*I = I`, so every Amount is an identity.
- `a = 0` gives `U = I` after the Gaussian still runs.
- `a = 1` gives `U = 2I - G_r*I`, the live default.
- `a = -1` gives `U = G_r*I`, an ordinary Gaussian blur.

For RGB, the engine runs this scalar model separately on R, G, and B. It does
not derive one shared luminance
or constrain hue. A selected lightness/luminance component instead runs the
same model on one converted plane and reconstructs RGB through the common
Channel path. A derived component's reconstruction saturates an integer image
at its type's range and keeps a floating image's values outside `[0, 1]`.

The clip range is the image's full scale. A float image is held to `[0,1]`,
so negative float samples are clipped to `0`; an integer image is held to its
type's range and rounded once when written back. Double input is filtered at
single precision and written back to double storage. The focused tests hold the
result to scikit-image's `unsharp_mask` over the recorded plane.

If an enabled mask has effective normalized weight `m(p)`, the shared process
path computes

\[
O(p)=(1-m(p))I(p)+m(p)U_c(p),
\]

where `U_c` is the clipped/reconstructed filter result. Mask inversion replaces
`m` with `1-m`. The filter still processes the entire selected preview/full
frame before this blend.

### Boundary and failure behavior

- Gaussian samples beyond an edge are reflected with the edge sample repeated,
  as SciPy's `reflect` mode does. Kernel support is truncated at four Radius
  values; cost/support grow with Radius.
- Radius cannot be negative in the UI. Radius `0` is accepted and is an identity.
- NaN/Inf are not rejected. The Gaussian spreads them into neighbouring float
  samples.
- Engine failures become an error notification; the still target is committed
  only after successful processing.
- The filter checks for Cancel between rows and tiles, so Cancel stops a still
  or a sequence frame promptly. No global lock serializes it with other
  processes.
- Sequence work uses temporary storage. Cancel removes the temporary sequence;
  success replaces or creates the destination and records one history step.

## Parameter interactions and starting points

| Combination | Conservative starting point | Too weak looks like | Too strong looks like | Adjustment order |
| --- | --- | --- | --- | --- |
| Radius × Amount | Radius `1`, Amount `1`, then reduce Amount if needed | Fine transitions remain unchanged | Bright/dark star rims, crunchy edges, boosted grain | Choose scale with Radius, then strength with Amount |
| RGB × luminance component | Compare RGB with **CIE Y (Luminance)** or **CIE L* (Lightness)** on a duplicate | Luminance-only result may leave chromatic noise | RGB creates colored edge fringes or unequal channel noise | Inspect View Channel, choose process Channel, then tune Radius/Amount |
| Amount × mask | Tune globally at modest Amount, then localize with a mask | Protected detail receives little effect | Mask edges or unprotected stars show abrupt halos | Tune unmasked preview, verify mask/inversion, inspect full frame |
| Negative Amount | Use `-1` only when a Gaussian blur is intentional | Values near `0` barely smooth | Values below `-1` create reversed edge overshoot | Set `-1`, choose Radius, then move toward `0` if too soft |

These are workflow starting points, not executed quality measurements.
The useful scale and strength depend on sampling, seeing, prior noise reduction,
and the final output size.

## Inputs, outputs, and metadata

- Output dimensions match the input. Channel count normally matches, except that
  sequence auto-debayer can convert configured CFA frames to RGB before the
  filtered temporary replacement is written. The source pixel type is kept;
  integer results are rounded and double data is processed at float32
  precision.
- Apply replaces the still only after the complete result is ready. Preview and
  Take snapshot do not mutate it; a snapshot creates a separate still view.
- The result keeps the source image's header cards and metadata. The process
  adds an Image History step containing Radius, Amount, and, for a colour image,
  ` in channel: <channel>`.
- It does not explicitly change linear/nonlinear processing-state provenance.
- Sequence Apply writes a temporary sequence, applies the same captured Radius,
  Amount, and Channel per frame, and replaces the original after success. Configured CFA frames can be auto-debayered to RGB
  first. The process writes no standalone report, mask, or high-pass layer.
- Successful Apply calls Reset. Radius and Amount return to `1`; an RGB target
  returns to RGB Channel. Target, Preview, mask, and preview-display state remain.

## Performance

- Gaussian convolution, float32 conversion, and copies scale with pixel count
  and channel count. Effective Gaussian support grows with Radius up to the UI
  maximum of `10` pixels.
- RGB processes three planes one after another on the threads the operation is
  granted, with working planes for the blur. Derived components add conversion
  and reconstruction.
- No global lock serializes this filter with other processes.
- Preview area reduces tuning cost; Apply still processes the full target. Use a
  representative crop and defer full-frame checks until scale/amount are stable.
- No production timing or peak-memory measurement was executed.

## Worked example

- **Evidence mode:** Source-and-equation-derived; not executed on approved
  astronomical data.
- **Input condition and settings:** A finite normalized image with one narrow
  star-like transition and smooth background; Radius `1`, Amount `1`, RGB or one
  deliberate luminance component.
- **Expected checkpoint:** The equation adds exactly one copy of
  `source - Gaussian(source)` before clipping. Switching Amount to `0` must make
  the mathematical output identical; switching it to `-1` must select the
  Gaussian term.
- **Inspect at 100%/history:** Compare original/result at the edge and background;
  inspect separate display channels for fringes; after Apply, confirm the history
  records Radius, Amount, and Channel.
- **Limitations:** No visual improvement, production runtime, sequence output,
  or memory claim is made without approved-data execution.

## Warnings and limitations

- Unsharp Mask amplifies noise and existing ringing together with real detail.
- Positive Amount can produce bright and dark halos; larger Radius makes them
  broader. It does not restore information lost to blur.
- RGB channels are independent. Use a deliberate luminance/lightness component
  when color edges must remain stable, and check gamut/color round-trip effects.
- Output is clipped to full scale. Highlight/shadow overshoot can be
  irreversibly flattened before mask blending, and negative float samples are
  clipped to `0`.
- Float values outside `[0,1]`, NaN, and infinity are not safely preserved.
  Double input is filtered at float32 precision.
- There is no threshold control to protect low-contrast noise and no automatic
  halo suppression. Use noise reduction, modest Amount, and masks deliberately.

## Troubleshooting

| Symptom or exact error | Verified cause/validation path | Recovery | Evidence source |
| --- | --- | --- | --- |
| Colored rims appear around stars | RGB applies the scalar model independently to R, G, and B | Compare CIE Y/L* or another deliberate luminance component; reduce Amount | Per-channel filter; shared Channel dispatcher |
| Bright/dark halos surround edges | Radius sets halo scale and positive Amount adds the residual | Reduce Amount first; then reduce Radius and inspect full-frame context | Unsharp equation and UI ranges |
| Result is unchanged | Radius `0`, Amount `0`, mask protection, or clipping may remove the visible effect | Check Radius/Amount, mask/inversion, process Channel, and one fixed display mode | Equation; shared mask blend |
| Amount `-1` looks blurred | This exact endpoint cancels the source term and leaves the Gaussian blur | Use a positive Amount for sharpening or move negative Amount toward `0` for weaker smoothing | `U=(1+a)I-aB` |
| Noise becomes coarse or crunchy | Fine residual and noise are amplified together | Denoise first; reduce Amount/Radius; protect smooth background with a mask | Linear residual model |
| Preview differs from Apply | Preview area, target/channel, mask, or display stretch changed | Match all controls and display state; uncheck Preview area for the final check | Shared preview/full execution paths |
| Negative background samples become `0` | The result is held to full scale, `[0,1]` for a float image | Offset or rescale the image into `[0,1]` before sharpening | Full-scale clamp |
| An error is reported | Invalid runtime data or insufficient memory | Verify target data and available memory; retry on a duplicate | Process error path |

## Related workflow steps

Complete calibration, registration, integration, gradient correction, color
calibration, stretching, and appropriate [noise reduction](../noise-reduction/wavelet-denoise.md)
before final detail enhancement. Compare [Sharpen Alpha](sharpen-alpha.md) when
detail must be distributed across several scales, or [High Pass Filter](high-pass-filter.md)
when the desired output is a neutral detail layer rather than a sharpened image.
Use [Image History](../../interface/image-history.md) to compare/revert Apply and
[Saving and Exporting](../../interface/saving-and-exporting.md) only after the
final output scale has been checked.

## References and provenance

- [scikit-image 0.26.0: `unsharp_mask`](https://scikit-image.org/docs/stable/api/skimage.filters.html#skimage.filters.unsharp_mask) — official equation, parameter endpoints, and channel warning of the filter the engine reproduces.
- [scikit-image: image data types and ranges](https://scikit-image.org/docs/stable/user_guide/data_types.html) — official float/range and conversion context; AfterNight's clip range is documented above from current source.
- AfterNight project sources — authoritative for the live menu, controls, engine detail filter, clipping, masks, metadata/history, preview, sequences, and cancellation behavior.
