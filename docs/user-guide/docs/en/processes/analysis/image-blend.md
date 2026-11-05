# Image Blend

## Purpose and expected result

Image Blend combines a base image `A` with a same-size blend image `B` using
one of 27 pixel or color-component modes. Each input can receive a linked
midtones transfer, the blend image can receive an optional filter, and the mode
result can be attenuated by opacity and the base image's active mask. **OK**
destructively replaces the base target after a successful run; the observable
checkpoint is a new **Image Blend** history step and a nonlinear target.

## At a glance

| Item | Current behavior |
| --- | --- |
| Menu path | **Process > Utilities > Image Blend** |
| Enabled when | At least one image view is active; the process does not require exclusive window access. |
| Inputs | Base still/sequence plus one other open image of exactly equal width and height. A grey base and a colour blend image, or the reverse, blend as the engine's composition promotes them; the second image is read in its own units whatever its representation, and the result keeps the base's representation. |
| Outputs | Destructively replaces the base still or sequence after success. **Take snapshot** creates an auxiliary still. No file is written directly. |
| Recommended image state | Depends on intent. Arithmetic combination of calibrated linear data can be meaningful, but the process always marks a successful result nonlinear. Contrast/color modes, input MTFs, and color filters are normally nonlinear operations. |
| Preview support | Checked by default; asynchronous full-image preview with histogram, original/result switch, display autostretch, and snapshots. **Preview area** is supported: the second image is cropped to the same area. |
| Sequence support | Applies each base-sequence frame against the second image taken from its view when **Apply** is clicked; every frame is blended as a still image of it would be. There is no paired-frame synchronization. |
| Mask support | Yes. The active base mask is applied after mode and opacity: ordinary mask white selects the processed result, black keeps the original; inversion reverses this. |
| External dependencies | None. Gaussian Blur and High Pass are the engine's exact Gaussian, at any radius; Hue and Color are the engine's colourizing filters. |

## Before you start

Register and crop both inputs to the same pixel coordinates. The blend selector
checks dimensions only; it does not compare WCS, distortion, channels, exposure,
background, or photometric scale. Duplicate the base or save a checkpoint
because **OK** is destructive.

**Preview area** crops the blend image to the same area as the base, so an
area preview shows the blend of that area.

For linear combination, start with identity stretches (**Black Point** `0`,
**Highlights** `1`, **Midtones** `0.5`) and **Filter Type: None**. Align and
normalize externally when required; Image Blend performs neither operation.

## Procedure

1. Activate the image to be replaced and open **Process > Utilities > Image
   Blend**. Confirm it in **Base image > Image**.
2. Select an equal-size registered view in **Blend image > Image**. Its blend,
   filter, and opacity controls become enabled.
3. Leave both **Stretch** groups at identity initially. Choose **Replace** to
   verify source selection, then select the required **Blend mode**.
4. Keep **Filter Type: None** until the unfiltered equation is understood.
   Add Gaussian Blur, High Pass, Hue, or Color only to `B`, before blending.
5. Reduce **Opacity** from `1` to mix the mode result back toward original `A`.
   If the base has an active mask, remember that the mask attenuates the result
   once more after opacity.
6. Use **Preserve lightness** only when it is enabled and you want the mode result
   reconstructed with base CIE L*. Inspect gamut clipping and color at 100%.
7. Compare **PREVIEW** with **BASE IMAGE** using **Switch image** under one
   display stretch. Inspect the result histogram; display Autostretch does not
   change output samples.
8. Select **OK**. Wait for processing to finish; the base is replaced and its
   history records the mode and selected blend image.

## Complete parameter and action reference

| UI label/action | Type and unit | Runtime default | Persisted-default exception | Valid values, step, or constraints | Effect of increasing, decreasing, or selecting | Dependencies and enable/visibility rules | Quality, artifact, runtime, memory, or disk trade-off | Scope: preview/apply, channel, mask, still/sequence | Persistence |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Histogram plot | Graph, normalized intensity/count | Current preview channels | None | Bins set by Plot resolution | Shows processed, opacity- and mask-aware preview distribution | Present while process is open | Recalculation adds work | Preview display only | Recomputed |
| Histogram left-drag | Pointer gesture | Idle | None | Press/drag | Shows bin coordinate/count readout | Histogram shown | Diagnostic only | One plot | Not persisted |
| Histogram middle-drag | Pointer gesture | Idle | None | Press/drag | Pans a zoomed plot | Histogram shown | Diagnostic only | One plot | Window lifetime |
| Zoom in | Histogram action | Fitted | None | `1.30x` per click | Magnifies plot | Histogram toolbar | More scrolling | Display only | Window lifetime |
| Zoom out | Histogram action | Fitted | None | `0.70x` per click | Shrinks plot | Histogram toolbar | Less detail | Display only | Window lifetime |
| Zoom reset | Histogram action | — | None | One click | Fits plot | Histogram toolbar | Display only | Display only | Immediate |
| Show grid | Toggle | Checked | None | Off/on | Shows plot grid | Histogram toolbar | Display only | Display only | Window lifetime; Reset unchanged |
| Show clipping info | Toggle | Checked | None | Off/on | Shows endpoint counts/percentages at normalized `<=0` and `>=1` | Histogram toolbar | Detects clipping in preview samples | Display only | Window lifetime |
| Absolute scales | Toggle | Unchecked | None | Off/on | Checked normalizes every visible channel to its own peak; unchecked uses a shared peak | Histogram toolbar | Checked conceals relative peak-count strength | Display only | Window lifetime |
| Draw histogram areas | Toggle | Unchecked | None | Off/on | Filled additive areas versus lines | Histogram toolbar | Filled overlap can obscure bins | Display only | Window lifetime |
| Show histogram | Toggle | Checked | None | Off/on | Shows/hides plot and disables/enables related controls | Histogram toolbar | Can reduce redraw distraction | Display only | Window lifetime |
| Plot resolution | Bin-count list | **8-bit (256)** | None | 64, 128, 256, 512, 1024, 4096, 16K, 64K, 100, 1000, or 10000 bins | More bins retain narrower distribution detail | Histogram shown | Higher computation/memory; output precision unchanged | Display only | Window lifetime |
| Base image > Image | Target selector | Active image | None | Any open target accepted by launch guards | Supplies `A`, mask, metadata base, history, and destructive destination | Changing target clears Blend image selector | Wrong target is replaced | Preview/OK; still/sequence | Target lifecycle; Reset unchanged |
| Base image > Stretch > Black Point | Double + slider, normalized | `0.0` | None | `0..1`, four decimals, spin step `0.01` | Raises lower clipping boundary of linked MTF | Always enabled | Clips more low samples | All base channels before mode; mask later | Captured; Reset/success `0` |
| Base image > Stretch > Highlights | Double + slider, normalized | `1.0` | None | `0..1`, four decimals, step `0.01` | Lowers upper clipping boundary when reduced | Always enabled | Clips/compresses high samples | All base channels before mode | Captured; Reset/success `1` |
| Base image > Stretch > Midtones | Double + slider, MTF midpoint | `0.5` identity | None | `0..1`, four decimals, step `0.01` | Below `0.5` brightens interior samples; above darkens | Always enabled | Extremes collapse most interior values | All base channels before mode | Captured; Reset/success `0.5` |
| Blend image > Image | Equal-size view selector | **No image** | None | Other open view matching base width/height | Supplies `B`; enables remaining blend controls | Rebuilt on view lifecycle/target changes; no channel/type/WCS/intensity validation | Misregistration creates doubled edges/seams | One fixed image for preview/OK and every base-sequence frame | Window lifetime; target change clears |
| Blend image > Stretch > Black Point | Double + slider, normalized | `0.0` | None | `0..1`, four decimals, step `0.01` | Raises `B` MTF lower boundary | Valid B required | Clips B shadows | All B channels before filter/mode | Captured; Reset/success `0` |
| Blend image > Stretch > Highlights | Double + slider, normalized | `1.0` | None | `0..1`, four decimals, step `0.01` | Reducing lowers B upper boundary | Valid B required | Clips/compresses B highlights | All B channels before filter/mode | Captured; Reset/success `1` |
| Blend image > Stretch > Midtones | Double + slider, MTF midpoint | `0.5` identity | None | `0..1`, four decimals, step `0.01` | Below `0.5` brightens B; above darkens | Valid B required | Strong nonlinear redistribution | All B channels before filter/mode | Captured; Reset/success `0.5` |
| Filter Type | Combo box | **None** | None | None, Gaussian Blur, High Pass, Hue, Color | Chooses preprocessing applied only to stretched B | Valid B required; reveals Radius or Hue/Saturation as appropriate | Adds buffers and processing; changes subsequent equation input | Before blend/opacity/mask | Captured; Reset/success None |
| Radius | Double + slider, Gaussian sigma / high-pass radius | `10.0` | None | `0.1..1000`, three decimals, step `0.01` | Larger Gaussian removes larger detail; larger High Pass isolates coarser residuals | Visible only for Gaussian Blur/High Pass | Large values cost more and can create broad halos/boundary effects | B only; exact Gaussian, then mode | Captured; Reset/success `10` |
| Saturation | Double + slider, normalized colorizer saturation | `1.0` | None | `0..1`, three decimals, step `0.01` | Raises strength of chosen fixed hue | Visible only for Hue/Color filters | High values can clip gamut | B-filter output before mode | Captured; Reset/success `1` |
| Hue | Double + slider, degrees | `0.0` | None | `0..360`, three decimals, step `0.01` | Rotates fixed filter color; 0/360 are red | Visible only for Hue/Color filters | Strong color replacement; output clipped | B-filter output before mode | Captured; Reset/success `0` |
| Opacity | Double + slider, blend fraction `o` | `1.0` | None | `0..1`, four decimals, step `0.05` | `0` returns original A; `1` uses full mode result; interior linearly interpolates | Valid B required | Lower values soften all mode/filter/MTF effects | After mode/Preserve lightness; before active mask | Captured; Reset/success `1` |
| Blend mode | Combo box, 27 choices | **Replace** | None | Every mode in the equations table below | Selects function `F(A,B)` | Valid B required | Contrast modes clip readily; Lab/HSI modes cost more | Before opacity/mask | Captured; Reset/success Replace |
| Preserve lightness | Checkbox | Unchecked | None | Off/on | After mode, replaces result CIE L* with stretched base L* | Disabled for Color, Lightness, or mono+mono; slider tracking pauses while checked | A colour conversion each way is costly, which is why slider tracking pauses; gamut can clip; opacity/mask can alter final measured L* afterward | RGB result before opacity/mask | Captured; Reset/success off |
| Preview | Checkbox | Checked | None | Off/on | Creates/destroys live private preview updates | Available while idle | Checked recomputes on changes | Preview only | Window lifetime; Reset delegates to shared preview reset |
| Preview pane | Viewer | Current result | None | Pan/scroll plus the toolbar controls listed below | Displays processed preview | Preview checked | Holds result/statistics buffers | Display only | Window lifetime |
| **Fit To Window** | Preview-toolbar action | Preview fitted initially | None | Click or **Ctrl+Shift+F** | Fits the whole preview image | Preview checked | Broad context; resampling can hide pixel-scale artifacts | Display only | Preview lifetime |
| **Zoom In** | Preview-toolbar action | Current scale | None | Click; `1.25×` relative zoom | Enlarges around the viewport center | Preview checked | Better local blend/halo inspection | Display only | Preview lifetime |
| **Zoom Out** | Preview-toolbar action | Current scale | None | Click; `0.8×` relative zoom | Reduces the displayed scale | Preview checked | More context, less pixel detail | Display only | Preview lifetime |
| **Zoom 100%** | Preview-toolbar action | Current scale | None | Click; absolute scale `1.0` | Shows one-pixel viewer scale | Preview checked | Preferred for clipping, halos, and registration residuals | Display only | Preview lifetime |
| Switch image | Preview action | PREVIEW shown | None | Click/Space/right-click/Ctrl+Shift+O | Toggles PREVIEW and BASE IMAGE | Preview checked | Display only | Preview | Window lifetime |
| Preview area | Preview checkbox | Unchecked | None | Off/on; move/resize overlay | Crops A and the selected B to the same area and shows their blend there | Preview checked | A smaller area is less work | Preview/snapshot only, never OK | Window lifetime; Reset may not clear |
| Take snapshot | Preview action | No snapshot | None | Click/Ctrl+Shift+K | Re-executes current settings and opens a new still | Preview active | Full-size allocation/processing; active mask applies | Auxiliary still; target unchanged | One view per action |
| **View Channel** | Preview-toolbar combo box | Follows the current result channel layout | None | RGB, grayscale, stored channels, and supported derived CIE/HSL/HSV/HSI/YCbCr display components | Changes only the displayed component | Preview active; choices follow result channels | Helps inspect per-channel or derived-component artifacts | Display only; output/histogram samples are unchanged | Preview lifetime |
| **Display Mode** | Preview-toolbar combo box | **Linear** | None | Linear or Autostretch | Linear shows stored blend samples directly; Autostretch derives a display presentation from result statistics | Preview active | Autostretch can conceal absolute changes or clipping | Display only; output/histogram samples are unchanged | Preview lifetime |
| **Autostretch Settings** | Preview-toolbar action | Initialized from the target view | None | Opens shadow clipping, target background, saturation, brightness, and contrast settings | Changes only Autostretch presentation/statistics | Preview active; relevant when Display Mode is Autostretch | Adds display computation; blend output is unchanged | Display only | Preview lifetime |
| OK | Dialog action | Available even before B selection | None | One click | With valid B, replaces target after success; without B, applies Base Stretch alone to A - one screen transfer on each colour channel - records the result nonlinear under `image-blend`, and describes the step with its three values, such as `Base stretch: shadows 0.000, midtones 0.250, highlights 1.000`; at the identity values (Black Point `0`, Midtones `0.5`, Highlights `1`) A is left as it was | Target lock required; only target/shared preview actions are generically disabled during work | Full-image buffers; destructive; most Image Blend controls remain live though run parameters are captured | Still/sequence, mask-aware | Success creates history then resets process values |
| Reset | Dialog action | Available | None | One click | Restores mode, stretches, filter, opacity, and preserve | Does not change target/B selectors or histogram state | Schedules preview | Configuration | Immediate |
| Close | Dialog action | Available | None | One click | Cancels the previews and, for a sequence run, the progress dialog, which stops that run; while a run is still in flight the window hides and closes once the run has landed, so a still OK already running still lands | Remains live during processing | Nothing waits on the GUI thread, so closing never blocks the window | Process/window | No settings persist after destruction |
| Sequence progress / Cancel | Shared progress | Created for sequence OK | None | Per-frame progress; cooperative cancel between/within supported stages | Writes temporary replacement, commits only on success | Base sequence only; B is fixed | Full sequence I/O and buffers | Sequence, opacity/mask per frame | Cancel removes temporary output |

## How the algorithm works

### Conceptual pipeline

1. Read base `A` and selected blend image `B`. Apply each image's linked MTF
   when its controls differ from identity.
2. Apply the selected filter to `B` only. A grayscale image meets an RGB one
   as three equal channels, except a grayscale `B` in Lightness, whose samples
   are the lightness itself. Both images are read as fractions of full scale -
   an unsigned integer as a fraction of its largest value, a signed integer
   counted from its smallest, a floating-point image as it is - and an integer
   base is worked in a Float32 copy (Float64 for 32-bit integers).
3. Evaluate the selected mode `R = F(A,B)`. When requested and supported,
   reconstruct RGB `R` using base CIE L* and result a*/b*.
4. Apply opacity: `Q = (1-o)A_original + oR`. If an active ordinary mask `M`
   exists, output `O = (1-M)A_original + MQ`; an inverted mask exchanges those
   weights. Write every sample once in the base's representation, rounded to
   nearest and saturating; build the result's metadata from both images, mark
   the output nonlinear, and commit only after success.

### Input stretch and filters

For normalized `x`, black point `s`, highlights `h`, and midtones `m`, linked
MTF is

`T(x)=0` for `x<=s`, `T(x)=1` for `x>=h`, otherwise with
`u=(x-s)/(h-s)`, `T(x)=((m-1)u)/((2m-1)u-m)`.

All channels use the same parameters. The UI does not enforce `s<h`; crossed
endpoints produce a degenerate clipped mapping rather than a useful stretch.
Identity `(0,1,0.5)` skips the allocation.

- **Gaussian Blur:** the engine's exact Gaussian with standard deviation
  **Radius**, at any radius; one above about 32 pixels runs as several exact
  passes whose variances add up to it.
- **High Pass:** `clamp(B - Gaussian(B,r) + 0.5, 0, 1)`.
- **Hue:** if B is RGB, first reduce it to normalized CIE L*; then use that
  scalar as HSV Value with selected Hue/Saturation. Mono follows the same
  scalar colorization.
- **Color:** apply the same fixed-HSV mapping directly to B. RGB is processed
  channel-wise, so it is not a conventional single-hue recoloring unless B is
  mono.

### Blend-mode equations

The table uses per-channel values `A,B` read as fractions of full scale, as
described above; `clamp` means `[0,1]`. Every mode - Replace, Darken/Min,
Lighten/Max, Linear Dodge/Add, and Subtract included - is computed on those
fractions for every representation, signed integers included, and written in
the base's representation. RGB **Darker/Lighter Color** select the entire pixel
by CIE L*; component modes are defined separately.

| Mode | Current implementation `F(A,B)` | Interpretation / boundary |
| --- | --- | --- |
| Replace | `B` | Replaces A before Preserve lightness/opacity/mask; the result is written in the base's representation, not B's |
| Darken/Min | `min(A,B)` | Per-channel darker sample |
| Multiply | `A*B` | Darkens; white is neutral, black returns black |
| Color Burn | `0` if `B=0`, else `1-min((1-A)/B,1)` | Strong shadow/contrast burn |
| Linear Burn | `clamp(A+B-1)` | Additive darkening with black clipping |
| Darker Color | Mono `min(A,B)`; RGB copies the complete pixel with lower CIE L* | Avoids channel-by-channel mixed color for RGB |
| Lighten/Max | `max(A,B)` | Per-channel lighter sample |
| Screen | `1-(1-A)(1-B)` | Lightens; black is neutral |
| Color Dodge | **Current code:** `0` if `B=1`, else `clamp(A/(1-B))` | The exact-white B special case returns black, unlike the limiting/conventional white result |
| Linear Dodge/Add | `min(A+B,1)`, computed in double for every representation | Brightens with white clipping |
| Lighter Color | Mono `max(A,B)`; RGB copies the complete pixel with higher CIE L* | Whole-pixel L* selection |
| Overlay | `2AB` if `A<=0.5`, else `1-2(1-A)(1-B)` | Threshold is base A |
| Soft Light | `clamp((1-2B)A^2+2AB)` | Quadratic contrast response |
| Hard Light | `2AB` if `B<=0.5`, else `1-2(1-A)(1-B)` | Threshold is blend B |
| Vivid Light | `0` if `B=0`; `max(0,1-(1-A)/(2B))` if `B<0.5`; otherwise `min(1,A/(2(1-B)))` | At exact `B=1`, division tends to infinity and clamps to 1 for positive A |
| Linear Light | `clamp(A+2B-1)` | Current `B=0` special case is also 0 |
| Pin Light | `clamp(max(2B-1,min(A,2B)))` | Current `B=0` special case is 0 |
| Difference | `abs(A-B)` | Identical registered samples become 0 |
| Exclusion | `clamp(0.5-2(A-0.5)(B-0.5))` | Lower-contrast difference family |
| Subtract | `max(A-B,0)`, computed in double for every representation | Clips negative results to black |
| Divide | `0` if `B=0`, else `clamp(A/B)` | Useful for ratios/flatness only after valid normalization |
| Power | `clamp(pow(A,B))` | `B<1` generally lifts nonzero A; implementation follows standard `pow` boundaries |
| Arctan | `clamp(atan2(A,B))` in radians | Values above 1 radian clip; it is not normalized by `pi/2` |
| Hue | HSI `(H_B,S_A,I_A)` converted to RGB | Grayscale inputs are promoted to neutral RGB |
| Saturation | HSI `(H_A,S_B,I_A)` converted to RGB | Takes saturation only from B |
| Color | CIE Lab `(L*_A,a*_B,b*_B)` converted to RGB | Preview and OK use the same conversion |
| Lightness | CIE Lab `(L*_B,a*_A,b*_A)` converted to RGB; mono B maps `B*100` to L* | Replaces luminosity while retaining base chroma; Preview and OK use the same conversion |

**Preserve lightness** performs a second Lab reconstruction after `F`: it uses
`(L*_A,a*_R,b*_R)`. Color already does this and Lightness intentionally replaces
L*, so the checkbox is disabled for those modes. Opacity and mask blending occur
later in RGB/sample space, so final measured CIE L* is not guaranteed to remain
exactly `L*_A` when either is fractional.

### Boundary and failure behavior

- Only width/height are filtered in the selector. The run uses the image the
  blend view showed when the run was asked for; one whose size no longer matches
  the base is refused with **the blend image does not have the target's size**.
- Each image must have one or three channels. A grayscale base and an RGB blend
  image, or the reverse, give an RGB result; a grayscale `B` in Lightness is
  read as the lightness itself.
- The result is always in the base's representation, whatever the blend image's;
  an integer result is rounded to the nearest code and saturates.
- Arithmetic/color functions clamp where the table says. Linear Dodge/Add and
  Subtract hold the sum at one and the difference at zero, in double, for every
  representation. A pixel either image's validity rejects keeps the base's
  samples; a sample that is not a finite number in a pixel its image states
  valid makes the engine refuse the run.
- Preview and OK run the same composition, so Darker/Lighter Color, Color,
  Lightness, Preserve lightness, and the Hue filter's L* use the same CIE
  conversion in both, computed in double through the engine's XYZ matrices.
- A run the engine refuses publishes nothing; the shared framework reports why
  and keeps the original target.

## Parameter interactions and starting points

| Combination | Conservative starting point | Too weak looks like | Too strong looks like | Adjustment order |
| --- | --- | --- | --- | --- |
| Difference for registration | Identity MTF, None filter, opacity `1`, no mask | Residuals hidden by low opacity | Broad residuals from scale/background mismatch dominate | Register/normalize first, inspect Difference, then lower opacity only for context |
| Screen/Add for emission/stars | Identity MTF, opacity below `1` | Added structure is barely visible | White clipping, enlarged stars, bright background | Match geometry/scale, choose mode, lower opacity, then mask |
| High Pass + contrast mode | Radius `10`, moderate opacity | Little local detail transfer | Halos/noise and clipped contrast | Set radius, mode, opacity, then mask |
| Preserve lightness + color mode | Use Hue/Saturation modes; opacity `1` initially | Color change subtle | Gamut clipping or false color | Choose component mode first; use Preserve only where enabled; then opacity |
| Active mask + opacity | Tune unmasked at low opacity, then enable mask | Effect absent in selected area | Hard mask transitions or excessive local effect | Tune equation/filter, opacity, then mask softness/inversion |

## Inputs, outputs, and metadata

On a successful still run, the base view is replaced and receives a history step
named **Image Blend** with `<mode> with <blend-view-name>`. Output geometry and
representation are the base's. The result has three channels when either image
has three or the mode or filter makes colour, and one otherwise. CFA mosaics
are treated as grayscale sample planes, and the result states no color filter
array.

The result's metadata is built from both images. Its exposure is the sum of
the exposures the two images state - even for Difference, Replace, low opacity,
or a mask - recorded once, and the images' `EXPOSURE` and `EXPTIME` cards are
dropped. The object, instrument, telescope, observer, filter, sensor
temperature, gain, binning, and exposure start are kept when both images state
the same value and dropped otherwise; every other header card of the base is
kept only when the blend image states it identically. The base's world
coordinate (WCS) cards and plate solution are kept, because the pixel grid is
the base's, without checking registration. The output processing state is then
set to nonlinear with `image-blend` provenance, and the lineage names the
engine composition.

For a base sequence, one fixed blend view image is reused for every frame and a
temporary sequence replaces the original only after success. It does not pair
two sequences by index or time.

## Performance

Core per-pixel modes are parallel and scale with width, height, channels, and
sequence frames. Linked MTF and filters add full-image buffers; Gaussian/High
Pass add neighborhood work; CIE/HSI modes and Preserve lightness add conversions.
Each preview and run applies the transfers again; nothing is cached between
runs. Plot resolution affects histogram cost, not blend precision. No production
timing or peak-memory measurement was made.

!!! note "Base Stretch on a sequence"

    Every frame of a base sequence is stretched by the Base Stretch values
    itself.

## Worked example

- Evidence mode: Source-and-test-derived.
- Input condition/provenance and settings: same-size registered pair; identity
  stretches; Filter None; Difference; Opacity `1`; Preview on; no active mask;
  Preview area off.
- Expected checkpoint: aligned equal pixels map to black under
  `abs(A-B)`; unequal structure appears as a positive residual; reducing opacity
  mixes that residual back toward original A.
- Inspect at 100%/statistics/history: inspect stars/edges at 100%, clipping and
  background in the histogram, then verify the Image Blend history description.
- Limitations: not executed on approved astronomical data; no alignment
  accuracy, visual quality, sequence output, timing, or memory claim.

## Warnings and limitations

- **OK is destructive.** Save or duplicate the base first.
- Equal dimensions do not prove registration or matching WCS. There is no
  alignment, resampling, background match, or normalization.
- Preview area crops both images to the same pixel area; it does not register
  them.
- B is one fixed image - the one its view showed when the run was asked for -
  not a synchronized sequence.
- Color Dodge returns black at exact `B=1` in the current implementation.
- Most Image Blend-specific controls remain enabled during execution. The run
  uses the parameters and the blend image captured when it was asked for.
- The result keeps the base's representation whatever the blend image's;
  integer results are rounded to the nearest code. A sample that is not a finite
  number in a pixel its image states valid makes the run fail.
- Metadata exposure summation does not account for blend mode, opacity, or mask;
  the base's WCS is not validated against B.

## Troubleshooting

| Symptom or exact error | Verified cause/validation path | Recovery | Evidence source |
| --- | --- | --- | --- |
| Blend image is absent | It is the base itself or has different width/height | Register/crop/resize first, then reopen selector | `isViewControlSupportedCustom()` |
| Blend controls are disabled | Blend image is No image, closed, missing, or null | Select a valid equal-size B | `validateParameters()` |
| Preview area shows misaligned structure | Both images are cropped to the same pixel area, but B is not registered to A | Register and crop the pair first | `process_utils::LeasedImage::read` |
| Preserve lightness is disabled | Mode is Color/Lightness, or both inputs are grayscale | This is intentional; select another compatible RGB mode if needed | `validateParameters()` |
| Result is black where B is pure white in Color Dodge | Current exact-`B=1` special case writes zero | Avoid exact white in B or use another dodge/lighten mode | `an::BlendMode::ColorDodge` |
| Colors/edges double or smear | Inputs share dimensions but are not registered/normalized | Register and match scale/background before blending | selector and worker validate dimensions only |
| Output intensity metadata looks implausible | Exposure metadata is summed independent of mode/opacity/mask | Treat combined exposure as implementation metadata, not photometric proof; correct downstream if needed | `process_utils::combinedResultMetadata` |
| OK with no B only stretches A, or appears to do nothing | Without a blend image the run applies Base Stretch alone; at its identity values it leaves A as it was | Select B before OK to blend; undo the step in the image's history if the stretch was not meant | `ImageBlendProcess::executeTypedProcessImpl` |

## Related workflow steps

Use [Image Registration](../registration-alignment/image-registration.md) and
normalization before blending data that must correspond pixel-for-pixel. Use
[Image Compare](image-compare.md) for non-destructive visual inspection,
[Starmask Recombination](../star-operations/starmask-recombination.md) for its
specific star/starless addition path, and [Pixel Math](../pixel-math/pixel-math.md)
for explicit formulas or output naming. Use [Masks](../../interface/masks.md) to
prepare and inspect the active base mask.

## References and provenance

This page documents AfterNight's current blend functions and focused tests. The equations are implementation contracts rather
than claims of conformance to an external compositing standard.
