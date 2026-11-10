---
kind: process
stage: finish
menu: Process > Adjustments > Lightness/Chroma/Hue
summary: >-
  Adjusts the perceptual lightness, chroma and hue of an RGB image in the CIE LCh model.
data_in: nonlinear
data_out: nonlinear
---
# Adjust LCh

## Purpose and expected result

Adjust LCh converts the current stored RGB values to CIE L*C*h°, adds to
lightness and hue, scales chroma, then converts back to RGB. It is useful for
separating a perceptual-lightness adjustment from colorfulness and hue.

The implementation reads the stored triples as sRGB with the D65 reference
white: through the sRGB transfer function, unless the image's description
states a linear RGB color space, in which case they are read as already linear.
The result is written back the same way.
It does not inspect an embedded ICC profile. Treat it as a display-oriented
finishing operation and work on a duplicate.

## At a glance

| Item | Current behavior |
| --- | --- |
| Menu path | **Process > Adjustments > Lightness/Chroma/Hue** |
| Enabled when | A color image (red, green, and blue) or a sequence showing one is active; disabled for grayscale. A launch is refused with a warning when this process is already attached to the image or an attached process holds exclusive access |
| Inputs | One RGB still, or a sequence whose decoded frames are RGB, in any integer or floating representation; optional enabled active target mask. The whole image is processed; there is no channel selector |
| Outputs | Final Apply replaces the target with the same geometry, representation, and RGB format; successful sequence Apply uses a temporary replacement sequence; Take snapshot creates a still |
| Recommended image state | Nonlinear RGB after color calibration and the main stretch; committed output is marked nonlinear |
| Preview support | Asynchronous preview that runs the same conversion and adjustment as Apply over the target, or over the Preview area; fixed linear display, histogram, original/result switch, Preview area, and snapshot |
| Sequence support | Every decoded frame is processed as Apply processes a still image of it, into a temporary replacement sequence, with per-frame progress and Cancel; decoded frames must be RGB unless automatic CFA debayer supplies RGB |
| Mask support | Active mask post-blends source/result for Preview, snapshot, still Apply, and sequence Apply; white applies, black protects, gray blends, Invert Mask reverses weights |
| External dependencies | None; the conversion is implemented in the bundled engine |

## Before you start

Finish background/color calibration and the intended stretch. L* is more
perceptual than HSL Lightness, but CIELAB/LCh is not perfectly uniform and is
not HDR appearance modeling. Results also depend on the sRGB/D65 reading
matching the meaning of the stored RGB values.

**Preview area** is a safe way to test a region: inside the area the Preview is
what Apply makes of that area.

## Procedure

1. Duplicate the RGB target and verify that no active mask is selected, or
   inspect the intended mask and inversion state.
2. Open **Process > Adjustments > Lightness/Chroma/Hue**.
3. Keep **Preview** on and use **Switch image** to compare; enable
   **Preview area** to work on a region.
4. Change one control at a time. Start within about `-5..5`; positive Chroma is
   unusually steep, so `+5` already means `1.2x`.
5. Inspect neutral objects, stars, saturated detail, histogram endpoints, and
   mask boundaries.
6. Apply to the duplicate. The Preview shows what Apply writes; reassess the
   committed RGB image.
7. Inspect Image History and metadata before saving or continuing.

## Complete parameter and action reference

| UI label/action | Type and unit | Runtime default | Persisted-default exception | Valid values, step, or constraints | Effect of increasing, decreasing, or selecting | Dependencies and enable/visibility rules | Quality, artifact, runtime, memory, or disk trade-off | Scope: preview/apply, channel, mask, still/sequence | Persistence |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Histogram plot | Graph, normalized intensity/count | Source or Preview RGB channels | None | Binning follows Plot resolution | Shows source or mask-aware Preview distribution and approximate endpoint counts | Show histogram controls visibility | Recalculation adds work; endpoints are bin-dependent | Display only | Recomputed |
| Left-drag in histogram | Graph gesture | No readout | None | Hold left/move | Shows normalized coordinates | Histogram shown | Diagnostic only | Display only | Ends on release |
| Middle-drag in histogram | Graph gesture | Fitted/unpanned | None | Hold middle/drag | Pans graph | Histogram shown | No output effect | Display only | Until reset/close |
| Ctrl+mouse wheel in histogram | Graph gesture | Fitted size | None | Up `1.20x`; down `0.8x` | Zooms around cursor | Interactive graph | Rendering only | Display only | Window lifetime |
| Zoom in | Toolbar action | Fitted | None | Each action `1.30x` | Enlarges plot | Disabled when histogram hidden | Rendering only | Display only | Window lifetime |
| Zoom out | Toolbar action | Fitted | None | Each action `0.70x` | Shrinks plot | Disabled when histogram hidden | Rendering only | Display only | Window lifetime |
| Zoom reset | Toolbar action | — | None | One action | Fits plot/resets scrolling | Disabled when histogram hidden | No output effect | Display only | Immediate |
| Show grid | Toggle | Checked | None | Checked/unchecked | Shows/hides graph grid | Disabled when histogram hidden | Display-only aid | Display only | Window lifetime |
| Show clipping info | Toggle | Checked | None | Checked/unchecked | Shows/hides endpoint-bin counts | Disabled when histogram hidden | Estimate, not exact float clipping | Display only | Window lifetime |
| Absolute scales | Toggle | Unchecked | None | Checked/unchecked | Normalizes each displayed channel to its own peak | Disabled when histogram hidden | Can conceal relative channel strength | Display only | Window lifetime |
| Draw histogram areas | Toggle | Unchecked | None | Checked/unchecked | Filled-area versus line graph | Disabled when histogram hidden | Overlap may obscure channels | Display only | Window lifetime |
| Show histogram | Toggle | Checked | None | Checked/unchecked | Hides/clears or reloads graph/toolbar | Idle | Saves graph work only | Display only | Window lifetime |
| Plot resolution | Combo, bins | **8-bit (256)** | None | **6-bit (64)** to **16-bit (64K)** plus **100/1000/10000 levels** | Refines graph and endpoint estimate | Disabled when histogram hidden | Higher values use more graph memory/rendering; output precision unchanged | Display only | Window lifetime |
| Hue | Integer/slider, degrees | `0` | None | `-180..180`, step `1`; the turned hue wraps modulo `360°` | Adds degrees to LCh h° and wraps around the circle | Idle; Apply disabled only at all-zero identity; locked during Apply | Large turns remap every chromatic color; hue is ill-defined near zero chroma | Preview/snapshot/Apply, every RGB pixel; mask post-blend; still/sequence | Captured per execution; Reset/success `0` |
| Chroma | Integer/slider, mapped strength | `0` | None | `-100..100`, step `1`; multiplier is `1+C/100` below zero and `1+C/25` above zero; result `0..200` | `-100` removes chroma; `+25` doubles; `+100` requests `5x` | Idle; locked during Apply | Positive side is four times steeper than percentage intuition; high C readily leaves sRGB gamut and clips | Preview/snapshot/Apply, every RGB pixel; mask post-blend; still/sequence | Captured; Reset/success `0` |
| Lightness | Integer/slider, CIE L* units | `0` | None | `-100..100`, step `1`; result clamps `0..100` | Adds absolute L* units | Idle; locked during Apply | Endpoint clipping removes tonal detail; not exposure scaling | Preview/snapshot/Apply, every RGB pixel; mask post-blend; still/sequence | Captured; Reset/success `0` |
| Target Image | RGB selector | Attached/active RGB target | None | Open color stills/sequences | Changes source, histogram, and Preview | Lists color views only; the window closes if its target stops holding a color image; locked during Apply | Selected target is destructively replaced | Source and active mask | Target lifecycle |
| Preview | Checkbox | Checked | None | Checked/unchecked | Creates/destroys asynchronous fixed-linear Preview | Remains interactive during Apply | Each preview run works on a copy of the target or of the Preview area, plus a floating working copy for integer images; a burst of changes collapses into one run with the latest values; Apply cancels a preview in progress | Preview/current frame | Window lifetime |
| Preview pane | Full-image viewer | Latest Preview result | None | Viewer zoom/pan/channel controls | Displays mask-aware result | Exists only with Preview | Shows the conversion and adjustment Apply writes, in the target's representation and RGB format | Preview only | Window lifetime |
| Switch image | Preview action | Result shown | None | Click, **Space**, right-click, or **Ctrl+Shift+O** | Toggles result/original | Preview exists | Comparison only | Display only | Preview lifetime |
| Preview area | Preview checkbox | Unchecked | None | Checked/unchecked; area selected in the Preview pane | Runs the Preview over the selected area only and shows that result over the rest of the image as it was | Preview exists; disabled during Apply | Faster regional test; inside the area the result is what Apply makes of an image of that area | Preview/snapshot | Window lifetime |
| Take snapshot | Preview action | None | None | Click or **Ctrl+Shift+K** | Creates `<target> - Adjust Lightness/Chroma/Hue - SNAPSHOT`; with Preview area on, the snapshot is the composite the Preview showed | Preview exists; disabled during Apply | Samples are what the Preview shows; the snapshot keeps the source's metadata and is not recorded as nonlinear | Auxiliary still; mask applies | Creates a view |
| Apply | Button/action | Disabled at all-zero identity | None | One action when any control nonzero | Captures values; runs the conversion and adjustment on a worker over the still or every decoded sequence frame | Target lock required; during the run Target, the sliders, Apply, Reset, Close, and the Preview pane's snapshot and area actions are disabled whether or not Preview is on; the Preview checkbox stays available | Destructive; full RGB↔LCh conversion in double precision per pixel; gamut clipping | Still/sequence replacement; mask applies | Success records history/resets |
| Reset | Button/action | Enabled idle | None | One action | Returns all three controls to `0` | Does not change target/Preview/histogram | No image change | Configuration only | Immediate |
| Close | Button/action | Enabled idle | None | One action | Closes without applying current values | Disabled during Apply; closing the window while a run is in progress hides it at once and it closes when the run has landed | Nothing waits on the GUI thread; a sequence Apply in progress is cancelled | Window/execution | Values not saved |
| Sequence progress / Cancel | Shared progress and Cancel | Sequence Apply | None | Per-frame progress; Cancel | Writes temporary sequence; replaces only when every frame succeeds | RGB decoded frames | Conversion plus decode/encode/temp disk; Cancel is observed between frames and inside the engine operation | Sequence Apply; same values/mask | Cancel discards temp; success resets |

## How the algorithm works

For normalized stored RGB `(r,g,b)`, each component is first decoded with the
sRGB transfer curve: the linear branch uses `v/12.92` through `0.04045`; the
other branch uses `((v+0.055)/1.055)^2.4`. An image whose description states a
linear RGB color space skips this step. The engine then applies the sRGB/D65
matrix, derived from the published sRGB primaries and the D65 white and shown
here rounded to seven places:

```text
X = 0.4124564 r + 0.3575761 g + 0.1804375 b
Y = 0.2126729 r + 0.7151522 g + 0.0721750 b
Z = 0.0193339 r + 0.1191920 g + 0.9503041 b
```

XYZ becomes Lab relative to the D65 white `(0.95047, 1, 1.08883)` - D65
`(95.047, 100, 108.883)` on the scale where white's Y is 100 - then
`C* = sqrt(a*²+b*²)` and `h° = atan2(b*,a*)` in `[0,360)`.
The controls apply:

```text
L' = clamp(L* + Lightness, 0, 100)
mC = Chroma > 0 ? 1 + Chroma/25 : 1 + Chroma/100
C' = clamp(C* × mC, 0, 200)
h' = (h° + Hue) mod 360               # wraps around the circle
```

The inverse reconstructs Lab/XYZ, applies the exact inverse of the matrix above
and the sRGB transfer (none for a linear-RGB image), then clamps each RGB
component to `0..1`. This is hard gamut clipping, not perceptual gamut mapping.
Every pixel is computed in double precision.

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

Preview runs this same conversion and adjustment over the target, or over the
Preview area, on a worker. The engine operation observes a stop request; a
stopped run publishes nothing.

## Parameter interactions and starting points

| Combination | Conservative start | Too weak | Too strong | Adjustment order |
| --- | --- | --- | --- | --- |
| Lightness + stretched RGB | `-3..3` L* | Little tonal change | Black/white clipping | Prefer main stretch for tone, then finish with L* |
| Chroma + saturated detail | `-5..5`; `+5=1.2x` | Colors remain muted | RGB gamut clips; halos/noise intensify | Raise slowly; watch endpoints and stars |
| Hue + calibrated color | `-5..5` degrees | Cast remains | Star/nebula colors move into unrelated hues | Turn first; the hue wraps, so a large turn carries colors all the way round |
| Mask + strong color change | Inspect mask first | Region barely changes | Visible boundary | Feather upstream/reduce amount |
| Preview area + regional test | Choose an area holding neutrals, stars, and saturated detail | The area misses the colors that matter | One area is taken to stand for the whole image | Tune on the area, then check the whole-image Preview before Apply |

## Inputs, outputs, and metadata

- RGB stills keep geometry, representation, and RGB format.
- Apply keeps every FITS card it does not own, the astrometric solution, and the
  acquisition record, and records the result as nonlinear on the
  processing-state cards: `ANLIN` nonlinear, `ANASRC` process, `ANOP`
  `lch-adjustment`, and `ANBASIS` `display-rgb` (a `monochrome` basis the
  source's cards recorded is kept). A linear encoding the source claimed is no
  longer claimed, and the image's lineage gains an entry naming
  `an::adjustImageLch` with the three values.
- Preview snapshots hold what the Preview showed. They keep the source's
  metadata and are not recorded as nonlinear.
- The active mask is applied after conversion. Mask geometry/channel validation
  and optional inversion are handled by the shared framework.
- Sequence Apply processes every decoded frame as Apply processes a still image
  of it, writes a temporary sequence, preserves the source unless every frame
  succeeds, and replaces the target only on success.
- History records one step named **Adjust Lightness/Chroma/Hue**, described as
  `Adjust lightness to 10 chroma to 30 and hue to -20` for those values; undo
  restores the previous image. The step does not record gamut-clipped counts,
  the mask, or the sRGB/D65 reading.
- Nothing is stored between openings of the window.

## Performance

Runtime is linear in pixels/frames and runs on the threads the run is granted.
The Preview keeps no cached LCh copy: Preview and Apply each work on a copy of
the target (a Preview area works on a copy of the area); an integer image adds a
floating working copy, and Apply copies the result once more when it restates
the processing-state cards. A run the session's memory budget cannot hold is
refused with the reason and publishes nothing. No production timing or
peak-memory run was performed.

## Worked example

For neutral sRGB `(0.5,0.5,0.5)`, `a*` and `b*` are near zero, so chroma is near
zero and hue is unstable/visually powerless. **Lightness `+10`** adds 10 L*
units and returns a lighter neutral unless gamut/endpoints clip. **Chroma +25**
doubles chroma but has almost no useful effect on an ideal neutral.

For a pixel whose computed hue is `170°`, **Hue +30** turns it to `200°`, the
direction a signed scale calls `-160°`.

## Warnings and limitations

!!! note "Hue wraps around the circle"
    The turned hue wraps modulo `360°`, so a large turn carries a color all the
    way round the circle.

- LCh output is forced into sRGB by per-component clipping; no gamut compression
  or profile conversion is performed.
- The fixed sRGB/D65 interpretation may be wrong for RGB stored in another
  transfer function, primaries, or white point; only a linear RGB description
  changes how the transfer is read.
- CIELAB/LCh is only approximately perceptual and becomes less reliable for
  extreme colors; hue is undefined as chroma approaches zero.
- Every processed pixel is clamped to `0..1`; floating values outside that
  range do not survive Apply.
- The process needs exactly red, green, and blue. A grayscale image or frame,
  or a color image that also carries an alpha channel, is refused at Apply with
  the reason, and nothing changes.

## Troubleshooting

| Symptom | Cause | Fix |
| --- | --- | --- |
| Menu disabled/process closes | Target is not a color image | Debayer/convert intentionally or select RGB |
| Apply disabled | All three values are `0` | Set a nonzero value |
| Chroma clips unexpectedly | Positive mapping is `1+C/25` and output clips to sRGB | Use smaller values; `+5=1.2x` |
| Hue shifts in nearly gray areas look erratic | Hue is ill-defined near `C*=0` | Reduce Hue/Chroma or protect neutrals |
| Error message includes `the LCh adjustment needs an image of red, green and blue` | The target, or a decoded sequence frame, is not three-channel RGB (grayscale, or color with alpha) | Supply RGB frames/valid auto-debayer; the target was not changed |
| Pixels holding NaN are unchanged | Non-finite pixels are left as they were | Repair or replace them upstream if needed |
| Snapshot carries no nonlinear record | Snapshots are Preview results and are not recorded | Apply on a duplicate for an archival result |
| Error names the memory budget | The session cannot hold the run's copies of the image | Close other images or work on a smaller crop; nothing was changed |
| Sequence Cancel leaves the sequence as it was | Cancel stops the run between frames or inside the engine operation, and the target is replaced only when every frame succeeds | Expected; Apply again when ready |

## Related workflow steps

Complete [Photometric Color Calibration](../color-calibration/photometric-cc.md)
or [Spectrophotometric Color Calibration](../color-calibration/spectrophotometric-cc.md)
before finishing. Compare [Adjust HSL](adjust-hsl.md) for the hue, saturation, and
lightness of the stored values, and
[Color Saturation](color-saturation.md) for hue-range-selective saturation. Use
[Histogram Transformation](../stretching/histogram-transformation.md) or
[Exposure Correction](../stretching/exposure-correction.md) for deliberate tone
mapping rather than substituting L*. Verify [Image History](../../interface/image-history.md)
and [Image Statistics](../../interface/image-statistics.md) after Apply.

## Credits

- [ICC sRGB characterization](https://www.color.org/sRGB.pdf) defines the sRGB transfer, primaries, and D65 white used by the engine's conversion constants.
- [CIE Standard Illuminant D65](https://www.cie.co.at/datatable/cie-standard-illuminant-d65) is the daylight reference represented by the engine's XYZ white point.
