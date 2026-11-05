# Narrowband Palette Composer

## Purpose and expected result

Narrowband Palette Composer prepares selected Ha, OIII, SII, and optional OSC
stills, calculates 12 fixed palette formulas, and presents them as a synchronized
grid or a large single preview. Per-role intensity controls multiply prepared
Ha/OIII/SII values before mapping, clipping, and final global peak
normalization.

The expected Apply result is one new 32-bit float RGB still named from the
selected OIII, then Ha, then SII source and palette—for example,
`OIII - SHO Narrowband Palette`. OSC-only input falls back to
`<palette> Narrowband Palette`. Sources remain unchanged.

This is preset formula exploration, not a physical spectral model. Role names
are user assertions: the process does not read filter identities, passbands,
exposures, WCS, registration, or PSFs, and it does not provide a custom matrix
editor.

## At a glance

| Item | Current behavior |
| --- | --- |
| Menu path | **Process > Color Spaces > Narrowband Palette Composer** |
| Enabled when | Always available as a top-level process; it can open without an active image or any open images |
| Inputs | OIII plus Ha or SII after optional OSC synthesis; all selectors accept open grayscale or RGB stills and may reuse the same view |
| Outputs | One new float RGB still for the selected palette; no source replacement, auxiliary channels, snapshot, report, preset file, or direct disk output |
| Recommended image state | Registered/cropped linear masters with gradients corrected; leave Input autostretch on for linear data unless you intentionally prepared display-ready inputs |
| Preview support | Automatic Palette Grid and Single Preview, built on a worker under an operation of their own once the inputs are chosen - the latest request wins and nothing older is shown; all 12 palettes at full resolution as 8-bit display conversions; no standard viewer/histogram/autostretch toolbar |
| Sequence support | Not supported by any source selector; no target sequence is needed to open the top-level window |
| Mask support | Not supported; the process explicitly disables ordinary target masking |
| External dependencies | No runtime tool, Python environment, model, catalog, or network service; SetiAstro lineage is recorded under [References and provenance](#references-and-provenance) |

## Before you start

- Register and crop sources to one geometry whenever possible. **Match sizes to
  reference** only resizes width/height; it does not align stars, preserve aspect
  ratio, inspect WCS, or match PSFs.
- Select semantic roles carefully. RGB images in Ha/OIII/SII use red only. OSC1
  synthesizes Ha from red and OIII from the green/blue mean; OSC2 synthesizes
  SII from red and OIII from the green/blue mean.
- Decide whether input pixels are linear. **Input autostretch** applies a fixed
  unlinked AfterNight stretch with shadows clipping `-2.8` and target median
  `0.25`, derived from the exact median and median absolute deviation of every
  sample; neither value is exposed in this window.
- Expect every palette to receive a global peak normalization. Even with Input
  autostretch off and intensities at 100%, Apply does not preserve the absolute
  scale of the masters.
- Remove unused selections rather than treating them as passive metadata. Every
  selected direct/OSC source participates in synthesis and in the metadata
  built for the output.

## Procedure

1. Open the registered Ha, OIII, SII, and optional OSC stills, then choose
   **Process > Color Spaces > Narrowband Palette Composer**. No active target is
   required.
2. Select OIII plus Ha or SII. Alternatively, select OSC1/OSC2 sources that
   synthesize those roles. Watch **Status** and the warning text for RGB channel
   extraction, mono OSC broadcast, or resize notes.
3. Leave **Input autostretch** checked for linear masters. Turn it off for data
   already stretched or when you explicitly want only formula/clipping/global-
   normalization behavior.
4. Prefer same-size sources. If they differ, leave **Match sizes to reference**
   checked only when simple resampling is acceptable; full UI output uses Ha as
   dimension reference, then OIII, then SII.
5. In **Palette Grid**, select a tile to choose Apply's palette. Use the wheel
   and drag to inspect the same location/zoom across all formulas, or switch to
   **Single Preview** and choose from its bottom gallery.
6. Adjust Ha, OIII, and SII intensity from `100%` one role at a time. The grid
   rebuilds all 12 formula outputs after each committed slider/spin change.
7. Inspect clipped highlights, noisy weak roles, and star alignment at the same
   zoom. The previews are normalized 8-bit displays of the float palette result,
   not a separate display STF.
8. Select **Apply** and wait for the named RGB view. Apply runs in a background
   worker but exposes no progress dialog or user Cancel action.

## Complete parameter and action reference

| UI label/action | Type and unit | Runtime default | Persisted-default exception | Valid values, step, or constraints | Effect of increasing, decreasing, or selecting | Dependencies and enable/visibility rules | Quality, artifact, runtime, memory, or disk trade-off | Scope: preview/apply, channel, mask, still/sequence | Persistence |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Ha | Image selector, semantic role | **No image** | None; top-level process has no active-target auto-selection | Open grayscale/RGB still or **No image**; duplicates allowed | Supplies explicit Ha; RGB uses red; OSC1 contribution averages 50/50 with it | Readiness also needs OIII; if absent, prepared SII substitutes in Ha formula positions | Wrong role/misregistration creates false color; source preparation/cache costs full image | Grid/Apply; Ha role; still only; no mask | Window lifetime; tracks create/close/rename; Reset/success retains selection |
| OIII | Image selector, semantic role | **No image** | None | Open grayscale/RGB still or **No image** | Supplies required explicit OIII; RGB uses red; OSC contributions blend into it | OIII must exist after direct/OSC synthesis | Central to every formula; weak/noisy OIII can dominate cyan/blue after normalization | Grid/Apply; OIII role | Window lifetime; Reset/success retains selection |
| SII | Image selector, semantic role | **No image** | None | Open grayscale/RGB still or **No image** | Supplies explicit SII; RGB uses red; OSC2 contribution averages 50/50 with it | Readiness also needs OIII; if absent, prepared Ha substitutes in SII formula positions | Substitution makes SII-labelled formula positions reuse Ha, not an inferred sulfur signal | Grid/Apply; SII role | Window lifetime; Reset/success retains selection |
| OSC1 | Image selector, helper role | **No image** | None | Open RGB/mono still or **No image** | Synthesizes `Ha=R` and `OIII=(G+B)/2`; mono is broadcast to RGB first | May satisfy both readiness sides by itself | Mixing OSC with explicit roles uses fixed 50/50 blends and can transfer broadband stars/gradients | Grid/Apply; Ha/OIII synthesis | Window lifetime; Reset/success retains selection |
| OSC2 | Image selector, helper role | **No image** | None | Open RGB/mono still or **No image** | Synthesizes `SII=R` and `OIII=(G+B)/2`; mono is broadcast first | May satisfy both readiness sides by itself | If OSC1 and OSC2 both feed OIII, sequential averaging weights the later OSC2 contribution more | Grid/Apply; SII/OIII synthesis | Window lifetime; Reset/success retains selection |
| Input autostretch | Checkbox, fixed unlinked STF | Checked | Hidden defaults: target median `0.25`, shadows clipping `-2.8` | Checked/unchecked | Checked stretches each prepared direct mono role independently and each OSC RGB channel independently before OSC extraction/mixing | Source/preparation change triggers a full grid rebuild | Makes linear signal visible but permanently changes palette pixels; unsuitable for already-stretched data; statistics/stretch cost per source | Grid/Apply before synthesis; no mask | Window lifetime; Reset/success checks it |
| Match sizes to reference | Checkbox | Checked | None | Checked/unchecked | Checked resizes prepared roles to Ha dimensions, then OIII, then SII; unchecked rejects remaining mismatches | OSC synthetic contribution may still be resized to an existing explicit channel before this policy is evaluated | Resampling changes sampling/PSF and can distort aspect ratio; unchecked is safer for detecting bad geometry | Grid/Apply; all prepared roles | Window lifetime; Reset/success checks it |
| Mode | Combo box | **Palette Grid** | None | **Palette Grid**, **Single Preview** | Changes presentation only; selected palette remains synchronized | Both modes always receive all 12 built frames | No engine rebuild; Single Preview devotes more screen area to one result | Preview display only | Window lifetime; Reset/success returns Palette Grid |
| Palette Grid tiles | Twelve selectable preview cards | **SHO** selected | None | SHO, HOO, HSO, HOS, OSS, OHH, OSH, OHS, HSS, Realistic1, Realistic2, Foraxx | Clicking a tile selects Apply palette and begins possible pan | Frames appear only after readiness/build; descriptions appear as hover tooltips | All frames are full-resolution 8-bit display copies of 12 float RGB results | Preview selection; Apply uses selected id | Selection shared with Single Preview; Reset/success selects SHO |
| Palette Grid mouse wheel | Shared zoom gesture | `1x`, centered | None | `1.15^wheel_steps`, clamped `1x..32x` | Zooms the same source crop across every tile | Requires built frames | High zoom helps registration/clipping checks; no pixel recomputation | Preview display only | Resets when frames rebuild or double-clicked |
| Palette Grid left-drag | Shared pan gesture and selection | Centered | None | Drag any tile after selecting it | Moves shared normalized view center for every tile | Panning acts only above `1x` | Enables formula comparison at identical coordinates | Preview display only | Until rebuild/double-click |
| Palette Grid double-click | Reset gesture | — | None | Left double-click | Resets shared zoom to `1x` and center to image midpoint | Grid visible | Display-only | Preview display only | Immediate |
| Single Preview gallery | Twelve selectable thumbnails | **SHO** selected | None | Same 12 palettes | Chooses large preview and Apply palette; tooltip shows formula description | Single Preview mode; selection synchronized with grid | Gallery thumbnails use same built frames; no further engine run | Preview selection | Shared/window; Reset/success selects SHO |
| Single Preview main image | Large 8-bit presentation | Selected full-frame palette | None | Current selected palette | Displays a larger aspect-fitted view | Single Preview mode and built frames | Display conversion may downsample to fit; float Apply output is unchanged | Preview display only | Replaced on grid rebuild |
| Single Preview mouse wheel | Zoom gesture | `1x`, centered | None | Pointer over main image; `1.15^steps`, `1x..32x` | Zooms selected main image | Does not zoom gallery | Display-only inspection | Preview display only | Until rebuild/double-click |
| Single Preview left-drag | Pan gesture | Centered | None | Drag main image | Pans selected preview at zoom > `1x` | Main image only | Display-only inspection | Preview display only | Until rebuild/double-click |
| Single Preview double-click | Reset gesture | — | None | Double-click main image | Resets zoom/center | Main image only | Display-only | Preview display only | Immediate |
| Ha | Linked slider + spin box, percent | `100%` | This is the intensity row, distinct from Ha source selector | `0..200%`; step `1`; slider page `10`; slider tracking off | Multiplies prepared/substituted Ha before selected formula | Spin updates immediately; slider commits value changes without continuous tracking | High values clip before normalization; `0` removes Ha-role contribution, including substituted SII in Ha positions | All 12 grid formulas and Apply; semantic Ha factor | Window lifetime; Reset/success returns `100%` |
| OIII | Linked slider + spin box, percent | `100%` | Intensity row | `0..200%`; step `1`; slider page `10`; tracking off | Multiplies required prepared OIII before mapping | Same synchronization as Ha | High values often drive cyan/blue clipping and global scale; `0` can collapse OIII-dependent output | All palettes/Apply; OIII factor | Window lifetime; Reset/success returns `100%` |
| SII | Linked slider + spin box, percent | `100%` | Intensity row | `0..200%`; step `1`; slider page `10`; tracking off | Multiplies prepared/substituted SII before mapping | Same synchronization as Ha | High values clip SII positions; with SII missing, this scales substituted Ha in SII positions | All palettes/Apply; semantic SII factor | Window lifetime; Reset/success returns `100%` |
| Status | Read-only text | “Select OIII plus Ha or SII…” | None | Readiness/updating/ready messages | Reports missing roles, grid update, or selected-palette Apply readiness | Changes with selection/build state | Diagnostic only; not a progress percentage | Window state | Recomputed; not saved |
| Warning details | Read-only wrapped text | Empty | None | Preparation warnings or caught build error | Reports RGB-red extraction, mono OSC broadcast, synthesis resize, final reference resize, or grid failure | Cleared on invalid readiness/new request; successful grid uses first result's preparation warnings | Warnings can explain silent-looking role/resample changes; Apply itself has no report | Grid/preparation diagnostics | Recomputed; not saved |
| Busy spinner | Activity indicator | Inactive | None | Automatic during grid rebuild | Signals the worker's preparation and 12-palette build | The window stays responsive; a change during a build starts the next build, with the latest values, when this one lands | Does not provide percentage or cancellation | Preview-grid update only | Per rebuild |
| Apply | Button/action | Disabled until required synthesized roles exist | None | One action; disabled during worker execution | Captures current selected formula, settings, source names, and final output name, then creates a new RGB still in background | The selectors, preparation controls, preview selection, intensities, Apply, Reset, and Close are disabled until completion; the captured attachment/lock reference is OIII, then Ha, SII, OSC1, OSC2 | Full-resolution prepare/map/normalize cost; no overwrite or direct disk use | Apply; still sources; no mask/sequence | Successful Apply retains selectors but resets options, mode, palette, intensities |
| Reset | Button/action | Enabled while idle | None | One action | Restores Input autostretch/Match sizes on, Grid mode, SHO, and all intensities to 100% | Does not clear source selectors or preview view center directly; disabled during Apply; emitted changes can rebuild grid | May trigger expensive rebuilds | Configuration | Immediate; no saved process instance |
| Close | Button/action | Enabled while idle | None | One action | Closes top-level process | Button is disabled during Apply; a window-close request still waits on the execution lock | Wait for the current calculation to finish | Dialog only | Ends window-local state/cache |
| Mask / snapshot / progress / Cancel | Absent facilities | Not available | None | — | No target mask blend, standard Switch image/Preview area/Take snapshot, Apply progress, or user cancellation | Custom preview and top-level process intentionally omit these controls | Protect/localize inputs upstream; wait for builds/runs to finish | All operations | None |

## How the algorithm works

### Input preparation and channel synthesis

Every selected source converts to normalized 32-bit float. Direct Ha/OIII/SII
mono is retained; direct RGB uses red and posts a warning. OSC RGB is retained;
mono OSC broadcasts its one value into R/G/B and posts a warning.

When **Input autostretch** is checked, AfterNight calculates MAD statistics and
applies its unlinked automatic stretch with shadows clipping `-2.8` and target
background `0.25`. Direct RGB narrowband is reduced to red before this mono
stretch. OSC is stretched per RGB channel before synthesis.

Prepared OSC roles are:

\[
Ha_{OSC1}=R_1,\quad SII_{OSC2}=R_2,
\quad OIII_{OSCn}=\frac{G_n+B_n}{2}.
\]

If an explicit role already exists, its synthetic addition is a fixed
`0.5 explicit + 0.5 synthetic` blend. OSC1 is processed before OSC2, so with an
explicit OIII and both helpers the final OIII weights are `0.25 explicit +
0.25 OSC1 + 0.50 OSC2`. A synthetic contribution with different dimensions is
resized to the existing explicit role even when **Match sizes to reference** is
off; a warning records this synthesis-specific resize.

After synthesis, OIII is mandatory and at least one of Ha/SII must exist. For
formula mapping only, absent Ha uses prepared SII and absent SII uses prepared
Ha. Their intensity controls remain semantic: Ha intensity scales the substitute
used in Ha positions; SII intensity scales the substitute used in SII positions.

If final prepared dimensions differ and matching is on, full UI preview/Apply
uses Ha dimensions, otherwise OIII, otherwise SII. Downsampling in either
dimension selects area interpolation; pure enlargement selects bilinear. This
can resize the other dimension and does not preserve aspect ratio automatically.

### Palette formulas

Let `H`, `O`, and `S` be the prepared Ha, OIII, and SII samples after their
respective intensity factors. Substitution has already occurred. Each row below
produces raw `(R,G,B)` before clipping and final peak normalization.

| Palette | Red | Green | Blue | Notes |
| --- | --- | --- | --- | --- |
| SHO | `S` | `H` | `O` | Sulfur–hydrogen–oxygen direct assignment |
| HOO | `H` | `O` | `O` | OIII supplies green and blue |
| HSO | `H` | `S` | `O` | Direct assignment |
| HOS | `H` | `O` | `S` | Direct assignment |
| OSS | `O` | `S` | `S` | SII supplies green and blue |
| OHH | `O` | `H` | `H` | Ha supplies green and blue |
| OSH | `O` | `S` | `H` | Direct assignment |
| OHS | `O` | `H` | `S` | Direct assignment |
| HSS | `H` | `S` | `S` | SII supplies green and blue |
| Realistic1 | `0.5H + 0.5S` | `0.3H + 0.7O` | `0.1H + 0.9O` | Fixed SetiAstro weighted blend |
| Realistic2 | `0.7H + 0.3S` | `0.3S + 0.7O` | `O` | Fixed SetiAstro weighted blend |
| Foraxx, Ha present and SII absent | `H` | `U H + (1-U)O` | `O` | Two-channel branch, `U=(HO)^(1-HO)` |
| Foraxx, otherwise | `T S + (1-T)H` | `U H + (1-U)O` | `O` | `T=C^(1-C)`, `C=clip(O,10^-6,1)`; `U=(HO)^(1-HO)` |

The Foraxx branch decision uses actual prepared availability after OSC synthesis,
before missing-role substitution. Thus OSC1 can count as Ha and OSC2 as SII.

### Clipping and global normalization

Every raw mapped component is independently converted with this policy before
normalization:

- non-finite value → `0`;
- value below `0` → `0`;
- value above `1` → `1`;
- otherwise unchanged.

The engine then finds one maximum `M` across every pixel and all three channels.
If `M>0`, every component is divided by `M` and clamped again. Consequently,
each nonblack result has at least one sample at 1. Intensities change relative
role contributions, but clipping occurs first: once a raw component reaches 1,
raising it further cannot increase the normalization maximum. When every
intensity is zero, output remains black because `M=0` skips division.

## Parameter interactions and starting points

| Combination | Start with | What it controls | Risk or diagnostic |
| --- | --- | --- | --- |
| Input autostretch + linear sources | On | Fixed per-source/per-channel display preparation | Turning it on for stretched data applies another nonlinear transform |
| Direct roles + OSC helpers | One source path first | Fixed synthesis and 50/50 averaging | Both OSC helpers make OIII averaging order-dependent; metadata exposures are not weighted by synthesis coefficients |
| Missing Ha/SII + intensity | 100% | Substituted formula positions | Label remains Ha/SII even though the same other role supplies both positions |
| Match sizes + mixed geometry | Prefer pre-registration | Pixel dimensions only | Area/bilinear resize cannot repair translation, rotation, distortion, crop origin, or PSF |
| Palette + intensity | Select formula, then tune one role | Matrix input values | Global normalization can hide overall scale changes; inspect relative channels and clipping |
| Intensity above 100% + clipping | Raise slowly | Pre-matrix role strength | Values clip to 1 before peak normalization, flattening highlights and reducing further response |
| Palette Grid + Single Preview | Either | Presentation and selection | Same 12 frames; switching modes does not recalculate or change Apply formula |
| Grid spin edits + full resolution | Coarse adjustments | Rebuild frequency | Each committed change remaps/normalizes 12 full RGB images on a worker; the latest change wins |

## Inputs, outputs, and metadata

- Selectors accept valid mono/RGB stills of mixed pixel types and dimensions.
  Invalid/unsupported images, missing synthesized requirements, disabled resize
  with remaining mismatches, invalid hidden stretch options, and invalid
  intensity factors are rejected. UI intensity values are finite `0..2`.
- Apply always creates one float RGB still. Naming prefers the selected OIII
  view, then Ha, then SII. OSC names are not used as stems, so OSC-only output is
  simply `<palette> Narrowband Palette`. The final name is captured with the
  pixel parameters before the background worker starts.
- The result's metadata is built from every selected source in role order Ha,
  OIII, SII, OSC1, OSC2. Its exposure is the sum of the exposures they state,
  recorded once. The object, instrument, telescope, observer, filter, sensor
  temperature, gain, binning, and exposure start are kept when every selected
  source states the same value and dropped otherwise, so a filter is kept only
  when every source shares it.
- The result keeps the header cards every selected source states identically,
  but no exposure, processing-state or identity card of a source; a card the
  sources state differently, or only some state, is dropped. It keeps the first
  selected source's plate solution with its world coordinate (WCS) cards only
  when the result keeps that source's size. When it does not - the first source
  was resized to the reference's size, or the reference was synthesized from a
  one-shot-colour source of another size - the result carries no solution,
  because that solution would label another pixel grid.
- The output is always marked nonlinear with operation provenance
  `narrowband-palette-composer`, including when Input autostretch is off.
- Apply records “Created `<palette>` Narrowband Palette Composer image,” then
  asynchronously resets preparation, presentation, palette, and intensities.
  Source selections remain.
- Palette Grid preparation warnings are visible only in the open process.
  Apply does not create a report or attach synthesis coefficients to output
  metadata.

## Preview and execution behavior

Every source, preparation, or intensity change rebuilds the prepared Ha/OIII/SII
channels and all 12 palettes. The grid is not built from reduced-resolution
thumbnails: each float result is full resolution and is converted to an 8-bit
QImage for the grid and single preview; Apply independently rebuilds the
selected float result.

Grid generation runs on the thread pool, as one operation that prepares the
channels and maps all formulas. The window shows a busy spinner and stays
responsive; a change made during a build starts the next build, with the latest
values, when that one lands, and only the latest grid is shown. Closing the
window stops a build; there is no Cancel action. Apply is asynchronous but also
has no progress object, so the engine's cooperative cancellation path is not
exposed to the user.

Because this top-level window uses custom preview widgets, it has no target
selector, display autostretch, histogram, Switch image, Preview area, Take
snapshot, or mask blending. Selecting the first usable source chooses an
internal attachment/lock reference in priority order OIII, Ha, SII, OSC1,
OSC2; this is distinct from the Ha-first resize reference and OIII-first output
name.

## Performance

Preparation scales with pixels across all selected direct and OSC sources.
Autostretch adds MAD statistics and a full transform per direct mono role or per
OSC RGB channel. Synthesis, channel extraction, averaging, and resizing add
more float mono buffers. Each palette performs a per-pixel matrix/Foraxx map,
clipping, a serial global-maximum scan, and a parallel normalization pass.

The grid maps 12 palettes in parallel with oneTBB and keeps 12 full-resolution
float RGB outputs at once before creating display QImages for both presentations.
Every rebuild prepares the channels again and allocates the 12 outputs. Apply
builds another selected output in a background thread. Peak
memory therefore scales roughly with source pixels plus prepared mono channels,
12 float RGB palette images, and 8-bit preview frames. Close unused images,
avoid unnecessary OSC/direct duplicates, and commit coarse intensity changes
instead of stepping through many spin values. No approved dataset was timed.

## Worked example

- **Evidence mode:** Source- and focused-test-derived; not executed on
  project-owned astronomical data.
- **Prepared samples:** Ha `H=0.20`, OIII `O=0.40`, SII `S=0.80`; Input
  autostretch off; intensities all `100%`.
- **SHO matrix:** Raw RGB is `(S,H,O)=(0.80,0.20,0.40)`.
- **Clipping checkpoint:** All components are finite and within `[0,1]`, so none
  changes.
- **Normalization checkpoint:** Global maximum is `M=0.80`; division gives
  `(1.00,0.25,0.50)`.
- **Interpretation:** These exact values reproduce the focused SHO engine
  regression. They show why even sub-unit inputs are rescaled; they do not claim
  that this single-pixel ratio is suitable for a real palette.

## Warnings and limitations

!!! warning "Every preview change builds 12 full-resolution palettes"
    The grid is built on a worker while the window stays responsive. Every
    change prepares the channels again and rebuilds mapping, clipping,
    normalization, float outputs, and display frames for every formula. There is
    no preview Cancel.

!!! warning "Peak normalization follows clipping"
    Components above 1 are clipped before the shared maximum is measured. This
    prevents a strong outlier from setting a maximum above 1, but it also makes
    intensity response saturate and can flatten highlights.

!!! warning "Resize is not registration"
    Match sizes changes dimensions only and can alter aspect ratio. It does not
    align stars, preserve WCS, model distortion, or match focus/PSF. Prepare
    registered geometry upstream.

!!! warning "Apply has no progress or Cancel"
    Apply runs in the background. Source selectors, preparation controls,
    preview selection, intensities, Apply, Reset, and Close are disabled after
    parameters and the output name are captured. They are restored when the
    worker finishes; there is still no progress percentage or Cancel action.

## Troubleshooting

| Symptom | Likely cause | Fix |
| --- | --- | --- |
| Window opens with empty selectors | This top-level process does not auto-select an active view | Choose sources explicitly; open supported mono/RGB stills if lists are empty |
| Apply is disabled | Synthesized inputs lack OIII or lack both Ha and SII | Select OIII plus Ha/SII, or use OSC1/OSC2 to synthesize the roles |
| Warning says RGB source uses red | RGB was selected in a direct narrowband role | Use a mono extracted master if red-only behavior is unintended |
| OSC contribution looks wrong | OSC1/OSC2 semantic role is wrong, mono was broadcast, or explicit/OSC data were averaged | Verify role and input color format; remove helpers while diagnosing |
| OIII seems biased toward OSC2 | Explicit OIII, OSC1, and OSC2 are averaged sequentially | Use one helper or precombine externally with deliberate weights |
| Build fails with Match sizes off | Prepared channel dimensions differ after synthesis | Register/crop to exact dimensions, or enable matching only if simple resize is acceptable |
| Image is stretched/distorted but stars still do not align | Match sizes resampled dimensions without registration | Register and crop sources upstream; do not use resize as alignment |
| Preview looks much brighter than source | Input autostretch and mandatory peak normalization both change levels | Turn off Input autostretch if sources are already stretched; remember peak normalization always remains |
| Raising intensity stops changing highlights | Formula components already clipped to 1 before global normalization | Lower the dominant role and rebalance other roles |
| Missing SII palette resembles Ha | Engine substitutes Ha into SII formula positions | Supply real SII or choose an HOO-style formula intentionally |
| Palette selection changes but grid does not rebuild | Selection only chooses an already-built formula | This is expected; intensity/source/preparation changes trigger rebuild |
| The grid takes a while to update after a spin/selector change | All 12 full-resolution palettes are rebuilt on a worker | Wait for the spinner to clear; make coarse changes and reduce source dimensions upstream for experiments |
| Selectors, previews, intensities, or dialog buttons are disabled | A non-cancellable Apply is running | Let Apply finish before making another change |
| Output name lacks OSC source | Naming uses only OIII, Ha, then SII selector names | Rename the output after Apply if OSC-only naming needs context |

## Related workflow steps

- [Narrowband Imaging](../../workflows/narrowband-imaging.md) — calibration,
  registration, gradients, normalization, palette choice, and later stretching.
- [Image Registration](../registration-alignment/image-registration.md) — align
  source geometry before palette resampling/composition.
- [Linear Match](../color-calibration/linear-match.md) — prepare gross channel
  scale relationships before fixed formulas when appropriate.
- [Channel Composition](channel-composition.md) — direct user-chosen channel
  assignment without palette grid, OSC synthesis, or mandatory peak normalize.
- [NB Color Mapper](nb-color-mapper.md) — per-layer HSV mapping, modifiers, and
  screen mixing instead of fixed matrices.
- [NBRGB Combination](nbrgb-combination.md) — add continuum-corrected
  narrowband structure to broadband RGB.
- [Image Viewer](../../interface/image-viewer.md) — inspect the created float RGB
  still with normal display tools after Apply.
- [Glossary](../../reference/glossary.md) — SHO, HOO, palette, linear/nonlinear,
  autostretch, clipping, and normalization terminology.

## References and provenance

- AfterNight source and focused tests.
- `docs/dev/NARROWBAND_PALETTE_COMPOSER_PROVENANCE.md` records the reviewed
  SetiAstro SASPro source snapshot and hashes, Franklin Marek attribution, and
  GPL-3.0 license. AfterNight implements the behavior with native C++ engine and
  Qt controls; Python runtime objects, PyQt widgets, icons, and the upstream SASPro
  UI assets are not included.
- The same attribution and port boundary are exposed in **About > Licenses**.
