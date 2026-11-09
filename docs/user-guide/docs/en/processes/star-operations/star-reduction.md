---
kind: process
stage: finish
menu: Process > Star Processing > Star Reduction
summary: >-
  Shrinks the stars of an image by comparing it with a matching starless version and transferring through a protection
  mask.
data_in: nonlinear
data_out: nonlinear
derived: true
---
# Star Reduction

## Purpose and expected result

Star Reduction reduces stars in a target by comparing it with a geometrically
matching starless version of the same image. It offers two MTF-based transfer
methods and two iterative methods, can use an ordinary active mask to protect
small stars, and can produce a star-reduced image plus a separate image of the
removed/reduced stars.

Use it after creating and verifying a starless counterpart with
[Starnet++](starnet.md) or a compatible extension. **OK** is offered only once a
starless image is selected. A successful default still run leaves the target
unchanged and opens both a star-reduced still and a reduced-stars-only still.

## At a glance

| Item | Current behavior |
| --- | --- |
| Menu path | **Process > Star Processing > Star Reduction** |
| Enabled when | A still or sequence view is active, no duplicate Star Reduction window is attached to it, and no attached process holds exclusive access |
| Inputs | Target grayscale/RGB still or sequence in any integer or floating representation, plus one open, static starless view with the same width, height, channel count, and grayscale/RGB layout. Representations may differ |
| Outputs | Default: new `<target> - star reduced` still/sequence. Still runs can instead modify the target, and also open `<target> - reduced stars` beside it; sequence runs never create the reduced-stars-only image |
| Recommended image state | Matching nonlinear target/starless pair. The process always marks results nonlinear; do not mix linear and stretched counterparts |
| Preview support | Native real-time full/area preview (the target unchanged until a starless image is selected), original/result switching, snapshots, zoom/channel/display controls, and display-only execution autostretch |
| Sequence support | Supported through a temporary output sequence that is published whole or not at all. The starless still and active mask taken when **OK** is clicked are used for every frame, and each frame is reduced as a still image of it would be; reduced-stars-only output is skipped |
| Mask support | Supported through AfterNight's normal active-mask blend. Non-inverted white applies the reduction and black preserves the target; Invert reverses that. **Initialise small star protection** builds a grayscale mask in the background, then opens, attaches, enables, and hides it as a non-inverted mask |
| External dependencies | None for reduction. **Generate starless view** requires an available registered generator; native Starnet++ requires configured StarNet2 2.5.3+ |

## Before you start

1. Prepare a target and starless image from the same registered geometry. They
   must have identical width, height, channel count, and grayscale/RGB layout;
   their storage types may differ.
2. Keep both at the same processing state and stretch. The selector filters out
   obviously incompatible open views but cannot prove content alignment or
   matching processing history.
3. Inspect the starless image for removed nebula/galaxy detail. Any incorrect
   starless structure participates directly in the reduction equations.
4. Keep the default new-image output while tuning. Direct target replacement is
   destructive except for ordinary Image History/undo recovery.
5. If using a target sequence, keep one compatible starless still open. Every
   frame is reduced against that one image, taken when **OK** is clicked.

## Procedure

1. Select the target and open **Process > Star Processing > Star Reduction**.
2. Choose **Starless image**; **OK** and **Initialise small star protection**
   stay disabled until you do. If none exists, select a **Generator** and click
   **Generate starless view**; configure that process, run it, and confirm the
   generated compatible view is selected on return.
3. Start with **Star Method**, **Strong**, and **1** iteration. Leave
   **Create new star-reduced image** and **Create reduced-stars-only image** on.
4. Use **Show preview**, **Switch image**, and 100% zoom to compare the result
   with the target. Use **Preview area** for quick tuning, then inspect the full
   frame before execution.
5. If faint small stars disappear, click **Initialise small star protection**
   and keep the window open until the `<target> - star reduced mask` view
   opens; it becomes the target's mask. Inspect it, then keep
   **Enable small star protection** checked.
6. Tune one dimension at a time: method, then method-specific strength
   (**Stretch factor** or **Mode**), then **Iterations** for iterative methods.
7. Choose whether to replace the target or create a new reduced image. Set
   optional IDs, and choose reverse-stretch extraction only when that derived
   star-layer representation is intentionally wanted.
8. Click **OK**. On a still, inspect the reduced result and optional stars
   image. On a sequence, wait for the temporary sequence to finish and note that
   no stars-only sequence is created. **Cancel** in the progress dialog stops
   the run and changes nothing.

## Complete parameter and action reference

| UI label/action | Type and unit | Runtime default | Persisted-default exception | Valid values, step, or constraints | Effect of increasing, decreasing, or selecting | Dependencies and enable/visibility rules | Quality, artifact, runtime, memory, or disk trade-off | Scope: preview/execution, channel, mask, still/sequence | Persistence |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Target image | Image/sequence selector | Attached active target | None | Open eligible view | Selects source, output parent/name, mask, and history target | Active target required; changing target rebuilds compatible starless list | Target and starless must describe the same geometry/content | Preview/OK; all channels; still/sequence | Target lifetime |
| Starless image | Image selector | **No Image** | None | Open non-target view with the same width, height, channel count, and grayscale/RGB layout; any representation | Supplies static background reference; the image is taken from its view when a run is asked for | **OK** and **Initialise small star protection** are enabled only while one is selected; closing the selected view returns the selector to **No Image** | Wrong content/alignment produces false reductions. It is read in its own normalized units and converted to the target's floating working precision, not to the target's storage type | Preview/OK; a preview area crops it with the target; the same starless still for all sequence frames | Dialog lifetime |
| Generator | Registered-process selector | First available generator | Registry/global readiness is external | Native Starnet++ and extension processes declaring starless output | Chooses helper process to open | Selector exists when a generator is registered; action requires selected generator availability | Generator runtime/quality occurs outside Star Reduction | Configuration handoff only | Dialog lifetime |
| Generate starless view | Action | Enabled for available generator | Native availability follows global StarNet2 path/version | Click | Opens the generator on the target. Native Starnet++ opens configured for this use (no star mask, unscreen star extraction, closing once done) and hands its starless view back, which is selected; for another generator, the next compatible view that opens is selected | Disabled with no available generator | May launch external inference and create an auxiliary still | Generator process, not reduction execution | Generated view persists independently |
| Initialise small star protection | Action | Disabled until starless selected | None | Click | Builds a grayscale mask in the background; when it is ready, opens `<target> - star reduced mask`, attaches it to the target, enables it non-inverted, hides the overlay, and checks Enable | Requires selected starless; disabled while a mask is being built; ignored while a run is in progress | Several full-image neighborhood passes and a mask view; no progress dialog and no Cancel; discarded if the window closes before it is ready | Target/starless still; preview/OK then use normal mask blend | Generated mask view persists |
| Enable small star protection | Checkbox | Off | Existing target masks can be toggled before this dialog generates its own mask | Off/On | Toggles target mask enablement when current mask is eligible | Needs a current mask; after generation it only controls that generated mask when selected | Non-inverted black preserves target and white applies reduction | Generic active-mask blend; still/sequence | Target mask state, not a saved process preset |
| Reduction method | Selector | **Star Method** | None | **Transfer Method**, **Halo Method**, **Star Method**, **Lightness Method** | Selects equation family | Transfer/Halo enable Stretch; Star/Lightness enable Mode and Iterations | Different families target overall stars, halos, or lightness-weighted structure differently | All target channels; preview/OK | Reset/success restores Star |
| Stretch factor | Normalized value | `0.20` | None | `0.01..0.99`, step `0.01`; the transfer is defined strictly inside the unit interval | Changes MTF midtones `1 - factor` for Transfer/Halo; also supplies reverse-stretch extraction even when disabled | Enabled only for Transfer/Halo | Values near the endpoints drive strong mappings; output is guarded and clipped | Transfer/Halo reduction; optional stars extraction | Reset/success restores `0.20` |
| Mode | Selector | **Strong** | None | **Strong**, **Moderate**, **Soft** | Chooses iterative postprocessing | Enabled only for Star/Lightness | Moderate/Soft retain progressively more of the original stellar signal by their formulas | Star/Lightness only | Reset/success restores Strong |
| Iterations | Integer passes | `1` | None | `1..3` | Repeats Star or Lightness recurrence | Enabled only for Star/Lightness | More passes usually increase reduction and can flatten cores/structure | Star/Lightness; preview/OK | Reset/success restores `1` |
| Reduce stars on target image | Checkbox | Off | None | Off/On | Replaces target/sequence after successful OK execution | Selecting it unchecks Create new; at least one reduced-output destination is always forced | Destructive working-view change; sequence output stays temporary until it is published whole | OK only; not Preview/snapshot | Reset/success restores Off |
| Create new star-reduced image | Checkbox | On | None | Off/On | Opens a new reduced still/sequence | Selecting target replacement unchecks it; turning both destinations off forces this back on | Safer comparison but adds image/sequence memory/disk | OK output | Reset/success restores On |
| Image id | Text | `<Auto>` | None | Empty, `<Auto>`, or view name | Auto uses `<target> - star reduced`; other trimmed text is requested name | Enabled only with Create new | Normal view creation uniquifies collisions | New reduced still/sequence | Reset/success restores `<Auto>` |
| Create reduced-stars-only image | Checkbox | On | None | Off/On | After a successful still OK execution, opens beside the target a stars layer extracted from the reduced result and the starless image | Controls reverse-stretch and Stars id; ignored for preview/snapshot/sequence | Adds full-image copies, an extraction, and another still | Still OK only; the extraction reads the result as the active mask weighs it | Reset/success restores On |
| Enable reverse stretch | Checkbox | Off | None | Off/On | Uses MTF-transform/unscreen/inverse-MTF extraction instead of direct unscreen | Enabled only with stars-only output | Produces a different derived layer; uses Stretch factor even when Star/Lightness makes that field disabled | Stars-only still | Reset/success restores Off |
| Stars id | Text | `<Auto>` | None | Empty, `<Auto>`, or view name | Auto uses `<target> - reduced stars` | Enabled only with stars-only output | Naming only; normal view creation uniquifies collisions | Stars-only still | Reset/success restores `<Auto>` |
| Show preview | Checkbox | On | None | Off/On | Creates/destroys real-time preview and schedules changes | Preview actions require it | Preview consumes image buffers and repeated engine runs | Preview only | Dialog lifetime |
| Preview pane | Image viewer | Filtered result | Execution autostretch supported | Pan/scroll and toolbar actions | Shows mask-aware result without changing target; shows the target until a starless image is selected | Exists with Show preview | Holds source/result/display buffers | Preview only | Preview-window lifetime |
| Fit To Window | Preview action | Preview fitted initially | None | Click or **Ctrl+Shift+F** | Fits whole image uniformly | Preview required | Broad context; resampling can hide small-star artifacts | Display only | Viewer lifetime |
| Zoom In | Preview action | Current scale | None | Click; `1.25x` relative zoom | Enlarges around viewport center | Preview required | Improves local inspection | Display only | Viewer lifetime |
| Zoom Out | Preview action | Current scale | None | Click; reciprocal relative zoom | Shrinks view | Preview required | Improves context; hides pixel-scale artifacts | Display only | Viewer lifetime |
| Zoom 100% | Preview action | Current scale | None | Click or **Ctrl+Shift+1** | Shows one image pixel per display pixel | Preview required | Best for cores, halos, ringing, and small stars | Display only | Viewer lifetime |
| View Channel | Preview selector | **RGB** for RGB; disabled **Grayscale** for mono | None | Stored/derived RGB, CIE, HSL/HSV/HSI, and YCbCr components | Changes displayed component only | RGB preview required for derived components | Helps inspect color/core changes without changing samples | Display only | Viewer lifetime |
| Display Mode | Preview selector | Inherits target Linear/Autostretch state | None | **Linear**, **Autostretch** | Changes preview display only | Autostretch enables settings | Strong display stretch can exaggerate faint residuals | Display only | Preview view state |
| Autostretch Settings | Preview dialog action | Disabled in Linear mode; inherits target settings | Shared values load when opened | Opens preview display controls; Apply Stretch hidden | Tunes preview visualization | Autostretch mode only | Never changes reduction samples | Display only | Viewer-scoped |
| Switch image | Preview action | Result shown | None | Click, **Space**, right-click, or **Ctrl+Shift+O** | Toggles result/target | Preview required | Comparison only | Display only | Preview lifetime |
| Preview area | Preview checkbox/overlay | Checked with initial `1024x768` area when preview-area support is enabled | None | Off/On; move/resize overlay | Crops target, starless, and active mask consistently for computation, then composites result over full preview | Preview required; OK always processes the full frame | Faster tuning; can hide field-wide artifacts | Preview and snapshot | Preview lifetime |
| Take snapshot | Preview action | No snapshot | None | Click or **Ctrl+Shift+K** | Re-executes and opens `<target> - Star Reduction - SNAPSHOT` | Preview required | Adds an execution and auxiliary still; no stars-only auxiliary | New still; area/mask honored | Each click creates a view |
| OK | Dialog action | Disabled until a starless image is selected | None | Click | Runs the parameter snapshot taken when clicked | Enabled only while a starless image is selected and no run is in progress | Full-frame work; controls lock through normal execution | Still/sequence | Successful execution resets method/output controls |
| Reset | Dialog action | Enabled | None | Click | Restores method/output defaults | Does not change the starless selection or delete a generated mask or starless view | Can reschedule preview | Dialog only | Immediate |
| Close | Dialog action | Enabled while idle | None | Click | Closes without starting a new run | Normal process close rules apply during work | Does not delete auxiliary views; a protection mask still being built is discarded | Dialog only | Discards dialog settings |
| Progress / Cancel | Progress action | Per execution/frame | None | Cooperative Cancel | Reports processing/sequence progress and requests a stop | OK and sequence runs; small-star mask building has no progress dialog | The engine checks the stop between row blocks and stages; a stopped run publishes nothing, leaves the image and its history as they were, opens no new image, and shows no error; sequence temp output is removed | OK/sequence | Per run |

## How the algorithm works

### Conceptual pipeline

1. On the GUI thread, take the run's snapshot: the typed parameters, the
   selected starless image, the target's enabled mask, and the preview area.
   The starless image is read under its own lease; in a preview with a preview
   area, it is cropped to the same rectangle as the target. A starless image
   whose size or channel count does not match the target's is refused.
2. Read both images in normalized units: an unsigned integer as a fraction of
   its largest value, a signed one counted from its smallest value, a floating
   sample as it is. The working samples are Float32, or Float64 for Float64 and
   32-bit integer targets; the starless image is converted to the target's
   working precision when it differs. Every sample is then held to the unit
   interval: a non-finite sample becomes zero and every other is clamped to
   `[0,1]`.
3. Plan and run the engine's reduction (`planStarReduction`, `reduceStars`) per
   pixel/channel in parallel, clamp the result, and narrow it back to the
   target's representation, to the nearest code (ties to even).
4. On a still **OK** only, when the stars image is asked for: copy the reduced
   result, weigh it by the target's enabled mask as the result itself will be
   weighed, extract the stars against the starless image
   (`extractReducedStars`), and stage the stars image. It opens beside the
   target only once the reduced result has been published.
5. Record the result as nonlinear `star-reduction`, keeping the target's
   metadata, and name `an::reduceStars` with the method, strength, iterations,
   stretch factor, and starless image in its lineage.
6. If a target mask is enabled, blend original `T` and reduced `R` by mask `A`:
   non-inverted output is `T(1-A) + RA`; inverted output is `TA + R(1-A)`.
   Then publish the result in place, as a new still, or as a new or replacing
   sequence.

### Model and equations

Let `t` be a normalized target sample, `s` the matching starless sample,
`f` the Stretch factor, and `M(m,x)` AfterNight's normalized rational midtones
transfer `(m-1)x / ((2m-1)x - m)` at midtones `m`, pinned to `0` and `1` at the
ends. A denominator at zero gives the guarded value zero, and every final result
is clamped. The engine accepts `f` only strictly between `0` and `1`, and checks
it for every method.

**Transfer Method**, with `m = 1-f`:

`r = 1 - ((1 - M(m,t)) / (1 - M(m,s))) x (1-s)`

**Halo Method** first computes

`e = [1 - (1-t)/(1-s)] - [1 - (1-M(m,t))/(1-M(m,s))]`

then

`r = t - 0.5(1+t)e(1-0.5s)`.

**Star Method** starts with

`v1 = t^2 + (1-t)s`

and repeats `v(next) = v^2 + (1-v)s` for up to three iterations.

**Lightness Method** computes one CIE lightness `l=L*/100` per RGB pixel (or
uses `l=t` for mono). The pixel's red, green, and blue are read the way the
channel selector of every process reads a colour image: as linear RGB when the
image states that it is linear, and as sRGB-encoded otherwise. It starts with

`v1 = lt + (1-l)s`

and repeats `v(next) = lv + (1-l)s`.

Strong returns the iterative `v`. Moderate replaces it with

`max(v, tv + v(1-v))`.

Soft first applies that Moderate expression, then returns

`0.5[(1-t)t + (1+t)v]`.

All four final results are clamped to `[0,1]`.

### Small-star protection mask

The mask is built in the background under its own operation, from the full
target and starless images held to the unit interval as above, with the
default reduction settings; it does not depend on the method controls or the
preview area. The generator averages target and starless channels separately,
in Float32 working planes. It maps each field's minimum and maximum linearly to
`-0.001` and `1.001`, then clamps the mapped samples to `[0,1]`; a constant
field becomes zero. It then forms a guarded ratio

`q = clamp((T-S) / (T(1-S)))`.

It calculates an 11-pixel circular-median background estimate and rescales that
field to `[0,0.95]`. After subtracting it, the generator applies
`M(0.3, abs(...))` and rescales the residual to `[0,0.75]`. It then mixes the
result equally with a 3-pixel cross dilation (each pixel and its four edge
neighbors), applies a 7x7 box average, and inverts. Neighborhoods clamp to the
nearest edge sample. The result is a one-channel floating image (Float32, or
Float64 for Float64 and 32-bit integer targets) recorded nonlinear as
`small-star-protection-mask`. When it is ready, the window opens it as
`<target> - star reduced mask` and attaches it as a normal non-inverted active
mask; black areas preserve the target. If the window has been closed by then,
the mask is discarded.

### Reduced-stars extraction

Normal extraction uses the unscreen equation

`stars = clamp((reduced - starless) / (1-starless))`.

When `starless >= 1`, the guarded result is zero. Reverse-stretch extraction
first transforms both samples with `M(f,x)`, unscreens them, then applies
`M(1-f,x)` and clamps. It uses Stretch factor even when the current reduction
method disables that control. `reduced` is the reduced result as the target's
enabled mask weighs it, so a protected area contributes no stars.

### Boundary and failure behavior

- **OK** and **Initialise small star protection** are disabled while
  **Starless image** is **No Image**. The preview then shows the target
  unchanged; a run asked for without a starless image is refused with
  `select a starless image of the target to reduce against`.
- The selector offers only views with the target's width, height, channel
  count, and grayscale/RGB layout; representations may differ and are read in
  normalized units. A starless image that no longer matches when the run reads
  it is refused with `the starless image does not have the target's size and
  channels`.
- The engine also refuses an undebayered color-filter-array (mosaic) target and
  a Lightness reduction of anything but a mono or RGB image (for example, RGBA).
- Non-finite input becomes zero, and input outside `[0,1]` is clamped, before
  the equations run; a sample holding no number does not stop the run.
- Cancellation is checked between row blocks and mask stages. A stopped run
  publishes nothing, opens no stars image, leaves the image and its history as
  they were, and shows no error; a cancelled sequence's temporary output is
  removed. A run the engine refuses (for example, for lack of working memory)
  also publishes nothing and says why.
- The static starless view can be renamed/closed/changed while the dialog is
  open; selectors refresh, and closing the selected view returns the selector
  to **No Image**. A run already started keeps reading the image it was given.

## Parameter interactions and starting points

| Combination | Conservative starting point | Too weak looks like | Too strong looks like | Adjustment order |
| --- | --- | --- | --- | --- |
| General reduction | Star, Strong, 1 iteration, new output | Stellar dominance changes little | Cores flatten, color/structure disappears, dark rings emerge | Verify pair/alignment; choose method; choose mode; then iterations |
| Halo-focused | Halo, Stretch `0.20` | Wide glow remains | Dark/bright rings or clipped transitions | Tune factor at 100% within `0.01..0.99`; compare with Star method |
| Gentle RGB | Lightness, Soft or Moderate, 1 iteration | Bright stars remain dominant | Color/core relationships look flattened | Inspect derived View Channels; increase mode strength before iterations |
| Small-star protection | Generated mask, non-inverted, enabled | Faint stars still vanish | Too much field remains unreduced | Inspect mask itself; verify black protection; then retune reduction |

## Inputs, outputs, and metadata

The target controls the result's representation, dimensions, and channels.
Both images are read in normalized units and worked in floating precision; a
different-type starless image is converted to the target's working precision,
not to its storage type, so an integer target does not quantize a floating
starless reference. The reduced result is narrowed to the nearest code of the
target's representation. The reduced-stars-only still has the target's
representation and channels; the protection mask is a one-channel floating
image.

The reduced result keeps the target's metadata: its acquisition record
(including exposure and filter), astrometric solution, and cards. Its
processing-state cards record it as nonlinear, set by a process named
`star-reduction`; it claims no linear encoding; and its lineage names
`an::reduceStars` with the method, strength, iterations, stretch factor, and the
starless image's name. Nothing of the starless image's metadata is merged into
it, so the exposure is the target's own.

The reduced-stars-only still and the protection mask are new images derived
from the target. They keep its acquisition record, solution, and lineage, and
carry no other card than their processing state: nonlinear `reduced-stars` (or
`reduced-stars-reverse-stretch`) and `small-star-protection-mask`. Every sample
is valid, and they carry no output decision.

A successful in-place OK execution records one Star Reduction step on the
target, described as `Star reduction using <starless name>`, which undoes. A new
reduced still or sequence opens with that description as its first history
entry and leaves the target and its history unchanged. The stars view starts
with `Star Reduction: Reduced-stars image created from '<target>'`, and the
mask view with `Star Reduction: Small-star mask created from '<target>'`.
Successful OK execution resets method and output choices to defaults; it does
not change the starless selection or delete generated views. A window opened
again shows the dialog's own defaults.

## Performance

Core reduction is parallel and linear in pixels x channels x iterations, with a
small fixed iteration cap. Each run first makes floating working copies of the
target and starless image (an integer image is converted, and a starless image
of another precision is converted again), so memory grows with the image size
and the target's representation. A still OK with the stars image adds copies of
the result, its mask-weighted version, and the stars image. Preview-area
cropping reduces tuning cost. Small-mask generation is more expensive: it copies
both full images, holds three Float32 working planes, and its 11-pixel circular
median and 7x7 box pass use explicit neighborhoods. Sequence runs write a
temporary sequence and use one static starless/mask image for all frames. No
production timing or peak-memory measurement is claimed.

## Worked example

- Evidence mode: Source-and-test-derived; no approved astronomical pair was
  executed.
- Synthetic checkpoint: for mono `t=0.82`, `s=0.43`, Star/Strong/3 iterations
  evaluates the recurrence three times: `v1 = 0.7498`, `v2 ~ 0.6698`,
  `v3 ~ 0.5906`. The six behavior records (default mono and RGB,
  Transfer, Halo, Lightness, and Soft with three iterations) are reproduced
  through the window within their recorded tolerance.
- Workflow checkpoint: with a compatible starless view and default output
  choices, a successful still OK execution opens a reduced still and a
  reduced-stars still beside the target while leaving the target unchanged.
- Inspect at 100%: cores, halos, color, protected faint stars, nonstellar detail,
  and mask transitions under identical display conditions.
- Limitation: synthetic equations prove implementation behavior, not desirable
  settings or visual quality on astronomical data.

## Warnings and limitations

- A starless image is a spatial reference, not merely any star-free image. Wrong
  alignment, crop, channel layout, or processing state invalidates the result.
- **Stretch factor** runs from `0.01` to `0.99`: the midtones transfer is
  defined strictly inside the unit interval. **Reset** restores `0.20`.
- The generated protection mask is attached through the ordinary target-mask
  system. Before generation, the checkbox can toggle an already selected target
  mask; after generation, changing the current mask can make the checkbox stop
  controlling it. Closing the window before the mask is ready discards it.
- The stars-only auxiliary is not generated for previews, snapshots, or
  sequences.
- Reverse stretch uses Stretch factor for every method, including Star/Lightness
  where the control is disabled and normally remains `0.20`.
- Results are always declared nonlinear and clipped to the normalized range.

## Troubleshooting

| Symptom or exact error | Verified cause/validation path | Recovery | Evidence source |
| --- | --- | --- | --- |
| **OK** is disabled | Starless selector is **No Image**, or a run is in progress | Select a compatible starless view before clicking OK | `validateParameters` in the window; `MasksProcessCase` |
| Desired starless view is absent | It is the target itself or differs in width, height, channel count, or grayscale/RGB layout | Generate/reopen a matching view; align/crop before retry | `isStarlessViewSupported` |
| `the starless image does not have the target's size and channels` | The selected image no longer matches the target when the run reads it | Reselect a matching view | run validation |
| `star reduction has no meaning on an unresolved colour filter array` | The target is an undebayered mosaic | Debayer the target and starless image first | `planStarReduction` |
| Small stars still disappear | Mask missing/disabled/inverted or does not cover them with low values | Inspect current mask, use non-inverted black protection, regenerate if needed | generated-mask pipeline and generic blend |
| Protection mask never appears | The window was closed before the mask was ready, or building it failed and an error was shown | Keep the window open until the mask view opens; retry | background mask landing |
| Reduced-stars image is missing | Preview/snapshot/sequence run, option off, or a cancelled/failed execution | Use a successful still OK execution with the option enabled and a compatible starless view selected | staged-output gate |

## Related workflow steps

Create the matched pair with [Starnet++](starnet.md) or another registered
starless generator, and inspect its [StarNet++ Star Mask](starnet-star-mask.md)
or an analytical [Star Mask](star-mask.md) where helpful. Use
[Masks](../../interface/masks.md) to inspect/invert the active protection mask.
The reduced-stars-only output can be inspected or passed to a later
[Starmask Recombination](starmask-recombination.md) workflow; that process is
documented on its own page and is not redefined here.

## References and provenance

- The exact four equation families, mode postprocessing, mask construction,
  unscreen/reverse-stretch extraction, normalization, clipping, types,
  cancellation, metadata, and output behavior come from AfterNight's source and
  its tests.
- [StarNet command-line tool reference](https://starnetastro.com/documentation/starnet/command-line-tool/)
  applies only when native Starnet++ is chosen as the helper generator; Star
  Reduction itself does not call StarNet2.
- Star/Lightness mode tooltips describe Star as the general-purpose
  starting method and Moderate/Soft as progressively gentler choices. These are
  implementation-provided starting guidance, not quality measurements.
