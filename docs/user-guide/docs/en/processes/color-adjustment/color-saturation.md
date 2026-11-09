---
kind: process
stage: finish
menu: Process > Intensity Transformations > Color Saturation
summary: >-
  Changes the saturation of an image, everywhere or within one hue band, optionally leaving dark pixels alone.
data_in: nonlinear
data_out: nonlinear
---
# Color Saturation

## Purpose and expected result

Color Saturation multiplies HSL saturation globally or inside one fixed hue
interval, while optionally leaving dark pixels unchanged. Use it for selective
display-color finishing after calibration and the main stretch. The successful
result replaces the target; Preview snapshots create separate stills.

This is a nonlinear operation on current normalized stored RGB values. It does
not use an ICC working space or perceptually uniform color model. Work on a
duplicate and judge the committed result with a consistent viewer stretch.

## At a glance

| Item | Current behavior |
| --- | --- |
| Menu path | **Process > Intensity Transformations > Color Saturation** |
| Enabled when | A color image (RGB, with or without alpha) or a sequence displaying one is active, the same process is not already attached, and no attached process holds exclusive access; grayscale targets are excluded |
| Inputs | One RGB still or sequence whose decoded frames are RGB; intended finite normalized samples (a pixel holding NaN or an infinity is left as it was, whatever the image's validity rule); optional enabled active mask |
| Outputs | Replaces the target at the same geometry, pixel type, and RGB format; a successful sequence uses a temporary replacement sequence; Take snapshot creates an auxiliary still |
| Recommended image state | Nonlinear display data after color calibration and the main stretch; every committed result is recorded nonlinear, including an identity-looking Apply |
| Preview support | Asynchronous same-type RGB Preview running the Apply's own operation, fixed linear display, histogram, original/result switch, selectable Preview area, and snapshot |
| Sequence support | Generic temporary replacement-sequence path with progress and Cancel; each frame is processed as an Apply processes a still image, so the background threshold is recomputed per frame; the target is replaced only when every frame succeeds |
| Mask support | Active target mask post-blends source and transformed result for Preview, still Apply, and sequence Apply; white applies, black protects, gray blends, and Invert Mask reverses weights |
| External dependencies | None; bundled AfterNight engine only |

## Before you start

Finish background and color calibration, then make the intended main stretch.
Color Saturation uses ordinary HSL from stored RGB, so it is a display-oriented
finishing tool rather than a replacement for calibration or a linear chroma
workflow.

Choose **Background factor** deliberately. It does not measure or protect
low-saturation pixels. It derives one lightness cutoff from the source green
channel and completely bypasses pixels at or below that cutoff. A high setting
can protect faint background but can also exclude faint colored structures, and
on a bright image it can push the cutoff to `1`, where no pixel is adjusted.

## Procedure

1. Select a nonlinear RGB duplicate and open **Process > Intensity
   Transformations > Color Saturation**.
2. Start with **Hue: Global**, **Amount: 0.00**, and **Background factor: 1.00**.
   Keep the same viewer display mode while comparing Preview and original.
3. Choose a hue interval, then move **Amount** in small steps. Inspect stars,
   nebular transitions, neutrals, and histogram endpoints.
4. Adjust **Background factor** only if dark-background protection is needed.
   Recheck faint colored structures because the cutoff is hard, not feathered.
5. If using **Preview area**, it shows what Apply writes inside it: its
   cutoff is the whole image's, not one measured on the area.
6. Select **Apply**, then inspect the committed result, active-mask boundary,
   metadata/state, and Image History.

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
| Hue | Selector | **Global** | None | **Global** `0..360°`; **Pink-Red / Red-Orange** `346..360°` or `0..20°`; **Orange-Brown / Yellow** `21..60°`; **Yellow-Green / Green-Cyan** `61..200°`; **Cyan** `170..200°`; **Cyan-Blue / Blue-Magenta** `201..280°`; **Magenta / Pink** `281..345°` | Selects a hard inclusive interval; no feathering. Cyan duplicates a subset of Yellow-Green / Green-Cyan, and fractional hues in each one-degree boundary gap are excluded | RGB target; disabled during Apply | Hard boundaries can make color transitions uneven; named ranges are implementation labels, not calibrated spectral bands | Preview/Apply; all RGB channels together; mask post-blend; still/sequence | Captured per execution; Reset/success returns Global |
| Amount | Double value plus slider, multiplier offset | `0.00` | None | `-1.00..1.00`, step `0.01` | Multiplies selected saturation by `1 + Amount`; `-1` removes saturation, `+1` doubles it before clamp | RGB target; disabled during Apply | Positive values can clip saturation and amplify chromatic noise/halos; negative values mute selected colors | Preview/Apply; eligible hue/lightness pixels; mask post-blend; still/sequence | Captured per execution; Reset/success returns `0.00` |
| Background factor | Double value plus slider, threshold scale | `1.00` | None | `0.00..2.00`, step `0.01`; the cutoff is held to `0..1`, so a cutoff that would pass `1` adjusts no pixel | At `0`, protects only zero-lightness black. Above `0`, cutoff is `factor × (green median + green sigma)`, from the exact median and the population standard deviation of the normalized green channel, a green sample that holds no number left out of both under every validity rule, and, under a validity rule other than all-valid or finite-only or with a validity mask, every pixel whose samples the rule does not all admit; pixels with HSL lightness at or below it remain unchanged | RGB target; disabled during Apply | Higher values protect more dark pixels but can abruptly exclude faint colored signal; positive values add green-channel statistics work | Preview/Apply; threshold precedes hue test and mask; measured once on the whole target for every preview - a Preview area and a drag's preview included - and per frame for a sequence | Captured per execution; Reset/success returns `1.00` |
| Target Image | RGB image selector | Attached/active RGB target | None | Open eligible RGB still/sequence | Changes source, statistics, histogram, and Preview | Color images only (one with alpha is listed but refused at Apply); target closes process if it becomes unsupported; disabled during Apply | Wrong target is destructively replaced | Preview/Apply source and active mask | Target lifecycle; Reset does not change it |
| Preview | Checkbox | Checked | None | Checked/unchecked | Creates/destroys asynchronous fixed-linear Preview and switches histogram source/result | Remains interactive during Apply | Preview processing and green statistics add work | Preview only; current displayed frame | Window lifetime; Reset does not change it |
| Preview pane | Full-image viewer | Current processed result | None | Pan with scroll bars or dragging; toolbar actions below | Shows mask-aware result in same-type RGB | Exists only with Preview | Holds source/result/display buffers; fixed-linear display may differ from an autostretched target display | Preview only; target unchanged | Window lifetime |
| Fit To Window | Preview-toolbar action | Preview fitted when first displayed | None | Click or **Ctrl+Shift+F** | Fits the whole image uniformly in the viewport | Preview image required | Resampling can hide color boundaries and noise | Preview display only | Viewer lifetime |
| Zoom In | Preview-toolbar action | Current scale | None | Click; `1.25x` relative zoom within viewer limits | Enlarges around viewport center | Preview image required | Rendering only | Preview display only | Viewer lifetime |
| Zoom Out | Preview-toolbar action | Current scale | None | Click; `0.8x` relative zoom within viewer limits | Reduces around viewport center | Preview image required | Shows more context but hides fine color artifacts | Preview display only | Viewer lifetime |
| Zoom 100% | Preview-toolbar action | Current scale | None | Click; absolute scale `1.0` | Shows the viewer's one-pixel scale | Preview image required | Useful for inspecting noise, masks, and hard hue-boundary artifacts | Preview display only | Viewer lifetime |
| View Channel | Preview-toolbar combo box | **RGB** | None | Stored and derived RGB/CIE/HSL/HSV/HSI/YCbCr display components | Selects only the displayed component | RGB Preview required | Helps diagnose saturation and clipping without changing pixels | Preview display only | Viewer lifetime; not in process history |
| Display Mode | Not offered | — | None | — | Color Saturation's Preview has no Display Mode control; it always shows its result in the viewer's linear display, which rounds each sample to the nearest display byte | No setting to enable | Faint results can look dark; judge strength in the target view after Apply | Preview display only | — |
| Autostretch Settings | Not offered | — | None | — | Color Saturation's Preview toolbar has no Autostretch Settings action | No setting to enable | Use the histogram and the target view's own display stretch | Preview display only | — |
| Switch image | Preview-toolbar action | Processed Preview shown | None | Click, **Space**, right-click, or **Ctrl+Shift+O** | Toggles result/original display | Exists only with Preview | Display comparison only | Preview display only | Preview-window lifetime |
| Preview area | Preview-toolbar checkbox | Unchecked | None | Movable bounded rectangle | Processes the area and composites it over a full-image copy | Exists only with Preview; disabled during Apply | Cutoff uses the whole image's green median/sigma, so the area shows what Apply writes inside it | Preview and snapshot path | Window lifetime |
| Take snapshot | Preview-toolbar action | No snapshot | None | Click or **Ctrl+Shift+K** | Re-executes current Preview settings and opens `<target> - Color Saturation - SNAPSHOT` | Exists only with Preview; disabled during Apply | Full-frame snapshot is same-type RGB; Preview-area snapshot takes the whole image's cutoff; a snapshot is not recorded nonlinear | Auxiliary still; target unchanged; mask applies | Each action creates a view |
| Apply | Button/action | Enabled, including identity settings | None | One action | Captures Hue, Amount, and Background factor and transforms the still or every decoded sequence frame | Target lock required; during every Apply, with or without Preview, Target, Hue, Amount, Background factor, Apply, Reset, Close, Preview area, and Take snapshot are disabled, and the Preview checkbox stays live | Destructive HSL conversion; integer images are narrowed to the nearest code; positive factor computes per-source statistics; a still Apply has no progress dialog or Cancel | Still/sequence replacement; active mask applies | Success records history and resets settings |
| Reset | Button/action | Enabled while idle | None | One action | Returns Hue to Global, Amount to `0.00`, and Background factor to `1.00` | Does not change Target, Preview, histogram, or toolbar; disabled during Apply | No image change | Configuration only | Immediate |
| Close | Button/action | Enabled while idle | None | One action | Closes without applying current settings | Disabled during Apply; closing the window another way while a run is in flight hides it, and it closes once the run has landed | Nothing waits on the GUI thread | Window/execution | Parameters are not saved |
| Sequence progress / Cancel | Shared progress dialog and Cancel | Created for sequence Apply | None | Per-frame progress; Cancel | Writes processed frames to a temporary sequence and replaces the target only after every frame succeeds | Sequence target; every decoded frame must be RGB, or the run fails and nothing is replaced | Statistics and transform repeat per frame; decode/encode and temporary disk add cost; the engine operation on the current frame observes Cancel | Sequence Apply; same Hue/Amount/factor and mask, frame-local cutoff | Cancel replaces nothing; success resets settings |

## How the algorithm works

### Conceptual pipeline

1. Start the destination as a copy of the source: same geometry, RGB format,
   pixel type, and metadata.
2. Read the samples in normalized units: unsigned integers divided by their
   largest value, signed integers counted from their smallest value to their
   largest, floating samples as stored. Integer samples are converted into a
   Float32 working image (Float64 for 32-bit integers); Float32 and Float64
   samples are worked on where they are.
3. If **Background factor** is positive, compute the normalized green-channel
   exact median and population standard deviation and form one cutoff;
   otherwise use zero.
4. Convert each RGB pixel to HSL. Bypass pixels whose HSL lightness is not above
   the cutoff and, whatever the image's validity rule, pixels holding NaN or an
   infinity. For the rest, multiply saturation only when hue is inside the
   selected hard interval.
5. Clamp a multiplied saturation to `0..1`, convert HSL back to RGB, clamp RGB
   to `0..1`, and narrow integer results to the nearest code of the original
   type. Record the result nonlinear; the framework then applies the active
   mask.

### Model and equations

Let `R`, `G`, and `B` be normalized stored samples, with
`v=max(R,G,B)`, `m=min(R,G,B)`, and ordinary HSL lightness

\[
l=(v+m)/2.
\]

For Background factor `f`, the cutoff is

\[
b=\begin{cases}
0,&f=0,\\
f\,[\operatorname{median}(G)+\sigma(G)],&f>0,
\end{cases}
\]

where `median(G)` is the exact median and `σ(G)` the population standard
deviation of the normalized green samples.

For Amount `a`, source saturation `s`, and the selected hue set `H`, the output
saturation is

\[
s'=\begin{cases}
\operatorname{clamp}(s(1+a),0,1),&l>b\ \text{and}\ h\in H,\\
s,&\text{otherwise}.
\end{cases}
\]

Hue is represented internally in six sextants; UI degree limits are divided by
`60`. A range whose lower bound exceeds its upper bound wraps around red. All
tests are inclusive and binary: there is no distance weighting or feathering.

Arithmetic is double per pixel. Results are held in the working image and
narrowed to the original pixel type at the nearest code, ties to even. The
operation runs on the engine's worker threads
and observes a stop request. The output's processing-state cards record it
nonlinear with operation `saturation-enhancement`.

### Boundary and failure behavior

The menu action and the **Target Image** selector offer only color images, with
or without an alpha channel. If an Apply reaches an image that is not exactly
red, green, and blue - a color image with alpha, or a sequence frame that is not
RGB - it fails with
`the colour saturation needs an image of red, green and blue` and publishes
nothing. On an image whose samples are all valid, a pixel holding NaN or an
infinity in any channel is left as it was; on a floating image whose validity
is exact-zero-is-no-data or an explicit mask, such a pixel in a valid position
stops the Apply with an error and
nothing changes. Floating samples outside `0..1` are read as they are; every
pixel above the cutoff is rebuilt from its HSL - its saturation clamped to
`0..1` when its hue is selected - and its RGB clamped to `0..1`, and integer
output is narrowed at its original bit depth.

The background cutoff is held to `0..1`. If it reaches or exceeds every pixel's
lightness - as it does when a high Background factor on a bright image would
take it past `1` - no pixel is adjusted and the output is the source; this
applies to Preview as well as Apply. A Preview area, and a drag's preview, take
the whole target's background, as Apply does. A stop request is observed by the
engine operation, and a stopped Apply publishes nothing.

## Parameter interactions and starting points

| Combination | Conservative starting point | Too weak looks like | Too strong looks like | Adjustment order |
| --- | --- | --- | --- | --- |
| Hue + Amount | Choose the narrowest relevant interval; try `±0.05` | Intended colors barely change | Boundary transitions, clipped chroma, colored noise, or halos become conspicuous | Choose Hue, then move Amount slowly |
| Background factor + faint color | Start at `1.00`, compare with `0.00` | Background noise gains color | Faint nebula/star color is completely bypassed, and nothing is adjusted once the cutoff reaches `1` | Tune Amount first, then raise protection only as needed |
| Overlapping/gapped ranges | Compare adjacent options around cyan and boundaries | Target hue falls in neither neighboring interval | Cyan receives an unexpectedly broad selection | Inspect with Global, then compare exact named ranges |
| Active mask + hard hue/lightness gates | Verify mask before tuning | Intended region changes too little | Mask and hue/cutoff boundaries combine visibly | Inspect mask, then Hue, Amount, and factor |
| Preview area + Apply | Use it to judge part of the image at full speed; it shows what Apply writes there | The area shows little because its pixels are below the whole image's cutoff | Pixels outside the area are not shown processed | Switch Preview area off to see the whole result |

## Inputs, outputs, and metadata

- Final still Apply retains dimensions, RGB channel count, pixel type, image
  name, and the source's FITS cards, astrometric solution, and acquisition
  record, and marks processing state nonlinear with `saturation-enhancement`:
  its processing-state cards are restated as `ANLIN` nonlinear, `ANASRC`
  process, `ANOP` `saturation-enhancement`, and the stretched channel basis
  (`display-rgb`, unless the source's cards recorded `monochrome`). A linear
  encoding the source claimed is no longer claimed, and the result's lineage
  gains an `an::enhanceImageSaturation` entry naming the amount, the background
  as held to `0..1`, and the hue range.
- Image History records **Color Saturation** with the description
  `Double coeff <amount>, background factor <factor>, hue min <degrees>, hue max <degrees>`,
  for example `Double coeff 0.5, background factor 1, hue min 0, hue max 360`.
  It does not record the selected label, active mask, or computed
  median/sigma/cutoff. Undo restores the previous image.
- Preview snapshots are separate same-type RGB stills and are not recorded
  nonlinear. With Preview area on, the processed area is pasted over a
  full-image copy, with the whole image's threshold.
- Sequence Apply processes every frame as a still Apply would, recomputing
  statistics and the cutoff per frame, writes a temporary sequence, and replaces
  the target only after every frame succeeds. Automatic CFA debayer can supply
  RGB frames before this process.
- Nothing is stored between openings of the window: it always opens at
  **Global**, `0.00`, and `1.00`.

## Performance

The RGB/HSL transform is linear in pixel count and runs on the engine's worker
threads. Every positive Background factor also computes the green-channel exact
median and sigma; median calculation and its temporary/statistics storage add
work beyond the pixel transform. Integer images are converted into a floating
working image and back, and an Apply copies the result once more to restate its
cards. A mask adds a full-image blend. Sequence Apply repeats those costs per
frame and adds decode, encode, and temporary-disk work.

Use Preview area for responsiveness; its cutoff is the whole image's.
Disabling Preview saves the Preview work once settings are final. No production
timing or peak memory was measured.

## Worked example

- **Evidence mode:** Source/equation-derived; not executed on project-owned
  astronomical data.
- **Input checkpoint:** a normalized red pixel has HSL `(0°,1,0.5)` and falls
  inside both Global and Pink-Red / Red-Orange.
- **Amount `-0.50`, cutoff below `0.5`:** saturation becomes `0.5`; the same
  hue/lightness reconstructs approximately `(0.75,0.25,0.25)`.
- **Amount `+1.00`:** requests `2×` saturation, then clamps this already fully
  saturated red back to `1`, so it remains red.
- **Cutoff `0.5≤b≤1`:** the strict `l>b` test fails and the source pixel is
  copied unchanged regardless of Hue or Amount.
- **Inspect in real use:** hue boundaries, faint colored structures, background
  noise, saturation clipping, mask boundaries, and the final full-frame result.
- **Limitations:** These checkpoints verify the current mapping and hard gates,
  not astronomical color quality, a saved sequence, runtime, or memory use.

## Warnings and limitations

!!! warning "Background factor is a hard lightness gate"
    It does not softly weight saturation or detect low-saturation background.
    Pixels at or below the green-statistics-derived HSL-lightness cutoff are
    completely unchanged, and a cutoff that reaches `1` leaves every pixel so.

!!! note "Preview area uses the whole image's statistics"
    A Preview area takes its green median and sigma from the whole image, so
    inside the area it shows what Apply writes. Switch it off to inspect the
    full Preview before Apply.

- Hue intervals are hard, inclusive implementation bands. There are fractional
  one-degree gaps between most neighboring bands, and Cyan `170..200°` is a
  subset of Yellow-Green / Green-Cyan `61..200°`.
- HSL is not perceptually uniform, and stored RGB is used without ICC-profile or
  transfer-curve interpretation.
- Amount `+1` is a `2×` multiplier before clamp, not a guaranteed doubling of
  perceived colorfulness.
- Apply remains available at Amount `0`. It can produce an identity-looking
  image while still adding history and nonlinear processing state.
- A still Apply has no progress dialog or Cancel; a sequence Apply can be
  cancelled and then replaces nothing.

## Troubleshooting

| Symptom or exact error | Verified cause/validation path | Recovery | Evidence source |
| --- | --- | --- | --- |
| Menu action is disabled or process closes | Target is not RGB | Convert/debayer intentionally, or use an RGB target | `ActionStateCoordinator::setEnableImageColorActions`; `ColorSaturationProcess::isViewControlSupported` |
| **Error executing the process** reporting `the colour saturation needs an image of red, green and blue` | The image an Apply reached is not exactly RGB (grayscale, or color with alpha) | Use an RGB target; for a sequence, make every frame RGB | `ColorSaturationProcess::executeTypedProcessImpl` |
| Result is unchanged | Amount is `0`, selected hue excludes the pixels, or cutoff is at/above their lightness | Check Global with factor `0`, then restore selective settings one at a time | `an::enhanceImageSaturation` |
| Faint target color is not adjusted | Background cutoff is too high | Lower Background factor and protect background with a suitable mask instead | `an::deriveSaturationEnhancementBackground` |
| Cyan selection seems broader or duplicates another choice | Cyan `170..200°` lies inside Yellow-Green / Green-Cyan `61..200°` | Choose the narrower Cyan option intentionally; compare with Global | `ColorSaturationProcess::hueRange` |
| A hue near a named boundary is unchanged | Fractional hue lies in a one-degree gap between hard intervals | Use Global or the wider overlapping range; verify at modest Amount | `an::enhanceImageSaturation` inclusive hue tests |
| Preview area and full Apply differ | Outside the area the Preview shows the image as it was; inside it, the area takes the whole image's cutoff and shows what Apply writes there | Turn Preview area off and compare full Preview before Apply | `ColorSaturationProcess::executeTypedProcessImpl` (the whole target's background for a run over a Preview area) |
| Saturated colors flatten or halos/noise intensify | `s(1+Amount)` reached the `1` clamp | Reduce Amount or use a better mask | `an::enhanceImageSaturation` saturation clamp |
| A cancelled sequence left the target unchanged | Cancel stops the run; nothing is published | Apply again when ready | `ImageRTPreviewControl::finishRun`; sequence run |

## Related workflow steps

Complete [Photometric Color Calibration](../color-calibration/photometric-cc.md)
or [Spectrophotometric Color Calibration](../color-calibration/spectrophotometric-cc.md)
before display-color finishing. Use [Adjust HSL](adjust-hsl.md) for a global
circular hue/saturation/lightness transform, [Adjust LCh](adjust-lch.md) for CIE
L*/chroma coordinates with its documented gamut/hue limits, and
[Curves Transformation](../stretching/curves-transformation.md) when a curve is
the intended tonal or channel-local control. Verify [Image History](../../interface/image-history.md)
and [Image Statistics](../../interface/image-statistics.md) after Apply.

## References and provenance

- [W3C CSS Color 4: HSL colors and conversion](https://www.w3.org/TR/css-color-4/#the-hsl-notation) describes the ordinary sRGB-oriented HSL model and its perceptual limitations. AfterNight's exact hard hue bands, cutoff, multiplier, and execution behavior come from the project source.
- AfterNight project source is authoritative for the green-channel exact-median-plus-sigma cutoff, the whole-image cutoff a Preview area takes, metadata/state/history, masks, and temporary sequence workflow.
- The dialog's histogram plot is drawn by
  `src/AfterNightControls/widgets/HistogramWidget.cpp` from counts
  `src/AfterNightControls/widgets/HistogramComputation.cpp` makes through the
  engine's
  `src/AfterNightEngine/include/AfterNightEngine/Algorithms/Statistics/Histogram.h`,
  which places a sample by exact interval arithmetic, so a whole code on a bin
  boundary lands in its own bin.
