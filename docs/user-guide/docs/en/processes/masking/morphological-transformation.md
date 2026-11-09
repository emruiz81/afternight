---
kind: process
stage: any
menu: Process > Morphology > Morphological Transformation
summary: >-
  Grows or shrinks bright and dark structures by replacing each sample with a local minimum, maximum or other rank.
data_in: either
---
# Morphological Transformation

<!-- MEDIA_SLOT: UG-MEDIA-102 -->

## Purpose and expected result

Morphological Transformation changes the spatial extent of bright or dark
structures by replacing each sample with a local minimum or maximum - or, with
**Morphological Selection**, any rank between them. It can shrink bright mask
regions, grow them, remove small bright islands, close small dark gaps, or take
a local median through a shaped neighbourhood. The neighborhood is an editable
binary structuring element from `3×3` through `25×25`.

**Apply is destructive:** a successful run replaces the selected still or
sequence. The process does not create or assign a mask. To refine a mask, make
the mask image itself the target (preferably a duplicate), then assign that
image separately if it is not already attached to the intended target.

## At a glance

| Item | Current behavior |
| --- | --- |
| Menu path | **Process > Morphology > Morphological Transformation** |
| Enabled when | An image or sequence is active, this process is not already attached to it, and no attached process holds exclusive access |
| Inputs | One grayscale or RGB still/sequence; optional active target mask; editable odd square binary kernel |
| Outputs | Replaces the target with the same geometry, channel count, and pixel type; Preview snapshots are separate auxiliary images |
| Recommended image state | Work on a duplicate grayscale/binary mask with no active mask. Linear or nonlinear science images are accepted, but morphology is destructive and derived-channel reconstruction can change colors. |
| Preview support | Asynchronous RT preview, compatible Preview area, original/result switch, snapshot, zoom, display autostretch, and display-channel selection |
| Sequence support | Supported through a temporary replacement sequence with generic progress; source metadata is kept per frame, and Cancel reaches the morphology running on the current frame |
| Mask support | Supported: after morphology and Amount blending, the shared framework blends the original and result through the target's enabled active mask |
| External dependencies | None; the AfterNight engine's own morphology and structuring-element rank filter |
| Non-finite samples | A NaN or infinite sample is treated as missing: it is left out of every neighbourhood and left where it is |

## Before you start

Duplicate a mask or science image before experimenting. Disable the target's
active mask when the entire mask image should be changed; otherwise the active
mask limits the morphology result and can introduce intermediate gray values.
Inspect the current structuring-element grid, not just its **Size** label. A
size change preserves the centered old cells and initializes the new rim to
zero; it does not automatically fill the larger square.

Keep the center cell active for conventional centered morphology. **Clear
grid** creates an element with no active samples; **Apply** is disabled until
at least one cell is active again, and the preview leaves the image unchanged.
Choose a preset, **Fill grid**, or click at least one cell.

## Procedure

1. Activate a duplicate mask and open **Process > Morphology > Morphological
   Transformation**. Confirm **Target Image**, remove/disable an unintended
   active mask, and keep **Preview** checked.
2. Choose the operation by intent: Erosion shrinks bright regions; Dilation
   grows them; Opening removes bright details smaller than the element; Closing
   fills dark gaps smaller than the element; Morphological Selection takes the
   rank set by **Selection** among the samples the element covers (`0` is
   Erosion, `1` is Dilation, `0.5` the element's median).
3. Choose an odd **Size**, then select Circle, Diamond, Cross, or Diagonal, use
   **Fill grid**, or click cells to make a custom binary element. Confirm the
   center cell and the intended symmetry.
4. Start with **Iterations** `1` and **Amount** `1.00`. On RGB images, choose
   **RGB** to process all stored channels independently, or choose one component
   to process it and reconstruct the color image.
5. Inspect the result at **Zoom 100%**, switching between original and result.
   Check borders and structures near the kernel scale. Use **Preview area** to
   tune quickly, then inspect more than one representative region.
6. Reduce **Amount** when the full result is too abrupt. Increase kernel size
   before Iterations when the neighborhood shape matters; increase Iterations
   when the same element should reach farther.
7. Select **Apply** and wait for the target update. Check Image History, but
   also record the grid separately when reproducibility matters: current
   history stores operator, iterations, Amount, Selection for Morphological
   Selection, and the channel of a colour image, not structuring-element size
   or pattern.
8. If the processed image should become another image's mask, activate that
   image and use **Mask > Select Mask**. Morphological Transformation itself
   neither creates nor assigns that relationship.

## Complete parameter and action reference

| UI label/action | Type and unit | Runtime default | Persisted-default exception | Valid values, step, or constraints | Effect of increasing, decreasing, or selecting | Dependencies and enable/visibility rules | Quality, artifact, runtime, memory, or disk trade-off | Scope: preview/apply, channel, mask, still/sequence | Persistence |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| **Operator** | Selector | Erosion (Minimum) | None | Erosion, Dilation, Opening, Closing, Morphological Selection; see label defect below | Selects local minimum, local maximum, erosion→dilation, dilation→erosion, or the rank **Selection** names | Disabled during Apply | Opening/Closing perform both primitive operations; Selection ranks every covered sample | Preview/Apply; chosen channel(s) | Captured index and history text; Reset/success Erosion |
| **Selection** | Double + slider | `0.50` | None | `0..1`; spin step `0.05`; slider `0..1000` | Rank among the samples the element covers: `0` the minimum (Erosion), `1` the maximum (Dilation), `0.5` the median; between two ranks the value is interpolated linearly | Enabled only for Morphological Selection; disabled during Apply | Ranking costs more than a minimum/maximum, and grows with the active cell count | Preview/Apply; chosen channel(s) | Captured; recorded in history for Morphological Selection; Reset/success `0.50` |
| **Iterations** | Integer | `1` | None | `1..20`; step `1` | Repeats the primitive operation(s), extending reach; every requested repetition is applied | Disabled during Apply | Runtime and effect increase with count | Preview/Apply | Captured; Reset/success `1` |
| **Amount** | Double + slider | `1.00` | None | `0..1`; spin step `0.05`; slider `0..1000` | Blends from original (`0`) to full morphology (`1`) | Disabled during Apply | The full morphology still runs at Amount `0`; blend adds a pass | Preview/Apply; before active mask | Captured; Reset/success `1` |
| Structuring-element **grid** | Clickable binary grid | Filled `3×3` | None | Each cell active/inactive; centered anchor | Active cells participate in min/max; click toggles one cell | Disabled during Apply | Sparse elements sample fewer positions conceptually; asymmetric grids shift directional influence around the fixed center | Preview/Apply; all processed channels | Deep-copied into each execution snapshot; Reset/success filled `3×3` |
| **Size** | Selector | `3 (9 elements)` | None | Odd `3,5,…,25`; label reports square capacity | Enlarging preserves centered cells and adds zero rim; shrinking center-crops | Disabled during Apply | Larger footprint increases reach and work | Preview/Apply | Live element; Reset/success `3` |
| **Clear grid** | Button | — | None | Click | Sets every cell to zero | Disabled during Apply | An empty element has nothing to apply: Apply is disabled and the preview leaves the image unchanged until a cell is active | Preview/Apply settings | Until changed/reset |
| **Fill grid** | Button | — | None | Click | Activates every cell in the selected square | Disabled during Apply | Strong isotropic square footprint; greatest active-cell count for size | Preview/Apply settings | Until changed/reset |
| **Invert** | Button | — | None | Click | Swaps active and inactive cells | Disabled during Apply | Can deactivate center or produce disconnected footprint | Preview/Apply settings | Until changed/reset |
| **Circular structure** | Button | — | None | Click; cells whose squared distance from the center is at most `r²+r`, where `r` is the radius | Replaces grid with a filled circle-like footprint | Disabled during Apply | Smooth, near-isotropic growth/shrinkage | Preview/Apply settings | Until changed/reset |
| **Diamond structure** | Button | — | None | Click; Manhattan distance from center ≤ radius | Replaces grid with a filled diamond | Disabled during Apply | Axis-weighted footprint | Preview/Apply settings | Until changed/reset |
| **Cross structure** | Button | — | None | Click | Activates center row and center column | Disabled during Apply | Connects/grows along horizontal and vertical axes | Preview/Apply settings | Until changed/reset |
| **Diagonal structure** | Button | — | None | Click | Activates both diagonals (an X) | Disabled during Apply | Connects/grows on diagonals; center active | Preview/Apply settings | Until changed/reset |
| **Target Image** | Image/sequence selector | Attached active target | None | Any target accepted by launch guards | Changes destructive destination, active mask, channels, and preview | Disabled during Apply | Wrong target is overwritten | Preview/Apply; still/sequence | Target lifetime; Reset unchanged |
| **Channel** | Selector | RGB or disabled Grayscale | None | Grayscale; RGB, R/G/B; CIE L*/a*/b*/c*/h*/X/Y/Z; HSL H/S/L; HSV H/S/V; YCbCr luma/blue chroma/red chroma | RGB applies independently to all stored channels; another RGB component is extracted, processed, then color is reconstructed | Non-grayscale choices only for RGB targets; selector disabled during Apply | Derived-space conversion/reconstruction adds work and can change out-of-gamut colors | Preview/Apply; selected component | Target-dependent; captured for execution/history; Reset unchanged |
| **Preview** | Checkbox | Checked | None | Off/on | Enables asynchronous live result | Not explicitly disabled during Apply | Recomputes after setting edits; buffers add memory | Preview only; active-mask-aware | Window lifetime; Reset unchanged |
| **Preview pane** | Image viewer | Full-frame result | None | Standard pan/scroll and actions below | Shows post-Amount, post-mask result without commit | Exists while Preview checked | Holds source/result/display buffers | Preview only | Window lifetime |
| **Fit To Window** | Preview action | Fitted initially | None | Click or **Ctrl+Shift+F** | Fits whole image | Preview required | Broad context can hide single-pixel changes | Display only | Viewer lifetime |
| **Zoom In** | Preview action | Current scale | None | Click; `1.25×` | Enlarges view | Preview required | Better kernel-scale inspection | Display only | Viewer lifetime |
| **Zoom Out** | Preview action | Current scale | None | Click; `0.8×` | Reduces view | Preview required | More context, less pixel detail | Display only | Viewer lifetime |
| **Zoom 100%** | Preview action | Current scale | None | Click; scale `1.0` | Shows one-pixel viewer scale | Preview required | Best for speckles, gaps, and borders | Display only | Viewer lifetime |
| **View Channel** | Preview selector | Follows result | None | Supported stored/derived display components | Changes displayed component only | RGB choices require RGB result | Helps isolate channel-dependent structures | Display only | Viewer lifetime |
| **Display Mode** | Preview selector | Linear | None | Linear, Autostretch | Changes display transfer only | Preview required | Autostretch may exaggerate low-level residuals | Display only | Preview lifetime |
| **Autostretch Settings** | Preview action | Initialized from target view | None | Opens display settings | Adjusts Autostretch only | Relevant in Autostretch mode | Adds display/statistics work | Display only | Preview lifetime |
| **Switch image** | Preview action | Result shown | None | Click, **Space**, right-click, or **Ctrl+Shift+O** | Toggles result/original | Preview required | Direct edge comparison | Preview/current frame | Preview lifetime |
| **Preview area** | Preview checkbox/region | Unchecked; centered `600×600` where geometry permits | None | Off/on; movable supported area | Processes a supplied crop and composites it into full presentation | Disabled during Apply; Apply always processes full frame | Reduces tuning work; can miss structures elsewhere | Preview/snapshot only | Window lifetime |
| **Take snapshot** | Preview action | No snapshot | None | Click or **Ctrl+Shift+K** | Re-executes and opens auxiliary result | Preview required; disabled during Apply | Adds processing/allocation; target unchanged | New still | One view per action |
| **Apply** | Dialog action | Enabled while the element has an active cell | None | One click | Processes full target and replaces it after success | Disabled while the grid is empty and while Apply runs; morphology/grid/channel controls lock | Destructive; work scales with image, element, iterations, and channels | Still/sequence; active-mask-aware | Adds incomplete history; resets settings after success |
| **Close** | Dialog action | Enabled | None | One click | Closes without starting another Apply | Disabled while Apply runs | A running preview is cancelled | Dialog | Discards UI state |
| **Reset** | Dialog action | Enabled | None | One click | Restores Erosion, 1, Amount 1, Selection `0.50`, filled `3×3`; schedules Preview | Disabled while Apply runs | May trigger preview work | UI/preview | Immediate; also after success |
| **Sequence progress / Cancel** | Shared progress dialog | Created on sequence Apply | None | Cooperative Cancel | Writes frames to a temporary sequence and replaces target only after success | Sequence target only; the morphology reports no progress within a frame | Temporary disk/codec cost; Cancel stops the current frame promptly | Destructive sequence replacement | Cancel removes temporary output |

## How the algorithm works

### Operators and equations

Let `S` be the set of active offsets in the binary structuring element, centered
on its middle cell, and let `X_c(p)` be channel `c` at pixel `p`.

- Erosion computes `E_c(p) = min_{q in S} X_c(p+q)`.
- Dilation computes `D_c(p) = max_{q in S} X_c(p+q)`.
- Opening computes dilation after erosion: `O = D(E(X))`.
- Closing computes erosion after dilation: `C = E(D(X))`.
- Morphological Selection computes the rank `s` of the covered samples:
  `R_c(p) = quantile_s {X_c(p+q) : q in S}`, interpolated linearly between two
  ranks. At `s=0` it equals Erosion and at `s=1` Dilation by the same element;
  at `s=0.5` over a filled square it equals the Median Filter of that size.

These are the operations executed by AfterNight's engine and verified by the
focused tests.
The current selector text says **Opening (Dilation + Erosion)** and **Closing
(Erosion + Dilation)**; those parenthetical descriptions are reversed. Selection
still dispatches standard Opening and Closing as defined above.

For Amount `a`, the shared framework then computes

`Y = X(1-a) + M(X)a`,

where `M` is the selected full morphology result. If an enabled active mask has
normalized value `w`, the final non-inverted result is

`Z = X(1-w) + Yw`.

Inverted masking swaps the weights. Amount and mask blending can therefore
produce gray transition values even from a binary source mask.

### Iterations, channels, and types

For Erosion, Dilation, or Morphological Selection, **Iterations** repeats that
primitive. For Opening and Closing, each primitive stage runs the requested
iteration count: for example, Opening at `2` erodes twice and then dilates
twice. It is not two independently completed Opening cycles. The engine repeats
a primitive up to sixteen times per call, so a larger count is split across
calls; all requested repetitions are applied.

Each stored RGB channel is processed independently.
Selecting Red, Green, or Blue processes only that plane and copies the others.
Selecting a CIE, HSL, HSV, or YCbCr component converts the image, processes one
component, and reconstructs RGB from the processed and untouched components.
Hue is circular data, but this process still takes ordinary numeric minima or
maxima; values around the hue wrap can therefore be poor candidates for direct
morphology.

The engine works on every sample type in its stored units, in single
precision, and preserves geometry, channel count, and pixel type; 32-bit
integer and 64-bit float values are rounded to single precision. A NaN or
infinite sample in an image that marks every sample valid is treated as
missing: it is left out of every neighbourhood and left in place. An image that
carries its own validity rule or mask is read under that rule.

### Borders and element resizing

The anchor is fixed at the element center. Out-of-image samples are left out,
as with OpenCV's default morphology border: they do not introduce zero-valued
erosion halos or maximum-valued dilation rims, and Morphological Selection
ranks only the element's cells that lie inside the image. Only available
in-image values determine an edge result.

Changing size center-crops when shrinking. When growing, it copies the old
element into the center and fills the new border cells with zero. For example,
changing the default filled `3×3` to size `5` leaves only the centered `3×3`
active until a preset, Fill, Invert, or manual edit changes it.

### Output, state, and failure behavior

The engine result is written into a worker image, then Amount and the active
mask are applied. A still is swapped into the target only after successful
completion. A sequence is written to a temporary replacement and committed
only after success; Cancel removes that temporary output. The engine observes
Cancel while it runs, so a morphology already running on one frame stops
promptly.

Each execution deep-copies the structuring element and captures the operator
and channel labels before starting its worker. The worker reads only that
immutable snapshot, not Qt controls or the live editable grid.

The result keeps the source image's header cards and metadata, and each
sequence frame keeps its own. Unlike some nonlinear processes, the current
implementation does not update processing-state linearity/provenance, so a
nonlinear morphology result can retain the source's prior state metadata.
Image History records operator, iterations, Amount, Selection for
Morphological Selection, and, for a colour image, ` in channel: <channel>`, but
omits structuring-element size and active-cell pattern. Preserve the grid in
project notes or a screenshot when exact repetition matters.

An engine/allocation failure is reported through the shared process error path
without committing the still. The UI constrains size, iteration, Amount, and
Selection, and disables Apply while the element has no active cell.

## Parameter interactions and starting points

| Goal | Conservative start | Expected effect | Common failure | Adjustment order |
| --- | --- | --- | --- | --- |
| Shrink a white mask | Erosion, Circle `3`, 1 iteration, Amount `1` | Boundary moves inward by the active neighborhood | Thin desired structures vanish | Element shape, size, iterations, Amount |
| Grow a white mask | Dilation, Circle `3`, 1 iteration | Boundary moves outward | Nearby regions merge | Size, iterations, Amount |
| Remove isolated bright specks | Opening, Circle/Cross `3`, 1 iteration | Small bright components disappear while larger shapes regrow | Desired small stars/details disappear | Shape, size, Amount |
| Fill small dark holes/gaps | Closing, Circle `3`, 1 iteration | Dark gaps close while larger bright shape is restored | Separate bright regions bridge | Shape, size, Amount |
| Directional connection | Dilation/Closing with Cross or Diagonal | Growth follows selected axes | Orientation bias or jagged connectivity | Preset, custom cells, iterations |
| Shaped median or soft rank | Morphological Selection, Circle `5`, Selection `0.5` | Impulses inside the element's shape are replaced by its median | Fine structure smaller than half the element disappears | Shape and size first, then Selection toward `0` or `1` |
| Softer transition | Any full result, then Amount below `1` | Mixes source and morphology | Output is no longer strictly binary | Establish full operation first, then Amount |

Kernel size and Iterations are not interchangeable. A larger element changes
which offsets participate and can encode shape/direction. More iterations
repeat the same footprint and extend its reach. Start with one iteration so the
element's effect is visible before compounding it.

## Inputs, outputs, and metadata

The input is the selected grayscale or RGB still/sequence plus its optional
active mask. The output replaces that target and preserves its width, height,
channel count, pixel type, and copied metadata. Derived-channel selection may
convert and reconstruct RGB, while RGB itself is processed independently per
stored channel. The process does not create or assign a mask.

Still Apply commits only after the worker result, Amount blend, and active-mask
blend succeed. Sequence Apply commits a temporary replacement only after all
frames succeed, and each output frame explicitly receives its input frame's
metadata. Current processing-state metadata is preserved without a new
morphology provenance marker. History omits the element size and pattern.

## Performance

Work grows with pixel count, processed channels, active element footprint, and
Iterations. Opening and Closing run both erosion and dilation stages. A `25×25`
element at 20 iterations is substantially more expensive than the `3×3`, one-
iteration default, and derived color components add conversion/reconstruction.
Morphological Selection ranks every covered sample and costs more than a
minimum or maximum. No fixed timing is claimed.

Amount `0` does not skip morphology. Disable Preview while making many grid
edits, use Preview area for tuning, and return to a representative full-frame
preview before Apply. Sequence processing also pays temporary output/codec and
disk costs. Run on a short duplicate sequence first; the current page has no
approved production timing, peak-memory, or disk-space measurement.

## Worked example

- Evidence mode: Source/test/equation-derived.
- Input condition/provenance and settings: duplicate binary grayscale mask with
  no active mask, one isolated white pixel away from the border, default filled
  `3×3` element, one iteration, and Amount `1` unless noted.
- Expected checkpoint: Erosion and Opening remove the isolated white pixel;
  Dilation produces a `3×3` white block; Dilation with Amount `0.5` produces
  half-strength values where white was added. On a separate solid white field
  with one isolated black pixel, Closing fills the hole.
- Inspect at 100%/statistics/history: use 100% zoom to count the Dilation block,
  use Image Statistics to distinguish binary from half-strength output, and
  verify operator, iterations, Amount, and channel in Image History. Record the
  grid separately because history omits its size and active-cell pattern.
- Limitations: these deterministic checkpoints follow AfterNight's source,
  equations, and focused engine tests; they do not establish astronomical
  visual quality, sequence output quality, production timing, memory use, or
  disk use.

## Warnings and limitations

- The menu route disagrees with the process registry's Filters category; the
  visible menu is **Morphology**.
- Opening/Closing selector parentheticals are reversed. Trust the operation
  name and the standard order documented on this page.
- Structuring-element size/pattern is absent from history. Per-frame source
  metadata is preserved, but processing-state provenance is not updated.
- An all-zero grid disables Apply. NaN and infinity are treated as missing and
  stay in the result.
- Morphological Selection between two ranks interpolates, so its result need not
  be one of the covered samples; a binary mask can gain intermediate values.

## Troubleshooting

| Symptom | Likely cause | Fix |
| --- | --- | --- |
| Opening/Closing seems opposite to the label | The current parenthetical UI descriptions are reversed | Trust the operation name: Opening is erosion→dilation; Closing is dilation→erosion |
| Larger size barely changes the result | Size growth preserved the centered old cells and added inactive rim | Select a preset or **Fill grid** after changing Size |
| Apply is disabled | Every grid cell is inactive | Activate at least the center cell, or choose a preset |
| Preview or Apply reports a morphology error | Allocation or engine processing failed | Retry on a duplicate with a smaller element |
| Selection has no effect | An operator other than Morphological Selection is chosen | Select **Morphological Selection** |
| Result is gray instead of binary | Amount is below `1` or an active mask blended source/result | Set Amount `1` and disable/remove the target's active mask |
| RGB edges become colored | Stored RGB channels were morphed independently, or one derived component was reconstructed | Inspect **View Channel**; choose a luminance/lightness component when appropriate and check gamut |
| Border structures change unexpectedly | The centered element is partly outside the image | Inspect at 100%; remember missing samples are neutral, not reflected/replicated pixels |
| Apply settings differ from Image History | History omits element size/pattern | Record the grid before Apply; do not rely on history alone |
| NaN or infinity survives | Non-finite samples are treated as missing and left in place | Repair them first if the result must be finite |

## Related workflow steps

- Draw a custom grayscale region with [Mask Drawing](mask-drawing.md).
- Convert a soft mask to a hard classification with [Binarize](binarize.md).
- Generate an automatic stellar selection with [Star Mask](../star-operations/star-mask.md).
- Assign, invert, and enable the result through [Masks](../../interface/masks.md).
- Inspect the incomplete operation record in [Image History](../../interface/image-history.md).

## References and provenance

- AfterNight source and focused tests are the
  authority for current UI and integration behavior.
- [OpenCV image-filtering reference](https://docs.opencv.org/4.x/d4/d86/group__imgproc__filter.html)
  defines the erosion, dilation, morphologyEx, center-anchor, iteration, and
  default-border behavior the engine's morphology follows.
- AfterNight source and focused tests.
