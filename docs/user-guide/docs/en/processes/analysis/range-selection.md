# Range Selection

## Purpose and expected result

Range Selection creates a new mask image from an inclusive normalized intensity
interval. It can produce a hard binary selection, soften its two boundaries,
retain the selected source intensity through Screening, blur the result, and
invert it. On a color target, the initial **Lightness** setting generates one
grayscale mask from CIE L*; with Lightness off, each stored channel is
thresholded independently.

The source target is not replaced. A successful run opens a neighboring image
or sequence named `<target-name> - MASK` and closes the process window.

## At a glance

| Item | Current behavior |
| --- | --- |
| Menu path | **Process > Mask Generation > Range Selection** |
| Enabled when | An image or sequence is active, this process is not already attached to it, and no attached process holds exclusive access |
| Inputs | One grayscale or color still, or a sequence, in any integer or floating sample type. Values are read as fractions of their code range: an unsigned integer as a fraction of its largest code, a signed one counted from its smallest code, a floating sample as stored. An enabled mask on the target is ignored |
| Outputs | A new same-size image or sequence named `<target> - MASK`. Color plus Lightness produces a one-channel floating grayscale mask; otherwise the mask keeps the target's channel count and sample type |
| Recommended image state | A finite image whose range of interest is already on the intended normalized scale; there is no automatic normalization, background measurement, or threshold estimator |
| Preview support | Asynchronous preview: while a control is dragged, over what the preview view shows - the image reduced to the view's size, smoothed by Smoothness divided by the reduction, or the part shown at 1:1 and closer with the smoothing's reach around it - and the preview you rest on over the whole image, computed exactly as OK computes it; display autostretch, original/result switch, snapshot, zoom, display-channel selection, and **Preview area**, which shows the mask of the area alone over the image |
| Sequence support | Supported: every output frame is computed from its own source frame and written to a new temporary sequence in the target's container |
| Mask support | None: an enabled mask on the target weighs no preview, snapshot, OK result, or sequence frame, so the result is always the pure range mask |
| External dependencies | None for the user; the selection and the Gaussian are part of AfterNight's engine |

## Before you start

Decide whether the selection should follow CIE L* or individual stored channels.
Keep **Lightness** checked for a single perceptual-lightness mask from a color
image. Turn it off only when a mask with the target's channels is intentional.
For grayscale inputs, Lightness makes no difference.

A mask enabled on the target does not need to be disabled first. The
selection is a mask of its own, so Range Selection ignores the target's mask
and always produces the pure range mask.

## Procedure

1. Activate a representative image or sequence and open
   **Process > Mask Generation > Range Selection**.
2. Confirm **Target Image** and keep **Preview** checked. For a quick look at a
   large image, check **Preview area**; the area shows what OK would make of
   that area alone. For color, decide whether the output should use initial
   **Lightness** (one grayscale mask) or per-channel thresholds (color mask).
3. Set **Lower limit** and **Upper limit** around the samples to select. Both
   endpoints are included. Use Image Statistics or pixel readout to choose
   values; the process does not measure the background for you.
4. If you need to slide a fixed-width interval, move either endpoint away from
   the full `0..1` range, then check **Link Range Limits**. The link preserves
   `Upper-Lower` while clamping the interval at 0 and 1.
5. Raise **Fuzziness** to replace hard edges with linear boundary ramps. Raise
   **Smoothness** only after the interval is correct; it applies a Gaussian blur
   of that standard deviation, in pixels, to the entire generated result.
6. Enable **Screening** when selected pixels should retain their source
   intensity (multiplied by the fuzzy weight) instead of becoming white. Enable
   **Invert** only after judging the non-inverted selection; inversion occurs
   after smoothing.
7. Compare **PREVIEW** and the original with **Switch image** under the same
   display mode. Inspect the mask in Linear view and at 100% as well as fitted.
   Uncheck **Preview area** to judge the whole frame before OK.
8. Select **OK**. Wait for the new `<target> - MASK` image or sequence to open.
   The original remains unchanged and the process closes after success.

## Complete parameter and action reference

| UI label/action | Type and unit | Runtime default | Persisted-default exception | Valid values, step, or constraints | Effect of increasing, decreasing, or selecting | Dependencies and enable/visibility rules | Quality, artifact, runtime, memory, or disk trade-off | Scope: preview/apply, channel, mask, still/sequence | Persistence |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| **Lower limit** | Double + slider, normalized | `0.000000` | None | `0..1`; 6 decimals; spin step `0.05`; linked slider 0..1000 | Raising excludes darker samples and moves the lower fuzzy ramp upward | If moved to/above Upper, Upper is moved to the same value; disabled during OK | Narrower selection; equality selects only samples exactly equal to the shared limit | Preview/OK; every selected source channel | Captured per run; Reset `0`; a reopened window starts at `0` |
| **Upper limit** | Double + slider, normalized | `1.000000` | None | `0..1`; 6 decimals; spin step `0.05`; linked slider 0..1000 | Lowering excludes brighter samples and moves the upper fuzzy ramp downward | If moved to/below Lower, Lower is moved to the same value; disabled during OK | Narrower selection; equality is an exact-value selection even when Fuzziness is positive | Preview/OK; every selected source channel | Captured per run; Reset `1`; a reopened window starts at `1` |
| **Link Range Limits** | Checkbox | Unchecked and disabled at `[0,1]` | None | Off/on after either endpoint leaves full range | Preserves the captured width `Upper-Lower` while either endpoint moves; clamps the interval at `[0,1]` | Disabled for exact full range and during OK; it is UI-only and does not enter the engine equation | Faster interval scanning; no output cost | Parameter editing only | Captured but unused by the engine; Reset off |
| **Fuzziness** | Double + slider, fraction of selected interval | `0.00` | None | `0..1`; 2 decimals; spin step `0.05`; slider 0..1000 | For `Upper>Lower`, replaces hard interval edges with two linear ramps, each width `(Upper-Lower)Fuzziness/2`; `1` forms a triangle peaking at interval midpoint | Disabled during OK; when limits are equal, the ramp width is zero and the result remains an exact-value selection | Softer transitions reduce hard boundaries; larger values reduce the full-weight plateau only for a nonzero interval | Preview/OK; per channel before Smoothness | Captured per run; Reset `0` |
| **Smoothness** | Double + slider, Gaussian standard deviation in pixels | `0.00` | None | `0..32`; 2 decimals; spin step `1`; slider 0..320 | Applies a wider exact Gaussian blur as it rises; `0` skips the blur | Disabled during OK; runs after range/Screening and before Invert | Broader halos and selection bleed; work grows with the deviation (the blur reaches four deviations); mirror boundaries; integer destinations quantize | Whole generated image, all channels | Captured per run; Reset `0` |
| **Screening** | Checkbox | Unchecked | None | Off/on | Off outputs the selection weight; on outputs `source × weight` | Disabled during OK | On preserves intensity gradients instead of a conventional white mask plateau | Per channel before smoothing/inversion | Captured per run; Reset off |
| **Lightness** | Checkbox | **Checked** | None | Off/on | For a color target, checked selects over CIE L* as one plane and produces a floating grayscale mask; unchecked thresholds each stored channel separately | No effect for grayscale; disabled during OK | Preview and OK use the same conversion; adds a full-size lightness plane | Source preparation before threshold; still, preview area, or each sequence frame | Captured per run; Reset on |
| **Invert** | Checkbox | Unchecked | None | Off/on | Replaces final normalized output `y` with `1-y` | Disabled during OK | Reverses selected/protected meaning after blur; a sample that held no number reads as unselected, white without Smoothness | Whole output after Smoothness | Captured per run; Reset off |
| **Target Image** | Image/sequence selector | Attached active target | None | Any open target accepted by launch guards | Changes the source and output name | Active target required; disabled during OK | Wrong target creates the wrong mask | Preview/OK; still or sequence | Target lifetime; Reset unchanged |
| **Preview** | Checkbox | Checked | None | Off/on | Creates/destroys live asynchronous preview updates | Available while idle; disabled during OK | Checked reruns the selection, conversion, and blur after edits: over what the preview view shows while a control is dragged, then over the whole image (or the area) | Preview only; the target's mask is not applied | Window lifetime; Reset unchanged |
| **Preview pane** | Image viewer | Full-frame result; execution autostretch supported | None | Standard pan/scroll and actions below | Displays the generated result without changing the target | Exists while Preview is checked | Holds source/result/display buffers; Lightness adds another plane | Preview only | Window lifetime |
| **Fit To Window** | Preview-toolbar action | Preview fitted initially | None | Click or **Ctrl+Shift+F** | Fits the whole image | Preview image required | Broad context; resampling can hide small edge artifacts | Display only | Viewer lifetime |
| **Zoom In** | Preview-toolbar action | Current scale | None | Click; `1.25×` relative zoom | Enlarges around viewport center | Preview image required | Better local boundary inspection | Display only | Viewer lifetime |
| **Zoom Out** | Preview-toolbar action | Current scale | None | Click; `0.8×` relative zoom | Reduces scale | Preview image required | More context, less pixel detail | Display only | Viewer lifetime |
| **Zoom 100%** | Preview-toolbar action | Current scale | None | Click; absolute scale `1.0` | Shows one-pixel viewer scale | Preview image required | Preferred for ramps, blur, quantization, and channels | Display only | Viewer lifetime |
| **View Channel** | Preview-toolbar combo box | **Grayscale** for a Lightness mask; RGB/derived choices where channels allow | None | Stored and supported derived RGB/CIE/HSL/HSV/HSI/YCbCr display components | Changes only the displayed component | List follows preview result channel layout; separate from Lightness processing | Helps detect accidental per-channel color masks | Display only | Viewer lifetime |
| **Display Mode** | Preview-toolbar combo box | **Linear** | None | Linear or Autostretch | Linear shows stored mask values directly; Autostretch derives a display presentation from preview statistics | Preview active | Autostretch can conceal the mask's absolute range and clipping | Display only; generated samples are unchanged | Preview lifetime |
| **Autostretch Settings** | Preview-toolbar action | Initialized from the target view | None | Opens shadow clipping, target background, saturation, brightness, and contrast settings | Changes only Autostretch presentation/statistics | Preview active; relevant when Display Mode is Autostretch | Adds display computation; does not change generated mask values | Display only | Preview lifetime |
| **Switch image** | Preview-toolbar action | Result shown | None | Click, **Space**, right-click, or **Ctrl+Shift+O** | Toggles generated result and original target | Preview required | Comparison only; original may be color while a Lightness result is grayscale | Preview | Preview-window lifetime |
| **Preview area** | Preview-toolbar checkbox | Unchecked; checking creates a centered `600×600` area, smaller when the image is | None | Off/on | Runs the process on the area's crop and shows it over the image; with Lightness on a color target, the image's luminance is shown around the area | Visible while Preview is checked | Faster preview of large images; near the area's edges Smoothness mirrors the area's own edge, so the border can differ from the same pixels of a full OK | Preview and snapshot only, never OK | Window lifetime; Reset unchanged |
| **Take snapshot** | Preview-toolbar action | No snapshot | None | Click or **Ctrl+Shift+K** | Re-executes settings and opens an auxiliary still of what the preview shows, Preview area included | Preview required; the target's mask is not applied | Adds full-size processing/allocation; the snapshot is not recorded as a range-selection result | New still; target unchanged | One view per action |
| **OK** | Dialog action | Enabled while idle | None | One click | Captures settings, locks target, creates `<target> - MASK`, closes on success | All process/shared controls disabled during run; the target's mask is not applied | Full-image conversion, mask, blur, and working copies; source remains unchanged | New still, or new sequence for a sequence target | Result recorded nonlinear with `range-selection`; its first history step has no description; dialog closes on success |
| **Cancel** | Dialog action | Enabled while idle | None | One click | Closes without creating the OK result | Disabled during OK; a still run shows no progress dialog, but closing the window or the target stops it | A stopped run makes nothing and shows no error | Dialog | Settings discarded |
| **Reset** | Dialog action | Enabled while idle | None | One click | Restores limits `0/1`, Fuzziness/Smoothness `0`, Lightness on, and Screening/Invert/link off | Leaves Target, Preview, mask, and toolbar state unchanged | Checked Preview schedules another full execution | Configuration only | Immediate |
| **Sequence progress / Cancel** | Shared process/frame progress | Created for sequence OK | None | Cooperative cancel between frames and inside each frame's work | Writes a new temporary sequence and opens it only after every frame succeeded | Sequence target only; each frame is read as the sequence view decodes it | One full selection per frame plus the sequence write | New sequence; the target's mask is not applied | Cancel or failure removes the temporary output; success opens `<target> - MASK` |

## How the algorithm works

### Conceptual pipeline

1. Read the image the run is given: the whole target for OK and for the
   preview you rest on, the area's crop for a preview area, and each frame in
   turn for a sequence. While a control is dragged, a preview reads the image
   reduced to the preview view's size - each pixel the mean of its block - and
   smooths by Smoothness divided by the reduction, or reads the part the view
   shows at 1:1 and closer with the smoothing's reach of four deviations
   around it.
2. If **Lightness** is checked and that image is color, compute its CIE L* into
   a new floating grayscale plane in the `[0,1]` range, read the way the
   process channel selector reads a color image. An integer image is first
   converted to fractions of its code range. Otherwise work on every stored
   channel of a copy of the image, an integer copy through a floating working
   copy.
3. For each channel and pixel, calculate an inclusive hard or fuzzy range weight
   into a working image that starts at zero. With Screening off, write that
   weight; with Screening on, multiply it by the normalized source sample. A
   sample that holds no number, or that the image's own validity marks as
   meaningless, is not selected and stays zero.
4. If Smoothness is positive, apply the engine's exact Gaussian blur of that
   standard deviation, with mirror boundaries. If Invert is checked, take the
   complement `1-y` of every sample afterward.
5. Without Lightness, narrow an integer result back to its own sample type, to
   the nearest code, ties to even.
6. For OK and for each sequence frame, record the result nonlinear with
   `range-selection` provenance and state every sample valid. The shared
   process framework then creates a new image or sequence instead of replacing
   the source. It applies no mask of the target.

### Selection equations

Let normalized source sample `x`, limits `L<=U`, and Fuzziness `f` be in
`[0,1]`. With `f=0`, the selection weight is

`w(x)=1` when `L<=x<=U`, otherwise `w(x)=0`.

With `f>0` and `U>L`, define

`z=(U-L)f/2`, `L'=L+z`, and `U'=U-z`.

Then

- `w=0` outside `[L,U]`;
- `w=(x-L)/z` for `L<=x<L'`;
- `w=1` for `L'<=x<=U'`;
- `w=(U-x)/z` for `U'<x<=U`.

Fuzziness `1` makes `L'=U'` at the midpoint, producing two ramps with a
full-weight midpoint. With Screening off, the pre-blur output is `w`. With
Screening on, it is `xw`. After the optional Gaussian `G_s` of standard
deviation `s`, Invert produces `1-G_s(w)` or `1-G_s(xw)`.

When `U=L`, the ramp width `z` is zero and the selection is the hard one: only
a sample exactly equal to that shared limit is selected. Screening off writes
full weight for that sample, Screening on retains the sample value, and every
other sample maps to zero. Positive Fuzziness therefore does not soften an
equal-limit selection.

`G_s` is the exact Gaussian: taps sampled from the normal density out to four
standard deviations and renormalized to sum to one, applied along rows and then
columns, with the image mirrored about its edge sample beyond the border.

### Active-mask equation

None applies. The selection is a mask of its own, so no mask of the target
weighs it: with a mask enabled on the target, a preview, a snapshot, OK, and
every sequence frame produce exactly what they produce without one.

### Boundary and failure behavior

- Limits are inclusive. The UI maintains `Lower<=Upper`, and equality is a
  valid single-exact-value selection.
- Samples are normalized according to stored sample type. There is no
  image-wide min/max normalization, histogram percentile, background estimate,
  or automatic threshold choice.
- Finite values outside `[L,U]` map to zero before smoothing and inversion. A
  sample holding no number (NaN or an infinity) is not selected either: it is
  zero before smoothing, so it can darken its neighbors through Smoothness, and
  after Invert it is white unless Smoothness spread its neighbors into it.
  Every sample of the result holds a number, and the result states every
  sample valid.
- The preview you rest on and OK run the same computation. A preview area
  runs it on the area's crop, so near the area's edges Smoothness mirrors the
  area's own edge instead of reading the pixels beyond it.
- A preview while a control is dragged over the image reduced is an
  approximation: the limits and the ramps do not commute with the reduction's
  block means, so a thin structure can be selected more or less than at full
  scale. Over the part shown at 1:1 it is the full computation there.
- A sequence run processes every frame from that frame's own samples.
- A run the engine cannot give memory to fails with **Error executing the
  process** naming the memory budget; nothing is made and the target is
  unchanged.
- Closing the process window or the target image while a run works, or
  **Cancel** in a sequence's progress dialog, stops it. A stopped run makes no
  new image or sequence and shows no error.

## Parameter interactions and starting points

| Combination | Conservative starting point | Too weak looks like | Too strong looks like | Adjustment order |
| --- | --- | --- | --- | --- |
| Lightness range mask | Lightness on, Screening off, Fuzziness `0`, Smoothness `0` | Desired structure stays black because interval misses it | Too much background/highlight becomes white | Measure sample values, set Lower, then Upper, then soften |
| Soft interval | Correct hard interval, Fuzziness `0.1`, Smoothness `0` | Abrupt contour remains | Plateau collapses toward a triangle and selection loses certainty | Limits first, Fuzziness second, Smoothness last |
| Screening | Correct interval, Screening off for comparison | Off may ignore intensity variation you wanted | On may make a dim selected region too weak as a mask | Establish weight mask, then compare Screening, then invert if needed |
| Smooth mask | Fuzziness first, then small Smoothness | Pixel noise/jagged boundary remains | Broad selection bleed and halos cross structures | Limits, Fuzziness, Smoothness, full-frame inspection |
| Inverted protection mask | Tune non-inverted first | Protected/selected meaning is reversed | Not a strength issue; wrong polarity applies later processing to wrong area | Finish selection and smoothing, then Invert once |

## Inputs, outputs, and metadata

With Lightness on a color target, the output is one channel of CIE L*
selection, stored as 32-bit floating point (64-bit floating point for a 64-bit
floating or 32-bit integer target), whatever the target's own sample type.
Otherwise the output keeps the target's width, height, channel count, and
sample type, and every channel receives its own independent selection; an
integer result is narrowed to the nearest code, so fuzzy, screened, blurred,
and inverted values are quantized. CFA input is treated as a single stored
mosaic plane; Range Selection does not demosaic a still. A sequence's frames
are read as the sequence view decodes them.

Every result is recorded nonlinear: its processing-state cards say nonlinear,
stated by a process, with the operation `range-selection`; it no longer claims
a linear encoding; and its lineage gains an entry naming `an::rangeSelectImage`
with the limits, fuzziness, smoothness, and switches used. It keeps the
target's acquisition record and plate solution. It states every sample valid,
so a validity mask the target carried does not travel with it.

The two paths differ in header cards. A Lightness mask of a color target is a
new image and carries none of the target's header cards except the
processing-state cards. Without Lightness, the mask starts as a copy of the
target, so it keeps the target's other header cards, such as its object name
and exposure, beside the updated processing-state cards. Range Selection writes
no file directly, and the new view's first history step has no description.
Use the new view as a mask only after inspecting its polarity, channel layout,
metadata, and range; assignment to another target is a separate Masks workflow.

A sequence run writes a new temporary-backed sequence in the target's
container and opens it as `<target> - MASK` only when every frame succeeded;
otherwise the temporary output is removed.

## Performance

Thresholding, Screening, and inversion are parallel per-pixel operations whose
work grows with width, height, and channel count. Every run holds a full-size
floating working image for the selection; Lightness adds a CIE L* conversion
into a plane of its own, and an integer target adds a floating working copy.
Smoothness adds two exact Gaussian passes whose work grows with the deviation.
Preview reruns after each setting change. While a control is dragged it runs
over what the preview view shows, so it costs a preview-sized image a step
whatever the image's size; the preview you rest on runs over the whole image,
or the area when Preview area is checked. A Lightness mask of a color image
shown in Autostretch at 1:1 is the exception: a part of it cannot be stretched
as the whole mask is, so every preview then runs over the whole image. No
production timing or peak-memory measurement was made.

`Test_ProcessesMasks` holds the numerical results, the preview, the new view,
metadata, every sample type, cancellation, and a memory refusal. It does not
measure time or memory.

## Worked example

- Evidence mode: Source-and-equation-derived.
- Input condition/provenance and settings: finite normalized color still,
  Lightness on, `L=0.20`, `U=0.60`, Fuzziness `0.20`, Smoothness `0`,
  Screening off, Invert off.
- Expected checkpoint: `z=(0.60-0.20)×0.20/2=0.04`; samples at `0.20`, `0.24`,
  `0.40`, `0.56`, `0.60` receive weights `0`, `1`, `1`, `1`, `0`
  respectively. A sample at `0.22` receives `0.5`. OK opens a one-channel,
  floating, same-size `<target> - MASK` still and leaves the color target
  unchanged.
- Inspect at 100%/statistics/history: inspect transitions at 100%, check mask
  minimum/maximum and channel count in Image Statistics, and confirm that no
  original-target history step was created.
- Limitations: not executed on approved astronomical data; no visual-quality,
  sequence, timing, or peak-memory claim. The numerical checkpoints follow the
  engine's equation; the captured records that `MasksRecordTest` reproduces
  cover other settings.

## Warnings and limitations

- The target's enabled mask is ignored; the result is the pure range mask.
- With Lightness on a color target, the mask is floating point whatever the
  target's sample type, and carries none of the target's header cards except
  the processing-state cards.
- Without Lightness, the mask keeps the target's header cards, which describe
  the target rather than the mask.
- A sample holding no number is not selected; after Invert it is white,
  or nearly so after Smoothness.
- A preview area is computed as an image of its own, so Smoothness near its
  edges can differ from the full OK.
- A preview while a control is dragged over a fitted view is computed on the
  image reduced to the view's size; the preview you rest on is the full
  computation.
- In Autostretch at 1:1, a Lightness mask of a color image previews over the
  whole image while a control is dragged, at the rate that allows.
- Lower=Upper selects only the exact shared-limit value; positive Fuzziness
  does not soften it.
- Screening is source-intensity multiplication, not a screen blend mode.
- There is no automatic threshold, normalization, registration, resampling, or
  statistics-driven range selection.

## Troubleshooting

| Symptom or exact error | Verified cause/validation path | Recovery | Evidence source |
| --- | --- | --- | --- |
| Range Selection is absent under Sensor Calibration | The current menu route is Mask Generation; registry category metadata uses the sensor-calibration key | Use **Process > Mask Generation > Range Selection** | `AfterNightAppUiBuilder` menu composition and registry built-in |
| Link Range Limits is disabled | Limits are exactly the full `[0,1]` interval | Move either limit inward, then check Link | `checkLinkRangeLimits()` |
| The target's enabled mask has no effect on the result | Range Selection makes a mask of its own and applies no mask of the target | Expected; combine the result with another mask afterward, for example in Pixel Math, if you need both | `RangeSelectionProcess` constructor and `MasksProcessCase` |
| **Error executing the process** naming the memory budget | The engine could not allocate the run's working images | Close other images or lower the image size, then retry | `MasksEngineRefusalTest` |
| Preview area shows grayscale around the area | With Lightness on a color target, the area shows the grayscale mask and the image around it is shown as its luminance | Expected; uncheck Preview area to see the whole mask | `buildProcessCompositeBase()` |
| Fuzziness does not soften an equal-limit range | With Lower=Upper the ramp width is zero, so the hard selection applies | Widen the interval before increasing Fuzziness | `rangeSelectImage()` ramp calculation |
| Mask lacks the target's FITS header cards | With Lightness on a color target, the mask is a new image that carries only the processing-state cards | Treat the mask as a generated auxiliary image; its acquisition record and plate solution are kept | `extractLightness()` and `derivedImageMetadata()` |
| Screening result is dim instead of white | Screening writes `xw`, not `w` | Turn Screening off for a conventional white plateau | `RangeSelectionRequest::screening` |
| A single pixel is white in an inverted mask | That source sample held no number, so it was not selected and Invert complemented its zero | Sanitize the source first if the pixel matters | `executeTypedProcessImpl()` and `skippingNonFinite()` |

## Related workflow steps

Use [Image Statistics](../../interface/image-statistics.md) to measure candidate
limits, [Binarize](../masking/binarize.md) for channel thresholds without range
ramps, and [Masks](../../interface/masks.md) to assign, enable, invert, and
inspect the generated image. Use [Pixel Math](../pixel-math/pixel-math.md) when
you need an explicit custom selection equation. Apply morphology or blur only
after verifying that Fuzziness and Smoothness do not already provide the needed
transition.

## References and provenance

This page documents AfterNight's current range-selection engine operation,
process framework, and focused tests. Its equations
are implementation contracts; no external thresholding standard is claimed. The
captured behavior records, which `MasksRecordTest` reproduces through the
window, are its numerical regression check.
