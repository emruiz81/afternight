# High Pass Filter

## Purpose and expected result

High Pass Filter replaces the target with a neutral-centered detail layer. It
subtracts the exact Gaussian blur of the source from the source, adds the fixed
offset `0.5` (half of full scale), and clips the result. Smooth regions become middle gray; transitions
become lighter or darker than gray according to the sign of the residual.

This process does **not** directly sharpen the image. There is no Amount control,
blend mode, or luminance-only Channel selector. Apply destructively replaces the
target with the detail layer. Use it to inspect or prepare frequency detail for
a deliberate downstream layer/compositing workflow, not as a one-click final
sharpen.

## At a glance

| Item | Current behavior |
| --- | --- |
| Menu path | **Process > Details > High Pass**; window title **High Pass Filter** |
| Enabled when | An image or sequence is active, the same process is not already attached to it, and no attached process holds exclusive access |
| Inputs | One grayscale or RGB still or sequence; a still CFA mosaic is treated as one grayscale sample plane; an optional active mask must match target geometry |
| Outputs | Destructively replaces a still, or after successful temporary processing a sequence, with a same-size, same-type neutral-gray detail layer; **Take snapshot** creates an auxiliary still; no file is written directly |
| Recommended image state | Finite, normalized data after calibration/registration/stacking and noise control; duplicate the target because the output is a layer, not the original plus detail |
| Channel behavior | No process Channel selector: every stored channel is blurred and subtracted independently; RGB neutral is `(0.5,0.5,0.5)` |
| Preview support | Full-image preview initially; while Radius is dragged, frames run over what the preview shows - the image reduced by its zoom, or the part shown at 100% - and the settled frame is the full image, as Apply computes it; optional movable `600×600` Preview area when enabled, original/result switching, histogram, snapshot, and display-channel selection; no Display Mode or Autostretch Settings |
| Mask support | Yes, but it crossfades the original and the neutral detail layer; it does not perform masked sharpening |
| Sequence support | Yes; one captured Radius and mask state is applied frame by frame to temporary storage, with frame progress and Cancel, before replacement |
| External dependency | None; the AfterNight engine's own detail filter; no Python, G'MIC, network, or separate installation |

## Before you start

- Duplicate the target. Apply replaces the image with a high-pass layer; it does
  not retain the original appearance.
- Reduce noise and hot-pixel residue first. High-frequency noise appears as real
  detail around neutral gray.
- Use finite, normalized samples. The residual is always clipped to `[0,1]`;
  out-of-range and non-finite data are not preserved safely.
- Debayer a CFA still before making a color-detail layer. Otherwise the filter
  extracts the sensor mosaic pattern as one grayscale plane.
- There is no luminance-only process Channel. Extract or duplicate a luminance
  image first if chromatic residuals are not wanted.
- Expect long runs at very large Radius. Above `32` the blur is composed of
  several exact passes, and the count grows with the square of Radius.

## Procedure

1. Duplicate a finite, normalized still and open **Process > Details > High
   Pass**. Confirm **Target Image** and leave **Preview** checked.
2. Start with **Radius 10.0**. The histogram should cluster smooth areas near
   `0.5`; enable clipping information to see residuals reaching black or white.
3. Toggle **Switch image**. Smaller useful radii isolate finer transitions;
   larger radii retain progressively broader structure in the residual.
4. Inspect at **Zoom 100%** for noise, bright/dark edge pairs, star rings, and
   independent RGB color residuals. Enable **Preview area** only to reduce tuning
   work; it starts unchecked and initializes a centered `600×600` area.
5. Check the full-frame preview and histogram. If the result will be composited
   elsewhere, prefer **Take snapshot** so the original target remains unchanged.
6. If Apply is intentional, select **Apply**. For a sequence, monitor frame
   progress; Cancel preserves the original. Radius returns to `10` after
   success.

## Complete parameter and action reference

| UI label/action | Type and unit | Runtime default | Persisted-default exception | Valid values, step, or constraints | Effect of increasing, decreasing, or selecting | Dependencies and enable/visibility rules | Quality, artifact, runtime, memory, or disk trade-off | Scope: preview/apply, channel, mask, still/sequence | Persistence |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Histogram plot | Graph, normalized intensity/count | Source or current mask-aware preview; RGB channels overlaid | None | Binning comes from **Plot resolution** | Shows the source/detail-layer distribution and approximate endpoint-bin clipping; a clean smooth residual clusters at `0.5` | **Show histogram** controls visibility | Recalculation adds work; endpoint counts are resolution-dependent | Display only; stored channels | Recomputed after source, preview, or resolution changes |
| Left-drag in histogram | Graph gesture | No readout | None | Hold left button and move | Shows normalized coordinate readout | Histogram shown | Diagnostic only | Display only | Ends on release |
| Middle-drag in histogram | Graph gesture | Fitted/unpanned | None | Hold middle button and drag | Pans within graph scroll area | Useful beyond viewport | No output effect | Display only | Until reset/window close |
| Ctrl+mouse wheel in histogram | Graph gesture | Fitted size | None | Up `1.20×`; down `0.8×`; plain wheel scrolls | Zooms around cursor | Interactive graph enabled | Rendering area only | Display only | Window lifetime |
| Zoom in | Histogram-toolbar action | Fitted size | None | Each action `1.30×` | Enlarges plot | Disabled with hidden histogram | Rendering only | Display only | Window lifetime |
| Zoom out | Histogram-toolbar action | Fitted size | None | Each action `0.70×` | Shrinks plot | Disabled with hidden histogram | Rendering only | Display only | Window lifetime |
| Zoom reset | Histogram-toolbar action | — | None | One action | Fits plot and resets scrolling | Disabled with hidden histogram | Rendering only | Display only | Immediate |
| Show grid | Histogram toggle | Checked | None | Checked/unchecked | Shows or hides grid | Disabled with hidden histogram | Display-only aid | Display only | Window lifetime; hiding histogram resets grid internally |
| Show clipping info | Histogram toggle | Checked | None | Checked/unchecked | Shows or hides endpoint-bin counts | Disabled with hidden histogram | Binning-dependent estimate; useful for residual saturation but not an exact process-clipping count | Display only | Window lifetime |
| Absolute scales | Histogram toggle | Unchecked | None | Checked/unchecked | Checked normalizes each visible channel histogram to its own peak | Disabled with hidden histogram | Can conceal relative channel strength; never normalizes pixels | Display only | Window lifetime |
| Draw histogram areas | Histogram toggle | Unchecked | None | Checked/unchecked | Filled-area versus line drawing | Disabled with hidden histogram | Area overlap can obscure RGB residual differences | Display only | Window lifetime |
| Show histogram | Histogram toggle | Checked | None | Checked/unchecked | Hides/clears graph and disables toolbar, or reloads source/preview | Always available while idle | Hiding saves graph work, not filtering | Display only | Window lifetime; Reset unchanged |
| Plot resolution | Combo box, bins | **8-bit (256)** | None | **6-bit (64)**, **7-bit (128)**, **8-bit (256)**, **9-bit (512)**, **10-bit (1024)**, **12-bit (4096)**, **14-bit (16K)**, **16-bit (64K)**, **100 levels**, **1000 levels**, **10000 levels** | More bins refine the neutral peak and endpoint estimates | Disabled with hidden histogram | Higher bins cost graph memory/rendering; output precision unchanged | Display only | Window lifetime |
| Radius | Double value plus linear slider, requested Gaussian sigma in pixels | `10.00` | Label says Radius, but the engine treats it as the standard deviation of an exact Gaussian | `0.10..1000.00`; two decimals; spin step `0.50`; slider is remapped to 1001 positions over the full range, about one unit per tick | Small values retain only the finest residual; larger values subtract a broader base, so broader structures remain in the layer; at `0.10` the Gaussian barely blurs and the layer is almost uniformly neutral | Enabled while idle | Cost grows with Radius; above `32` the blur is composed of several exact passes, about `(Radius/32)²` of them, so the largest values are very slow to apply; while the slider is dragged, the preview blurs beyond `32` by three box passes whose cost does not grow with Radius | Preview/Apply; all stored channels; before mask; still or sequence | Captured per execution; Reset and successful still Apply return `10.00` |
| Target Image | Image selector | Attached/active target | None | Open image or sequence | Changes source, histogram, Preview, active mask, and Apply destination | Active image required; duplicate/exclusive launch gates apply | A wrong target is destructively replaced after success | Preview/Apply source and active mask | Target lifetime; Reset unchanged |
| Preview | Checkbox | Checked | None | Checked/unchecked | Enables/disables asynchronous full-image or selected-area high-pass execution and changes histogram source/result | Process idle | Runs blur, residual, clamp, mask, and histogram passes | Preview only; all stored channels; still/current displayed sequence frame | Window lifetime; Reset unchanged |
| Preview pane | Image viewer | High-pass result in Linear mode; window initially uses shared `1000×700` default | Execution autostretch is not enabled for this process | Pan with scroll bars or dragging; toolbar actions below | Shows the mask-aware neutral detail layer without modifying target | Exists only with Preview | Holds source/result/display and blur buffers | Preview only | Window lifetime |
| Fit To Window | Preview-toolbar action | Preview fitted when first displayed | None | Click or **Ctrl+Shift+F** | Fits whole image uniformly | Preview image required | Good for broad residuals; resampling hides fine noise | Preview display only | Viewer lifetime |
| Zoom In | Preview-toolbar action | Current scale | None | Click; `1.25×` relative zoom | Enlarges around viewport center | Preview image required | Improves local inspection | Preview display only | Viewer lifetime |
| Zoom Out | Preview-toolbar action | Current scale | None | Click; `0.8×` relative zoom | Reduces around viewport center | Preview image required | Shows context but hides fine residuals | Preview display only | Viewer lifetime |
| Zoom 100% | Preview-toolbar action | Current scale | None | Click; absolute scale `1.0` | Shows one-pixel viewer scale | Preview image required | Preferred for noise, rings, and color residuals | Preview display only | Viewer lifetime |
| View Channel | Preview-toolbar combo box | **RGB** for RGB; disabled **Grayscale** for mono | None | Stored and derived RGB/CIE/HSL/HSV/HSI/YCbCr display components | Selects only the displayed component; it is not a process Channel | RGB preview required for components | Helps inspect colored residuals without changing pixels | Preview display only | Viewer lifetime; not in history |
| Switch image | Preview-toolbar action | High-pass result shown | None | Click; **Space**; right-click; **Ctrl+Shift+O** | Toggles result/original | Preview required | Comparison only; no recomputation | Preview display; still/current frame | Preview-window lifetime |
| Preview area | Preview-toolbar checkbox | Unchecked | When first enabled without an explicit process size, initializes centered `600×600` or full image if smaller | Checked/unchecked; move/resize overlay | Limits preview/snapshot processing and composites the patch over the original | Preview required | Reduces tuning work; can hide broad-radius context; Apply remains full-frame | Preview and Take snapshot; mask cropped consistently | Preview-window lifetime; Reset unchanged |
| Take snapshot | Preview-toolbar action | No snapshot | None | Click or **Ctrl+Shift+K** | Re-executes Radius and opens `<target> - High Pass Filter - SNAPSHOT` | Preview required | Allocates an auxiliary still; safest way to retain original target while creating a detail layer | New still; area/mask honored | Each action creates a separate view |
| Apply (still) | Button/action | Enabled at every valid Radius | None | One action | Captures Radius, locks the target, filters every stored channel, applies mask, and replaces the still | Still target and successful lock; controls disabled during commit | No progress dialog or Cancel is provided for a still; destructive layer replacement | Full still; all channels; mask crossfade | Records `Apply high pass filter with radius <value>`, then resets Radius |
| Reset | Button/action | Enabled while idle | None | One action | Restores Radius `10`; if Preview is off, reloads source histogram | Does not change Target, Preview, histogram, mask, or preview toolbar | Checked Preview schedules another execution | Configuration only | Immediate; also invoked after successful still Apply |
| Close | Button/action | Enabled while idle | None | One action | Closes without applying current Radius | Disabled during still Apply | Requests preview cancellation, which reaches the blur between rows | Window/execution | Settings discarded at destruction |
| Apply (sequence) | Same visible Apply action | Shared frame progress dialog with Cancel | None | One action | Applies the captured Radius and mask to every frame in temporary storage | Sequence target and successful lock | Decode/encode, temporary disk, and the blur per frame add cost; Cancel stops the current frame between rows and removes temporary output | Sequence target; every frame | Success replaces the sequence and records one history step, then resets Radius |

## How the algorithm works

### Neutral-centered residual

For a stored channel sample `I(p)`, the engine computes the exact Gaussian base
`B_r(p)` with standard deviation equal to the displayed Radius `r`, then returns

\[
H_r(p)=\operatorname{clip}_{[0,1]}\left(I(p)-B_r(p)+0.5\right).
\]

The fixed `0.5` represents zero residual. If a region is constant, its blur is
the same constant and its output is `0.5`, regardless of the original level.
Where the source is locally brighter than its base, the layer rises above gray;
where it is darker, the layer falls below gray. A hard transition therefore
creates paired light/dark structure. Values below `0` or above `1` are clipped,
so residual magnitude beyond `0.5` is lost.

Radius controls the low-pass base, not the intensity of the result. Increasing
it removes a broader base and leaves a wider band of structures in `H`. There is
no Amount control. A conventional high-pass sharpening composite would require
a separate blend of this layer with the original; this process does not perform
that step.

### Exact Gaussian base

The base at Apply, and in the preview once Radius settles, is a separable
Gaussian: taps sampled at each pixel centre out to four standard deviations,
renormalized to sum to one, applied along the rows and then down the columns.
Beyond each edge the image is mirrored without repeating the edge sample. The
engine's widest kernel has a radius of `128` pixels, which four standard
deviations reach at Radius `32`; a wider Radius is composed of several exact
passes of equal width whose variances add up to `r²`, so the base is still the
Gaussian of the requested Radius.

At the minimum Radius `0.10` the Gaussian barely blurs, so `B≈I` and the layer
is almost uniformly the neutral `0.5`. The minimum does not preserve the source
and does not reveal detail.

### The preview while Radius is dragged

While the Radius slider is held, each preview frame runs over what the preview
shows rather than over the whole image. Zoomed out, it runs over the image
reduced by the view's zoom - each pixel the mean of its block - with Radius
reduced by the same factor. Because the clip to `[0,1]` does not commute with
averaging, such a frame comes close to the reduction of the full result rather
than being it, most visibly around bright stars. At 100% and closer it runs at
full scale over the part of the image the view shows, grown by the blur's reach,
and is the Apply's answer there up to Radius `32`. Beyond `32` a dragged frame
blurs by three running-sum box passes along each axis, whose cost does not grow
with Radius and which stay within a few parts in a thousand of the Gaussian.

Releasing the slider, or holding it still, shows the settled frame: the whole
image at full scale, the same samples Apply produces. A typed Radius is shown
first by such a frame over what the view shows, then by the settled one.

### Channels, types, mask, and clipping

Grayscale uses one stored plane. RGB runs the same scalar operation separately
on R, G, and B; there is no shared luminance or process Channel conversion.
Neutral RGB is gray, but unequal channel residuals can create colored edges.

Every type is filtered as fractions of full scale - an integer type's lowest
code is `0` and its highest `1` - so the layer means the same thing for an
integer image as for a float one. An image in which every sample is valid is
computed on a single-precision working copy of those fractions at every Radius,
blurred by the engine's compiled Gaussian kernels - the ones Gaussian Blur uses
- and converted back once, rounded to the nearest code for an integer image. An
image that states some of its samples are not valid is filtered in its stored
units up to Radius `32`, the blur accumulating in double precision and reading
its border substitute in place of an invalid neighbour, and on the working copy
beyond. Double data is filtered at single precision. The midpoint may round to
the nearest available integer code.

If an active mask has effective normalized weight `m(p)`, the shared process
path computes

\[
O(p)=(1-m(p))I(p)+m(p)H_r(p).
\]

Mask inversion substitutes `1-m`. This crossfades between an ordinary image and
a neutral detail layer. It is not the result of adding masked detail to the
original, and a mask edge can create an abrupt visual transition between those
two representations.

## Parameter interactions and starting points

| Combination | Conservative starting point | Too weak looks like | Too strong looks like | Adjustment order |
| --- | --- | --- | --- | --- |
| Radius × feature size | Radius `10`, then lower while inspecting at 100% | Small-radius layer is nearly uniform gray except very fine noise/edges | Large radius retains broad gradients/halos rather than only fine detail | Choose the widest feature that should remain, then reduce Radius |
| Radius × clipping | Watch endpoint bins and bright/dark edge pairs | Residual stays close to `0.5` | Peaks reach `0`/`1`, losing residual magnitude | Reduce Radius or preprocess extreme transitions |
| RGB × luminance-only goal | Inspect individual View Channels | RGB residual may be acceptable | Colored rims appear because channels differ | Extract luminance first; High Pass has no process Channel |
| Radius × mask | Prefer a snapshot/unmasked layer | Masked area receives little detail-layer representation | Boundary visibly switches between original and gray-centered layer | Create the layer first; composite deliberately downstream |

These are source-derived workflow checkpoints, not executed astronomical
quality measurements. A useful radius depends on sampling, seeing, noise,
feature scale, and the intended downstream blend.

## Inputs, outputs, and metadata

- Output dimensions, stored channels, pixel format, and source pixel type match
  the source. Integer output is rounded; float/double residuals are clipped.
- The result keeps the source image's header cards and metadata. The process
  sets no processing-state transition.
- The inherited linear/nonlinear state can therefore describe the source rather
  than the new neutral detail layer. Treat it as unreliable provenance.
- Image History records only `Apply high pass filter with radius <value>`; it
  does not record the fixed offset `0.5`, mask/inversion, lack of Channel choice,
  or how many blur passes ran.
- Take snapshot creates a separate still from current preview scope. Display
  channel and histogram settings are not baked into its samples.
- Successful Apply resets Radius to `10`; Target, Preview, histogram, mask, and
  preview toolbar remain.
- A sequence is processed frame by frame into temporary storage with one
  captured Radius and replaces the original only after success; Cancel/error
  removes the temporary output.

## Performance

- The exact Gaussian costs two separable passes of `2 × ceil(4 × Radius) + 1`
  taps per sample, so work grows with Radius as well as pixel and channel count.
  Above Radius `32` each additional pass costs as much as one blur at `32`:
  Radius `100` takes ten passes, and Radius `1000` close to a thousand.
- RGB channels are blurred separately on the threads the operation is granted.
  Memory includes working planes for the blur, destination, preview
  presentation, mask, and histogram buffers.
- While Radius is dragged, the preview runs over what the view shows - the
  image reduced by the view's zoom, or the part shown at 100% - so its frames
  follow the slider on a large image; beyond Radius `32` they blur by box passes
  whose cost does not grow with Radius. The settled frame that follows the
  release costs as much as Apply.
- Preview area can reduce preview work after it is enabled; Apply always uses the
  full still. Histogram recomputation adds binning/rendering cost.
- The blur checks for cancellation between rows, so closing or cancelling stops
  it promptly.
- No approved astronomical workload has been timed and peak memory has not been
  measured.

## Worked example

- **Evidence mode:** Source-and-equation-derived; not executed on approved
  astronomical data.
- **Constant checkpoint:** If `I=0.20` throughout a region, `B=0.20`, so
  `H=0.20-0.20+0.50=0.50`. The same is true for any constant normalized level.
- **Edge checkpoint:** If a local source sample is `0.80` and its blurred base is
  `0.60`, the layer sample is `0.70`; if source is `0.40` over the same `0.60`
  base, it is `0.30`.
- **Mask checkpoint:** With original `0.20`, high-pass `0.70`, and mask `0.25`,
  the committed sample is `0.20×0.75 + 0.70×0.25 = 0.325`; it is not `0.20`
  plus one-quarter of the residual.
- **Inspect:** Smooth histogram mass near `0.5`, endpoint clipping, star rings,
  background noise, separate RGB residuals, and the Radius-only history step.
- **Limitations:** No sequence, visual improvement, compositing result,
  production runtime, or memory result is claimed.

## Warnings and limitations

- Apply replaces the original with a neutral-gray detail layer. It does not
  sharpen or blend that layer back into the source.
- There is no Amount or process Channel control. RGB channels are independent;
  luminance-only work requires a separate image/component workflow.
- Radius `0.10` produces an almost neutral layer because a Gaussian that narrow
  barely blurs. The control minimum is not an identity image output.
- Very large Radius values are slow: above `32` the blur needs about
  `(Radius/32)²` exact passes.
- Residuals clip at black/white. Strong edges and out-of-range float data can
  lose magnitude; NaN and infinity are unsupported.
- Double input is filtered at single precision, and integer output is rounded
  to whole codes.
- Mirror borders pull reflected structure into the border region at large
  radii.
- Masks crossfade original and layer representations; they do not implement
  masked high-pass sharpening.
- The preview is linear-only and starts full-frame. No Display Mode or
  Autostretch Settings action is exposed.
- Still Apply and snapshots have no progress dialog; a sequence Apply shows
  frame progress and Cancel.

## Troubleshooting

| Symptom or exact error | Verified cause/validation path | Recovery | Evidence source |
| --- | --- | --- | --- |
| Result is gray instead of sharpened | The output is `source-blur+0.5`, not source plus detail | Undo or use a duplicate/snapshot; composite the layer deliberately elsewhere | High-pass equation |
| Radius `0.1` gives almost uniform gray | A Gaussian that narrow barely blurs, so the residual is near zero | Raise Radius | Exact Gaussian base |
| Bright/dark rings surround stars | A high-pass residual naturally has opposite-sign edge lobes; larger Radius broadens them | Reduce Radius; inspect before downstream blending | Residual equation |
| Colored edges appear | R, G, and B are processed independently with no luminance option | Extract a luminance component before High Pass | Process initialization and channel loop |
| Histogram reaches black/white | Residual exceeded the fixed ±`0.5` representable range | Reduce Radius or handle high-contrast transitions before filtering | Fixed offset and clamp |
| Masked result looks partly normal and partly gray | Shared mask crossfades original and detail layer | Create an unmasked layer and use an intentional later composite | Mask equation |
| Preview is different from an autostretched target | High Pass preview exposes Linear mode only | Compare under matched Linear display conditions | Process initialization |
| Apply, or the preview after the slider is released, is slow at a large Radius | Above `32` the blur is composed of about `(Radius/32)²` exact passes | Reduce Radius; while the slider is dragged the preview shows a faster approximation, and a smaller Preview area reduces the settled frame's work | Composed exact passes |
| An error is reported | Invalid data or insufficient memory | Verify finite normalized data and memory; retry on a smaller duplicate | Engine/process error path |

## Related workflow steps

Complete calibration, registration, integration, gradient correction, color
calibration, and appropriate [noise reduction](../noise-reduction/wavelet-denoise.md)
before extracting a final-scale detail layer. Use [Unsharp Mask](unsharp-mask.md)
for direct single-radius sharpening or [Sharpen Alpha](sharpen-alpha.md) for a
multiscale alpha reconstruction. Use [Image History](../../interface/image-history.md)
to revert an accidental destructive Apply and [Saving and Exporting](../../interface/saving-and-exporting.md)
to preserve a snapshot layer for an external/compositing workflow.

## References and provenance

- [Peter Kovesi, “Fast Almost-Gaussian Filtering”](https://www.peterkovesi.com/papers/FastGaussianSmoothing.pdf) — the repeated-box approximation of a Gaussian, which the preview runs while Radius is dragged beyond `32`; Apply and the settled preview use the exact Gaussian.
- AfterNight engine and process sources — authoritative for the exact Gaussian and its composed passes, mirror border, fixed `0.5` offset, clipping, type behavior, live controls, histogram, preview, mask, metadata/history, and sequence path.
