# NB Color Mapper

## Purpose and expected result

NB Color Mapper prepares any number of same-size mono or RGB stills as layers,
optionally colorizes mono layers with an HSV hue and saturation, and combines
the included layers with an N-layer screen blend. A mono layer can first use
another listed layer as an additive, multiplicative, or boost modifier.

The expected primary result is a new float still named
`<first listed image> - NB Color Mapper`. It is RGB when at least one prepared
layer is RGB or colorized and can remain grayscale when every included layer is
neutral mono. Optional auxiliary windows contain individual colorized mono
layers. Sources are not replaced.

This is a free-form color mixing process, not fixed SHO/HOO channel assignment.
It does not identify filters, normalize exposure or bandwidth, register
geometry, match PSFs, solve WCS, or derive a physically calibrated spectrum.

## At a glance

| Item | Current behavior |
| --- | --- |
| Menu path | **Process > Color Spaces > NB Color Mapper** |
| Enabled when | A still is active, the same process is not already attached, and no attached process holds exclusive access; the descriptor does not restrict the active channel count |
| Inputs | One or more listed, same-width/height grayscale or three-channel RGB stills; a supported active still is added as layer `#1` automatically |
| Outputs | One new Float32 RGB still; optionally one Float32 RGB still for each included layer colourized alone; **Take snapshot** creates another still; no direct file output |
| Recommended image state | Registered, cropped, calibrated linear masters with gradients and gross scale differences corrected upstream |
| Preview support | Always-on asynchronous preview with histogram, pixel-changing Preview STF, and separate viewer display autostretch; starts on the full image with **Preview area** unchecked |
| Sequence support | Not supported: the registered action needs still images, and a sequence run is refused by name |
| Mask support | Not applied: the result is a new image, not an edit of the active view |
| External dependencies | No runtime external tool, model, catalog, or network service; the native port's source lineage is recorded under [References and provenance](#references-and-provenance) |

## Before you start

- Register and crop every layer to one geometry. The selector checks still-image
  status, grayscale/RGB format, and dimensions against the first resolvable
  layer only. It does not inspect stars, WCS, crop origin, pixel scale,
  orientation, or PSF.
- Correct gradients and bring very different channel levels into a workable
  relationship upstream. NB Color Mapper has per-layer tonal shaping and
  optional background neutralization, but no exposure, filter-bandwidth, or
  photometric normalization model.
- Activate the image you want to associate with the process before opening it.
  There is no visible target selector. The active view supplies the initial
  layer, mask, preview comparison source, output-neighbour placement, and
  fallback name even if you later edit the layer list.
- Disable the active mask while judging raw layer registration or palette
  values. The ordinary process mask is applied after the entire combination,
  including Preview STF, and blends against the active source rather than the
  first included layer.
- Decide whether the combined pixels should receive a final STF. **Preview STF**
  changes preview pixels only. **Apply STF to combined output** separately bakes
  the same family of transform into Apply output. Viewer autostretch is a third,
  display-only operation.

## Procedure

1. Activate a registered mono or RGB still and open **Process > Color Spaces >
   NB Color Mapper**. The active still appears as layer `#1` with **Include
   layer** checked.
2. Select `#1` in **Layers**. For mono input, set **Hue** and **Saturation** with
   the wheel or numeric fields. Useful anchor hues are red `0`, green `120`, and
   blue `240`; these are color coordinates, not filter identities.
3. Select **Add** to duplicate the current row and select the new row. Then choose
   another compatible image under **Image** and set its mapping. Repeat for every
   source. Reusing one image in multiple rows is allowed when you want different
   mappings.
4. Use **Midtones** and **Blackpoint** per row to control its value plane before
   colorization and mixing. Start at the identity values `0.500` and `0.000`.
5. To shape a mono row with another layer, choose **Add**, **Multiply**, or
   **Boost** under **Mode**, choose **With layer**, and tune **Factor**. An
   excluded row can still be a modifier.
6. Keep **Preview STF** checked while inspecting linear inputs, but avoid also
   enabling viewer autostretch unless you intentionally want two stretches.
   Move or resize **Preview area** for faster local tuning; it starts unchecked.
7. Enable **Background Neutralization** only after the blend is sensible. On a
   Preview-area run, its sample population is that cropped area; on a full
   preview or Apply, it samples the full result.
8. Choose whether to create auxiliary layer images or bake STF into the combined
   Apply output. Select **Apply** and wait for the new window. The current build
   provides no Apply progress dialog or user Cancel action.

## Complete parameter and action reference

| UI label/action | Type and unit | Runtime default | Persisted-default exception | Valid values, step, or constraints | Effect of increasing, decreasing, or selecting | Dependencies and enable/visibility rules | Quality, artifact, runtime, memory, or disk trade-off | Scope: preview/apply, channel, mask, still/sequence | Persistence |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Histogram plot | Graph, normalized intensity/count | Current combined preview; RGB channels overlaid | None | Binning comes from **Plot resolution** | Shows the mask-aware preview distribution and approximate endpoint-bin clipping | Always present | Recalculation adds work; endpoint counts depend on binning | Display only; preview result | Recomputed after preview/resolution changes |
| Left-drag in histogram | Graph gesture | No readout | None | Hold left button and move in graph | Shows normalized coordinate readout | Histogram always present | Diagnostic only | Display only | Ends on release |
| Middle-drag in histogram | Graph gesture | Fitted/unpanned | None | Hold middle button and drag | Pans the graph | Useful after zooming | Display cost only | Display only | Until reset/window close |
| Ctrl+mouse wheel in histogram | Graph gesture | Fitted size | None | Up `1.20x`; down `0.8x`; wheel without Ctrl scrolls | Zooms around cursor | Interactive graph enabled | Larger graph needs more rendering area | Display only | Window lifetime |
| Zoom in | Toolbar action | Fitted size | None | Each selection multiplies size by `1.30` | Enlarges plot | Available while idle | Plot rendering only | Display only | Window lifetime |
| Zoom out | Toolbar action | Fitted size | None | Each selection multiplies size by `0.70` | Shrinks plot | Available while idle | Plot rendering only | Display only | Window lifetime |
| Zoom reset | Toolbar action | — | None | One action | Fits graph and resets scrolling | Available while idle | Plot rendering only | Display only | Immediate |
| Show grid | Toggle | Checked | None | Checked/unchecked | Shows or hides graph grid | Always present | Display-only aid | Display only | Window lifetime; process Reset unchanged |
| Show clipping info | Toggle | Checked | None | Checked/unchecked | Shows/hides endpoint-bin counts and percentages | Always present | Bin-based diagnostic, not an exact clipped-pixel proof | Display only | Window lifetime |
| Absolute scales | Toggle | Unchecked | None | Checked/unchecked | Normalizes each visible histogram channel to its own peak | Always present | Can conceal relative RGB strength; pixels unchanged | Display only | Window lifetime |
| Draw histogram areas | Toggle | Unchecked | None | Checked/unchecked | Chooses filled-area or line rendering | Always present | Filled areas can obscure overlaps | Display only | Window lifetime |
| Show histogram | Toggle | Checked | None | Checked/unchecked | Shows or hides histogram curves | Always present | Display-only | Display only | Window lifetime |
| Plot resolution | Combo box, bins | **8-bit (256)** | None | **6-bit (64)** through **16-bit (64K)** plus **100**, **1000**, **10000 levels** | More bins show finer distribution | Always present | Higher counts increase histogram memory/rendering; engine precision unchanged | Display only | Window lifetime |
| Midtones | Double spin box, MTF midpoint | `0.500` | Active supported image is auto-added, then its selected-row value starts here | `0.001..0.999`; step `0.010` | Below `0.5` brightens interior values; above `0.5` darkens them | Edits the selected row; applies to mono, RGB, excluded, and modifier-source rows | Extreme values compress levels and emphasize noise/clipping; adds a full float transform when nonidentity | Preview/Apply; all channels of selected layer before modification/color | Stored per row for window lifetime; Reset/success changes only selected row to `0.500` |
| Blackpoint | Double spin box, normalized value | `0.000` | Same auto-added-row behavior as Midtones | `0.000..1.000`; step `0.001` | Raises the MTF shadows endpoint and suppresses values at/below it | Edits selected row; active for all layer formats/states | High values erase faint signal and can leave a sparse modifier | Preview/Apply; all channels of selected layer before modification/color | Per row; Reset/success changes only selected row to `0.000` |
| Layers table | Five-column single-row selector | One auto-added active mono/RGB still when supported | No saved process instance | Read-only **Include**, **Layer**, **Image**, **Color**, **Modifier** cells; duplicate images allowed | Selects the row loaded into every per-layer editor | Apply requires at least one valid row and one included row; every row must still resolve and match dimensions, even if excluded | Every row is prepared, so excluded/reference rows still cost conversion/stretch memory and time | Configuration; stills only | Rows survive Reset/success; window lifetime only |
| Add | Button/action | Enabled | None | Requires a selected **Image** other than **No image** | Appends current editor values as a new row and selects it | Same-size mono/RGB selector filter; does not deduplicate | More rows add preparation memory/time even if excluded | Configuration | Row persists until Remove/window close |
| Remove | Button/action | Enabled | None | One selected row | Deletes row; dependants using it reset to **Mode: None**; later modifier indices shift | No action without selection | Can change several mappings; releases future layer work | Configuration | Immediate; no undo in process window |
| Image | Combo box, layer source | Active supported still for auto row; otherwise current editor selection | Reset does not clear selection | Open grayscale/RGB still; dimensions must match first resolvable row | Replaces the selected row source | If first row is unresolved, selector temporarily accepts any supported dimensions; Apply still validates all rows | Wrong replacement causes registration artifacts; source itself remains unchanged | Preview/Apply; selected row | Per row/window; tracks renames; closing source disables Apply but does not delete row |
| Include layer | Checkbox | Checked | Reset affects selected row only | Checked/unchecked | Checked contributes prepared row to screen blend; unchecked can still serve as modifier | At least one row must be included | Excluded rows still undergo base preparation and metadata merge | Preview/Apply blend participation | Per row; Reset/success checks selected row |
| Color picker | Hue wheel plus saturation strip | Hue `0`, saturation `0.5` | Mirrors selected row | Drag wheel/strip; hue wraps; saturation clamps `0..1` | Sets the two numeric color fields together | Enabled only for selected mono source not referenced as another row's modifier; RGB ignores it | Saturated mappings can expose registration/PSF differences | Preview/Apply; selected mono row | Per row; Reset/success returns selected row to red/`0.5` |
| Hue | Double spin box, degrees | `0.000` | Same as color picker | `0.000..359.990`; step `1.000` | Rotates fixed HSV hue; `0` red, `120` green, `240` blue | Mono only and disabled when this row is used as a modifier; RGB ignores it | Large hue changes redistribute layer value among channels, not intensity scaling | Preview/Apply; selected mono row | Per row; Reset/success returns selected row to `0.000` |
| Saturation | Double spin box, HSV saturation | `0.500` | Same as color picker | `0.000..1.000`; step `0.010` | `0` leaves mono neutral/grayscale; `1` uses full fixed hue | Same enable rules as Hue | More saturation can clip/color-separate structure; exactly `0` can keep all-neutral output grayscale | Preview/Apply; selected mono row | Per row; Reset/success returns selected row to `0.500` |
| Mode | Combo box | **None** | Reset affects selected row only | **None**, **Add**, **Multiply**, **Boost** | Chooses modifier equation for selected mono row | Enabled only for mono row not referenced as another modifier; RGB and referenced-modifier rows are never modified | Add/Boost can clip highs; Multiply can suppress faint target signal | Preview/Apply; selected mono row before colorization | Per row; Reset/success returns selected row to **None** |
| With layer | Combo box, row reference | **None** | None | Any other row; self-reference omitted | Selects modifier's stretched grayscale values | Enabled only when Mode is not None; every non-None mode requires a valid other row | Using a noisy/poorly aligned layer transfers its structure; reference row may be excluded | Preview/Apply; selected mono target | Per row; removing reference resets Mode |
| Factor | Double spin box, multiplier | `1.00` | Reset affects selected row only | `0.00..10.00`; step `0.05` | Strengthens Add/Multiply/Boost modifier term; `0` makes Add/Boost identity but makes Multiply zero | Enabled only with active modifier mode | High Add/Boost clips at blend input; high Multiply can also reach the clamp; no extra buffer-size change | Preview/Apply; selected mono row | Per row; Reset/success returns selected row to `1.00` |
| Invert modifier | Checkbox | Unchecked | Reset affects selected row only | Checked/unchecked | Uses `1-u` instead of modifier value `u` | Enabled only with active modifier mode | Moves influence from bright to dark modifier regions; can boost background | Preview/Apply; modifier values only | Per row; Reset/success unchecks selected row |
| Median offset | Checkbox | Unchecked | Reset affects selected row only | Checked/unchecked | Replaces modifier with `max(0,u-median(u))` after optional inversion | Enabled only with active modifier mode | Suppresses half/below-median influence but needs a full modifier vector and median selection | Preview/Apply; modifier values only | Per row; Reset/success unchecks selected row |
| Enable | Background-neutralization checkbox | Unchecked | None | Checked/unchecked; requires Upper sigma > Lower sigma | Subtracts a robust background median per RGB channel, then conditionally rescales/clamps | Does nothing to grayscale combined output; sigma controls remain editable when off | Can remove color casts, but nebulosity-dominated samples can subtract real signal | Preview/Apply after screen blend; RGB only | Global/window; Reset/success unchecks |
| Rescale high | Background checkbox | Unchecked | None | Checked/unchecked | Checked uses each channel's actual corrected maximum for shared range; unchecked uses `1-background` | Evaluated only when Enable is checked and output is RGB | Checked expands available highs more aggressively and can amplify outliers/change balance | Preview/Apply; all valid RGB channels | Global/window; Reset/success unchecks |
| Lower sigma | Double spin box, normalized-MAD multiplier | `-2.80` | Ignored while Enable is off | `-10.00..10.00`; step `0.10`; must be below Upper sigma when enabled | Higher values reject more dark samples; lower values admit them | Field remains enabled when neutralization is off | Tight bounds can select no samples; broad bounds may include artifacts | Preview/Apply background sampling | Global/window; Reset/success returns `-2.80` |
| Upper sigma | Double spin box, normalized-MAD multiplier | `2.00` | Ignored while Enable is off | `-10.00..10.00`; step `0.10`; must exceed Lower sigma when enabled | Lower values reject more bright structure; higher values admit it | Field remains enabled when neutralization is off | Too high admits stars/nebulosity; too low can leave no samples | Preview/Apply background sampling | Global/window; Reset/success returns `2.00` |
| Preview STF | Checkbox, pixel transform | Checked | Separate from viewer display autostretch | Checked/unchecked | Checked computes a linked MTF from preview median luminance and bakes it into preview pixels | Does not control Apply; STF target is always validated | Makes linear mappings visible, but double-stretching with viewer autostretch misleads comparisons and costs a full pass | Preview/snapshot only, before active mask | Global/window; Reset/success checks it |
| STF target | Double spin box, normalized median target | `0.250` | Shared by Preview STF and optional Apply STF | `0.010..0.990`; step `0.010`; engine requires open interval `(0,1)` | Higher target brightens the mapped median; lower target darkens it | Value is validated even when both STF checkboxes are off | Extreme targets compress tones and can expose clipping/noise | Preview and optional Apply; linked channels | Global/window; Reset/success returns `0.250` |
| Create colorized layer images | Checkbox | Unchecked | None | Checked/unchecked | Apply also publishes each included mono row actually converted to RGB, before screen/background/output STF | Preview never creates auxiliaries; RGB, neutral-saturation, excluded, and referenced-modifier rows produce none | Extra full-size float RGB windows consume memory and later disk if saved | Apply only; auxiliary stills | Global/window; Reset/success unchecks |
| Apply STF to combined output | Checkbox, pixel transform | Unchecked | Independent of Preview STF | Checked/unchecked | Bakes median-target MTF into primary Apply output | Does not affect auxiliary colorized layers | Produces display-ready nonlinear pixels but reduces later linear-processing flexibility | Apply primary output only, before active mask | Global/window; Reset/success unchecks |
| Preview pane | Image viewer | Processed full-image preview | Inherits active view display-autostretch settings | Standard zoom/pan plus viewer linear/autostretch display | Shows current combined result and supports original/result comparison | Always present; no Preview checkbox | Holds source/result/statistics buffers; viewer stretch is display-only | Preview; active still | Window lifetime |
| Switch image | Preview-toolbar action | Processed preview shown | None | Click; **Space**; right-click; **Ctrl+Shift+O** | Toggles processed result and attached active source | Always present | Comparison is invalid if display modes differ | Preview display only | Preview-window lifetime |
| Preview area | Preview-toolbar checkbox and movable ROI | Unchecked; first enable creates centered area up to `512 x 512` | Reset does not change it | Checked/unchecked; drag/resize overlay | Checked crops every layer consistently, combines ROI, then pastes result over active source; background sampling uses cropped ROI | Snapshot follows this state; disabled during an in-flight execution | Faster/local tuning; outside ROI is not calculated and can hide global imbalance | Preview/snapshot only; active mask applied after composite | Window lifetime |
| Take snapshot | Preview-toolbar action | Enabled while idle | None | One action | Creates a still from current preview settings, including Preview STF and Preview-area composite | Disabled during captured execution; auxiliary-layer option suppressed | Adds an image window/memory; may not represent full Apply | Preview-derived still | New image persists until closed/saved |
| Apply | Button/action | Enabled when all rows resolve/match, one is included, modifiers are valid, STF target is valid, and enabled sigma limits are ordered | No saved process instance | One action; disabled during execution | Captures current parameters and output-name stem, then creates primary/optional auxiliary images asynchronously | The controls pane and Apply/Reset/Close buttons are disabled until completion | Full-frame work; the captured run and auxiliary names cannot drift with UI edits | Apply; still only; mask after complete result | Successful run resets globals and selected-row defaults but retains rows |
| Reset | Button/action | Enabled while idle | None | One action | Restores global defaults and defaults of the currently selected row | Does not remove rows, clear Image, reset histogram/viewer state, or reset Preview area; disabled during Apply | Recomputes preview | Configuration | Immediate; no process-instance save |
| Close | Button/action | Enabled while idle | None | One action | Closes process window | Disabled during Apply because no user cancellation controller exists | Wait for the full Apply to finish | Dialog only | Ends window-local settings |

The Apply path has no progress dialog or Cancel action. Preview recalculations do
use an internal cooperative controller so a superseded preview can stop at
engine polling boundaries; that controller is not a user-facing Apply control.

## How the algorithm works

### 1. Prepare every row

Every listed row, including excluded rows, converts to normalized 32-bit float.
For input sample `x`, **Midtones** `m`, and **Blackpoint** `b`, the per-layer
transfer is:

\[
T(x;m,b)=
\begin{cases}
0 & x\le b,\\
1 & x\ge 1,\\
\dfrac{(m-1)u}{(2m-1)u-m} & \text{otherwise},
\quad u=\dfrac{x-b}{1-b}.
\end{cases}
\]

The engine clamps `m` to `[10^-6,1-10^-6]`, clamps `b` to `[0,1]`, and clamps
the result to `[0,1]`. The UI's narrower Midtones range avoids those engine
endpoints. At `m=0.5,b=0`, this stage is skipped as identity.

### 2. Apply an optional modifier

Modification applies only to a mono target row that is not itself referenced as
another row's modifier. RGB targets, referenced modifier rows, and **Mode:
None** pass through. The modifier uses its already-stretched base image;
RGB modifiers convert to grayscale. It does not use the modifier row's own
colorization or modification.

For target `t`, modifier `u`, and Factor `f`:

- **Invert modifier** first changes `u` to `1-u`.
- **Median offset** then changes it to `max(0,u-median(u))`.
- **Add:** `t' = t + f u`.
- **Multiply:** `t' = t(f u)`.
- **Boost:** `t' = t(1 + f u)`.

This stage does not immediately clamp. Values are clamped when the prepared
layer is read by the blend. A referenced row is deliberately not modified or
colorized, preventing recursive modifier evaluation; it may still be included
as a neutral grayscale layer.

### 3. Colorize mono layers

An eligible mono layer with Saturation greater than floating-point epsilon uses
its sample as HSV Value with fixed Hue and Saturation, then converts HSV to RGB.
Hue is normalized modulo 360 and saturation is clamped to `[0,1]`. For example,
full saturation maps value `0.25` at hue `0` to RGB `(0.25,0,0)` and value
`0.4` at hue `120` to `(0,0.4,0)`. Saturation `0` skips colorization and leaves
the layer grayscale. RGB layers ignore Hue and Saturation.

### 4. Screen the included layers

For every output channel `c`, each prepared layer sample is converted to `0` when
it is NaN or infinite; finite samples are clamped to `[0,1]`. The engine then
calculates:

\[
O_c=\operatorname{clip}_{[0,1]}
\left(1-\prod_{i=1}^{N}(1-L_{i,c})\right).
\]

If any included prepared layer is RGB, output is RGB and grayscale layers are
broadcast into all three channels. If none is RGB, output remains grayscale.
Screen is commutative for the prepared samples: row order does not change the
blend equation, although the first row still controls dimension filtering,
metadata base, and output name.

### 5. Optionally neutralize RGB background

Neutralization is skipped for grayscale output. For each RGB channel, the
engine samples the whole calculated image: that is the cropped Preview area
during ROI preview and the full image otherwise. It calculates median `M`, raw
MAD, and `sigma = 1.4826 MAD`, then keeps samples in the inclusive interval:

\[
[\operatorname{clip}(M+l\sigma),\operatorname{clip}(M+h\sigma)],
\]

where `l` and `h` are the lower and upper controls. The median `B_c` of retained
samples is the channel background. If no sample survives, that channel is left
unchanged and the engine records a warning in its result; the process has no
visible warning report for it.

For valid channels, it subtracts `B_c`. It forms one shared RGB minimum and
maximum. The low endpoint uses each channel's actual `min-B_c`; the high
endpoint uses `1-B_c` by default or actual `max-B_c` with **Rescale high**. If
the shared range extends below 0 or above 1 and has nonzero width, all valid
channels use `(x-B_c-global_min)/(global_max-global_min)`. Results are clamped.

### 6. Optionally apply linked STF

The engine takes the median of per-pixel arithmetic RGB mean `(R+G+B)/3`, or
the mono sample for grayscale. For median `P` and **STF target** `q`, it derives
`m=MTF(P,q,0,1)` and then maps every channel with `MTF(x,m,0,1)`. It skips this
stage when `P` is effectively 0 or 1. **Preview STF** requests the stage only
for preview/snapshot; **Apply STF to combined output** requests it only for the
primary Apply image.

The active-view mask is blended after all six stages. Mask weight `a` produces
`active_source(1-a)+combined*a`; an inverted mask swaps those weights.

## Parameter interactions and starting points

| Combination | Start with | What it controls | Risk or diagnostic |
| --- | --- | --- | --- |
| Midtones + Blackpoint | `0.5` / `0` | Value distribution before every later operation | Strong blackpoint can erase modifier/palette signal before colorization |
| Hue + Saturation | Anchor hues, moderate saturation | Fixed HSV mapping of one mono row | It does not identify Ha/OIII/SII; wrong row assignment is accepted |
| Mode + With layer + Factor | None, then low factor | Spatial cross-layer shaping | Misregistration transfers doubled stars/halos; referenced layer is nonrecursive |
| Include layer + modifier reference | Include targets; exclude mask-only rows | Blend participation is separate from modifier availability | Excluded rows still must remain open, match dimensions, and incur preparation cost |
| Enable + sigma limits | Off; then `-2.8..2.0` | Robust channel backgrounds | Full-frame nebulosity is not clean sky; empty selections are silently skipped in the UI |
| Rescale high + background | Off | Shared RGB post-subtraction range | On can expand isolated highs/outliers to the range endpoint |
| Preview STF + viewer autostretch | Preview STF on, viewer stretch off | Pixel stretch versus display stretch | Enabling both makes preview much brighter than final unstretched output |
| Preview STF + Apply STF | Judge independently | Preview and final pixel policies | Matching checkboxes use same target but preview ROI/full-image medians can differ |
| Preview area + background | Representative empty/background region | Preview sample population and processing cost | Apply returns to full-image sampling; local result need not predict global neutralization |
| Active mask + layer list | Mask off while mapping | Final outer blend against launch-time active image | Mask does not weight individual layers and can mix an unrelated active source into result |

## Inputs, outputs, and metadata

- All rows must resolve to open grayscale or RGB stills with identical width and
  height. Pixel types may differ; working and output buffers are 32-bit float.
  Empty lists, invalid images, dimension mismatches, no included layer, invalid
  modifier references, non-finite parameters, invalid STF target, and enabled
  unordered sigma limits are rejected. Non-finite input pixels are not a
  parameter error: at the screen-blend input they are replaced with `0` so they
  contribute no light to the combined result.
- Apply creates `<first listed image> - NB Color Mapper`; the fallback is the
  active view name. Sources remain unchanged. There is no palette preset,
  matrix, report, mask, or direct disk file.
- **Create colorized layer images** publishes only included mono layers actually
  converted to RGB. Their names are
  `<base> - <layer image name> colorized`. They represent the per-layer MTF and
  optional modification/colorization, before screen blend, background
  neutralization, combined-output STF, and active mask.
- The primary output's metadata is built from every row - including excluded
  and modifier-only rows - the first row leading. With more than one row, its
  exposure is the light the rows hold together, recorded once: the sum, over
  the distinct acquisitions, of each row's whole integration - its `TOTEXP`
  card, else its `LIVETIME` card, else its exposure times its frame count
  (`STACKCNT`, else `NCOMBINE`, else one). Rows that state the same exposure
  start are one acquisition - an image and copies made of it, such as its
  starless image, its stars or a channel taken out of it - and count once,
  with the largest integration and the largest frame count any of them states,
  since a copy such as a StarNet output may have lost its `TOTEXP` and
  `STACKCNT` cards; a row that states no exposure start always counts on its
  own. One row keeps its own exposure. The observation start is the earliest
  any row states. The object, instrument, telescope, observer, filter, sensor
  temperature, gain, and binning are kept when every row states the same value
  and dropped otherwise, so a filter is kept only when every row shares it.
- The primary output keeps the header cards every row's image states
  identically, but no exposure, processing-state or identity card of a row; a
  card the rows state differently, or only some state, is dropped, except the
  first row's pointing cards (`RA`, `DEC`, `OBJCTRA`, `OBJCTDEC`, `OBJRA`,
  `OBJDEC`, `TELRA`, `TELDEC`), kept because the result is on its grid, and the
  observation-start cards (`DATE-OBS`, `DATE-LOC`, `MJD-OBS`, `DATE-BEG`,
  `MJD-BEG`, `TIME-OBS`), taken together from the row that began earliest.
  With more than one row the output states its own `EXPTIME` and `TOTEXP` as
  the exposure above, `LIVETIME` the same when a row states one, and
  `STACKCNT` and `NCOMBINE` as the frame counts of the distinct acquisitions
  summed, each when a row states it. It keeps the first row's plate solution
  with its world coordinate (WCS) cards, so the viewer, the astrometry window
  and a new plate solve find it on the result. A colorized layer image keeps
  its own row's record whole, with the colorization appended to its lineage.
- The primary output is always marked nonlinear with operation provenance
  `nb-color-mapper`, whether or not final STF is checked. Auxiliary colorized
  layers are marked nonlinear with `nb-color-mapper-layer`.
- Successful Apply records the description “Created NB Color Mapper combination
  from selected mono and RGB layers,” resets global controls and the selected
  row's defaults, and retains the layer rows/selections.
- **Take snapshot** follows current Preview STF, active mask, and Preview area.
  With the area checked, only that ROI is a calculated combination in the
  full-size snapshot.

## Performance

Base preparation is proportional to total pixels across every listed row, not
only included rows. Nonidentity per-layer MTF, mono modification, HSV
colorization, N-layer screen blending, background statistics/rescale, and STF
each add full-plane passes. Pixel transforms and the screen loop use oneTBB;
layer preparation order and background sample/statistics scans are otherwise
serial at the process level.

Memory scales with image dimensions and row count. The engine holds a float base
image for every row, prepared included images, the float combined result, and a
full `double` modifier vector for each modified mono target while it is built.
Colorized layers are three-channel float images; requesting auxiliary outputs
copies them. Preview, histogram, source, mask, and snapshot buffers add to the
working set. Use **Preview area** for local tuning and remove unused rows rather
than only unchecking Include. No approved astronomical set was timed.

## Worked example

- **Evidence mode:** Source- and focused-test-derived; not executed on
  project-owned astronomical data.
- **Target row:** one mono sample `t=0.20`, identity Midtones/Blackpoint, hue
  `0`, saturation `1`, **Mode: Add**, Factor `0.5`.
- **Modifier row:** one mono sample `u=0.50`, identity tonal controls,
  **Include layer** unchecked. Add produces `t'=0.20+0.5(0.50)=0.45`.
- **Color checkpoint:** HSV hue `0`, saturation `1`, value `0.45` produces
  `(0.45,0,0)`.
- **Second included row:** an RGB sample `(0.20,0.30,0.40)` with identity tonal
  controls.
- **Screen checkpoint:** the result is
  `(1-(1-.45)(1-.20), 1-(1-0)(1-.30), 1-(1-0)(1-.40))`, or
  `(0.56,0.30,0.40)`.
- **Policy:** Background neutralization and both STF stages are off; no active
  mask is applied. The modifier value, HSV mapping, and screen equation reproduce
  focused engine fixtures, but the combined single-pixel scenario is a derived
  teaching checkpoint rather than an astronomical quality claim.

## Warnings and limitations

!!! warning "Preview STF and viewer autostretch are separate"
    Preview STF permanently transforms the preview buffer. Viewer autostretch
    changes only presentation. If both are enabled, the preview is stretched
    twice and does not predict an Apply output with final STF off.

!!! warning "The active view is still an implicit target"
    The window has no target selector, but the launch-time active image remains
    the comparison/mask source. A mask blends that source against the whole
    combination; it does not mask individual layers.

!!! warning "Apply has no progress or Cancel"
    The engine supports cooperative cancellation, but this process exposes no
    Apply controller. The mapping/output controls and Apply, Reset, and Close are
    disabled after parameters and output names are captured, then restored when
    Apply finishes.

!!! warning "Dimensions are the only geometry compatibility test"
    Equal size does not prove registration, crop origin, orientation, scale, or
    PSF agreement. Colorization and modifiers make misalignment conspicuous as
    colored or transferred star structure.

## Troubleshooting

| Symptom | Likely cause | Fix |
| --- | --- | --- |
| Menu action is disabled | No active still, same process already attached, or an exclusive process is attached | Activate a still and close the conflicting process |
| Expected source is absent from Image | It is a sequence, not mono/RGB, or differs from the first resolvable row's dimensions | Convert/extract a still and register/crop it to exact dimensions |
| Apply is disabled | A row's image closed, dimensions differ, no row is included, modifier reference is invalid, or enabled sigma limits are reversed | Repair every row, include at least one, and ensure Upper sigma exceeds Lower sigma |
| Preview is unchanged active source | Parameters are not ready, or active mask is black over the result | Restore valid rows and temporarily disable/check the mask |
| Color controls are disabled | Selected image is RGB or this mono row is referenced as another row's modifier | Select an eligible mono target; use a separate duplicate row if it must also be colorized |
| Modifier mode has no visible effect | Target is RGB/referenced, Factor is zero for Add/Boost, Multiply has zero Factor, or modifier was suppressed by Median offset | Use an eligible mono target and inspect modifier values/settings |
| Excluded row still affects result | It is referenced as another row's modifier | Set the target row's Mode to None or choose another modifier |
| Preview is much brighter than Apply | Preview STF is on, Apply STF is off, viewer autostretch is also on, or ROI/full-image medians differ | Compare with one display mode; align the two STF checkboxes only if desired; test full preview |
| Background neutralization changes color strongly | Nebulosity/outliers dominated samples or Rescale high expanded the shared range | Use a representative Preview area, tighten sigma limits cautiously, or disable Rescale high/neutralization |
| One background channel is not corrected | No samples survived that channel's sigma interval; UI does not surface engine warnings | Widen limits and choose a cleaner area |
| Auxiliary layer image is missing | Row was excluded, RGB, saturation zero, or referenced as a modifier | Include an eligible colorized mono row and reapply |
| Output is grayscale | Every included prepared row remained neutral mono | Set nonzero Saturation on an eligible mono row or include an RGB row |
| Doubled stars or color rims | Sources share dimensions but are not registered or PSF-matched | Register/crop and, if needed, match star profiles upstream |
| Mapping controls or dialog buttons are disabled | A non-cancellable Apply is running | Let Apply finish; tune with Preview area before the next full run |

## Related workflow steps

- [Narrowband Imaging](../../workflows/narrowband-imaging.md) — calibration,
  registration, gradients, scaling, palette choice, and later stretching.
- [Image Registration](../registration-alignment/image-registration.md) — align
  layer geometry before mapping or cross-layer modifiers.
- [Linear Match](../color-calibration/linear-match.md) — correct gross relative
  scale differences upstream when appropriate.
- [Channel Composition](channel-composition.md) — use explicit scalar-to-RGB
  channel assignment instead of HSV layers and screen mixing.
- [NBRGB Combination](nbrgb-combination.md) — inject continuum-corrected
  narrowband structure into an existing broadband RGB target.
- [Narrowband Palette Composer](narrowband-palette-composer.md) — compare its
  fixed palette formulas and normalized mixes.
- [Process Windows](../../interface/process-windows.md) — shared preview,
  masks, snapshots, Apply, Reset, and history behavior.
- [Glossary](../../reference/glossary.md) — midtones transfer function, HSV,
  background neutralization, and palette terminology.

## References and provenance

- AfterNight source and focused tests.
- `docs/dev/NBCOLORMAPPER_PROVENANCE.md` records the reviewed Cosmic Photons
  NBColorMapper v2.2 source snapshot, file hashes, Mike Cranfield authorship,
  and GPLv3 license. The native AfterNight port uses engine primitives and Qt
  controls; the upstream PixInsight/PJSR APIs, generated resources, and script
  bitmap assets are not included.
- The same attribution and port boundary are exposed in **About > Licenses**.
