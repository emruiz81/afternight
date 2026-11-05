# CLAHE

## Purpose and expected result

CLAHE means contrast-limited adaptive histogram equalization. It divides the
selected grayscale data into a square grid of tiles, builds a clipped local
histogram mapping for every tile, and bilinearly interpolates neighboring
mappings at every pixel. The clip limit restrains—but does not eliminate—noise
amplification and extreme local contrast.

The expected result is a same-size still or replacement sequence with stronger
local tonal separation. With the default **RGB** process Channel, AfterNight
converts the red, green, and blue channels to CIE Lab, equalizes only L*, and
converts back; a* and b* and any alpha channel are left untouched. **Amount**
then crossfades the complete CLAHE result with the source. An Apply records the
result as nonlinear.

## At a glance

| Item | Current behavior |
| --- | --- |
| Menu path | **Process > Intensity Transformations > CLA Histogram Equalization**; the process window is titled **CLAHE** |
| Enabled when | An image or sequence is active, the same process is not already attached to it, and no attached process holds exclusive access |
| Inputs | One grayscale or RGB still or sequence; optional active mask with matching geometry; a CFA still is treated as one grayscale mosaic plane, while a CFA sequence is auto-debayered to RGB first when its view has sequence auto-debayer enabled |
| Outputs | Destructively replaces the still or, after successful temporary processing, the sequence; **Take snapshot** creates an auxiliary still; no file is written directly |
| Recommended image state | Usually a finite, normalized nonlinear working copy after noise reduction and global stretch; linear data can suffer severe background/noise remapping |
| Preview support | Asynchronous movable-area preview, original/result switching, snapshot, display-channel selection, and a separate source/result histogram; no execution autostretch |
| Sequence support | Yes; numeric parameters and process Channel are captured once when Apply starts, CFA frames can be auto-debayered first, and Cancel stops the current frame's equalization |
| Mask support | Yes; the active mask blends the Amount-mixed result with the original after CLAHE |
| External dependency | None; the AfterNight engine's own adaptive histogram equalization; no OpenCV call, network access, or optional plugin |
| Processing state | Apply records the result as nonlinear under the operation `clahe`; a preview records nothing |

## Before you start

- Complete calibration, registration, integration, gradient correction, color
  calibration, global stretching, and appropriate noise reduction first.
- Debayer CFA stills before color work. For CFA sequences, confirm the view's
  sequence auto-debayer setting: enabled frames enter as RGB; disabled frames
  remain a mosaic plane whose sensor pattern can be reinforced by tile
  equalization.
- Duplicate the target or confirm Image History is available. Apply replaces
  the target only after the full operation succeeds.
- Include smooth background, faint structure, small stars, a bright edge, and
  more than one tile boundary in the Preview area. Recheck the full frame.
- Start with the default `8 × 8` tile grid and `2.0` Contrast Limit. Grid Size
  is a number of tiles per axis, not a tile width in pixels.
- Prefer **RGB** or a deliberate luminance/lightness Channel. Equalizing hue or
  chroma components can produce discontinuities and out-of-gamut color.

## Procedure

1. Select the target and open **Process > Intensity Transformations > CLA
   Histogram Equalization**. Confirm **Target Image** and process **Channel**.
2. Keep **Preview** selected and begin with Grid Size `8`, Contrast Limit `2`,
   and Amount `1`.
3. At **Zoom 100%**, lower Contrast Limit until background grain and star rims
   are controlled. The value is normalized by tile population and histogram
   depth; it is not an absolute count.
4. Adjust Grid Size for the structures of interest. A larger number makes
   smaller tiles and more local mappings; a smaller number makes larger tiles.
5. Inspect tile-scale mottling, seams, halos, clipping, and color changes. Use
   **Switch image** under identical display conditions.
6. Reduce Amount for a final source/result crossfade. Confirm mask and
   inversion, disable Preview area, check the full frame, and then Apply.

## Complete parameter and action reference

| UI label/action | Type and unit | Runtime default | Persisted-default exception | Valid values, step, or constraints | Effect of increasing, decreasing, or selecting | Dependencies and enable/visibility rules | Quality, artifact, runtime, memory, or disk trade-off | Scope: preview/apply, channel, mask, still/sequence | Persistence |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Grid Size | Integer value plus linear slider, tiles per horizontal and vertical axis | `8` | None | `1..256`, step `1`; the engine divides the frame into `N × N` tiles; a grid with more tiles than the image has pixels along an axis is refused | Increasing N creates more, smaller tiles; decreasing N creates fewer, larger tiles; `1` approaches one globally equalized tile with clipping | Enabled while idle; disabled during Apply, and the running job uses its captured value | More tiles increase LUT count and can expose tile-scale mottling/seams; tiny tiles have sparse histograms; fewer tiles can create broad halos and miss small structure | Preview/Apply; selected process Channel; before Amount and mask; still/sequence | Captured per execution; Reset and successful Apply restore `8` |
| Contrast Limit | Double value plus linear slider, normalized clip limit | `2.00` | None | `1.00..64.00`; spin step `0.50` with adaptive stepping; slider maps `10..640` to tenths | Raises or lowers the per-bin cap after normalization by tile pixels and histogram bins; higher values permit stronger mappings | Captured for execution; its absolute bin cap also depends on Grid Size and the histogram depth (256 bins for an 8-bit image, 65,536 otherwise) | Higher values amplify noise, halos, and clipping; lower values restrain enhancement but redistributed counts still change the CDF | Preview/Apply; selected Channel; before interpolation, Amount, and mask | Captured per execution; Reset/success restore `2` |
| Amount | Double value plus linear slider, final normalized blend | `1.00` | None | `0.00..1.00`; spin step `0.05` with adaptive stepping; slider maps `0..1000` to thousandths | Crossfades source and complete CLAHE result; `0` is source pixels and `1` is full result | Independent of Grid Size/Contrast Limit; captured for execution | Reducing it controls all artifacts but does not reduce CLAHE computation or temporary buffers | Preview/Apply; reconstructed selected Channel; before mask; still/sequence | Captured per execution; Reset/success restore `1` |
| Histogram plot | Interactive source/result histogram | Source initially, preview result after execution | None | Mouse wheel/drag and toolbar actions below | Displays distributions and clipping diagnostics; never changes samples | Updated from target/preview; separate from preview display stretch | A histogram cannot reveal tile seams, mottling, or halos | Display only | Window lifetime |
| Zoom in | Histogram-toolbar action | Current histogram range | None | Click | Narrows displayed histogram range | Histogram shown | Diagnostic only | Histogram display only | Window lifetime |
| Zoom out | Histogram-toolbar action | Current histogram range | None | Click | Widens displayed histogram range | Histogram shown | Diagnostic only | Histogram display only | Window lifetime |
| Zoom reset | Histogram-toolbar action | Full histogram range | None | Click | Restores full plot range | Histogram shown | Diagnostic only | Histogram display only | Window lifetime |
| Show grid | Checkable histogram action | Checked | None | Checked/unchecked | Shows or hides plot grid | Histogram shown | Display only; unrelated to CLAHE tiles | Histogram display only | Window lifetime |
| Show clipping info | Checkable histogram action | Checked | None | Checked/unchecked | Shows/hides endpoint clipping readouts | Histogram shown | Binned diagnostic, not the CLAHE histogram clip cap | Histogram display only | Window lifetime |
| Absolute scales | Checkable histogram action | Unchecked | None | Checked/unchecked | Uses common absolute vertical scaling instead of per-channel normalization | Histogram shown | Can hide a low-population channel | Histogram display only | Window lifetime |
| Draw histogram areas | Checkable histogram action | Unchecked | None | Checked/unchecked | Fills areas under plotted channels | Histogram shown | Overlap can obscure channel detail | Histogram display only | Window lifetime |
| Show histogram | Checkable histogram action | Checked | None | Checked/unchecked | Hides/clears graph and toolbar, or reloads source/preview | Always available while idle | Hiding saves graph work, not CLAHE work | Display only | Window lifetime |
| Plot resolution | Combo box, histogram bins | **8-bit (256)** | None | **6-bit (64)**, **7-bit (128)**, **8-bit (256)**, **9-bit (512)**, **10-bit (1024)**, **12-bit (4096)**, **14-bit (16K)**, **16-bit (64K)**, **100 levels**, **1000 levels**, **10000 levels** | Changes diagnostic binning only | Disabled when histogram hidden | More bins cost graph memory/rendering; unrelated to the 256/65536-bin processing histogram | Histogram display only | Window lifetime |
| Target Image | Image selector | Attached/active target | None | Open eligible image or sequence | Changes source, Channel list, histogram, Preview, mask, and Apply destination | Active target required; duplicate/exclusive gates apply; disabled during Apply | A wrong selection before Apply replaces the wrong target | Preview/Apply source and active mask | Target lifetime; Reset unchanged |
| Channel | Combo box | **RGB** for RGB; disabled **Grayscale** for mono | None | **RGB**, **Red**, **Green**, **Blue**, **CIE L* (Lightness)**, **CIE a***, **CIE b***, **CIE c***, **CIE h***, **CIE X**, **CIE Y (Luminance)**, **CIE Z**, **HSL Hue**, **HSL Saturation**, **HSL Lightness**, **HSV Hue**, **HSV Saturation**, **HSV Value**, **YCbCr Luma**, **YCbCr Blue Chroma**, **YCbCr Red Chroma** | Default RGB equalizes CIE L* of the red, green, and blue channels and leaves a*, b*, and alpha untouched; another component is extracted as a scalar, equalized over its full range, and reconstructed | RGB target required for components; disabled during Apply; the selection is captured when Apply starts | RGB usually limits chroma shifts but Lab round trips can clip gamut; hue/chroma equalization can be destructive | Preview/Apply; mask post-blend; still/sequence | Dialog-local; Reset/success restore RGB for RGB targets |
| Preview | Checkbox | Checked | None | Checked/unchecked | Enables/disables asynchronous full-image or area execution | Process idle | Runs full conversions, tile mappings, and interpolation after each change | Preview only; mask-aware; still/current frame | Window lifetime; Reset unchanged |
| Preview pane | Image viewer | Full-frame filtered result in Linear mode | Execution autostretch not enabled | Pan/scroll and actions below | Shows mask-aware result without changing target | Exists only with Preview; Preview area starts unchecked | Holds source/result/display and conversion/tile-table buffers | Preview only | Window lifetime |
| Fit To Window | Preview-toolbar action | Preview fitted initially | None | Click or **Ctrl+Shift+F** | Fits whole image | Preview image required | Broad context; resampling can hide artifacts | Preview display only | Viewer lifetime |
| Zoom In | Preview-toolbar action | Current scale | None | Click; `1.25×` relative zoom | Enlarges around viewport center | Preview image required | Improves local inspection | Preview display only | Viewer lifetime |
| Zoom Out | Preview-toolbar action | Current scale | None | Click; `0.8×` relative zoom | Reduces around viewport center | Preview image required | Shows context but hides fine artifacts | Preview display only | Viewer lifetime |
| Zoom 100% | Preview-toolbar action | Current scale | None | Click; absolute scale `1.0` | Shows one-pixel viewer scale | Preview image required | Preferred for noise, seams, and color shifts | Preview display only | Viewer lifetime |
| View Channel | Preview-toolbar combo box | **RGB** for RGB; disabled **Grayscale** for mono | None | Stored and derived RGB/CIE/HSL/HSV/HSI/YCbCr display components | Selects displayed component only | Separate from process Channel | Helps diagnose chroma changes without altering pixels | Preview display only | Viewer lifetime |
| Switch image | Preview-toolbar action | Result shown | None | Click; **Space**; right-click; **Ctrl+Shift+O** | Toggles result/original | Preview required | Comparison only | Preview display; still/current frame | Preview-window lifetime |
| Preview area | Preview-toolbar checkbox | Unchecked; enabling it creates a centered `600×600` area when possible | None | Checked/unchecked; move/resize overlay | Processes selected area and composites it over original preview | Preview required | Faster tuning, but a crop can hide grid context and broad halos; Apply is full-frame | Preview and snapshot; mask cropped consistently | Preview-window lifetime |
| Take snapshot | Preview-toolbar action | No snapshot | None | Click or **Ctrl+Shift+K** | Re-executes and opens `<target> - CLAHE - SNAPSHOT` | Preview required | Adds a complete execution and auxiliary still | New still; target unchanged; area/mask honored | Each action creates a new view |
| Apply | Button/action | Enabled | None | One action | Captures three numeric values, requests a processing lock on the current target, processes the full still/every frame, then blends Amount/mask and commits on success | Target, Channel, and process controls lock during Apply | Tile tables, color conversions, Amount/mask, and sequence encoding add CPU/memory/disk cost | Destructive still/sequence replacement after success; the result is recorded nonlinear | History records numeric values and, for a colour image, the selected Channel text; then Reset runs |
| Reset | Button/action | Enabled while idle | None | One action | Restores `8/2/1` and RGB on RGB targets | Leaves Target, Preview, histogram, mask, and preview toolbar unchanged; disabled during Apply | Checked Preview schedules another full execution | Configuration only | Immediate; also after successful Apply |
| Close | Button/action | Enabled while idle | None | One action | Closes without applying current settings | Disabled during Apply | Closing cancels a running preview, which the equalization observes | Window/execution | Settings discarded at destruction |
| Sequence progress / Cancel | Shared process/frame progress and Cancel | Created for sequence Apply | None | Cooperative Cancel between processing opportunities | Configured CFA frames are auto-debayered first; frames are written to a temporary sequence and the target sequence is replaced only after success | Sequence target only | Cancel stops the current frame; captured parameters and Channel apply to every frame; temporary disk/encode cost applies | Full sequence; mask and Amount per frame | Cancel removes temporary output; success records one history step and resets settings |

## How the algorithm works

### Conceptual pipeline

1. The shared Channel dispatcher supplies either the whole image or one
   extracted scalar component.
2. A grayscale image or a single component is equalized in its own stored units
   over its type's full range, with 256 histogram bins for an 8-bit type and
   65,536 for every other type. An RGB image's red, green, and blue - read as
   linear when the image says so and sRGB-encoded otherwise - are converted to
   CIE Lab under that encoding's white point on a single-precision working copy;
   only L* is equalized, over `0..100` with the same bin rule, and Lab is
   converted back to the same encoding. a*, b*, and any alpha channel are not
   filtered.
3. The engine divides the frame into `N × N` tiles as equal as whole pixels
   allow; it does not pad the image.
4. Each tile histogram is clipped. Excess counts are redistributed evenly across
   all bins, and the cumulative count becomes that tile's mapping onto the full
   range.
5. Every output pixel bilinearly interpolates the mappings of the four
   surrounding tiles, with tile centres at the centres of their pixels. A pixel
   beyond the outermost tile centres takes the border tile's own mapping. The
   result returns to the original pixel type, rounded for an integer image.
6. AfterNight reconstructs any selected component, records the result as
   nonlinear on Apply, crossfades by Amount, applies the active mask, and
   commits only a completed still or temporary sequence. A derived component's
   reconstruction saturates an integer image at its type's range and keeps a
   floating image's values outside `[0, 1]`.

### Clip limit, CDF, and interpolation

For a tile with `P` admitted pixels, `B` histogram bins, and UI Contrast Limit
`C`, each bin is capped at `C` times the tile's average bin, as a whole count:

`cap = max(floor(C × P / B), 1)`

Counts over `cap` are accumulated and distributed evenly across all `B` bins,
and any remainder is placed at evenly spaced bin indices. A tile that holds no
admitted pixel - possible only when the image's validity rule or mask rejects
all of its samples - maps every value to itself.
If `H(k)` is the redistributed histogram, the tile mapping at bin `k` is
approximately the cumulative fraction `sum(H(0..k)) / P` of the full range.

At a pixel, the engine evaluates the source sample in the mappings of the two
neighbouring horizontal and vertical tiles and takes their bilinear weighted
sum. Its tile centres are the centres of their pixels. This interpolation
reduces hard tile boundaries; it cannot guarantee that aggressive neighboring
mappings will be visually seamless.

### Amount, masks, and state

If `S` is the source, `F` the reconstructed full CLAHE result, and `a` Amount,
the pre-mask output is `B = (1-a)S + aF`. Thus Amount `0` is an exact pixel
identity after the full computation, while `1` uses the complete result. The
active mask then blends `B` with `S`; inversion reverses mask influence.

An Apply records the result as nonlinear in the processing-state cards
(`ANLIN` false, `ANOP` `clahe`), keeping every other card and adding a lineage
entry; a preview records nothing. Image History receives the CLAHE step
description.

## Parameter interactions and starting points

| Goal | Grid Size | Contrast Limit | Amount | What to inspect |
| --- | ---: | ---: | ---: | --- |
| Conservative first pass | `8` | `1..2` | `0.25..0.6` | Background grain, faint gradients, small-star rims |
| Broader structures | `2..6` | `1..3` | `0.25..0.7` | Wide halos and uneven background |
| Smaller structures | `12..24` | `1..2` | `0.15..0.5` | Tile mottling, seams, noise, star cores |
| Diagnose clipping only | Keep chosen grid | Compare `1`, `2`, then higher | `1` temporarily | Histogram endpoints and spatial artifacts; reduce Amount afterward |

These are starting ranges, not validated presets. Effective clipping changes
with tile population and working depth. Use identical display conditions and a
representative crop before deciding.

## Inputs, outputs, and metadata

- Input/output geometry and pixel type are preserved. Every type is equalized
  over its own full range in single precision, with 256 bins for an 8-bit type
  and 65,536 otherwise, and rounded back for an integer image. The process
  expects finite image data.
- Default RGB equalizes CIE L* alone, not R, G, and B independently; a* and b*
  keep their values and alpha is untouched.
  Selecting Red/Green/Blue or another component changes the
  extraction/reconstruction path and can change color or gamut.
- The image is not padded; tiles are as equal as whole pixels allow.
- Apply replaces a still only after success. Sequence Apply writes a temporary
  sequence and promotes it only when all frames finish.
- The result keeps the source image's header cards and metadata, and an Apply
  records its processing state as nonlinear under the operation `clahe`.
- The history description records Grid Size, Contrast Limit, Amount, and, for a
  colour image, ` in channel: <channel>`. Grayscale inputs have no selectable
  color component to append.
- CFA sequence frames are auto-debayered before filtering when sequence
  auto-debayer is enabled; otherwise CLAHE receives the mosaic plane.

## Performance

The engine builds the tile tables and interpolates rows on the threads the
operation is granted. Runtime grows primarily with pixel count, histogram depth,
color conversions, and number of tiles. Every type except 8-bit uses 65,536
entries per tile, so high Grid Size values can increase table memory sharply;
the tables are rebuilt per channel rather than kept for all channels. Preview
area reduces tuning work, but Apply is always full-frame; Amount `0` does not
skip CLAHE.

The focused tests hold the engine to the recorded captures, a colour image's
equalization to its lightness alone, and the process to every representation,
mask, sequence, and cancellation path. They do not validate visual seams,
astronomical quality, or production peak memory.

## Worked example

For a `1024 × 1024` 8-bit grayscale image at Grid Size `8`, each tile is
`128 × 128`, so `P = 16,384` and `B = 256`. At Contrast Limit `2`, the cap is
`floor(2 × 16,384 / 256) = 128` counts per bin before redistribution.

Keeping the same image but increasing Grid Size to `16` makes `64 × 64` tiles.
Then `P = 4,096`, so the same Contrast Limit gives a cap of `32`. A 16-bit image
of the same size uses `B = 65,536`, so its cap at Grid Size `8` is the minimum
of `1` count per bin and nearly every populated bin is clipped. More tiles make
the mappings more local, with more tile tables and greater risk of tile-scale
texture.
Amount `0.5` finally averages source and full result pixel-for-pixel before the
mask. These are exact source-derived checkpoints, not a visual-quality claim.

## Warnings and limitations

- CLAHE enhances noise and residual calibration defects along with structure.
- Bilinear LUT interpolation reduces hard boundaries but does not prevent
  tile-scale mottling or visible seams under aggressive settings.
- Large tiles can produce broad halos; many tiny tiles can produce granular
  texture, star ringing, sparse histograms, and high memory use.
- Contrast Limit is normalized, not an absolute bin count, and depends on tile
  population and histogram depth (256 bins for 8-bit, 65,536 otherwise).
- Default RGB equalizes only L*, which protects chroma better than independent
  RGB equalization, but Lab conversion and gamut clipping can still change
  saturated colors.
- Hue, saturation, and chroma Channels are mathematically valid scalar inputs
  but are rarely safe photographic choices; cyclic hue has a wrap boundary.
- Every type except 8-bit is equalized with 65,536 bins over its full range;
  32-bit integer and 64-bit float images are processed at single precision.
- An Apply marks the image nonlinear (`clahe`); later processes that expect
  linear data will see that state.

## Troubleshooting

| Symptom | Likely cause | Corrective action | Verification |
| --- | --- | --- | --- |
| Background becomes grainy | Contrast Limit, Grid Size, or Amount too high | Reduce Contrast Limit first, then Amount; use fewer tiles; denoise earlier | Inspect smooth background at 100% |
| Blocks, seams, or mottling appear | Neighboring tile LUTs differ strongly | Reduce Grid Size/Contrast Limit; include several boundaries in preview | Toggle original/result at full-frame and 100% |
| Stars gain rings or broad halos | Tile scale and local CDF overemphasize bright transitions | Lower Amount/Contrast Limit; change Grid Size; mask stars | Inspect small and bright stars at 100% |
| Color shifts or discontinuities | Chroma/hue component selected, or Lab round-trip clipped gamut | Use RGB or CIE L*; lower Amount; protect saturated areas | Compare RGB and component views |
| Changing Contrast Limit seems weak | Normalized cap rounded to `1`, or Amount/mask attenuates result | Change Grid Size, raise limit gradually, confirm Amount and mask | Calculate `floor(C×P/B)` and compare unmasked preview |
| Preview differs from Apply | Preview area, Channel, mask, or display conditions changed | Match controls, disable area | Compare same target/view/history step |
| Grid Size is refused on a small image | The grid has more tiles than the image has pixels along an axis | Lower Grid Size | Error names the tile grid |

## Related workflow steps

Complete [noise reduction](../noise-reduction/wavelet-denoise.md) before local
equalization. Compare [Local Histogram Equalization](local-histogram-equalization.md)
for a per-pixel neighborhood CDF without tiles or clipping, and [Local Contrast
Enhancement](local-contrast-enhancement.md) for a high-radius residual model.
Use [Masks](../../interface/masks.md) to protect smooth background and [Image
History](../../interface/image-history.md) to compare or revert Apply.

## References and provenance

- [OpenCV 4.12.0 `cv::CLAHE` class reference](https://docs.opencv.org/4.12.0/d6/db6/classcv_1_1CLAHE.html)
  — the clip-limit and tile-grid contract of OpenCV's implementation; accessed
  2026-07-14.
- [OpenCV 4.12.0 CLAHE implementation](https://github.com/opencv/opencv/blob/4.12.0/modules/imgproc/src/clahe.cpp)
  — the tile padding, histogram sizes, normalized cap, redistribution, CDF LUT,
  and bilinear interpolation the engine's equalization is compared with;
  accessed 2026-07-14.
- AfterNight sources/tests are authoritative for the
  shipped controls, engine equalization, Lab composition, channels,
  Amount/masks, metadata/state/history, preview, and sequence behavior.
