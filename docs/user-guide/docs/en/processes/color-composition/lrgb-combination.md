# LRGB Combination

<!-- MEDIA_SLOT: UG-MEDIA-087 -->

## Purpose and expected result

LRGB Combination replaces the CIE L\* lightness of an RGB target with, or
blends it toward, a same-size grayscale luminance image. It keeps the target's
CIE chroma and hue unless **Saturation MTF** reshapes chroma, converts the
result back to RGB, and finally applies independent red, green, and blue
multipliers.

The expected result is a same-size RGB image carrying detail from the selected
luminance source and color from the RGB target. The process does not register,
resample, normalize, or linear-match its inputs. Prepare the two images so that
stars and other structures coincide before opening the process.

The luminance may have another representation than the RGB target: each image
is read as a fraction of its own full scale, and the result is written in the
target's representation. **Preview area** crops the luminance to the same area
as the target, so an area preview shows the combination of that area.

## At a glance

| Item | Current behavior |
| --- | --- |
| Menu path | **Process > Color Spaces > LRGB Combination** |
| Enabled when | An RGB image or RGB sequence is active; the same process is not already attached to it; and no attached process holds exclusive access |
| Inputs | One three-channel RGB target plus one open grayscale luminance image with exactly the same width and height; the luminance may be of any representation and is read in its own units |
| Outputs | Destructively replaces the RGB target after success; **Take snapshot** creates an auxiliary still; no file is written directly |
| Recommended image state | Geometrically aligned, normalized sources prepared consistently for the sRGB/D65 CIE conversion; the process does not enforce or convert an ICC profile or processing state |
| Preview support | Always-on asynchronous full-image preview with histogram and display autostretch; **Preview area** is supported, the luminance cropped to the same area |
| Sequence support | Framework-supported. Apply captures the luminance image from its view once, and every target frame is combined with it as a still image of the frame would be; there is no frame pairing or luminance-sequence traversal |
| Mask support | Supported after combination: white applies the result, black keeps the RGB target, gray blends, and **Invert Mask** reverses the weights |
| External dependencies | None; execution uses the bundled engine and does not access a tool, model, catalog, or network service |

## Before you start

- Register the luminance and RGB masters upstream. Equal dimensions only prove
  that the selector will accept them; the process does not compare stars, WCS,
  pixel scale, orientation, crop origin, or other geometry.
- The two inputs may have different representations. Each is read in its own
  units - an unsigned integer as a fraction of its largest value, a signed
  integer counted from its smallest, a floating-point image as it is - so a
  16-bit luminance over a 32-bit float target needs no conversion first.
- Keep both images normalized to their ordinary `[0,1]` interpretation. The
  target RGB is decoded using the sRGB transfer (or read as linear when the image
  states linear RGB) and a D65 white point. The grayscale source is used
  directly as normalized L\*, where `0` means L\* 0 and `1` means L\* 100.
- Complete linear matching or other intensity preparation before this process.
  There is no automatic matching between the luminance and target lightness.
- **Preview area** crops the target and the luminance to the same area, so it
  is a safe way to reduce preview cost.
- When the RGB target is a sequence, understand that Apply snapshots the
  selected view's currently displayed luminance image and reuses that snapshot
  for every target frame. Use a still target unless that behavior is intentional.
- Duplicate the target or be prepared to use Image History. **Apply** replaces
  the selected target; it does not create a new image by default.

## Procedure

1. Select an aligned RGB target and open **Process > Color Spaces > LRGB
   Combination**. Confirm the intended image in **RGB**.
2. Select a same-size grayscale master in **Luminance**. The
   **Apply** button remains disabled until both selections resolve to open
   images.
3. Start with all channel weights at `1.0` and both MTF controls at `0.5`.
4. Reduce **Lightness** below `1.0` if the separate luminance overwhelms the
   target's original lightness. This is the actual L\* blend control.
5. Adjust **Lightness MTF** only when the incoming luminance needs a global
   midtone remap. Values below `0.5` lift interior values; values above `0.5`
   lower them.
6. Adjust **Saturation MTF** cautiously. Despite the UI label, it maps the CIE
   L\*C\*h chroma plane, not an HSL/HSV saturation channel.
7. Use **Red**, **Green**, and **Blue** only for final downward channel scaling.
   They multiply the reconstructed RGB channels and cannot raise a channel
   above its unweighted value.
8. Compare the preview and original with **Switch image** under one display
   stretch. Inspect stars and high-contrast edges for registration errors, then
   select **Apply**. The luminance selection, all six numeric controls, Apply,
   Reset, and Close remain disabled until the run ends.

## Complete parameter and action reference

| UI label/action | Type and unit | Runtime default | Persisted-default exception | Valid values, step, or constraints | Effect of increasing, decreasing, or selecting | Dependencies and enable/visibility rules | Quality, artifact, runtime, memory, or disk trade-off | Scope: preview/apply, channel, mask, still/sequence | Persistence |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Histogram plot | Graph, normalized intensity/count | Current LRGB preview; RGB channels overlaid | None | Binning comes from **Plot resolution** | Shows the mask-aware preview distribution and approximate endpoint-bin clipping | Always present | Recalculation adds work; endpoint counts depend on binning | Display only; preview RGB | Recomputed after preview or resolution changes |
| Left-drag in histogram | Graph gesture | No readout | None | Hold left button and move in graph | Shows normalized coordinate readout | Histogram always present | Diagnostic only | Display only | Ends on button release |
| Middle-drag in histogram | Graph gesture | Fitted/unpanned | None | Hold middle button and drag | Pans the graph | Useful after zooming | Display cost only | Display only | Until reset/window close |
| Ctrl+mouse wheel in histogram | Graph gesture | Fitted size | None | Up `1.20x`; down `0.8x`; wheel without Ctrl scrolls | Zooms around the cursor | Interactive graph enabled | Larger graph needs more rendering area | Display only | Window lifetime |
| Zoom in | Toolbar action | Fitted size | None | Each selection multiplies size by `1.30` | Enlarges plot | Available while idle | Plot rendering only | Display only | Window lifetime |
| Zoom out | Toolbar action | Fitted size | None | Each selection multiplies size by `0.70` | Shrinks plot | Available while idle | Plot rendering only | Display only | Window lifetime |
| Zoom reset | Toolbar action | — | None | One action | Fits graph and resets scrolling | Available while idle | Plot rendering only | Display only | Immediate |
| Show grid | Toggle | Checked | None | Checked/unchecked | Shows or hides graph grid | Available while idle | Display-only aid | Display only | Window lifetime; process Reset does not change it |
| Show clipping info | Toggle | Checked | None | Checked/unchecked | Shows or hides histogram endpoint-bin counts and percentages | Available while idle | Counts are bin-based diagnostics | Display only | Window lifetime |
| Absolute scales | Toggle | Unchecked | None | Checked/unchecked | Checked normalizes each visible histogram channel to its own peak | Available while idle | Can conceal relative RGB strength; never changes pixels | Display only | Window lifetime |
| Draw histogram areas | Toggle | Unchecked | None | Checked/unchecked | Chooses filled-area or line rendering | Available while idle | Filled areas can obscure overlaps | Display only | Window lifetime |
| Show histogram | Toggle | Checked | None | Checked/unchecked | Shows or hides histogram curves | Available while idle | Display-only | Display only | Window lifetime |
| Plot resolution | Combo box, bins | **8-bit (256)** | None | **6-bit (64)**, **7-bit (128)**, **8-bit (256)**, **9-bit (512)**, **10-bit (1024)**, **12-bit (4096)**, **14-bit (16K)**, **16-bit (64K)**, **100 levels**, **1000 levels**, **10000 levels** | More bins show finer distribution | Available while idle | Higher counts increase histogram memory/rendering; processing precision is unchanged | Display only | Window lifetime |
| Luminance | Image selector | **No image** | None | Open grayscale image with target width and height, of any representation | Chooses the plane used as normalized L\*, read as a fraction of its own full scale | Rebuilt when images open, close, rename, update, or the RGB target changes; Apply requires a valid selection and disables the selector during execution | Wrong registration creates edge/color artifacts | Preview uses the current selection; Apply captures one luminance image snapshot and reuses it for every target-sequence frame | Survives Reset/successful Apply; target change clears it; not saved after close |
| RGB | Target selector | Attached active RGB view | None | Open three-channel RGB image or sequence | Supplies original L\*, C, h, metadata base, mask, and destructive destination | Menu opens only for active RGB; choosing another RGB target rebuilds Luminance choices | Target is replaced on Apply | Preview/Apply source; all RGB channels; still/sequence | Follows attachment lifecycle; Reset does not change it |
| Lightness | Double value plus slider, blend fraction | `1.0` | None | `0..1`; spin step `0.05`; slider `0..1000`, tracking off | `0` keeps target L\*; interior values blend; `1` uses selected luminance fully | Apply requires both images | Higher values transfer more luminance contrast/detail | Preview/Apply before mask; still/sequence | Captured per preview/execution; Reset/success returns `1.0` |
| Red | Double value plus slider, multiplier | `1.0` | None | `0..1`; spin step `0.05`; slider `0..1000`, tracking off | Scales reconstructed red downward; `0` removes it | Applied after L\*C\*h-to-RGB conversion | Strong reduction shifts color and can clip detail to black | Preview/Apply; red output; before mask | Captured; Reset/success returns `1.0` |
| Green | Double value plus slider, multiplier | `1.0` | None | `0..1`; spin step `0.05`; slider `0..1000`, tracking off | Scales reconstructed green downward; `0` removes it | Applied after conversion | Strong reduction shifts color and luminance appearance | Preview/Apply; green output; before mask | Captured; Reset/success returns `1.0` |
| Blue | Double value plus slider, multiplier | `1.0` | None | `0..1`; spin step `0.05`; slider `0..1000`, tracking off | Scales reconstructed blue downward; `0` removes it | Applied after conversion | Strong reduction shifts color and can suppress blue structure | Preview/Apply; blue output; before mask | Captured; Reset/success returns `1.0` |
| Lightness MTF | Double value plus slider, MTF midpoint | `0.5` (identity) | None | `0..1`; spin step `0.05`; slider `0..1000`, tracking off | Below `0.5` lifts interior luminance; above `0.5` lowers it; endpoints remain 0/1 | Applied to selected luminance before Lightness blend; exact `0.5` skips the stage | Extremes collapse most interior values toward white (`0`) or black (`1`) | Preview/Apply; luminance source; before mask | Captured; Reset/success returns `0.5` |
| Saturation MTF | Double value plus slider, CIE-chroma MTF midpoint | `0.5` (identity) | None | `0..1`; spin step `0.05`; slider `0..1000`, tracking off | Below `0.5` raises normalized C; above `0.5` lowers it | Maps target CIE C plane; it does not inspect luminance source color | Strong settings distort color and can drive later gamut clipping | Preview/Apply; target chroma; before mask | Captured; Reset/success returns `0.5` |
| Preview pane | Full-image viewer | Current combination shown | None | Standard viewer zoom/pan/display controls | Shows asynchronous mask-aware result; display autostretch does not change data | Always present; no Preview checkbox | Holds source/result/statistics buffers | Preview only; current target/displayed sequence frame | Window lifetime |
| Switch image | Preview-toolbar action | Processed preview shown | None | Click; **Space**; right-click; **Ctrl+Shift+O** | Toggles processed preview and original RGB target | Available in preview pane | Display comparison only | Preview display only | Preview-window lifetime |
| Preview area | Preview-toolbar checkbox | Unchecked | None | Off/on; move/resize overlay | Crops the RGB target and the luminance to the same area and shows their combination there | Visible while idle | A smaller area is less preview work | Preview and Take snapshot only; not Apply | Window lifetime; Reset does not clear it |
| Take snapshot | Preview-toolbar action | No snapshot | None | Click or **Ctrl+Shift+K** | Re-executes current settings at source depth and opens `<target> - LRGB Combination - SNAPSHOT` | Preview available | Allocates/processes a new full-size still; active mask applies | New still; target unchanged | Each action creates a view |
| Apply | Button/action | Disabled until valid RGB and luminance selections | None | One action | Captures the luminance image plus six numeric values and replaces the target after successful processing | Target must lock; target/luminance selectors, numeric controls, Apply, Reset, Close, and shared preview actions disable during work | Full-image conversion and buffers; target is destructive | Still/sequence; mask-aware | Success registers an **LRGB Combination** history step and resets six numeric controls; selectors remain |
| Reset | Button/action | Enabled while idle | None | One action | Restores four weights to `1.0` and both MTF values to `0.5` | Does not clear either selector, histogram state, or Preview area | Recomputes preview; no target change | Configuration only | Immediate |
| Close | Button/action | Enabled while idle | None | One action | Closes without applying current settings | Disabled with other controls during Apply | No output | Window/execution | Values are not persisted after destruction |
| Sequence progress / Cancel | Shared progress dialog and Cancel action | Created when target-sequence Apply begins | None | Progress per decoded target frame; cooperative Cancel | Writes a temporary replacement and commits only after success | Target sequence only; the Apply-time luminance snapshot remains fixed | Full sequence I/O plus per-frame CIE conversion; Cancel removes temporary output | Sequence framework; no luminance frame pairing | Success replaces sequence and resets values; Cancel retains original |

## How the algorithm works

### Target conversion and encodings

For each RGB target pixel, AfterNight reads the stored channels as fractions
of full scale, applies the sRGB decoding curve (unless the image states linear
RGB), converts sRGB to CIE XYZ with the D65 matrix, and then converts XYZ to
CIE L\*a\*b\*, computing each pixel in double. The cylindrical form is

\[
C^*=\sqrt{a^{*2}+b^{*2}},\qquad
h=\operatorname{atan2}(b^*,a^*).
\]

The engine reads L\* as `L*/100` and C\* as `C*/200`. The combination never
computes `h` itself: it keeps the hue as the direction of `a*` and `b*`,
scaling both by the transferred chroma over the original chroma. No source ICC
profile is selected or transformed by this dialog.

### Lightness and chroma transfer

Both transfer controls use the same midtones transfer function. For a
normalized sample `x` and control value `m`, with endpoints fixed at 0 and 1,

\[
T_m(x)=
\begin{cases}
0,&x\le0,\\
1,&x\ge1,\\
\dfrac{(m-1)x}{(2m-1)x-m},&0<x<1.
\end{cases}
\]

At `m=0.5`, `T_m(x)=x`, and the process skips allocation of that transfer.
Values below `0.5` raise interior samples; values above `0.5` lower them.
The endpoint settings are severe: `m=0` sends every interior sample to 1,
while `m=1` sends every interior sample to 0.

Let `L_rgb` be normalized target L\*, `L_src` the selected grayscale sample,
`w_L` **Lightness**, and `C_rgb` the target's normalized CIE chroma. The planes
used for reconstruction are

\[
L_{out}=(1-w_L)L_{rgb}+w_L T_{m_L}(L_{src}),\qquad
C_{out}=T_{m_C}(C_{rgb}),\qquad h_{out}=h_{rgb}.
\]

`L_src` is read as a fraction of the luminance image's own full scale, whatever
its representation, so a luminance of another representation than the target's
needs no conversion. The result is written once in the target's
representation.

### RGB reconstruction, weights, and mask

AfterNight converts L\*C\*h back through CIE XYZ to sRGB, applies the sRGB
encoding curve, and clips each reconstructed channel to `[0,1]`. It then
multiplies R, G, and B independently by their corresponding weights, holds
each weighted channel to `[0,1]` again, and writes it in the target's
representation, an integer rounded to the nearest code.

If an active mask is enabled, the shared framework blends after all LRGB work:

\[
RGB_{final}=RGB_{target}(1-M)+RGB_{lrgb}M.
\]

An inverted mask swaps the two weights. The luminance/chroma calculation is
global; the mask does not change its inputs or derive settings locally.

A pixel either image's validity rejects keeps the target's samples.

## Parameter interactions and starting points

| Combination | Conservative starting point | Too weak looks like | Too strong or defective looks like | Adjustment order |
| --- | --- | --- | --- | --- |
| Lightness + source preparation | `1.0` after upstream matching; otherwise start near `0.5` | Added detail is subtle | Background/contrast discontinuity or luminance dominates color | Align and match upstream, then set Lightness |
| Lightness MTF + Lightness | MTF `0.5`, then change one control at a time | Luminance tonal mismatch remains | Crushed/washed interior values, especially near MTF endpoints | Set blend first, then shape luminance |
| Saturation MTF + RGB weights | Saturation MTF `0.5`; RGB all `1.0` | Color remains subdued | Gamut clipping, hue imbalance, or missing channel | Shape chroma first; use RGB multipliers only for final reduction |
| Mask + high-contrast detail | Use a smooth, registered mask | Protected area changes too little | Halos at mask/registration boundaries | Fix alignment, soften mask, reduce Lightness |
| Same dimensions + WCS | Treat dimensions as only the selector gate | — | Doubled/misaligned stars while target WCS still appears present | Register/crop first and inspect edges before Apply |
| Sequence target + luminance | Prefer a still target | — | Every target frame receives the same displayed luminance frame | Extract/pair frames upstream; do not expect automatic pairing |

## Inputs, outputs, and metadata

- The RGB selector accepts only three-channel RGB views. The luminance selector
  accepts grayscale views with the target width and height and excludes the
  target itself. It does not check WCS, origin, orientation, or
  image-processing state; the luminance may have any representation.
- A still Apply replaces the RGB target only after the worker succeeds. **Take
  snapshot** creates a separate still. The process creates no mask, model,
  report, preset, or direct disk file.
- Output dimensions are the common input dimensions, and the output is in the
  RGB target's representation whatever the luminance's.
- The result's metadata is built from both images. Its exposure is the light
  they hold together, recorded once: each image's whole integration - its
  `TOTEXP` card, else its `LIVETIME` card, else its exposure times its frame
  count (`STACKCNT`, else `NCOMBINE`, else one) - summed. Two images that state
  the same exposure start are one acquisition - an image and a copy made of
  it, such as a lightness channel taken out of it - and count once, with the
  larger integration and the larger frame count either states, since a copy
  may have lost its `TOTEXP` and `STACKCNT` cards. Their `EXPOSURE` and
  `EXPTIME` cards are dropped; the result states its own `EXPTIME` and
  `TOTEXP` as that total, `LIVETIME` the same when either image states one,
  and `STACKCNT` and `NCOMBINE` as their frame counts, summed the same way,
  each when either image states it.
- The observation start is the earliest either image states, and its
  `DATE-OBS`, `DATE-LOC`, `MJD-OBS`, `DATE-BEG`, `MJD-BEG` and `TIME-OBS` cards
  are taken together from that image. The object, instrument, telescope,
  observer, filter, sensor temperature, gain, and binning are kept when both
  images state the same value; every other header card of the target is kept
  only when the luminance states it identically.
- The RGB target's world coordinate (WCS) cards, plate solution and pointing
  cards (`RA`, `DEC`, `OBJCTRA`, `OBJCTDEC`, `OBJRA`, `OBJDEC`, `TELRA`,
  `TELDEC`) are kept, because the pixel grid is the target's. The process does
  not verify that the luminance shares that geometry.
- Output processing state is always marked nonlinear/display RGB with operation
  provenance `lrgb-combination`, or `lrgb-mtf-combination` when either MTF is
  not `0.5`. This happens even if the inputs carried linear declarations.
- Apply registers an Image History step named **LRGB Combination** whose
  description names the luminance image and the six settings: `Luminance:
  '<name>', lightness weight <w>, channel weights R <r> G <g> B <b>, lightness
  midtones <m>, saturation midtones <m>`.
- For a sequence target, the shared framework processes decoded target frames
  into a temporary sequence, supports cooperative cancellation, and replaces
  the target only after success. Apply captures the selected view's current
  displayed image before worker execution and passes that same snapshot through
  typed parameters for every frame; luminance sequences are not traversed or synchronized.

## Performance

The main work is linear in pixel count. The engine combines each pixel in one
walk on the operation's worker threads - the RGB-to-L\*a\*b\* conversion, both
transfers, the lightness blend, the conversion back to RGB, and the channel
weights - so an MTF value other than `0.5` adds per-pixel work but no extra
image. An integer target or luminance is first copied into a Float32 working
image (Float64 for 32-bit integers); mask blending, preview display, and the
histogram add their own passes, and a full preview retains the source, result,
and display buffers.

**Preview area** limits preview work to the area it covers. On a
large master, move sliders deliberately—the sliders have tracking disabled, so
dragging schedules work on release—and consider preparing a separate aligned
crop as both RGB and luminance inputs. No approved LRGB pair was timed.

## Worked example

- **Evidence mode:** Source-, focused-test-, and calculator-derived; not
  executed on project-owned astronomical data.
- **Input condition:** At one aligned pixel, the RGB target converts to
  normalized `L=0.40`, `C=0.20`, `h=0.60`; the grayscale luminance
  sample is `0.70`.
- **Settings:** Lightness `0.25`, Lightness MTF `0.5`, Saturation MTF `0.25`,
  and Red/Green/Blue `1.0`.
- **Lightness result:** The identity Lightness MTF leaves `0.70` unchanged, so
  `L_out = 0.75(0.40) + 0.25(0.70) = 0.475`.
- **Chroma result:** `T_0.25(0.20) = (-0.75×0.20) /
  (-0.50×0.20-0.25) = 3/7 ≈ 0.428571`; hue remains `0.60`.
- **Interpretation:** Reconstruction therefore uses
  `(L,C,h)=(0.475,0.428571,0.60)`, then clips the converted sRGB channels. The
  unit channel weights leave them unchanged, and an absent mask commits the
  whole pixel. This proves the plane operations and anchors, not that the
  setting is visually appropriate for a particular astronomical target.

## Warnings and limitations

!!! note "Mixed representations and Preview area"
    The luminance may have another representation than the target; each is
    read as a fraction of its own full scale. **Preview area** crops both images
    to the same pixel area; it does not register them.

!!! warning "Equal dimensions are not registration"
    The process performs no star matching, WCS comparison, translation,
    rotation, scale correction, distortion correction, or resampling. Align and
    crop the pair upstream.

!!! note "MTF values are nonlinear"
    `0.5` is identity. Values near `0` or `1` are not small adjustments; the
    endpoints map almost every interior sample to white or black respectively.

## Troubleshooting

| Symptom | Likely cause | Fix |
| --- | --- | --- |
| Apply is disabled | Luminance is **No image**, a selection disappeared, or the RGB selection no longer matches the attached target | Select the current RGB target and a valid same-size grayscale image |
| Luminance image is absent from the list | It is RGB, has different dimensions, is the target itself, or is not open | Convert/extract grayscale, register/crop to exact dimensions, and reopen/update it |
| Preview area shows misaligned structure | Both images are cropped to the same pixel area, but they are not registered | Register and crop the luminance to the RGB target upstream |
| Doubled stars or colored edge halos | Inputs share dimensions but are not geometrically registered | Register and crop the luminance to the RGB target upstream |
| Result is nearly white or black | An MTF control is near `0` or `1` | Return both MTF values to `0.5`, then move in small steps |
| Saturation change is unexpected | Saturation MTF maps CIE C\*, and later RGB conversion clips out-of-gamut values | Move Saturation MTF toward `0.5`; leave RGB weights at `1.0` while diagnosing |
| Color balance shifts | Red, Green, or Blue weight is below `1.0` | Reset the three channel multipliers and adjust only after the L/C result is stable |
| Sequence frames all receive the same luminance structure | Apply intentionally reuses its captured luminance snapshot for every target frame; it does not traverse the selected view's sequence | Combine paired still frames upstream or use one intended fixed luminance image knowingly |

## Related workflow steps

- [Channel Composition](channel-composition.md) — assign scalar planes directly
  to RGB, CIE, hue/chroma, or luma/chroma models.
- [Linear Match](../color-calibration/linear-match.md) — prepare source intensity
  relationships before composition; it does not align images.
- [Image Registration](../registration-alignment/image-registration.md) — align geometry before
  LRGB Combination.
- [Color Saturation](../color-adjustment/color-saturation.md) — adjust color after
  composition when a CIE-chroma transfer is not the intended control.
- [Process Windows](../../interface/process-windows.md) — shared preview, mask,
  Apply, Reset, and history behavior.
- [Image Viewer](../../interface/image-viewer.md) — display stretch, masks, and
  image-state inspection.

## References and provenance

- AfterNight source and focused tests.
- [ICC sRGB specification](https://www.color.org/sRGB.pdf) — sRGB transfer,
  primaries, and D65 reference behavior used by the engine conversion.
- [CIE 1976 L\*a\*b\* colour space, CIE Colorimetry Part 4](https://cie.co.at/publications/colorimetry-part-4-cie-1976-lab-colour-space-1)
  — external definition for the L\*a\*b\* basis from which C\* and h are derived.
