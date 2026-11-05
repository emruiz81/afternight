# Adjust HSL

## Purpose and expected result

Adjust HSL rotates hue, scales saturation, and offsets HSL lightness for every
pixel of an RGB image. Use it for a deliberate display-color adjustment after
color calibration and the main stretch. The successful result replaces the
target; Preview snapshots create separate stills.

This is a nonlinear, display-oriented operation. It works on current normalized
stored RGB values and does not decode a camera profile or an ICC working space.
Start on a duplicate, especially when color accuracy or metadata matters.

## At a glance

| Item | Current behavior |
| --- | --- |
| Menu path | **Process > Adjustments > Hue/Saturation/Lightness** |
| Enabled when | A color image (red, green, and blue) or a sequence showing one is active; the menu action is disabled for grayscale. A launch is refused with a warning when this process is already attached to the image or an attached process holds exclusive access |
| Inputs | One RGB still, or a sequence whose decoded frames are RGB, in any integer or floating representation; optional enabled active mask. The whole image is processed; there is no channel selector |
| Outputs | Replaces the target at the same geometry, representation, and RGB format; a successful sequence Apply writes a temporary replacement sequence; Take snapshot creates an auxiliary still |
| Recommended image state | Nonlinear display data after color calibration and the main stretch; the process marks committed output nonlinear |
| Preview support | Asynchronous preview that runs the same adjustment as Apply over the target, or over the Preview area; fixed linear display, histogram, original/result switch, Preview area, and snapshot |
| Sequence support | Every decoded frame is processed as Apply processes a still image of it, into a temporary replacement sequence, with per-frame progress and Cancel; every decoded frame must be RGB unless configured automatic CFA debayer supplies RGB |
| Mask support | Active target mask post-blends source and transformed result for Preview, snapshot, still Apply, and sequence Apply; white applies, black protects, gray blends, and Invert Mask reverses weights |
| External dependencies | None; bundled AfterNight engine only |

## Before you start

Complete background and color calibration, then perform the intended main
stretch. HSL Lightness is the average of the largest and smallest stored RGB
components, not perceived CIE lightness. Equal HSL Lightness values can look
very different across hues.

Work on an RGB duplicate. Floating samples are read as they are and every
processed pixel is clamped to `0..1`, so values above 1 or below 0 do not
survive. **Preview area** is a safe way to test a region: inside the area the
Preview is what Apply makes of that area.

## Procedure

1. Select a nonlinear RGB duplicate and open **Process > Adjustments >
   Hue/Saturation/Lightness**.
2. Keep **Preview** on; enable **Preview area** to work on a region. Use the
   same viewer display mode while comparing the Preview with the original.
3. Rotate **Hue** first, then adjust **Saturation**, and finish with small
   **Lightness** changes. The three controls are global and interact.
4. Inspect neutrals, star colors, saturated regions, histogram endpoints, and
   any active-mask boundary.
5. Select **Apply**. The Preview shows what Apply writes; recheck the committed
   RGB result, its history step, and its metadata.

## Complete parameter and action reference

| UI label/action | Type and unit | Runtime default | Persisted-default exception | Valid values, step, or constraints | Effect of increasing, decreasing, or selecting | Dependencies and enable/visibility rules | Quality, artifact, runtime, memory, or disk trade-off | Scope: preview/apply, channel, mask, still/sequence | Persistence |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Histogram plot | Graph, normalized intensity/count | Source or current Preview; RGB channels overlaid | None | Binning follows Plot resolution | Shows source or mask-aware Preview distribution and approximate endpoint-bin clipping | Show histogram controls visibility | Recalculation adds work; endpoint counts are bin-dependent | Display only | Recomputed after source, Preview, or resolution changes |
| Left-drag in histogram | Graph gesture | No readout | None | Hold left and move | Shows normalized coordinate readout | Histogram shown | Diagnostic only | Display only | Ends on release |
| Middle-drag in histogram | Graph gesture | Fitted/unpanned | None | Hold middle and drag | Pans the graph | Useful beyond viewport | No output effect | Display only | Until reset/window close |
| Ctrl+mouse wheel in histogram | Graph gesture | Fitted size | None | Up `1.20x`; down `0.8x` | Zooms around cursor | Interactive graph enabled | Rendering only | Display only | Window lifetime |
| Zoom in | Toolbar action | Fitted size | None | Each action `1.30x` | Enlarges plot | Disabled with hidden histogram | Rendering only | Display only | Window lifetime |
| Zoom out | Toolbar action | Fitted size | None | Each action `0.70x` | Shrinks plot | Disabled with hidden histogram | Rendering only | Display only | Window lifetime |
| Zoom reset | Toolbar action | — | None | One action | Fits plot and resets scrolling | Disabled with hidden histogram | Rendering only | Display only | Immediate |
| Show grid | Toggle | Checked | None | Checked/unchecked | Shows or hides graph grid | Disabled with hidden histogram | Display-only aid | Display only | Window lifetime |
| Show clipping info | Toggle | Checked | None | Checked/unchecked | Shows or hides endpoint-bin counts | Disabled with hidden histogram | Binning-dependent estimate, not an exact float clipping test | Display only | Window lifetime |
| Absolute scales | Toggle | Unchecked | None | Checked/unchecked | Normalizes each displayed histogram channel to its own peak | Disabled with hidden histogram | Can conceal relative channel strength; no image normalization | Display only | Window lifetime |
| Draw histogram areas | Toggle | Unchecked | None | Checked/unchecked | Filled-area versus line graph | Disabled with hidden histogram | Overlap can obscure channels | Display only | Window lifetime |
| Show histogram | Toggle | Checked | None | Checked/unchecked | Hides/clears graph and toolbar or reloads it | Available while idle | Saves graph work, not Preview processing | Display only | Window lifetime |
| Plot resolution | Combo box, bins | **8-bit (256)** | None | **6-bit (64)** through **16-bit (64K)**, plus **100**, **1000**, **10000 levels** | More bins refine graph and endpoint estimates | Disabled with hidden histogram | Higher bins cost graph memory/rendering; output precision unchanged | Display only | Window lifetime |
| Hue | Integer value plus slider, degrees | `0` | None | `-180..180`, step `1` | Adds `Hue/360` turns and wraps around the HSL circle | Idle; Apply disabled only when all three parameters are zero; locked during Apply | Large rotations remap every chromatic color; hue is powerless on neutral pixels | Preview/snapshot/Apply; all RGB pixels; mask post-blend; still/sequence | Captured per execution; Reset/success returns `0` |
| Lightness | Integer value plus slider, HSL percentage points | `0` | None | `-100..100`, step `1` | Adds `Lightness/100` to HSL L, then clamps `0..1` | Idle; locked during Apply | Positive values move pixels toward white; negative toward black; endpoint clipping loses detail | Preview/snapshot/Apply; all RGB pixels; mask post-blend; still/sequence | Captured per execution; Reset/success returns `0` |
| Saturation | Integer value plus slider, mapped strength | `0` | None | `-100..100`, step `1`; negative multiplier `1+S/100`, positive multiplier `1+S/25` | `-100` removes saturation; `+25` doubles it; `+100` requests `5x`; result clamps `0..1` | Idle; locked during Apply | Positive side is four times steeper than a percentage interpretation and quickly clips saturation | Preview/snapshot/Apply; all RGB pixels; mask post-blend; still/sequence | Captured per execution; Reset/success returns `0` |
| Target Image | RGB image selector | Attached/active RGB target | None | Open color stills/sequences | Changes source, histogram, and Preview | Lists color views only; the window closes if its target stops holding a color image; locked during Apply | Wrong target is destructively replaced | Preview/Apply source and active mask | Target lifecycle; Reset does not change it |
| Preview | Checkbox | Checked | None | Checked/unchecked | Creates/destroys asynchronous fixed-linear Preview and switches histogram source/result | Remains interactive during Apply | Each preview run works on a copy of the target or of the Preview area, plus a floating working copy for integer images; a burst of changes collapses into one run with the latest values; Apply cancels a preview in progress | Preview only; current displayed frame | Window lifetime; Reset does not change it |
| Preview pane | Full-image viewer | Latest Preview result | None | Standard viewer zoom/pan/channel controls | Shows mask-aware result | Exists only with Preview | Shows the adjustment Apply writes, in the target's representation and RGB format | Preview only; target unchanged | Window lifetime |
| Switch image | Preview-toolbar action | Processed Preview shown | None | Click, **Space**, right-click, or **Ctrl+Shift+O** | Toggles result/original display | Exists only with Preview | Display comparison only | Preview display only | Preview-window lifetime |
| Preview area | Preview-toolbar checkbox | Unchecked | None | Checked/unchecked; area selected in the Preview pane | Runs the Preview over the selected area only and shows that result over the rest of the image as it was | Exists only with Preview; disabled during Apply | Faster regional test; inside the area the result is what Apply makes of an image of that area | Preview and snapshot path | Window lifetime |
| Take snapshot | Preview-toolbar action | No snapshot | None | Click or **Ctrl+Shift+K** | Runs the current settings again and opens `<target> - Adjust Hue/Saturation/Lightness - SNAPSHOT`; with Preview area on, the snapshot is the composite the Preview showed | Exists only with Preview; disabled during Apply | Samples are what the Preview shows; the snapshot keeps the source's metadata and is not recorded as nonlinear | Auxiliary still; target unchanged; mask applies | Each action creates a view |
| Apply | Button/action | Disabled at all-zero identity | None | One action when any parameter is nonzero | Captures the three parameters and runs the adjustment on a worker over the still or every decoded sequence frame | Target lock required; during the run Target, the sliders, Apply, Reset, Close, and the Preview pane's snapshot and area actions are disabled whether or not Preview is on; the Preview checkbox stays available | RGB/HSL conversion in double precision per pixel and clamps; destructive result | Still/sequence replacement; active mask applies | Success records one history step and resets parameters |
| Reset | Button/action | Enabled while idle | None | One action | Returns Hue, Lightness, and Saturation to `0` | Does not change Target, Preview, histogram, or toolbar | No image change | Configuration only | Immediate |
| Close | Button/action | Enabled while idle | None | One action | Closes without applying current settings | Disabled during Apply; closing the window while a run is in progress hides it at once and it closes when the run has landed | Nothing waits on the GUI thread; a sequence Apply in progress is cancelled | Window/execution | Parameters are not saved |
| Sequence progress / Cancel | Shared progress dialog and Cancel | Created for sequence Apply | None | Per-frame progress; Cancel | Writes processed frames to a temporary sequence and replaces target only after every frame succeeds | Sequence target; every decoded frame must be RGB | Decode/encode and temporary disk cost; Cancel is observed between frames and inside the engine operation | Sequence Apply; same parameters/mask for every frame | Cancel discards temporary result; success resets settings |

## How the algorithm works

For normalized stored RGB values, AfterNight computes ordinary HSL. If
`v=max(R,G,B)`, `m=min(R,G,B)`, and `d=v-m`, then HSL lightness is
`l=(v+m)/2`; saturation is derived from `d` relative to `l`, and neutral pixels
have zero saturation and powerless hue.

For UI values `H`, `S`, and `L`, the current transform is

\[
h'=\operatorname{wrap}_{[0,1)}(h+H/360),
\]

\[
s'=\operatorname{clamp}(s\,k_S,0,1),\qquad
k_S=\begin{cases}
1+S/25,&S>0\\
1+S/100,&S<0\\
1,&S=0,
\end{cases}
\]

\[
l'=\operatorname{clamp}(l+L/100,0,1).
\]

The adjusted triplet is converted back to RGB and each channel is clamped to
`0..1`. The HSL is that of the stored samples as they are: the samples are read
and written in the same encoding, so no transfer function is decoded or
re-encoded on the way. Each pixel is computed in double precision.

Samples are read in normalized units: an unsigned integer divided by its largest
code, a signed integer counted from its smallest code to its largest, and a
floating sample as it is. Integer images are converted into a Float32 working
copy (Float64 for 32-bit integers) and the result is narrowed back to the
original representation, rounding to the nearest code with ties to even.
Floating images are worked on in place.

Pixels that the image's own validity marks as not valid are left as they were.
On an ordinary image, whose samples are all valid, a pixel holding NaN or an
infinity in any channel is also left as it was. Under any other validity rule,
a NaN or an infinity in a pixel that rule calls valid makes the engine refuse
the run: Apply publishes nothing and reports the error.

Preview runs this same adjustment over the target, or over the Preview area,
on a worker. Preview, Preview area, and snapshot hold what Apply makes, in the
target's representation and RGB format.

An active mask blends original and transformed samples after the adjustment.
The engine operation runs on the run's threads and observes a stop request; a
stopped run publishes nothing.

## Parameter interactions and starting points

| Combination | Conservative starting point | Too weak looks like | Too strong looks like | Adjustment order |
| --- | --- | --- | --- | --- |
| Hue + calibrated color | `-5..5` degrees | Color cast remains | Star/nebula colors move into unrelated hues | Rotate first, inspect known neutrals and stars |
| Saturation + stretched RGB | `-10..10`; remember `+10` means `1.4x` | Color separation barely changes | Saturation clips, noise and chromatic halos intensify | Raise slowly, inspect channel endpoints |
| Lightness + stretch | `-5..5` | Tonal change is negligible | Shadows/whites clip globally | Prefer the main stretch for tone; use HSL L only as finishing |
| Active mask + color change | Verify mask before tuning | Intended region changes too little | Boundary becomes visible | Inspect mask, reduce adjustment, feather upstream |
| Preview area + regional test | Choose an area holding neutrals, stars, and saturated detail | The area misses the colors that matter | One area is taken to stand for the whole image | Tune on the area, then check the whole-image Preview before Apply |

## Inputs, outputs, and metadata

- Apply retains dimensions, RGB channel count, representation, and image name.
  It keeps every FITS card it does not own, the astrometric solution, and the
  acquisition record, and records the result as nonlinear on the
  processing-state cards: `ANLIN` nonlinear, `ANASRC` process, `ANOP`
  `hsl-adjustment`, and `ANBASIS` `display-rgb` (a `monochrome` basis the
  source's cards recorded is kept). A linear encoding the source claimed is no
  longer claimed, and the image's lineage gains an entry naming
  `an::adjustImageHsl` with the three values.
- Image History records one step named **Adjust Hue/Saturation/Lightness**,
  described as `Adjust hue to 30 saturation to 40 and lightness to -10` for
  those values; undo restores the previous image. The step does not record the
  active mask or whether a sequence auto-debayer supplied RGB.
- Preview snapshots are separate stills holding what the Preview showed. They
  keep the source's metadata and are not recorded as nonlinear, so use Apply on
  a duplicate for an archival result.
- Sequence Apply processes every decoded frame as Apply processes a still image
  of it, into a temporary sequence, and replaces the target only after every
  frame succeeds. Automatic CFA debayer can alter the decoded frame format
  before this process.
- Nothing is stored between openings of the window.

## Performance

Conversion and adjustment are linear in pixel count and run on the threads the
run is granted. Preview and Apply each work on a copy of the target (a Preview
area works on a copy of the area); an integer image adds a floating working
copy, and Apply copies the result once more when it restates the
processing-state cards. A mask adds another full-image blend; sequence Apply
adds decode, encode, and temporary-disk work. A run the session's memory budget
cannot hold is refused with the reason and publishes nothing. No production
timing or peak memory was measured.

## Worked example

- **Evidence mode:** Source/equation-derived; not executed on project-owned
  astronomical data.
- **Input:** normalized red `(1,0,0)`, whose HSL is `(0°,1,0.5)`.
- **Hue `+120`:** wraps to `120°` and reconstructs green `(0,1,0)`.
- **Saturation `-50`:** uses multiplier `0.5`, producing HSL saturation `0.5`.
- **Lightness `+25`:** adds `0.25`, producing HSL lightness `0.75`; with the
  original red hue/saturation this reconstructs approximately `(1,0.5,0.5)`.
- **Inspect in real use:** neutral objects, star colors, saturation clipping,
  histogram endpoints, mask boundaries, and final RGB metadata/state.
- **Limitations:** These checkpoints verify the current mappings, not
  astronomical color quality, a saved sequence, runtime, or memory use.

## Warnings and limitations

- Positive Saturation is not a percentage: `+25` means `2x` and `+100` means
  `5x` before clamping. Negative values run linearly to zero at `-100`.
- HSL Lightness is not CIE L*, scene luminance, exposure, or a full stretch.
- HSL is not perceptually uniform; equal hue steps and equal lightness values do
  not imply equal visual differences.
- Input values are treated as current normalized stored RGB. No embedded ICC
  profile or transfer curve is consulted.
- Every processed pixel is clamped to `0..1`; floating values outside that
  range do not survive Apply.
- The process needs exactly red, green, and blue. A grayscale image or frame,
  or a color image that also carries an alpha channel, is refused at Apply with
  the reason, and nothing changes.

## Troubleshooting

| Symptom or exact error | Verified cause/validation path | Recovery | Evidence source |
| --- | --- | --- | --- |
| Menu action is disabled or process closes | Target is not a color image | Convert/debayer intentionally, or use an RGB target | `ActionStateCoordinator::setEnableImageColorActions`; `AdjustHSLProcess::isViewControlSupported` |
| Apply is disabled | Hue, Saturation, and Lightness are all `0` | Set any nonzero value | `AdjustHSLProcess::settingsUpdatedImpl` |
| Small positive Saturation looks unexpectedly strong | Positive mapping is `1+S/25` | Use smaller positive values; `+5` is already `1.2x` | `AdjustHSLProcess::hslAdjustment` |
| Hue rotation does nothing in gray areas | Zero-saturation HSL hue is powerless | This is expected; change saturation only if color is intended | `an::adjustImageHsl` (RGB-to-HSL conversion) |
| Error message includes `the HSL adjustment needs an image of red, green and blue` | The target, or a decoded sequence frame, is not three-channel RGB (grayscale, or color with alpha) | Supply RGB images or frames, or configure valid CFA auto-debayering; the target was not changed | `AdjustHSLProcess::executeTypedProcessImpl` |
| Output clips toward white or black | Additive Lightness clamps to `0..1`; saturation and the RGB result also clamp | Reduce Lightness/Saturation and start again from duplicate | `an::adjustImageHsl` |
| Pixels holding NaN are unchanged | Non-finite pixels are left as they were | Repair or replace them upstream if needed | `process_utils::skippingNonFinite` |
| Snapshot carries no nonlinear record | Snapshots are Preview results and are not recorded | Apply on a duplicate for an archival result | `AdjustHSLProcess::executeTypedProcessImpl`; `process_utils::recordNonlinearResult` |
| Error names the memory budget | The session cannot hold the run's copies of the image | Close other images or work on a smaller crop; nothing was changed | `process_utils::openNormalizedDestination`; `process_utils::recordNonlinearResult` |
| Sequence Cancel leaves the sequence as it was | Cancel stops the run between frames or inside the engine operation, and the target is replaced only when every frame succeeds | Expected; Apply again when ready | `ImageRTPreviewControl::runOverSequence`; `an::adjustImageHsl` |

## Related workflow steps

Complete [Photometric Color Calibration](../color-calibration/photometric-cc.md)
or [Spectrophotometric Color Calibration](../color-calibration/spectrophotometric-cc.md)
before display-color finishing. Compare [Adjust LCh](adjust-lch.md) when CIE L*,
chroma, and hue are the desired coordinates.
Use [Color Saturation](color-saturation.md) for hue-range-selective saturation,
and use [Histogram Transformation](../stretching/histogram-transformation.md),
[Generalized Hyperbolic Transformation](../stretching/gh-transformation.md), or
[Exposure Correction](../stretching/exposure-correction.md) for deliberate tone
work rather than treating HSL Lightness as a substitute. Verify
[Image History](../../interface/image-history.md) and
[Image Statistics](../../interface/image-statistics.md) after Apply.

## References and provenance

- [W3C CSS Color 4: HSL colors and conversion](https://www.w3.org/TR/css-color-4/#the-hsl-notation) describes the ordinary sRGB-oriented HSL model, circular hue, and its perceptual limitations. AfterNight's exact parameter mappings and Preview behavior come from the project source.
- AfterNight project source is authoritative for the asymmetric Saturation mapping, additive Lightness, representation/metadata/state behavior, masks, Preview and Preview-area behavior, and temporary sequence workflow.
