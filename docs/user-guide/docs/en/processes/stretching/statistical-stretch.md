# Statistical Stretch

## Purpose and expected result

Statistical Stretch derives a black point and source median from the target, then
uses one rational transfer to place that median near **Target Median** while
keeping the computed black and normalized white anchors at `0` and `1`. It can
derive one pooled transform for RGB (**Linked Stretch**) or an independent
transform for each RGB channel. Optional post-stages lift the upper range with a
fixed piecewise-linear curve and divide by the output maximum.

The intended result is a nonlinear, same-size still image in the source pixel
type. On ordinary finite normalized data, defaults usually provide a quick
first display stretch. The target value is exact only for the transform's
computed median before **Apply Curves Adjustment**, **Normalize Image**, mask
blending, integer quantization, and output clamping. This is a one-pass mapping,
not an iterative solver.

Every sample at or below the computed black point becomes black, and every
statistic is the whole image's: the target's for **Apply**, the preview, and a
**Preview area**, and each frame's own for a sequence, whose frames are each
stretched from their own statistics.

## At a glance

| Item | Current behavior |
| --- | --- |
| Menu path | **Process > Intensity Transformations > Statistical Stretch** |
| Enabled when | An image or sequence is active, the same process is not already attached to it, and no attached process holds exclusive access |
| Inputs | One grayscale or three-channel RGB still image or sequence; any other channel count is refused with the error "a statistical stretch needs a mono or RGB source"; unsigned integer samples are read as fractions of their full scale, signed ones from their smallest value to their largest, and floating samples as they are; samples the image marks as holding no data are left out of the statistics; an optional active mask must match the target geometry |
| Outputs | Destructively replaces a still target after success; **Take snapshot** creates an auxiliary still without replacing the target; dimensions, channel count, pixel format, and pixel type are preserved; no file is written directly |
| Recommended image state | Linear, background-corrected, and color-calibrated data before the main display stretch |
| Preview support | Asynchronous RT preview that is the result **Apply** writes, plus a histogram of the mask-aware preview; disabling **Preview** returns the histogram to the source; a **Preview area** shows the whole target's stretch cropped to the area, exactly what **Apply** writes there |
| Sequence support | Supported: every frame is stretched from its own statistics, as the process stretches that frame alone, with progress, cooperative cancellation, and replacement of the sequence only after every frame succeeds |
| Mask support | Supported for preview, Apply, and sequences; white applies the result, black keeps the source, gray blends, and **Invert Mask** reverses the weights |
| External dependencies | None; execution uses the bundled engine and does not access a tool, model, catalog, or network service |

## Before you start

- Finish background correction and color calibration first. The estimator does
  not identify gradients, remove a pedestal selectively, or calibrate RGB.
- Use a one-channel or three-channel image. Any other channel count is refused
  by name when the process runs.
- Prefer floating-point data while stretching. Processing uses single-precision
  normalized buffers (double precision for a 64-bit floating or 32-bit integer
  image); integer results are rounded to the nearest code of the original type.
- Leave **Black Point Sigma** near its default until the clip readout and preview
  have been checked. Lower values raise the computed black point, and every
  sample at or below it becomes black.
- **No Black Clipping** keeps the threshold from rising above the image minimum.
  Despite its label, the minimum sample is still mapped to black and counted by
  the `<= black point` diagnostic.
- A **Preview area** is a view of the target: it shows the whole target's
  stretch cropped to the area.
- Duplicate the target or be prepared to use Image History. Apply replaces the
  current image after processing succeeds.

## Procedure

1. Select a calibrated linear still and open **Process > Intensity
   Transformations > Statistical Stretch**. Confirm **Target Image** and leave
   **Preview** selected.
2. Start with **Target Median: 0.25**, **Black Point Sigma: 5.0**, and all four
   checkboxes clear. Read the separate black-point/clipped-pixel line below the
   curve controls; it is not the histogram's endpoint-bin overlay.
3. On RGB, compare **Linked Stretch** off and on. Off targets each channel's own
   median and can neutralize a color cast. On pools all RGB samples to derive one
   transfer, but still evaluates that nonlinear function per channel and does
   not preserve RGB ratios.
4. Adjust **Target Median** for overall midtone placement. Do not assume the
   final measured median must equal the control when curves, normalization, a
   mask, quantization, or clipping is active.
5. If the computed black point rises above the source minimum, the readout's
   clipped pixels become black. Increase **Black Point Sigma** or select **No
   Black Clipping** when that loses faint signal.
6. Use **Apply Curves Adjustment** only when extra upper-midtone/highlight lift
   is wanted; then increase **Curves Boost** cautiously. Use **Normalize Image**
   only when scaling the global post-curve maximum to `1` is intentional.
7. Inspect histogram endpoints, background, bright stars, channel balance, and
   mask boundaries at 100% zoom, then select **Apply**.

## Complete parameter and action reference

| UI label/action | Type and unit | Runtime default | Persisted-default exception | Valid values, step, or constraints | Effect of increasing, decreasing, or selecting | Dependencies and enable/visibility rules | Quality, artifact, runtime, memory, or disk trade-off | Scope: preview/apply, channel, mask, still/sequence | Persistence |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Histogram plot | Graph, normalized intensity/count | Source or current preview; RGB channels overlaid | None | Binning comes from **Plot resolution** | Shows source or mask-aware result distribution and endpoint clipping; never changes samples | Always present; Preview chooses result versus source | Recalculation adds work; the endpoint counts do not depend on resolution | Display only; shown source/preview channels | Recomputed after source, preview, or resolution changes |
| Left-drag in histogram | Graph gesture | No readout | None | Hold left button and move in graph | Shows normalized coordinate readout | Histogram is always present | Diagnostic only; does not choose the statistical black point | Display only | Ends on button release |
| Middle-drag in histogram | Graph gesture | Fitted/unpanned | None | Hold middle button and drag | Pans the graph within its scroll area | Useful only when graph exceeds viewport | No output cost or effect | Display only | Until pan/zoom reset or window closes |
| Ctrl+mouse wheel in histogram | Graph gesture | Fitted size | None | Up `1.20x`; down `0.8x`; wheel without Ctrl scrolls normally | Zooms around the cursor | Interactive graph enabled | Larger graph uses more rendering area | Display only | Window lifetime; process Reset does not change it |
| Zoom in | Toolbar action | Fitted size | None | Each selection multiplies size by `1.30` | Enlarges plot | Always available while idle | Plot rendering only | Display only | Window lifetime; process Reset does not change it |
| Zoom out | Toolbar action | Fitted size | None | Each selection multiplies size by `0.70` | Shrinks plot | Always available while idle | Plot rendering only | Display only | Window lifetime; process Reset does not change it |
| Zoom reset | Toolbar action | — | None | One action | Fits graph and resets scrolling | Always available while idle | Plot rendering only | Display only | Immediate |
| Show grid | Toggle | Checked | None | Checked/unchecked | Shows or hides graph grid | Always available while idle | Display-only visual aid | Display only | Window lifetime; process Reset does not change it |
| Show clipping info | Toggle | Checked | None | Checked/unchecked | Shows or hides the share of samples at or below `0` and at or above `1` in the plotted image | Independent of the statistical clip-threshold text line | Counts exact endpoint samples, independent of Plot resolution; values just inside `0` or `1` are not counted | Display only; source or preview histogram | Window lifetime; process Reset does not change it |
| Absolute scales | Toggle | Unchecked | None | Checked/unchecked | Checked normalizes each visible channel to its own peak; unchecked preserves relative/global height | Always available while idle | Can conceal relative channel strength; never normalizes image data | Display only | Window lifetime; process Reset does not change it |
| Draw histogram areas | Toggle | Unchecked | None | Checked/unchecked | Selects filled-area or line rendering | Always available while idle | Area mode can obscure overlapping channel lines | Display only | Window lifetime; process Reset does not change it |
| Plot resolution | Combo box, bins | **8-bit (256)** | None | **6-bit (64)**, **7-bit (128)**, **8-bit (256)**, **9-bit (512)**, **10-bit (1024)**, **12-bit (4096)**, **14-bit (16K)**, **16-bit (64K)**, **100 levels**, **1000 levels**, **10000 levels** | More bins show finer distribution; the endpoint counts do not change | Always available while idle | Higher bin counts increase histogram memory/rendering; transform precision is unchanged | Display only | Window lifetime; process Reset does not change it |
| Target Median | Double value plus slider, normalized intensity | `0.25` | None | `0.01..0.99`; three decimals; spin step `0.01`; slider is linear over 1,000 positions | Raises or lowers the rational transfer's median anchor before optional post-stages | Enabled while idle | High targets brighten background/noise and compress more upper range; low targets give a darker result | Preview/Apply; pooled or per-channel transform; mask post-blend; stills and every frame of a sequence | Captured per execution; Reset or a successful Apply returns `0.25` |
| Linked Stretch | Checkbox | Unchecked | None | Checked/unchecked | Checked pools RGB samples and uses one black point/median/transfer; unchecked computes each channel independently | Enabled only when current target reports RGB; disabled for mono | Linked does not preserve RGB ratios; unlinked can neutralize or shift channel balance by forcing separate medians | Preview/Apply; RGB only; mask post-blend | Captured per execution; Reset or a successful Apply clears it |
| Black Point Sigma | Double value plus slider, robust-sigma multiplier | `5.0` | None | `0.5..10.0`; two decimals; spin step `0.1`; slider is linear over 1,000 positions | Larger values lower the threshold toward the source minimum; smaller values raise it and increase the nominal `<= threshold` count | Disabled while **No Black Clipping** is selected | Low settings clip more shadows to black; the spread is estimated from a deterministic sample of at most 400,000 values per channel/pool | Preview/Apply; pooled or per-channel estimator; stills and every frame of a sequence | Captured per execution; Reset or a successful Apply returns `5.0` |
| No Black Clipping | Checkbox | Unchecked | None | Checked/unchecked | Uses the image/channel minimum as black point instead of `median - sigma × robust noise` | Disables **Black Point Sigma** | Avoids any sample below the selected black point, but still maps/counts the minimum as black | Preview/Apply; pooled or per-channel; stills and every frame of a sequence | Captured per execution; Reset or a successful Apply clears it |
| Normalize Image | Checkbox | Unchecked | None | Checked/unchecked | After the stretch and optional curve, divides every processed sample by the single global maximum when that maximum is positive | Enabled while idle | Forces the brightest sample to `1`; changes the requested median and can amplify noise; one global RGB maximum preserves inter-channel scaling only at this stage | Preview/Apply; whole mono/RGB output; stills and every frame of a sequence | Captured per execution; Reset or a successful Apply clears it |
| Apply Curves Adjustment | Checkbox | Unchecked | None | Checked/unchecked | Adds the fixed six-anchor piecewise-linear upper-range lift after the rational stretch | Enables **Curves Boost** | Can brighten upper midtones, flatten highlights, and move the measured median/maximum | Preview/Apply; same curve on every stored sample; stills and every frame of a sequence | Captured per execution; Reset or a successful Apply clears it |
| Curves Boost | Double value plus slider, unitless | `0.20` | None | `0..1`; two decimals; spin step `0.05`; slider is linear over 1,000 positions | Raises two anchors above **Target Median**; `0` is identity and `1` drives both upper anchors to white | Enabled only with **Apply Curves Adjustment** | Strong values can create steep transitions and broad near-white highlights | Preview/Apply; all processed channels; stills and every frame of a sequence | Captured per execution even when ignored; Reset or a successful Apply returns `0.20` |
| Black clipping readout | Selectable diagnostic text | Computed from current target and defaults | None | Threshold shown to six decimals; count/total and percentage to four decimals | Updates for source, Linked Stretch, sigma, and No Black Clipping | RGB-unlinked shows R/G/B thresholds; RGB count is a pixel count where any channel is `<=` its threshold | Count is exact over the current precomputed image, but threshold noise uses deterministic subsampling; the text counts exactly the valid pixels the stretch sends to black in at least one channel | Source diagnostic only; not mask-aware; one pixel counted at most once | Recomputed; not saved |
| Target Image | Image selector | Attached/active target | None | Open eligible image or sequence | Changes source statistics, histogram, preview, and Apply | Active image required; duplicate/exclusive process gates apply; a channel count other than one or three is refused by name when the process runs | Wrong target is destructively replaced; a sequence's frames are each stretched from their own statistics | Preview/Apply source and active mask | Follows attachment/selection lifecycle; Reset does not change it |
| Preview | Checkbox | Checked | None | Checked/unchecked | Starts/removes asynchronous full-image preview and switches histogram between preview/source | Process must be idle to change it | Full-image float conversion, statistics, transform, mask blend, and histogram consume time/memory | Preview only; mask-aware; current still/current displayed sequence frame | Window lifetime; Reset does not change it |
| Preview pane | Full-image viewer | Current transformed target | None | Standard viewer zoom/pan/display controls | Shows the mask-aware full-image transform | Exists only while Preview is checked | Holds an additional float/result image; frequent parameter changes schedule new work | Preview only; target remains unchanged | Window lifetime |
| Switch image | Preview-toolbar action | Transformed preview shown | None | Click; **Space**; right-click; **Ctrl+Shift+O** | Toggles the preview viewer between the current transformed result and original target | Exists only while Preview is checked | Display comparison only; does not recompute or change pixels | Preview display only; still/current displayed sequence frame | Preview-window lifetime |
| Preview area | Preview-toolbar checkbox | Unchecked | None | Checked/unchecked; move/resize the selection overlay | Shows the whole target's stretch cropped to the area, exactly what **Apply** writes there; the area's own statistics are never used | Exists only while Preview is checked | Stretches the whole target for each change, its statistics measured once and kept | Preview and Take snapshot only | Preview-window lifetime; Reset does not change it |
| Take snapshot | Preview-toolbar action | No snapshot | None | Click or **Ctrl+Shift+K** | With Preview area clear, re-executes the current settings at source depth and opens a new still named `<target> - Statistical Stretch - SNAPSHOT` | Exists only while Preview is checked | Allocates and processes an auxiliary image; active mask is honored; with Preview area selected, it is processed as the preview is | New still image; target is unchanged | Each action creates a separate image view |
| Apply | Button/action | Enabled after initialization | None | One action | Captures all parameter values and destructively processes the target | Target must lock; controls disable during work | Full-image and parallel; a sequence's frames are each stretched from their own statistics | Stills, sequences, and the active mask | A successful commit records history and resets stretch settings |
| Reset | Button/action | Enabled while idle | None | One action | Restores `0.25`, `5.0`, `0.20`; clears all four checkboxes; recomputes statistics/readout | Does not change target, Preview, or histogram toolbar state/zoom | No target image change | Configuration only | Immediate |
| Close | Button/action | Enabled while idle | None | One action | Closes the process without applying current settings | Disabled with other controls during Apply; shared close handling applies to active workers | No output by itself | Window and active execution | No settings persist after destruction |
| Sequence progress / Cancel | Shared modal progress dialog and Cancel action | Created automatically when sequence Apply starts | None | One progress update per frame; cooperative Cancel | Processes into a temporary sequence, removes it on cancellation, and replaces the target only after success | Sequence target only | Protects the original sequence from partial replacement | Sequence framework; every frame stretched from its own statistics | Cancel discards the temporary sequence; success replaces the sequence, records one history step, and resets settings |

## How the algorithm works

### Statistics and black point

The process converts the whole target to normalized single-precision float
(double for a Float64 or 32-bit integer image) and measures it once for each
change of the image, keeping the measurement for every preview and run. For
mono and unlinked RGB it computes one median per channel. For linked RGB it
computes one median across the pooled RGB samples.

The minimum `x_min` and the median `M` are exact: every sample the image's
validity admits takes part, and the median of an even count is the lower middle
value plus half the distance to the upper one. The lower-half spread is
estimated from a deterministic sample of at most 400,000 values per stream.
It keeps sampled values at or below the sample's own median (which is `M` while
the sample holds every value), finds their median `L`, and estimates

\[
\sigma_{\mathrm{lower}}=1.4826\,
  \operatorname{median}_{x\le M}\left(|x-L|\right).
\]

With **Black Point Sigma** `k`, the black point is

\[
B=\min\!\left(0.99,
   \max\!\left(x_{\min},M-k\sigma_{\mathrm{lower}}\right)\right).
\]

**No Black Clipping** instead sets `B=x_min`. The control therefore prevents
the threshold from rising above the minimum; it does not make the minimum pass
through unchanged.

### Rational target-median transfer

For a source sample `x`, define the black-rescaled sample and median

\[
z=\frac{x-B}{\max(1-B,10^{-12})},\qquad
m=\frac{M-B}{\max(1-B,10^{-12})},
\]

and let `t` be **Target Median**. The engine evaluates

\[
f(x)=
\frac{(m-1)t z}
     {m(t+z-1)-tz},
\]

replacing a denominator whose absolute magnitude is below `10^-12` with
positive `10^-12`. After optional post-stages, it clamps the image to `[0,1]`.
When the denominator is well behaved, `f(B)=0`, `f(M)=t`, and `f(1)=1`.
This establishes anchors in one pass; there is no convergence loop or tolerance.
For an even-length sample set, transforming the two middle samples separately
also need not make their average exactly `t`.

`z` is held to `[0,1]` before the transfer, so every sample at or below `B`
becomes black - the population the clipping readout counts - and nothing above
white exceeds it.

### Linked and unlinked RGB

- **Linked Stretch** uses one pooled `M`, `B`, and `f` for all RGB samples, then
  applies the same nonlinear scalar function independently to R, G, and B. It
  is not luminance scaling and does not preserve channel ratios.
- Unlinked RGB derives `M_c`, `B_c`, and `f_c` separately for each channel. In
  the pre-post-stage transform, each channel median is directed toward the same
  target, which can reduce an existing background/color imbalance as well as
  stretch it.

### Optional curve and normalization

With **Apply Curves Adjustment**, AfterNight builds six x anchors

\[
0,\quad 0.5t,\quad t,\quad t+0.25(1-t),\quad
t+0.75(1-t),\quad 1.
\]

The first three and last y anchors equal x. For boost `q`, the other two are

\[
y_3=x_3^{1-q},\qquad y_4=x_4^{(1-q)^2}.
\]

Linear interpolation between the anchors leaves the range through `t`
unchanged and lifts the upper range when `q>0`. At `q=0` it is identity; at
`q=1` both adjustable anchors are `1`.

**Normalize Image** then finds one maximum over every processed sample (pooled
across RGB) and divides all values by it when it is positive. Final clamping and
conversion restore the source pixel type, rounding to the nearest code. The
output is marked nonlinear with operation provenance `statistical-stretch`; RGB
becomes display RGB and mono retains its monochrome basis. An active mask is
blended afterward.

## Parameter interactions and starting points

| Combination | Conservative starting point | Too weak looks like | Too strong or defective looks like | Adjustment order |
| --- | --- | --- | --- | --- |
| Target Median + Black Point Sigma | `0.25` and `5.0` | Background/faint structure remains too low | Washed background or crushed shadows | Establish a safe black point first, then set median |
| Linked + RGB balance | Compare unchecked and checked on calibrated RGB | Channels remain separated more than intended | Unlinked neutralizes a real color signal; linked nonlinear mapping shifts saturation | Confirm color calibration, compare histograms, then choose estimator mode |
| No Black Clipping + faint signal | Select it when a low sigma clips faint signal | Background pedestal remains more visible | Minimum still maps to black; it does not preserve every faint sample | Keep the faint signal first, then adjust Target Median |
| Curves Adjustment + Curves Boost | Leave off; if needed start near `0.05` | Upper midtones remain flat | Highlight plateau/steep transition near adjustable anchors | Finish rational stretch, enable curve, raise boost slowly |
| Normalize + target accuracy | Leave off unless maximum scaling is required | Brightest sample remains below `1` | Median and noise rise because the entire image is divided by a sub-unity maximum | Set stretch/curve first, inspect maximum, normalize last |
| Mask + global statistics | Derive settings on the whole source, then judge mask-aware preview | Protected region changes too little | Transition is visible or unmasked statistics poorly represent the applied region | Inspect mask, reduce transform strength, fix mask upstream |

## Inputs, outputs, and metadata

- One target produces one modified image or sequence. Dimensions, channel count,
  pixel format, and pixel type remain unchanged. The transform works in float and
  rounds back to the nearest code for integer sources.
- The target is replaced only after successful still processing. **Take
  snapshot** instead creates an auxiliary full-size still at source depth; no
  background model, clipping mask, statistics file, preset, or file is created.
- The source's header cards, acquisition record, and astrometric solution are
  kept. Processing state is updated to nonlinear with provenance
  `statistical-stretch`; RGB is display RGB and mono remains monochrome. The
  image states no transfer function, and its processing history records the
  stretch. Existing color-calibration provenance remains in the metadata.
- Image History records **Statistical Stretch**, target median, black-point
  sigma, No Black Clipping, Normalize, and whether curves were applied. It omits
  Linked Stretch and the Curves Boost value, so history cannot reproduce every
  execution setting.
- Mask blending is `source × (1-mask) + transformed × mask`, reversed when the
  mask is inverted. Statistics and the clipping-threshold readout are calculated
  from the unmasked source, not only the applied region.
- Each sequence frame is stretched from its own statistics. The shared framework
  creates a progress dialog, supports cooperative cancellation, writes a
  temporary sequence, and replaces the target only after success.

## Performance

The whole target is measured once for each change of the image and kept, so
moving a control or the preview area does not measure it again. The minimum and
median count every sample; the spread reads at most 400,000 values per stream.
The transform, optional curve, normalization, clamp, mask blend, and histogram
traverse image-sized buffers, and a **Preview area** stretches the whole target
before cropping it. Pixel loops and the
maximum reduction run on the engine's threads. Linked RGB computes one statistics stream;
unlinked computes three. Preview holds float/result buffers and refreshes its
histogram after completed generations.

Prototype on a duplicate crop or disable **Preview** while making several rapid
changes on a large image. Increasing histogram resolution changes only the
diagnostic graph's memory/render cost, not transform precision; the histogram
places a whole code on a bin boundary in its own bin, by exact interval
arithmetic. No approved astronomical target was timed.

## Worked example

- **Evidence mode:** Source-, focused-test-, and calculator-derived; not executed
  on project-owned astronomical data.
- **Input condition and settings:** A normalized mono row
  `[0.02, 0.08, 0.15, 0.22, 0.30, 0.42, 0.62, 0.85]`, with **Target Median**
  `0.25`, **Black Point Sigma** `0.5`, and both optional post-stages off.
- **Estimator checkpoint:** `M=0.26`. The lower-half values are
  `[0.02,0.08,0.15,0.22]`, their median is `0.115`, the median absolute
  deviation is `0.065`, and `sigma_lower=0.096369`. The resulting black point is
  `B=0.2118155`; the readout therefore counts the first three samples as
  `<= B`.
- **Transfer checkpoint:** The formula maps the conceptual median anchor to
  `0.25`. It maps input `0.22` to about `0.05098`; inputs `0.02`, `0.08`, and
  `0.15` lie at or below `B` and become black, the three samples the readout
  counts.
- **Inspect in real use:** If the threshold is above the minimum, compare the
  threshold readout with the darkest background at 100%. Prefer **No Black
  Clipping** or a higher sigma when faint signal is clipped.
- **Limitations:** No real-image stretch quality, median accuracy after masking,
  color behavior, or production timing was measured.

## Warnings and limitations

!!! warning "Low-sigma black points clip faint signal"
    Every sample at or below the computed black point becomes black. Use **No
    Black Clipping** or raise sigma when the readout counts more pixels than
    the background should lose.

- **Target Median** is an internal anchor, not a guarantee about the final
  measured median after curves, normalization, masks, clamp, quantization, or an
  even-sample median definition.
- Linked RGB shares a function but does not use common luminance scaling. Any
  nonlinear per-sample transfer can change RGB ratios and saturation.
- Unlinked RGB pushes each channel's statistics toward a common target and can
  remove or create color balance changes. Use only after color calibration.
- **No Black Clipping** still maps the minimum to black and the readout counts
  it as clipped. It means “do not raise black above the minimum,” not “preserve
  every low sample.”
- **Normalize Image** uses one global maximum and changes the median target.
  One hot/saturated sample can determine the scale.
- The robust-noise estimator is deterministic but sampled on large images; it
  is not a full-resolution per-pixel noise model and is not mask-aware.
- A **Preview area** stretches the whole target before cropping it, so it costs a
  whole-image stretch per change on a large image.
- A non-finite sample in a pixel the image's validity admits makes the engine
  refuse the run, and nothing is published; pixels the validity rule rejects
  are left out of the statistics and left as they were. Supply finite
  normalized input.
- History omits Linked Stretch and Curves Boost, so it cannot reproduce the
  full settings.

## Troubleshooting

| Symptom or exact error | Verified cause/validation path | Recovery | Evidence source |
| --- | --- | --- | --- |
| Error: "a statistical stretch needs a mono or RGB source" | The image has other than one or three channels | Extract or combine to a mono or RGB image first | `stretchImageStatistically` input validation, reached from `StatisticalStretch::executeTypedProcessImpl` |
| Error: "no pixel participates in this statistical-stretch analysis" | Every sample of the image is marked as holding no data, so there is nothing to measure | Check the image's validity, for example a registration border covering the frame | `analyzeImageStatisticalStretch` |
| Darker background than the readout suggests | Every sample at or below a raised black point becomes black | Select **No Black Clipping** or raise Black Point Sigma | `stretchImageStatistically`; `computeStatisticalStretchClipStatistics` |
| Final median differs from Target Median | Curves, normalization, mask blending, quantization/clamp, or even-sample statistics change the final distribution | Disable post-stages, compare unmasked float output, and regard the control as the rational anchor | Statistical transfer and post-stage order |
| RGB color balance changes in Linked Stretch | One nonlinear function is applied independently to R/G/B values rather than as a common scale | Compare unlinked carefully or use a color-preserving stretch model; inspect star colors | `analyzeImageStatisticalStretch` (linked); `stretchImageStatistically` |
| RGB background becomes unexpectedly neutral/shifted with Linked off | Each channel receives its own median/black-point transfer toward the same target | Re-enable Linked Stretch or recalibrate/reassess channel intent | `analyzeImageStatisticalStretch` (unlinked); `stretchImageStatistically` |
| Curves Boost has no effect | **Apply Curves Adjustment** is clear, or boost is `0` | Select the checkbox and increase boost cautiously | UI dependency; the highlight curve in `stretchImageStatistically` |
| Normalize changes brightness more than expected | Global post-curve maximum was below `1`, so every sample was divided by it | Clear Normalize or judge it only after final curve settings | normalization in `stretchImageStatistically` |
| Histogram clipping and text clipping disagree | Histogram counts samples at or below `0` and at or above `1` in the source or mask-aware preview, over every channel; text counts source pixels at/below statistical thresholds | Use each diagnostic for its stated purpose and confirm output statistics after Apply | Histogram widget; statistical clip readout |
| History cannot reproduce the result | Step description omits Linked Stretch and Curves Boost | Record those values externally or preserve a duplicate result | `StatisticalStretch::executeTypedProcessImpl` |
| Apply resets the controls | Shared successful-commit path calls Statistical Stretch Reset | This is expected; re-enter settings for another still | `resetSettingsAsync`; `StatisticalStretch::resetSettings` |

## Related workflow steps

Complete [Background Neutralization](../color-calibration/background-neutralization.md)
and [Photometric Color Calibration](../color-calibration/photometric-cc.md) or
[Spectrophotometric Color Calibration](../color-calibration/spectrophotometric-cc.md)
before nonlinear work. Compare Statistical Stretch with
[Histogram Transformation](histogram-transformation.md),
[Generalized Hyperbolic Transformation](gh-transformation.md), and
[ArcSinh Stretch](arcsinh-stretch.md). Continue with
[Curves Transformation](curves-transformation.md) and deliberate nonlinear
finishing while monitoring [Image History](../../interface/image-history.md) and
[Image Statistics](../../interface/image-statistics.md).

## References and provenance

- AfterNight project source — authoritative for the
  custom estimator, rational transfer, curve anchors, normalization, UI,
  masks, state, history, the whole-image statistics rule, and the sequence and
  below-black-point behavior.
