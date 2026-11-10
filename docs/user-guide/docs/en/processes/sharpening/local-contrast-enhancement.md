---
kind: process
stage: finish
menu: Process > Details > Local Contrast Enhancement
summary: >-
  Raises contrast at a large local scale by scaling the bright and dark detail around a Gaussian base, with G'MIC.
data_in: nonlinear
data_out: nonlinear
derived: true
---
# Local Contrast Enhancement

## Purpose and expected result

Local Contrast Enhancement applies a high-radius unsharp-mask-style transform.
It separates each selected channel into a Gaussian base and a local residual,
scales bright and dark residuals independently, and then blends the filtered
result back with the source. It is a local transition enhancer, not
deconvolution and not histogram equalization.

The expected result is a same-size still or replacement sequence with stronger
mid- to broad-scale texture. Large **Weight** or tone-level factors can amplify
noise and produce bright/dark halos around stars. **Amount** is a separate final
crossfade: `0` preserves the source and `1` commits the complete G'MIC result.

## At a glance

| Item | Current behavior |
| --- | --- |
| Menu path | **Process > Details > Local Contrast Enhancement** |
| Enabled when | An image or sequence is active, the same process is not already attached to it, and no attached process holds exclusive access |
| Inputs | One grayscale or RGB still or sequence; optional active mask with matching geometry; a CFA still is treated as one grayscale mosaic plane, while a CFA sequence is auto-debayered to RGB first when its view has sequence auto-debayer enabled |
| Outputs | Destructively replaces the still or, after successful temporary processing, the sequence; **Take snapshot** creates an auxiliary still; no file is written directly |
| Recommended image state | Usually a finite, normalized nonlinear working copy after noise reduction and global stretch; use on linear data only with deliberate low settings and matched display conditions |
| Preview support | Asynchronous movable-area preview, original/result switching, snapshot, display-channel selection, and a separate source/result histogram; no execution autostretch |
| Sequence support | Yes; numeric parameters and process Channel are captured once when Apply starts, CFA frames can be auto-debayered first, and temporary output replaces the sequence only after success |
| Mask support | Yes; the active mask blends the Amount-mixed result with the original after filtering |
| External dependencies | Bundled G'MIC 3.5.4/cgmic, run through AfterNight's optional G'MIC adapter, with Arto Huotari's `fx_LCE` definitions built into the process; no separate installation, script file, or network access. In a build without G'MIC, Preview and Apply are disabled and their tooltip says why |
| Non-finite samples | An image holding a NaN or infinite sample is refused before G'MIC sees it, and left unchanged |

## Before you start

- Complete calibration, registration, integration, gradient correction, color
  calibration, stretching, and appropriate noise reduction first. This process
  boosts noise and residual defects along with structure.
- Debayer CFA stills before color enhancement. For CFA sequences, confirm the
  view's sequence auto-debayer setting: enabled frames enter this process as RGB;
  disabled frames remain a single mosaic plane that can reinforce the sensor
  pattern.
- Duplicate the target or confirm Image History is available. Apply replaces
  the target only after the full operation succeeds.
- Use finite, normalized samples. G'MIC works in float32 and its result is
  clamped to `[0,1]`. An image holding a NaN or infinite sample is refused with
  "the image holds a sample that is not a finite number, which G'MIC cannot
  process" and left unchanged.
- This process needs G'MIC, which AfterNight normally includes. In a build
  without it, **Preview** and **Apply** are disabled and their tooltip reads
  "This build of AfterNight carries no G'MIC library, so this process cannot
  run."
- Decide whether independent RGB or a luminance/lightness component matches the
  goal. Independent RGB can create colored rims.
- Include smooth background, small stars, a bright edge, and faint structure in
  the Preview area, then recheck full-frame context before Apply.

## Procedure

1. Select the target and open **Process > Details > Local Contrast
   Enhancement**. Confirm **Target Image** and choose **RGB** or one deliberate
   luminance/lightness **Channel**.
2. Keep **Preview** selected and start at Spatial Radius `80`, Weight `0.5`,
   Darkness Level `1`, Lightness Level `1`, and Amount `1`.
3. Set Spatial Radius for the widest structure to enhance. Inspect at **Zoom
   100%**; use the histogram only as a clipping diagnostic.
4. Adjust Weight for residual strength. Then balance Darkness Level and
   Lightness Level only if negative and positive residuals need different gain.
5. Reduce Amount to crossfade the entire filtered result toward the source. Use
   **Switch image** to compare without changing samples.
6. Check noise, colored edges, black/white clipping, and star halos with Preview
   area off. Apply only after confirming target, Channel, mask, and inversion.

## Complete parameter and action reference

| UI label/action | Type and unit | Runtime default | Persisted-default exception | Valid values, step, or constraints | Effect of increasing, decreasing, or selecting | Dependencies and enable/visibility rules | Quality, artifact, runtime, memory, or disk trade-off | Scope: preview/apply, channel, mask, still/sequence | Persistence |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Spatial Radius | Double value plus linear slider, Gaussian standard deviation in pixels | `80.00` | None | `30.00..200.00`; spin uses adaptive decimal stepping; slider maps `300..2000` to tenths | Widens the Gaussian base and therefore the scale of transitions represented by the residual | Enabled while idle; disabled during Apply, and the running job keeps its captured value | Larger values create broader halos and require more blur work/support | Preview/Apply; selected process Channel; before Amount and mask; still/sequence | Captured per execution; Reset and successful Apply restore `80` |
| Weight | Double value plus linear slider, residual multiplier | `0.50` | None | `0.00..5.00`; spin default step `0.01`; slider maps `0..500` to hundredths | Multiplies the high-radius residual inside bundled `fx_LCE`; `0` suppresses its detail term | Enabled while idle; captured for execution | High values amplify texture/noise and can clip or halo; `0` should preserve the script input apart from float/type round-trip | Preview/Apply; selected process Channel; before tone gains, Amount, and mask | Captured per execution; Reset/success restore `0.5` |
| Darkness Level | Double value plus linear slider, negative-residual gain | `1.00` | None | `0.00..4.00`; spin step `0.05` with adaptive stepping; slider maps `0..400` to hundredths | Scales the negative residual branch; `0` suppresses dark-side local contrast | Has no visible effect when Weight is `0`; captured for execution | High values deepen dark edge shoulders and can clip shadows; it does not change blur cost | Preview/Apply; selected Channel; before final Amount/mask | Captured per execution; Reset/success restore `1` |
| Lightness Level | Double value plus linear slider, positive-residual gain | `1.00` | None | `0.00..4.00`; spin step `0.05` with adaptive stepping; slider maps `0..400` to hundredths | Scales the positive residual branch; `0` suppresses bright-side local contrast | Has no visible effect when Weight is `0`; captured for execution | High values brighten edge shoulders and can clip highlights; it does not change blur cost | Preview/Apply; selected Channel; before final Amount/mask | Captured per execution; Reset/success restore `1` |
| Amount | Double value plus linear slider, final normalized blend | `1.00` | None | `0.00..1.00`; spin step `0.05` with adaptive stepping; slider maps `0..1000` to thousandths | Crossfades source and complete G'MIC result after channel processing; `0` is the source, `1` is full result | Independent of Weight; captured for execution | Reducing it controls all filter artifacts without changing expensive G'MIC work | Preview/Apply; reconstructed selected Channel; before mask; still/sequence | Captured per execution; Reset/success restore `1` |
| Histogram plot | Interactive source/result histogram | Source initially, preview result after execution | None | Mouse wheel/drag and toolbar actions below | Displays distribution and clipping diagnostics; never changes samples | Updated from target/preview; separate from preview-view display stretch | High bin counts add analysis/rendering cost; a histogram cannot diagnose spatial halos | Display only | Window lifetime |
| Zoom in | Histogram-toolbar action | Current histogram range | None | Click | Narrows the displayed histogram range | Histogram shown | Diagnostic only | Histogram display only | Window lifetime |
| Zoom out | Histogram-toolbar action | Current histogram range | None | Click | Widens the displayed histogram range | Histogram shown | Diagnostic only | Histogram display only | Window lifetime |
| Zoom reset | Histogram-toolbar action | Full histogram range | None | Click | Restores full plot range | Histogram shown | Diagnostic only | Histogram display only | Window lifetime |
| Show grid | Checkable histogram action | Checked | None | Checked/unchecked | Shows or hides plot grid | Histogram shown | Display only | Histogram display only | Window lifetime |
| Show clipping info | Checkable histogram action | Checked | None | Checked/unchecked | Shows/hides endpoint clipping readouts | Histogram shown | Binned diagnostic, not proof that spatial artifacts are absent | Histogram display only | Window lifetime |
| Absolute scales | Checkable histogram action | Unchecked | None | Checked/unchecked | Uses shared absolute vertical scaling instead of local per-channel normalization | Histogram shown | Makes channel population/height comparisons easier but can hide a low-population channel | Histogram display only | Window lifetime |
| Draw histogram areas | Checkable histogram action | Unchecked | None | Checked/unchecked | Fills areas under plotted channels | Histogram shown | Display only; overlap can obscure channel detail | Histogram display only | Window lifetime |
| Show histogram | Checkable histogram action | Checked | None | Checked/unchecked | Hides/clears the graph and disables its toolbar, or reloads source/preview | Always available while idle | Hiding saves graph work, not filter work | Display only | Window lifetime |
| Plot resolution | Combo box, histogram bins | **8-bit (256)** | None | **6-bit (64)**, **7-bit (128)**, **8-bit (256)**, **9-bit (512)**, **10-bit (1024)**, **12-bit (4096)**, **14-bit (16K)**, **16-bit (64K)**, **100 levels**, **1000 levels**, **10000 levels** | Changes diagnostic binning only | Disabled when histogram is hidden | More bins cost graph memory/rendering; output precision is unchanged | Histogram display only | Window lifetime |
| Target Image | Image selector | Attached/active target | None | Open eligible image or sequence | Changes source, Channel list, histogram, Preview, active mask, and Apply destination | Active target required; duplicate/exclusive launch gates apply; disabled during Apply | A wrong selection before Apply replaces the wrong target | Preview/Apply source and active mask | Target lifetime; Reset unchanged |
| Channel | Combo box | **RGB** for RGB; disabled **Grayscale** for mono | None | **RGB**, **Red**, **Green**, **Blue**, **CIE L* (Lightness)**, **CIE a***, **CIE b***, **CIE c***, **CIE h***, **CIE X**, **CIE Y (Luminance)**, **CIE Z**, **HSL Hue**, **HSL Saturation**, **HSL Lightness**, **HSV Hue**, **HSV Saturation**, **HSV Value**, **YCbCr Luma**, **YCbCr Blue Chroma**, **YCbCr Red Chroma** | RGB lets `fx_LCE` process all stored channels together; a component is extracted, filtered, and reconstructed | RGB target required for components; disabled during Apply; the selection is captured when Apply starts | RGB can create color halos; component round trips add buffers and, in an integer image, clip at its type's range | Preview/Apply; mask post-blend; still/sequence | Dialog-local; Reset/success restore RGB for RGB targets |
| Preview | Checkbox | Checked | None | Checked/unchecked | Enables/disables asynchronous full-image or area execution and preview pane | Process idle | Runs the complete G'MIC command after each parameter change | Preview only; mask-aware; still/current displayed frame | Window lifetime; Reset unchanged |
| Preview pane | Image viewer | Filtered result in Linear mode; initial selected area is centered `600×600` or full image if smaller | Execution autostretch is not enabled | Pan/scroll and toolbar actions below | Shows mask-aware result without modifying target | Exists only with Preview | Holds source/result/display buffers in addition to G'MIC buffers | Preview only | Window lifetime |
| Fit To Window | Preview-toolbar action | Preview fitted initially | None | Click or **Ctrl+Shift+F** | Fits whole image uniformly | Preview image required | Broad context; resampling can hide noise/rims | Preview display only | Viewer lifetime |
| Zoom In | Preview-toolbar action | Current scale | None | Click; `1.25×` relative zoom | Enlarges around viewport center | Preview image required | Improves local inspection | Preview display only | Viewer lifetime |
| Zoom Out | Preview-toolbar action | Current scale | None | Click; `0.8×` relative zoom | Reduces around viewport center | Preview image required | Shows context but hides fine artifacts | Preview display only | Viewer lifetime |
| Zoom 100% | Preview-toolbar action | Current scale | None | Click; absolute scale `1.0` | Shows one-pixel viewer scale | Preview image required | Preferred for noise, ringing, and color rims | Preview display only | Viewer lifetime |
| View Channel | Preview-toolbar combo box | **RGB** for RGB; disabled **Grayscale** for mono | None | Stored and derived RGB/CIE/HSL/HSV/HSI/YCbCr display components | Selects displayed component only | Separate from process Channel | Helps diagnose colored residuals without changing pixels | Preview display only | Viewer lifetime |
| Switch image | Preview-toolbar action | Result shown | None | Click; **Space**; right-click; **Ctrl+Shift+O** | Toggles result/original | Preview required | Comparison only; no recomputation | Preview display; still/current frame | Preview-window lifetime |
| Preview area | Preview-toolbar checkbox | Checked with centered `600×600` area when possible | None | Checked/unchecked; move/resize overlay | Processes only selected area and composites it over original preview | Preview required | Reduces tuning work but can hide broad-radius halos; Apply remains full-frame | Preview and snapshot; mask cropped consistently | Preview-window lifetime |
| Take snapshot | Preview-toolbar action | No snapshot | None | Click or **Ctrl+Shift+K** | Re-executes and opens `<target> - Local Contrast Enhancement - SNAPSHOT` | Preview required | Adds a complete execution and auxiliary still | New still; target unchanged; area/mask honored | Each action creates a new view |
| Apply | Button/action | Enabled | None | One action | Captures numeric values, requests a processing lock on the current target, processes the full still or every frame, then blends Amount/mask and commits on success | Target, Channel, and process controls lock during Apply | G'MIC blur, float conversion, component reconstruction, masks, and sequence encoding add cost | Destructive still/sequence replacement after success | History records five numeric values and, for a colour image, the selected Channel text; then Reset runs |
| Reset | Button/action | Enabled while idle | None | One action | Restores `80/0.5/1/1/1` and RGB on RGB targets | Leaves Target, Preview, histogram, mask, and preview toolbar unchanged; disabled during Apply | Checked Preview schedules another full execution | Configuration only | Immediate; also after successful Apply |
| Close | Button/action | Enabled while idle | None | One action | Closes without applying current settings | Disabled during Apply | Requests preview cancellation; G'MIC observes a shared abort flag but may stop only at its next check | Window/execution | Settings discarded at destruction |
| Sequence progress / Cancel | Shared indeterminate process/frame progress and Cancel | Created for sequence Apply | None | Cooperative Cancel | Optionally auto-debayers configured CFA frames, processes them into a temporary sequence, and replaces the target sequence only after success | Sequence target only | Numeric values and Channel are captured once for every frame; temporary disk and encode cost apply | Full sequence; mask and Amount applied per frame | Cancel removes temporary output; success records one history step and resets settings |

## How the algorithm works

### Conceptual pipeline

1. AfterNight extracts the selected process Channel when necessary, checks it
   for non-finite samples, and hands it through its G'MIC adapter, one G'MIC
   session per run, as planar float32 fractions of full scale, together with
   the `fx_LCE` definitions, which the process carries itself.
2. Bundled `fx_LCE` forms a Gaussian base with Spatial Radius and a signed local
   residual. Its script describes the method as high-radius unsharp masking.
3. Weight scales the residual. A fixed zero threshold keeps nonzero residual
   locations, Darkness Level scales its negative branch, and Lightness Level
   scales its positive branch. The script adds the branches to the source and
   clips to its `0..255` working convention.
4. AfterNight clamps the result to normalized `[0,1]`, converts back to the
   original pixel type, and reconstructs a selected color component; the result
   keeps the source metadata.
5. The shared framework crossfades the complete result by Amount, then blends
   the active mask. Still Apply swaps only the completed destination; sequence
   Apply promotes only a completed temporary sequence.

### Model and equations

Let `I(p)` be a selected normalized sample at position `p`, and let
`B_r(p)` be the G'MIC Gaussian blur at Spatial Radius `r`. The signed residual is

\[
D(p)=w\,[I(p)-B_r(p)],
\]

where `w` is **Weight**. Split it into positive and negative branches,
`D_+(p)=max(D(p),0)` and `D_-(p)=min(D(p),0)`. The script-level result is
conceptually

\[
F(p)=\operatorname{clip}\!\left(I(p)+lD_+(p)+dD_-(p),0,1\right),
\]

where `l` is **Lightness Level** and `d` is **Darkness Level**. This equation
expresses the bundled command's source/blur/residual split and branch gains; the
actual G'MIC command evaluates in its 0–255 channel convention before AfterNight
clamps the result.

AfterNight then applies the separate Amount `a`:

\[
M(p)=(1-a)I(p)+aF(p).
\]

Thus Amount `0` is an identity even though the expensive filter still runs;
Weight `0` makes `D=0` and should also make the mathematical result the source.
With effective mask weight `m(p)`, the final sample is

\[
O(p)=(1-m(p))I(p)+m(p)M(p).
\]

Mask inversion substitutes `1-m`. Clipping happens inside the filter before
Amount and mask blending, so those blends can hide but cannot restore clipped
residual detail.

### Color, boundaries, and precision

- RGB passes all stored channels to `fx_LCE`; the Gaussian is spatial and the
  arithmetic is per channel. A component choice extracts one scalar component,
  filters it, and reconstructs RGB. A derived component's reconstruction
  saturates an integer image at its type's range and keeps a floating image's
  values outside `[0, 1]`.
- G'MIC `blur` defaults to Neumann boundary conditions and a Gaussian kernel in
  the reviewed command contract. Broad radii can therefore behave differently
  near image edges from an explicitly mirrored or periodically padded model.
- All storage types enter G'MIC as float32. Integer types are normalized;
  double narrows to float. Every result is clamped to `[0,1]` and returned in the
  original storage type, rounded to nearest, so integer quantization and float
  clipping apply.
- The filter itself does not mark the result nonlinear. The result keeps the
  input processing state even though the operation is normally used after
  stretch; treat that state as incomplete provenance.

## Parameter interactions and starting points

| Combination | Conservative starting point | Too weak looks like | Too strong looks like | Adjustment order |
| --- | --- | --- | --- | --- |
| Spatial Radius × feature size | Radius `80`, Weight `0.5` on a representative area | Desired broad texture remains unchanged | Broad star/nebula shoulders and gradients become halos | Radius for scale, then Weight for strength |
| Weight × Amount | Keep Amount `1` while finding a modest Weight; lower Amount last | Low Weight and low Amount hide whether the scale is correct | High Weight clips internally even if low Amount makes the final blend look mild | Radius, Weight, tone gains, then Amount |
| Darkness × Lightness | Start `1/1` | One side of transitions lacks desired separation | Dark troughs or bright rims become asymmetric/clipped | Adjust only the branch with the problem |
| RGB × luminance goal | Compare RGB with CIE Y or L* | Luminance-only result may be subtle | Independent RGB produces colored halos | Choose Channel before strength tuning |
| Radius × Preview area | Use area for tuning, full frame for final check | Small area misses large-scale context | Edge/halo behavior surprises on Apply | Tune locally, then disable Preview area |
| Amount × mask | Tune unmasked Amount first | Double attenuation makes effect disappear | Hard mask boundaries reveal abrupt contrast change | Filter parameters, Amount, then mask feathering |

These are source-derived workflow checkpoints, not executed astronomical
quality measurements.

## Inputs, outputs, and metadata

- Output dimensions, channel count, pixel format, and source pixel type match
  the processed still/frame. Integer output is quantized; float/double output is
  still clipped and double has passed through float32.
- Still Apply modifies the selected image only after success. Take snapshot
  creates one new still; display channel and histogram settings are not baked in.
- The result keeps the source image's header cards and metadata, but LCE does
  not record a nonlinear processing state. The inherited state may therefore
  describe the source rather than the enhanced result.
- History records Spatial Radius, Weight, Darkness Level,
  Lightness Level, Amount, and, for a colour image, ` in channel: <channel>`. It
  does not record mask, inversion, G'MIC version, float hand-off, or clipping.
- Successful still or sequence Apply resets numeric settings and RGB selection;
  Target, Preview, histogram, mask, and preview toolbar remain.
- Sequence Apply writes a temporary sequence and promotes it only on success.
  Cancel removes temporary output. No direct output name/path is exposed.
- CFA sequence frames are auto-debayered before filtering when sequence
  auto-debayer is enabled; the resulting temporary sequence follows that RGB
  processing path rather than preserving the mosaic-plane interpretation.

## Performance

- The dominant work is a large-radius G'MIC Gaussian plus several full-image
  arithmetic passes. Cost grows with pixel count, stored/selected channel work,
  and Gaussian radius/support.
- Every run opens one G'MIC session and parses the built-in `fx_LCE`
  definitions before the command, because no session is kept between runs.
  Rapid Preview changes repeat that overhead.
- Memory includes float32 G'MIC input/result and internal blur/list images,
  source/destination, optional color-conversion planes, mask, preview, and
  histogram buffers. Sequence Apply also needs temporary encoded storage.
- Preview area reduces filter work while tuning; Amount `0` and Weight `0` do
  not bypass the G'MIC call. Apply always processes the full target.
- G'MIC receives a cancellation flag. Response depends on when the current
  command checks it; a broad blur can delay completion.
- No approved astronomical workload was timed, and peak memory was not measured.

## Worked example

- **Input condition and settings:** A finite normalized nonlinear image with
  smooth background and one broad transition; Radius `80`, Weight `0.5`, both
  tone levels `1`, Amount `1`.
- **Identity checkpoints:** Weight `0` gives `D=0`, so the mathematical filter
  result is the source. Amount `0` gives `M=I` regardless of the filtered result.
- **Blend checkpoint:** If a source sample is `0.40`, the clipped filter result
  is `0.60`, and Amount is `0.25`, then `M=0.45`. With mask weight `0.5`, the
  committed sample is `0.425`.
- **Inspect at 100%/history:** Check smooth-noise amplification, colored rims,
  broad bright/dark shoulders, endpoint bins, frame edges, and the five recorded
  numeric values after Apply.
- **Limitations:** No visual improvement, sequence-output, production runtime,
  or memory claim is made.

## Warnings and limitations

- This is high-radius unsharp masking, not adaptive histogram equalization and
  not deconvolution. It cannot recover information lost to blur.
- Weight is the internal residual strength. Amount is a later whole-result
  blend. Reducing Amount does not prevent internal clipping or save filter work.
- Darkness/Lightness factors can make opposite edge sides asymmetric. High
  settings create dark troughs, bright rims, and clipped endpoints.
- RGB channels are processed independently enough to produce colored halos;
  use a deliberate luminance/lightness component when appropriate.
- Every storage type is clamped to `[0,1]` after G'MIC. Out-of-range float data
  is lost and double precision narrows. An image holding NaN or infinity is
  refused by name; without G'MIC in the build, the process cannot run and
  Preview and Apply stay disabled.
- The operation does not mark nonlinear processing state, and history omits
  mask and hand-off details.

## Troubleshooting

| Symptom | Cause | Fix |
| --- | --- | --- |
| Result is unchanged | Weight `0`, Amount `0`, tone gains `0`, protective mask, or scale mismatch | Check Weight/Amount/mask first; choose Radius for visible feature scale |
| Bright/dark halos surround stars | High-radius residual plus Weight/tone gains | Reduce Weight first, then Radius; lower only the offending tone branch |
| Colored rims appear | RGB path processes color channels through the local residual model | Compare CIE Y/L*; reduce Weight/Amount |
| Background becomes coarse | The residual boosts noise at selected scales | Denoise first; reduce Weight; protect smooth background with a feathered mask |
| Preview differs from Apply | Preview area, Channel, mask/inversion, or display conditions changed | Match controls; disable Preview area for final check |
| Histogram clips although low Amount looks mild | G'MIC clips before final Amount blend | Reduce Weight/tone levels; Amount cannot restore clipped detail |
| Edge behavior differs near frame boundary | G'MIC Gaussian uses its default Neumann boundary condition | Judge away from border or crop/plan boundary handling before LCE |
| Cancel waits | Current G'MIC command has not reached an abort check | Wait; tune on a smaller area or radius |
| Error: the image holds a sample that is not a finite number | The image has a NaN or infinite sample, which G'MIC cannot process | Repair or crop the non-finite samples, then retry; the image was left unchanged |
| Preview and Apply are disabled; tooltip says the build carries no G'MIC library | This build was made without G'MIC | Use a build that includes G'MIC, or another contrast process |
| A G'MIC error is reported | G'MIC failure, invalid data, or memory exhaustion | Verify finite normalized data and memory; retry on a duplicate |

## Related workflow steps

Complete [noise reduction](../noise-reduction/wavelet-denoise.md) before local
contrast enhancement. Compare [Unsharp Mask](unsharp-mask.md) for a simpler
single residual, [Local Histogram Equalization](local-histogram-equalization.md)
for neighborhood-CDF equalization, and [CLAHE](clahe.md) for tiled
contrast-limited equalization. Use [Image History](../../interface/image-history.md)
to revert Apply and [Masks](../../interface/masks.md) to protect smooth regions.

## Credits

- [G'MIC `blur` reference](https://gmic.eu/reference/blur.html) — official
  standard-deviation, Gaussian/Deriche, and boundary-condition command contract,
  as the bundled G'MIC 3.5.4 applies it.
- [G'MIC `apply_channels` reference](https://gmic.eu/reference/apply_channels.html)
  — official channel working-range and selection semantics used by bundled
  `fx_LCE`.
- The filter is `fx_LCE` of the G'MIC script `arto_huotari.gmic`, which credits
  Arto Huotari and PhotoComiX and notes original unsharp-mask code by David
  Tschumperlé.
