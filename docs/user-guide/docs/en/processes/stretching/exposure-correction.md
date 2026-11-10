---
kind: process
stage: stretch
menu: Process > Adjustments > Exposure
summary: >-
  Multiplies the image, or one of its color components, by a factor and clamps the result, like a brighter or darker
  exposure.
data_in: linear
data_out: nonlinear
---
# Exposure Correction

## Purpose and expected result

Exposure Correction multiplies normalized samples or one selected RGB-derived
component by a scalar and clamps the result to `[0,1]`. In linear light, a
positive scalar can model a brighter exposure and a scalar below one can model
a darker exposure. **The Exposure control is not an adjustment in stops: it
does not use the standard stop equation `2^e`, and its tooltip states the factor
it applies.**

For entered value `e`, AfterNight computes the float multiplier `m` as

\[
m(e)=
\begin{cases}
1.02 + e/5, & e<0,\\
1, & e=0,\\
1 + e/5, & e>0,
\end{cases}
\qquad
y=\operatorname{clamp}(m(e)x,0,1).
\]

Thus `+1` means `1.2x`, not the `2x` of a true +1-stop adjustment; only the
positive endpoint `+5` happens to produce `2x`. Negative values use an extra
`0.02` offset, so the mapping is discontinuous at zero and small negative
values can brighten rather than darken. The guide reports this current behavior
so that results are reproducible; it is not a recommended stop definition.

For unclipped RGB in **RGB** mode, the common multiplier preserves stored
channel ratios. Once one channel clips, color and highlight ratios change.
Selecting one stored or converted component changes only that component before
RGB reconstruction and is not a physical exposure adjustment.

Every Apply records its result as nonlinear on the processing-state cards, even
an unclipped scaling of linear data, and keeps the source's other FITS cards,
astrometric solution, and acquisition record; see **Inputs, outputs, and
metadata**.

## At a glance

| Item | Current behavior |
| --- | --- |
| Menu path | **Process > Adjustments > Exposure** |
| Enabled when | An image or sequence is active, the same process is not already attached to it, and no attached process holds exclusive access |
| Inputs | One intended normalized grayscale or RGB still or sequence; an optional active mask must match target geometry |
| Outputs | Destructively replaces a still or, after successful temporary processing, a sequence; **Take snapshot** creates an auxiliary still without replacing the target; component modes reconstruct RGB; automatic sequence CFA debayer can change frame format/channels; no file is written directly |
| Recommended image state | Linear-light data for exposure-like scaling; on nonlinear data this is only encoded-value brightness scaling; either way the result is recorded as nonlinear |
| Preview support | Asynchronous RT preview of the full image or a selected preview area, using the same multiply-and-clamp operation as Apply, plus a histogram of the mask-aware result; the preview viewer can display Linear values or a display-only Autostretch |
| Sequence support | Supported: each decoded frame is processed exactly as a still image of it would be, into a temporary sequence that replaces the target only after every frame succeeds, with per-frame progress and Cancel; the configured CFA auto-debayer request can produce RGB frames |
| Mask support | Supported for preview, still Apply, and sequence Apply by post-transform blending; white applies, black protects, gray blends, and **Invert Mask** reverses weights |
| External dependencies | None; bundled engine only; no tool, profile, catalog, or network service |

## Before you start

- Use linear-light data if the goal is exposure-like scaling. Multiplying
  gamma-encoded or already stretched samples changes display values, not scene
  exposure.
- Apply records its result as nonlinear even when no sample clipped. If later
  steps rely on a recorded linear state, run them before Exposure Correction.
- Treat the control as a project-specific scalar control, not a stop control.
  For a true multiplier `2^s`, the current positive branch can represent only
  `1..2x`, and its label does not identify the equivalent stop value.
- Inspect highlights before Apply. Every stored/component value above `1/m` is
  clipped to `1`; no later operation can recover those distinctions.
- **RGB** preserves ratios only while all channels remain below the clamp.
  Red/Green/Blue or derived-component processing intentionally changes color.
- Avoid hue/chroma modes for ordinary exposure work. Their normalized encoded
  coordinates are multiplied and clamped as ordinary scalars; hue is not
  treated as a circular angle.
- In a converted-component mode only the component is clamped: on a floating
  image the reconstructed RGB color is not clamped to `[0,1]`, and on an integer
  image it saturates at the type's range.
- For a sequence, verify the selected Channel, active mask, and CFA auto-debayer
  setting before Apply. The same captured Exposure/Channel selection is used for
  every decoded frame; cancellation discards the temporary output.

## Procedure

1. Select a still or sequence and open **Process > Adjustments > Exposure**. Confirm
   **Target Image**, leave **Channel: RGB**, and keep **Preview** selected.
2. Change **Exposure** slowly. Positive values multiply by `1+e/5`; negative
   values multiply by `1.02+e/5`. The identity value `0` disables Apply.
3. Inspect the histogram's white endpoint and bright colored stars at 100%.
   Reduce the value if one or more channels form a clipped plateau. On a large
   image, check **Preview area** on the preview view to tune on one region.
4. If choosing a single stored/derived channel, inspect color and gamut. This
   is component scaling, not exposure.
5. If a mask is active, judge the mask-aware preview. Histogram clipping refers
   to displayed result bins after blending.
6. Apply when the result is acceptable. For a sequence, monitor the progress
   dialog and use Cancel to preserve the original sequence. Image History holds
   one **Exposure Correction** step that Undo reverses, and the result is
   recorded as nonlinear with `exposure-correction` as the operation.

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
| Show clipping info | Toggle | Checked | None | Checked/unchecked | Shows/hides histogram endpoint-bin counts | Disabled with hidden histogram | Binning-dependent estimate, not a count of values clipped specifically by this process | Display only | Window lifetime |
| Absolute scales | Toggle | Unchecked | None | Checked/unchecked | Checked normalizes each visible histogram channel to its own peak | Disabled with hidden histogram | Can conceal relative channel strength; no image normalization | Display only | Window lifetime |
| Draw histogram areas | Toggle | Unchecked | None | Checked/unchecked | Filled-area versus line drawing | Disabled with hidden histogram | Area overlap can obscure channels | Display only | Window lifetime |
| Show histogram | Toggle | Checked | None | Checked/unchecked | Hides/clears graph and disables toolbar, or reloads source/preview | Always available while idle | Hiding saves graph work, not preview processing | Display only | Window lifetime; process Reset does not change it |
| Plot resolution | Combo box, bins | **8-bit (256)** | None | **6-bit (64)**, **7-bit (128)**, **8-bit (256)**, **9-bit (512)**, **10-bit (1024)**, **12-bit (4096)**, **14-bit (16K)**, **16-bit (64K)**, **100 levels**, **1000 levels**, **10000 levels** | More bins refine the graph/endpoint estimates | Disabled with hidden histogram | Higher bins cost graph memory/rendering; output precision unchanged | Display only | Window lifetime |
| Exposure | Double value plus slider labelled **Exposure**; its tooltip states the factor below | `0.00` identity | None | `-5.00..+5.00`; two decimals; spin step `0.03`; slider linear over 1,000 positions | Positive uses `1+e/5`; negative uses `1.02+e/5`; `-0.10` is another identity and `(-0.10,0)` brightens despite being negative | Enabled while idle; Apply disabled only exactly at `0.0` | Positive values clip samples above `1/m`; negative branch reaches `0.02x` at `-5`; displayed value is not a true stop count | Preview/Apply; selected channel/component; mask post-blend; still/sequence | Captured per execution; Reset/success returns `0.0` |
| Target Image | Image selector | Attached/active target | None | Open eligible image or sequence | Changes source, Channel list, histogram, Preview, Apply | Active image required; duplicate/exclusive gates apply | Wrong target is destructively replaced; sequence writes a temporary result and replaces only after success | Preview/Apply source and active mask | Target lifecycle; Reset does not change it |
| Channel | Combo box | **RGB** for RGB; disabled **Grayscale** for mono | None | **RGB**, **Red**, **Green**, **Blue**, **CIE L* (Lightness)**, **CIE a***, **CIE b***, **CIE c***, **CIE h***, **CIE X**, **CIE Y (Luminance)**, **CIE Z**, **HSL Hue**, **HSL Saturation**, **HSL Lightness**, **HSV Hue**, **HSV Saturation**, **HSV Value**, **YCbCr Luma**, **YCbCr Blue Chroma**, **YCbCr Red Chroma** | RGB/Grayscale multiplies stored samples; Red/Green/Blue multiplies one stored channel; other modes convert, multiply one encoded component, and reconstruct RGB | Component choices require RGB; mono selector disabled | RGB preserves ratios until clipping; other modes add conversion work and gamut risk (a floating reconstructed color is not clamped to `[0,1]`, an integer one saturates at the type's range); source metadata is kept in every mode | Preview/Apply; selected component; mask post-blend; still/sequence | Captured by current selection; Reset/success returns RGB when target is RGB |
| Preview | Checkbox | Checked | None | Checked/unchecked | Enables/disables the asynchronous multiply/clamp preview and switches histogram result/source | Process idle | Result/histogram buffers cost time/memory; same mathematical transfer as Apply | Preview only; mask-aware; current still/current displayed sequence frame | Window lifetime; Reset does not change it |
| Preview pane | Full-image viewer | Current result | None | Standard viewer zoom/pan/display controls | Shows mask-aware result | Exists only with Preview | Holds result image; component conversions add buffers | Preview only; target unchanged | Window lifetime |
| Display Mode | Preview-toolbar combo box | Inherits the target view's **Linear** or **Autostretch** mode when the preview is created | None | **Linear**, **Autostretch** | Changes only how the Exposure result is displayed; Autostretch uses cached full-target source statistics without changing process output pixels | Exists only with Preview; Autostretch enables **Autostretch Settings** | A source-derived display stretch can reveal faint structure or exaggerate noise and must not be mistaken for Exposure output | Preview display only; no effect on Apply, snapshots, masks, or sequences | Preview-view state; process Reset does not change it |
| Autostretch Settings | Preview-toolbar dialog action | Disabled in Linear mode; current target-view settings are inherited | Current shared-view values load when opened | Opens the shared settings for Target Background, Shadows Clipping, linked channels, neutralization/color calibration, saturation/background, brightness, contrast, banner, Reset, and Close | Tunes only the preview viewer's display stretch | Enabled only in Autostretch mode; the action is disabled while its dialog is open; **Apply Stretch** is hidden in this RT-preview dialog | Aggressive display settings can exaggerate noise/color without changing Exposure output | Preview display only | Viewer-scoped; not captured in Exposure parameters or history |
| Switch image | Preview-toolbar action | Exposure preview shown | None | Click; **Space**; right-click; **Ctrl+Shift+O** | Toggles the preview viewer between the current result and original target | Exists only with Preview | Display comparison only; no recomputation or pixel change | Preview display only; still/current displayed sequence frame | Preview-window lifetime |
| Preview area | Preview-toolbar checkbox | Unchecked | None | Checked/unchecked; move/resize the selection overlay | Limits preview and snapshot processing to a selected area and composites that result over the original full image; inside the area the preview is what Apply makes of an image of that area | Exists only with Preview | Reduces multiply/component work while tuning; does not limit final Apply | Preview and Take snapshot only; mask is cropped consistently | Preview-window lifetime; Reset does not change it |
| Take snapshot | Preview-toolbar action | No snapshot | None | Click or **Ctrl+Shift+K** | Re-executes the current Exposure settings at source depth and opens a new still named `<target> - Exposure Correction - SNAPSHOT` | Exists only with Preview | Allocates and processes an auxiliary image; active mask and selected preview area are honored; Display Mode/Autostretch is not baked into the snapshot | New still image; target is unchanged; a snapshot is a preview result and is not recorded as nonlinear | Each action creates a separate image view |
| Apply | Button/action | Disabled at Exposure `0.0` | None | One action at any other displayed value, including the second identity `-0.10` | Captures Exposure and runs one fused multiply-and-clamp program over the selected channel for a still or each decoded sequence frame | Target must lock; controls disabled during the run | One parallel engine pass; integer samples add a floating working copy; conversions and masks add passes; recording the nonlinear state usually copies the result once more; clipping is irreversible | Destructive still/sequence replacement; active mask applies | Success records one history step, then resets Exposure and Channel |
| Reset | Button/action | Enabled while idle | None | One action | Restores Exposure `0.0` and RGB for an RGB target | Does not change Target, Preview, or histogram toolbar/zoom | No output cost | UI only | Immediate |
| Close | Button/action | Enabled while idle | None | One action | Closes without Apply | Disabled during commit | Pending preview result is discarded | UI only | No setting persistence |
| Sequence progress / Cancel | Shared modal progress dialog and Cancel action | Created automatically when sequence Apply starts | None | One progress update per frame; cooperative Cancel | Processes decoded frames into a temporary sequence and replaces the target only after success | Sequence target only | Sequence processing adds decode/encode and temporary-disk cost; automatic CFA debayer can produce RGB frames | Sequence Apply; chosen channel and active mask apply to every frame | Cancel or a failed frame discards the temporary sequence; success replaces the sequence, records one history step, and resets settings |

## How the algorithm works

### Transfer, clipping, and channel behavior

The standard exposure-stop law is `m=2^s`: `+1` doubles linear-light samples
and `-1` halves them. AfterNight instead uses the piecewise linear control map
shown above. On the positive branch, the equivalent stop change is
`log2(1+e/5)`; on the negative branch it is `log2(1.02+e/5)`.

For normalized finite input, multiplication is followed by an unconditional
clamp. Positive values therefore create a hard highlight plateau wherever
`x >= 1/m`. The negative branch's multiplier remains positive over its allowed
range, reaching `0.02` at `-5`, so ordinary normalized values do not black-clip
except exact zero. Floating samples outside `[0,1]` are also clamped after
multiplication; a not-a-number sample stays not-a-number. Samples that the
image's own validity rule or mask marks as invalid are left as they were.

### RGB and selected components

- **RGB** uses one multiplier independently on every stored R/G/B value. Ratios
  are preserved before clipping; they diverge when channels reach white at
  different input levels.
- **Red**, **Green**, or **Blue** changes only one stored channel.
- CIE, HSL, HSV, and YCbCr modes convert the image's red, green, and blue,
  multiply and clamp one component handed over in the unit interval as a
  Float32 working plane (Float64 for a 64-bit float image), then reconstruct
  RGB. A source whose description says it is linear is read as linear RGB; any
  other is read as sRGB-encoded. Reconstruction can leave the gamut or alter
  hue/chroma: a floating result is not clamped to `[0,1]`, and an integer result
  is rounded to the nearest code and saturated at the type's range. CIE
  components are reconstructed through the exact inverse of the XYZ matrix, and
  YCbCr through unrounded luma-chroma coefficients. Hue receives no circular
  wrap treatment.
- Grayscale and one-channel CFA stills are treated as one stored channel. This
  process does not demosaic a still.

The operation keeps the source pixel type. Integer input is read in normalized
units (an unsigned integer as a fraction of its largest value, a signed one
counted from its smallest value to its largest), converted to a floating working
copy (Float64 for the two 32-bit types, Float32 otherwise), multiplied by the
single-precision factor, clamped, and rounded back to the nearest code, ties to
even. Float input remains float and double remains double. The multiply and
the clamp are one fused engine pass on the operation's threads. Preview and
Apply use the same engine operation, although histogram bins remain diagnostic
only.

## Inputs, outputs, and metadata

### Image state, metadata, masks, and history

Scalar multiplication is exposure-like only in linear light and, before
clipping, preserves linearity. Every Apply nevertheless records its result as
nonlinear on the processing-state cards a saved file carries: `ANLIN` becomes
`nonlinear`, `ANASRC` `process`, `ANOP` `exposure-correction`, and `ANBASIS` the
basis a stretched image of its kind is in (for example `display-rgb` for an RGB
image). A linear transfer function the source claimed is no longer claimed, and
the image's lineage gains an entry naming the engine operation with the exposure
and its factor.

- Every other FITS card, the astrometric solution, and the acquisition record
  are kept, in stored-channel and converted-component modes alike.
- A **Take snapshot** image is a preview result and is not recorded as
  nonlinear.

An active mask is blended afterward as
`source*(1-mask) + corrected*mask`, reversed for an inverted mask. Factor
derivation and clipping happen before this blend. Image History records one step
named **Exposure Correction**, described as `Adjust exposure to <e>`, followed
by ` in channel: <Channel>` for an RGB target (for example
`Adjust exposure to 1.5 in channel: RGB`); it states the displayed control
value, not the actual multiplier/equivalent stops. Undo restores the previous
image. Successful still Apply resets Exposure to zero and Channel to RGB.

A run whose stop is requested (Cancel in a sequence's progress dialog, or the
target closing while a run works) is stopped inside the engine operation and
publishes nothing; the image and its history stay as they were.

Sequence Apply processes each decoded frame as an Apply processes a still image
of it, active mask included, writes the frames to a temporary sequence, supports
Cancel from its progress dialog, and replaces the target only after every frame
succeeds; a cancelled or failed run leaves no temporary output. The current CFA
auto-debayer request is passed to the sequence reader. One history step is
recorded.

## Parameter interactions and starting points

| Combination | Conservative starting point | Too weak looks like | Too strong or defective looks like | Adjustment order |
| --- | --- | --- | --- | --- |
| Exposure + highlight headroom | Small positive increments around `0.1..0.3` | Brightness barely changes because multipliers are only `1.02..1.06` | White plateau and colored-star saturation | Inspect maximum/channel histograms, raise slowly, stop before clipping |
| Negative value + current discontinuity | Use `e <= -0.10` only after checking the formula | `-0.10` changes nothing | Values between `-0.10` and `0` brighten; near zero the preview jumps | Treat the value as scalar control, not stops; verify result numerically |
| RGB + channel clipping | Leave **RGB** selected | Desired brightening remains modest | One channel clips before others and changes color | Inspect each RGB endpoint, then reduce Exposure |
| Component mode + color | Prefer RGB for physical exposure intent | Selected component has little visible effect | Hue, saturation, or gamut shifts after reconstruction | Choose component only for deliberate color editing; inspect colors |
| Mask + clipping | Use soft transitions and modest positive values | Protected region changes too little | Applied region clips or mask boundary becomes visible | Tune unmasked transfer, then inspect mask-aware result |

## Performance

The core is one parallel multiply/clamp pass. Integer samples add a floating
working copy; stored single-channel modes avoid color conversion; component
modes compute a floating working plane and convert back. The run works on a copy
of the target, and recording the nonlinear state usually copies the result once
more; if the session's memory budget refuses one of these copies, Apply
publishes nothing and reports the refusal. An active mask adds another
full-image blend, and Preview also retains a result and updates the histogram.
Histogram resolution changes diagnostic rendering, not output precision or the
multiplier.

Disable Preview while entering several values on a large image, or use
**Preview area**, then re-enable the full preview for the final check. No
approved project image was timed, so no production throughput or memory
figure is claimed.

## Worked example

- **Positive setting:** With Exposure `+1`, the current multiplier is `1.2`, not
  `2`. Inputs `[0.25, 0.50, 0.90]` become `[0.30, 0.60, 1.00]`; the last raw
  value is `1.08` and clips. The equivalent unclipped change is about
  `log2(1.2)=+0.263` stops.
- **Negative setting:** With Exposure `-1`, the multiplier is `0.82`, so input
  `0.50` becomes `0.41`. This is about `-0.286` stops, not `-1` stop (`0.25`).
- **Discontinuity checkpoint:** Exposure `-0.10` produces multiplier `1.0` even
  though Apply is enabled. Exposure `-0.05` produces `1.01`, so input `0.50`
  becomes `0.505`: a negative displayed value brightens. At exactly `0`, the
  multiplier returns to `1` and Apply is disabled.
- **Inspect in real use:** Check each RGB white endpoint, the brightest star
  cores, and the recorded history value. Do not translate the displayed value
  into stops without applying the piecewise equation.
- **Limitations:** No real-image color fidelity, clipping recovery, metadata
  round trip, executed sequence output, or production timing was measured.

## Warnings and limitations

!!! danger "The current control is not calibrated in exposure stops"
    AfterNight does not compute `2^Exposure`. A displayed `+1` gives `1.2x`, and
    `-1` gives `0.82x`. Use the documented piecewise multiplier when exact
    scaling matters.

!!! danger "Small negative values can brighten"
    The negative branch adds `0.02`, creating a discontinuity at zero.
    `-0.10` is an enabled identity and values between `-0.10` and `0` brighten.

- Positive scaling hard-clips every value above `1/m`; clipped detail and color
  ratios cannot be restored later.
- Scaling nonlinear/encoded samples is not scene exposure compensation. Work in
  linear light when that interpretation matters.
- Every Apply is recorded as nonlinear, even an unclipped scaling of linear
  data. Run steps that rely on a recorded linear state before Exposure
  Correction.
- Component modes are color-coordinate edits, not exposure changes, and a
  floating reconstructed color is not clamped to `[0,1]`.
- History records the displayed value rather than actual multiplier, so the
  piecewise formula is required to reproduce the math outside AfterNight.
- Integer results are rounded to the nearest code, ties to even.
- A not-a-number floating sample is not repaired. Use finite normalized data.

## Troubleshooting

| Symptom | Cause | Fix |
| --- | --- | --- |
| `+1` does not look one stop brighter | Current multiplier is `1+1/5=1.2`, not `2^1=2` | Use the current formula, which the tooltip states, and judge highlights; do not read the value as stops |
| A small negative value brightens | Negative branch is `1.02+e/5`; it exceeds one for `-0.10<e<0` | Use `e<-0.10` for darkening and verify the actual multiplier |
| Exposure `-0.10` runs but changes no pixel in `[0,1]` | It is a second mathematical identity, while Apply is disabled only at exact `0.0`; floating samples outside `[0,1]` are still clamped, and the result is still recorded as nonlinear with a history step | Reset to `0`; avoid committing `-0.10` |
| Highlights become flat white or change color | Multiply result is clamped to one; RGB channels can reach the clamp at different inputs | Reduce Exposure or create headroom before scaling |
| A floating image has values outside `[0,1]` after a component mode | Only the component is clamped; it is converted back to RGB without clamping a floating result | Use a smaller change or a stored-channel mode |
| A later step treats the result as nonlinear | Apply records `ANLIN` `nonlinear` and `ANOP` `exposure-correction` on the result, even without clipping | Expected; run steps that need a recorded linear state first, or undo the step in Image History |
| Apply fails with an error naming the memory budget | The session's memory budget cannot hold the run's copy of the target | Close other images, work on a smaller image, or raise the memory limit on the Processing settings page (it takes effect at the next start); the target is unchanged |
| A cancelled sequence does not replace the target | The sequence run writes a temporary sequence and publishes it only when every frame succeeded; Cancel stops the run and leaves no output | This is expected; restart Apply when ready |
| Apply resets the controls | Shared successful-commit path calls Exposure Reset | This is expected; re-enter settings for another still |

## Related workflow steps

Complete calibration and linear color work before using an exposure-like
correction. Use [Image Statistics](../../interface/image-statistics.md) and the
histogram to measure highlight headroom. Compare
[Gamma Correction](gamma-correction.md) for nonlinear midtone shaping,
[Histogram Transformation](histogram-transformation.md) or
[Statistical Stretch](statistical-stretch.md) for a main stretch, and
[Adjust Brightness/Contrast](../color-adjustment/adjust-brightness-contrast.md)
for an encoded-value brightness/contrast control. Monitor
[Image History](../../interface/image-history.md) after Apply.
