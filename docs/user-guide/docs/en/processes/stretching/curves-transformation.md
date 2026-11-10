---
kind: process
stage: finish
menu: Process > Intensity Transformations > Curves Transformation
summary: >-
  Reshapes tone and color through curves you draw with control points, usually after the main stretch.
data_in: nonlinear
data_out: nonlinear
---
# Curves Transformation

```figure
image: ../../assets/images/processes/stretching/curves-transformation.webp
alt: >-
  The Curves Transformation window. On the left, the curve drawn over the image's histogram, the
  Interpolation box, Target Image, Channel and Preview. On the right, the preview of the image.
caption: >-
  Curves Transformation as it opens, here on a linear master, with a straight curve that changes
  nothing yet.
```

## Purpose and expected result

Curves Transformation is a point-controlled remapping tool for deliberate tone
and color shaping. It is intended for users who have completed calibration and,
for most workflows, the main linear-to-nonlinear stretch. You define a spline
from normalized input intensity on the horizontal axis to normalized output on
the vertical axis, then apply that spline to RGB samples or to one selected
color-space component.

The expected result is one modified still image or replacement sequence with the
same dimensions and pixel type. Channel count and pixel format are normally
preserved, except that automatic CFA debayering can turn a one-channel mosaic
sequence into RGB before the curve runs. A curve above the diagonal
brightens the affected range; a curve below it darkens that range. Component
curves can change lightness, saturation, chroma, hue, or color balance without
requiring channel extraction.

## At a glance

| Item | Current behavior |
| --- | --- |
| Menu path | **Process > Intensity Transformations > Curves Transformation** |
| Enabled when | An image or sequence is active, the same process is not already attached to it, and no attached process holds exclusive access |
| Inputs | One intended normalized grayscale or three-channel RGB still image or sequence; a one-channel CFA still is technically treated as grayscale; an optional active mask must match the target geometry |
| Outputs | Destructively replaces the target still or sequence after success; dimensions and pixel type are preserved, while automatic sequence CFA debayering can change one-channel CFA to RGB; no auxiliary image or file |
| Recommended image state | Usually nonlinear for finishing contrast, saturation, or color balance; a gentle first stretch is possible but offers no statistics-driven protection |
| Preview support | Asynchronous RT preview: while you drag a point, of what the preview view shows; when you let go, of the whole image, as Apply computes it. The graph's histogram counts the last whole-image preview |
| Sequence support | Supported; the same locked curve, interpolation, and selected component are applied to every decoded frame through a temporary replacement sequence. The sequence reader honors the view's automatic CFA debayer setting. |
| Mask support | Supported by post-transform blending; white applies the result, black keeps the source, gray blends, and **Invert Mask** reverses the weights |
| External dependencies | None; the curve is the bundled engine's native interpolation, run on its threads; no external tool, model, catalog, or network access |

## Before you start

- Finish background correction and color calibration while the image is still
  linear. Curves can make faint gradients and color casts harder to correct.
- For a primary display stretch, compare
  [Histogram Transformation](histogram-transformation.md),
  [Generalized Hyperbolic Transformation](gh-transformation.md), or
  [ArcSinh Stretch](arcsinh-stretch.md). Curves has no automatic black-point,
  median, or highlight-protection calculation.
- Duplicate the target or be prepared to use Image History. **Apply** replaces
  the current target after processing succeeds.
- Prefer floating-point data while making strong component curves. Color-space
  conversion works in a 32-bit float plane (64-bit for a Float64 image) and an
  integer image's reconstruction is rounded once to its codes, so integer
  images incur output quantization.
- Keep normalized floating samples within `[0,1]`. The transfer spline is
  defined only on that domain.
- Do not use a one-channel CFA still as a grayscale target: Curves does not
  debayer still images and would remap the mosaic samples directly. For a CFA
  sequence, decide whether the view's automatic debayer option should convert
  every decoded frame to RGB; a successful replacement then remains RGB.
- If a mask is active, remember that the curve editor is an unmasked
  diagnostic, while its histogram and clipping readout count the preview's
  mask-blended result. Judge the mask-aware result in the preview pane.

## Procedure

1. Select the target and open **Process > Intensity Transformations > Curves
   Transformation**. Confirm **Target Image** and leave **Preview** selected.
   The right pane should show the current full-image result.
2. Choose **Channel**. Start with **RGB** to apply one curve independently to
   all three stored channels, or choose a lightness/value component when you
   want to limit direct color-balance changes.
3. Leave **Interpolation: Cubic** for a smooth general-purpose curve. Choose
   **Steffen** or **Monotone Cubic** when avoiding overshoot between monotonic
   control points matters.
4. Click the existing diagonal to add an interior control point, then drag it.
   Drag upward for more output at that input range and downward for less. The
   curve and preview update together.
5. Add neighboring points to anchor ranges you want to protect. Use **Show
   guide** to compare with the identity diagonal and **Show histogram** to see
   where samples are concentrated.
6. Inspect shadows, transitions, highlights, and color at 100% zoom. Check the
   graph's **Clipping Low/High** readout as a coarse warning, then use
   [Image Statistics](../../interface/image-statistics.md) for exact checks.
7. Select **Apply**. A successful still-image run replaces the target, writes an
   Image History step containing Channel and Interpolation, then resets the
   curve to identity, selects RGB when available, and resets graph zoom. A
   sequence run reports progress, replaces the sequence only after the temporary
   output succeeds, offers **Cancel**, and performs the same UI reset. The new
   identity curve keeps the interpolation the combo shows.

## Complete parameter and action reference

| UI label/action | Type and unit | Runtime default | Persisted-default exception | Valid values, step, or constraints | Effect of increasing, decreasing, or selecting | Dependencies and enable/visibility rules | Quality, artifact, runtime, memory, or disk trade-off | Scope: preview/apply, channel, mask, still/sequence | Persistence |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Curve editor: add/select/drag point | Graph gesture, normalized x/y | Identity endpoints `(0,0)` and `(1,1)` | None | Left-click within about 5.5 screen pixels of the drawn curve to add/select; y clamps to `[0,1]`; endpoint x remains `0`/`1`; interior x stays ordered and inside `(0,1)` | Raising a point brightens its neighborhood; lowering it darkens; horizontal movement relocates the affected input range | Editor enabled while idle; Apply stays disabled for the exact two-endpoint identity | Closely spaced or extreme points can create abrupt contrast, noise emphasis, or spline overshoot | Defines the selected Channel transform for preview/apply, mask post-blend, still/sequence | Until Reset or successful Apply rebuilds the identity curve |
| Curve editor: delete point | Graph action | — | None | Right-click an interior point; endpoints cannot be deleted | Removes that constraint and rebuilds the interpolant | Must hit an existing interior point | May broaden the influence of neighboring points | Same transform scope as editor | Immediate; no undo inside the dialog |
| Curve editor: coordinate readout | Graph gesture | Hidden | None | With histogram shown, left-drag away from the curve | Shows normalized x and displayed histogram height, not an editable node value | Suppressed when selecting/dragging a curve point or when histogram is hidden | Diagnostic only; histogram height is display-normalized | Unmasked graph diagnostic | Gesture lifetime |
| Curve editor: pan | Mouse gesture | No offset | None | Middle-drag after zooming | Scrolls the enlarged editor | Useful only when content exceeds the scroll area | Display only | Graph only | Until Zoom reset, Reset, or successful Apply recenters the graph |
| Curve editor: Ctrl+wheel zoom | Mouse/wheel gesture | Fit | None | **Ctrl+wheel up/down** scales around the pointer by `1.2`/`0.8` | Enlarges or reduces the graph while keeping the pointer location anchored | Interactive zoom is always enabled | Display only | Graph only | Until Zoom reset, Reset, or successful Apply restores fit |
| Zoom in | Toolbar action | — | None | Repeated action, `1.30x` per click | Enlarges the editor | Always available while idle | Display only | Graph only | Until Reset or successful Apply restores fit |
| Zoom out | Toolbar action | — | None | Repeated action, `0.70x` per click | Reduces the editor | Always available while idle | Display only | Graph only | Until Reset or successful Apply restores fit |
| Zoom reset | Toolbar action | — | None | Single action | Fits the editor to its parent area | Always available while idle | Display only | Graph only | Not persisted |
| Show grid | Checkable toolbar action | Selected | None | On/off | Shows or hides the eight-by-eight graph grid | Always available while idle | Display only | Graph only | Window lifetime; Reset does not change it |
| Show guide | Checkable toolbar action | Selected | None | On/off | Shows or hides the identity diagonal `y=x` | Always available while idle | Display only; useful for seeing brightening/darkening | Graph only | Window lifetime; Reset does not change it |
| Draw histogram areas | Checkable toolbar action | Cleared | None | On/off | Fills displayed histogram channels instead of outlines | Disabled whenever **Show histogram** is cleared | Rendering preference only | Unmasked graph diagnostic | Window lifetime; Reset does not change it |
| Show histogram | Checkable toolbar action | Selected | None | On/off | Shows the current preview-result histogram and enables **Draw histogram areas** | Histogram is available only while Preview exists; clearing it disables area mode control but retains its checked state | Recounted after each whole-image preview; while you drag a point it keeps the last one's counts | 8-bit graph diagnostic of the mask-blended preview; display-channel selection follows the preview viewer, not process Channel | Window lifetime; Reset does not change it |
| Clipping Low / Clipping High | Read-only overlay, percent | Current preview histogram endpoints | None | Three decimals; sums endpoint counts across histogram channels | Reports the share of samples at or below `0` and at or above `1` in the image the graph's histogram counts | Visible with the curve editor; updated with its current histogram image | Counts the preview's mask-blended result, or the target while the preview shows its original; values just inside `0` or `1` are not counted | Diagnostic only; preview/still source while configuring a sequence | Recomputed, not persisted |
| Intensity gradient | Read-only graph strip | Black-to-white, or black-to selected preview-display channel | None | Normalized `0`–`1` | Shows graph direction and current preview display-channel color | Always drawn; follows the preview viewer's display composition | Display only | Graph only | Window lifetime |
| Interpolation | Combo box | **Cubic** | Reset and successful Apply keep the displayed selection and rebuild the identity curve in it; reopening shows Cubic | **Linear**, **Cubic**, **Akima**, **Steffen**, **Monotone Cubic**; two points always evaluate linearly; selected Akima or Steffen falls back to Cubic below five points | Changes how the curve passes between the same nodes | Rebuilds the spline; controls lock during Apply | Cubic/Akima can overshoot between points before final output clamp; Steffen and Monotone Cubic preserve monotonicity between monotonic nodes; Linear can show corners | Preview/apply, all selected channel modes, still/sequence | Current selection applies, and Reset or a successful Apply keeps it for the new identity curve |
| Target Image | View selector | Active view at launch | None | Any open grayscale/RGB still or sequence view | Retargets channel choices, preview, histogram, and Apply | Target must be lockable; unsupported exclusive/duplicate process state blocks launch or execution | Recalculation cost follows target size and type | Source for preview/apply and active mask | Window lifetime; Reset does not change it |
| Channel | Combo box | **RGB** for RGB; disabled **Grayscale** for mono | None | **RGB**, **Red**, **Green**, **Blue**, **CIE L* (Lightness)**, **CIE a***, **CIE b***, **CIE c***, **CIE h***, **HSL Hue**, **HSL Saturation**, **HSL Lightness**, **HSV Hue**, **HSV Saturation**, **HSV Value**, **YCbCr Luma**, **YCbCr Blue Chroma**, **YCbCr Red Chroma**; mono **Grayscale** | Selects stored channels or one converted color-space component | Component choices require a three-channel target; mono selector is disabled | Individual/color-component curves can change balance, gamut, hue, or saturation; conversions add full-image work and quantization | Preview/apply, still/sequence; active mask blends after RGB reconstruction | Resets to RGB after success/Reset; remains Grayscale for mono |
| Preview | Check box | Selected | None | On/off | Enables/disables asynchronous RT preview updates | Requires a valid target; stale preview generations are superseded | While you drag a point the preview works on what the preview view shows; the whole-image preview, its component conversions and the histogram recount follow when you let go, and can be expensive | Preview only; target remains unchanged; active mask is shown | Window lifetime; Reset does not change it |
| Preview image pane | Image display | Current transformed target | None | Shared viewer zoom/pan/display-channel behavior | Shows the mask-aware result: of what the view shows while you drag a point, of the whole image otherwise | Updated only while Preview is selected and work completes | Holds an additional result image; component modes also hold conversion buffers | Still target preview; current sequence frame while configuring a sequence | Window lifetime |
| Apply | Button/action | Disabled for exact identity curve | None | Single action after any non-identity curve | Starts destructive still/sequence execution | Target must lock; all mutable controls disable during execution | Curve evaluation is parallel; component conversions and sequence temporary output add CPU/memory/disk cost | Replaces target; mask post-blend; still/sequence | Success resets the curve to identity, selects RGB when available, and resets graph zoom; the new curve keeps the shown interpolation; graph toggles remain |
| Reset | Button/action | Enabled while idle | None | Single action | Restores an identity curve in the shown interpolation, RGB channel when available, and graph zoom | Does not reset target, Preview, the visible Interpolation selection, Grid, Guide, Histogram, or Area states | No image change | Configuration only | The visible Interpolation selection remains and the rebuilt identity curve uses it |
| Close | Button/action | Enabled while idle | None | Single action | Closes immediately when idle; an active worker follows shared deferred-close/cancellation handling | Disabled with the other controls during Apply | No output by itself | Window and active execution | No settings persist after the window is destroyed |
| Sequence progress | Progress dialog | Appears during sequence Apply | None | Frame progress | Reports temporary replacement-sequence processing | Sequence target only | Temporary output uses disk space proportional to the encoded sequence | Sequence Apply only | Run lifetime |
| Cancel | Sequence progress action | Available while sequence work runs | None | Cooperative request | Stops before replacement, closes/removes temporary output, and preserves the source sequence | Sequence progress dialog only | Current frame may finish before cancellation is observed | Sequence Apply only | Run lifetime |

## How the algorithm works

### Conceptual pipeline

1. The editor maintains ordered control points `(x_i,y_i)` on the normalized
   `[0,1]` square. Endpoints remain at `x=0` and `x=1`; all y values clamp to
   `[0,1]`.
2. After every edit, AfterNight builds a Linear, natural Cubic, Akima, Steffen,
   or Monotone Cubic interpolant through the points. With two points, it uses Linear;
   selected Akima and Steffen use Cubic until there are at least five points.
3. RT preview and Apply run one plan: an exact table over every code of an
   unsigned eight- or sixteen-bit image (each entry held in single precision, so
   a result is the nearest code to the curve within that rounding), and an
   8,193-entry table read linearly over a floating image (or a signed or 32-bit
   integer read as fractions into a Float32 copy), whose largest error against
   the curve the plan measures and reports (under one millionth for a smooth
   three-point Cubic; an overshoot
   held to `[0,1]` counts in it). The preview the window rests on once you let
   a point go is therefore the result Apply writes, and a **Preview area**
   shows exactly what Apply writes there. While you drag a point, the same
   plan runs over what the preview view shows - the image reduced to the
   view's size, each pixel the mean of its block, or the part of it shown at
   1:1 and closer - so the drag keeps up on a large image.
4. RGB/Grayscale maps stored samples directly. A component mode converts RGB to
   that color space in a 32-bit float working plane (64-bit for a Float64
   image), maps one component, and reconstructs RGB. An image described as sensor
   codes or a physical quantity, as files are read, is converted as fractions of
   full scale. The result keeps the source's cards, acquisition record, and
   solution, and is marked nonlinear.
5. The shared process framework then blends an active mask. A successful still
   swaps the result into the target; a sequence is written to a temporary
   replacement and committed only after success. Sequence readers first honor
   automatic CFA debayer, so a decoded CFA frame can already be RGB here.
6. After either successful commit, the process resets its identity spline,
   channel, and graph zoom; the identity spline keeps the interpolation the combo
   shows.

### Model and equations

Let the ordered control points be

\[
(x_0,y_0),(x_1,y_1),\ldots,(x_n,y_n),
\qquad 0=x_0 < \cdots < x_n=1.
\]

The selected interpolation constructs a continuous function `S(x)` that passes
through every point, so `S(x_i)=y_i`. The stored result for a normalized sample
`x` is

\[
y=\operatorname{clamp}_{[0,1]}(S(x)).
\]

The clamp means interpolation overshoot does not survive outside the display
range: it becomes black or white. It does not prevent a Cubic or Akima curve
from changing direction between points. The natural Cubic has zero second
derivative at the two endpoints; the engine reproduces the GNU Scientific
Library's rules for these interpolants. Steffen and Monotone Cubic are the
exposed methods that guarantee no new extrema between control points.

For **RGB**, the same scalar function is evaluated independently:

\[
(R',G',B')=(S(R),S(G),S(B)),
\]

with the clamp applied to each result. A nonlinear `S` does not generally
preserve `R:G:B` ratios, so RGB mode can change saturation even though it uses
one shared curve.

For a component mode, write `C(R,G,B)` for the conversion and `C^{-1}` for RGB
reconstruction. If component `k` is selected, AfterNight computes

\[
(c_0,c_1,c_2)=C(R,G,B),\qquad
c'_k=S(c_k),\qquad
(R',G',B')=C^{-1}(c_0,\ldots,c'_k,\ldots,c_2).
\]

The encoded components are normalized: CIE L* maps `0..100` to `0..1`, CIE a*
and b* use their signed ranges around neutral `0.5`, CIE c* maps `0..200`, and
CIE h* maps `-180..180` degrees to `0..1`. HSL/HSV hue is also normalized and
treated as an ordinary line, not as a circular angle. Strong hue curves near
the wrap boundary can therefore produce discontinuous-looking color changes.
A floating image keeps a CIE or YCbCr reconstruction outside `[0,1]` where the
inverse conversion puts it; an integer image saturates at its range. CIE
components are reconstructed through the exact inverse of the XYZ matrix, and
YCbCr through unrounded luma-chroma coefficients.

### Boundary and failure behavior

- Apply is disabled only for the exact two-point identity. Adding a point on the
  diagonal makes the spline non-identity by structure even when its output is
  visually identical.
- Control-point y values and the curve's results clamp to `[0,1]`. A floating
  input outside the curve's `[0,1]` domain takes the curve's value at the
  nearer end.
- CIE, HSL, HSV, and YCbCr conversions preserve the target pixel type. The
  component is a float plane; an integer result is rounded to the nearest code.
- There is no NaN-specific guard in the curve evaluator and no per-pixel error
  report. Use finite normalized input.
- The output is recorded as nonlinear with provenance `curves-transformation`,
  states no transfer function, and its lineage records the curve step
  (`an::applyChannelScope`) with the Channel and Interpolation text Image
  History shows, not the nodes.
- Image History records only Channel and Interpolation, not control-point
  coordinates, graph toggles, or mask settings. History cannot reconstruct the
  exact curve.
- Sequence cancellation is cooperative. A cancelled or failed run does not
  replace the source sequence and its temporary output is removed.
- A one-channel CFA still follows the Grayscale path and is remapped without
  demosaicing. A CFA sequence with automatic debayer enabled is decoded to RGB
  before execution and the successful replacement sequence is RGB.
- Reset and successful Apply rebuild the identity spline in the interpolation
  the combo shows.

## Parameter interactions and starting points

| Combination | Conservative starting point | Too weak looks like | Too strong looks like | Adjustment order |
| --- | --- | --- | --- | --- |
| Tone curve + anchor points | Add one gentle point, then one anchor on each side | Little visible separation in the intended range | Flat/steep transitions, crushed endpoints, or amplified noise | Place anchors, move the main point, inspect histogram and 100% view |
| Interpolation + node spacing | Cubic with a few well-spaced points; Steffen for monotonic safety | Curve cannot shape a narrow range | Cubic/Akima bows or oscillates between close points | Simplify nodes first, then compare Steffen/Linear |
| RGB + saturation | Use a shallow S-curve after color calibration | Contrast change is negligible | Channel ratios diverge; bright colors saturate or clip | Set tone contrast, inspect RGB clipping and star color, reduce curvature |
| Lightness/value + color preservation | Try **CIE L*** or **HSV Value** with a gentle curve | Subject remains flat | RGB reconstruction clips gamut or changes perceived saturation | Choose component, shape curve, inspect high-saturation regions |
| Saturation/chroma + hue | Use **HSL Saturation** or **CIE c*** with broad, small changes | Color remains muted | Color noise, neon saturation, gamut clipping | Adjust saturation/chroma first; use hue curves only for deliberate remapping |
| Mask + curve strength | Tune the unmasked curve gently, then judge the blended preview | Protected region changes too little | Mask edge or transition becomes obvious | Inspect mask, reduce curve strength, then soften/correct mask upstream |

## Inputs, outputs, and metadata

- One still target produces one modified still; one sequence produces one
  replacement sequence. Dimensions and pixel type do not change. Channel count
  and pixel format normally remain unchanged, but automatic sequence CFA
  debayer converts a one-channel mosaic replacement to RGB.
- Output keeps the source pixel type. An unsigned eight- or sixteen-bit channel
  curve maps every code through an exact table; a signed or 32-bit integer is
  read as fractions into a Float32 copy, curved through the 8,193-entry table,
  and rounded back to the nearest code; a component mode works on a float plane
  and rounds the reconstruction once, to the nearest code.
- The source target is replaced only after successful processing. Sequence work
  uses a temporary file and swaps it in after success.
- The source's FITS/XISF cards, acquisition record, and astrometric solution are
  kept; the output states no transfer function and its lineage records the
  curve step (`an::applyChannelScope`) with the Channel and Interpolation text,
  not the nodes. No auxiliary map, curve preset, or curve file is written.
- Image History receives **Curves Transformation** with the selected Channel and
  Interpolation. It omits spline nodes and toolbar states.
- The processing state is nonlinear with provenance `curves-transformation`.

## Performance

Curve evaluation is linear in the number of image samples and runs on the
engine's threads, the preview through the same plan as Apply. While you drag
a point the preview evaluates only what the preview view shows, and the 8-bit
histogram, when it is shown, is recounted from the whole-image preview that
follows when you let go.

RGB/Grayscale and individual stored-channel modes need the least auxiliary
work. CIE, HSL, HSV, and YCbCr modes allocate a full converted image and perform
forward and reverse conversion. Sequences add temporary output proportional to
the encoded sequence size. Disable **Preview** while placing many points on a
large target, or prototype on a duplicate crop; graph zoom itself does not
change processing cost or precision.

## Worked example

- **Input condition/provenance and settings:** A color-calibrated nonlinear RGB
  image. Select **Channel: CIE L* (Lightness)** and **Interpolation: Steffen**.
  Keep endpoints `(0,0)`/`(1,1)`, add a broad midtone point slightly above the
  diagonal, and add low/high anchors on the diagonal.
- **Expected checkpoint:** Source logic maps only encoded L* through the spline
  and reconstructs RGB with the original a*/b* components; a floating image
  keeps an out-of-gamut value outside `[0,1]`, and an integer image saturates at
  its range. The curve should remain monotonic when the nodes are monotonic.
- **Inspect at 100%/statistics/history:** Check dark noise, star cores, saturated
  colors, mask transitions, clipping statistics, and the Channel/Interpolation
  history description.
- **Limitations:** No real-image contrast, color-preservation, clipping, or
  timing outcome was measured. The exact nodes are intentionally qualitative
  and are not stored in history.

## Warnings and limitations

!!! warning "Apply is destructive and the curve is not saved"
    A successful Apply replaces the target and immediately resets the spline to
    identity. Image History does not record node coordinates, so it cannot
    reproduce the executed curve after Apply, Reset, or reopening the process.

- Do not assume a shared RGB curve preserves color ratios. Nonlinear per-channel
  mapping can change saturation and push channels into the output clamp.
- CIE a*/b* and YCbCr chroma are signed concepts encoded around normalized
  neutral `0.5`; broad ordinary tone curves are usually inappropriate for them.
- Hue is circular, but the spline is not. Curves across the `0/1` hue boundary
  can create a seam-like color change.
- The histogram and coordinate readouts are 8-bit and display-normalized, and
  the histogram and clipping readout count the mask-blended preview. The
  histogram places a whole code on a bin boundary in its own bin, by exact
  interval arithmetic. They are diagnostic, not a full-precision output
  measurement.
- Preview and Apply run one plan: an exact table over every code of an
  unsigned eight- or sixteen-bit image and an 8,193-entry linear table over a
  floating one, so the preview the window rests on after you let a point go
  is the result Apply writes.
- The output's processing state is recorded as nonlinear
  (`curves-transformation`); treat any non-identity curve result as nonlinear in
  workflow decisions.
- There is no node list, numeric point editor, preset, save/load, or undo within
  the process window.
- Successful Apply resets the curve, channel, and graph zoom. Reset does the
  same without changing the image. In both cases the rebuilt identity curve
  keeps the interpolation the combo shows.
- A one-channel CFA still is processed as grayscale, not debayered. A CFA
  sequence becomes RGB when its automatic debayer option is enabled.

## Troubleshooting

| Symptom | Cause | Fix |
| --- | --- | --- |
| **Apply** is disabled | The curve is the exact two-endpoint identity | Add or move a point; use Reset when no change is intended |
| Selected Akima/Steffen looks like another spline | The current implementation substitutes Cubic below five points; any two-point curve is Linear | Add enough well-spaced points or choose the effective method explicitly |
| Curve overshoots or flattens against black/white | Cubic/Akima interpolation crossed `[0,1]`, then output clamped | Remove close points, reduce curvature, or use Steffen, Monotone Cubic, or Linear |
| Saturated colors shift after an RGB curve | The same nonlinear function changes each channel value independently, not by a common ratio | Use a gentler curve or compare a lightness/value component |
| Hue changes abruptly around one color | Hue is encoded on a linear `0..1` axis even though physical hue wraps | Avoid crossing the wrap boundary; prefer a localized color tool |
| Preview and Apply differ | Preview and Apply run one curve plan, so a difference comes from a changed mask, target, or display setting, a preview still on its way, or a point not yet let go, whose preview shows the image reduced to the view | Compare identical mask/display state, wait for the current preview, and inspect statistics after Apply |
| History cannot reproduce the result | The step description stores Channel and Interpolation only | Record node values externally or preserve a duplicate result |
| Sequence remains unchanged after Cancel | Cancellation intentionally removes temporary output before replacement | Retry with final settings; keep enough temporary disk space |
| The next curve is identity/RGB after Apply | Successful still and sequence commits call the process reset path | This is expected; rebuild the curve and reselect the intended channel |
| A CFA sequence becomes RGB after Apply | Automatic sequence CFA debayer was enabled, so the reader decoded each mosaic frame to RGB before Curves ran | Disable automatic sequence debayer when preserving a one-channel CFA representation is required; do not stretch CFA mosaics as grayscale |

## Related workflow steps

Complete [Background Neutralization](../color-calibration/background-neutralization.md)
and [Photometric Color Calibration](../color-calibration/photometric-cc.md) or
[Spectrophotometric Color Calibration](../color-calibration/spectrophotometric-cc.md)
before nonlinear work. Use [Histogram Transformation](histogram-transformation.md),
[Generalized Hyperbolic Transformation](gh-transformation.md), or
[ArcSinh Stretch](arcsinh-stretch.md) for the main stretch, then use Curves for
intentional tone/color finishing. Continue with
[Color Saturation](../color-adjustment/color-saturation.md), targeted noise
reduction, and sharpening while monitoring [Image History](../../interface/image-history.md)
and [Image Statistics](../../interface/image-statistics.md).

## Credits

- [GNU Scientific Library 2.8: Interpolation](https://www.gnu.org/software/gsl/doc/html/interp.html) — the definitions of the Linear, natural Cubic, Akima, and monotonic Steffen interpolants and their point requirements. AfterNight implements the same rules itself and does not use the library.
