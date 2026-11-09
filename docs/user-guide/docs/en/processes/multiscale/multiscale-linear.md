---
kind: process
stage: noise
menu: Process > Multiscale > Multiscale Linear Transform
summary: >-
  Separates an image into detail layers and a coarse residual, changes the layers you choose and reconstructs the
  image.
data_in: linear
data_out: linear
---
# Multiscale Linear Transform

<!-- MEDIA_SLOT: UG-MEDIA-103 -->

## Purpose and expected result

Multiscale Linear Transform separates an image into detail layers plus a coarse
residual, changes selected layers, and reconstructs the image. Use it for
scale-selective detail suppression or enhancement, multiscale noise reduction,
edge deringing, or residual-tone adjustment. It offers two undecimated models:
configurable starlet smoothing (**Starlet Transform**) and Gaussian smoothings
of the original image (**Multiscale Linear Transform**).

**Apply is destructive:** a successful run replaces the selected still or
sequence. Optional deringing maps are separate views, and Preview snapshots are
auxiliary images. Start on a duplicate. For calibrated linear data, keep strong
stretching and range remapping disabled until the intended linear operations are
verified.

## At a glance

| Item | Current behavior |
| --- | --- |
| Menu path | **Process > Multiscale > Multiscale Linear Transform** |
| Enabled when | An image or sequence is active. A launch is refused with a message when this process is already attached to the image or an attached process holds exclusive access |
| Inputs | One grayscale or color still or sequence, in any integer or floating representation; optional enabled active target mask; 1–11 detail layers plus one residual, fewer for some algorithm and spacing combinations |
| Outputs | Replaces the target with the same geometry, channel count, and representation; on a still Apply only, optional dark/bright deringing-map stills open beside it; Preview snapshots are auxiliary stills |
| Default state | Multiscale Linear Transform, Dyadic, 6 detail layers, all layers enabled at bias 0, all optional processing groups off, global Amount 1, Processed Image preview, Preview area on |
| Recommended image state | A duplicate linear image for noise/detail work. LSTF and DRE are nonlinear display/range operations; derived color components can change reconstructed RGB colors. |
| Preview support | Asynchronous processed preview that runs what Apply runs (over the Preview area, what Apply makes of that area), three selected-layer diagnostic modes, Linear Mask display, original/result switch, snapshot, zoom, autostretch, and display-channel selection. Global Amount and the active target mask post-blend every preview mode, including layer and mask diagnostics. |
| Sequence support | Every frame is processed as Apply processes a still image of it, into a temporary replacement sequence, with progress and Cancel; settings are captured once for the run; deringing maps are not made for sequence frames |
| Mask support | Two distinct masks: **Linear Mask** weights thresholding inside the transform; the target's active mask blends the final result afterward, together with global Amount |
| External dependencies | None outside the deployed application; the transform is the bundled AfterNight engine's, and the window's layer arithmetic runs as engine PixelMath programs on the threads the run is granted |

## Before you start

Duplicate the target and inspect it at 100% with a stable display stretch. Note
whether an active target mask is enabled: it affects the committed/processed
result but is not the same as **Linear Mask**. Decide whether the task needs
noise/detail changes on the detail layers or a tone change on the residual.

Use **Processed Image** to judge the real pipeline. The three **Layer** preview
modes are signed coefficient visualizations, not isolated simulations of the
enabled layer, noise reduction, deringing, masks, LSTF, or DRE. At zero layer
bias they are flat mid-gray or black by design.

The transform refuses a depth it cannot build rather than narrowing it. With
**Dyadic** spacing, the Multiscale Linear Transform accepts at most 7 detail
layers and the Starlet Transform at most 10; use **Linear** spacing for more.

## Procedure

1. Activate a duplicate and open **Process > Multiscale > Multiscale Linear
   Transform**. Confirm **Target Image**, **Channel**, active-mask state, and
   checked **Preview**.
2. Leave **Multiscale Linear Transform**, **Dyadic**, and 6 **Detail Layers**
   for a first pass. Choose **Starlet Transform** only when a specific scaling
   kernel is part of the intended model. Use Linear spacing when evenly
   increasing supports, or more layers than Dyadic accepts, are required.
3. In the layer tree, select a detail layer. Toggle **Enabled** to locate the
   relevant scale, or use a small **Bias**. Compare at 100%; positive bias is a
   strong nonlinear UI mapping, not a simple `1 + bias` gain.
4. For noise reduction, enable the selected layer's **Noise Reduction**, start
   at Threshold `3`, and reduce **Amount** before adding **Iterations**. Use
   **Copy to All** only after checking which details should share the setting.
5. If noise reduction should follow brightness, enable **Linear Mask** and use
   **Preview Mask** to inspect it. Remember that this mask weights per-layer
   and k-Sigma thresholding only; it does not protect bias, LSTF, DRE, or
   deringing.
6. Use **k-Sigma** for one threshold policy across every detail layer. Compare
   Soft versus hard thresholding; enable **Use MRS** only when robust iterative
   estimation of the first-detail noise scale is useful.
7. Enable **Deringing** only for visible edge overshoot. Keep dark/bright values
   low. On a still, request maps when their layer-stage corrections need
   inspection; they open beside the target after the result lands. No maps are
   made by Preview, snapshots, or sequence Apply.
8. Adjust the residual with **LSTF** only when a nonlinear residual tone mapping
   is intended. Use **Low Range**/**High Range** last because DRE remaps and
   clamps the reconstructed result.
9. Return to **Processed Image**, compare original/result, inspect several
   regions at 100%, then use **Apply**. Check Image History but save the full
   settings separately: history is only a summary and omits per-layer values.

## Complete parameter and action reference

| UI label/action | Type and unit | Runtime default | Persisted-default exception | Valid values, step, or constraints | Effect of increasing, decreasing, or selecting | Dependencies and enable/visibility rules | Quality, artifact, runtime, memory, or disk trade-off | Scope: preview/apply, channel, mask, still/sequence | Persistence |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| **Algorithm** | Selector | Multiscale Linear Transform | None | Starlet Transform, Multiscale Linear Transform | Chooses recursive à trous/starlet smoothing or Gaussian smoothings of the original image | Scaling Function enabled only for Starlet Transform | The two have different scale responses and different depth limits; changing it rebuilds layers and noise weights | Preview/Apply; selected channel(s) | Captured; Reset/success Multiscale Linear Transform |
| **Detail Layers** | Integer | `6` | None | `1..11`; step `1`; residual is added automatically. The transform refuses a deeper layer than it can build: with Dyadic spacing at most `7` for Multiscale Linear Transform and `10` for Starlet Transform; with Linear spacing, Multiscale Linear Transform needs `(Detail Layers − 1) × Linear Scale ≤ 95` | Adds finer-to-coarser detail bands before the residual | Rebuilds layer tree | More layers increase transform, threshold, memory, and reconstruction work; a refused depth shows an error in Preview and Apply | Preview/Apply | Captured; Reset/success `6` |
| **Layer Spacing** | Selector | Dyadic | None | Dyadic, Linear | Dyadic doubles the scale per layer (`1, 2, 4, …`); Linear adds **Linear Scale** per layer (`1, 1+q, 1+2q, …`) | Linear Scale visible only for Linear | Larger scales represent coarser structure. The Multiscale Linear Transform's Gaussian radius grows with the scale and is refused above 128 pixels; Starlet keeps a fixed tap count and spreads the taps up to 512 pixels apart | Preview/Apply | Captured; Reset/success Dyadic |
| **Linear Scale** | Integer | `1` | None | `1..16`; step `1` | In Linear spacing, raises the scale increment between consecutive layers | Visible only when Layer Spacing is Linear | Larger values separate scales faster and reach the Multiscale Linear Transform's depth limit sooner; they do not change Starlet's tap count | Preview/Apply | Captured; Reset/success `1` |
| **Scaling Function** | Selector | B3 Spline (5) | None | Linear Interpolation (3), B3 Spline (5), Gaussian (5), Gaussian (7), Gaussian (9), Gaussian (11), Small Scale 3/4/5/6/8/12/16/24/32 (3) | Selects the normalized starlet smoothing kernel | Enabled only for Starlet Transform; ignored by the Multiscale Linear Transform | Wider kernels smooth more broadly and cost more | Starlet Preview/Apply | Captured; Reset/success B3 Spline (5) |
| Global **Amount** | Double + slider | `1.00` | None | `0..1`; spin step `0.05`; slider `0..1000` | Mixes original (`0`) to the fully processed result (`1`) | Transform still runs at `0` | Lower values soften every change together; full processing cost remains | Preview/Apply; blended with the active target mask after the process | Captured; Reset/success `1` |
| Layer tree (**En / Layer / Bias / NR**) | Tree/table | Layers 1–6 + Residual; all enabled, bias `0`, NR off | None | One row per detail plus Residual; single selection | Selects the inspector row and summarizes enabled/bias/NR state | Residual has no per-layer NR (`—`) | More detail layers add rows and processing | Preview/Apply settings | Rebuilt on layer-count change; Reset/success defaults |
| **Enable All** | Button | — | None | Click | Enables every detail and the residual | Transform controls enabled outside Apply | Restores all bands but not their biases/NR settings | Preview/Apply settings | Until changed/reset |
| **Disable All** | Button | — | None | Click | Disables every detail and the residual | Transform controls enabled outside Apply | Reconstructs zero before later range stages; useful only diagnostically | Preview/Apply settings | Until changed/reset |
| **Reset Biases** | Button | — | None | Click | Sets every layer bias to `0` | Does not change enabled or NR state | Removes layer gains without removing suppression/NR choices | Preview/Apply settings | Until changed/reset |
| **Linear Mask** | Checkable group | Off | None | Off/on | Builds an intensity mask from the processed channel that weights threshold Amount pixel by pixel | Built and used only when per-layer NR or k-Sigma is active | Adds one mask plane and an exact Gaussian smoothing per channel | Preview/Apply; processed channel; separate from target mask | Captured; Reset/success off |
| **Amplification** | Integer + slider | `100` | None | `1..300`; step `1` | Raises `A*x` before mask clamping, making more samples reach full mask weight | Linear Mask group | High values saturate most of the mask and reduce tonal selectivity | Threshold weighting/Preview Mask | Captured; Reset/success `100` |
| **Smoothness** | Double + slider | `1.0` px sigma | None | `0..10`; step `0.1` | Gaussian-smooths the mask with this sigma; `0` leaves it unsmoothed | Linear Mask group | More smoothing broadens transitions and adds convolution work | Threshold weighting/Preview Mask | Captured; Reset/success `1` |
| **Preview Mask** | Checkbox | Off | None | Off/on | Shows each processed channel's Linear Mask in place of the processed preview; takes precedence over the Layer preview modes | Linear Mask group must be checked; does not change Apply; shared global Amount and active-target-mask blending still run afterward | Hides the processed transform, but Amount below `1` or an active target mask mixes original pixels into the displayed diagnostic | Preview/snapshot display only; post-blended by global Amount and target mask | Window lifetime; Reset/success off |
| **Inverted** | Checkbox | Checked | None | Off/on | Uses `1 - clamp(A*x)` before smoothing | Linear Mask group | Reverses which intensity range receives more thresholding | Threshold weighting/Preview Mask | Captured; Reset/success checked |
| **k-Sigma** | Checkable group (**k-Sigma Thresholding**) | Off | None | Off/on | Applies one sigma-threshold policy to every detail after per-layer NR | Detail layers only; residual excluded | Can remove weak real detail together with noise | Preview/Apply | Captured; Reset/success off |
| **Threshold (k)** | Double + slider | `3.0` sigma | None | `0..10`; step `0.1` | Raises or lowers the global `k*sigma` threshold | k-Sigma group | Higher values suppress a wider coefficient range | Preview/Apply; all detail layers | Captured; Reset/success `3` |
| k-Sigma **Amount** | Double + slider | `1.00` | Header initializer says `0.5`, but live initialization and Reset set `1.0` | `0..1`; step `0.05` | Blends unthresholded (`0`) to thresholded (`1`) coefficients | k-Sigma group; multiplied by the Linear Mask weight when enabled | Lower values retain more coefficient energy | Preview/Apply | Captured; Reset/success `1` |
| **Soft Threshold** | Checkbox | Checked | None | Off/on | On shrinks magnitudes beyond threshold; off zeros magnitudes strictly below threshold | k-Sigma group | Soft is smoother but biases retained magnitudes; hard can look harsher | Preview/Apply; all details | Captured; Reset/success checked |
| **Use MRS** | Checkbox | Off | None | Off/on | Refines the first-detail noise estimate three times using coefficients whose magnitude is at most `k*sigma` | k-Sigma group | Adds robust-estimation passes; it is not a persistent multiresolution support mask | Preview/Apply | Captured; Reset/success off |
| **Deringing** | Checkable group | Off | None | Off/on | Enables edge-weighted coefficient attenuation and a post-reconstruction overshoot correction | Effective when Dark or Bright is above `0` | Can soften legitimate high-contrast detail | Preview/Apply | Captured; Reset/success off |
| **Dark** | Double + slider | `0.10` | None | `0..1`; step `0.05` | Reduces negative detail coefficients and negative reconstruction differences near edges | Deringing group | Higher values suppress dark halos but can flatten dark edge structure | Preview/Apply | Captured; Reset/success `0.1` |
| **Bright** | Double + slider | `0.00` | None | `0..1`; step `0.05` | Reduces positive detail coefficients and positive reconstruction differences near edges | Deringing group | Higher values suppress bright halos but can flatten highlights | Preview/Apply | Captured; Reset/success `0` |
| **Output Deringing Maps** | Checkbox | Off | None | Off/on | Requests both dark and bright layer-stage correction maps, opened beside the target after the result lands | Still Apply only; ignored in Preview, snapshots, and sequence processing; both maps are made when the corresponding strength is `0` | Adds two Float32 map images and views; a zero-strength side produces an all-zero map | Still Apply; selected processed channel or component | Captured; Reset/success off |
| **LSTF Mode** | Selector (**Large-Scale Transfer Function**) | None | None | None, Hyperbolic, Log10, Natural Log (ln) | Maps the weighted residual before reconstruction | Breakpoint shown only for Hyperbolic; no effect when the residual is disabled | All non-None modes are nonlinear and clamp the weighted residual to `[0,1]` | Preview/Apply; residual only | Captured; Reset/success None |
| **Breakpoint** | Double + slider | `0.75` | Builder starts at `1.0`, but live initialization and Reset set `0.75` | `0.1..1`; step `0.05` | In Hyperbolic mode, smaller values brighten more strongly | Visible only for Hyperbolic | White maps below 1; not a normalized endpoint-preserving stretch | Preview/Apply; residual only | Captured; Reset/success `0.75` |
| **Low Range** | Double + slider | `0.00` | None | `0..1`; step `0.05` | Adds to reconstructed values before division by total range | Always available (**Dynamic Range Extension**) | Shifts values upward while the shared denominator compresses the range | Preview/Apply; after reconstruction/deringing | Captured; Reset/success `0` |
| **High Range** | Double + slider | `0.00` | None | `0..1`; step `0.05` | Enlarges the denominator without adding to the numerator | Always available (**Dynamic Range Extension**) | Compresses highlights/dynamic range; result is clamped | Preview/Apply; after reconstruction/deringing | Captured; Reset/success `0` |
| **Selected Layer** | Group title and read-only label | Layer 1 | None | `Layer n`, `Residual Layer`, or `No layer selected` | Identifies which layer the inspector edits | Changes with tree selection | Avoid editing the wrong spatial scale | Settings and layer diagnostic preview | Window lifetime |
| Selected-layer **Enabled** | Checkbox | Checked | None | Off/on | Includes (`on`) or removes (`off`) the selected layer in reconstruction | Applies to residual too | Disabling a layer can remove real structures; disabling residual removes the base image | Preview/Apply | Captured per layer; Reset/success checked |
| **Copy to All** | Button | — | None | Click | Copies selected Enabled and Bias to every row; copies NR only between non-residual details | Residual never receives NR; copying from Residual leaves detail NR unchanged | Broad changes can erase carefully tuned scale differences | Preview/Apply settings | Until changed/reset |
| **Reset Layer** | Button | — | None | Click | Resets selected row to enabled, bias 0, NR off, threshold 3, Amount 0.5, iterations 1 | Residual NR remains unavailable | Per-layer NR Amount becomes `0.5`, unlike full Reset/live startup `1.0` | Preview/Apply settings | Until changed/full reset |
| **Bias** | Double + slider | `0.00` | None | `-1..15`; spin step `0.01`; paired slider spans negative/positive adjustment | `-1` removes the layer; negative values attenuate; positive values strongly amplify | Applies to selected detail or residual | Positive UI values map to `6×` gain increments, `8×` on the Multiscale Linear Transform's first detail; clipping/ringing can grow quickly | Preview/Apply; layer diagnostic uses a separate display gain of `5×` bias | Captured per layer; Reset/success `0` |
| Selected-layer **Noise Reduction** | Checkable group | Off | None | Off/on | Enables repeated soft thresholding for selected detail | Disabled/unavailable for Residual | Can smooth noise and weak structures at that scale | Preview/Apply; detail layers only | Captured; Reset/success off |
| NR **Threshold (σ)** | Double + slider | `3.0` UI sigma | None | `0..40`; step `0.1`; the threshold is `0.05` of the UI value in noise deviations | Raises the coefficient threshold used for selected-layer shrinkage | Selected-layer NR | Higher values remove more coefficient energy | Preview/Apply; weighted by the Linear Mask when enabled | Captured; Reset/success `3` |
| NR **Amount** | Double + slider | `1.00` at startup/full Reset | **Reset Layer** sets `0.50`; layers added by raising Detail Layers also start at `0.50` | `0..1`; step `0.05` | Blends each shrinkage pass from original to thresholded coefficient | Selected-layer NR | Lower values soften each pass | Preview/Apply; weighted by the Linear Mask when enabled | Captured; full Reset `1`, layer Reset `0.5` |
| NR **Iterations** | Integer | `1` | None | `1..10`; step `1` | Repeats shrink/blend on the already changed coefficient | Selected-layer NR | More passes increase suppression and work; no re-decomposition between passes | Preview/Apply | Captured; Reset/success `1` |
| **Target Image** | Image/sequence selector | Attached active target | None | Any target accepted by launch guards | Changes destructive destination, active mask, channels, and preview | Disabled during Apply | Wrong target is overwritten | Preview/Apply; still/sequence | Target lifetime; Reset unchanged |
| **Channel** | Selector | RGB, or a disabled Grayscale for a grayscale target | None | Grayscale; RGB, Red, Green, Blue, CIE L* (Lightness), CIE a*, CIE b*, CIE c*, CIE h*, CIE X, CIE Y (Luminance), CIE Z, HSL Hue/Saturation/Lightness, HSV Hue/Saturation/Value, YCbCr Luma/Blue Chroma/Red Chroma | RGB transforms the stored channels independently; another choice is extracted, transformed, then recomposed into the color image | Non-grayscale choices only for color targets; disabled during Apply | Derived conversion/recomposition adds work and can alter out-of-gamut colors | Preview/Apply | Captured; target-dependent; Reset unchanged |
| **Preview** | Checkbox | Checked | None | Off/on | Enables asynchronous live preview | Stays available during Apply; Apply cancels a preview in progress | Recomputes after parameter edits; a burst of changes collapses into one run with the latest values | Preview only; active-target-mask-aware | Window lifetime; Reset unchanged |
| Preview mode | Selector | Processed Image | None | Processed Image, Layer (All Changes), Layer (Increasing Pixels), Layer (Decreasing Pixels) | Selects real pipeline or signed selected-layer visualization (the residual can be shown too) | Enabled only while Preview is checked; global Amount and the active target mask post-blend the selected visualization; Preview Mask takes precedence | Layer modes are diagnostics, not reconstructed output; Amount or a target mask can mix the original into them | Preview/snapshot only | Reset/success Processed Image |
| **Preview pane** | Image viewer | Processed result over the Preview area | None | Standard pan/scroll and actions below | Shows processed or diagnostic output without commit | Exists while Preview checked | Holds source/result/display buffers | Preview only | Window lifetime |
| **Fit To Window** | Preview action | Fitted initially | None | Click or **Ctrl+Shift+F** | Fits whole image | Preview required | Broad context can hide small-scale effects | Display only | Viewer lifetime |
| **Zoom In** | Preview action | Current scale | None | Click; `1.25×` | Enlarges view | Preview required | Better coefficient/noise inspection | Display only | Viewer lifetime |
| **Zoom Out** | Preview action | Current scale | None | Click; `0.8×` | Reduces view | Preview required | More context, less pixel detail | Display only | Viewer lifetime |
| **Zoom 100%** | Preview action | Current scale | None | Click; scale `1.0` | Shows one-pixel viewer scale | Preview required | Best for noise, halos, and ringing | Display only | Viewer lifetime |
| **View Channel** | Preview selector | Follows result | None | Supported stored/derived display components | Changes displayed component only | RGB choices require RGB result | Helps isolate channel-dependent artifacts | Display only | Viewer lifetime |
| **Display Mode** | Preview selector | Linear | None | Linear, Autostretch | Changes display transfer only; Autostretch measures the image's median and MAD exactly and rounds each display byte to the nearest code | Preview required | Autostretch can exaggerate threshold residuals | Display only | Preview lifetime |
| **Autostretch Settings** | Preview action | Initialized from target view | None | Opens display settings | Adjusts Autostretch only | Relevant in Autostretch mode | Adds display/statistics work | Display only | Preview lifetime |
| **Switch image** | Preview action | Result shown | None | Click, **Space**, right-click, or **Ctrl+Shift+O** | Toggles result/original | Preview required | Direct structure/noise comparison | Preview/current frame | Preview lifetime |
| **Preview area** | Preview checkbox/region | Checked when the Preview opens; centered, at most `1200×1200` (the whole image when smaller) | None | Off/on; movable supported area | Runs the preview over the area only, as Apply would over an image of that area, and shows the image as it was around it | Disabled during Apply; Apply processes the full frame | Reduces tuning work; large-scale layers near the area's edge see the area's border rather than the rest of the image | Preview/snapshot only | Window lifetime |
| **Take snapshot** | Preview action | No snapshot | None | Click or **Ctrl+Shift+K** | Runs the current settings again as a preview and opens `<target> - Multiscale Linear Transform - SNAPSHOT`; with Preview area on, the snapshot is the composite the Preview showed | Preview required; disabled during Apply | Adds processing/allocation; target unchanged; a Layer or Preview Mask mode is what is taken; no deringing maps | New still | One view per action |
| **Apply** | Dialog action | Enabled | None | One click | Captures parameters, processes the full target (still or every frame), and replaces it after success | Transform/layer/global/inspector groups, Target, Channel, Close, Reset, and the Preview pane's snapshot and area actions are disabled during the run; Preview and the preview-mode selector stay available; a second Apply is ignored while busy | Destructive; work scales with pixels, channels, layers, support, thresholds, and iterations | Still/sequence; active-target-mask-aware | Adds one summary history step; resets process settings after success |
| **Close** | Dialog action | Enabled | None | One click | Closes without starting Apply | Disabled while Apply runs | Use Cancel in the progress dialog to stop a run | Dialog | Discards UI state |
| **Reset** | Dialog action | Enabled | None | One click | Restores live defaults and schedules Preview | Disabled while Apply runs | Can trigger a full preview transform | UI/preview | Immediate; also after success; Target/Channel/Preview retained |
| **Apply progress / Cancel** | Shared progress dialog | Created on still and sequence Apply | None | Cancel | Stops the run at the engine's next checkpoint: the target is not replaced, no map opens, and no error is shown; a sequence run also removes its temporary output | Apply only | A long transform pass can finish before the stop is observed; sequences also incur temporary disk/codec cost | Still/sequence Apply | Target unchanged after Cancel |

## How the algorithm works

### Decomposition, spacing, and boundaries

Let `c_0` be one processed channel in normalized units. With detail-layer index
`l` starting at zero, AfterNight uses the scale

- `s_l = 2^l` for **Dyadic**, or
- `s_l = 1 + lq` for **Linear**, where `q` is **Linear Scale**.

The Starlet Transform smooths the current coarse layer with the selected
separable kernel, its taps spread `s_l` pixels apart, then stores
`w_l = c_l - c_(l+1)`. The Multiscale Linear Transform instead smooths the
original `c_0` at each scale with a Gaussian of `sigma = s_l/3`, truncated at
four standard deviations (radius `ceil(4 sigma)`), and stores the difference
between consecutive smoothings, the first
detail being `c_0` minus the finest smoothing. In both cases the last smoothing
is the **Residual**, and the details plus the residual add back to the input to
single-precision rounding when every weight is 1.

Starlet kernels are normalized: Linear Interpolation `[1,2,1]/4`, B3 Spline
`[1,4,6,4,1]/16`, odd Gaussian supports 5/7/9/11 with `sigma = radius/3`, and
Small Scale `n` uses `[1,n,1]/(n+2)`. Every smoothing repeats the nearest edge
sample beyond the frame. Neither algorithm is decimated: every layer retains
full image geometry.

The transform refuses a depth it cannot build, rather than quietly using a
narrower filter: a Starlet layer whose taps would be more than 512 pixels apart,
or a Multiscale Linear Transform layer whose Gaussian radius would exceed 128
pixels. Preview and Apply then show an error saying that the deepest layer
needs a tap spacing or a Gaussian radius above the stated maximum, and to ask
for fewer layers or a smaller linear step. With Dyadic spacing this allows at
most 10 Starlet and 7 Multiscale Linear Transform detail layers; with Linear
spacing, Starlet accepts all 11 at every Linear Scale, and the Multiscale Linear
Transform accepts `(Detail Layers − 1) × Linear Scale ≤ 95`.

### Layer enablement, bias, and reconstruction

An unchecked layer is removed (weight `0`). For an enabled layer with bias
`b <= 0`, the weight is `1+b`, so `-1` removes it and `0` preserves it.
Positive bias uses the current AfterNight gain mapping:

- Starlet Transform: `1 + 6b`;
- Multiscale Linear Transform first detail: `1 + 8b`;
- other Multiscale Linear Transform details and its residual: `1 + 6b` (the
  residual uses `1 + 8b` when there is a single detail layer).

This source behavior is stronger than the inspector tooltip's simple
`1 + bias` description. The weights apply when the layers are added back, after
thresholding and deringing. When LSTF is on, the residual's weight is applied
before the transfer and the transferred residual is added back with weight one.
The output is the sum of the weighted details and the residual.

### Noise reduction, k-Sigma, and the Linear Mask

Each channel's first-detail noise deviation is the robust scale
`sigma_1 = median(|w_0|)/0.67448975`. The noise of every other layer is
`sigma_1` times that layer's share of white noise relative to the first layer's.
A layer's share is the norm of its response to a single bright pixel, measured
once per run by decomposing an impulse at the center of a square of 512 to 1024
pixels with the same transform. It is deterministic and the same on every
build. If the first detail's median is zero, each
layer's deviation is measured on its own coefficients.

Selected-layer NR converts the displayed threshold `t` to `0.05t`, multiplies
it by the larger of `sigma_1` and the layer's own deviation, soft-shrinks the
coefficients, and blends by Amount. Iterations repeat that shrink/blend on the
changed coefficient; they do not re-decompose the image.

Global k-Sigma follows per-layer NR. For each detail and threshold
`T = k sigma_l`, Soft computes `sign(w) max(|w|-T,0)`, while hard thresholding
sets `|w| < T` to zero (a coefficient exactly at `T` survives), and the result
is blended by k-Sigma Amount. **Use MRS** re-estimates `sigma_1` three times
from the first-detail coefficients whose magnitude is at most `k sigma_1`. It
refines that noise estimate only; it does not build a persistent
multiresolution-support map.

When enabled, Linear Mask starts from the processed channel:
`m = clamp(Ax,0,1)`, optionally replaces it by `1-m`, smooths it with an exact
Gaussian of the chosen sigma, and clamps again. Each shrinkage then blends by
`Amount × m` pixel by pixel. This mask affects both per-layer NR and k-Sigma,
but not layer bias, deringing, LSTF, DRE, or the final target-mask blend. If
neither thresholding mode is active, it is not built and changes no normal
output; **Preview Mask** can still display it.

### Deringing, residual stretch, and range mapping

Deringing builds a per-channel edge weight from the channel itself: the larger
of the absolute horizontal and vertical central differences, the edge sample
repeated beyond the frame. It is normalized by its 90th percentile, or by its
maximum when that percentile is below `0.001`, and clamped to `[0,1]`; an image
whose maximum edge is also below `0.001` has no edge to speak of, and deringing
then changes next to nothing. After thresholding, each detail coefficient `w`
becomes `w + D e max(0,-w) - B e max(0,w)`, where `D` is **Dark**, `B` is
**Bright**, and `e` the edge weight. The optional maps add up these
nonnegative layer-stage corrections over every detail layer. After
reconstruction, a second edge-weighted pass moves the result `y` toward the
original `x`: `y + D e max(0,x-y) - B e max(0,y-x)`. That second pass is not
accumulated in the map views.

LSTF changes the residual after its weight `c`, with `v = clamp(c R,0,1)`:

- Hyperbolic: `f(v) = v/(v+b)` for Breakpoint `b`;
- Log10: `f(v) = log10(1+9v)`;
- Natural Log: `f(v) = ln(1+(e-1)v)`.

Hyperbolic does not preserve white: `f(1)=1/(1+b)`. Log10 and natural log
brighten normalized midtones; all three make the pipeline nonlinear.

After reconstruction and post-deringing, DRE maps

`y = clamp((x + Low Range)/(1 + Low Range + High Range), 0, 1)`.

With both controls at zero the result is still clamped to `[0,1]`: without
Deringing or DRE the reconstruction itself is clamped, so every result lies in
`[0,1]`.

### Channel, precision, preview, and masks

The run works on the channel or color component the selector names. **RGB**
processes each stored channel independently. A selected Red/Green/Blue or
derived CIE/HSL/HSV/YCbCr component is extracted, transformed, and recomposed
with the other components; derived-component conversion can introduce rounding
and out-of-gamut clipping.

Samples are read in normalized units: an unsigned integer divided by its
largest code, a signed integer counted from its smallest code to its largest,
and a floating sample as it is. Integer images are converted into a floating
working copy (Float64 for 32-bit integers) and the result is narrowed back to
the original representation, rounding to the nearest code. The transform and
its layer arithmetic work in single precision on every representation.
Floating values outside `[0,1]` do not survive, because the result is clamped.

A sample holding NaN or an infinity does not stop the run: the transform counts
it as zero for its neighbors while it decomposes the image, and the result
leaves that sample as it was. Samples that the image's own validity marks as not
valid are treated the same way.

Processed preview runs what Apply runs, over the target or the Preview area;
inside the area the Preview is what Apply makes of an image of that area. Layer
modes instead decompose the unprocessed source and display the selected row's
coefficient `w` (the residual can be selected too) times `k = 5 × Bias`:
**All Changes** shows `clamp(0.5 + kw)`, **Increasing Pixels**
`clamp(k max(0,w))`, and **Decreasing Pixels** `clamp(k max(0,-w))`. The layer
visualization ignores Enabled, NR, k-Sigma, Linear Mask, deringing, LSTF, and
DRE. At Bias `0`, All Changes is therefore mid-gray and the sign-only modes are
black before shared preview blending; a negative Bias inverts All Changes and
leaves the sign-only modes black.

After any process-local preview result is produced—including a layer mode or
**Preview Mask**—the shared framework applies global Amount and the active
target mask. Amount `0` therefore shows the original source rather than the raw
diagnostic, and protected target-mask pixels also show the original. Use Amount
`1` and disable the active target mask when an unmixed layer or Linear Mask
diagnostic is required.

The target's enabled active mask blends original and result after the process,
together with global Amount. Its inversion/weight is independent of the Linear
Mask inside the noise-threshold stages. Each engine operation observes the run's
stop request; a stopped run publishes nothing.

## Parameter interactions and starting points

- Locate scale before applying gain: temporarily disable one layer at a time or
  inspect its signed preview, then return to Processed Image.
- Positive Bias is deliberately strong. Start near zero and watch both sides
  of high-contrast edges; use global Amount to reduce the combined result.
- Per-layer NR is for different thresholds by scale. k-Sigma is an additional
  pass across every detail, so enabling both compounds suppression.
- Linear Mask only weights threshold Amount. Use the target's active mask when
  bias, deringing, residual stretch, DRE, or the whole final result needs spatial
  protection.
- Deringing maps show the layer-stage correction, not every change responsible
  for the final deringed image.
- Set global Amount to `1` and disable the active target mask when inspecting a
  raw Layer or Preview Mask diagnostic; both shared blends run after the
  process-local preview mode.
- Apply LSTF to the residual before DRE. DRE then remaps and clips the entire
  reconstruction, including altered details.
- More depth needs Linear spacing: Dyadic stops at 7 Multiscale Linear Transform
  or 10 Starlet detail layers, and a larger Linear Scale lowers the Multiscale
  Linear Transform's limit.

## Inputs, outputs, and metadata

A still result keeps source geometry, channel count, representation, and all
of its metadata: every FITS card, the astrometric solution, the acquisition
record, and the image's stated meaning. It is not recorded as nonlinear, even
when LSTF, DRE, clipping, or derived-channel recomposition made it so. The
image's lineage gains an entry naming `an::reconstructImageMultiscale` with the
history description.

A sequence is processed frame by frame into a temporary replacement, each frame
as Apply processes a still image of it; configured CFA auto-debayering may
supply RGB frames through the shared sequence path. The same captured settings
are used for every frame. Apply shows a progress dialog with Cancel for both
stills and sequences. Cancel prevents replacement; a sequence Cancel also
removes the temporary output.

On a still Apply only, requested deringing maps open beside the target once the
result has landed, named from the target plus ` - Deringing Dark Map` or
` - Deringing Bright Map`. On a color target whose Channel is not RGB, the
channel follows in parentheses, for example `M42 - Deringing Dark Map (Red)`.
Each map is a Float32 image with one channel per processed channel or
component, and its processing-state cards record it as nonlinear, set by a
process, under `multiscale-deringing-dark-map` or
`multiscale-deringing-bright-map`. The maps keep the target's acquisition
record, solution and lineage but none of its other cards, and their history
starts with
`Multiscale Linear Transform - Deringing Dark Map` or `... Bright Map`. They
are not created for Preview, snapshots, or sequence frames.

Image History records one step named **Multiscale Linear Transform**, for
example `Algorithm: Multiscale Linear Transform, Layers: 3, Spacing: Dyadic in
channel: RGB`. It summarizes algorithm, layer count, spacing (`Linear (scale
q)` for Linear), Starlet scaling function, the values of each enabled optional
group, DRE, and, on a color target, the channel. It omits individual layer
enable/bias/NR values and global Amount, so it is not a complete recipe.

Settings live only for the process window. **Reset** restores runtime defaults;
a successful Apply also resets process settings while retaining Target,
Channel, and the Preview checkbox. There is no process preset/file persistence
in this dialog.

## Performance

Runtime grows approximately with pixels × processed channels × layers ×
effective kernel work. Starlet support is the selected kernel spread by the
layer's scale, but its tap count stays fixed. The Multiscale Linear Transform's
Gaussian radius grows with the scale up to the 128-pixel limit. Per-layer
iterations, MRS estimation, k-Sigma, mask smoothing, deringing, and
derived-color conversion add passes; thresholding also decomposes one impulse
image of 512 to 1024 pixels a side per run. The engine runs on the threads the
run is granted and observes a stop request within its passes, not at every
sample.

A decomposition holds every layer at once: `width × height × processed
channels × (Detail Layers + 1) × 4` bytes. A run adds a single-precision copy
of the processed samples, three single-channel scratch planes, a
single-precision result, and, for an integer image, a floating working copy.
Increasing layers costs more memory even if some rows are disabled, because
decomposition occurs before reconstruction choices. Preview holds additional
source/result/display buffers, and a Preview area reduces the work to the area.
Sequence Apply also uses temporary encoded storage. A run the session's memory
budget cannot hold is refused with an error naming the memory budget, and
nothing is published. No production timing or peak-memory benchmark was run for
this page.

## Worked example

- Evidence mode: source/equation-derived; no astronomical data was run.
- Start with a normalized constant grayscale field `c_0 = 0.25`, either
  algorithm, any supported spacing, every layer enabled, Bias `0`, and all
  optional groups off. Every detail is zero because each kernel sums to one and
  the repeated edge keeps the field constant at the borders, while the residual
  remains `0.25`; reconstruction is `0.25`. The engine's decomposition is a
  partition whose layers add back to the input to single-precision rounding.
- Disable only the residual. With all zero details, Processed Image becomes
  zero before global Amount/active-mask blending. At global Amount `0.5`, no
  active target mask, the final checkpoint is `0.125` because the shared blend
  mixes the original and zero reconstruction equally.
- On real linear data, use this same reasoning as a diagnostic: if disabling a
  layer changes structures far from the expected scale, reconsider Algorithm,
  spacing, and support before adding Bias or thresholding. This example does
  not establish visual quality on astronomical images.

## Warnings and limitations

- Apply replaces the still or sequence. Work on a duplicate and verify Target.
- Positive Bias is mapped much more strongly than the current tooltip implies.
- LSTF, DRE, clipping, and many derived-component recompositions are nonlinear;
  the main output keeps the source's metadata and is not recorded as nonlinear.
- Every result is clamped to `[0,1]`; floating values outside that range do not
  survive Apply.
- A depth the transform cannot build is refused with an error, not narrowed.
  The dialog offers eleven detail layers, but Dyadic spacing stops at 7
  (Multiscale Linear Transform) or 10 (Starlet Transform).
- Disabling the residual removes the coarse base; disabling all layers produces
  zero before later mappings and blends.
- Layer previews are diagnostic coefficient displays, not previews of isolated
  production stages.
- Linear Mask is not the target's active mask and cannot protect bias,
  deringing, LSTF, or DRE.
- Deringing maps omit the post-reconstruction correction and are made only by a
  still Apply.
- Cancel is observed at the engine's next checkpoint, so the current pass may
  finish first, but the target is not replaced afterward.
- The worker captures settings for Apply, but Preview and the preview-mode
  selector are not locked with the main settings groups. They do not change the
  captured Apply parameters.
- History is incomplete for reproduction. Save settings separately.
- A run the engine refuses (a depth it cannot build, or more memory than the
  session's budget) shows the reason and leaves the target unchanged.

## Troubleshooting

| Symptom/question | Verified interpretation or cause | Resolution/next page | Evidence source |
| --- | --- | --- | --- |
| Error says the deepest layer needs a Gaussian radius or a tap spacing above the stated maximum | The Detail Layers, spacing, and Linear Scale ask for a layer deeper than the transform can build | Use fewer Detail Layers, Linear spacing, or a smaller Linear Scale | `planMultiscaleTransform` in `MultiscaleTransform.cpp`; `process_utils::multiscale::planTransform` |
| Layer preview is flat gray or black | Bias `0` gives zero visualization gain; All Changes centers zero at gray and sign-only modes clamp to black | Change Bias slightly for diagnosis, or return to Processed Image for real output | `process_utils::multiscale::previewLayerInPlace` |
| Scaling Function has no effect | The Multiscale Linear Transform builds a Gaussian from the scale and ignores the Starlet kernel selector | Choose Starlet Transform when a listed scaling function is required | `MultiscaleExecution.cpp` smoothing; UI algorithm-state update |
| Result is much stronger than the Bias number suggests | Positive bias maps to `1+6b`, or `1+8b` on the Multiscale Linear Transform's first detail | Start near zero; compare at 100%; reduce global Amount | `process_utils::multiscale::layerWeight` |
| Linear Mask changes nothing | It weights only per-layer NR/k-Sigma; neither may be enabled | Enable the intended thresholding stage, or use the target active mask for whole-result protection | `process_utils::multiscale::transformInPlace` mask/threshold order |
| Noise reduction is unexpectedly strong | Per-layer NR and k-Sigma run sequentially; Iterations repeat shrinkage; Linear Mask may be inverted | Disable one threshold stage, restore Iterations 1, inspect Preview Mask and inversion | `process_utils::multiscale::transformInPlace` |
| Reset Layer gives Amount 0.5 instead of startup 1.0 | The per-layer reset path assigns `0.5`; full Reset/live initialization assigns `1.0` | Set the intended value explicitly; use full Reset for startup defaults | `MultiscaleLinearProcess.cpp` reset paths |
| Hyperbolic white becomes darker | `f(1)=1/(1+b)`; this implementation does not normalize the endpoint back to 1 | Compare None/Log10/ln or follow with intentional DRE | `process_utils::multiscale::transformInPlace` residual transfer |
| Deringing map does not explain every final correction | Maps contain only layer-stage corrections, not the post-reconstruction pass | Treat maps as diagnostics, not an exact final-difference image | `process_utils::multiscale::transformInPlace` deringing stages |
| A deringing map is entirely black | Both maps are made on a still Apply whenever Deringing and Output Deringing Maps are enabled; a zero corresponding strength produces a zero map | Raise the relevant Dark/Bright strength if that correction is intended; do not expect maps from Preview/snapshot/sequence | `MultiscaleLinearProcess::executeTypedProcessIteration` |
| Active mask and Preview Mask disagree, or Preview Mask resembles the source | Preview Mask selects the internal intensity mask first, then global Amount and the target mask blend original pixels back into the display | Set Amount to `1`, disable the active target mask for the diagnostic, then restore the intended settings before Processed Image/Apply | `MultiscaleLinearProcess::executeTypedProcessIteration`; shared run blend in `ImageRTPreviewControl.cpp` |
| Error mentions the memory budget | The decomposition and its working planes do not fit the session's memory budget | Use fewer Detail Layers, a single channel or component, or raise the processing memory limit | `MasksWindowTest` refusal case; `MultiscaleExecution.cpp` allocation |
| Cancel takes time | Cancellation is observed between and within engine passes; the current pass may finish first | Wait for cleanup; reduce layers/support/iterations before retrying | `ProgressDialogControl.h`; shared run landing in `ImageRTPreviewControl.cpp` |
| Image History cannot reproduce the result | It omits per-layer settings and global Amount | Record the layer table and inspector values separately | `multiscale_detail::describeTransform` |

## Related workflow steps

- [Wavelet Denoise](../noise-reduction/wavelet-denoise.md) uses a different,
  decimated Haar analysis for denoising.
- [Sharpen Alpha](../sharpening/sharpen-alpha.md) offers a more focused
  multiscale detail-enhancement workflow.
- [Histogram Transformation](../stretching/histogram-transformation.md) is the
  dedicated page for deliberate display stretching after linear processing.
- [Masks](../../interface/masks.md) explains the target active mask that blends
  the complete process result.

## References and provenance

- The AfterNight process, its layer composition, the shared preview/Apply
  framework, the engine transform, and the focused tests are the authority for this page.
- Algorithm names here describe AfterNight's current implementation. They do
  not claim parameter or output compatibility with another application's tool
  of a similar name.
