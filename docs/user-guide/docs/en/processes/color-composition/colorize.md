---
kind: process
stage: finish
menu: Process > Color Spaces > Colorize
summary: >-
  Turns a grayscale or RGB image into a color image of the hue and saturation you choose.
data_out: nonlinear
---
# Colorize

## Purpose and expected result

Colorize converts one grayscale or RGB target into a same-size RGB image using
a selected Hue, Saturation, and either HSV Value or HSI Intensity. It is useful
for turning a mono intensity plane into a fixed-color layer, making a false-color
version of a mask or support image, or remapping an RGB image with the process's
special channel-wise behavior.

The expected result is an RGB image at the source pixel type: an integer image
stays integer. Apply replaces the target only after the calculation succeeds. A
grayscale target therefore becomes three-channel RGB. The process records the
output nonlinear on its processing-state cards; it is a display/color
construction operation, not spectral calibration or a physical narrowband
mapping.

!!! warning "RGB input has two distinct paths"
    With **Use lightness** checked, Colorize first derives one CIE L* plane and
    maps that shared scalar to RGB. With it unchecked, the process does not
    convert the source RGB pixel to HSV/HSI and replace its Hue and Saturation.
    Instead, it maps source Red into output Red, source Green into output Green,
    and source Blue into output Blue independently. Except at special hues or on
    neutral input, the selected Hue is therefore not the measured hue of the
    resulting RGB pixel.

## At a glance

| Item | Current behavior |
| --- | --- |
| Menu path | **Process > Color Spaces > Colorize** |
| Enabled when | An image or sequence is active, Colorize is not already attached to it, and no attached process holds exclusive access |
| Inputs | One grayscale or RGB still or sequence; an optional active mask must match target geometry; a still CFA mosaic is accepted as one grayscale plane; an image with an alpha channel, or any other channel layout, is refused by name at Apply and nothing changes |
| Outputs | Destructively replaces the target with same-size, same-type RGB; **Take snapshot** creates an auxiliary RGB still; no file is written directly |
| Recommended image state | Finite normalized data on a duplicate or reversible history state; normally use after physical calibration and color calibration because output is recorded nonlinear |
| Preview support | Checked by default; asynchronous full-image linear display with histogram, optional Preview area, original/result switching, display-channel inspection, and snapshot; runs the same operation as Apply, CIE L* conversion included; no Display Mode or Autostretch Settings control |
| Sequence support | Exposed and processed frame by frame through temporary storage; one captured parameter/mask state applies to every frame; Cancel stops the run and replaces nothing |
| Mask support | Yes; the shared post-process blend mixes original and colorized RGB by the active mask and inversion state |
| External dependency | None; HSV, HSI, CIE L*, masking, and sequence support are built into AfterNight |

## Before you start

- Decide whether the source should supply one structural plane or three separate
  stored channels. Use a mono target, or enable **Use lightness** on RGB, when a
  fixed Hue/Saturation should describe each output pixel in the selected model.
- Duplicate important data or verify Image History. Apply replaces the target
  and changes grayscale/CFA input into RGB.
- Use finite samples in the normalized range expected by the pixel type. HSI
  clamps finite output to `[0,1]` at any nonzero Saturation; HSV, and HSI at
  Saturation `0`, add no final clamp for float or double data outside that
  range. On an image whose samples are all valid, a pixel holding NaN or an
  infinity is not colorized: an RGB pixel without **Use lightness** is left as
  it was, and otherwise the pixel carries its non-finite intensity in all three
  channels.
- Confirm the active mask and inversion state. Mask alpha is applied after
  colorization, so partial mask values mix the original and colorized RGB.
- The preview is a fixed linear display. Low-valued linear data can look dark,
  and Colorize does not expose the shared preview autostretch controls. Use the
  histogram and one consistent external viewing strategy rather than judging
  the process from a fitted dark preview alone.

## Procedure

1. Select a grayscale or RGB target and open **Process > Color Spaces > Colorize**.
   Confirm **Target Image**, the active mask, and whether the target is a still
   or sequence.
2. For a mono target, leave **Use lightness** unavailable. For RGB, enable it if
   you want one CIE L*-derived structure plane; leave it off only when you intend
   the channel-wise mapping described below.
3. Choose **HSV** for Value-preserving fixed-color mapping or **HSI** for an
   intensity-based mapping that can clip bright saturated colors.
4. Set Hue in degrees. Start with Saturation below `1` when highlight clipping
   or strongly unequal channels would be distracting. Hue `360` is identical to
   `0`.
5. Keep Preview checked, compare with **Switch image**, inspect output channels,
   and use **Preview area** for a representative local check. Then uncheck the
   area and inspect the full frame before Apply.
6. Select **Apply**. For a still, there is no progress dialog or Cancel action.
   For a sequence, use the shared progress dialog; Cancel stops the run and the
   original sequence is kept.

## Complete parameter and action reference

| UI label/action | Type and unit | Runtime default | Persisted-default exception | Valid values, step, or constraints | Effect of increasing, decreasing, or selecting | Dependencies and enable/visibility rules | Quality, artifact, runtime, memory, or disk trade-off | Scope: preview/apply, channel, mask, still/sequence | Persistence |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Histogram plot | Graph, normalized intensity/count | Current colorized preview; RGB overlaid | None | Binning comes from **Plot resolution** | Shows the mask-aware preview distribution and approximate endpoint-bin clipping | Present while Preview pane exists | Recalculation adds work; endpoint counts depend on bins | Display only; RGB preview | Recomputed with preview/resolution |
| Left-drag in histogram | Graph gesture | No readout | None | Hold left button and move | Shows normalized coordinate readout | Histogram present | Diagnostic only | Display only | Ends on release |
| Middle-drag in histogram | Graph gesture | Fitted/unpanned | None | Hold middle button and drag | Pans the graph | Useful after zoom | Display cost only | Display only | Window lifetime |
| Ctrl+mouse wheel in histogram | Graph gesture | Fitted size | None | Up `1.20x`; down `0.8x`; wheel without Ctrl scrolls | Zooms around cursor | Interactive graph | Larger graph needs more rendering area | Display only | Window lifetime |
| Zoom in | Histogram-toolbar action | Fitted size | None | Each action `1.30x` | Enlarges plot | Histogram present | Plot rendering only | Display only | Window lifetime |
| Zoom out | Histogram-toolbar action | Fitted size | None | Each action `0.70x` | Shrinks plot | Histogram present | Plot rendering only | Display only | Window lifetime |
| Zoom reset | Histogram-toolbar action | — | None | One action | Fits plot and resets scrolling | Histogram present | Plot rendering only | Display only | Immediate |
| Show grid | Toggle | Checked | None | Checked/unchecked | Shows/hides graph grid | Histogram present | Display-only aid | Display only | Window lifetime; Reset unchanged |
| Show clipping info | Toggle | Checked | None | Checked/unchecked | Shows/hides endpoint-bin counts and percentages | Histogram present | Bin-based diagnostic, not exact float clipping proof | Display only | Window lifetime |
| Absolute scales | Toggle | Unchecked | None | Checked/unchecked | Normalizes each visible histogram channel to its own peak | Histogram present | Can conceal relative RGB strength; pixels unchanged | Display only | Window lifetime |
| Draw histogram areas | Toggle | Unchecked | None | Checked/unchecked | Chooses filled-area or line rendering | Histogram present | Filled areas can obscure overlap | Display only | Window lifetime |
| Show histogram | Toggle | Checked | None | Checked/unchecked | Shows/hides histogram curves | Histogram present | Display-only | Display only | Window lifetime |
| Plot resolution | Combo box, bins | **8-bit (256)** | None | **6-bit (64)**, **7-bit (128)**, **8-bit (256)**, **9-bit (512)**, **10-bit (1024)**, **12-bit (4096)**, **14-bit (16K)**, **16-bit (64K)**, **100 levels**, **1000 levels**, **10000 levels** | More bins show finer distribution | Histogram present | Higher values add histogram memory/rendering; output precision unchanged | Display only | Window lifetime |
| Method | Combo box | **HSV** | None | **HSV**, **HSI** | Chooses Value-based sector mapping or mean-Intensity trigonometric mapping | Remains editable during Apply, but the run uses the captured choice | HSI can clip bright saturated values; HSV preserves the scalar as maximum for a shared mono plane | Preview/Apply; all output channels; still/sequence | Reset and success return **HSV** |
| Hue | Double value plus slider, degrees | `0.00` | None | `0.00..360.00`; spin step `1.00`; two decimals; slider has 1,000 positions (`0.36` degrees nominal) | Rotates fixed model hue through red, yellow, green, cyan, blue, and magenta; `360=0` | Disabled during Apply | Hue controls channel ratios; on RGB without Use lightness, unequal source channels can shift measured output hue | Preview/Apply; still/sequence | Reset and success return `0.00` |
| Saturation | Double value plus slider, normalized | `1.00` | None | `0.00..1.00`; spin step `0.01`; two decimals; slider nominal resolution `0.001` but spin rounds to two decimals | `0` is achromatic; `1` is maximum requested model saturation | Disabled during Apply | Higher values increase channel separation and HSI highlight clipping; lower values approach gray/identity cases | Preview/Apply; still/sequence | Reset and success return `1.00` |
| Use lightness | Checkbox | Unchecked; disabled on grayscale and on an image with alpha | None | Checked/unchecked | Checked converts RGB to one normalized CIE L* plane before HSV/HSI mapping | Enabled only while the current target image is red, green, and blue without alpha; switching to mono, or to an image with alpha, disables and unchecks it; remains editable during Apply but the current run is captured | Better structural consistency than three-channel mapping, but it does not guarantee the output's measured CIE L* equals the input L*; Preview and Apply use the same exact CIE conversion | Preview/Apply; RGB input only; still/sequence | Reset/success do not clear it while RGB remains selected; window lifetime |
| Target Image | Image/sequence selector | Attached active target | None | Any open image/sequence accepted by the generic launch guards; Apply processes grayscale or RGB and refuses an image with alpha by name | Changes source, mask, preview, and Apply destination; updates Use lightness availability | Disabled during Apply | Wrong target is destructively replaced | Preview/Apply; still/sequence | Target lifecycle; Reset unchanged |
| Preview | Checkbox | Checked | None | Checked/unchecked | Creates/destroys asynchronous full-image or ROI preview | Remains live during execution | Full image adds RGB output, histogram, and display buffers; disabling it removes visual verification | Preview only; current displayed sequence frame | Window lifetime; Reset unchanged |
| Preview pane | Fixed-linear image viewer | Colorized, mask-aware result | No target autostretch inheritance | Pan plus toolbar actions below | Shows computed pixels without modifying target | Exists only while Preview is checked; no Display Mode/Autostretch Settings controls | Linear display can hide faint data; buffers add memory | Preview only; current still/frame | Preview lifetime |
| Fit To Window | Preview-toolbar action | Fitted on first display | None | Click or **Ctrl+Shift+F** | Fits whole image | Preview image required | Overview resampling hides pixel-scale differences | Display only | Viewer lifetime |
| Zoom In | Preview-toolbar action | Current scale | None | Click; `1.25x` relative zoom | Enlarges view | Preview image required | More local detail, more panning | Display only | Viewer lifetime |
| Zoom Out | Preview-toolbar action | Current scale | None | Click; `0.8x` relative zoom | Reduces view | Preview image required | More context, less fine detail | Display only | Viewer lifetime |
| Zoom 100% | Preview-toolbar action | Current scale | None | Click; absolute `1.0` scale | Shows one viewer pixel per image pixel | Preview image required | Preferred for clipping/banding inspection | Display only | Viewer lifetime |
| View Channel | Preview-toolbar combo box | **RGB** | None | **RGB**, **Red**, **Green**, **Blue**, **CIE L* (Lightness)**, **CIE a***, **CIE b***, **CIE a*=R / b*=G (Chrominance)**, **CIE c***, **CIE h***, **CIE X**, **CIE Y (Luminance)**, **CIE Z**, **HSL Hue**, **HSL Saturation**, **HSL Lightness**, **HSV Hue**, **HSV Saturation**, **HSV Value**, **HSI Hue**, **HSI Saturation**, **HSI Intensity**, **YCbCr Luma**, **YCbCr Blue Chroma**, **YCbCr Red Chroma** | Selects only the displayed stored/derived component | Colorize preview is always RGB | Helps verify whether chosen model and RGB path produced expected components; conversion is display-only | Preview display | Viewer lifetime |
| Switch image | Preview-toolbar action | Processed preview shown | None | Click; **Space**; right-click; **Ctrl+Shift+O** | Toggles colorized result/original | Preview required | Comparison only; no recomputation | Preview display; still/current frame | Preview lifetime |
| Preview area | Preview-toolbar checkbox and movable ROI | Unchecked; first enable creates centered area up to `600 x 600` | None | Checked/unchecked; drag/resize within bounds | Processes selected crop and pastes it over original full preview | Disabled during Apply; full Apply always uses full target | Reduces tuning work; outside ROI is deliberately original and can hide global channel behavior | Preview/Take snapshot; mask cropped consistently | Preview lifetime; Reset unchanged |
| Take snapshot | Preview-toolbar action | No snapshot | None | Click or **Ctrl+Shift+K** | Re-executes and opens `<target> - Colorize - SNAPSHOT` | Disabled during Apply | Allocates an RGB still; current ROI/mask honored; target unchanged; the snapshot is not recorded nonlinear | New still; no sequence snapshot | Each action creates a view |
| Apply | Button/action | Enabled | None | One action | Captures Method/Hue/Saturation/Use lightness and processes full target in background | Target must lock; repeat clicks ignored while running. During every Apply, with or without Preview, Target, Hue, Saturation, Apply, Reset, Close, Preview area, and Take snapshot disable; Method, Use lightness, Preview, and histogram remain live | Still run has no progress/Cancel; sequence writes temporary output; RGB allocation and optional CIE conversion add cost | Destructive still/sequence replacement after success; active mask post-blend | Records incomplete history text, then resets Method/Hue/Saturation |
| Reset | Button/action | Enabled while idle | None | One action | Restores Method **HSV**, Hue `0`, Saturation `1`; schedules preview | Does not clear Use lightness, Target, Preview, mask, histogram, or Preview area; disabled during Apply | Recomputes default red mapping; does not undo applied pixels | Configuration only | Immediate; also follows success |
| Close | Button/action | Enabled while idle | None | One action | Closes without applying pending settings | Disabled during Apply; closing the window another way while a run is in flight hides it, and it closes once the run has landed | No output itself; nothing waits on the GUI thread | Window | Values not saved after destruction |
| Sequence progress / Cancel | Shared frame/task progress and Cancel | Created for sequence Apply | None | Cancel stops the run; the engine operation on the current frame observes the stop | Sequence target only | One captured parameter and mask state; optional configured auto-debayer happens before each frame | Temporary sequence disk plus RGB expansion; Cancel replaces nothing | Full sequence; success replaces sequence and records one step | Temporary until success; then target sequence owns result |

## How the algorithm works

### 1. Choose the scalar source

Every stored sample is read in normalized units: unsigned integers divided by
their largest value, signed integers counted from their smallest value to their
largest, float and double samples as stored. Integer samples are held in a
Float32 working image (Float64 for 32-bit integers), float and double samples
are used where they are, and each pixel's arithmetic is double. The result is
narrowed back to the source's pixel type at the nearest code.

For grayscale input, the normalized sample `x` is the shared HSV Value or HSI
Intensity. For RGB with **Use lightness**, AfterNight reads the image's RGB as
the process framework reads every color image (sRGB primaries and D65 white,
through the sRGB transfer curve unless the image's color space says linear RGB),
converts it to CIE L*, and uses `x=L*/100`. The L* plane is computed in double
but held in Float32 (Float64 only for a 64-bit float image), so a 32-bit integer
image's lightness carries Float32 precision. Preview and Apply use the same
exact conversion.

For RGB with **Use lightness** off, there is no RGB-to-HSV/HSI analysis. If
`F_R`, `F_G`, and `F_B` are the output components of the selected fixed-Hue
mapping, the implementation computes

\[
R'=F_R(H,S,R),\qquad
G'=F_G(H,S,G),\qquad
B'=F_B(H,S,B).
\]

Each output component therefore receives a different source scalar. At
Saturation `0`, both methods reduce to `R'=R`, `G'=G`, `B'=B`. At nonzero
Saturation, this path is not equivalent to preserving source Value/Intensity
while replacing Hue and Saturation.

### 2. HSV mapping

Hue `H_d` in degrees becomes `H=H_d/360`; any hue is wrapped modulo 360 degrees,
so the UI endpoint `360` is `0`. For a shared scalar Value `V=x`, define

\[
C=VS,\qquad
X=C\left(1-\left|\left((6H)\bmod 2\right)-1\right|\right),\qquad
m=V-C.
\]

One of `(C,X,0)`, `(X,C,0)`, `(0,C,X)`, `(0,X,C)`, `(X,0,C)`, or `(C,0,X)`
is selected by the 60-degree Hue sector, then `m` is added to all channels. For
finite `V` in `[0,1]` and UI Saturation in `[0,1]`, output remains in gamut and
the maximum channel equals `V`. The mapping does not clamp afterward, so
out-of-range float/double input gives out-of-range output; integer output is
held to its type's range when it is narrowed.

### 3. HSI mapping

HSI uses `I=x` as arithmetic mean before clipping. In the first 120-degree
sector, with Hue expressed in degrees,

\[
B=I(1-S),\qquad
R=I\left(1+\frac{S\cos H}{\cos(60^\circ-H)+10^{-12}}\right),\qquad
G=3I-(R+B).
\]

The next two sectors rotate the roles of R, G, and B after subtracting 120 or
240 degrees. Finally each component is clamped independently to `[0,1]`; at
Saturation `0` HSI returns `(I,I,I)` without that clamp, so float or double
input outside `[0,1]` stays outside it. Before that clamp, `(R+G+B)/3=I`;
clipping bright saturated colors breaks that equality and can flatten one or
more channels.

### 4. Output, mask, and state

The result is a new three-channel RGB image with the source width, height, and
pixel type, described with no color filter array and carrying the source's
metadata (FITS cards, astrometric solution, acquisition record) and validity
mask. An image with an alpha channel, or any channel layout other than
grayscale or RGB, is refused before any work with
`the colorization needs a grey image or an image of red, green and blue`; the
run publishes nothing. Colorization runs on the engine's worker threads. A pixel
holding NaN or an infinity in a channel the mapping reads (with
**Use lightness**, a pixel whose CIE L* is not finite) is not colorized on an
image whose samples are all valid, and always with **Use lightness**; without
it, on a floating image whose validity is exact-zero-is-no-data or an explicit
mask, such a pixel in a valid position stops the Apply with an error. An RGB
image colorized channel by channel leaves an uncolorized pixel as it was; a
grayscale image, or a colour image read through its lightness, starts each of
the result's three channels as the intensity, so the pixel keeps its non-finite
intensity in all three.

The result's processing-state cards are then restated: `ANLIN` nonlinear,
`ANASRC` process, `ANOP` `colorize`, and a stretched channel basis. A source
recorded as linear, camera, or display RGB, recorded as `raw-cfa`, or with no
recorded basis becomes `display-rgb`; a source recorded as `monochrome` keeps
that basis even though the output has three colored channels, which is a
metadata limitation. A linear encoding the source claimed is no longer claimed,
and the result's lineage gains an `an::colorizeImage` entry naming the hue,
saturation, model, and whether CIE L* was used. Other source cards are kept; a
FITS file saved from the result carries no `BAYERPAT` card, because the FITS
writer states that card from the image's description.

If an active mask is enabled, the shared framework blends a grayscale original
against every output channel when necessary and applies, for each output
component,

\[
O=(1-M)A+MB,
\]

where `A` is original, `B` is colorized, and `M` is mask alpha. Inversion swaps
the two weights. A one-channel mask weighs every channel and a three-channel
mask weighs each channel by its own; a mask of another channel count, width, or
height makes the run fail. The entire image still carries the global nonlinear
`colorize` state even where mask alpha is zero.

## Parameter interactions and starting points

| Combination | Useful starting point | Too weak / expected identity | Too strong / failure symptom | Correction |
| --- | --- | --- | --- | --- |
| HSV + mono/shared lightness | Saturation `0.4..0.8`; chosen line Hue | Near `0` approaches neutral gray; mono still becomes RGB | High saturation gives one dominant channel and can expose integer banding | Lower Saturation; inspect at 100% |
| HSI + bright mono/shared lightness | Saturation below `0.5` first | `0` is neutral gray at intensity `I` | Bright values clip one/two components, so mean intensity and detail are not preserved | Reduce Saturation or choose HSV |
| RGB + Use lightness off | Use only when channel-wise scaling is intentional | Saturation `0` is the original RGB pixel | Selected Hue does not match measured output Hue on unequal channels | Enable Use lightness or convert a chosen mono plane first |
| Use lightness + image color space | Compare full frame and histogram | Neutral input can look similar either way | The same samples give a different CIE L* when the image is recorded as linear RGB than when it is read as sRGB-encoded | Check the image's color space before judging the structure |
| Mask + saturated mapping | Soft finite mask, verify inversion | Alpha `0` leaves original samples | Hard edges or channel masks can create colored seams | Feather/repair mask before Colorize |

These are conservative source-derived starting points, not measured quality
recommendations for a particular astronomical dataset.

## Inputs, outputs, and metadata

- Successful Apply replaces the selected still or sequence with an RGB result.
  Width, height, and source pixel type are retained; grayscale input expands to
  three channels.
- Source metadata is carried onto the RGB result, which is described with no
  color filter array. The processing-state cards become nonlinear with
  `colorize` provenance; the state-dependent channel-basis behavior is described
  above. The lineage entry names the hue, saturation, model, and whether CIE L*
  was used.
- Still Image History records `Colorize hue to <value> and saturation to
  <value>`, for example `Colorize hue to 200 and saturation to 0.6`. It does not
  record Method or Use lightness, so the history text alone cannot reproduce the
  result. Undo restores the previous image.
- **Take snapshot** creates an RGB still from current preview/ROI/mask settings
  and leaves the target unchanged. Linear preview display settings are not baked
  into its pixels, and the snapshot is not recorded nonlinear.
- Sequence Apply captures one Method/Hue/Saturation/Use lightness state, applies
  it frame by frame to a temporary sequence, then replaces the original only on
  success. Configured CFA auto-debayering can turn frames into RGB before this
  process; cancellation or an error leaves the original sequence in place.
- Reset after success restores HSV, Hue `0`, and Saturation `1`, but does not
  clear Use lightness on an RGB target. Nothing is stored between openings of
  the window.

## Performance

Both mappings are `O(W x H)` with constant work per pixel and run on the
engine's worker threads. HSV uses arithmetic and a sector switch. HSI adds
trigonometric operations and three clamps. **Use lightness** adds a complete
RGB-to-XYZ-to-CIE-L* pass and a temporary floating lightness plane.

The destination always allocates three source-type channels; an integer image
also needs a three-channel floating working image, and an Apply copies the
result once more to restate its cards. Preview also keeps source/result
presentation buffers and histogram data; masks add blend inputs. Sequence mode
writes a temporary RGB sequence before replacement, so disk and codec cost can
dominate and can approach three times the uncompressed sample storage of a mono
source. No approved astronomical workload was timed, and peak memory was not
measured.

## Worked example

The values below reproduce the current formulas for a normalized scalar
`x=0.40`. They were calculated from source and were not executed on approved
astronomical data.

| Settings | Output RGB | What it demonstrates |
| --- | --- | --- |
| HSV, Hue `0`, Saturation `0.5`, Value `0.4` | `(0.40, 0.20, 0.20)` | HSV keeps the maximum channel equal to the source scalar |
| HSI, Hue `0`, Saturation `0.5`, Intensity `0.4` | `(0.80, 0.20, 0.20)` | Unclipped channel mean remains `0.40` |
| HSI, Hue `0`, Saturation `1.0`, Intensity `0.5` | raw `(1.50, 0, 0)`, stored `(1.00, 0, 0)` | HSI gamut clamp loses the requested mean intensity |
| Either method, Saturation `0`, shared scalar `0.4` | `(0.40, 0.40, 0.40)` | Zero saturation is achromatic |

For the RGB no-lightness path, take source `(0.20,0.40,0.60)`, HSV Hue `60`,
and Saturation `1`. A conventional fixed-HSV mapping would use one shared Value.
Colorize instead keeps the red component from a Value-`0.20` mapping and the
green component from a Value-`0.40` mapping, producing `(0.20,0.40,0)`. Its
measured HSV hue is not the requested 60 degrees. Enable **Use lightness** when
that channel-wise behavior is not intended.

## Warnings and limitations

- **Use lightness** selects a CIE L*-derived source plane; it does not guarantee
  output CIE L* preservation. HSV treats that scalar as maximum channel; HSI
  treats it as pre-clamp arithmetic mean.
- RGB without Use lightness is a channel-wise construction, not ordinary
  RGB-to-HSV/HSI editing. The selected Hue need not be the output Hue.
- At nonzero Saturation HSI clips finite components independently to `[0,1]`;
  high Intensity and Saturation can lose highlight ratios. HSV, and HSI at
  Saturation `0`, lack a final clamp for out-of-range float/double input. On an
  image whose samples are all valid, a pixel holding NaN or an infinity is not
  colorized and keeps its non-finite value.
- An image with an alpha channel is refused by name at Apply, and
  **Use lightness** is not offered for it; remove the alpha channel first.
- The preview is fixed Linear and has no autostretch controls. Histogram/viewer
  tools change only display, never Colorize samples.
- A mono/CFA source becomes RGB described with no color filter array. An
  existing `monochrome` channel basis remains `monochrome`; an existing
  `raw-cfa` basis becomes `display-rgb`.
- Image History omits Method and Use lightness; the result's lineage records
  them.
- Still Apply offers no progress or Cancel facility. Sequence Cancel stops the
  run and keeps the original sequence.
- Method, Use lightness, and Preview stay editable during Apply. The run uses
  the values captured when Apply was selected. Every Apply, with or without
  Preview, locks Target, Hue, Saturation, Apply, Reset, and Close.

## Troubleshooting

| Symptom | Likely cause | Check or correction | Evidence |
| --- | --- | --- | --- |
| Colorize is not under Process > Color | The current menu is named **Color Spaces** | Use **Process > Color Spaces > Colorize** | `AfterNightAppUiBuilder.h`; `ProcessRegistryBuiltins.cpp` |
| Selected Hue is not the measured RGB hue | RGB target is using channel-wise no-lightness path | Enable Use lightness or provide a mono structural plane | `an::colorizeImage` three-channel path |
| Bright HSI result clips or loses structure | Sector formula exceeded `[0,1]` and final clamp flattened channels | Reduce Saturation or use HSV | `an::colorizeImage` with `ColorizationModel::Hsi` |
| HSV float result contains out-of-range samples | Float/double source was outside the normalized range; HSV adds no final clamp | Sanitize/rescale a duplicate before Colorize | `an::colorizeImage` with `ColorizationModel::Hsv` |
| NaN or infinite pixels remain in the result | A non-finite pixel is not colorized: it is left as it was, or carries its intensity into all three channels | Repair non-finite samples on a duplicate before Colorize | `ColorizeProcess::executeTypedProcessImpl`; `process_utils::skippingNonFinite` |
| Use lightness is disabled | Current target image is grayscale or carries an alpha channel | This is expected; grayscale is already the shared scalar source, and an image with alpha is refused at Apply | `ColorizeProcess::onImageUpdated`; `ColorizeProcess::onViewControlChanged` |
| Use lightness result differs between two images with the same samples | One image's color space says linear RGB, so its RGB is read as linear; the other is read as sRGB-encoded | Check the image's color space | `processChannelScopeRequest` in `ProcessExecution.cpp` |
| Preview looks almost black | Colorize preview is fixed Linear | Inspect histogram; use an appropriate upstream/display workflow without assuming pixels are zero | No execution autostretch in `ColorizeProcess` |
| Error message includes `the colorization needs a grey image or an image of red, green and blue` | The target, or a decoded sequence frame, has an alpha channel or another channel layout | Remove the alpha channel first, or extract one plane and colorize it; the target was not changed | `ColorizeProcess::executeTypedProcessImpl` |
| Reset did not clear Use lightness | Colorize reset omits that checkbox | Uncheck it manually before the next run | `ColorizeProcess::resetSettings` |
| History cannot reproduce the result | Method and Use lightness are omitted from step description | Read them from the result's lineage, or record them separately | `ColorizeProcess::describeTypedProcessStep`; `process_utils::recordNonlinearResult` |
| Method, Use lightness, or Preview stay enabled during Apply | They are not part of the window's control locking | The run uses the captured values; change them after it finishes | `ColorizeProcess::enableProcessControlsImpl` |

## References and provenance

- Alvy Ray Smith, [“Color Gamut Transform Pairs”](https://alvyray.com/Papers/CG/color78.pdf),
  SIGGRAPH 1978, describes the hexcone HSV transform family. AfterNight's
  `an::colorizeImage` sector code is authoritative for the exact implementation.
- International Color Consortium, [How to interpret the sRGB color space](https://www.color.org/sRGB.pdf),
  documents the sRGB transfer and D65 RGB/XYZ basis used by AfterNight's CIE L*
  conversion for an image not recorded as linear RGB. AfterNight does not
  inspect or apply an embedded ICC profile here.
- `ColorAdjustment.h` (`an::colorizeImage`), `ChannelScope.h`,
  `PointwiseProcessUtils.cpp`, and `ColorizeProcess.cpp` are the authoritative
  sources for the HSV and HSI equations, clamp policy, RGB channel-wise mapping,
  CIE L* reading, sample units, metadata record, and UI execution behavior.

## Related workflow steps

- [NB Color Mapper](nb-color-mapper.md) provides multi-layer HSV colorization,
  modifiers, background neutralization, and screen mixing for narrowband work.
- [Narrowband Palette Composer](narrowband-palette-composer.md) uses fixed
  palette matrices rather than a single Hue/Saturation mapping.
- [Adjust HSL](../color-adjustment/adjust-hsl.md) modifies an existing RGB
  image in HSL instead of constructing RGB from fixed HSV/HSI settings.
- [Masks](../../interface/masks.md), [Process Windows](../../interface/process-windows.md),
  and [Image History](../../interface/image-history.md) explain shared blending,
  preview, Apply, and verification behavior.
