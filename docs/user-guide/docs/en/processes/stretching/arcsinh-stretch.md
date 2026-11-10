---
kind: process
stage: stretch
menu: Process > Intensity Transformations > ArcSinh Stretch
summary: >-
  Lifts faint linear signal with an inverse hyperbolic sine transfer that compresses the highlights and keeps the
  color ratios of an RGB image.
data_in: linear
data_out: nonlinear
---
# ArcSinh Stretch

## Purpose and expected result

ArcSinh Stretch is a display-stretch process for lifting faint linear signal while
compressing the rate of change toward the highlights. On a monochrome image it
applies one normalized inverse-hyperbolic-sine transfer. On RGB it calculates a
luminance-like value and multiplies all three black-adjusted channels by one
factor. That common factor preserves the post-black-point RGB ratios until an
individual channel clips.

The expected result is one nonlinear still image or replacement sequence with
the same dimensions and pixel type. Channel count and pixel format normally
remain unchanged, except that automatic CFA debayering can turn a one-channel
mosaic sequence into RGB before the stretch runs. A larger **Stretch Factor**
raises low and middle values more strongly while keeping the normalized black
and white endpoints fixed. **Black Point** first clips samples at or below the
chosen threshold and expands the remaining range; it is not an automatic
background estimator.

## At a glance

| Item | Current behavior |
| --- | --- |
| Menu path | **Process > Intensity Transformations > ArcSinh Stretch** |
| Enabled when | An image or sequence is active, the same process is not already attached to it, and no attached process holds exclusive access |
| Inputs | One intended normalized grayscale or three-channel RGB still image or sequence; a one-channel CFA still is technically sent through the mono path; an optional active mask must match the target geometry |
| Outputs | Destructively replaces the target still or sequence after success; dimensions and pixel type are preserved, while automatic sequence CFA debayering can change one-channel CFA to RGB; Apply writes no auxiliary image or file |
| Recommended image state | Linear, background-corrected, and color-calibrated data before the main display stretch |
| Preview support | Asynchronous RT preview of the whole image or a selected Preview area, running Apply's own operation, plus a histogram of the preview result; disabling **Preview** returns the histogram to the source target |
| Sequence support | Supported; one captured parameter set is applied to every decoded frame through a temporary replacement sequence. The sequence reader honors the view's automatic CFA debayer setting. |
| Mask support | Supported by post-transform blending; white applies the result, black keeps the source, gray blends, and **Invert Mask** reverses the weights |
| External dependencies | None; execution uses the bundled engine and does not access a tool, model, catalog, or network service |

## Before you start

- Finish background correction and color calibration first. The process does
  not estimate a background threshold, linearize encoded RGB, or calibrate
  channel weights.
- Use a normalized one-channel or three-channel image. Only an image of exactly
  three channels is stretched through a luminance. The launch gate does not
  check the channel count: a two- or four-channel image is stretched channel by
  channel without a luminance (a fourth channel receives the black point but not
  the curve), and an image of more than four channels is refused with a message.
- Do not treat a one-channel CFA still as ordinary monochrome: ArcSinh does not
  debayer still images and would stretch the mosaic samples directly. For a CFA
  sequence, decide whether its automatic debayer option should convert decoded
  frames to RGB; a successful replacement then remains RGB.
- Prefer floating-point data while stretching. Integer samples are worked in a
  floating copy (Float32, or Float64 for 32-bit integers) and rounded back to
  the nearest code of the source pixel type.
- Inspect the background before increasing **Black Point**. Every channel sample
  at or below that value becomes zero before the stretch.
- Duplicate the target or be prepared to use Image History. **Apply** replaces
  the current still or sequence after processing succeeds.

## Procedure

1. Select the target and open **Process > Intensity Transformations > ArcSinh
   Stretch**. Confirm **Target Image** and leave **Preview** selected.
2. Leave **Black Point** at `0` initially. Increase **Stretch Factor** from `0`
   until faint structure is visible without excessive highlight clipping.
3. On RGB, leave **Human-weighted luminance** selected for the
   `0.2126 R + 0.7152 G + 0.0722 B` weighting. Clear it to use the plain mean of
   the three channels. The choice has no effect on monochrome data.
4. If a measured background pedestal should be removed, increase **Black Point**
   cautiously. Revisit **Stretch Factor** because black-point normalization
   changes the values supplied to the stretch.
5. Use the histogram and preview together. Check low/high clipping, inspect
   bright colored stars for single-channel clipping, and compare at 100% zoom.
   On a large image, **Preview area** limits the preview to a representative region.
6. If a mask is active, judge the mask-aware preview rather than assuming the
   mathematical transfer applies at full strength everywhere.
7. Select **Apply**. For a sequence, watch the progress window and use
   **Cancel** if necessary; the source sequence is replaced only after successful
   completion. After either a successful still or sequence commit, the process
   returns Stretch Factor and Black Point to `0` and rechecks Human-weighted luminance.

## Complete parameter and action reference

| UI label/action | Type and unit | Runtime default | Persisted-default exception | Valid values, step, or constraints | Effect of increasing, decreasing, or selecting | Dependencies and enable/visibility rules | Quality, artifact, runtime, memory, or disk trade-off | Scope: preview/apply, channel, mask, still/sequence | Persistence |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Histogram plot | Graph, normalized intensity/count | Source or current preview; RGB channels overlaid | None | Binning comes from **Plot resolution** | Shows sample distribution and approximate endpoint-bin clipping; never changes samples | Uses mask-aware RT result with Preview on and source target with it off | Recalculation adds work; endpoint counts are resolution-dependent diagnostics | Preview/source display only; all shown channels | Recomputed after source, preview, or resolution changes |
| Left-drag in histogram | Graph gesture | No readout | None | Hold left button and move in graph | Shows normalized coordinate readout | Histogram must be shown | Diagnostic only; does not pick Black Point | Display only | Ends on button release |
| Middle-drag in histogram | Graph gesture | Fitted/unpanned | None | Hold middle button and drag | Pans through graph scroll area | Useful only when graph exceeds viewport | No output cost or effect | Display only | Until pan/zoom reset or window closes |
| Ctrl+mouse wheel in histogram | Graph gesture | Fitted size | None | Up `1.20x`; down `0.8x`; wheel without Ctrl scrolls normally | Zooms around cursor | Interactive graph enabled | Larger graph uses more rendering area | Display only | Window lifetime; not changed by process Reset |
| Zoom in | Toolbar action | Fitted size | None | Each selection multiplies size by `1.30` | Enlarges plot | Histogram shown | Plot rendering only | Display only | Window lifetime; not changed by process Reset |
| Zoom out | Toolbar action | Fitted size | None | Each selection multiplies size by `0.70` | Shrinks plot | Histogram shown | Plot rendering only | Display only | Window lifetime; not changed by process Reset |
| Zoom reset | Toolbar action | — | None | One action | Fits graph and resets scrolling | Histogram shown | Plot rendering only | Display only | Immediate |
| Show grid | Toggle | Checked | None | Checked/unchecked | Shows or hides graph grid | Disabled while histogram is hidden | Display-only visual aid | Display only | Window lifetime; clearing Show histogram resets the widget grid internally |
| Show clipping info | Toggle | Checked | None | Checked/unchecked | Shows or hides low/high endpoint-bin counts and percentages | Disabled while histogram is hidden | Higher resolution changes the estimate; not an exact floating endpoint total | Display only; follows shown source/preview channels | Window lifetime; not changed by process Reset |
| Absolute scales | Toggle | Unchecked | None | Checked/unchecked | Checked normalizes each visible channel to its own peak; unchecked preserves relative/global height | Disabled while histogram is hidden | Can conceal relative channel strength; never normalizes image data | Display only | Window lifetime; not changed by process Reset |
| Draw histogram areas | Toggle | Unchecked | None | Checked/unchecked | Selects filled-area or line rendering | Disabled while histogram is hidden | Area mode can obscure overlapping channel lines | Display only | Window lifetime; not changed by process Reset |
| Show histogram | Toggle | Checked | None | Checked/unchecked | Unchecked clears graph, resets graph zoom, and disables graph controls; checked reloads preview or source | Always available while process is idle | Hiding saves histogram rendering but not image-preview processing | Display only | Window lifetime; not changed by process Reset |
| Plot resolution | Combo box, bins | **8-bit (256)** | None | **6-bit (64)**, **7-bit (128)**, **8-bit (256)**, **9-bit (512)**, **10-bit (1024)**, **12-bit (4096)**, **14-bit (16K)**, **16-bit (64K)**, **100 levels**, **1000 levels**, **10000 levels** | More bins show finer distribution and change endpoint estimates | Disabled while histogram is hidden | Higher bin counts increase histogram memory/rendering; output precision is unchanged | Display only | Window lifetime; not changed by process Reset |
| Stretch Factor | Double value plus slider, unitless | `0` | None | `0..1000`; spin step `1`; slider positions `0..1000` | Larger values raise shadows/midtones more strongly; `0` makes only the arcsinh part identity | Enabled while idle; together with Black Point controls Apply enablement | Strong values can amplify background/noise or clip saturated RGB channels | Preview/apply; mono or common RGB scale; mask post-blend; still/sequence | Captured per execution; Reset, successful Apply, or a new window returns `0` |
| Black Point | Double value plus slider, normalized intensity | `0` | None | `0..0.2`; five decimals; spin step `0.001`; slider maps `0..1000` to effective `0.0002` steps | Larger values clip more samples to zero and expand the surviving range | Enabled while idle; nonzero value enables Apply even at zero Stretch Factor | Can erase faint signal and change channel ratios before RGB common scaling | Preview/apply; each channel; mask post-blend; still/sequence | Captured per execution; Reset, successful Apply, or a new window returns `0` |
| Human-weighted luminance | Checkbox | Checked | None | Checked uses `0.2126`, `0.7152`, `0.0722`; unchecked uses the plain mean (`1/3` each) | Changes the luminance driving the common RGB scale | Enabled while idle; no effect on mono | Weights are applied to the stored samples and assume linear light | Preview/apply; RGB only; mask post-blend; still/sequence | Captured per execution; Reset, successful Apply, or a new window checks it |
| Target Image | Image selector | Attached/active target | None | Open eligible image or sequence | Changes source for histogram, preview, and Apply | Active image required; duplicate/exclusive process gates apply; channel count is not validated here | Wrong target is destructively replaced; a two- or four-channel target is stretched channel by channel and more than four channels are refused | Preview/apply; still/sequence and its active mask | Follows attachment/selection lifecycle; Reset does not change it |
| Preview | Checkbox | Checked | None | Checked/unchecked | Starts or removes asynchronous preview and switches histogram between preview/source | Not locked while Apply runs | Each preview pass and its histogram consume time/memory over the whole image or the Preview area | Preview only; mask-aware; current still/frame display | Window lifetime; Reset does not change it |
| Preview pane | Viewer | Current transform | None | Standard viewer zoom/pan; its toolbar offers **Switch image**, **Preview area**, and **Take snapshot** | Shows transform, mask blend, and waiting indicator | Exists only while Preview is checked | Holds a preview result of the whole image or of the Preview area composited over the original | Preview only; current target/frame | Rebuilt with Preview lifecycle; independent of histogram zoom |
| Preview area | Preview checkbox | Unchecked | None | Checked/unchecked; move/resize overlay on the preview | Processes the selected area as Apply would process an image of that area and composites it over the original | Preview exists | Reduces preview work; does not limit Apply | Preview/snapshot; mask cropped consistently | Window lifetime |
| Apply | Dialog action | Disabled at numeric identity | None | Enabled unless Stretch Factor and Black Point are both `0` | Captures parameters, locks controls, and replaces target after success | Requires process lock; disabled during execution | Destructive still/sequence operation; sequence writes temporary output | Apply; mono/RGB, mask, still/sequence | Adds `ArcSinhStretch` history with three values, then resets both numeric controls and rechecks Human-weighted luminance |
| Reset | Dialog action | — | None | One action | Restores Stretch Factor `0`, Black Point `0`, and checked Human-weighted luminance | Available while idle | May schedule one preview pass | Parameter UI and preview; does not alter committed target | Does not reset target, Preview, toolbar, resolution, or graph zoom |
| Close | Dialog action | — | None | One action | Closes without committing current preview | Disabled while Apply controls are locked; closing the window another way during a run hides it until the run has landed, and closing the image view stops the run | No output artifact; a stopped run publishes nothing | Window only | Current uncommitted parameters are discarded |
| Sequence progress | Progress display | Hidden until sequence Apply | None | Preparation plus per-frame progress | Reports temporary sequence creation and current frame | Sequence target and active execution required | Temporary output uses disk space until success/cancel cleanup | Apply; every frame with captured params/mask | Ends when run succeeds, fails, or cancels |
| Cancel | Progress action | Available during sequence run | None | Cancellation request | Stops the run; the engine operation observes the request | Sequence progress must be active | Unsuccessful temporary output is removed | Apply; sequence only | Original remains when cancellation prevents successful replacement |

## How the algorithm works

### Conceptual pipeline

1. Read the source image or frame. A sequence reader first performs its
   configured automatic CFA debayer, while a CFA still remains one-channel and
   enters the mono path.
2. Work in normalized units: an unsigned integer divided by its largest value, a
   signed integer counted from its smallest value to its largest, a floating
   sample as it is. Integer samples are converted into a floating working copy.
3. Clip and rescale each sample with **Black Point**.
4. Hold every sample to `[0,1]`. For mono, evaluate the normalized arcsinh curve
   directly. For RGB, calculate a weighted or equal-channel luminance from the
   black-adjusted samples and scale all three channels by one factor, clipping
   each to `[0,1]`.
5. Narrow integer results back to the original pixel type at the nearest code.
6. Record the successful result nonlinear with `arcsinh-stretch` provenance,
   blend with the source if a mask is active, and publish it as the replacement
   still or sequence.
7. After commit, reset the two numeric controls and Human-weighted luminance to
   their defaults; the executed values remain only in Image History.

### Black-point normalization

For normalized input `x` and black point `B`, every mono or RGB channel first
becomes

\[
x' = \min\left(1, \max\left(0, \frac{x-B}{1-B}\right)\right).
\]

The UI constrains `B` to `[0,0.2]`, so the denominator remains positive. Values
at or below `B` collapse to zero; `x=1` remains `1`. At `B=0` this step is
skipped, and every sample is still held to `[0,1]` before the curve, so a
floating value above `1` is treated as `1`. Black-point adjustment can change
RGB ratios before the color-preserving common scale is calculated.

### Monochrome transfer

For stretch factor `S`, the output is

\[
f(x') =
\begin{cases}
x', & S=0,\\
\dfrac{\operatorname{asinh}(Sx')}{\operatorname{asinh}(S)}, & S>0.
\end{cases}
\]

For `S>0`, `f(0)=0` and `f(1)=1`. The curve is increasing and concave: it lifts
low and middle values more than values near white. The engine evaluates a zero
stretch factor as the identity, which leaves only the black-point remap active.
The curve is the image engine's asinh stretch with no protection points, which
is exactly this ratio.

### RGB common scaling

After applying the black point separately to `r`, `g`, and `b`, the process
calculates

\[
L = w_R r' + w_G g' + w_B b'.
\]

Checked **Human-weighted luminance** uses weights `(0.2126, 0.7152, 0.0722)`;
unchecked uses the plain mean, `(1/3, 1/3, 1/3)`. It then applies

\[
k =
\begin{cases}
1, & S=0,\\
\dfrac{\operatorname{asinh}(SL)}{L\operatorname{asinh}(S)}, & S>0,
\end{cases}
\qquad
(r_{out},g_{out},b_{out}) = \operatorname{clamp}_{[0,1]}(k(r',g',b')).
\]

A pixel whose luminance is at most `1e-12` is written black (strictly, as a
neutral gray at `f(L)` when every channel is that dark, which is black for any
practical purpose).

One `k` preserves the ratios among `r'`, `g'`, and `b'`. It does not guarantee
the original pre-black-point ratios, and an individual channel that reaches
`1` loses ratio preservation through clipping. The standard coefficients
describe relative luminance for linear-light RGB, but this implementation does
not decode a nonlinear transfer function before applying them. Use this process
on calibrated linear RGB for the intended interpretation.

### Boundary, mask, and state behavior

- Every sample is held to `[0,1]` before the curve, and final samples below or
  above the normalized range are clipped to `0` or `1`.
- Under the default all-valid rule a pixel holding NaN or an infinity is left as
  it was, and so is a pixel that the image's own validity marks as having no
  data. Under any other validity rule, a NaN or infinity in a pixel that rule
  calls valid makes the engine refuse the run: Apply publishes nothing and
  reports the error.
- The active mask is applied after the full transform. The histogram follows
  that blended preview when **Preview** is enabled.
- Floating samples are worked in their own precision; 8- and 16-bit integer
  samples in a Float32 copy and 32-bit integers in a Float64 copy. The curve and
  luminance are computed in double precision. The output retains the source
  type, and integer results are rounded to the nearest code, ties to even.
- Successful output is recorded nonlinear on the processing-state cards:
  `ANBASIS` is `monochrome` for a grayscale result and `display-rgb` for an RGB
  result, and the other recorded fields, including the color calibration
  record, are kept from the source.
- A black-point-only run (`S=0`, `B>0`) is marked nonlinear too, even though its
  surviving-sample remap is affine.
- A one-channel CFA still is processed by the mono branch without demosaicing
  and is marked nonlinear/monochrome; this is not a valid raw-CFA workflow. A
  CFA sequence with automatic debayer enabled is decoded and replaced as RGB.

## Parameter interactions and starting points

| Goal | Starting point | What to watch |
| --- | --- | --- |
| First controlled display stretch | `B=0`, human weights on, then increase `S` from `1` | Background lift, bright-star cores, and channel clipping |
| Preserve measured background removal | Set `B` only after measuring the pedestal; readjust `S` afterward | Crushed faint signal and channel-dependent color shifts near black |
| Compare luminance models | Hold `S` and `B` fixed and toggle the checkbox | Green-dominant structures receive more influence with human weights; equal weights are not perceptual luminance |
| Black-point remap without arcsinh lift | `S=0`, choose `B>0` | This clips/rescales the background and is recorded nonlinear despite the zero stretch factor |
| Masked regional stretch | Tune the unmasked transform, then inspect the active-mask blend | The common RGB scale is computed before mask blending; gray mask values reduce the total effect |

## Inputs, outputs, and metadata

- **Still input:** the active normalized mono or RGB image. The run starts from
  a copy of the target with the same geometry, channels, pixel type, and
  metadata, which replaces the target after success.
- **Sequence input:** the active sequence. The shared runner processes each
  frame as Apply processes a still image of it, writes the frames to a temporary
  sequence, and replaces the source sequence only on success; cancellation
  removes the temporary output. Automatic sequence CFA debayer can change the
  replacement from one-channel mosaic data to RGB.
- **Mask:** the active target mask is a post-transform blend. The mask and its
  inversion are captured when **Apply** is selected.
- **History:** the step is named `ArcSinhStretch`; its description stores the
  three execution values, for example `Stretch 50, Black Point 0.001, Human
  Weight true`. Histogram display state and mask identity are not in the step
  description. Undo restores the previous image.
- **Linearity:** successful output is recorded nonlinear on the processing-state
  cards a saved file carries (`ANLIN` nonlinear, `ANASRC` process, `ANOP`
  `arcsinh-stretch`, and the stretched `ANBASIS`), a linear encoding the source
  claimed is no longer claimed, and the image lineage gains an entry naming
  `an::stretchImage` with the three values. The FITS cards, astrometric
  solution, and acquisition record are otherwise kept from the source. Do not
  treat the output as linear input for later calibration. Preview and snapshot
  results are not recorded.
- **Auxiliary output:** Apply writes none. The process does not write a report,
  curve file, or diagnostic image; the preview's **Take snapshot** action creates
  a separate still from the preview only when you select it.

## Performance

The black point and the stretch each run once over the image on the operation's
threads. RGB requires the black-point adjustment, one luminance and scale
calculation, and three writes per pixel. A larger stretch factor changes math
values, not algorithmic complexity. An integer image is converted into a
floating working copy and back, and Apply normally makes one further full-image
copy to write the nonlinear record onto the result.

RT preview processes the full image, or only the **Preview area** when one is
selected, and maintains a destination image; changing a parameter can therefore
consume a preview pass plus histogram regeneration. Higher **Plot resolution**
increases histogram bins and rendering work but never output precision. For a
large image or sequence, disable **Preview** or use a **Preview area** while
making coarse numeric edits, then check the whole image before Apply. No
production timing is claimed without an approved representative master.

## Worked example

This exact normalized mono example comes from the implemented equation, not a
real-image quality run. With `S=10` and `B=0`:

| Input `x` | Output `asinh(10x) / asinh(10)` |
| ---: | ---: |
| `0.01` | `0.033298` |
| `0.10` | `0.293965` |
| `0.50` | `0.771270` |
| `0.90` | `0.965053` |
| `1.00` | `1.000000` |

The smallest sample is multiplied by about `3.33`, while `0.90` moves only to
about `0.965`; that is the intended shadow lift/highlight compression pattern.

Now set `B=0.05` while retaining `S=10`. An input of `0.10` first becomes

\[
x' = \frac{0.10-0.05}{1-0.05} = 0.0526316,
\]

then becomes `0.168308` after the arcsinh curve. Inputs at or below `0.05` become
zero. With `S=0`, the same input would remain at the black-adjusted `0.0526316`;
the black-point control is not disabled by a zero stretch factor.

## Warnings and limitations

- The launch gate does not check the channel count. A two- or four-channel image
  is stretched channel by channel rather than through a luminance, and an image
  of more than four channels is refused; use this process on grayscale or
  three-channel RGB data.
- **Black Point** is manual and channel-wise. It can erase faint signal and alter
  color ratios before the common RGB scale is applied.
- RGB ratio preservation is conditional: it applies after black subtraction and
  before individual output clipping. It is not a gamut-mapping method.
- **Human-weighted luminance** does not linearize encoded RGB. Its standard
  coefficients are meaningful as relative luminance only when samples already
  represent linear light.
- Equal-channel mode is the exact mean of the three channels.
- Every sample is held to `[0,1]` before the curve; unbounded floating data
  above `1` is treated as `1`.
- Integer output is rounded to the nearest code of the source type.
- Histogram clipping values count endpoint bins at the selected plot resolution;
  they are not exact floating-point endpoint counts and do not change output.
- The process cannot save or reload parameter presets. A newly opened window
  starts from the built-in defaults.
- Successful Apply resets Stretch Factor, Black Point, and Human-weighted
  luminance to their defaults. Image History retains the executed values, but
  the controls do not.
- A still Apply shows no progress window and has no Cancel button; closing the
  process window hides it until the Apply lands, and closing the image view
  stops it. A stopped or refused Apply publishes nothing.
- A one-channel CFA still is stretched as mono rather than debayered. A CFA
  sequence becomes RGB when its automatic debayer option is enabled.

## Troubleshooting

| Symptom | Cause | Fix |
| --- | --- | --- |
| **Apply** is disabled | Both **Stretch Factor** and **Black Point** are `0`, so the transform is the identity. | Increase either numeric control. |
| Faint background structure disappears | **Black Point** is above that signal. | Reduce it toward `0`; use a mask if only part of the image needs suppression. |
| Bright colored stars change hue | One or more scaled RGB channels clipped at `1`, or channel-wise black subtraction changed the ratios. | Reduce **Stretch Factor** or **Black Point** and inspect channels/clipping at 100%. |
| Human and equal weighting look unexpectedly similar | The target is mono, neutral RGB, or the chosen values produce little difference. | Confirm a three-channel target and compare on chromatic regions with identical numeric settings. |
| Histogram shape changes but the image does not | A plot-only action such as **Absolute scales**, area mode, or resolution changed. | Adjust transform controls; histogram toolbar actions never alter image samples. |
| Preview differs from Apply | Target, active mask/inversion, or source sequence frame changed, or a **Preview area** shows only the result for that area. | Reconfirm the target and mask, wait for the last preview, and compare with **Preview area** off. |
| A few pixels are unchanged after Apply | They hold NaN or an infinity, or the image's validity marks them as having no data. | This is expected; repair or replace those samples upstream if they must be stretched. |
| Sequence remains unchanged after Cancel | Replacement is transactional through a temporary sequence. | This is expected; rerun and allow every frame to finish. |
| Later calibration treats the image as nonlinear | ArcSinh output is deliberately recorded nonlinear. | Perform calibration before ArcSinh Stretch or return to the pre-stretch history state. |
| Controls return to defaults after Apply | Successful still and sequence commits call the process reset path after recording history. | This is expected; read the executed values from Image History and re-enter them for another run. |
| A CFA sequence becomes RGB after Apply | Automatic sequence CFA debayer was enabled, so frames were decoded to RGB before ArcSinh ran. | Disable automatic sequence debayer when preserving a one-channel CFA representation is required; do not stretch CFA mosaics as mono. |

## Related workflow steps

- Use [Histogram Transformation](histogram-transformation.md) for a rational
  midtones transfer with manual or statistics-derived linked/unlinked controls.
- Use [Generalized Hyperbolic Transformation](gh-transformation.md) for more
  transfer families, symmetry/protection points, channel/component modes, and
  analytical inverse behavior.
- Use [Curves Transformation](curves-transformation.md) for point-controlled
  nonlinear finishing and selected color-space components.
- Check [Histogram Analysis](../../interface/image-histogram.md) and
  [Image Statistics](../../interface/image-statistics.md) before accepting black
  or white clipping.
- Perform [Spectrophotometric Color Calibration](../color-calibration/spectrophotometric-cc.md)
  before the main nonlinear stretch.

## Credits

- [W3C WCAG 2.2: relative
  luminance](https://www.w3.org/TR/WCAG22/#dfn-relative-luminance) records the
  standard `0.2126`, `0.7152`, and `0.0722` linear-light RGB coefficients used
  by the checked luminance mode.
