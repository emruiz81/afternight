---
kind: process
stage: finish
menu: Process > Adjustments > Brightness/Contrast
summary: >-
  Offsets the brightness of an image and expands or compresses its contrast around mid-gray, for small corrections
  after the stretch.
data_in: nonlinear
data_out: nonlinear
---
# Adjust Brightness/Contrast

## Purpose and expected result

Adjust Brightness/Contrast first adds a fixed normalized offset, clamps it, then
expands or compresses values around a `0.5` pivot and clamps again. Use it for a
small display-tone correction after the main stretch, not as exposure control
or a substitute for a purpose-built stretch.

The process can transform stored RGB channels together, one stored channel, or
one converted color-space component before reconstructing RGB.

## At a glance

| Item | Current behavior |
| --- | --- |
| Menu path | **Process > Adjustments > Brightness/Contrast** |
| Enabled when | A grayscale/RGB still or displayable sequence is active, this process is not already attached, and no attached process holds exclusive access |
| Inputs | One grayscale/RGB still or sequence in any sample type, read in normalized units (in a stored channel a sample holding NaN passes through unchanged and an infinity ends at `0` or `1`); optional active mask; RGB exposes 21 channel/component choices |
| Outputs | Same geometry/pixel type in every channel mode; converted component modes reconstruct RGB; successful sequence Apply uses a temporary replacement sequence; snapshot creates a still |
| Recommended image state | Nonlinear display data after the main stretch; every Apply result is recorded nonlinear |
| Preview support | Exact (runs Apply's own program), histogram, original/result switch, Preview area, snapshot, fixed-linear display |
| Sequence support | Temporary replacement sequence with progress and Cancel; automatic CFA debayer may change decoded frame format |
| Mask support | Active mask post-blends source/result; white applies, black protects, gray blends, inversion reverses weights |
| External dependencies | None; bundled engine |

## Before you start

Perform calibration, integration, background correction, color calibration, and
the intended primary stretch first. Duplicate the target. Strong Brightness or
Contrast clips information and cannot be undone after saving.

Component selections are powerful but literal: applying a scalar brightness/
contrast curve to encoded hue or chroma coordinates does not preserve circular
hue relationships or perceptual uniformity. Prefer a purpose-built color tool
unless you have a specific component-space reason.

## Procedure

1. Duplicate the target and inspect any active mask/inversion.
2. Open **Process > Adjustments > Brightness/Contrast**.
3. Leave **Channel** at **RGB** (or disabled **Grayscale**) for a conventional
   adjustment. Choose a component only deliberately.
4. Start near `Brightness -10..10` and `Contrast -10..10`. Tune Brightness
   first because its clamp occurs before Contrast.
5. Inspect black/white endpoint counts, background, bright stars, noise, color
   balance, and mask edges. Use Preview area for a representative region only.
6. Compare with **Switch image**, then Apply to the duplicate. Preview runs the
   same program as Apply, so the whole-image Preview shows the values Apply writes.
7. Verify Image History, processing state, and metadata before saving.

## Complete parameter and action reference

| UI label/action | Type and unit | Runtime default | Persisted-default exception | Valid values, step, or constraints | Effect of increasing, decreasing, or selecting | Dependencies and enable/visibility rules | Quality, artifact, runtime, memory, or disk trade-off | Scope: preview/apply, channel, mask, still/sequence | Persistence |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Histogram plot | Graph, normalized intensity/count | Source or Preview | None | Binning follows Plot resolution | Shows source or mask-aware Preview and approximate endpoint counts | Show histogram controls visibility | Recalculation adds work; endpoints bin-dependent | Display only | Recomputed |
| Left-drag in histogram | Graph gesture | No readout | None | Hold left/move | Shows normalized coordinates | Histogram shown | Diagnostic only | Display only | Ends on release |
| Middle-drag in histogram | Graph gesture | Fitted/unpanned | None | Hold middle/drag | Pans graph | Histogram shown | No output effect | Display only | Until reset/close |
| Ctrl+mouse wheel in histogram | Graph gesture | Fitted size | None | Up `1.20x`; down `0.8x` | Zooms around cursor | Interactive graph | Rendering only | Display only | Window lifetime |
| Zoom in | Toolbar action | Fitted | None | Each `1.30x` | Enlarges plot | Disabled with hidden histogram | Rendering only | Display only | Window lifetime |
| Zoom out | Toolbar action | Fitted | None | Each `0.70x` | Shrinks plot | Disabled with hidden histogram | Rendering only | Display only | Window lifetime |
| Zoom reset | Toolbar action | — | None | One action | Fits/resets graph | Disabled with hidden histogram | No output effect | Display only | Immediate |
| Show grid | Toggle | Checked | None | Checked/unchecked | Shows/hides grid | Disabled with hidden histogram | Display aid | Display only | Window lifetime |
| Show clipping info | Toggle | Checked | None | Checked/unchecked | Shows/hides endpoint-bin counts | Disabled with hidden histogram | Estimate, not exact float clipping | Display only | Window lifetime |
| Absolute scales | Toggle | Unchecked | None | Checked/unchecked | Normalizes each displayed channel to its own peak | Disabled with hidden histogram | Can conceal relative channel strength | Display only | Window lifetime |
| Draw histogram areas | Toggle | Unchecked | None | Checked/unchecked | Filled-area versus line graph | Disabled with hidden histogram | Overlap can obscure channels | Display only | Window lifetime |
| Show histogram | Toggle | Checked | None | Checked/unchecked | Hides/clears or reloads graph/toolbar | Idle | Saves graph work only | Display only | Window lifetime |
| Plot resolution | Combo, bins | **8-bit (256)** | None | **6-bit (64)** through **16-bit (64K)** plus **100/1000/10000 levels** | Refines graph/endpoint estimate | Disabled with hidden histogram | More graph memory/rendering; output unchanged | Display only | Window lifetime |
| Brightness | Integer/slider, code-value units | `0` | None | `-150..150`, step `1`; offset `B/255` = about `-0.5882..+0.5882` | Adds offset, then clamps before Contrast | Idle; Apply disabled only when both controls zero | Moves every selected scalar equally; early clipping permanently flattens ends | Preview/Apply; selected channel/component; mask post-blend; still/sequence | Captured; Reset/success `0` |
| Contrast | Integer/slider, mapped strength | `0` | None | `-100..100`, step `1`; factor `259(C+255)/(255(259-C))` ≈ `0.4385..2.2677` | Scales around `0.5` after Brightness/clamp; negative compresses, positive expands | Idle | Positive clips shadows/highlights; negative flattens toward mid-gray | Preview/Apply; selected channel/component; mask post-blend; still/sequence | Captured; Reset/success `0` |
| Channel | Combo | **RGB** for RGB; **Grayscale** and disabled for mono | Reset/success returns RGB for RGB | RGB; Red; Green; Blue; CIE L*, a*, b*, c*, h*; CIE X, Y, Z; HSL Hue/Saturation/Lightness; HSV Hue/Saturation/Value; YCbCr Luma/Blue Chroma/Red Chroma | Chooses stored planes or converts, transforms one normalized component, and reconstructs RGB | Component list only for RGB | Conversion/reconstruction cost; reconstruction saturates an integer image at its range but can leave a floating one outside `0..1`; hue components are treated linearly, not circularly | Preview/Apply, selected component; still/sequence | Window lifetime until Reset/success |
| Target Image | Grayscale/RGB selector | Attached/active target | None | Eligible still/sequence | Refreshes source/channel/histogram/Preview | Supported view only | Selected target is replaced | Source and active mask | Target lifecycle |
| Preview | Checkbox | Checked | None | Checked/unchecked | Creates/destroys async fixed-linear Preview | Not locked while Apply runs | Runs Apply's own program, so it shows the values Apply writes | Preview/current displayed frame | Window lifetime |
| Preview pane | Viewer | Current result | None | Viewer zoom/pan/channel controls | Shows mask-aware result | Exists with Preview | Same values as Apply; inside a Preview area, Apply's result for an image of that area | Preview only | Window lifetime |
| Switch image | Preview action | Result shown | None | Click, **Space**, right-click, or **Ctrl+Shift+O** | Toggles result/original | Preview exists | Comparison only | Display only | Preview lifetime |
| Preview area | Preview checkbox | Unchecked | None | Checked/unchecked; move/resize overlay | Processes selected area and composites it over the original | Preview exists | Reduces Preview work; does not limit Apply | Preview/snapshot; mask cropped consistently | Window lifetime |
| Take snapshot | Preview action | None | None | Click or **Ctrl+Shift+K** | Creates `<target> - Adjust Brightness And Contrast - SNAPSHOT` | Preview exists | Allocates an auxiliary still of the Preview result; respects Preview area/mask; not given Apply's nonlinear record | Auxiliary still | Creates a view |
| Apply | Button/action | Disabled at `B=0,C=0` | None | One action when nonidentity | Captures parameters, channel, target, and mask, then runs the fused program | While it runs, target, channel, both values, Apply, Reset, and Close are disabled, with or without Preview | Destructive, clips twice; reconstruction from a component can saturate an integer image again | Still/sequence replacement; mask applies | Success records history/resets values/channel |
| Reset | Button/action | Enabled idle | None | One action | Sets both values `0`; RGB channel returns RGB | Does not change target/Preview/histogram | No image change | Configuration only | Immediate |
| Close | Button/action | Enabled idle | None | One action | Closes without applying current values | Disabled while Apply runs | Closing the window another way during a run hides it at once; it closes when the run has landed, and a still Apply in place still lands. Closing the image view stops the run, which then publishes nothing | Window/execution | Values not saved |
| Sequence progress / Cancel | Shared progress/Cancel | Sequence Apply | None | Per-frame progress/Cancel | Writes temporary sequence; target replaced only after success | Decoded frames and selected component must be supported | Decode/encode/conversion/temp disk; the engine operation observes Cancel | Sequence Apply; same params/channel/mask | Cancel discards temp; success resets |

## How the algorithm works

For normalized selected value `x`, Brightness `B`, and Contrast `C`:

```text
b = B / 255
k = 259(C + 255) / (255(259 - C))
u = clamp(x + b, 0, 1)
y = clamp(k(u - 0.5) + 0.5, 0, 1)
```

Order matters: Brightness is applied and clipped before Contrast. The contrast
pivot is exactly `0.5`; with no brightness, `x=0.5` stays `0.5` for every C.
Apply runs the four steps as one fused pointwise program of the image engine
over the selected samples, with `b` and `k` computed in single precision. At
`C=0` the factor is exactly `1` and the contrast steps are skipped; at `B=0`
and `C=0` nothing is done.

`x` is the sample in normalized units: an unsigned integer divided by its
largest value, a signed integer counted from its smallest value to its largest,
and a floating sample as it is. Floating samples are worked where they are, in
their own precision, and a value outside `0..1` is brought into it by the first
clamp. Integer samples are converted into a Float32 working copy (Float64 for
32-bit integers) and narrowed back to the source type at the nearest code, ties
to even.

Preview runs the same program over the same samples, so it shows the values
Apply writes.

For RGB, **RGB** transforms all stored planes; Red/Green/Blue transform one
stored plane and preserve the others. Other selections convert to the named
space, transform one component with the same scalar equation, then reconstruct
RGB. The conversion reads the stored red, green, and blue as sRGB-encoded unless
the image states a linear RGB color space, and hands the component over in the
unit interval (CIE L\*a\*b\* and L\*C\*h packed into it, XYZ divided by the
white point, hues in turns) as a Float32 working plane (Float64 for a 64-bit
float image). Reconstruction saturates an integer image at its range but does
not clamp a floating one, which can keep values outside `0..1`. CIE components
are reconstructed through the exact inverse of the XYZ matrix and YCbCr through
coefficients derived from the BT.601 weights.

## Parameter interactions and starting points

| Combination | Conservative start | Too weak | Too strong | Adjustment order |
| --- | --- | --- | --- | --- |
| Brightness + Contrast | `B=-10..10`, then `C=-10..10` | Tone remains flat/dim | Ends clip after one or both stages | Set offset first, then separation |
| Positive Brightness + Contrast | Watch highlights before raising C | Highlights little changed | Brightness clips them before C | Reduce B, then C |
| Negative Brightness + positive Contrast | Watch background | Background remains high | Shadows collapse to zero | Reduce negative B or C |
| Component channel + RGB | Start with RGB | Desired component unchanged | Hue/gamut artifacts | Use purpose-built color process if possible |
| Mask + tonal change | Inspect mask first | Region barely changes | Visible mask boundary | Feather/reduce upstream |

## Inputs, outputs, and metadata

- Apply preserves geometry and pixel type in every channel mode, and the result
  keeps the source's FITS cards, astrometric solution, and acquisition record,
  converted component modes included.
- Every Apply result is recorded nonlinear on the processing-state cards a
  saved file carries: `ANLIN` nonlinear, `ANBASIS` the basis of a stretched
  image (`display-rgb` for color and `monochrome` for grayscale, unless the
  source's cards already recorded a basis: a recorded `monochrome` is kept, and
  a recorded linear, camera, or display RGB becomes `display-rgb`), `ANASRC`
  process, and `ANOP` `brightness-contrast`. A linear encoding the source
  claimed is no longer claimed, and the image lineage gains an entry naming
  `an::executePointwiseProgram` with both values. The record is kept in every
  channel mode. Preview and snapshot results are not recorded.
- A pixel that the image's own validity marks as having no data is left as it was.
- A sequence Apply processes each frame as Apply processes a still image of it
  and writes the frames to a temporary sequence in the source's container; the
  target is replaced only after every frame succeeds.
- Active mask blending happens after the local result and can restore protected
  source samples. Preview area affects Preview/snapshot only.
- History uses **Adjust Brightness And Contrast** and records both numbers, for
  example `Adjust brightness to 40 and contrast to 25 in channel: RGB`; the
  ` in channel:` label (any Channel choice) is added only for a color target.
  It does not record the mask, clipping, or component-space reconstruction
  details. Undo restores the previous image.

## Performance

Apply and Preview run the same fused program, linear in selected pixels/frames,
on the operation's threads. An integer image is converted into a floating
working copy and back; component modes convert to a working plane and
reconstruct RGB; and Apply normally makes one further full-image copy to write
the nonlinear record onto the result. Preview cost also includes mask blend and
histogram. Sequences also require temporary output storage. No
production timing or peak-memory run occurred.

## Worked example

For `x=0.25`, **Brightness `+25`** gives
`u=0.25+25/255≈0.348039`. **Contrast `+50`** gives
`k≈1.482222`, so `y≈1.482222(0.348039-0.5)+0.5≈0.274760`.
The same Contrast leaves `u=0.5` unchanged.

At the control limits, Contrast factors are approximately `0.438528` at `-100`
and `2.267727` at `+100`. Brightness `+150` adds about `0.588235`, so any input
above about `0.411765` clips before Contrast. These checkpoints verify the
current scalar mapping, not a recommended astronomical setting.

## Warnings and limitations

- Brightness clips before Contrast; later lowering Contrast cannot recover
  samples already clamped by Brightness.
- Integer results are rounded to the nearest code, ties to even.
- Component modes treat hue coordinates as ordinary linear values and can break
  wrap continuity. Reconstruction to RGB saturates an integer image at its range;
  a floating image can keep values outside `0..1`.
- This is not exposure: the current [Exposure Correction](../stretching/exposure-correction.md)
  multiplies samples by its mapped scalar (and is itself not standard `2^stops`).
  Brightness adds `B/255`; Contrast then pivots around `0.5`.
- This is not a stretch: Histogram/GHT/ArcSinh provide deliberate nonlinear
  transfer shapes, black/mid/white controls, and better diagnostic feedback.
- A still Apply shows no progress window and has no Cancel button; closing the
  process window hides it until the Apply lands, and closing the image view
  stops it. A stopped or refused Apply publishes nothing and leaves the image
  and its history as they were.

## Troubleshooting

| Symptom or exact error | Verified cause/validation path | Recovery | Evidence source |
| --- | --- | --- | --- |
| Apply disabled | Both values are `0` | Set B or C nonzero | `AdjustBrightnessAndContrastProcess::settingsUpdatedImpl` |
| Highlights stay flat after lowering Contrast | Brightness clipped before Contrast | Reset/start from duplicate; lower Brightness | `AdjustBrightnessAndContrastProcess::executeTypedProcessIteration` (`an::buildBrightnessProgram`, `an::buildClampProgram`) |
| Mid-gray moves unexpectedly | Brightness changes value before the `0.5` pivot | Tune Brightness first or use `B=0` | `AdjustBrightnessAndContrastProcess::executeTypedProcessIteration` (`an::buildContrastProgram`) |
| Preview and Apply differ | Target, mask/inversion, Channel, or sequence frame changed after the last Preview; a Preview area shows only Apply's result for that area | Wait for the latest Preview; compare with Preview area off | `AdjustBrightnessAndContrastProcess::executeTypedProcessImpl` (one body for Preview and Apply); `an::ui::cropProcessRegion`, `an::ui::composeProcessRegion` |
| Floating values above `1` or below `0` end at `1` or `0` | The Brightness step holds its result to `0..1` before Contrast | Expected; rescale the data upstream if its range must be kept | `AdjustBrightnessAndContrastProcess::executeTypedProcessIteration` (`an::buildClampProgram`) |
| Color shifts in CIE/HSL/HSV/YCbCr mode | Scalar curve changed one encoded component; reconstruction saturated an integer image or left a floating color outside `0..1` | Use RGB or purpose-built color process | `an::ui::applyProcessChannelScope` |
| Saved image is marked nonlinear | Every Apply records its result nonlinear | Expected; return to the pre-Apply history step for linear work | `process_utils::recordNonlinearResult` |
| Sequence unchanged after Cancel | Frames are written to a temporary sequence published only on success; the engine operation observes Cancel | Expected; rerun and let every frame finish | `ImageRTPreviewControl::runOverSequence`; `process_utils::runNormalizedChain` |

## Related workflow steps

Use [Histogram Transformation](../stretching/histogram-transformation.md),
[Generalized Hyperbolic Transformation](../stretching/gh-transformation.md), or
[ArcSinh Stretch](../stretching/arcsinh-stretch.md) for the primary stretch.
Compare [Exposure Correction](../stretching/exposure-correction.md) for its
project-specific multiplicative mapping, and [Gamma Correction](../stretching/gamma-correction.md)
for a power curve. Use [Adjust HSL](adjust-hsl.md) or [Adjust LCh](adjust-lch.md)
for named color coordinates. Verify [Image History](../../interface/image-history.md)
and [Image Statistics](../../interface/image-statistics.md) after Apply.

## References and provenance

- AfterNight source is authoritative for the exact transfer, `0.5` pivot, parameter ranges, double clipping, normalized working units and rounding, channel conversions, metadata/state behavior, masks, and sequences.
- The worked values were calculated directly from the source equation; no third-party brightness/contrast convention is assumed.
