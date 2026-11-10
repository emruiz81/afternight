---
kind: process
stage: finish
menu: Process > Star Processing > Profiled Starmask Recombination
summary: >-
  Adds a stars-only image to a matching starless base where a saved Star Profiler detection list places the stars.
data_out: nonlinear
derived: true
---
# Profiled Starmask Recombination

## Purpose and expected result

Profiled Starmask Recombination adds a stars-only image to a matching starless
base, then uses a saved Star Profiler v1 or v2 detection list to choose where
that addition is visible. It is intended for a nonlinear starless/stars pair
from the same registered image when only selected stars should be restored or
their edges should be reduced before restoration.

With a valid profile, the expected result is the untouched starless base outside
the generated mask and `starless + tightened-stars` inside it. **Invert
Selection** reverses those regions. **OK** creates a new still named
`<starless> - <stars> - MERGED` and closes the process; the selected inputs are
not replaced. Without a nonempty profile, however, no mask is generated and the
process performs a full unprofiled addition. Treat a loaded, checked profile as
a required input even though OK does not enforce it.

## At a glance

| Item | Current behavior |
| --- | --- |
| Menu path | **Process > Star Processing > Profiled Starmask Recombination** |
| Enabled when | The top-level descriptor remains available without an active image. **Preview** and **OK** enable only after both image selectors resolve; profile controls enable after a profile list loads |
| Inputs | Starless Image, Stars Image, and an optional `.anspv1` or `.anspv2` profile. Image selectors list all open still/sequence views without compatibility filtering |
| Outputs | One new still named `<starless> - <stars> - MERGED`; a sequence selection contributes only its currently displayed frame. Preview snapshots create auxiliary stills |
| Recommended state | Geometrically identical nonlinear float starless/stars stills plus the profile detected from the same geometry before either branch was resampled or cropped |
| Profile scope | The current in-memory list can be filtered destructively; Reload restores the file. Profile coordinates are rasterized against the selected Stars Image dimensions without identity/geometry validation |
| Mask support | An internal mono profile mask only, painted by the engine: binary areas are each star's fitted ellipse scaled and softened by the exact Gaussian; shaped masks use the engine's star-mask shapes. Ordinary active target masks are not applied |
| Preview support | Full-frame asynchronous result, profile overlays, display autostretch, zoom/channel/display controls, and snapshots. **Preview area** crops the stars and the painted mask to the same area; Switch image shows the starless image |
| Sequence support | No true sequence output. OK always creates one still from the currently displayed selected images |
| External dependencies | None; a profile must already have been created or saved by [Star Profiler](../registration-alignment/star-profiler.md) |

## Before you start

1. Use a starless/stars pair from the same registered image. Confirm equal
   width, height, and channel count; selectors do not check compatibility.
2. Use a profile created on that same geometry. Cropping, resampling, rotation,
   or selecting a profile from another image moves the mask away from the stars;
   out-of-frame profile areas are silently clipped.
3. Prefer floating-point images when the stars may sum above one. Two
   floating-point images read from files keep their sum above one; a pair that
   includes an integer image is read as fractions of full scale (a signed integer
   counted from its smallest value) and its sum is held at one.
4. Load the profile and confirm **Current Stars** is nonzero before OK. An empty,
   cleared, or execution-time failed profile produces no mask and restores the
   entire Stars Image.
5. **Preview area** crops the stars and the painted mask to the same area as the
   starless image, so an area preview shows the recombination of that area.
6. A profile that cannot be read is reported under **Load Star Profile**; the
   star list is then empty and the profile controls are disabled until a valid
   profile loads.
7. **OK** reads the Stars Image, the current star list, and every mask, edge,
   and inversion setting when it is pressed; a later change reaches the next run
   only.

## Procedure

1. Open **Process > Star Processing > Profiled Starmask Recombination** and
   select the matching **Starless Image** and **Stars Image**.
2. Enter or open a `.anspv1` or `.anspv2` **Path**. Confirm the displayed
   **Current Stars** count and inspect **Show Star Profiles** over the image.
3. Open **Stars Filter** if only a subset should be restored. Tune with its
   Preview, click OK only after checking the count, and remember that confirmation
   replaces the in-memory list. Use Reload to recover the saved list.
4. Start with **Binary Areas**, **Scale Factor 1.00**, **Soften Edges 2.00**,
   and **Tighten Edges 0.00**. Switch to a shaped profile only when a graded
   radial transition is wanted.
5. Enable the main **Preview**. Inspect at 100% for star alignment, retained
   halos, hard mask boundaries, color fringes, and clipped cores. **Show Star
   Profiles** changes only the overlay, not the recombination.
6. If necessary, adjust **Scale Factor** for binary geometry or **% Pixel
   Increase** plus **Pixel Increase** for shaped geometry. Increase **Tighten
   Edges** cautiously because it subtracts an edge map from the Stars Image.
7. Use **Invert Selection** only when the complement of the listed stars should
   be restored. Click **OK** to create the new merged still, then verify its
   name, history/state, geometry, cores, halos, and frame borders.

## Complete parameter and action reference

| UI label/action | Type and unit | Runtime default | Persisted-default exception | Valid values, step, or constraints | Effect of increasing, decreasing, or selecting | Dependencies and enable/visibility rules | Quality, artifact, runtime, memory, or disk trade-off | Scope: preview/execution, channel, mask, still/sequence | Persistence |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Starless Image | Image/sequence selector | **No Image** | None | Any open view; addition requires the same width, height, and channel count as Stars Image | Supplies base `B`, metadata, preview display state, and name prefix | Required for Preview/OK; no type/state/alignment filtering | Wrong geometry/state gives displaced or double structure | Preview/OK; all stored channels; current frame only for a sequence | Dialog lifetime |
| Stars Image | Image/sequence selector | **No Image** | None | Any open view compatible with Starless Image | Supplies stars layer `S` and dimensions/type for the profile mask | Required for Preview/OK; profile coordinates are not validated against it | Non-star residuals and misalignment are restored directly | Preview/OK; all stored channels; current frame only | Dialog lifetime |
| Path | Editable profile path | Empty | None | Filename must end in `v1` or `v2`, case-insensitively, and contain the matching profile data | Typing immediately attempts a load and chooses the v1/v2 mask/filter engine from the suffix | Image selection is not required to load; nonempty list enables profile actions | Invalid text can show repeated errors; a failed load empties the list and disables the profile controls; profile file/list consumes memory | In-memory star selection and internal mask | Dialog lifetime; path itself is not saved here |
| Open profile (`...`) | File action | — | Remembers folder key `__PROF_STAR_MASK_RECOMB_FOLDER__` | Filter shown as `Files (*.anspv1;*.anspv2)` | Selects the last chosen file and assigns Path | Always available | File parse/I/O cost; no geometry check | Profile load | Last folder persists; selection lives in dialog |
| Reload profile | Button | Disabled | None | Current nonempty valid Path | Reloads the saved profile, discarding in-memory filtering | Enabled after a profile list loads | Useful recovery; reparses and regenerates mask | Profile/list/mask | File is source of truth |
| Clear profile | Button | Disabled | None | Click | Empties Path, closes an open Stars Filter, unloads the list, and clears the mask | Enabled after a profile list loads | Subsequent Preview/OK becomes full addition unless another profile is loaded | Profile/list/mask | Not persisted |
| Current Stars | Read-only count | `0`; field initially disabled | None | Nonnegative list size | Shows current loaded/filtered list size | Enabled after a valid list loads | A zero count means no profile mask even if the file was syntactically valid | Display/list | Updates for accepted filter changes |
| Stars Filter | Button/dialog | Disabled | None | Requires nonempty profile and both selected images | Opens the version-specific filter inventory below | Enabled only for a nonempty list and selected pair | Filtering reduces mask work; OK is destructive in memory | Profile list and overlay/mask | Reload recovers file contents |
| Show Star Profiles | Checkbox | On but disabled | None | Off/On | Shows/hides profile geometry overlay; no sample effect | Valid profile required; meaningful with a view/Preview | Dense overlays can obscure inspection | Display only | Dialog lifetime; Reset does not change it |
| Invert Selection | Checkbox | Off but disabled | None | Off/On | Off restores stars under mask `A`; On restores the complement `1-A` | Valid profile list required | Wrong polarity can restore all unwanted stars or background residuals | Internal mask blend | Dialog lifetime; Reset turns it off |
| Scale Factor | Multiplicative geometry | `1.00` | None | `0.00`–`2.00`; step `0.05` | Enlarges binary v1 circles/v2 ellipses and profile overlay | Valid profile required; affects Binary Areas, not shaped mask synthesis | Large values merge regions; zero degenerates binary geometry | Binary mask and display overlay | Dialog lifetime; Reset `1.00` |
| Tighten Edges | Amount | `0.00` | None | `0.00`–`2.00`; step `0.03` | Subtracts more four-direction edge magnitude from the entire Stars Image before addition, reading a constant zero border at each row's ends | Valid profile controls enable it, but the operation is global before mask blending | Can shrink halos but also darken/erase edges and create color artifacts | All stars channels before masked/full addition | Dialog lifetime; Reset `0.00` |
| Shape | Selector | **Binary Areas** | None | Binary Areas, Bell, Truncated Bell, Linear, Truncated Linear, Cubic, Quadratic | Chooses filled detected geometry or a circular radial profile | Mask Generation group requires a valid profile | Shaped wings overlap by maximum; v2 shaped modes discard ellipticity/angle | Internal mono mask | Dialog lifetime; Reset Binary Areas |
| Soften Edges | Gaussian blur scale | `2.00` | None | `0.00`–`10.00`; step `0.03` | Broadens binary-mask transitions | Enabled/effective only for Binary Areas | Reduces hard seams but can restore halos/background farther from stars | Binary mono mask | Dialog lifetime; Reset `2.00` |
| % Pixel Increase | Radius multiplier | `0.50` | None | `0.00`–`1.00`; step `0.03` | Sets shaped radius multiplier; `0.50` means half the base radius, not +50% | Enabled only for non-binary Shape | Small values truncate stars; large values merge wings | Shaped mono mask | Dialog lifetime; Reset `0.50` |
| Pixel Increase | Additive radius | `0 px` | None | `0`–`20 px`; step `1 px` | Adds a fixed radius after the percentage multiplier | Enabled only for non-binary Shape | Dominates small stars first; large values increase overlap/cost | Shaped mono mask | Dialog lifetime; Reset `0` |
| Preview | Checkbox | Off; disabled | None | Off/On | Creates/destroys asynchronous full-frame preview | Both images required; a profile is not required | Repeated mask/type/filter work allocates full-frame buffers | Preview only | Dialog lifetime |
| Preview pane | Image viewer | Processed result | Execution autostretch supported | Pan/scroll and actions below | Displays result without committing inputs | Exists with Preview | Holds result, alternate, display, and mask buffers | Preview only | Preview lifetime |
| Fit To Window | Preview action | Fitted initially | None | Click or **Ctrl+Shift+F** | Fits the whole frame | Preview required | Resampling can hide one-pixel misalignment | Display only | Viewer lifetime |
| Zoom In | Preview action | Current scale | None | Click; `1.25x` relative zoom | Enlarges local detail | Preview required | Less context | Display only | Viewer lifetime |
| Zoom Out | Preview action | Current scale | None | Click; reciprocal relative zoom | Shows more context | Preview required | Less pixel detail | Display only | Viewer lifetime |
| Zoom 100% | Preview action | Current scale | None | Click or **Ctrl+Shift+1** | Shows one image pixel per display pixel | Preview required | Best for masks, edge subtraction, and clipping | Display only | Viewer lifetime |
| View Channel | Preview selector | **RGB** for RGB; disabled **Grayscale** for mono | None | Stored/derived RGB, CIE, HSL/HSV/HSI, and YCbCr components | Changes displayed component only | RGB result required for derived channels | Helps reveal channel-dependent edge artifacts | Display only | Viewer lifetime |
| Display Mode | Preview selector | Inherits Starless Image Linear/Autostretch state | None | Linear, Autostretch | Changes visualization only | Autostretch enables settings | Stretch can conceal clipping or exaggerate mask boundaries | Display only | Preview view state |
| Autostretch Settings | Preview dialog action | Disabled in Linear mode | Shared values load when opened | Opens display settings; Apply Stretch hidden | Tunes preview display | Autostretch mode only | Does not change output samples | Display only | Viewer-scoped |
| Switch image | Preview action | Profiled result shown | None | Click, **Space**, right-click, or **Ctrl+Shift+O** | Toggles between the result and the starless image | Preview required | Compares the recombination with its base | Display only | Preview lifetime |
| Preview area | Preview checkbox/overlay | Off | None | Off/On; move/resize overlay | Crops the stars and the painted mask to the same area as the starless image and shows their recombination there | Preview required | A smaller area is less preview work | Preview/snapshot | Preview lifetime |
| Take snapshot | Preview action | No snapshot | None | Click or **Ctrl+Shift+K** | Re-executes into an auxiliary still | Preview required | Adds full processing/memory | New still | Each click creates a view |
| OK | Dialog action | Disabled | None | Click | Creates `<starless> - <stars> - MERGED` and closes after success | Both images required; profile not required; no compatibility precheck; the Stars Image, the star list, and every setting are read when OK is pressed | Full addition occurs if the profile is absent or empty | One new still | Inputs unchanged; dialog closes after success |
| Reset | Dialog action | — | None | Click | Resets Scale, Tighten, Invert, Soften, percentage/pixel growth, and Shape to their initial values | Does not clear selectors, Path, filtered list, Show Star Profiles, or Preview | Restores mask controls while retaining selection | Dialog controls | Not a factory-state restore |
| Close | Dialog action | Enabled | None | Click | Closes the window without starting a run | Remains enabled | Does not delete snapshots or prior merged stills | Dialog | Discards dialog state |

### Stars Filter dialog

| UI label/action | Type and unit | Runtime default | Persisted-default exception | Valid values, step, or constraints | Effect of increasing, decreasing, or selecting | Dependencies and enable/visibility rules | Quality, artifact, runtime, memory, or disk trade-off | Scope: preview/execution, channel, mask, still/sequence | Persistence |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| v1 Intensity Min/Max | Data-derived range | Full observed range | — | Current extrema; step `0.1` | Narrows retained v1 intensity | v1 only | Can remove faint or bright stars | Current list/mask | Dialog until OK |
| v1 Quality Min/Max | Data-derived range | Full observed range | — | Current extrema; step `0.1` | Narrows v1 quality | v1 only | Quality is not the v2 metric | Current list/mask | Dialog until OK |
| v1 Radius Min/Max | Data-derived pixels | Full observed range | — | Current extrema; step `0.1 px` | Narrows v1 radius | v1 only | Excludes tiny detections or large structures | Current list/mask | Dialog until OK |
| v2 Mag Min/Max | Instrumental magnitude | Full range; Min starts at larger/fainter value, Max at smaller/brighter value | — | Current extrema; step `0.1`; retains `Max <= Mag <= Min` | Narrows v2 instrumental flux magnitude | v2 only | Not a calibrated catalog magnitude | Current list/mask | Dialog until OK |
| v2 FWHMx Min/Max | Data-derived pixels | Full observed range | — | Current extrema; step `0.1 px` | Narrows major-axis width | v2 only | Can select sharp/broad fits | Current list/mask | Dialog until OK |
| v2 FWHMy Min/Max | Data-derived pixels | Full observed range | — | Current extrema; step `0.1 px` | Narrows minor-axis width | v2 only | Combine with FWHMx to avoid shape bias | Current list/mask | Dialog until OK |
| Random selection | Percentage | `100%` | — | `0`–`100%`; step `0.5%` | Retains exactly that share of the stars the ranges kept, chosen from a seed, so the same stars are kept while the filter is open | Both versions | `0%` keeps no star | Current list/mask | Dialog until OK |
| Include Neighbors | Checkbox | Off | — | Off/On | Adds spatially intersecting neighbors after a threshold/random filter | Effective only when another filter is active | Can expand selection substantially in dense fields | Current list/mask | Dialog until OK |
| Preview | Checkbox | On | — | Off/On | Updates overlay/count while tuning without committing the list | Both versions | Large lists can refilter noticeably | Overlay/list preview; not output pixels by itself | Not persisted |
| Filtered Stars | Read-only count | `total/total` | — | Read-only | Shows retained/original counts | Always visible | No processing effect | Filter dialog | Not persisted |
| OK | Button | — | — | Confirmation required | After Yes, replaces the process's in-memory star list and closes | Refusal leaves list unchanged | Removed stars require Reload to recover | Profile list/mask | Not written to profile file |
| Reset | Button | — | — | Click | Restores data-derived ranges and 100% random selection | Preview/Include Neighbors are not explicitly reset | Recomputes preview | Filter dialog | Not persisted |
| Cancel/Close | Button/window action | — | — | Click | Restores original overlay/list and closes | — | No data change | Filter dialog | Not persisted |

## How the algorithm works

### Conceptual pipeline

1. Load the profile version from the filename suffix and keep its star list in
   memory. Optional Stars Filter confirmation replaces that list.
2. Rasterize a one-channel mask at the selected Stars Image dimensions. Binary
   Areas use filled v1 circles or v2 rotated ellipses; shaped modes evaluate one
   of six circular profiles and combine overlaps by maximum.
3. On every run, the engine paints the mask over the whole Stars Image and
   crops it, like the stars, to a preview area as the framework crops the
   starless image.
4. Read both images in one domain: two floating-point images that state a
   physical quantity in their own units, any other pair as fractions of full
   scale. If Tighten Edges is positive, subtract the stars' four-direction edge
   magnitude, reading a constant zero border at each row's ends, and hold the
   result to `[0,1]` in a normalized domain.
5. Add base and processed stars in double, holding the sum at one in a
   normalized domain. If a profile mask exists, blend between base and sum using
   that mask or its inverse. Otherwise publish the full sum. Every sample is
   written once in the higher of the two images' representations.
6. Keep the starless image's record, append the recombination to its lineage,
   mark the result nonlinear with operation `profiled-starmask-recombination`,
   and open the named still.

### Profile-mask synthesis

For v1 Binary Areas, each profile record supplies a circular radius `r`; the
mask draws a filled circle with radius `Scale Factor x r`. For v2 it draws a
rotated ellipse centered at the fitted position with full axes
`4 x FWHMx x Scale Factor` and `4 x FWHMy x Scale Factor`, rotated by the
negative fitted angle. **Soften Edges** then applies the engine's exact Gaussian
of that standard deviation; the mask is a 32-bit float image in `[0,1]`.

Shaped modes first choose base radius `R_base = v1 radius` or
`max(v2 FWHMx, v2 FWHMy)`, so v2 ellipticity and angle are discarded. The final
radius is

`R = (% Pixel Increase) x R_base + Pixel Increase`.

For distance `d` from the center, the six functions are:

- Bell: `exp(-d^2 / (2 R^2))`.
- Truncated Bell: `min(1, exp(0.5) x exp(-d^2 / (2 R^2)))`.
- Linear: `max(0, 1 - d / (3 R))`.
- Truncated Linear: `max(0, 1.5 - 1.5 d / (3 R))`.
- Cubic: `max(0, 1 - (d / (3 R))^3)`.
- Quadratic (implementation label): `max(0, 1 - (d / (3 R))^4)`.

The engine samples within a `3R` bounding box, clamps each contribution to
`[0,1]`, and keeps the maximum where profiles overlap. Despite its label,
Quadratic uses a fourth power. The dialog passes a broad `0..100` diameter
filter, so normal profile records are controlled by Stars Filter rather than a
separate shaped-mask size range.

When the effective shaped radius is zero—both the percentage contribution and
Pixel Increase produce `R=0`—the generated mask remains black. With Invert
Selection off, that preserves the starless base and returns no stars; with
Invert Selection on, the black mask selects the complement and returns the full
sum. This differs from an absent profile, which generates no mask and therefore
always takes the full-addition branch.

### Edge tightening, addition, and mask equations

Let `B(x,c)` be starless, `S(x,c)` the original stars layer, `E(S)` the modified
Sobel magnitude, `t` Tighten Edges, and `A(x)` the generated mono mask. The edge
stage is

`S_t = clamp(S - t E(S), 0, 1)` in a normalized domain; for two floating-point
images stating a physical quantity `S_t = S - t E(S)`, not held.

`E` sums the absolute horizontal, vertical, and two diagonal 3x3 responses and
multiplies by `0.05`, independently per stored channel, and reads a constant
zero border beyond the first and last sample of each row. It darkens detected
edges; it is not deconvolution, morphological shrink, or halo-aware fitting.

The additive result is `R = min(B + S_t, 1)` in a normalized domain and
`R = B + S_t` for two floating-point images stating a physical quantity. With
Invert off, `O = B(1-A) + RA`; with Invert on,
`O = BA + R(1-A)`. Mask zero therefore preserves the base and mask one restores
the processed stars, unless inverted.

The result is written in the higher of the two images' representations in the
ranking `sint8 < uint8 < sint16 < uint16 < sint32 < uint32 < float < double`,
with or without a mask. There is no lower clamp, so a negative floating-point
sum remains negative; an integer result saturates when it is written. A signed
integer image is added as a fraction counted from its smallest value.

### Boundary and failure behavior

- A pair of unequal width/height or channel count is refused at execution with
  **the star image does not have the starless image's size and channels**.
- The profile is read on the thread pool, the latest request winning. A file
  that cannot be read is reported under **Load Star Profile** and leaves an
  empty list, so the profile controls are disabled and a run performs the full
  unprofiled addition.
- A valid empty list also creates no mask. OK remains enabled from the image
  selectors alone and performs the full sum.
- Profile identity and geometry are unchecked. Mask raster bounds clip to the
  selected Stars Image; off-frame detections disappear and coordinate mismatch
  selects the wrong content.
- The edge sampler reads a constant zero border at the left and right ends of
  every row, so frame edges can darken slightly when Tighten Edges is nonzero.
- Ordinary active masks are ignored. The generated mask is mono and applies to
  every output channel.
- True per-frame sequence processing is not implemented. A pixel that the
  starless image's, the stars' or the mask's validity rejects keeps the
  starless image's samples; a sample that is not a finite number in a pixel its
  image states valid makes the run fail. The engine's mask painting and
  recombination honour the run's stop.

## Parameter interactions and starting points

| Combination | Conservative starting point | Too weak looks like | Too strong looks like | Adjustment order |
| --- | --- | --- | --- | --- |
| Binary selection | Scale `1.00`, Soften `2.00`, Tighten `0.00` | Selected halos/stars are cut off | Neighboring regions merge; too much background returns | Filter list -> Scale -> Soften -> Tighten |
| Shaped selection | Truncated Bell, percentage `0.50`, pixels `0`, Tighten `0.00` | Small cores or abrupt wings | Broad overlapping restoration | Filter list -> shape -> percentage -> pixels -> Tighten |
| Edge control | Tighten `0.00`, inspect at 100% | Original star halos remain | Dark rims, erased edges, channel fringes | Mask geometry first -> raise Tighten in small steps |
| Complement restoration | Build/check normal mask first; then Invert | Unwanted stars remain excluded | Nearly the entire stars layer/background residual returns | Validate count/overlay -> normal Preview -> Invert |
| v2 elliptical fits | Binary Areas at Scale `1.00` | Circular shaped mask misses elongated wings | Large ellipses merge in crowded fields | Filter FWHM -> Scale -> Soften |

## Inputs, outputs, and metadata

- **Inputs:** one displayed Starless Image, one displayed Stars Image, and an
  optional saved `.anspv1` or `.anspv2` star profile. The two images must match
  in width, height, and stored channel count; profile coordinates are not
  checked against either image.
- **Output:** OK creates one new still named from both inputs with ` - MERGED`.
  It does not modify either input. A selected sequence contributes only its
  currently displayed frame; no output sequence or auxiliary mask view is
  published.
- **Pixel type:** the output is written in the higher of the two images'
  representations, with or without a generated profile mask.
- **Metadata and history:** the output keeps the Starless Image's record - its
  acquisition record, exposure included, header cards, and plate solution -
  with the recombination appended to its lineage, and is marked nonlinear with
  operation `profiled-starmask-recombination`. History records the two image
  names, the `add` operation, the number of profiled stars, how they were
  painted (binary areas with scale and softening, or the shape with its
  percentage and pixel increase), the edge tightening, and the inversion; it
  does not record the profile path. The profile file itself is never rewritten.
- **Temporary data:** mask, edge, sum, and alternate buffers are in memory.
  There is no external process, temporary file, overwrite prompt, or partial
  output to clean up.

## Worked example

For a controlled check, use matching float mono stills with representative
samples `B=0.20`, `S=0.30`, Tighten Edges `0`, and a profile mask that is `A=1`
at a selected star, `A=0.5` at its softened boundary, and `A=0` elsewhere.
The expected non-inverted outputs are `0.50`, `0.35`, and `0.20`. Invert changes
them to `0.20`, `0.35`, and `0.50`.

Before accepting the astronomical result:

- verify the overlay is centered on the intended stars at 100%;
- compare Current Stars before and after filter confirmation;
- inspect bright cores for upper clipping and RGB stars for color fringes;
- inspect left/right frame borders when Tighten Edges is nonzero;
- confirm the result is a new `... - MERGED` still and the input views are
  unchanged; and
- switch to the alternate only to compare against raw full restoration, not to
  judge original versus processed.

## Performance

Mask work grows with star count and covered area. The engine paints the binary
and the shaped masks alike, in parallel over the mask's rows, each pixel keeping
the largest contribution any star makes, so the mask does not depend on the
order of the list or the number of threads; v1 and v2 profiles take the same
path. Large Scale/Pixel Increase values enlarge covered areas and overlaps.
Preview, edge tightening, integer working copies, addition, mask, and display
each require full-frame buffers; peak memory can be several image copies.

OK creates one still even when a sequence view is selected, so there is no
sequence progress dialog. The engine's mask painting and recombination honour
the run's stop. Disable Preview while changing many filter/mask controls on
large images, then re-enable it for a deliberate full-frame check. OK runs on a
worker from a snapshot of the Stars Image, the star list, and the controls
taken when it is pressed, so a later change reaches the next run only.

## Warnings and limitations

- A profile is operationally optional but scientifically required for selective
  restoration; no profile means full addition.
- A failed profile load leaves an empty list and disables the profile controls;
  a run then performs the full addition.
- Profile/image geometry and image-pair compatibility are not prevalidated.
- Tighten Edges affects the entire stars layer before the profile blend and
  reads a constant zero border at each row's ends.
- Preview area crops the stars and the mask to the area; Switch image shows the
  starless image.
- Output is always a still from current displayed images, not a processed
  sequence.
- The output is in the higher of the two images' representations, masked or
  not.
- Reset restores Soften Edges and the other mask controls to their initial
  values and retains the profile, filtered list, selectors, Show Star Profiles,
  and Preview state.
- Random filter selection is drawn from a seed and keeps exactly the share
  asked; filter OK is destructive only to the in-memory list, and the profile
  file is never updated here.
- OK reads the inputs and settings when it is pressed; a later change reaches
  the next run only.

## Troubleshooting

| Symptom | Cause | Fix |
| --- | --- | --- |
| Every star returns | No/empty profile or a failed load at execution creates no mask; an inverted black mask also selects the full sum | Reload a valid profile, confirm Current Stars > 0, Preview with Invert Selection off, and inspect the mask effect |
| No stars return in shaped mode | Effective shaped radius is zero and Invert Selection is off, producing a black mask | Raise % Pixel Increase or Pixel Increase; preview the mask before OK |
| Overlay/mask misses stars | Profile came from different geometry or branches were cropped/resampled | Use the profile and pair from the same unchanged registered image |
| **the star image does not have the starless image's size and channels** | Width, height, or channel count differs | Match all three properties; do not rely on selectors |
| **Load Star Profile** reports an error | The file is not a readable `.anspv1`/`.anspv2` profile; the list is now empty | Load a valid profile; a run without one performs the full addition |
| Dark rims or colored borders | Tighten Edges too high, or the zero border at each row's ends | Reduce Tighten Edges; inspect 100% frame borders and channels |
| Shaped v2 mask looks round | Shaped mode uses max FWHM as a circular radius | Use Binary Areas to preserve fitted axis ratio/angle |
| Percent `0.50` makes masks smaller | It multiplies radius by one half | Raise toward `1.00` and/or add Pixel Increase |
| Result is held at one in bright cores | The pair includes an integer or normalized image, so both are read as fractions of full scale and the sum is held at one | Expected for such a pair; two floating-point images read from files keep their sum above one; reduce the stars layer upstream if needed |

## Related workflow steps

- [Star Profiler](../registration-alignment/star-profiler.md) — create, inspect,
  filter, and save v1/v2 profiles.
- [Star Mask](star-mask.md) — generate binary or shaped masks directly from an
  image without a saved profile.
- [Starmask Recombination](starmask-recombination.md) — add a full stars layer
  with an optional user-supplied mask.
- [Star Reduction](star-reduction.md) — reduce stars against a matching
  starless reference rather than restoring a selected profile.
- [Glossary](../../reference/glossary.md) — image-state, profile, mask, and
  clipping terminology.

## Credits

- The shaped profile masks use the engine's star-mask shapes, adapted from
  [DeepSkyStacker](https://github.com/deepskystacker/DSS)'s star-mask shapes under
  the BSD 3-Clause License of Luc Coiffier and the DeepSkyStacker contributors,
  whose licence notice **About > About AfterNight** reproduces with its credit.
