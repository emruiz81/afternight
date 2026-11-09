---
kind: process
stage: noise
menu: Process > Filters > Bilateral Filter
summary: >-
  Smooths an image in one pass that weights each neighbor by both its distance and its value difference, so that
  strong edges mix less.
data_in: linear
---
# Bilateral Filter

## Purpose and expected result

Bilateral Filter performs one local, nonlinear smoothing pass whose weight for a
neighbor depends on both pixel distance and sample-value difference. Nearby
samples with similar values contribute most; a large value jump can contribute
little even when it is spatially close. This can suppress fine noise while
retaining stronger edges better than an ordinary Gaussian blur.

The expected result is a same-size, same-type still or replacement sequence with
weaker local variation. It is not a detail-recovery process: strong settings can
flatten faint stars and texture, produce piecewise-smooth or cartoon-like areas,
or shift color. **Sigma Color** is a fraction of full scale for every sample
type, while **Sigma Space** is in pixels, so the two scales are very
different; the live Sigma Color default `30` is extremely broad for samples
that span `[0,1]`.

## At a glance

| Item | Current behavior |
| --- | --- |
| Menu path | **Process > Filters > Bilateral Filter** |
| Enabled when | An image or sequence is active, the same process is not already attached to it, and no attached process holds exclusive access |
| Inputs | One grayscale or RGB still or sequence; a still CFA mosaic is treated as one grayscale sample plane unless debayered first; an optional active mask must match target geometry |
| Outputs | Destructively replaces a still or, after successful temporary processing, a sequence; **Take snapshot** creates an auxiliary still; dimensions, channels, and source pixel type are retained unless a selected component reconstructs RGB; no file is written directly |
| Recommended image state | Usually a finite, normalized linear master after calibration, registration, stacking, gradient correction, and color calibration; use a deliberate luminance component if independent chromatic smoothing is undesirable |
| Preview | Asynchronous full-image or movable-area preview, original/result switching, snapshot, display-channel selection, and display-only autostretch; with no area drawn, a slider held runs on the part the preview view shows at the image's own scale, its window's reach around it, exactly the pixels **Apply** writes there |
| Sequence support | Yes; one captured parameter/channel/mask state is applied frame by frame to temporary storage before replacement |
| Mask support | Yes; the active mask blends the filtered result with the original after Amount blending |
| External dependency | None; the AfterNight engine's own bilateral filter, held to OpenCV's `cv::bilateralFilter` - its circular window, radius rule, and reflecting border; no separate user installation or network access |

## Before you start

- Work from a calibrated, registered, and stacked master. Remove gross hot pixels
  before broad smoothing; a bilateral filter is not a substitute for cosmetic
  correction.
- Use finite samples. The process does not sanitize NaN or infinity before
  filtering.
- Keep both sigmas positive when you want filtering. The process leaves the
  image unchanged when either Sigma Space or Sigma Color is `0`.
- Duplicate the target or verify Image History. Apply replaces the target only
  after the complete still operation or temporary sequence succeeds.
- Judge under one display stretch at **Zoom 100%**. Include smooth background,
  faint stars, a high-contrast edge, and color detail in the Preview area.
- Treat **Sigma Space** as pixels and **Sigma Color** as a fraction of full
  scale. Their numeric scales are not comparable, so do not select **Link Sigma
  controls** merely because the names both contain sigma.

## Procedure

1. Select a finite, normalized target and open **Process > Filters > Bilateral
   Filter**. Confirm **Target Image**, choose **RGB** or one deliberate component,
   and leave **Preview** selected.
2. Start with **Diameter 5** and **Amount 1**. For actual edge discrimination on
   normalized data, reduce **Sigma Color** far below the live default and compare
   the same edge and background under one display stretch.
3. Set **Sigma Space** for the desired spatial falloff. Diameter is first
   truncated to an integer; a result at or below `0` selects automatic support,
   a circular window of radius `round(1.5 × Sigma Space)` held to at most `32`.
   A positive integer selects a circular window of radius `floor(d/2)`, at least
   `1`. Sigma Space changes weights inside that support.
4. Toggle **Switch image** and inspect the result at **Zoom 100%**. Watch faint
   stars, narrow dark lanes, color boundaries, and smooth background for
   flattening or blotchy plateaus.
5. Use **Amount** to blend back the original after finding useful kernel values.
   If localized protection is needed, verify the active mask and inversion state.
6. Check the full frame, target, Channel, and mask before selecting **Apply**.
   For a sequence, monitor frame progress; Cancel stops the filter between rows,
   preserves the original, and removes temporary output.

## Complete parameter and action reference

| UI label/action | Type and unit | Runtime default | Persisted-default exception | Valid values, step, or constraints | Effect of increasing, decreasing, or selecting | Dependencies and enable/visibility rules | Quality, artifact, runtime, memory, or disk trade-off | Scope: preview/apply, channel, mask, still/sequence | Persistence |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Diameter | Double value plus slider, displayed in pixels but truncated to an integer `d` | `5.00` | None | `0.00..20.00`; two decimals; spin step `0.50`; slider has 1000 positions; positive fractions are truncated | A truncated value at or below `0` derives the radius `round(1.5 × Sigma Space)`, held to at most `32`; otherwise the radius is `floor(d/2)`, at least `1`, and the window is the circle of that radius inside a `2r+1` square | Always enabled while idle | Displayed values below `1` enter automatic mode; `1`/`2` become an effective `3`, `3` stays `3`, `4` becomes `5`, and so on; automatic support with large Sigma Space can be much slower | Preview/Apply; selected channel/component; still/sequence; before Amount/mask | Captured per execution; Reset and successful Apply return `5.00` |
| Sigma Space | Double value plus slider, pixels | `30.00` | None | `0.00..200.00`; two decimals; spin step `0.50`; slider resolution about `0.20` | Broadens spatial weighting; in automatic mode it also grows the selected neighborhood, up to radius `32`, which Sigma Space `21.00` already reaches; `0` leaves the source unchanged before any weighting | When **Link Sigma controls** is checked, changing it copies the value to Sigma Color; either sigma at `0` disables the complete bilateral effect | Larger values mix farther pixels and can be very expensive with automatic Diameter; effect saturates inside fixed support | Preview/Apply; selected channel/component; still/sequence; before Amount/mask | Captured per execution; Reset and successful Apply return `30.00` |
| Sigma Color | Double value plus slider, fraction of full scale (an integer image's value is restated in its own codes) | `30.00` | None | `0.00..200.00`; two decimals; spin step `0.50`; slider resolution about `0.20`; manual entry can use two decimals | Broadens range weighting; larger values let dissimilar samples mix, while small positive values protect value edges more strongly; `0` leaves the source unchanged | When **Link Sigma controls** is checked, changing it copies the value to Sigma Space; either sigma at `0` disables the complete bilateral effect | On ordinary `[0,1]` data, `30` makes even a full-scale scalar difference retain about `99.94%` of its range weight; overly small positive values can preserve noise or produce piecewise-smooth regions | Preview/Apply; every channel or component uses its own absolute difference; before Amount/mask | Captured per execution; Reset and successful Apply return `30.00` |
| Link Sigma controls | Checkbox | Off | None | Checked/unchecked | On selection, immediately copies Sigma Space to Sigma Color; afterward changing either sigma copies it to the other | Copy guard prevents a recursive update; controls remain editable | The two sigmas use different physical scales in AfterNight, so linking usually prevents useful independent tuning | Preview/Apply configuration; still/sequence | Captured but not written to history; Reset and successful Apply turn it off |
| Amount | Double value plus slider, blend fraction | `1.00` | None | `0.00..1.00`; two decimals; spin step `0.05`; slider resolution `0.001` | Blends `original × (1-Amount) + filtered × Amount`; `0` returns original pixels after the filter has still run | Always enabled while idle | Lower values reduce smoothing without reducing filter cost; values near one expose all kernel artifacts | Preview/Apply; after Channel reconstruction, before mask; still/sequence | Captured per execution; Reset and successful Apply return `1.00` |
| Target Image | Image selector | Attached/active target | None | Open eligible image or sequence | Changes source, Channel list, Preview, active mask, and Apply destination | Active image required; duplicate/exclusive gates apply | Wrong target is destructively replaced after success | Preview/Apply source and active mask | Target lifecycle; Reset does not change it |
| Channel | Combo box | **RGB** for RGB; disabled **Grayscale** for mono | None | **RGB**, **Red**, **Green**, **Blue**, **CIE L* (Lightness)**, **CIE a***, **CIE b***, **CIE c***, **CIE h***, **CIE X**, **CIE Y (Luminance)**, **CIE Z**, **HSL Hue**, **HSL Saturation**, **HSL Lightness**, **HSV Hue**, **HSV Saturation**, **HSV Value**, **YCbCr Luma**, **YCbCr Blue Chroma**, **YCbCr Red Chroma** | RGB filters each stored channel separately, each with range weights from its own differences; a component filters one plane and reconstructs/copies the others | Component choices require RGB; mono selector disabled | Separate RGB channels protect an edge only in the channels where it is strong, which can shift color at weak chromatic edges; component round trips add buffers and can alter color | Preview/Apply; active mask post-blend; still/sequence | Captured by current selection; Reset does not restore RGB |
| Preview | Checkbox | Checked | None | Checked/unchecked | Enables/disables asynchronous full-image or selected-area execution | Process idle | Runs the complete filter; a large automatic neighborhood can make updates slow | Preview only; mask-aware; current still/current displayed sequence frame | Window lifetime; Reset does not change it |
| Preview pane | Image viewer | Current result | None | Pan with scroll bars/dragging; toolbar actions below | Shows the mask-aware result without modifying the target | Exists only with Preview | Holds source/result and display buffers in addition to filter workspaces | Preview only | Window lifetime |
| Fit To Window | Preview-toolbar action | Preview fitted when first displayed | None | Click or **Ctrl+Shift+F** | Fits the whole image uniformly in the viewport | Preview image required | Good overview; resampling can hide fine noise and edge halos | Preview display only | Viewer lifetime |
| Zoom In | Preview-toolbar action | Current scale | None | Click; `1.25×` relative zoom within viewer limits | Enlarges around viewport center | Preview image required | Improves local inspection; more panning | Preview display only | Viewer lifetime |
| Zoom Out | Preview-toolbar action | Current scale | None | Click; `0.8×` relative zoom within viewer limits | Reduces around viewport center | Preview image required | More context, less fine detail | Preview display only | Viewer lifetime |
| Zoom 100% | Preview-toolbar action | Current scale | None | Click; absolute scale `1.0` | Shows the viewer's one-pixel scale | Preview image required | Preferred for judging residual noise, flattened stars, and piecewise-smooth artifacts | Preview display only | Viewer lifetime |
| View Channel | Preview-toolbar combo box | **RGB** for RGB; disabled **Grayscale** for mono | None | **RGB**, **Red**, **Green**, **Blue**, **CIE L* (Lightness)**, **CIE a***, **CIE b***, **CIE a*=R / b*=G (Chrominance)**, **CIE c***, **CIE h***, **CIE X**, **CIE Y (Luminance)**, **CIE Z**, **HSL Hue**, **HSL Saturation**, **HSL Lightness**, **HSV Hue**, **HSV Saturation**, **HSV Value**, **HSI Hue**, **HSI Saturation**, **HSI Intensity**, **YCbCr Luma**, **YCbCr Blue Chroma**, **YCbCr Red Chroma** | Selects only the displayed stored/derived component | Separate from process Channel; RGB target required for components | Reveals channel artifacts without changing pixels | Preview display only | Viewer lifetime; not in process history |
| Display Mode | Preview-toolbar combo box | Inherits target **Linear** or **Autostretch** when preview is created | None | **Linear**, **Autostretch** | Changes display only; Autostretch uses cached full-target source statistics | Autostretch enables settings action | Aggressive display stretch can exaggerate residual noise and plateaus | Preview display only | Preview-view state; not in process history |
| Autostretch Settings | Preview-toolbar dialog action | Disabled in Linear mode; inherits target-view settings | Shared-view values load when opened | Opens Target Background, Shadows Clipping, linked channels, neutralization/color calibration, saturation/background, brightness, contrast, banner, Reset, and Close; **Apply Stretch** is hidden | Tunes preview display only | Autostretch mode only; disabled while dialog open | Can change visual judgment without changing filtering | Preview display only | Viewer-scoped; not in Bilateral Filter parameters |
| Switch image | Preview-toolbar action | Filtered preview shown | None | Click; **Space**; right-click; **Ctrl+Shift+O** | Toggles result/original | Preview required | Comparison only; no recomputation | Preview display; still/current frame | Preview-window lifetime |
| Preview area | Preview-toolbar checkbox | Checked with an initial `800×800` area when the preview is created | None | Checked/unchecked; move/resize overlay | When checked, processes the selected area and composites it over the original preview; uncheck for full-image preview | Preview required | Default area reduces tuning cost; full Apply remains full-frame | Preview and Take snapshot; mask cropped consistently | Preview-window lifetime; Reset unchanged |
| Take snapshot | Preview-toolbar action | No snapshot | None | Click or **Ctrl+Shift+K** | Re-executes settings and opens `<target> - Bilateral Filter - SNAPSHOT` | Preview required | Adds a complete execution and auxiliary image; display stretch is not baked in | New still; target unchanged; area/mask honored | Each action creates a separate image view |
| Apply | Button/action | Enabled | None | One action | Captures all filter values and selected Channel, then processes the full still or every sequence frame | Target must lock; controls disabled during commit | Neighborhood work dominates; component conversion, mask, sequence encode, and temporary storage add cost | Destructive still/sequence replacement after success | Records partial history, then calls Reset; Target, Channel, Preview, and preview toolbar remain |
| Reset | Button/action | Enabled while idle | None | One action | Restores Diameter `5`, both sigmas `30`, Link off, and Amount `1` | Does not reset Target, Channel, Preview, mask, or preview toolbar | Checked Preview schedules another complete execution | Configuration only | Immediate; also invoked after successful Apply |
| Close | Button/action | Enabled while idle | None | One action | Closes without applying current settings | Disabled during Apply | Active preview cancellation is requested and reaches the filter between rows | Window/execution | No process settings persist after destruction |
| Sequence progress / Cancel | Shared per-process/frame progress and Cancel | Created for sequence Apply | None | Cooperative Cancel; frame/task text updates around temporary processing | Sequence target only | The filter checks for Cancel between rows, so a frame stops promptly | Sequence Apply; captured parameters, Channel, Amount, and mask apply to every frame | Cancel removes temporary output; success replaces sequence, records one history step, and resets parameters |

## How the algorithm works

For output position `p`, let `q` range over its neighborhood, let `I` be the
selected scalar or RGB data, let `sigma_s` be Sigma Space, and let `sigma_r` be
Sigma Color. Define the range distance as

\[
\Delta_I(p,q)=|I(p)-I(q)|
\]

for a scalar plane. Every channel is a scalar plane here: in **RGB** each
stored channel is filtered on its own, with range weights from its own
differences. The bilateral result is

\[
F(p)=\frac{1}{W_p}\sum_{q\in\Omega_p}
\exp\left(-\frac{\lVert p-q\rVert^2}{2\sigma_s^2}\right)
\exp\left(-\frac{\Delta_I(p,q)^2}{2\sigma_r^2}\right)I(q),
\]

where `W_p` is the sum of the same spatial and range weights. The spatial term
falls with pixel distance. The range term falls across a sample or color
difference, making the operation nonlinear and edge-aware.

AfterNight runs its engine's bilateral filter once, into a separate result. The
filter reproduces OpenCV's `cv::bilateralFilter`: the window is the circle of
radius `r` (`dx² + dy² <= r²`), the displayed Diameter is truncated to an
integer `d`, a `d <= 0` derives `r = round(1.5 × Sigma Space)`, and otherwise
`r = floor(d/2)`, at least `1`. AfterNight holds a derived radius to at most
`32` pixels, the widest window the engine filters; without that limit,
Diameter `0` with the dialog's default Sigma Space of `30` would ask for `45`.
Beyond each edge
the image is reflected without repeating the edge sample, as OpenCV's
`BORDER_REFLECT_101` does. The range weight is evaluated for each neighbour
rather than read from a table, which agrees with OpenCV within its table's
interpolation error.

If either sigma is at or below `1e-6`, the process leaves the image unchanged,
as OpenCV does. With the UI's two-decimal values, setting either sigma to `0`
therefore makes the filter an identity operation even if the other sigma and
Diameter are nonzero.

### Precision and color path

The filter reads every sample type in its stored units and computes in single
precision. Sigma Color is a fraction of full scale: for a float image it is
compared with the samples directly, and for an integer image it is restated in
the image's own codes (multiplied by the type's full span), so the same Sigma
Color means the same edge strength whether a picture is stored as 16-bit
integers or as 32-bit floats. The result is written back in the source type,
rounded for an integer image; a 64-bit float image is filtered at single
precision.

In **RGB**, each of the three stored channels is filtered separately with the
same window and sigmas. Choosing Red, Green, Blue, or a derived component sends
a single plane through the filter and then reconstructs or copies the remaining
data through the shared Channel dispatcher. A derived component's
reconstruction saturates an integer image at its type's range and keeps a
floating image's values outside `[0, 1]`. Sigma Color is in fractions of full
scale, not 8-bit `0..255` units, which is why linking its numeric value to a
spatial sigma measured in pixels is usually inappropriate.

After filtering, Amount computes

\[
B=(1-a)I+aF,
\]

where `a` is Amount. If a mask with effective weight `m` is enabled, the shared
process path then computes `(1-m)I+mB` (with `m` inverted when requested).
Amount `0` therefore restores the source pixels, but does not skip the expensive
bilateral call.

## Parameter interactions and starting points

- **Diameter × Sigma Space** controls support. With Diameter `5`, increasing
  Sigma Space cannot reach beyond that five-pixel neighborhood. A displayed
  Diameter below `1` truncates to `0`; like exact `0`, it selects automatic
  support, which grows with Sigma Space up to radius `32` and can sharply
  increase runtime.
- **Sigma Color × full scale** controls edge selectivity. For a scalar
  full-scale difference of `1`, Sigma Color `30` gives a range weight of about
  `exp(-1/1800)=0.99944`; the default is therefore almost range-insensitive at
  every sample type.
- **Zero sigma × any other setting** is an identity endpoint. If either sigma is
  `0`, the process returns the source before Diameter, weights, or Amount can
  create a bilateral effect.
- **Link Sigma controls** equates two unlike quantities. A value useful in
  pixels is normally too broad as a fraction of full scale; a subunit color
  sigma makes the spatial falloff very narrow.
- **Amount × mask** produces two consecutive blends. Amount reduces the global
  effect; the mask then reduces or localizes that already blended effect.
- **Channel × Sigma Color** changes what counts as an edge. RGB judges each
  stored channel by its own differences, so an edge strong in one channel and
  weak in another is protected in the first and smoothed in the second; a
  luminance component judges one plane and can avoid direct smoothing of the
  other components.
- **Diameter fractions and even values** are misleading: the UI accepts half
  steps and two decimal places, but the process truncates first and then takes
  half the integer as the radius. For example, `0.50` becomes automatic; `4.99`
  truncates to `4` and uses effective diameter `5`; and `5.99` truncates to `5`,
  also using effective diameter `5`.

## Inputs, outputs, and metadata

- Output dimensions, channel count, and source pixel type are retained. Integer
  output is rounded to whole codes; double input is processed at float32
  precision.
- The result keeps the source image's header cards and metadata.
- The process does not update linear/nonlinear processing-state provenance. Do
  not infer state only from the presence of the history step.
- Still Image History records Diameter, Sigma Space, Sigma Color, Amount, and,
  for a colour image, ` in channel: <channel>`. The description does not record
  whether the sigma controls were linked.
- A still CFA mosaic is treated as one stored sample plane unless debayered
  first. Shared sequence preprocessing may auto-debayer configured CFA frames,
  so the successful replacement sequence can be RGB.
- Sequence frames use one captured parameter set, are processed to temporary
  storage, and replace the original sequence only after success. Cancel/error
  removes temporary output.
- **Take snapshot** creates a still from the current preview scope. It does not
  change the target or bake display autostretch into pixel values.

## Performance

For image width `W`, height `H`, channels `C`, and window radius `r`, direct
bilateral filtering is roughly proportional to `W × H × C × πr²`, because the
weights depend on the centre sample and cannot be split into two passes. A
radius above `2` (Diameter above `5`) is noticeably slower. Automatic Diameter
mode (every displayed value below `1` after truncation) hides the effective
radius, which grows with Sigma Space up to `32` - about 3,200 neighbours per
sample; combining it with a large spatial sigma is the highest-risk setting for
long previews.

The engine takes the window and, when it can, one bordered single-precision
copy of a plane, reused for every channel, and filters on the threads the
operation is granted. The destination and preview/component/mask buffers add
memory as needed. Sequence mode adds decode/encode buffers and temporary disk
roughly comparable to an output sequence. No production timing on
astronomical data is claimed.

## Worked example

This source-and-calculator example was not executed on an approved astronomical
dataset and makes no visual-quality claim.

For two scalar samples separated by one pixel and differing by `0.20`, with
Sigma Space `2` and Sigma Color `0.10`, their unnormalized weight is

```text
spatial weight = exp(-(1^2) / (2 × 2^2))       = 0.8825
range weight   = exp(-(0.20^2) / (2 × 0.10^2)) = 0.1353
combined       = 0.8825 × 0.1353                = 0.1194
```

Keeping Sigma Space `2` but using the live Sigma Color `30` changes the range
weight to about `0.99998`, so the combined weight is about `0.8825`: the value
edge is barely protected. If the full filter result at a pixel is `0.30`, the
source is `0.20`, Amount is `0.50`, and the mask weight is `0.25`, then

```text
amount blend = 0.20 × 0.50 + 0.30 × 0.50 = 0.25
mask blend   = 0.20 × 0.75 + 0.25 × 0.25 = 0.2125
```

Use the equations to predict direction, then verify real structure at 100%
zoom. For RGB, apply the same calculation to each channel's own difference.
For an unsigned 16-bit image, Sigma Color `0.10` is compared with differences
in codes as `0.10 × 65535 = 6553.5`.

## Warnings and limitations

- The live Sigma Color default `30` resembles an 8-bit-domain value, but it is a
  fraction of full scale. At ordinary `[0,1]` amplitudes it provides almost no
  edge discrimination.
- Link Sigma controls ties pixel distance to a fraction of full scale. It is
  syntactically valid but usually not a meaningful physical relationship.
- Diameter is displayed as a double but truncated to integer. Every displayed
  value below `1` becomes automatic, and positive integers are normalized to an
  odd effective diameter with a minimum of `3`.
- Automatic support is held to radius `32`. Sigma Space above about `21` no
  longer widens the window, only the weights inside it.
- Either sigma at `0` bypasses the bilateral calculation and returns the source.
  This endpoint differs from merely choosing a very small positive sigma.
- RGB channels are filtered separately, each judged by its own differences,
  not by a joint color distance or the perceptual CIE Lab metric discussed as
  an option in the original paper. Strong RGB smoothing can shift weak color or
  create broad color plateaus.
- Bilateral smoothing can produce piecewise-smooth/cartoon-like regions,
  gradient flattening, and halos or blotches around structures whose contrast
  is near the chosen range scale. It can also retain noise when Sigma Color is
  too narrow.
- NaN and infinite samples are not repaired; the filter spreads them into their
  neighbours.
- Apply automatically resets Diameter, both sigmas, Link, and Amount after
  success. Target, Channel, Preview, mask, and preview-toolbar state remain.

## Troubleshooting

| Symptom | Likely cause | Check or correction | Evidence |
| --- | --- | --- | --- |
| Edges blur almost like Gaussian smoothing at defaults | Sigma Color `30` is extremely broad as a fraction of full scale | Reduce Sigma Color independently; leave Link off; compare one edge at 100% | Bilateral equation |
| Link makes the result ineffective or overly broad | Spatial and range sigmas have different units | Uncheck Link and tune pixels separately from the fraction of full scale | Process link handler |
| Changing Diameter from `5.0` to `5.5` does nothing | Positive double value is truncated, and the radius is half the integer | Use deliberate odd values; remember that `4` and `5` both use effective diameter `5` | Process radius mapping |
| A positive Diameter below `1` becomes unexpectedly slow | Truncation produces integer `0`, which selects automatic support | Use Diameter `1` or greater for fixed support; prefer an odd displayed value | Process radius mapping |
| Raising Sigma Space with Diameter `0` stops widening the effect | The derived radius is held to `32` | Expected; the weights inside the window still change | Derived-radius limit |
| Sigma at `0` produces no visible effect | The process returns the source when either sigma is at or below its epsilon | Set both sigmas to positive values, then tune them independently | Zero-sigma endpoint |
| Preview becomes unexpectedly slow | Automatic Diameter lets a large Sigma Space choose a large neighborhood | Restore Diameter `5`, reduce Sigma Space, or use Preview area | Derived radius |
| Faint stars become flat or blotchy | Range/spatial support is too broad or Amount too high | Reduce Diameter/Sigma Space/Amount or narrow Sigma Color; compare original | Bilateral weighting and Amount blend |
| Colors shift | Separate per-channel range smoothing or a derived-component round trip | Compare RGB with a deliberate luminance component on a duplicate; inspect View Channel | Per-channel filtering and Channel dispatcher |
| Preview differs from Apply | Preview area, mask, target/channel, or display stretch changed | Match all controls and one Display Mode; Apply always processes the full target | Shared preview/process path |

## References and provenance

- [OpenCV `bilateralFilter` documentation](https://docs.opencv.org/master/d4/d86/group__imgproc__filter.html)
  defines the filter AfterNight's engine reproduces: Diameter behavior, both
  sigma effects, the circular window, and the default border argument.
  AfterNight's process and engine code remain authoritative for the values
  actually supplied, the per-channel filtering, and the radius limit of `32`.
- [OpenCV border types](https://docs.opencv.org/master/d2/de8/group__core__array.html)
  defines `BORDER_DEFAULT` as `BORDER_REFLECT_101` and illustrates the reflected
  sample pattern.
- [Tomasi and Manduchi, “Bilateral Filtering for Gray and Color Images,” ICCV
  1998](https://www.cs.jhu.edu/~misha/ReadingSeminar/Papers/Tomasi98.pdf)
  is the primary paper for the spatial-and-range weighting model. AfterNight
  filters stored channels separately rather than claiming every color-space
  option discussed in the paper.

## Related workflow steps

- [Gaussian Blur](gaussian-blur.md) is a linear spatial smoother without a
  sample-difference weight; it blurs across edges more predictably.
- [Median Filter](median-filter.md) uses local order statistics and is often a
  better comparison for isolated impulse noise.
- [Total Variation Denoise](total-variation-denoise.md) uses an iterative
  gradient regularizer with different edge and staircasing trade-offs.
- [Process Windows](../../interface/process-windows.md), [Image Viewer](../../interface/image-viewer.md),
  [Masks](../../interface/masks.md), and [Image History](../../interface/image-history.md)
  explain the shared controls and verification workflow.
