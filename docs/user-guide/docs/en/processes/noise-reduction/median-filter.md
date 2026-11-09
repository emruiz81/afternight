---
kind: process
stage: noise
menu: Process > Filters > Median Filter
summary: >-
  Replaces each sample with the median of its square neighborhood, blended with the source by Modulation, to remove
  impulse noise.
derived: true
---
# Median Filter

## Purpose and expected result

Median Filter replaces each sample with the median order statistic of a square
neighborhood, then blends that median with the current sample by **Modulation**.
Unlike Gaussian or bilateral filtering, neighbors receive no distance or
similarity weight: only their sorted order matters. An isolated high or low
sample can disappear when it does not occupy at least half the neighborhood.

The expected result is a same-size filtered still or replacement sequence with
impulsive defects reduced. Strong kernels or repeated iterations can erase faint
stars, shrink thin lines, flatten texture, and turn smooth gradients into
piecewise regions. Median filtering is nonlinear and is not a general substitute
for Gaussian-noise reduction, calibration, or targeted cosmetic correction.

## At a glance

| Item | Current behavior |
| --- | --- |
| Menu path | **Process > Filters > Median Filter** |
| Enabled when | An image or sequence is active, the same process is not already attached to it, and no attached process holds exclusive access |
| Inputs | One grayscale or RGB still or sequence; a still CFA mosaic is treated as one grayscale sample plane unless debayered first; an optional active mask must match target geometry |
| Outputs | Destructively replaces a still or, after successful temporary processing, a sequence; **Take snapshot** creates an auxiliary still; dimensions, channel count, and source pixel type are retained |
| Recommended image state | A calibrated image with sparse impulse defects, or a mask that needs deliberate order-statistic cleanup; inspect stars and thin structures at 100% |
| Preview | Asynchronous full-image or movable-area preview, original/result switching, snapshot, display-channel selection, and display-only autostretch; with no area drawn, a slider held runs on the part the preview view shows at the image's own scale, its window's reach around it, exactly the pixels **Apply** writes there |
| Sequence support | Yes; one captured kernel/iteration/modulation/channel/mask state is applied frame by frame to temporary storage before replacement |
| Mask support | Yes; the active mask blends the fully filtered result with the original after channel reconstruction |
| External dependency | None; the AfterNight engine's own order-statistic filter, using fixed sorting networks or a general rank selection, on the operation's granted threads |

## Before you start

- Remove systematic hot/cold pixels during calibration or Cosmetic Correction
  when possible. Median Filter is most appropriate for residual impulse-like
  defects, not broad random noise.
- Duplicate the target or verify Image History. Apply replaces the target only
  after the still operation or temporary sequence succeeds.
- Compare at **Zoom 100%** with the same display stretch. Include isolated faint
  stars, narrow diffraction spikes, dust lanes, mask edges, and frame borders.
- Debayer a CFA mosaic first unless mixing unlike color-filter positions inside
  one median neighborhood is intentional.
- Know where NaN and infinity samples are, such as the empty borders of a
  registered or stacked frame. Median Filter treats them as missing data: they
  are left out of every neighbourhood and left where they are, so they stay in
  the result.
- Start with `3x3`, one iteration, and Modulation below `1` when preservation is
  more important than complete impulse removal.

## Procedure

1. Select the target and open **Process > Filters > Median Filter**. Confirm
   **Target Image**, choose **RGB** or one deliberate component, and leave
   **Preview** selected.
2. Start with **Kernel Size 3x3**, **Iterations 1**, and **Modulation 1.00** for
   isolated hot/cold samples. Reduce Modulation if small legitimate structures
   change with the defects.
3. Toggle **Switch image** and inspect at **Zoom 100%**. A removed speck is not
   automatically noise: verify faint stars and line-like structures separately.
4. Increase Kernel Size only when the unwanted structure is wider than one or
   two pixels. Increase Iterations only after checking the output of each
   additional nonlinear pass.
5. Inspect all frame edges. Boundary neighborhoods are clipped rather than
   reflected, so their sample count and median can differ from the interior.
6. If localized protection is needed, verify the active mask and inversion
   state. Check the full frame, target, Channel, and mask before **Apply**.
7. For a sequence, monitor frame progress. Cancel stops the filter between
   groups of rows; the original sequence remains until all temporary output
   completes successfully.

## Complete parameter and action reference

| UI label/action | Type and unit | Runtime default | Persisted-default exception | Valid values, step, or constraints | Effect of increasing, decreasing, or selecting | Dependencies and enable/visibility rules | Quality, artifact, runtime, memory, or disk trade-off | Scope: preview/apply, channel, mask, still/sequence | Persistence |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Kernel Size | Combo box, square pixels | `3x3` | None | `3x3`, `5x5`, `7x7`, `9x9`, `11x11`, `13x13`, `15x15`; odd sizes only | Enlarges the square order-statistic neighborhood | Always enabled while idle | Larger sizes remove wider structures but erase more stars/lines and raise median-selection cost steeply | Preview/Apply; selected channel/component; still/sequence; before Modulation/mask | Captured per execution; Reset and successful Apply retain the current selection |
| Iterations | Integer value plus slider, passes | `1` | None | `1..10`; step `1` | Feeds each pass into the next; more passes move the image toward a median-filter root rather than merely increasing one kernel | Always enabled while idle | Cost is approximately proportional to Iterations; repeated nonlinear passes can grow flat zones and remove thin detail | Preview/Apply; selected channel/component; still/sequence; before mask | Captured per execution; Reset and successful Apply return `1` |
| Modulation | Double value plus slider, per-pass median fraction | `1.00` | None | `0.00..1.00`; spin step `0.05`; default display precision from `QDoubleSpinBox`; slider resolution `0.001` | Each pass writes `current + Modulation × (median-current)`; `0` is identity and `1` writes the full median | Always enabled while idle; Apply remains enabled at `0` | Lower values soften each pass but do not avoid median calculation; with multiple iterations the blend is reapplied to the previous output | Preview/Apply; selected channel/component; still/sequence; before mask | Captured as double; Reset and successful Apply return `1.00` |
| Target Image | Image selector | Attached/active target | None | Open eligible image or sequence | Changes source, Channel list, Preview, active mask, and Apply destination | Active image required; duplicate/exclusive gates apply | Wrong target is destructively replaced after success | Preview/Apply source and active mask | Target lifecycle; Reset does not change it |
| Channel | Combo box | **RGB** for RGB; disabled **Grayscale** for mono | None | **RGB**, **Red**, **Green**, **Blue**, **CIE L* (Lightness)**, **CIE a***, **CIE b***, **CIE c***, **CIE h***, **CIE X**, **CIE Y (Luminance)**, **CIE Z**, **HSL Hue**, **HSL Saturation**, **HSL Lightness**, **HSV Hue**, **HSV Saturation**, **HSV Value**, **YCbCr Luma**, **YCbCr Blue Chroma**, **YCbCr Red Chroma** | RGB computes a separate scalar median in each stored channel; a component filters one plane and reconstructs/copies the others | Component choices require RGB; mono selector disabled | Independent RGB medians can select values from different source pixels and shift hue; component conversions add buffers and can alter color | Preview/Apply; active mask post-filter; still/sequence | Captured by execution; Reset and successful Apply retain the current selection |
| Preview | Checkbox | Checked | None | Checked/unchecked | Enables/disables asynchronous full-image or selected-area execution | Process idle | Runs the complete selected iteration count and allocates work buffers | Preview only; mask-aware; current still/current displayed sequence frame | Window lifetime; Reset does not change it |
| Preview pane | Image viewer | Current result | None | Pan with scroll bars/dragging; toolbar actions below | Shows the mask-aware result without modifying the target | Exists only with Preview | Holds source/result and display buffers in addition to median work buffers | Preview only | Window lifetime |
| Fit To Window | Preview-toolbar action | Preview fitted when first displayed | None | Click or **Ctrl+Shift+F** | Fits the whole image uniformly in the viewport | Preview image required | Good overview; resampling can hide one-pixel defects | Preview display only | Viewer lifetime |
| Zoom In | Preview-toolbar action | Current scale | None | Click; `1.25×` relative zoom within viewer limits | Enlarges around viewport center | Preview image required | Improves local inspection; more panning | Preview display only | Viewer lifetime |
| Zoom Out | Preview-toolbar action | Current scale | None | Click; `0.8×` relative zoom within viewer limits | Reduces around viewport center | Preview image required | More context, less pixel-level detail | Preview display only | Viewer lifetime |
| Zoom 100% | Preview-toolbar action | Current scale | None | Click; absolute scale `1.0` | Shows the viewer's one-pixel scale | Preview image required | Preferred for judging impulse removal, star loss, thin-line changes, and flat zones | Preview display only | Viewer lifetime |
| View Channel | Preview-toolbar combo box | **RGB** for RGB; disabled **Grayscale** for mono | None | **RGB**, **Red**, **Green**, **Blue**, **CIE L* (Lightness)**, **CIE a***, **CIE b***, **CIE a*=R / b*=G (Chrominance)**, **CIE c***, **CIE h***, **CIE X**, **CIE Y (Luminance)**, **CIE Z**, **HSL Hue**, **HSL Saturation**, **HSL Lightness**, **HSV Hue**, **HSV Saturation**, **HSV Value**, **HSI Hue**, **HSI Saturation**, **HSI Intensity**, **YCbCr Luma**, **YCbCr Blue Chroma**, **YCbCr Red Chroma** | Selects only the displayed stored/derived component | Separate from process Channel; RGB target required for components | Reveals channel artifacts without changing pixels | Preview display only | Viewer lifetime; not in process history |
| Display Mode | Preview-toolbar combo box | Inherits target **Linear** or **Autostretch** when preview is created | None | **Linear**, **Autostretch** | Changes display only; Autostretch uses cached full-target source statistics | Autostretch enables settings action | A strong stretch can exaggerate flat zones and residual specks | Preview display only | Preview-view state; not in process history |
| Autostretch Settings | Preview-toolbar dialog action | Disabled in Linear mode; inherits target-view settings | Shared-view values load when opened | Opens Target Background, Shadows Clipping, linked channels, neutralization/color calibration, saturation/background, brightness, contrast, banner, Reset, and Close; **Apply Stretch** is hidden | Tunes preview display only | Autostretch mode only; disabled while dialog open | Can change visual judgment without changing filtering | Preview display only | Viewer-scoped; not in Median Filter parameters |
| Switch image | Preview-toolbar action | Filtered preview shown | None | Click; **Space**; right-click; **Ctrl+Shift+O** | Toggles result/original | Preview required | Comparison only; no recomputation | Preview display; still/current frame | Preview-window lifetime |
| Preview area | Preview-toolbar checkbox | Checked with an initial `600×600` area when the preview is created | None | Checked/unchecked; move/resize overlay | When checked, processes the selected area and composites it over the original preview; uncheck for full-image preview | Preview required | Default area reduces tuning cost; full Apply remains full-frame | Preview and Take snapshot; mask cropped consistently | Preview-window lifetime; Reset unchanged |
| Take snapshot | Preview-toolbar action | No snapshot | None | Click or **Ctrl+Shift+K** | Re-executes settings and opens `<target> - Median Filter - SNAPSHOT` | Preview required | Adds a complete execution and auxiliary image; display stretch is not baked in | New still; target unchanged; area/mask honored | Each action creates a separate image view |
| Apply | Button/action | Enabled while idle, including Modulation `0` | None | One action | Captures kernel, iterations, modulation, Channel, and mask, then processes the full still or every sequence frame | Target must lock; all controls disabled during commit | Cost scales with pixels, channels, iterations, and neighborhood selection; component conversion, mask, sequence encode, and temporary storage add cost | Destructive still/sequence replacement after success | Records history, then resets Iterations/Modulation but retains Kernel/Channel |
| Reset | Button/action | Enabled while idle | None | One action | Restores Iterations `1` and Modulation `1.00` only | Does not reset Kernel, Channel, Target, Preview, mask, or preview toolbar | Checked Preview schedules a new full-median execution | Configuration only | Immediate; also invoked after successful Apply |
| Close | Button/action | Enabled while idle | None | One action | Closes without applying current settings | Disabled during Apply | Active preview cancellation is requested and reaches the filter between groups of rows | Window/execution | No process settings persist after destruction |
| Sequence progress / Cancel | Shared per-process/frame progress and Cancel | Created for sequence Apply | None | Cooperative Cancel; frame/task text updates around temporary processing | Sequence target only | The filter checks for Cancel between groups of rows at every kernel size and type, so a frame stops promptly | Sequence Apply; captured settings and mask apply to every frame | Cancel/error removes temporary output; success replaces sequence, records one history step, and resets Iterations/Modulation |

## How the algorithm works

For a neighborhood containing sorted samples
`x(1) <= x(2) <= ... <= x(n)`, AfterNight selects the middle sample when `n`
is odd. When a clipped boundary neighborhood contains an even count, it uses
the arithmetic mean of the two middle samples. For the current sample `p`,
median `m`, and Modulation `a`, each pass writes

\[
p' = p + a(m-p) = (1-a)p + am.
\]

The next iteration uses the complete previous result, so Modulation is applied
inside every iteration rather than once after all median passes. Median filtering
is nonlinear: doubling an image, adding images, or combining two passes cannot
in general be reduced to a single equivalent convolution.

The `3x3`, `5x5`, `7x7`, and `9x9` interiors use fixed comparison networks.
The `11x11`, `13x13`, and `15x15` windows -
up to 225 samples - use the engine's general rank selection, and boundary
pixels at every size sort their smaller clipped window directly. Both routes
select the same sample, so the choice affects speed only.

### Boundaries, channels, and precision

AfterNight does not pad or mirror Median Filter neighborhoods. It discards
out-of-frame coordinates. A `3x3` corner therefore uses four samples, while an
interior pixel uses nine. The even corner median is the average of its second
and third sorted values. This avoids invented pixels but makes the effective
support smaller and potentially asymmetric at edges.

Each stored RGB channel is filtered independently. The red, green, and blue
median values at one output position can therefore come from different source
pixels. A selected derived component is extracted, filtered as one plane, and
then reconstructed with the other components by the shared Channel path. A
derived component's reconstruction saturates an integer image at its type's
range and keeps a floating image's values outside `[0, 1]`.

Every sample type is filtered in its own stored units: samples are read into a
single-precision working plane, ranked and blended there, and written back in
the source type, rounded to the nearest code for an integer image. All eight
sample types, signed 8-bit included, keep their type. For 32-bit integer and
64-bit float images, single precision cannot distinguish every adjacent stored
value, so the median of nearly equal samples is rounded to the nearest value
single precision can hold.

A NaN or infinite sample in an image that marks every sample as valid is
treated as missing: it is left out of every neighbourhood that would include
it, and its own position is not filtered, so it stays where it was. The median
of a window with missing samples is taken over the samples that remain, like a
clipped edge window. An image that carries its own validity rule or mask is
read under that rule instead.

If a mask is active, the shared path blends the fully iterated, reconstructed
result with the source after Median Filter's own per-pass Modulation.

## Parameter interactions and starting points

- **Kernel Size × structure width** controls which structures can become a
  minority. A one-pixel impulse is outvoted in `3x3`; a compact star or thin line
  can be outvoted for the same reason.
- **Iterations × Modulation** is recursive. With a stable zero median and
  Modulation `0.25`, a unit impulse becomes `0.75` after one pass and `0.5625`
  after two, not `0.50`.
- **Modulation 0** returns the current sample on every pass but still performs
  the neighborhood median work. It is not a runtime bypass and Apply stays
  enabled.
- **Channel × mask** determines the final scope. RGB filters channels
  independently; a component is reconstructed first; the active mask blends
  that result afterward.
- **Kernel × boundary** changes sample count. At a corner, increasing from
  `3x3` to `5x5` grows a clipped `2x2` neighborhood to `3x3`, not to 25 samples.
- **Pixel type × weak modulation** matters because an integer image's result is
  rounded to whole codes, while float images retain fractions.
- **Non-finite samples × neighbourhood** behaves like a hole: windows next to a
  NaN rank fewer samples, and the NaN itself is kept.

## Inputs, outputs, and metadata

- Output dimensions, channel count, and sample type are retained. The engine
  writes a separate result, not the target storage in place.
- The result keeps the source image's header cards and metadata.
- Median Filter does not update linear/nonlinear processing-state provenance.
- Still Image History records `Kernel size <selection> iterations <n>
  modulation <value>` and, for a colour image, ` in channel: <channel>`.
- A still CFA mosaic is treated as one plane. Sequence preprocessing may
  auto-debayer configured CFA frames before replacement.
- Sequence frames use one captured Kernel/Iterations/Modulation/Channel state,
  write temporary output, and replace the original sequence only after success.
  Cancel/error removes temporary output.
- **Take snapshot** creates a still from the current preview scope without
  changing the target or baking display autostretch into pixels.

## Performance

For image width `W`, height `H`, processed channels `C`, and iterations `I`, the
outer work is `W × H × C × I`. Median selection adds neighborhood-dependent
cost. The fixed `3x3` through `9x9` networks have increasingly large, fixed
comparison counts; `11x11` through `15x15` gather `K²` samples per pixel into a
bounded scratch buffer and select the rank, with work roughly proportional to
the gathered count. Larger kernels are therefore substantially more expensive
even though the window radius is capped at seven pixels.

The engine takes its working planes once per run - one more for more than one
iteration - plus one window buffer per group of rows, and filters on the
threads the operation is granted. Preview, derived-component conversion, masks,
destination images, and display add more buffers. Sequence processing also
needs temporary disk roughly comparable to an output sequence. No approved
astronomical workload has been timed.

## Worked example

These normalized float examples follow the current source equations. They were
not run on an approved astronomical image and make no visual-quality claim.

```text
3x3 neighborhood: 0 0 0 / 0 1 0 / 0 0 0
sorted values:     0 0 0 0 0 0 0 0 1
median:            0

Modulation 1.00, one iteration: center 1 -> 0
Modulation 0.25, one iteration: center 1 -> 0.75
Modulation 0.25, two iterations while surrounding median stays 0: 1 -> 0.75 -> 0.5625
```

For a `3x3` top-left corner whose clipped `2x2` samples sort to
`0, 0.2, 0.8, 1`, the boundary median is `(0.2 + 0.8) / 2 = 0.5`. No reflected
or repeated samples participate.

## Warnings and limitations

- Median Filter targets impulse-like outliers. It can leave Gaussian noise with
  a granular or blotchy appearance while removing real small structures.
- It is often described as edge preserving, but that is conditional. Corners,
  thin lines, close double stars, small stars, and texture can move or disappear
  when their samples are a neighborhood minority.
- RGB channels are independent, so channel medians can originate at different
  locations and create hue changes. Use a deliberate luminance/component path
  when chromatic geometry must remain coupled, and verify reconstruction.
- Boundaries are clipped. Their smaller/even neighborhoods do not match a
  reflected, replicated, or constant-padding median implementation.
- Repeated passes are not one larger median and can grow flat zones or converge
  toward a median root image.
- Modulation `0` is visually identity but does not skip the expensive filter.
- Filtering runs in single precision. 32-bit integer and 64-bit float images
  can lose precision between nearly equal samples.
- NaN and infinity are treated as missing: they are not filtered and remain in
  the result. Repair them first if the output must be finite.
- Successful Apply resets Iterations and Modulation only. Kernel, Channel,
  Target, Preview, mask, and preview-toolbar state remain.

## Troubleshooting

| Symptom | Likely cause | Check or correction | Evidence |
| --- | --- | --- | --- |
| Faint stars disappear with hot pixels | Both are small neighborhood minorities | Reduce Kernel/Iterations or Modulation; use Cosmetic Correction or a protection mask | Order-statistic rule |
| Smooth background becomes patchy | Repeated nonlinear passes create flat regions | Reduce Iterations first, then Kernel or Modulation | Recursive pass path |
| Border differs from the interior | Neighborhood coordinates are clipped | Inspect the full frame; do not assume reflection padding | Truncated-window border |
| Colors shift around small features | RGB channels select medians independently | Try a luminance component and inspect chroma channels before Apply | Shared Channel loop |
| Modulation `0` is still slow | Blend is identity, but median selection still runs | Disable Preview or close the process when no effect is wanted | Per-pass blend after rank selection |
| NaN or infinity survives filtering | Non-finite samples are treated as missing and left in place | Repair or reject non-finite samples first if the result must be finite | Missing-sample rule |
| Kernel remains after Reset | Reset changes only Iterations and Modulation | Re-select `3x3` manually if needed | `resetSettings()` |
| Preview differs from Apply | Preview area, mask, target/channel, or display stretch changed | Match all controls and one Display Mode; Apply always processes the full target | Shared preview/process path |
| Large kernel is slow | `11x11` and wider windows use the general rank selection over up to 225 samples per pixel | Use a smaller kernel or a Preview area while tuning; Cancel stops between groups of rows | Rank selection cost |

## References and provenance

- [Huang, Yang, and Tang, “A Fast Two-Dimensional Median Filtering Algorithm,” IEEE TASSP 27(1), 1979](https://doi.org/10.1109/TASSP.1979.1163188)
  is a primary reference for two-dimensional median filtering and its
  neighborhood-order-statistic model.
- AfterNight's engine order-statistic filter is authoritative for the current
  sorting paths, clipped boundaries, per-pass modulation, missing-sample rule,
  working precision, and cancellation.

## Related workflow steps

- [Bilateral Filter](bilateral-filter.md) adds distance and sample-difference
  weights instead of selecting a pure order statistic.
- [Gaussian Blur](gaussian-blur.md) is a linear smoother that weights every
  neighbour by distance instead of selecting one.
- [Cosmetic Correction](../calibration/cosmetic-correction.md) is preferable for
  known hot/cold defects that should not alter every neighborhood.
- [Process Windows](../../interface/process-windows.md), [Image Viewer](../../interface/image-viewer.md),
  [Masks](../../interface/masks.md), and [Image History](../../interface/image-history.md)
  explain the shared controls and verification workflow.
