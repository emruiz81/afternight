---
kind: process
stage: finish
menu: Process > Adjustments > Gamma Correction
summary: >-
  Applies a power-law tone curve to the image or one of its color components, brightening or darkening the midtones.
data_in: nonlinear
data_out: nonlinear
---
# Gamma Correction

## Purpose and expected result

Gamma Correction applies a pure normalized power law to stored samples or one
selected RGB-derived component. AfterNight interprets the **Gamma** value `g` as
an inverse exponent: output is `x^(1/g)`. Values above `1` brighten midtones;
values below `1` darken them. Black and normalized white remain fixed for
finite input in `[0,1]`.

The expected visual result is a nonlinear, same-size still image in the source
pixel type. This is a display-oriented tone operation, not an exposure change,
camera linearization, sRGB encoding/decoding function, or color-management
transform. RGB applies the power independently to every stored channel; a
selected channel/component changes only that channel in its working color space
before RGB reconstruction.

Every Apply records that expected state. The result keeps the source's FITS
cards, astrometric solution, and acquisition record in every Channel mode, and
its processing-state cards mark it nonlinear with `gamma-correction` as the
operation; see **Inputs, outputs, and metadata**.

## At a glance

| Item | Current behavior |
| --- | --- |
| Menu path | **Process > Adjustments > Gamma Correction** |
| Enabled when | An image or sequence is active, the same process is not already attached to it, and no attached process holds exclusive access |
| Inputs | One intended normalized grayscale or RGB still or sequence; finite samples in `[0,1]` give predictable power evaluation; an optional active mask must match target geometry |
| Outputs | Destructively replaces a still or, after successful temporary processing, a sequence; **Take snapshot** creates an auxiliary still without replacing the target; component modes reconstruct RGB; automatic sequence CFA debayer can change frame format/channels; no file is written directly |
| Recommended image state | Usually nonlinear display data for a final tonal adjustment; applying to linear data itself creates a nonlinear result, and the result is recorded as nonlinear |
| Preview support | Asynchronous RT preview of the full image or a selected preview area, plus a histogram of the mask-aware result; every plane previews through the operation Apply uses - an exact table over the codes of an unsigned 8- or 16-bit stored channel, the power law at the samples' own precision otherwise. While you drag **Gamma**, the preview works on what the preview view shows - the image reduced to the view's size, or the part shown at 1:1 and closer - and the histogram stays the whole image's; the preview you rest on is computed on the whole image. In autostretch the preview is stretched as the target is, so the stretch does not move while you adjust ([Process windows](../../interface/process-windows.md)) |
| Sequence support | Supported: each decoded frame is processed exactly as a still image of it would be, into a temporary sequence that replaces the target only after every frame succeeds, with per-frame progress and Cancel; the configured CFA auto-debayer request can produce RGB frames |
| Mask support | Supported for preview, still Apply, and sequence Apply by post-transform blending; white applies, black protects, gray blends, and **Invert Mask** reverses weights |
| External dependencies | None; bundled engine only; no tool, profile, catalog, or network service |

## Before you start

- Finish calibration, background correction, and color calibration before
  display-oriented gamma work. Gamma cannot recover clipped values or calibrate
  channels, and its result is recorded as nonlinear.
- Use finite normalized input. Apply has no clamp: floating samples above `1`
  are raised as they are, and a negative floating sample becomes not-a-number
  when the exponent `1/g` is not a whole number (on Float32 images even a
  whole-number exponent can give not-a-number). The preview does the same.
- Decide whether you want all stored RGB samples or one component. **RGB** is
  not color-preserving luminance scaling: a nonlinear power changes channel
  ratios. Hue/chroma/component modes can be even more disruptive.
- Prefer a lightness/luminance/value component only when RGB reconstruction is
  acceptable: on a floating image the reconstructed color is not clamped to
  `[0,1]`, and on an integer image it saturates at the type's range.
- The preview is what Apply writes on every plane: an exact table over the
  codes of an unsigned 8- or 16-bit stored channel, the power law at the
  samples' own precision otherwise.
- For a sequence, verify the selected Channel, active mask, and CFA auto-debayer
  setting before Apply. The same captured Gamma/Channel selection is used for
  every decoded frame; cancellation discards the temporary output.
- Duplicate a still or be prepared to use Image History. Apply replaces the
  target after success and then resets Gamma and Channel.

## Procedure

1. Select a still or sequence and open **Process > Adjustments > Gamma Correction**. Confirm
   **Target Image**, leave **Channel: RGB**, and keep **Preview** selected.
2. Move **Gamma** above `1` to brighten midtones or below `1` to darken them.
   The identity value `1` disables Apply.
3. Inspect the histogram endpoints and 100% view. A normalized RGB power curve
   preserves exact black/white endpoints but can change saturation between them.
   On a large image, check **Preview area** on the preview view to tune on one
   region.
4. If processing one channel/component, choose it explicitly and inspect color
   and gamut. Avoid ordinary gamma on hue/chroma unless a deliberate color remap
   is intended.
5. If a mask is active, judge the mask-aware preview. Histogram clipping refers
   to displayed result bins, not mathematical clipping introduced by gamma.
6. Select **Apply**. For a sequence, monitor the progress dialog and use Cancel
   to preserve the original sequence. The applied result is recorded as
   nonlinear, and Image History holds one step that Undo reverses.

## Complete parameter and action reference

| UI label/action | Type and unit | Runtime default | Persisted-default exception | Valid values, step, or constraints | Effect of increasing, decreasing, or selecting | Dependencies and enable/visibility rules | Quality, artifact, runtime, memory, or disk trade-off | Scope: preview/apply, channel, mask, still/sequence | Persistence |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Histogram plot | Graph, normalized intensity/count | Source or current preview; RGB channels overlaid | None | Binning comes from **Plot resolution** | Shows source or mask-aware preview distribution and approximate endpoint-bin clipping; never changes samples | **Show histogram** controls visibility | Recalculation adds work; endpoint counts are resolution-dependent | Display only; shown source/preview channels | Recomputed after source, preview, or resolution changes |
| Left-drag in histogram | Graph gesture | No readout | None | Hold left button and move | Shows normalized coordinate readout | Histogram must be shown | Diagnostic only | Display only | Ends on release |
| Middle-drag in histogram | Graph gesture | Fitted/unpanned | None | Hold middle button and drag | Pans within graph scroll area | Useful only beyond viewport | No output effect | Display only | Until reset/window close |
| Ctrl+mouse wheel in histogram | Graph gesture | Fitted size | None | Up `1.20x`; down `0.8x`; plain wheel scrolls | Zooms around cursor | Interactive graph enabled | Rendering area only | Display only | Window lifetime; process Reset does not change it |
| Zoom in | Toolbar action | Fitted size | None | Each action `1.30x` | Enlarges plot | Disabled with hidden histogram | Rendering only | Display only | Window lifetime |
| Zoom out | Toolbar action | Fitted size | None | Each action `0.70x` | Shrinks plot | Disabled with hidden histogram | Rendering only | Display only | Window lifetime |
| Zoom reset | Toolbar action | — | None | One action | Fits plot/resets scrolling | Disabled with hidden histogram | Rendering only | Display only | Immediate |
| Show grid | Toggle | Checked | None | Checked/unchecked | Shows/hides grid | Disabled with hidden histogram | Display-only aid | Display only | Window lifetime; hiding histogram resets grid internally |
| Show clipping info | Toggle | Checked | None | Checked/unchecked | Shows/hides histogram endpoint-bin counts | Disabled with hidden histogram | Binning-dependent estimate; not exact floating clipping | Display only | Window lifetime |
| Absolute scales | Toggle | Unchecked | None | Checked/unchecked | Checked normalizes each visible histogram channel to its own peak | Disabled with hidden histogram | Can conceal relative channel strength; no image normalization | Display only | Window lifetime |
| Draw histogram areas | Toggle | Unchecked | None | Checked/unchecked | Filled-area versus line drawing | Disabled with hidden histogram | Area overlap can obscure channels | Display only | Window lifetime |
| Show histogram | Toggle | Checked | None | Checked/unchecked | Hides/clears graph and disables toolbar, or reloads source/preview | Always available while idle | Hiding saves graph work, not preview processing | Display only | Window lifetime; process Reset does not change it |
| Plot resolution | Combo box, bins | **8-bit (256)** | None | **6-bit (64)**, **7-bit (128)**, **8-bit (256)**, **9-bit (512)**, **10-bit (1024)**, **12-bit (4096)**, **14-bit (16K)**, **16-bit (64K)**, **100 levels**, **1000 levels**, **10000 levels** | More bins refine the graph/endpoint estimates | Disabled with hidden histogram | Higher bins cost graph memory/rendering; output precision unchanged | Display only | Window lifetime |
| Gamma | Double value plus slider, unitless | `1.00` identity | None | `0.01..9.90`; two decimals; spin step `0.03`; slider linear over 1,000 positions | Above `1` uses exponent below `1` and brightens midtones; below `1` darkens; endpoints stay fixed for normalized finite samples | Enabled while idle; Apply disabled exactly at `1.0` | Extreme values compress most tones toward white/black and amplify quantization/banding | Preview/Apply; selected channel/component; mask post-blend; still/sequence | Captured per execution; Reset/success returns `1.0` |
| Target Image | Image selector | Attached/active target | None | Open eligible image or sequence | Changes source, Channel list, histogram, Preview, Apply | Active image required; duplicate/exclusive gates apply | Wrong target is destructively replaced; sequence writes a temporary result and replaces only after success | Preview/Apply source and active mask | Target lifecycle; Reset does not change it |
| Channel | Combo box | **RGB** for RGB; disabled **Grayscale** for mono | None | **RGB**, **Red**, **Green**, **Blue**, **CIE L* (Lightness)**, **CIE a***, **CIE b***, **CIE c***, **CIE h***, **CIE X**, **CIE Y (Luminance)**, **CIE Z**, **HSL Hue**, **HSL Saturation**, **HSL Lightness**, **HSV Hue**, **HSV Saturation**, **HSV Value**, **YCbCr Luma**, **YCbCr Blue Chroma**, **YCbCr Red Chroma** | RGB/Grayscale powers stored samples; Red/Green/Blue powers one stored channel; other modes convert, power one encoded component, and reconstruct RGB | Component choices require RGB; mono selector disabled | RGB can shift saturation; component modes add conversion work and gamut risk (a floating reconstructed color is not clamped to `[0,1]`, an integer one saturates at the type's range); source metadata is kept in every mode | Preview/Apply; selected component; mask post-blend; still/sequence | Captured by current selection; Reset/success returns RGB when target is RGB |
| Preview | Checkbox | Checked | None | Checked/unchecked | Enables/disables the asynchronous preview and switches histogram result/source | Process idle | Result/histogram buffers cost time/memory; the preview is what Apply writes | Preview only; mask-aware; current still/current displayed sequence frame | Window lifetime; Reset does not change it |
| Preview pane | Full-image viewer | Current preview result | None | Standard viewer zoom/pan/display controls | Shows mask-aware result | Exists only with Preview | Holds result image; component conversions add buffers | Preview only; target unchanged | Window lifetime |
| Switch image | Preview-toolbar action | Gamma preview shown | None | Click; **Space**; right-click; **Ctrl+Shift+O** | Toggles the preview viewer between the preview result and original target | Exists only with Preview | Display comparison only; no recomputation or pixel change | Preview display only; still/current displayed sequence frame | Preview-window lifetime |
| Preview area | Preview-toolbar checkbox | Unchecked | None | Checked/unchecked; move/resize the selection overlay | Limits preview and snapshot processing to a selected area and composites that result over the original full image; inside the area the preview is the preview of an image of that area, so it is what Apply writes there | Exists only with Preview | Reduces table/component work while tuning; does not limit final Apply | Preview and Take snapshot only; mask is cropped consistently | Preview-window lifetime; Reset does not change it |
| Take snapshot | Preview-toolbar action | No snapshot | None | Click or **Ctrl+Shift+K** | Re-executes the current preview path at source depth and opens a new still named `<target> - Gamma Correction - SNAPSHOT` | Exists only with Preview | Allocates and processes an auxiliary image; active mask and selected preview area are honored | New still image; target is unchanged; a snapshot is a preview result and is not recorded as nonlinear | Each action creates a separate image view |
| Apply | Button/action | Disabled at Gamma `1.0` | None | One action at non-identity value | Captures Gamma and runs the engine's gamma program over the selected channel for a still or each decoded sequence frame | Target must lock; controls disabled during the run | One parallel engine pass; integer types other than unsigned 8- and 16-bit add a floating working copy; component conversion and masks add passes; recording the nonlinear state usually copies the result once more | Destructive still/sequence replacement; active mask applies | Success records one history step, resets Gamma and Channel |
| Reset | Button/action | Enabled while idle | None | One action | Restores Gamma `1.0` and RGB for RGB target | Does not change Target, Preview, or histogram toolbar/zoom | No image change | Configuration only | Immediate |
| Close | Button/action | Enabled while idle | None | One action | Closes without applying current settings | Disabled with other controls during Apply | No output | Window/execution | No settings persist after destruction |
| Sequence progress / Cancel | Shared modal progress dialog and Cancel action | Created automatically when sequence Apply starts | None | One progress update per frame; cooperative Cancel | Processes decoded frames into a temporary sequence and replaces the target only after success | Sequence target only | Sequence processing adds decode/encode and temporary-disk cost; automatic CFA debayer can produce RGB frames | Sequence Apply; chosen channel and active mask apply to every frame | Cancel or a failed frame discards the temporary sequence; success replaces the sequence, records one history step, and resets settings |

## How the algorithm works

### Direct power law

For a normalized stored or encoded-component sample `x` and UI value `g`, Apply
computes

\[
y=x^{1/g},\qquad 0.01\le g\le 9.9.
\]

For `x` in `[0,1]` and positive `g`, `0` and `1` remain fixed. If `g>1`, then
`1/g<1` and midtones rise; if `g<1`, the exponent exceeds `1` and midtones
fall. `g=1` is identity and Apply is disabled.

A sample is read in normalized units: an unsigned integer as a fraction of its
largest value, a signed integer counted from its smallest value to its largest,
and a floating sample as it is. How the power is evaluated depends on the
plane's representation:

- An unsigned 8- or 16-bit plane uses an exact table over every code, each entry
  the single-precision power of that code's normalized value, rounded to the
  nearest code.
- Float32 samples use a vectorized single-precision power where possible, held
  within a small stated error of the C library `pow`; Float64 samples use `pow`
  in double precision.
- Signed integers and unsigned 32-bit integers are converted to a floating
  working copy (Float64 for the two 32-bit types, Float32 otherwise), powered
  there, and rounded back to the nearest code.

The engine runs the program in one pass on the operation's threads. Integer
results are rounded to the nearest code, ties to even.

No clamp follows gamma. Integer input is intrinsically within its normalized
range. Floating input is assumed normalized and finite: values above `1` are
raised as they are and stay above `1`, while a negative sample becomes
not-a-number when the exponent is not a whole number. On a Float32 image the
vectorized power returns not-a-number for every negative sample it evaluates;
the last few samples of a row, and every sample under a validity rule other
than all-valid, go through `pow` instead, which gives a finite value when
`1/g` is a whole number. Samples that the image's own validity rule or mask
marks as invalid are left as they were.

### Preview

The preview runs the program Apply runs, on every plane: an unsigned 8- or
16-bit stored channel through its exact table, every other plane through the
power law at the samples' own precision, out-of-range input included. While
you drag **Gamma**, the preview works on what the preview view shows - the
image reduced to the view's size, or the part shown at 1:1 and closer - and
the preview you rest on is computed on the whole image. **Take snapshot**
runs the same preview path.

### Channel and color-component paths

- **RGB/Grayscale** applies the power independently to every stored sample.
- **Red**, **Green**, or **Blue** copies the other two channels unchanged and
  powers only the selected stored channel.
- CIE Lab/LCh/XYZ, HSL, HSV, and YCbCr choices convert the source's red, green,
  and blue to that working representation, power one component handed over in
  the unit interval, and reconstruct RGB. A source whose description says it is
  linear is read as linear RGB; any other is read as sRGB-encoded. A power curve
  on hue is linear across `0..1`, not circular across the hue wrap. Powers on
  signed-concept chroma components operate on their normalized encodings, not
  signed physical values.
- The component is computed in double precision and held in a floating working
  plane (Float64 for a Float64 image, Float32 otherwise), so an integer image's
  component is not rounded to integer codes before the power. Reconstruction
  converts back once: a floating result is not clamped to `[0,1]`, and an
  integer result is rounded to the nearest code and saturated at the type's
  range. CIE components are reconstructed through the exact inverse of the XYZ
  matrix, and YCbCr through unrounded luma-chroma coefficients.

After channel execution, Apply records the result as nonlinear, and an active
mask then blends source and result pixel by pixel.

## Parameter interactions and starting points

| Combination | Conservative starting point | Too weak looks like | Too strong looks like | Adjustment order |
| --- | --- | --- | --- | --- |
| Gamma + nonlinear display image | `1.05..1.20` to brighten or `0.95..0.85` to darken | Midtones barely move | Background/noise lifts strongly or midtones collapse/band | Adjust near `1`, inspect histogram and 100% view |
| RGB + color balance | Use small changes after color calibration | Tonal correction insufficient | Channel ratios/saturation shift, bright colors crowd endpoints | Set tone gently, then inspect RGB channels/star colors |
| Lightness/luminance/value + gamut | Use a mild gamma | Global tone remains unchanged | Out-of-gamut colors after reconstruction: unclamped on a floating image, saturated on an integer image | Select component, apply small change, inspect colors |
| Hue/chroma + gamma | Avoid unless deliberately remapping color | Little color change | Hue wrap distortion, neutral shift, oversaturation | Prefer a dedicated color tool; if required, use tiny changes |
| Mask + Gamma | Tune globally near identity, then inspect blend | Targeted region changes too little | Mask boundary becomes visible or protected region still changes via display state | Inspect mask, reduce gamma change, correct mask upstream |

## Inputs, outputs, and metadata

- One still produces one modified still. Dimensions, channel count, and pixel
  type remain unchanged; component modes reconstruct RGB.
- The result keeps the source's FITS cards, astrometric solution, and
  acquisition record in every Channel mode, and is recorded as nonlinear on the
  processing-state cards a saved file carries: `ANLIN` becomes `nonlinear`,
  `ANASRC` `process`, `ANOP` `gamma-correction`, and `ANBASIS` the basis a
  stretched image of its kind is in (for example `display-rgb` for an RGB
  image). A linear transfer function the source claimed is no longer claimed,
  and the image's lineage gains an entry naming the engine operation
  (`an::buildGammaProgram`) and the gamma.
- Samples the image's validity rule or mask marks as invalid are left unchanged.
- Image History records one step named **Gamma Correction**, described as
  `Adjust gamma to <g>`, followed by ` in channel: <Channel>` for an RGB target
  (for example `Adjust gamma to 2.2 in channel: RGB`). It does not record mask
  state or Preview behavior. Undo restores the previous image.
- An active mask blends after gamma; the nonlinear record is kept on a masked
  result too.
- A **Take snapshot** image is a preview result and is not recorded as
  nonlinear.
- A run whose stop is requested (Cancel in a sequence's progress dialog, or the
  target closing while a run works) is stopped inside the engine operation and
  publishes nothing; the image and its history stay as they were.
- Sequence Apply processes each decoded frame as an Apply processes a still
  image of it, active mask included, writes the frames to a temporary sequence,
  supports Cancel from its progress dialog, and replaces the target only after
  every frame succeeds; a cancelled or failed run leaves no temporary output.
  The current CFA auto-debayer request is passed to the sequence reader. One
  history step is recorded.

## Performance

Apply is one engine pass over the selected samples on the run's threads.
Unsigned 8- and 16-bit planes use a table of every code; other integer types are
converted to a floating working copy and rounded back. The run works on a copy
of the target, and recording the nonlinear state usually copies the result once
more, so an Apply briefly holds the target and two full-size copies of it; if
the session's memory budget refuses one, Apply publishes nothing and reports the
refusal.
A preview while you drag applies the power over what the preview view shows;
the preview you rest on applies it over the whole image or the preview area,
and updates the chosen-resolution histogram.
RGB/Grayscale are the cheapest paths. One stored RGB channel needs a temporary
channel; component modes compute a floating working plane and convert back.
Masks add another full-image blend.

Disable Preview while making several changes on a very large target, use
**Preview area**, or test on a duplicate crop. Histogram resolution affects only
graph cost. No production timing was measured.

## Worked example

- **Input:** normalized samples `[0, 0.25, 0.5, 0.75, 1]` in RGB or Grayscale.
- **Gamma `2`:** direct Apply uses square root and gives approximately
  `[0, 0.5, 0.707107, 0.866025, 1]`; midtones brighten.
- **Gamma `0.5`:** direct Apply squares values and gives
  `[0, 0.0625, 0.25, 0.5625, 1]`; midtones darken.
- **Preview checkpoint:** the preview evaluates the same power law, so at input
  `0.5`, Gamma `2`, it shows approximately `0.707107`, as Apply does.
- **Inspect in real use:** endpoints, smooth gradients/banding, RGB ratios,
  mask transitions, and metadata/state after Apply.
- **Limitations:** No real-image visual, color, metadata round-trip, or timing
  result was measured. The numeric example proves only current transfer math.

## Warnings and limitations

!!! warning "Every Apply is recorded as nonlinear"
    Apply marks its result nonlinear on the processing-state cards with
    `gamma-correction` as the operation, in every Channel mode and with or
    without a mask. Finish steps that need linear data before Gamma Correction.

- Gamma uses `x^(1/gamma)`, not `x^gamma`. Above `1` brightens; below `1`
  darkens.
- The operation is not sRGB, ICC, monitor gamma, or file-format gamma handling.
  It powers current normalized stored values without decoding an external tone
  response.
- RGB mode changes channel ratios except at common endpoints/neutral equality.
- Hue is circular but Gamma treats its normalized encoding as an ordinary line.
  Chroma and opponent components are also encoded, not signed-domain powers.
- Apply does not clamp floating output, and negative floating samples can
  become not-a-number; the preview shows the same.
- Integer results are rounded to the nearest code, ties to even.
- In color-component modes a floating reconstructed color is not clamped to
  `[0,1]`.
- Apply is disabled only at exact Gamma `1.0`; Reset and successful Apply return
  Gamma to identity and Channel to RGB for RGB targets.

## Troubleshooting

| Symptom | Cause | Fix |
| --- | --- | --- |
| Gamma above `1` brightens instead of darkens | The gamma program encodes with exponent `1/gamma` | This is current behavior; use below `1` to darken |
| Apply is disabled | Gamma is exactly `1.0`, the identity | Choose a non-identity value |
| RGB saturation/color changes | Same nonlinear power is applied independently to stored R/G/B | Use a smaller change or evaluate a lightness/value mode on a duplicate |
| Hue changes abruptly near wrap | Hue is handed over as a fraction of a turn and powered linearly even though physical hue wraps | Avoid Hue Gamma; use a dedicated hue tool |
| A floating image has values outside `[0,1]` after a component mode | The powered component is converted back to RGB without clamping a floating result | Use a smaller change or a stored-channel mode |
| A later step treats the result as nonlinear | Apply records `ANLIN` `nonlinear` and `ANOP` `gamma-correction` on the result | Expected; run steps that need linear data first, or undo the step in Image History |
| Apply fails with an error naming the memory budget | The session's memory budget cannot hold the run's copy of the target | Close other images, work on a smaller image, or raise the memory limit on the Processing settings page (it takes effect at the next start); the target is unchanged |
| A cancelled sequence does not replace the target | The sequence run writes a temporary sequence and publishes it only when every frame succeeded; Cancel stops the run and leaves no output | This is expected; restart Apply when ready |
| Next operation is identity/RGB after Apply | Successful commit invokes Reset | Expected; re-enter Gamma and Channel |

## Related workflow steps

Finish [Background Neutralization](../color-calibration/background-neutralization.md)
and [Photometric Color Calibration](../color-calibration/photometric-cc.md) or
[Spectrophotometric Color Calibration](../color-calibration/spectrophotometric-cc.md)
before display tone work. Use [Histogram Transformation](histogram-transformation.md),
[Generalized Hyperbolic Transformation](gh-transformation.md),
[ArcSinh Stretch](arcsinh-stretch.md), or
[Statistical Stretch](statistical-stretch.md) for a main stretch. Compare
[Exposure Correction](exposure-correction.md) when you want the current
multiply-and-clamp scalar adjustment rather than a fixed-endpoint power law;
its displayed value is not calibrated in standard stops. Continue with
[Curves Transformation](curves-transformation.md) for deliberate finishing and
verify [Image History](../../interface/image-history.md),
[Image Statistics](../../interface/image-statistics.md), and critical metadata.
