---
kind: process
stage: finish
menu: Process > Details > Dark Structure Enhance
summary: >-
  Darkens the structures that are darker than their surroundings, such as dust lanes, through a mask it builds from
  the image.
data_out: nonlinear
---
# Dark Structure Enhance

## Purpose and expected result

Dark Structure Enhance builds a grayscale mask of the places darker than their
own multiscale surroundings, applies a darkening midtones transfer through that
mask, and can open the mask as a separate image. It is most useful late in
nonlinear processing for emphasizing dust lanes and dark nebular structure. It
does not sharpen bright detail, detect stars, or perform local deconvolution.

At the defaults, the process compares the selected channel or component with a
large-scale starlet smoothing of itself, rescales and denoises the difference
into a mask, maps Amount `0.40` to midtones `0.70`, and applies one masked pass.
**Apply** replaces the selected still, or every frame of the selected sequence,
only after the whole run succeeds. On a still, **Create mask image** also opens
the mask the enhancement used as a Float32 grayscale image named
`<target> - DarkStructureEnhance Mask` beside the target.

## At a glance

| Item | Current behavior |
| --- | --- |
| Menu path | **Process > Details > Dark Structure Enhance**. The window is titled **DarkStructureEnhance** followed by the target's name in angle brackets |
| Enabled when | An image or sequence is active. Without one the launch reports an error; it is refused with a warning when this process is already attached to the image or an attached process holds the image to itself |
| Input | Selected still or every sequence frame, in any integer or floating representation; RGB/Grayscale or one selected RGB/derived component |
| Recommended state | Nonlinear, already stretched image; start on CIE L* or RGB and protect stars/noise with an active target mask if needed |
| Main output | Replaces the selected still in place, or writes every frame to a temporary sequence that replaces the selected sequence only on success; geometry, representation, and channel layout are preserved; the result is recorded nonlinear |
| Internal mask | Float32 grayscale dark-structure mask. RGB averages the three channels' measurements equally into one mask; a selected component builds it from that component alone. Generated mask Preview shows it on a color target repeated over the three channels |
| Optional mask output | Still Apply only. Preview, snapshots, and sequence Apply never publish the separate mask even when checked |
| Active target mask | Supported as a second, outer mask after Dark Structure Enhance. In Generated mask mode it also blends the mask preview with the target; disable it to inspect the internal mask alone |
| Preview | On by default with a centered `800 x 800` Preview area, clipped to the image. Processed image or Generated mask modes, display autostretch, snapshots, and original/result switching are available; the Processed image Preview is what Apply writes |
| Sequence support | Every frame is processed as Apply processes a still image of it, into a temporary sequence that replaces the selected sequence only on success; the view's auto-debayer setting and Cancel apply |
| External dependencies | None; bundled AfterNight engine only |

## Before you start

1. Work on a duplicate or ensure the target can be restored from history;
   Apply is destructive.
2. Prefer a nonlinear target. The operation applies a nonlinear midtones
   transfer and is designed to deepen already visible dark structures.
3. Disable the target's active mask before examining **Generated mask**. The
   active mask is applied outside this process and mixes the diagnostic preview
   with the target.
4. Layers to Remove runs from `7` to `10`, the deepest smoothing the engine's
   starlet transform holds.
5. Make the final check on a full-frame preview. The default area is faster,
   but its starlet border context and both extreme-based rescales differ from
   full Apply.
6. For a sequence, budget temporary disk space and note that **Create mask
   image** has no sequence output.

## Procedure

1. Activate the target and open **Process > Details > Dark Structure Enhance**.
2. Confirm **Target Image** and choose **RGB**, **CIE L* (Lightness)**, or another
   component. CIE L* is a conservative color-preserving starting point.
3. Choose **Generated mask** in Preview Mode. Temporarily disable any active
   target mask, then inspect whether bright mask values follow the intended dark
   lanes instead of background gradients, star surroundings, or noise.
4. Tune **Layers to Remove** first, between `7` and `10`, then **Scaling
   Function**. More layers move the comparison smoothing to coarser scales;
   5x5 B3 Spline is smoother and more expensive than 3x3 Linear Interpolation.
5. Return Preview Mode to **Processed image**. Start with Amount `0.40` and one
   Iteration. Inspect the dark lanes, background, bright-star surroundings, and
   color at 100%.
6. Increase Amount or Iterations one at a time. Amount changes the transfer
   curve; Iterations reapplies that curve through the same fixed mask.
7. Disable **Preview area** for the final whole-frame check. Re-enable the target
   active mask if it should constrain the committed effect.
8. Optionally check **Create mask image**, then Apply. Verify the target
   history, clipping, and, on a still, the separate mask's name and content.

## Complete parameter and action reference

| UI label/action | Type and unit | Runtime default | Persisted-default exception | Valid values, step, or constraints | Effect of increasing, decreasing, or selecting | Dependencies and enable/visibility rules | Quality, artifact, runtime, memory, or disk trade-off | Scope: preview/apply, channel, mask, still/sequence | Persistence |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Layers to Remove | Integer starlet depth | `8` | None | `7`–`10`, step `1`; `10` is the deepest smoothing the engine's starlet transform holds | Moves the comparison smoothing to coarser dyadic scales as it rises | Always affects the internal mask; does not alter the transfer curve; disabled during Apply | Higher values cost more smoothing passes but no more memory, and can broaden selection or become border-dominated on small crops | Internal mask for Preview/Apply; all selected source channels | Captured per run; Reset/success restore `8`; not kept between openings |
| Scaling Function | Starlet kernel selector | **5x5 B3 Spline** | None | **3x3 Linear Interpolation**, **5x5 B3 Spline** | Chooses `[1,2,1]/4` or `[1,4,6,4,1]/16` for the fixed six-scale and selected-depth smoothings | The mask denoise always uses 3x3 Linear regardless; disabled during Apply | B3 is smoother/wider and performs more taps; Linear can retain harder boundaries | Internal mask only | Captured; Reset/success restore B3 |
| Amount | Midtones strength | `0.40` | None | `0.00`–`0.99`; step `0.01`; maps to midtones `m=0.5+0.5 Amount` | `0` is identity; higher values darken in-mask midtones more strongly | Does not affect the generated mask although it reruns the preview; disabled during Apply | High values crush dark detail; mask cost is paid even at zero | Processed result only; selected Channel | Captured; Reset/success restore `0.40` |
| Iterations | Masked transfer pass count | `1` | None | `1`–`10`; step `1` | Reapplies the same curve to the evolving result through the same fixed mask | Does not rebuild or change the mask; disabled during Apply | Effect and transfer/blend cost grow with count; high values can posterize or crush shadows | Processed result; selected Channel | Captured; Reset/success restore `1` |
| Preview Mode | Selector | **Processed image** | None | Processed image, Generated mask | Chooses what the preview computes and shows | Enabled only while Preview is checked; disabled during Apply; Apply always enhances the target | Generated mask builds the mask only, as a Float32 grayscale image - repeated over the three channels of a color target - and skips the iterations | Preview and snapshot only | Window lifetime; Reset/success restore Processed image |
| Target Image | Image/sequence selector | Attached active view | None | Open image or sequence | Changes source, active mask, Channel list, Apply destination, and sequence target | Active target required to launch; disabled during Apply | Wrong target is destructively replaced | Preview/Apply/mask publication | Reset unchanged |
| Channel | Process-component selector | **RGB** for color; disabled **Grayscale** for grayscale | None | RGB, Red, Green, Blue, CIE L* (Lightness), CIE a*, CIE b*, CIE c*, CIE h*, CIE X, CIE Y (Luminance), CIE Z, HSL Hue/Saturation/Lightness, HSV Hue/Saturation/Value, YCbCr Luma/Blue Chroma/Red Chroma | RGB builds one mask from the equal channel average and darkens every channel through it; another choice extracts that component in the unit interval, processes it alone, and puts it back | Color target required for components; disabled during Apply | Lightness/luma reduce color shifts; hue/chroma processing can be unstable or clip on reconstruction | Preview/Apply/internal mask; still/sequence; named in the history step | Reset/success leave selection unchanged |
| Preview | Checkbox | On | None | Off/On | Creates/destroys the asynchronous preview | Preview Mode enabled only when on; disabled during Apply | Each preview run works on a copy of the target or of the Preview area; a burst of changes collapses into one run with the latest values, and Apply cancels a preview in progress | Current still/displayed frame; active-mask aware | Reset/success leave state unchanged |
| Create mask image | Checkbox | Off | None | Off/On | After a successful still Apply, opens the internal mask as `<target> - DarkStructureEnhance Mask` beside the target | Ignored for Preview, snapshots, and sequence Apply; disabled during Apply | Adds one full-size Float32 mask image; target processing is unchanged | Still Apply auxiliary output only | Captured; Reset/success turn it off |
| Preview pane | Image viewer | Selected mode; centered `800 x 800` area | Execution autostretch supported | Pan/scroll and actions below | Shows the post-active-mask result Apply would write, or the mask | Preview required | Holds source/result/display buffers and the run's working images | Preview only | Preview lifetime |
| Fit To Window | Preview action | Fitted initially | None | Click or **Ctrl+Shift+F** | Fits whole image | Preview required | Resampling can hide fine mask/noise artifacts | Display only | Viewer lifetime |
| Zoom In | Preview action | Current scale | None | Click; `1.25x` relative zoom | Enlarges detail around the viewport center | Preview required | Less spatial context | Display only | Viewer lifetime |
| Zoom Out | Preview action | Current scale | None | Click; `0.8x` relative zoom | Shows more context | Preview required | Less pixel detail | Display only | Viewer lifetime |
| Zoom 100% | Preview action | Current scale | None | Click; absolute scale `1.0` | Shows one image pixel per display pixel | Preview required | Best for noise, mask edges, clipping, and color artifacts | Display only | Viewer lifetime |
| View Channel | Preview selector | Follows the shown image's channels | None | Stored/derived RGB and color-space display components, or Grayscale | Changes display component only | Separate from process Channel | Helps reveal color shifts | Display only | Viewer lifetime |
| Display Mode | Preview selector | Follows the target view (Linear or Autostretch) | None | Linear, Autostretch | Changes visualization only | Autostretch enables settings | Stretch can conceal shadow clipping or exaggerate mask noise | Display only | Preview-view state |
| Autostretch Settings | Preview dialog action | Disabled in Linear mode | Shared values load when opened | Opens display controls; Apply Stretch hidden | Tunes preview display | Autostretch mode only | Does not change target/mask samples | Display only | Viewer-scoped |
| Switch image | Preview action | Current result/mask shown | None | Click, **Space**, right-click, or **Ctrl+Shift+O** | Toggles to original target | Preview required | In Generated mask mode, compares mask/original rather than processed/original | Display only | Preview lifetime |
| Preview area | Preview checkbox/overlay | On with a centered `800 x 800` area, clipped to the image | None | Off/On; move/resize | Runs the Preview over the selected area only and shows that result over the rest of the image as it was; in Generated mask mode the mask is shown inside the area and the image around it | Preview required; disabled during Apply; Apply always uses the full image | Faster and lower memory; inside the area the result is what Apply makes of an image of that area, whose crop-local extremes and borders do not match full Apply | Preview/snapshot only; active mask cropped consistently | Preview lifetime |
| Take snapshot | Preview action | No snapshot | None | Click or **Ctrl+Shift+K** | Runs the current Preview Mode again and opens `<target> - DarkStructureEnhance - SNAPSHOT`; with Preview area on, the snapshot is the composite the Preview showed | Preview required; disabled during Apply | A Processed image snapshot keeps the source's metadata and is not recorded nonlinear; a Generated mask snapshot is the Float32 mask, grayscale or repeated over a color target's three channels | New still; target unchanged; no Create-mask side output | Each click creates a view |
| Active target mask | Existing target-view mask | Target state | None | Enabled/disabled and normal/inverted in target controls | Outer blend between original source and complete Dark Structure result | Ordinary process masking remains enabled; in Generated mask mode it blends the mask preview with the target | Useful protection, but can obscure internal-mask diagnosis; fixed mask is reused for sequence frames | After internal mask/transfer; Preview/Apply/snapshot; still/sequence | Target-view state; Reset unchanged |
| Apply | Dialog action | Enabled while idle | None | Click | Processes the full still in place, or every frame into a temporary replacement sequence | Requests the target lock; during the run Target, Channel, every process setting, Preview, Preview Mode, Create mask image, Apply, Reset, Close, and the Preview pane's snapshot and area actions are disabled | Mask build plus iterations; destructive, but the target changes only when the run succeeds | Full target; selected Channel; active mask; still/sequence | Adds one history step, records the result nonlinear, then resets parameters/Preview Mode/Create mask; keeps Target/Channel/Preview |
| Reset | Dialog action | Enabled while idle | None | Click | Restores `8`, B3, `0.40`, `1`, Processed image, and Create mask off | Leaves Target, Channel, Preview, active mask, and preview toolbar/area unchanged; disabled during Apply | Checked Preview schedules another run | Configuration only | Immediate; also runs after a successful Apply |
| Close | Dialog action | Enabled while idle | None | Click | Closes without another Apply | Disabled during Apply; closing the window while a run is in progress hides it at once, and it closes when the run has landed | Nothing waits on the GUI thread | Dialog/execution | Nothing is stored between openings |
| Progress / Cancel | Progress dialog with Cancel | Created for still and sequence Apply | None | Cancel | Shows activity rather than a percentage; a sequence also reports its frames; Cancel stops the run and publishes nothing | Apply only | The engine checks for Cancel at stage, layer, iteration, and row-block boundaries | Still/sequence; a cancelled sequence's temporary file is removed; no mask image | Per run |

## How the algorithm works

### Conceptual pipeline

1. Read the target, or the channel or color component **Channel** names, in
   normalized units: an unsigned integer divided by its largest code, a signed
   integer counted from its smallest code to its largest, and a floating sample
   as it is. Integer images are worked on in a Float32 copy (Float64 for 32-bit
   integers). **RGB** keeps all three channels; a selected component is
   extracted by the shared channel selector and put back afterwards.
2. Set aside every sample that holds no number (not-a-number or an infinity)
   and any sample the image's own validity marks as meaningless. Such a sample
   is read as zero while the mask is measured and is written back exactly as it
   was.
3. For each channel, remove the six finest dyadic starlet scales with the
   selected **Scaling Function**, then remove the **Layers to Remove** finest
   scales from that smoothing. Subtract the channel from the result: the
   difference is positive where the channel is darker than its smooth
   surroundings and negative where it is brighter.
4. Average the channels' differences equally into one plane and rescale it to
   `[0,1]` by its own finite extremes.
5. Denoise the mask with a fixed three-layer 3x3-linear starlet: discard its
   finest layer, soft-threshold the next at three noise deviations five times,
   keep the residual, then rescale to `[0,1]` by the extremes again.
6. Build one midtones transfer from Amount. For each Iteration, apply it to the
   whole channel, blend the darker result through the fixed mask, and hold the
   blend to `[0,1]`.
7. Narrow the result back to the source representation at the nearest code,
   apply any active target mask as an outer blend, and publish only after
   success.

### Starlet mask model

Let `F_c` be channel `c` of the normalized selected source, with set-aside
samples read as zero. Write `S_k(X)` for the residual of a `k`-detail-layer
undecimated dyadic starlet of `X`: `k` successive smoothings whose taps are
spaced `1, 2, 4, ..., 2^(k-1)` pixels apart. The fixed small-scale smoothing and
the selected large-scale comparison are

`B_c = S_6(F_c)` and `L_c = S_N(B_c)`,

where `N` is Layers to Remove. The dark-structure measurement is the signed
average

`Q = ((L_1 - F_1) + ... + (L_C - F_C)) / C`

over the `C` channels the run reads; it is not perceptual luminance. The first
mask is `A_0 = rescale_minmax(Q)`. Because `Q` is not clipped at zero, the place
darkest relative to its surroundings maps to one, the brightest relative to its
surroundings (usually a star core) maps to zero, and a place equal to its
smooth surroundings lands in between, at `-min(Q) / (max(Q) - min(Q))`.

The denoise decomposes `A_0` into two detail layers and a residual with the
3x3 Linear kernel. The finest layer is discarded. The second is soft-thresholded
five times at `t = 3 x 1.4826 x MAD`, where `MAD` is its median absolute
deviation around its own median. The two remaining layers are added back and a
second extreme-based rescale, clamped to `[0,1]`, produces the final mask `A`.
Non-finite values are ignored when finding extremes and written as zero. If the
finite range of either rescale is no wider than `1e-7`, the mask becomes
entirely black and the image is left as it was, apart from the clamp described
below.

Both selectable kernels are separable. Linear is `[0.25, 0.50, 0.25]`; B3
Spline is `[1,4,6,4,1]/16`. Out-of-frame access reflects about the edge pixel
without repeating it, which matters most for small Preview areas and high layer
counts. The multiscale transform holds at most twelve layers, its residual
included, and spaces taps at most `512` pixels apart. `N = 10` reaches exactly
that spacing; `N = 11` would need `1024` and `N = 12` thirteen layers, so the
dialog stops at `10`.

### Midtones and blend equations

Amount `a` maps to midtones parameter

`m = 0.5 + 0.5a`,

held to single precision. For a normalized sample `x` strictly between zero and
one, the transfer is

`T_m(x) = ((m-1)x) / ((2m-1)x - m)`.

Endpoints map to themselves. `m=0.5` is identity; the dialog produces
`0.5 <= m <= 0.995`, so increasing Amount darkens intermediate samples.

With internal mask `A`, each iteration is

`W_(k+1) = clamp(W_k(1-A) + T_m(W_k)A, 0, 1)`.

The clamp applies everywhere, including where the mask is zero, so a floating
sample outside `[0,1]` moves to the nearer end even where nothing is darkened.
The mask is built once from the source and does not change between iterations.
If an ordinary active target mask `M` is enabled, the framework then computes
`O = F(1-M) + W M`, replacing `M` with `1-M` when inverted.

## Inputs, outputs, and metadata

The result is written in place at the source's geometry, representation, and
channel layout; integer results are narrowed to the nearest code, ties to even.
The engine computes in single precision (Float32) whatever the representation.
An 8- or 16-bit integer or a Float32 image loses nothing to that, but a Float64
or 32-bit integer image keeps only single-precision accuracy, also where the
mask is zero. A sample that holds no number is left exactly as it was and takes
no part in the darkening.

The result keeps the target's astrometric solution, its acquisition record, and
every card the process does not own. Its processing-state cards record it as
nonlinear, assigned by a process, under the operation name
`dark-structure-enhance`; it claims no linear encoding; and its
lineage gains an entry naming `an::enhanceImageDarkStructures` with the layers,
kernel, midtones, and iterations. The history step is named
**DarkStructureEnhance** and described, for example, as
`Layers: 7, Scaling: 5x5 B3 Spline, Amount: 0.80 (midtones 0.90), Iterations: 2`.
On a color image, a run on one channel or component appends ` in channel: ` and
the selector's label, such as `in channel: CIE L* (Lightness)`.

The mask image that **Create mask image** opens is a Float32 grayscale image at
the target's size, named `<target> - DarkStructureEnhance Mask`, whose first
history step reads `DarkStructureEnhance: mask created from '<target>'`. It
keeps the target's acquisition record, astrometric solution and lineage; its
processing-state cards mark it nonlinear as `dark-structure-mask`, and it
carries no other card. A Generated mask snapshot is the same kind of Float32
mask, also marked `dark-structure-mask` - on a color target repeated over its
three channels; a Processed image snapshot keeps the source's metadata and is
not recorded as nonlinear.

## Parameter interactions and starting points

| Combination | Conservative starting point | Too weak looks like | Too strong looks like | Adjustment order |
| --- | --- | --- | --- | --- |
| Dust lanes on color image | CIE L*, Layers `8`, B3, Amount `0.40`, Iterations `1` | Mask/result barely separates lanes | Crushed cores and unnatural black channels | Channel -> mask Layers -> Amount -> Iterations |
| Narrow dark filaments | Generated mask, Layers `7`, Linear or B3 comparison | Filament absent from mask | Fine noise/star rims dominate | Layers -> Scaling -> outer mask |
| Broad dark nebula | Generated mask, Layers `9..10`, B3 | Only small knots selected | Gradient/background becomes the structure | Layers first; inspect full frame before Amount |
| Stronger effect | Hold mask fixed, raise Amount slightly | Lanes still shallow | One-pass shadow compression/posterization | Amount before Iterations |
| Protected stars/background | Validate pure internal mask, then enable normal target mask | Unwanted areas still darken | Protection excludes desired lanes | Internal mask -> target mask geometry/polarity -> Amount |

## Worked example

At Amount `0.40`, `m=0.70`. For one iteration, `T(0.20)=0.0968`,
`T(0.50)=0.30`, and `T(0.80)=0.6316` approximately. At source `x=0.50`,
mask `A=0` leaves `0.50`, `A=0.5` yields `0.40`, and `A=1` yields `0.30`.
Amount `0` leaves all in-range samples unchanged even though the mask is still
built. A second full-mask pass takes the `0.30` result to about `0.155`.

For an astronomical acceptance check:

- inspect Generated mask with the active target mask disabled;
- compare area and full-frame masks near crop/frame edges;
- verify intended dark lanes are brighter in the mask than the background,
  and check how bright the plain background is in the mask;
- compare Processed image/original at 100% for black clipping and star rims;
- inspect RGB channels after RGB processing or color reconstruction; and
- on still Apply, verify the optional mask is a Float32 grayscale image and the
  target result retains its representation and geometry.

## Performance

Mask creation smooths each channel the run reads twice, through six layers for
the fixed stage and Layers to Remove for the comparison, with two separable
passes per layer; it then denoises the one mask plane through a three-layer
transform. B3 uses five taps per pass versus three for Linear. Each Iteration
adds one transfer and one blend pass per channel; the mask is not rebuilt
inside the loop. RGB costs about three times one component.

Memory does not grow with Layers to Remove or with the channel count: the engine
works one channel at a time in three single-precision working planes, a mask
plane, and the three-layer mask decomposition. Around it the window holds the
run's copy of the target, a floating working copy of an integer image, the
working image the enhancement writes into, and, with Create mask image checked,
the mask image. A run the session's memory budget refuses publishes nothing and
reports an error that names the memory budget.

The implementation runs on the operation's threads. The engine checks for
Cancel at stage, layer, iteration, and row-block boundaries. Apply uses a
progress dialog that shows activity rather than a percentage. A sequence is
written to a temporary file that replaces the selected sequence only when every
frame has succeeded and is removed otherwise; a still is changed only when its
run succeeds.

Preview area lowers pixel and memory cost but is not a final-quality proxy:
starlet borders and both extreme-based rescales are computed on the crop.
Disable it before Apply decisions. No approved-data production timing or
peak-memory measurement has been made.

## Warnings and limitations

- The actual route is **Process > Details**, not Sharpening or Star Processing.
- Generated mask is relative to the global extremes of a signed difference, not
  an absolute or percentile-robust threshold. Bright stars set its low end, so
  a background that equals its surroundings can take a substantial mask value
  and be darkened, and one outlier can compress most mask values. Inspect the
  full-frame mask before Apply.
- Amount and Iterations do not affect the mask, although changing them reruns a
  Generated mask Preview.
- Preview area uses crop-local multiscale context and extremes; it can differ
  substantially from full Apply.
- An enabled active target mask mixes the Generated mask Preview with the
  image. Disable it to inspect the internal mask alone.
- Create mask image publishes only after still Apply; it is ignored for
  sequences and snapshots, and the mask image carries none of the target's
  header cards except its processing state (it keeps the target's acquisition
  record and astrometric solution).
- A Float64 or 32-bit integer image keeps only single-precision accuracy, and
  floating samples outside `[0,1]` are clamped wherever the process runs.

## Troubleshooting

| Symptom | Likely cause | Fix | Evidence basis |
| --- | --- | --- | --- |
| Nothing changes | Amount `0`, black/constant mask, active mask protection, or wrong Channel | Inspect pure Generated mask; restore defaults and check active mask/channel | Engine/window source and tests |
| Whole background darkens | Layer depth selects a broad gradient, or bright stars push the plain background high in the extreme-based mask | Reduce Layers; correct gradients first; inspect the full-frame mask; protect regions with an active target mask | Mask source |
| Area preview differs after Apply | Crop-local extremes and starlet borders | Turn Preview area off for the final check | Framework/multiscale source |
| Generated mask looks like the target outside a region | The active target mask outer-blended the mask preview | Disable the target mask while inspecting Generated mask | Framework mask source |
| Higher Amount does not change Generated mask | Amount affects the transfer only | Switch to Processed image to judge Amount | Engine source |
| Iterations crush shadows quickly | Same darkening curve repeats through fixed mask | Return to one iteration and lower Amount | Transfer equation |
| Color shifts | RGB channels are darkened through one shared mask, or a component is reconstructed | Prefer CIE L*/luma and inspect channels | Channel/framework source |
| No separate mask after sequence Apply or a snapshot | Mask publication is still-Apply only | Apply to a representative still, or take a Generated mask snapshot | Window source and tests |
| Apply reports an error naming the memory budget | The session cannot hold the run's working images | Close images you no longer need, or work on a smaller image or one component | Window refusal test |
| Cancel takes a moment | The engine stops at its next stage, layer, iteration, or row-block boundary | Wait; reduce area/layers while tuning | Engine source |

## Related workflow steps

- [Multiscale Linear Transform](../multiscale/multiscale-linear.md) — general
  scale-layer processing; Dark Structure Enhance uses a fixed native starlet
  pipeline instead of exposing layer controls.
- [Local Contrast Enhancement](../sharpening/local-contrast-enhancement.md) —
  high-radius residual contrast rather than a dark-only mask plus midtones
  transfer.
- [Local Histogram Equalization](../sharpening/local-histogram-equalization.md)
  — neighborhood-CDF contrast with different halo/noise behavior.
- [Range Selection](../analysis/range-selection.md) — intensity-driven range
  selection when multiscale dark-structure detection is not wanted.
- [Mask Drawing](../masking/mask-drawing.md) — manually refine or protect
  regions with an ordinary active target mask.
- [Glossary](../../reference/glossary.md) — starlet, MTF, mask, clipping, and
  nonlinear-image terminology.

## References and provenance

This page is source-first (**S**) with focused window tests. The menu,
registry, window, shared target/channel/preview framework, the engine's
dark-structure operation and multiscale transform, the midtones transfer, mask
blending, representation/state paths, and tests were traced. AfterNight
was not launched. The window tests establish
launch, Preview equal to Apply, the Preview area, active-mask weighting,
frame-by-frame sequences, the history step, metadata and lineage, persistence,
cancellation, every representation, a pixel holding no number, a memory refusal,
the mask image beside the target with the target's record, and the Generated
mask preview of a color target under an active mask with a sample holding no
number. No behavior record exists for this process, and no approved astronomical
still/sequence was processed, so visual quality, production timing, and peak
memory remain unverified.
