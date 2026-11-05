# Generalized Hyperbolic Transformation

<!-- MEDIA_SLOT: UG-MEDIA-056 -->

## Purpose and expected result

Generalized Hyperbolic Transformation (GHT) is a permanent display stretch for
normalized mono or RGB images. It combines generalized-hyperbolic, histogram,
arcsinh, linear, inversion, and statistics-derived STF families in one window.
Its symmetry point and shadow/highlight protection boundaries let an experienced
processor concentrate nonlinear contrast around a chosen intensity while
keeping the transfer continuous at both protection boundaries.

The usual input is a calibrated linear master after operations that require
linear samples. Apply replaces the selected still image or sequence, preserves
geometry and pixel type, optionally post-blends the active view mask, records an
Image History step, and marks the result nonlinear. Two histograms and a
full-image real-time preview provide diagnostics; no LUT, preset, report, or
separate output file is created.

## At a glance

| Item | Current behavior |
| --- | --- |
| Menu path | **Process > Intensity Transformations > Generalized Hyperbolic Transformation** |
| Enabled when | An image or sequence view is active. A second GHT window for the same target is rejected, as is launch while an exclusive process owns it. GHT itself is nonexclusive; Apply still requires the selected target's processing lock. |
| Inputs | One intended one-channel grayscale or three-channel RGB still/sequence; normalized transform parameters; optional enabled view mask. RGB also exposes component and color-preserving modes. |
| Outputs | Destructive target replacement with preserved dimensions, pixel type, and source metadata; nonlinear processing-state provenance and one history step. Channel count and pixel format are also preserved except when sequence CFA autodebayering converts one-channel mosaic frames to three-channel RGB before transformation. A sequence is rebuilt at a temporary path before replacement. |
| Recommended image state | Normally a calibrated linear mono/RGB master after background/color calibration and any linear-only measurement, restoration, or denoising. **Image Inversion** and an analytical inverse are also available, but AfterNight still marks every successful GHT-process result nonlinear. |
| Preview support | Full asynchronous real-time preview, which is the result Apply writes, plus a lower source histogram and upper predicted/processed histogram. A **Preview area** shows exactly what Apply writes there; an STF preview area uses the whole target's statistics, never the area's own. Histogram-only prediction is quantized and does not include the active mask. |
| Sequence support | Supported by the generic replacement-sequence path with progress and cooperative cancellation. One UI parameter snapshot is captured for the run. STF statistics and its derived curve are recomputed from each frame, so STF is intentionally per-frame adaptive. The sequence reader may apply its configured CFA debayer request. |
| Mask support | Supported as a final original/processed blend for stills and sequences, including mask inversion and the framework's grayscale/RGB mask-channel rules. The mask does not alter the transfer curve or immediate histogram prediction. |
| External dependencies | None. The transform is the bundled engine's stretch, run on its threads. The cited GHS material is algorithm background, not a runtime dependency. |

## Before you start

1. Finish calibration, integration, gradient correction, color calibration, and
   any measurement, deconvolution, or denoising step that requires linear data.
   Every successful mode in this process declares the result nonlinear.
2. Activate the intended mono or RGB target. A one-channel CFA still is treated
   as grayscale and is not a substitute for debayering. A CFA sequence is
   different: if that view's automatic sequence debayer option is enabled, its
   frames are converted to RGB before the transform runs.
3. Save a copy or confirm that Image History is available. Apply replaces the
   target and repeated stretches compound rounding and endpoint clipping.
4. Inspect the active view mask. It is blended after the transform and is not
   represented in the transfer curve or histogram-only prediction.
5. Start with **Preview** selected and **Plot resolution** at **8-bit (256)**.
   Plot resolution changes diagnostic binning only, never output precision.
6. For GHT, Histogram, or Arcsinh, maintain `LP <= SP <= HP`. The spin controls
   and range handles enforce that order. For Linear Stretch, maintain
   `Black point < White point`; the UI enforces a minimum gap of `0.0001`.

## Procedure

1. Open **Process > Intensity Transformations > Generalized Hyperbolic
   Transformation**. Confirm that **Target Image** names the intended view and
   that the lower histogram represents its source data.
2. Choose **Channel**. Begin with **RGB** for a shared independent-channel
   transfer. Use one named channel for deliberate channel repair, or use
   **Lightness**, **Saturation**, or **Colour** only after reviewing their color
   models below.
3. Choose **Transformation type**. For a conventional first GHT stretch, leave
   **Generalized Hyperbolic**, **Invert** cleared, and **Local intensity b** at
   `0`. Set **Stretch factor(In(D+1))** gradually; the displayed value is
   `ln(D+1)`, not the internal strength `D`.
4. Place **Symmetry point SP** at the intensity around which nonlinear contrast
   should be concentrated. Type a value or use one of the mutually exclusive
   point/area/histogram pickers. Pickers remain armed after a selection, so each
   subsequent point, rectangle, or histogram click updates SP until you toggle
   the picker off, select another picker, edit SP manually, or change
   **Transformation type**.
5. Set **Protect shadows LP** no higher than SP and **Protect highlights HP** no
   lower than SP. Outside LP/HP the curve continues along boundary tangents; the
   controls do not hard-clip samples at those positions.
6. Inspect the upper transfer curve and preview while changing one control at a
   time. Use the lower three-handle selector for LP/SP/HP. In Linear Stretch its
   outside handles control black/white points; the center handle is displayed
   but currently has no process effect.
7. Check the target at 100% and inspect endpoints independently of the toolbar's
   **Show clipping info** overlay. That overlay is a coarse endpoint-occupancy
   diagnostic and has known component/prediction limits.
8. Select **Apply**. A successful still-image run replaces the target and resets
   transform settings. For a sequence, wait for the progress dialog to finish;
   cancellation preserves the original sequence and removes the temporary
   replacement.
9. Inspect Image History and the declared nonlinear state before continuing to
   [Curves Transformation](curves-transformation.md) or other nonlinear tone and
   color work.

## Complete parameter and action reference

| UI label/action | Type and unit | Runtime default | Persisted-default exception | Valid values, step, or constraints | Effect of increasing, decreasing, or selecting | Dependencies and enable/visibility rules | Quality, artifact, runtime, memory, or disk trade-off | Scope: preview/apply, channel, mask, still/sequence | Persistence |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Upper preview histogram | Interactive plot | Current transformed prediction/preview | None | Normalized horizontal intensity; vertical count scale | Shows the selected channel/component after the current transform; the curve is always drawn | Available with a valid target; recalculated after target/settings changes | Image-derived recomputation is slower than remapping existing bins | Diagnostic only; unmasked; still target shown while configuring a sequence | Window lifetime |
| Lower source histogram | Interactive plot | Current target source | None | Normalized horizontal intensity; vertical count scale | Shows untransformed source data and hosts the range selector | Available with a valid target | Higher plot resolution costs more histogram work and memory | Diagnostic only; unmasked; target still/source view | Window lifetime |
| Range selector handles | Three normalized handles | `LP=0`, `SP=0`, `HP=1` | None | Enforces left <= center <= right | In nonlinear modes sets LP/SP/HP; in Linear Stretch the outside handles set black/white points | Center handle has no process effect in Linear Stretch; hidden behavior is reflected by fields | Direct manipulation can move two constrained values when they meet | Preview/apply parameters; all applicable channels; still/sequence | Transform settings reset after success/Reset |
| Histogram mouse gestures | Mouse/wheel actions | No active gesture | None | Left click/drag, middle-drag pan, right-click reset, Ctrl+wheel zoom | Left interaction moves a handle or supplies histogram SP when that picker is active | Histogram-point picker uses the upper histogram; ordinary handles are on the lower histogram | Display navigation only, except deliberate handle/picker edits | Diagnostic and parameter input; mask-independent | Window lifetime |
| Zoom in | Toolbar action | — | None | Repeated action, `1.30x` per click | Enlarges both histogram widgets inside their scroll areas | Always available with histograms | Diagnostic only | Both plots only | Not persisted |
| Zoom out | Toolbar action | — | None | Repeated action, `0.70x` per click | Shrinks both histogram widgets inside their scroll areas | Always available with histograms | Diagnostic only | Both plots only | Not persisted |
| Zoom reset | Toolbar action | — | None | Single action | Fits both histogram widgets to their scroll areas | Always available with histograms | Diagnostic only | Both plots only | Not persisted |
| Show grid | Checkable toolbar action | Selected | None | On/off | Shows or hides grid lines on both histograms | Always available | Display only | Both plots only | Window lifetime; Reset does not change it |
| Show transfer curve | Checkable toolbar action | Selected | None | On/off | Shows or hides the curve on the lower source histogram | The upper histogram's transfer curve remains enabled | Display only; does not disable processing | Lower plot only | Window lifetime; Reset does not change it |
| Show clipping info | Checkable toolbar action | Selected | None | On/off | Shows or hides clipped-low/high overlays on both histograms | Values are each plot's endpoint counters; a bin-remapped prediction counts its end bins | Coarse and potentially misleading in predicted/component cases; see limitations | Both plots; unmasked diagnostic | Window lifetime; Reset does not change it |
| Absolute scales | Checkable toolbar action | Selected | UI-builder seed is cleared, but process construction selects it before use | On/off | Selected actually normalizes each displayed channel to its own peak; cleared preserves relative peak heights | Disabled for grayscale | Selected improves per-channel visibility but hides relative channel populations | Both plots only | Window lifetime; Reset does not change it |
| Draw histogram areas | Checkable toolbar action | Cleared | None | On/off | Fills histogram areas instead of drawing outlines | Always available | Rendering preference only | Both plots only | Window lifetime; Reset does not change it |
| Plot resolution | Combo box, bins | **8-bit (256)** | None | **6-bit (64)**, **7-bit (128)**, **8-bit (256)**, **9-bit (512)**, **10-bit (1024)**, **12-bit (4096)**, **14-bit (16K)**, **16-bit (64K)**, **100 levels**, **1000 levels**, **10000 levels** | More bins reveal finer diagnostic structure | Recalculates both histograms | More bins cost histogram time/memory; output samples are unchanged | Diagnostics only | Window lifetime; Reset does not change it |
| Channel | Combo box | **RGB** for RGB; disabled **Grayscale** for mono | None | **RGB**, **Red**, **Green**, **Blue**, **Lightness**, **Saturation**, **Colour**; mono **Grayscale** | Chooses independent sample channels or a component/color-preserving model | RGB-only choices on three-channel targets; controls below change with mode | Single-channel edits can unbalance color; component conversions can introduce gamut/rounding effects | Preview/apply, still/sequence; mask post-blend is unchanged | Transform settings reset after success/Reset |
| Colour clip | Combo box | **Clip** | None | **Clip**, **Rescale** | Clip clamps scaled RGB independently; Rescale reduces a common scale when a channel would exceed 1 | Visible only for **Channel: Colour** | Clip can alter ratios at the upper boundary; Rescale preserves ratios but yields less luminance increase | Colour mode, preview/apply, still/sequence | Transform settings reset after success/Reset |
| Luminance coefficients source | Combo box | **Default** | None | **Default**, **Image**, **Manual** | Selects pseudo-luminance weights for Colour mode | Visible only for Colour; manual fields appear only for Manual | Current Default and Image both use equal weights; Image does not read a profile | Colour mode only | Transform settings reset after success/Reset |
| R | Numeric coefficient | `1.0000` | None | `0`–`10`, step `0.1`, four decimals | Raises/lowers red's share after all three values are normalized | Visible only for Colour + Manual | Biased weights change which colors drive the common scale; all-zero falls back to equal thirds | Colour pseudo-luminance only | Transform settings reset after success/Reset |
| G | Numeric coefficient | `1.0000` | None | `0`–`10`, step `0.1`, four decimals | Raises/lowers green's normalized share | Visible only for Colour + Manual | Same as R | Colour pseudo-luminance only | Transform settings reset after success/Reset |
| B | Numeric coefficient | `1.0000` | None | `0`–`10`, step `0.1`, four decimals | Raises/lowers blue's normalized share | Visible only for Colour + Manual | Same as R | Colour pseudo-luminance only | Transform settings reset after success/Reset |
| Transformation type | Combo box | **Generalized Hyperbolic** | None | **Generalized Hyperbolic**, **Histogram Transformation**, **Arcsinh**, **Linear Stretch**, **Image Inversion**, **STF Transformation** | Selects the transfer family and its relevant controls | Visibility/enabling rules are described in the dependent rows | Families differ in contrast concentration, color behavior, and adaptation | Preview/apply, still/sequence | Transform settings reset after success/Reset |
| Invert | Check box | Cleared | None | On/off | Uses the analytical inverse of Generalized Hyperbolic, Histogram, or Arcsinh | Enabled only for those three families; switching to Linear, Image Inversion, or STF clears and disables it | Useful for reversing a matching curve, but conversion/clipping/quantization prevent perfect image restoration | Preview/apply, selected channel model, still/sequence | Transform settings reset after success/Reset |
| Linked STF | Check box | Cleared | None | On/off | Selected derives one RGB MTF; cleared derives one per RGB channel | Visible only for STF; mono is effectively linked. In Lightness/Saturation/Colour, current unlinked execution uses STF slot 0 rather than component statistics | Linked better preserves channel balance; unlinked neutralizes RGB backgrounds and can alter color | STF preview/apply; each sequence frame derives its own values | Transform settings reset after success/Reset |
| Stretch factor(In(D+1)) | Numeric natural-log strength | `0.0000` | None | `0`–`20`, step `0.01`, four decimals; internal `D=exp(value)-1` | More strength increases nonlinear separation; `0` is identity for GHT/Histogram/Arcsinh | Visible for GHT, Histogram, and Arcsinh only | High values can compress highlights and amplify noise/quantization | Preview/apply, applicable channel model, still/sequence | Transform settings reset after success/Reset |
| Local intensity b | Numeric shape parameter | `0.0000` | None | `-5`–`15`, step `0.01`, four decimals | Moves among logarithmic/integral/exponential/hyperbolic GHT shapes; positive values concentrate more locally around SP | Visible only for Generalized Hyperbolic | Strong positive localization can make transitions visually abrupt even though the curve remains continuous | GHT only | Transform settings reset after success/Reset |
| Symmetry point SP | Normalized numeric intensity | `0.0000` | None | `0`–`1`, step `0.01`, four decimals; LP <= SP <= HP | Moves the maximum-slope center to darker or brighter source intensities | GHT, Histogram, Arcsinh; synchronized with center handle/pickers | Poor placement can emphasize noise or compress the intended subject | Preview/apply, applicable channel model, still/sequence | Transform settings reset after success/Reset |
| Protect shadows LP | Normalized numeric intensity | `0.0000` | None | `0`–`1`, step `0.01`, four decimals; LP <= SP | Raises/lowers the point below which a tangent linear continuation replaces the nonlinear base curve | GHT, Histogram, Arcsinh | Larger LP protects a broader shadow range but reduces nonlinear shadow lift | Preview/apply, applicable channel model, still/sequence | Transform settings reset after success/Reset |
| Protect highlights HP | Normalized numeric intensity | `1.0000` | None | `0`–`1`, step `0.01`, four decimals; HP >= SP | Lowers/raises the point above which a tangent linear continuation replaces the nonlinear base curve | GHT, Histogram, Arcsinh | Lower HP protects a broader highlight range but reduces nonlinear highlight separation | Preview/apply, applicable channel model, still/sequence | Transform settings reset after success/Reset |
| Black point | Normalized numeric intensity | `0.0000` | None | `0`–`1`, step `0.01`, four decimals; at least `0.0001` below White point | Values at/below it become 0; raising it clips more shadows | Visible only for Linear Stretch; synchronized with left handle | Direct shadow clipping | Linear preview/apply, applicable channels, still/sequence | Transform settings reset after success/Reset |
| White point | Normalized numeric intensity | `1.0000` | None | `0`–`1`, step `0.01`, four decimals; at least `0.0001` above Black point | Values at/above it become 1; lowering it clips more highlights | Visible only for Linear Stretch; synchronized with right handle | Direct highlight clipping | Linear preview/apply, applicable channels, still/sequence | Transform settings reset after success/Reset |
| Pick from image point | Checkable icon action | Cleared | None | One mutually exclusive picker | Sets SP to the arithmetic mean of all source-channel samples at each selected image coordinate | GHT/Histogram/Arcsinh only; selecting it clears other pickers; manual SP edit cancels it | One hot/cold pixel is not representative; repeated clicks continue changing SP while armed | SP input from source target; preview view routes coordinates | Remains armed after completion; cleared by toggling it off, selecting another picker, manual SP edit/handle movement, changing Transformation type, Reset, or close |
| Pick from image area | Checkable icon action | Cleared | None | One mutually exclusive rectangular source-selection picker | Sets SP to the arithmetic mean of the per-channel means in each completed source rectangle | GHT/Histogram/Arcsinh only; selecting it clears other pickers; manual SP edit cancels it | A representative area is more stable but can mix background and structure; repeated selections continue changing SP while armed | SP input from source target; mask is not sampled | Remains armed after completion; cleared by toggling it off, selecting another picker, manual SP edit/handle movement, changing Transformation type, Reset, or close |
| Pick from histogram point | Checkable icon action | Cleared | None | One mutually exclusive picker accepting repeated clicks on the upper histogram | Sets SP to each clicked normalized x coordinate | GHT/Histogram/Arcsinh only; selecting it clears other pickers; manual SP edit cancels it | Quantized visual placement depends on plot/view scale; repeated clicks continue changing SP while armed | SP input; upper preview histogram | Remains armed after completion; cleared by toggling it off, selecting another picker, manual SP edit/handle movement, changing Transformation type, Reset, or close |
| Target Image | View selector | Active view at launch | None | Open supported still or sequence views | Retargets controls, source statistics, histograms, and preview | Apply later requires target processing lock | Recalculation cost follows image size and plot resolution | Source for preview/apply and picker sampling | Window lifetime; Reset does not change it |
| Preview | Check box | Selected | None | On/off | Enables/disables asynchronous full-image real-time preview updates | Requires a valid target; a new generation supersedes stale work | Full-image transforms and color conversions can be expensive | Preview only; target is unchanged | Window lifetime; Reset does not change it |
| Preview image pane | Image display | Current transformed preview | None | Zoom/pan through shared preview viewer behavior | Displays the current full-image result | Updated only while Preview is enabled and work completes | Allocates a transformed image; special color modes add conversion buffers | Mask-aware image preview; still target view while configuring sequence | Window lifetime |
| Apply | Button/action | Enabled when settings update and target can be locked | None | Single action | Captures typed settings and starts destructive still/sequence execution | Fails if the target is unavailable or already processing | Full-image/sequence CPU, memory, and temporary-disk cost | Target replacement; active mask post-blend; still/sequence | Successful run resets transform settings |
| Close | Button/action | Enabled | None | Single action | Closes immediately when idle; requests cancellation and defers close while work is active | Worker/preview lifecycle aware | Cancellation is cooperative | Window and active execution | Not persisted |
| Reset | Button/action | Enabled | None | Single action | Restores transform/channel settings, cancels pickers, resets histogram zoom, and refreshes the source preview | Does not reset target, Preview, Plot resolution, Grid, transfer/clipping/scale/area toolbar states | No image change | Configuration only | Restores execution defaults; listed display states survive |
| Sequence progress | Progress dialog | Appears only during sequence Apply | None | `0`–frame count | Reports replacement-sequence progress | Sequence targets only | Dialog updates add negligible cost; output uses temporary disk space | Sequence Apply only | Run lifetime |
| Cancel | Sequence progress action | Available while sequence work runs | None | Cooperative request | Stops before replacement, removes the temporary sequence, and preserves the source | Sequence progress dialog only | The current frame may finish before cancellation is observed | Sequence Apply only | Run lifetime |

## How the algorithm works

### Conceptual pipeline

1. A still target is converted to 32-bit float working data, 64-bit for a
   Float64 or 32-bit integer image. A sequence frame is decoded first,
   including optional CFA-to-RGB autodebayering, and that decoded frame is
   converted the same way. Every processed input sample is clamped to `[0,1]`;
   unselected RGB channels and an alpha channel are held aside and copied back
   exactly as they were. A non-finite sample in a processed channel of a pixel
   the image's validity admits makes the engine refuse the run.
2. GHT, Histogram, and Arcsinh build a continuous normalized transfer around SP
   and extend it linearly below LP and above HP. Linear, Image Inversion, and STF
   use their dedicated transfers. **Invert** selects an analytical inverse for
   the first three families.
3. RGB/Red/Green/Blue apply transfers independently. Lightness uses CIELAB L*,
   converted back through the exact inverse of the XYZ matrix;
   Saturation transforms HSV saturation and then restores the original CIELAB
   L*; Colour transforms a weighted pseudo-luminance and applies a common RGB
   scale.
4. The float result is converted back to the source pixel type, rounding to the
   nearest code. The framework then blends the active mask, commits the still or
   temporary sequence, and adds history; the result keeps the source's cards,
   acquisition record, and solution and is marked nonlinear.

### Model and equations

Let source intensity be `x` in `[0,1]`, strength be
`D = exp(displayed stretch factor) - 1`, symmetry point be `SP`, and protection
points be `LP` and `HP`. For the nonlinear families, define a positive-offset
base `g(u)` for `u >= 0` and construct

\[
q(x)=
\begin{cases}
-g(SP-LP)+g'(SP-LP)(x-LP), & x<LP,\\
-g(SP-x), & LP\le x<SP,\\
g(x-SP), & SP\le x\le HP,\\
g(HP-SP)+g'(HP-SP)(x-HP), & x>HP.
\end{cases}
\]

The final forward transfer is `F(x) = (q(x)-q(0))/(q(1)-q(0))`. This forces
`F(0)=0` and `F(1)=1`, places maximum slope at SP, and joins the protected
tangent regions continuously. LP and HP are therefore protection boundaries,
not clipping thresholds. The complete inverse construction is given below.

For **Generalized Hyperbolic**, the base family selected by `b` is:

\[
g(u)=
\begin{cases}
\ln(1+Du), & b=-1,\\
\dfrac{1-(1-bDu)^{(b+1)/b}}{D(b+1)}, & b<0,\ b\ne-1,\\
1-e^{-Du}, & b=0,\\
1-(1+Du)^{-1}, & b=1,\\
1-(1+bDu)^{-1/b}, & b>0,\ b\ne1.
\end{cases}
\]

The limiting/special cases are implemented separately for numerical stability.
At `b=0` the family is exponential; negative values move through gentler,
broader integral/logarithmic shapes, while positive values increasingly
concentrate the response around SP. `D=0` is identity.

For **Histogram Transformation**, `m=1/(2(D+1))` and

\[
g(u)=\frac{(m-1)u}{(2m-1)u-m}.
\]

It then uses the same symmetry/protection construction. This is not the same
control convention as the dedicated [Histogram Transformation](histogram-transformation.md)
window. **Arcsinh** instead uses
`g(u)=asinh(Du)=ln(Du+sqrt(D^2u^2+1))`, again with the same construction.

### Analytical inverse equations

For inverse input `y` in `[0,1]`, let `q0=q(0)`, `q1=q(1)`, and undo the
forward normalization with `z=q0+y(q1-q0)`. Define the transformed boundaries
`LPT=F(LP)`, `SPT=F(SP)`, and `HPT=F(HP)`. The inverse bases
`h(v)=g^{-1}(v)` are:

\[
h(v)=
\begin{cases}
\dfrac{e^v-1}{D}, & \text{GHT},\ b=-1,\\
\dfrac{1-(1-(b+1)Dv)^{b/(b+1)}}{Db},
    & \text{GHT},\ b<0,\ b\ne-1,\\
-\dfrac{\ln(1-v)}{D}, & \text{GHT},\ b=0,\\
\dfrac{v}{D(1-v)}, & \text{GHT},\ b=1,\\
\dfrac{(1-v)^{-b}-1}{bD}, & \text{GHT},\ b>0,\ b\ne1,\\
\dfrac{mv}{(2m-1)v-(m-1)}, & \text{Histogram},\\
\dfrac{\sinh(v)}{D}, & \text{Arcsinh}.
\end{cases}
\]

Using the same tangent slopes as the forward construction, the implemented
piecewise inverse is:

\[
F^{-1}(y)=
\begin{cases}
LP+\dfrac{z+g(SP-LP)}{g'(SP-LP)}, & 0\le y<LPT,\\
SP-h(-z), & LPT\le y<SPT,\\
SP+h(z), & SPT\le y<HPT,\\
HP+\dfrac{z-g(HP-SP)}{g'(HP-SP)}, & HPT\le y\le1.
\end{cases}
\]

At `D=0`, forward and inverse are identity. The implementation evaluates the
GHT limiting cases separately and precomputes the transformed breakpoints and
branch coefficients before processing samples. At exactly `b=-1` the engine
inverts the logarithmic branch directly and holds it to the family's
definition.

### Linear, Image Inversion, and STF

**Linear Stretch** uses
`0` at/below BP, `(x-BP)/(WP-BP)` between the points, and `1` at/above WP.
**Image Inversion** is `1-clamp(x,0,1)`; it is distinct from the **Invert**
checkbox.

**STF Transformation** applies the rational midtones function

\[
M(x;m,\ell,r)=
\begin{cases}
0, & x\le\ell,\\
1, & x\ge r,\\
\dfrac{(m-1)t}{(2m-1)t-m}, & \ell<x<r,
\end{cases}
\qquad t=\dfrac{x-\ell}{r-\ell}.
\]

For channel `c`, let `p_c` be its median and let
`s_c=1.4826* MAD_c`; an exactly zero `s_c` is replaced by `0.001`. If at least
one channel median is at or below `0.5`, the normal branch derives

\[
\ell_c=\max(0,p_c-2.8s_c),\quad r_c=1,\quad
m_c=M(p_c-\ell_c;0.25,0,1)
\]

for unlinked STF. Linked STF instead uses
`p=mean(p_c)`, `ell=max(0,mean(p_c-2.8s_c))`, `r=1`, and
`m=M(p-ell;0.25,0,1)`. When every channel median exceeds `0.5`, the inverted
branch uses

\[
\ell_c=0,\quad r_c=\min(1,p_c+2.8s_c),\quad
m_c=1-M(r_c-p_c;0.25,0,1)
\]

for unlinked STF. Its linked counterpart uses
`p=mean(p_c)`, `ell=0`, `r=min(1,mean(p_c+2.8s_c))`, and
`m=1-M(r-p;0.25,0,1)`. The resulting curve is `M(x;m_c,ell_c,r_c)` per
channel or `M(x;m,ell,r)` when linked.

Derived STF values are not exposed in the UI. They are derived from the exact
median and MAD of the whole image a run changes: the target's for a preview, a
preview area, and Apply, measured once for each change of the image, and each
frame's own for a sequence. For Lightness,
Saturation, or Colour, the current component path evaluates STF slot 0; with
**Linked STF** cleared that is the curve derived from the red input channel,
not a curve derived from the selected component.

In **Colour** mode, normalized nonnegative weights form
`L = wR*R + wG*G + wB*B`, then `L'=F(L)` and the nominal common scale is
`L'/L`. Clip clamps each scaled channel. Rescale divides the common scale by
`max(1, L'*max(R,G,B)/L)`, preserving RGB ratios when possible. Zero luminance
uses a guarded neutral/black result. Current **Default** and **Image** coefficient
sources both resolve to equal thirds; only **Manual** changes the weights.

### Histograms, masks, precision, and metadata

The lower plot is the source histogram. The upper plot is either an immediate
bin-remapped prediction or a histogram of a transformed image. Lightness,
Saturation, and Colour add a computed component as an auxiliary histogram and
display that component; STF also requires image/statistics-derived work. Plot
bin count changes only these diagnostics. A whole code on a bin boundary lands
in its own bin, by exact interval arithmetic; a 12-bit or finer histogram is
redrawn to fill its plot when the dialog is resized; and the transfer curve is
drawn so that it stays visible in the light theme.

The clipping overlay sums the clipped counters of every channel a plot holds,
an auxiliary component included, and divides by the samples those channels
admitted. On the lower plot, and on an upper plot recounted from a stretched
target, a clipped sample is one at or beyond `0` or `1`; on a bin-remapped upper
plot it is a count that lands in the first or last bin after the remap, so that
figure is a binned forecast. Treat the overlay as orientation only.

Samples are held in a 32-bit float working image, 64-bit for a Float64 or
32-bit integer source; color-space intermediates and the curves are evaluated in
double (a one-channel or independent row may use the engine's compiled Float32
evaluation within its stated accuracy) and stored at the working image's
precision. Conversion back preserves the original pixel type, rounding an
integer output to the nearest code. The active mask is a later original/result blend and is absent from
the histogram-only prediction.

The source's header cards, acquisition record, and astrometric solution are
kept; the result states no transfer function and its processing history records
the stretch. The processing state becomes nonlinear with `ght-stretch`
provenance for every mode. History records the process/transform,
channel, displayed strength, `b`, LP/SP/HP, and BP/WP. It does not record the
mask, Colour clip, luminance coefficient source/values, linked STF state or
derived values, or histogram/display settings. Even an inverse, Linear Stretch,
Image Inversion, or identity-strength Apply is marked nonlinear.

### Boundary and failure behavior

- The intended input is one-channel grayscale or three-channel RGB. The menu
  gate requires only an active image/sequence, so other channel counts are not
  proactively rejected even though the UI and color paths are designed for
  mono/RGB.
- Nonlinear sample inputs are clamped to `[0,1]`; Linear Stretch and Image
  Inversion also clamp their outputs. A Lightness or Saturation conversion back
  to RGB can leave a value outside `[0,1]`: a floating image keeps it and an
  integer image saturates at its range.
- The UI constrains LP/SP/HP and BP/WP. The engine still has a defensive
  `WP <= BP` step response for programmatic callers.
- STF statistics are measured from the whole image a run changes. When they
  cannot be measured, or the engine refuses the derivation (no valid sample, a
  non-finite admitted sample, or a channel median outside `[0,1]`), the preview
  or Apply fails with an "Error executing the process" message and nothing is
  published.
- Apply cannot acquire a busy target. Preview generations discard stale results.
  Sequence cancellation is cooperative, deletes its temporary output, and does
  not replace the source. A failed still transform does not commit a result.
- No transform-specific preset or parameter persistence exists. Successful Apply
  and Reset restore execution defaults; several toolbar/preview/target states
  deliberately survive Reset for the current window only.

## Parameter interactions and starting points

| Combination | Conservative starting point | Too weak looks like | Too strong looks like | Adjustment order |
| --- | --- | --- | --- | --- |
| GHT strength + SP | `b=0`, `LP=0`, `HP=1`; set SP from a representative subject/background boundary, then raise strength from `0` | Histogram/preview stays near the source | Important regions crowd toward an endpoint; noise/contrast becomes overemphasized | SP, strength, then protection points |
| LP/SP/HP | Begin with full protection range `0 <= SP <= 1`; move LP upward or HP downward only to reduce curvature outside the subject range | Protected region is not visibly distinct from the central nonlinear response | Broad tonal ranges become nearly linear and lose desired separation | SP first, LP second, HP third, recheck strength |
| Local intensity b | Start at `0` and compare nearby values with the same D/SP/LP/HP | Desired local contrast remains broad | Contrast becomes overconcentrated near SP | Strength/SP first, then b, then protection |
| RGB vs Lightness/Colour | Begin with RGB; compare Lightness or Colour only when channel balance/gamut requires it | Color behavior remains unchanged from independent channels | Saturated colors clip, rescale, or shift through component conversion | Choose channel model, then transfer, then inspect all channels/endpoints |
| Colour Clip vs Rescale | Start with Rescale when ratio preservation matters; compare Clip at identical settings | Bright colors receive less luminance lift under Rescale | Clip drives one or more channels to 1 and changes ratios | Coefficients, transfer, then clip policy |
| STF linked/unlinked | Start linked for a color-balanced RGB image; unlinked is a deliberate background-neutralizing choice | Linked curve leaves channel backgrounds separated | Unlinked curves alter intended color balance | Inspect medians/MAD, choose linked state, then apply; do not tune hidden values |
| Linear BP/WP | Start at `0/1`, move one endpoint at a time using fields or outer handles | Dynamic range is barely expanded | Shadows/highlights become exactly 0/1 | BP, WP, then inspect endpoint counts independently |

These are source-backed adjustment orders, not measured recommendations. No
approved real master was executed.

## Inputs, outputs, and metadata

- **Input count and compatibility:** one open still image or sequence target.
  Intended formats are mono and RGB; normalized access is provided for all
  supported source pixel types.
- **Output:** the same target is replaced after success. Width, height, pixel
  type, and source metadata are preserved. Channel count and pixel format are
  normally preserved too. If automatic sequence CFA debayer is enabled, each
  one-channel mosaic frame is decoded as three-channel RGB before processing,
  so the successful replacement sequence is RGB rather than CFA. A sequence is
  written to a `SEQ_SWAP` file in the session's temporary directory, and the
  view shows it in place of the original only after full success; the original
  file is not modified.
- **Precision:** processing is through a 32-bit float working image, 64-bit
  for a Float64 or 32-bit integer source. The output is converted to the
  original integer/float/double type.
- **Masks:** the active mask is blended after transformation. Its inversion and
  channel mapping follow the shared process framework. The mask does not change
  the stored history parameters or histogram prediction.
- **State/history:** output is marked nonlinear (`ght-stretch`). History records
  the transform name (including inverse), channel and primary numeric points,
  but omits the secondary color/STF/mask/display details listed above.
- **Auxiliary artifacts:** none. Preview images and sequence temporary data are
  transient. No output filename, overwrite prompt, LUT, preset, or report is
  exposed.

## Performance

The main independent-channel loop is linear in pixels × processed channels and
uses oneTBB. GHT/Histogram/Arcsinh scalar evaluation is constant work per sample.
Lightness and Saturation add full-image RGB↔CIELAB and/or HSV conversions;
Colour adds weighted-luminance and per-pixel scaling; STF adds median/MAD
statistics, measured once per revision of the target and kept for later
previews and Apply, and once for each sequence frame. All paths allocate at
least one float working image, and component modes allocate additional
full-image buffers.

Real-time preview transforms the full target. Disable **Preview** while making a
series of control edits on a large image, then re-enable it for a checkpoint.
Use **8-bit (256)** plot resolution unless finer diagnostic bins are necessary.
Higher bin counts increase histogram time and memory but cannot improve output
precision. A sequence additionally requires temporary disk space approximately
comparable to its rewritten content. No network or external process is used.
No production timing was measured.

## Worked example

- **Evidence mode:** Source-and-test-derived; no project-owned linear master was
  executed.
- **Input condition and settings:** normalized grayscale samples with
  **Generalized Hyperbolic**, displayed strength `ln(D+1)=1.252763` (`D=2.5`),
  `b=0`, `LP=0.10`, `SP=0.35`, `HP=0.90`, and **Invert** cleared. This is the
  exponential branch and matches the focused parity test's representative
  D/LP/SP/HP settings.
- **Expected checkpoint:** the exact forward transfer gives `0→0`,
  `0.05→0.047488`, `0.10→0.094977`, `0.20→0.202880`, `0.35→0.424830`,
  `0.50→0.646780`, `0.90→0.955136`, `0.95→0.977568`, and `1→1`. LP and HP are
  smooth tangent boundaries; neither clips to an endpoint.
- **Inspect at 100%/statistics/history:** for a real run, verify endpoint counts
  independently, confirm that intended detail remains separated, and confirm a
  nonlinear `ght-stretch` state/history entry. Do not treat the toolbar clipping
  overlay as an exact prediction.
- **Limitations:** the values verify equations and boundaries only. They do not
  establish visual quality, noise behavior, mask appearance, sequence
  consistency, or production runtime on an astronomical image.

## Warnings and limitations

- Apply is destructive and every mode marks the result nonlinear. The inverse
  checkbox does not restore a linear declaration or guarantee a pixel-perfect
  round trip after clipping, color conversion, or quantization.
- **Image Inversion** is a separate `1-x` operation. **Invert** means the
  analytical inverse of GHT/Histogram/Arcsinh.
- The UI label **Stretch factor(In(D+1))** uses a capital `I`; the implemented
  quantity is natural logarithm `ln(D+1)`.
- Current **Luminance coefficients source: Image** does not read image/profile
  coefficients; it behaves like Default with equal RGB weights.
- STF is recomputed per frame in sequences. This can normalize frames
  differently, as Histogram Transformation's Auto-stretch also does; use a
  manual curve here, or Histogram Transformation's manual values, for one fixed
  transfer across frames.
- In Lightness, Saturation, and Colour modes, unlinked STF uses the red-derived
  STF slot for the transformed component. Use linked STF when a shared RGB
  statistic is more appropriate; no component-statistics STF is exposed.
- Lightness/Saturation/Colour use float color conversions and can encounter
  gamut/rounding effects. Colour Clip can change RGB ratios; Rescale preserves a
  common ratio by reducing the requested luminance increase.
- The upper/lower clipping overlays are histogram endpoint diagnostics. A
  bin-remapped prediction counts its first and last bins, and every percentage
  is a share of all the channels a plot holds, the auxiliary component included,
  so neither is an acceptance measurement.
- **Absolute scales** is selected by default but actually enables per-channel
  local peak normalization. Clear it to compare relative channel populations.
- In Linear Stretch, the center histogram handle remains visible but does not
  change any processing parameter.
- The point/area SP pickers average all source channels regardless of the
  selected processing channel. The histogram picker uses the upper plot. All
  three remain armed after a selection and continue replacing SP until
  explicitly cancelled by the actions described in the parameter table.
- A sequence with automatic CFA debayer enabled is transformed and replaced as
  RGB. Disable automatic sequence debayer when preserving the one-channel CFA
  representation is required; a CFA still is not automatically debayered here.
- The analytical inverse at exactly `b=-1` is held to the logarithmic branch's
  definition by the engine's tests.
- Reset does not restore the target, Preview, Plot resolution, or most histogram
  toolbar choices. No process setting is persisted across windows.
- A Float64 or 32-bit integer image is evaluated through a 64-bit working
  image, every other representation through 32-bit float. Plot resolution
  never changes this processing precision.
- The primary GHS reference describes its upstream implementation and theory;
  current AfterNight source/tests are authoritative where controls, precision,
  metadata, defaults, or behavior differ.

## Troubleshooting

| Symptom or exact error | Verified cause/validation path | Recovery | Evidence source |
| --- | --- | --- | --- |
| GHT is disabled in the menu | No active image/sequence view exists | Activate the intended view, then reopen **Process > Intensity Transformations** | Registry/launcher/action-state source |
| A second window will not open | The same process is already open for the target, or an exclusive process owns it | Use/close the existing process or finish the exclusive operation | ProcessLauncher source |
| Apply cannot start | The selected target is closed or its processing lock is busy | Re-select an open idle target and retry | ImageProcessControl execution source |
| Strength changes do nothing | Displayed strength is `0`, or a special combination is effectively identity; an STF whose statistics cannot be derived fails with an error instead | Raise strength for GHT/Histogram/Arcsinh; for STF verify a valid nonempty mono/RGB target and statistics | Engine GHT/STF source |
| LP or HP seems to clip data | LP/HP are tangent boundaries, while actual endpoint clipping may come from input clamping, Linear BP/WP, color conversion, or output conversion | Inspect the transfer and real output statistics; do not infer clipping from protection-point location | GHT equations/parity tests |
| Colour mode shifts bright color | Clip clamps channels independently, or chosen luminance weights drive a large common scale | Compare **Rescale** and check manual coefficients; inspect channels at 100% | Colour model source/parity test |
| **Image** coefficients look identical to **Default** | Both currently resolve to equal RGB weights | Use **Manual** for explicit weights; record this limitation in the workflow | Parameter mapping/engine source |
| Histogram clipping reads zero or an implausible percentage | A bin-remapped prediction counts its first and last bins after the remap, and every figure is a share of all the channels a plot holds, an auxiliary component included | Measure the applied/preview image independently; use the overlay only for orientation | `HistogramWidget::updateClippingInfo`; `remapHistogramThroughStretch` |
| Sequence frames receive different STF curves | STF statistics are recomputed for every input frame | Use a manual GHT/Histogram/Arcsinh curve when a fixed transfer across frames is required | Engine sequence execution source |
| A CFA sequence becomes RGB after Apply | Automatic sequence CFA debayer was enabled, so frames were decoded to three channels before transformation | Disable automatic sequence debayer before Apply when the output must remain a one-channel CFA sequence | RT-preview, ImageViewControl, demosaic, and concrete sequence sources |
| Unlinked STF in a component mode follows red statistics | Lightness/Saturation/Colour evaluate STF slot 0 after RGB statistics are prepared | Select **Linked STF** for one shared RGB curve, or choose RGB/a named channel for true per-channel behavior | Engine special-component and STF source |
| Linear center handle has no effect | Linear mode maps only the outside handles to Black/White point | Use the numeric BP/WP fields or outside handles | Process range-handler source |
| Preview and Apply differ | Active mask, target, picker source, stale preview generation, or sequence per-frame STF adaptation differs | Confirm target/mask, wait for current preview, and remember that sequence STF is per frame | RT-preview/process/engine source |
| Inverse does not restore original state | Float conversion, clipping, quantization, component conversion, or prior unmatched settings prevent exact reversal; state is always marked nonlinear | Apply inverse only with matching parameters to a suitable copy and compare numerically | Parity/state tests and engine source |

## Related workflow steps

- Complete [Background Extraction](../background-extraction/background-extraction.md),
  [Background Neutralization](../color-calibration/background-neutralization.md),
  and the appropriate color calibration before the first display stretch.
- Compare the dedicated [Histogram Transformation](histogram-transformation.md)
  for direct shadows/midtones/highlights control; its manual values apply one
  fixed curve to every sequence frame.
- Compare [ArcSinh Stretch](arcsinh-stretch.md) when the simpler dedicated
  arcsinh workflow is sufficient.
- After the nonlinear transition, use [Curves Transformation](curves-transformation.md)
  for deliberate tone/color shaping and continue with nonlinear finishing.
- See [Image History](../../interface/image-history.md) before experimenting
  with destructive variations.

## References and provenance

- Mike Cranfield,
  [Generalized Hyperbolic Stretch tool documentation](https://www.ghsastro.co.uk/doc/tools/GeneralizedHyperbolicStretch/GeneralizedHyperbolicStretch.html):
  primary background for the five-parameter GHS construction, transfer families,
  inverse, protection points, and color models. The document credits David
  Payne's 2021 proposal and his collaboration with Mike Cranfield. Checked
  2026-07-13.
- GHS Astro,
  [Generalized Hyperbolic Stretch information and publication history](https://ghsastro.co.uk/information/):
  authorship and algorithm-publication context. Checked 2026-07-13.
- Current AfterNight process, engine, histogram, framework, and focused test
  sources are authoritative for the shipped UI,
  float precision, defaults, metadata, masks, previews, and sequence behavior.
