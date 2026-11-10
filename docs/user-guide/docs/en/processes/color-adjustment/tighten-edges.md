---
kind: process
stage: finish
menu: Process > Morphology > Tighten Edges
summary: >-
  Darkens samples in proportion to their local edge strength, hardening the edges of bright structures.
data_in: either
data_out: either
---
# Tighten Edges

## Purpose and expected result

Tighten Edges darkens samples in proportion to a fixed local edge magnitude,
then blends that filtered image with the source. On bright structures against a
dark field this can contract or harden the bright-side transition. It is not a
deconvolution, an unsharp mask, or a selectable-scale morphology operation.

Use it sparingly on a duplicate, star layer, or mask whose edges actually need
contraction. The operation is mathematically nonlinear because it uses absolute
gradient responses and clamps, even though the process accepts both linear and
nonlinear images and does not change their recorded linearity state.

## At a glance

| Item | Current behavior |
| --- | --- |
| Menu path | **Process > Morphology > Tighten Edges** |
| Enabled when | An image or sequence is active, the same process is not already attached, and no attached process holds exclusive access |
| Inputs | One grayscale or RGB still/sequence; RGB can use stored or derived component channels; intended finite normalized samples; optional enabled active mask |
| Outputs | Replaces the target at the same geometry and pixel type; RGB/component reconstruction follows the selected Channel; a successful sequence uses a temporary replacement sequence; Take snapshot creates an auxiliary still |
| Recommended image state | Linear or nonlinear is accepted; visually easier to tune after a display stretch or on a dedicated mask/star layer; recorded processing state is preserved rather than explicitly changed |
| Preview support | Asynchronous full-image Preview with Linear/Autostretch display, original/result switch, selectable Preview area, and snapshot |
| Sequence support | Generic temporary replacement-sequence path with progress and cooperative cancellation; configured CFA auto-debayer can supply RGB frames |
| Mask support | Active target mask post-blends source and the Amount-blended process result; white applies, black protects, gray blends, and Invert Mask reverses weights |
| External dependencies | None; the AfterNight engine's own four-direction edge-tightening filter |

## Before you start

Reduce noise before tightening edges: the filter responds to every local
gradient, including grain, ringing, hot pixels, mask stair steps, and compression
artifacts. Inspect at 100% and include both a representative transition and
smooth background.

Choose the process **Channel** before tuning. **RGB** filters stored R, G, and B
independently and can produce colored rims. A luminance/lightness component can
reduce that risk, but component conversion/reconstruction can change colour,
and an integer image's reconstruction clips at its type's range (a floating
image keeps values outside `[0, 1]`).

## Procedure

1. Select a duplicate, mask, or star layer and open **Process > Morphology >
   Tighten Edges**.
2. Choose **Channel**. For RGB color data, compare **RGB** with **CIE L*
   (Lightness)** or **CIE Y (Luminance)** rather than assuming they are
   equivalent.
3. Set **Value** to `0.10..0.25` and keep **Amount** below `1.00` while finding a
   useful direction. This is conservative guidance, not a measured optimum.
4. Inspect bright and dark sides of edges, small stars, noise, all four frame
   borders, and any active-mask boundary. Use 100% zoom.
5. If using **Preview area**, ignore its one-pixel perimeter when judging the
   full result; cropping creates different edge neighbors there.
6. Select **Apply** and recheck the committed target, metadata/state, and Image
   History before continuing.

## Complete parameter and action reference

| UI label/action | Type and unit | Runtime default | Persisted-default exception | Valid values, step, or constraints | Effect of increasing, decreasing, or selecting | Dependencies and enable/visibility rules | Quality, artifact, runtime, memory, or disk trade-off | Scope: preview/apply, channel, mask, still/sequence | Persistence |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Value | Double value plus slider, edge-subtraction scale | `0.00` | None | `0.00..2.00`; displayed spin step `0.50` with adaptive decimal stepping; slider has 2001 positions | Multiplies the modified Sobel magnitude before subtraction; `0` is identity for finite inputs within full scale | Apply disabled only at exactly `0`; control disables during Apply | Larger values darken/clip more edge samples and amplify border, ringing, noise, and color artifacts; fixed kernel cost is unchanged | Preview/Apply; selected channel/component; before Amount and mask; still/sequence | Captured per execution; Reset/success returns `0.00` |
| Amount | Double value plus slider, blend fraction | `1.00` | None | `0.00..1.00`; displayed spin step `0.50` with adaptive decimal stepping; slider has 1001 positions | Blends source to the already filtered/clamped result: `0` source, `1` full filter | Apply gate ignores Amount; with Value nonzero, Amount `0` permits an identity-looking Apply | Reduces visible effect without changing edge-map work; cannot undo clipping inside the filtered branch, only mix source back | Preview/Apply; after selected-channel reconstruction and before active mask; still/sequence | Captured per execution; Reset/success returns `1.00` |
| Target Image | Image selector | Attached/active target | None | Open eligible still/sequence | Changes source, Channel list, Preview, active mask, and Apply destination | Active image required; duplicate/exclusive launch guards apply | Wrong target is destructively replaced after success | Preview/Apply source and active mask | Target lifetime; Reset does not change it |
| Channel | Combo box | **RGB** for RGB; disabled **Grayscale** for mono | None | **RGB**, **Red**, **Green**, **Blue**, **CIE L* (Lightness)**, **CIE a***, **CIE b***, **CIE c***, **CIE h***, **CIE X**, **CIE Y (Luminance)**, **CIE Z**, **HSL Hue**, **HSL Saturation**, **HSL Lightness**, **HSV Hue**, **HSV Saturation**, **HSV Value**, **YCbCr Luma**, **YCbCr Blue Chroma**, **YCbCr Red Chroma** | RGB filters three stored planes independently; a component filters one converted plane and reconstructs/copies the others | Component choices require RGB; mono selector disabled; disabled during Apply; the selection is captured when Apply starts | RGB can make color rims; component round trips add buffers/quantization/gamut effects; hue planes are treated as ordinary noncircular scalars | Preview/Apply; active mask post-blend; still/sequence | Dialog-local; Reset/success does not restore RGB |
| Preview | Checkbox | Checked | None | Checked/unchecked | Enables/disables asynchronous full-image or selected-area execution | Remains interactive during Apply | Full filter, float buffers, optional component conversion, and display autostretch add work | Preview only; mask-aware; still/current displayed sequence frame | Window lifetime; Reset does not change it |
| Preview pane | Image viewer | Filtered result | None | Pan with scroll bars or dragging; toolbar actions below | Shows mask-aware result without modifying the target | Exists only with Preview | Holds source/result/display buffers in addition to filter buffers | Preview only | Window lifetime |
| Fit To Window | Preview-toolbar action | Preview fitted when first displayed | None | Click or **Ctrl+Shift+F** | Fits the whole image uniformly in the viewport | Preview image required | Resampling can hide one-pixel rims and noise | Preview display only | Viewer lifetime |
| Zoom In | Preview-toolbar action | Current scale | None | Click; `1.2×` relative zoom within viewer limits | Enlarges around viewport center | Preview image required | Rendering only | Preview display only | Viewer lifetime |
| Zoom Out | Preview-toolbar action | Current scale | None | Click; `0.8×` relative zoom within viewer limits | Reduces around viewport center | Preview image required | Shows more context but hides fine artifacts | Preview display only | Viewer lifetime |
| Zoom 100% | Preview-toolbar action | Current scale | None | Click; absolute scale `1.0` | Shows the viewer's one-pixel scale | Preview image required | Required for one-pixel edge/border artifacts | Preview display only | Viewer lifetime |
| View Channel | Preview-toolbar combo box | **RGB** for RGB; disabled **Grayscale** for mono | None | Stored and derived RGB/CIE/HSL/HSV/HSI/YCbCr display components | Selects only the displayed component | Separate from process Channel; RGB target required for components | Helps diagnose colored rims without changing pixels | Preview display only | Viewer lifetime; not in process history |
| Display Mode | Preview-toolbar combo box | Inherits target **Linear** or **Autostretch** when Preview is created | None | **Linear**, **Autostretch** | Changes display only; Autostretch uses prepared target/result statistics | Autostretch enables settings action | Strong stretch can exaggerate noise and rims | Preview display only | Preview-view state; not in process history |
| Autostretch Settings | Preview-toolbar dialog action | Disabled in Linear mode; inherits target-view settings | Shared-view values load when opened | Opens Preview autostretch controls; **Apply Stretch** is hidden | Tunes Preview display only | Autostretch mode only; disabled while its dialog is open | Changes visual judgment, never filter samples | Preview display only | Viewer-scoped; not a Tighten Edges parameter |
| Switch image | Preview-toolbar action | Filtered Preview shown | None | Click, **Space**, right-click, or **Ctrl+Shift+O** | Toggles result/original | Preview required | Comparison only; no recomputation | Preview display; still/current frame | Preview-window lifetime |
| Preview area | Preview-toolbar checkbox | Unchecked; first enablement centers a bounded `600×600` area or the full image when smaller | None | Checked/unchecked; move/resize overlay | Processes only the crop and composites it over the original Preview; Apply remains full-frame | Preview required | Faster tuning, but crop borders use zero neighbors and differ from full-image interior; inspect outside its one-pixel perimeter | Preview and Take snapshot; mask cropped consistently | Preview-window lifetime; Reset unchanged |
| Take snapshot | Preview-toolbar action | No snapshot | None | Click or **Ctrl+Shift+K** | Re-executes settings and opens `<target> - Tighten Edges - SNAPSHOT` | Preview required | Adds a complete execution and auxiliary image; Preview-area perimeter behavior is baked into the snapshot | New still; target unchanged; area/mask honored | Each action creates a separate image view |
| Apply | Button/action | Disabled while Value is `0`; enabled otherwise, even when Amount is `0` | None | One action | Captures Value/Amount, locks target, then filters the full still or every sequence frame | Apply visually stays enabled during work when Value is nonzero, although repeat clicks are ignored; Target, Channel, Value, Amount, Close, and Reset are disabled during Apply | Full float edge/filter buffers plus component conversion, blend, mask, and sequence work | Destructive still/sequence replacement after success | Records Value/Amount and, for a colour image, Channel; success resets Value/Amount only |
| Reset | Button/action | Enabled while idle | None | One action | Restores Value `0.00` and Amount `1.00` | Does not change Target, Channel, Preview, mask, or Preview toolbar; disabled during Apply | Checked Preview schedules another full execution | Configuration only | Immediate; also invoked after successful Apply |
| Close | Button/action | Enabled while idle | None | One action | Closes without applying current settings | Disabled during Apply | A preview being computed is cancelled | Window/execution | Process settings do not persist after destruction |
| Sequence progress / Cancel | Shared per-frame progress and Cancel | Created for sequence Apply | None | Cooperative Cancel; frame/task text updates around temporary processing | Sequence target only; automatic CFA debayer can produce RGB frames | The filter observes Cancel between rows, so a frame stops promptly | Temporary encode/disk cost; Value, Amount, and Channel are captured once for every frame | Cancel removes temporary output; success replaces sequence, records one history step, and resets Value/Amount |

## How the algorithm works

### Conceptual pipeline

1. The engine reads the selected stored image or component in its stored units,
   in single precision; double data is narrowed to float32.
2. For every plane, compute four signed 3×3 directional responses: horizontal,
   vertical, and two diagonals. Sum their absolute magnitudes and multiply by
   `0.05`.
3. Subtract **Value** times that nonnegative edge map from the source, then clamp
   the filtered branch to the image's full scale (`0..1` for float, the type's
   range for an integer image) and write it back in the target pixel type.
4. Reconstruct RGB for a selected component, blend source and filtered result by
   **Amount**, then post-blend the active mask.

### Model and equations

For one plane `S`, written as fractions of full scale, and the four fixed
derivative kernels `K_j`, the
edge map is

\[
E(x,y)=0.05\sum_{j=1}^{4}\left|K_j*S\right|(x,y).
\]

There is one fixed 3×3 neighborhood: no radius, iteration, pyramid, or scale
selector exists. With Value `v` and Amount `a`,

\[
T(x,y)=\operatorname{clamp}(S(x,y)-vE(x,y),0,1),
\]

\[
O(x,y)=(1-a)S(x,y)+aT(x,y).
\]

Where `T` has not clipped, the net subtraction is approximately `avE`; after
`T` reaches zero, lowering Amount mixes source back but does not change the
clipped filtered branch. The active mask is a second blend after `O`.

The filter only subtracts a nonnegative edge magnitude. It does not add a
high-pass residual or solve a point-spread function. Bright-side transitions can
contract; dark-side pixels remain at the lower clamp rather than being sharpened
symmetrically.

### Boundary and failure behavior

Every neighbour outside the image is a constant zero, on all four sides. The
zero border can create dark/colored frame rims. A Preview crop creates new
boundaries, so its one-pixel perimeter is not Apply-equivalent.

The engine does not validate finite floating samples. Its clamp maps finite
output below the range to its bottom and above it to its top; NaN is not
sanitized. The filter observes Cancel between rows.

## Parameter interactions and starting points

| Combination | Conservative starting point | Too weak looks like | Too strong looks like | Adjustment order |
| --- | --- | --- | --- | --- |
| Value × Amount | Value `0.10..0.25`, Amount `0.25..0.50` | Edge width/brightness barely changes | Dark rims, clipped bright-side detail, crunchy noise | Set modest Value, then blend with Amount |
| RGB × luminance component | Compare RGB with **CIE Y (Luminance)** or **CIE L* (Lightness)** | Luminance-only result may leave chromatic edges | RGB produces unequal channel rims or colored borders | Choose Channel first, then tune Value/Amount |
| Fixed scale × sampling | Inspect at 100%; resize/output scale matters | Coarse/soft structures are unaffected | Fine noise and single-pixel stars are eroded | Judge at final sampling; use another tool for broader scales |
| Preview area × borders | Ignore one-pixel crop perimeter | Interior change seems subtle | Crop seam/rim is mistaken for full-image behavior | Tune interior, then verify full Preview and frame borders |
| Amount × active mask | Start with a soft verified mask and modest Amount | Protected edge changes too little | Filter and mask boundaries combine visibly | Tune unmasked direction, then enable/inspect mask |

## Inputs, outputs, and metadata

- Output dimensions and target pixel type match the input. RGB processing
  returns RGB; grayscale remains grayscale. Component conversion reconstructs
  RGB through the shared Channel dispatcher.
- The result keeps the source image's header cards, metadata, and existing
  processing state for every Channel choice; Tighten Edges adds no explicit
  linear/nonlinear provenance marker.
- The target view is replaced in place. Non-float/component temporary paths do
  not explicitly guarantee the image-internal name/file-name, although the view
  identity remains.
- Image History records **Tighten Edges**, Value, and Amount, and for a colour
  image ` in channel: <channel>` with the selected Channel, derived components
  included.
- Sequence Apply writes a temporary sequence and replaces the target only after
  success. Value, Amount, and Channel are captured once for every frame.

## Performance

Cost is linear in pixel count with a fixed four-response 3×3 neighborhood, run
on the threads the operation is granted. RGB processes three planes. The filter
holds one float32 plane of accumulated edge magnitudes beside its working
planes; double data is narrowed for computation. Amount adds a full-image blend and a
mask adds another. Derived components add conversion and reconstruction buffers.
Sequences add decode, encode, and temporary-disk work per frame.

Preview area reduces pixel work but introduces crop-border differences. No
production timing or peak memory was measured.

## Worked example

- **Identity checkpoint:** Value `0` gives `T=S` for normalized finite inputs;
  Apply is disabled. With Value nonzero and Amount `0`, `O=S`, but Apply remains
  enabled and still records a process step.
- **Bright-side checkpoint:** for `S=0.8`, local edge magnitude `E=0.2`, Value
  `0.5`, and Amount `1`, `T=O=0.7`.
- **Blend checkpoint:** with the same `S`, `E`, and Value but Amount `0.25`,
  `O=0.775`.
- **Clip checkpoint:** if `vE>S`, the filtered branch is `0`; Amount `0.25`
  returns `0.75S`, not a negative sample.
- **Inspect in real use:** both sides of transitions, noise, small stars, channel
  rims, all frame borders, Preview-area perimeter, mask boundary, and metadata.
- **Limitations:** These equations verify the implementation order, not halo
  reduction quality, a saved sequence, runtime, or memory use.

## Warnings and limitations

!!! warning "Inspect every frame border"
    Neighbours outside the image count as zero on every side. Strong Value
    settings can create dark or colored border rims.

!!! note "Preview-area perimeter is not Apply-equivalent"
    Cropping creates new zero neighbors. Judge the Preview-area
    interior, then switch to full Preview before Apply.

- Tighten Edges has one fixed 3×3 scale. **Value** is strength, not radius or
  number of scales; **Amount** is only a final source/result blend.
- The transform subtracts edge magnitude. It can contract bright structures but
  can also erode faint detail, deepen dark rims, and amplify visible noise.
- RGB planes are independent. Derived hue components are processed as ordinary
  linear scalars even though hue is circular; avoid HSL/HSV/CIE h* unless that
  noncircular behavior is explicitly intended.
- Apply can look enabled during execution, but repeat clicks are ignored; the
  other controls are disabled until it finishes.
- The kernel computes in float32, and a derived component's reconstruction of a
  floating image is not clamped to `[0, 1]`.

## Troubleshooting

| Symptom | Cause | Fix |
| --- | --- | --- |
| Tighten Edges menu is disabled | No active image/sequence or generic process launch guard failed | Activate a target; close the same or exclusive process |
| Apply is disabled | Value is exactly `0` | Set a nonzero Value; Amount alone cannot enable Apply |
| Apply runs but result is unchanged | Amount is `0`, or selected Channel has negligible local gradients | Raise Amount or choose the intended Channel |
| Dark/colored frame rim appears | Neighbours outside the image count as zero | Reduce Value/Amount; inspect/crop final border only after processing |
| Preview-area seam differs after Apply | Crop created a different one-pixel neighborhood | Ignore crop perimeter and verify full Preview |
| RGB result shows color fringes | R, G, and B edge maps are independent | Try CIE Y/L* on a duplicate; lower Value/Amount |

## Related workflow steps

Use [Unsharp Mask](../sharpening/unsharp-mask.md) when you need a selectable
Gaussian scale and symmetric detail addition, or
[Morphological Transformation](../masking/morphological-transformation.md) for
explicit erosion/dilation/opening/closing with a chosen structuring element.
For star-layer-specific tightening inside recombination, see
[Profiled Starmask Recombination](../star-operations/profiled-starmask-recomb.md),
which applies the same engine filter before its mask/addition stage. Verify
[Image History](../../interface/image-history.md),
[Image Statistics](../../interface/image-statistics.md), and frame borders after
Apply.
