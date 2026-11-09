---
kind: workflow
stages: [calibrate, register, stack, color, noise, stretch, finish]
summary: >-
  Turn mono narrowband or dual-band color captures into a mapped-color image, from calibration to the finished
  picture.
---
# Narrowband Imaging

## Purpose, audience, prerequisites, and expected result

Use this workflow to turn mono emission-line or narrowband OSC captures into
traceable mapped-color images. It is for users who can classify calibration and
light frames, inspect linear images, and distinguish a display stretch from a
pixel-changing stretch. It gives concrete starting points and checkpoints, not
a promise that one palette or frame count suits every target.

You need:

- mono Ha/OIII/SII or other clearly identified filter data, or a calibrated and
  demosaiced narrowband OSC master whose passbands you understand;
- calibration frames matched to camera mode, exposure/temperature policy,
  binning, and filter/optical train;
- consistent filter labels, geometry, session, gain/ISO, offset/readout mode,
  object, and CFA metadata where applicable;
- a writable output directory outside the source tree, with room for masters,
  registered lights, history, reports, and optional diagnostics; and
- for continuum or broadband-star branches, registered same-target broadband
  data on the same pixel grid.

Expected branch products are:

| Branch | Required linear inputs | Branch product and preservation point |
| --- | --- | --- |
| Mono HOO | Ha + OIII masters | Registered/matched channels and an HOO composition; retain both mono masters |
| Mono SHO/custom | Ha + OIII + SII masters | Registered/matched channels and SHO, Foraxx, or custom mapped-color variants; retain all mono masters |
| Narrowband OSC | Calibrated/demosaiced RGB master, optionally direct mono roles | A palette-composer result after explicit OSC1/OSC2 role review; retain the original RGB master |
| NBRGB continuum enhancement | Registered linear RGB plus one or more mono narrowband masters | An RGB result with selected channels enhanced; retain RGB and narrowband inputs |
| Star-separated finish | Stretched color image plus a verified star mask/star image or optional external extraction | Matched starless/stars products and a recombined master; retain the pre-separation image |

## Verification data and evidence mode

| Item | Record |
| --- | --- |
| Project-owned sample set | The mono SHO data set of M16 captured on 2025-06-18: maintainer-supplied project-owned mono Ha/OIII/SII lights and per-filter flats. Permission covers validation and retention of sanitized settings, hashes, observations, and derived evidence. |
| Branches covered | Physical mono SHO path: eight lights plus eight flats per Ha/OIII/SII group, 48 selected inputs. Direct SHO/HOO/Foraxx/custom, matching, native star, export, recovery, and negative paths were executed in the historical partial run. OSC1 and NBRGB arithmetic used same-target derivatives; representative physical OSC and matched broadband-continuum branches remain blocked. |
| External tools/models | None. StarNet, DeepSNR, and other optional executables/models were not used. |
| Catalogs/network | None. Gaia enrichment, online solving, response packs, catalogs, and network access were disabled/not used. |
| Platform/application build | Historical partial execution: Windows MSVC x64 Release and Debug, Qt 6.10.3. The blocked-remediation numbered builds and complete-suite results are retained separately in the evidence record. |
| Evidence artifacts | Sanitized frame IDs/hashes, historical partial-run binary/source/output/report hashes, exact settings, stage results, warnings, bounded image observations, export/reopen, recovery evidence, exact focused commands, and blocked-remediation validation are in the guide's validation record. Private paths, reports, logs, manifests, and generated images remain only in isolated approved output trees. |
| Unverified claims excluded | Physical narrowband OSC acquisition/calibration/demosaic/sensor behavior and matched broadband-continuum production behavior remain unverified, so verification of this workflow is incomplete. No claim is made that eight frames or recorded values are production-optimal; that uncalibrated flats are recommended; or that external tools, other platforms, polished aesthetics, or printing were accepted. |

The retained mode-E run proves only the recorded mono branch and bounded proxy
artifacts. It does not satisfy the mandatory representative-data gate for the
OSC and continuum branches. Apply the checkpoints to your data instead of
copying the values blindly.

## Inputs and outputs

### Acquisition assumptions and organization

Keep each emission line and calibration relationship explicit. Folder names do
not control the workflow after import, but a stable structure helps prevent an
Ha flat from silently serving OIII or SII:

```text
project/
  lights/Ha/
  lights/OIII/
  lights/SII/
  lights/OSC/                 optional dual/tri-band branch
  calibration/bias/
  calibration/dark/<exposure>/
  calibration/flat/Ha|OIII|SII/
  calibration/dark-flat/
  broadband/RGB/              optional continuum or star branch
  output/                     keep outside source folders
```

| Input | Match or record | Reject or isolate when |
| --- | --- | --- |
| Mono lights | Filter, object, dimensions, binning, gain/offset/readout, exposure, temperature/session, time | Cloud, tracking, focus, saturation, framing, or metadata would weaken registration/normalization |
| OSC lights | Same capture fields plus CFA pattern and passband/filter identity | CFA is unknown, frames were pre-demosaiced inconsistently, or different dual-band filters are mixed |
| Bias/dark/dark-flat | Camera mode, binning, dimensions; dark exposure and temperature policy | A fallback is only dimensionally similar or would scale an unsuitable sensor pattern |
| Flats | Filter, optical train/orientation, focus, camera mode, binning, dimensions | Dust/focus/rotation/filter changed, or the calibration master is missing without an explicit warning decision |
| Broadband RGB/stars | Same target, session provenance, compatible PSF, registered/cropped geometry | Stars double, halos shift, or a different field/crop is being treated as equivalent |

Image Integration does not modify source frames. It can create calibration
masters, registered lights, normalization artifacts, FITS masters, previews,
history snapshots, logs, manifests, and HTML/Markdown reports. Rejection maps,
drizzle products, and debug-calibrated intermediates increase disk use.

### Linear/nonlinear state and precision

- Calibrate, register, normalize, stack, correct gradients, and match intensity
  while the mono/OSC data is linear.
- OSC calibration must occur before demosaic. Do not debayer bias, dark, or flat
  frames just to make them display in color.
- Viewer autostretch is inspection only. Saving an image does not bake the
  viewer stretch into its pixels.
- Channel Composition can preserve a linear handoff. Narrowband Palette
  Composer and NB Color Mapper process outputs are tagged nonlinear by their
  process contracts; consult their pages before feeding them to a step that
  requires linear data.
- Keep high-precision FITS/XISF working masters. Make PNG/JPEG delivery copies
  from a deliberately stretched copy, not from the only linear master.

## Workflow map and decision points

The release guide does not include a diagram of this workflow. The physical
narrowband OSC and matched broadband-continuum branches have not passed their
representative-data gate, so a combined visual flow would imply verification
that the current evidence does not provide. The decision table below remains
available as source-derived planning guidance; see **Verification data and
evidence mode** for the exact boundary.

| Decision | Choose this path | Check before continuing |
| --- | --- | --- |
| Full Image Integration or Lite | Use full Image Integration for per-filter groups, explicit matching/settings, one shared registration group, reports, history, or incremental reruns. | Execution Plan lists the intended Ha/OIII/SII groups and output names |
| Shared or separate registration | Use one shared reference when all channels cover the same field and sampling. Use separate groups only when you will explicitly register resulting masters afterward. | Blink stars and edges; equal dimensions are not proof of alignment |
| Ha or another reference | Ha often supplies abundant structure, but choose the cleanest structurally representative frame/master. | Reference is not clouded, saturated, trailed, or near a bad edge |
| Direct mapping or palette process | Channel Composition for exact RGB role assignment; Palette Composer for its 12 fixed formulas and OSC synthesis; NB Color Mapper for flexible hue/saturation/layer blending. | Inputs satisfy each process's state, geometry, and OIII requirements |
| HOO, SHO, or custom | HOO needs Ha/OIII; SHO needs SII/Ha/OIII; custom mapping is useful when fixed matrices hide weak structures. | Histograms are not clipped and no one channel dominates only because it was left unmatched |
| Continuum enhancement | Use NBRGB only with a real registered RGB base and meaningful effective bandwidths for production. | RGB/NB geometry, background, units, channel assignment, and widths are verified |
| Direct or star-separated finish | Direct finish avoids external dependencies. Use native masks for local star protection/reduction; use StarNet only after its external prerequisites pass. | Preserve a pre-star copy and verify the recombination at 100% |

## Step-by-step procedure

### 1. Audit the acquisition set

1. Separate mono lights and flats by filter. Separate OSC raw lights and their
   calibration frames from already demosaiced products.
2. Compare dimensions, binning, gain/ISO, offset/readout mode, exposure,
   temperature/session, filter, CFA, object, and optical-train history.
3. Inventory bias, dark, dark-flat/flat-bias, and flats. A missing item becomes
   an explicit limitation, not an assumed match.
4. Choose an output directory outside the sources and preserve acquisitions
   read-only.

**Checkpoint:** every frame has one intended group, each flat has the correct
filter, and every calibration relationship can be explained before execution.

**Recovery:** correct metadata or isolate incompatible frames. Do not merge
groups merely to silence a missing-master warning.

### 2. Import, group, and validate in Image Integration

1. Open **Process > Preprocess > Image Integration** or its Dashboard card.
2. Add Bias, Darks, Flats, and Lights with **Add Files...** or **Add Folder...**.
3. Set **Output Directory**. Verify that filter remains a grouping field for
   mono lights/flats and that exposure/session/camera-mode splits are intended.
4. On **Workflow > Calibration**, inspect coverage, assignments, overrides,
   fallback indicators, and issues.
5. Open **Execution Plan**. Count the expected calibration, registration,
   alignment, normalization, and stacking groups before running.

**Checkpoint:** the plan contains one output per intended emission-line group,
and no frame is assigned to a master from another filter/camera mode.

**Recovery:** fix metadata, grouping, matching policy, or explicit assignments;
rebuild and reread the plan before execution.

### 3. Calibrate by filter

Start with the [Image Integration](../processes/stacking-integration/image-integration.md)
recommendations, then choose rejection from the actual frame count. The bounded
worked run used:

| Product | Combination/rejection | Normalization | Handoff |
| --- | --- | --- | --- |
| Ha/OIII/SII flat masters | Average; Sigma Clipping `3.0/3.0` | Global, Additive With Scaling, IKSS; no weighting | Eight selected flats per filter; flat auto-level on during light calibration |
| Ha/OIII/SII lights | In-memory calibration | Per-filter flat | Cosmetic correction off; no bias/dark was available in the fixture |

The worked run's three uncalibrated-flat warnings are evidence that the report
exposes this weakness. For production, supply correctly matched bias/dark-flat
or dark calibration where your sensor/workflow requires it and recheck the
report.

For OSC, keep raw CFA data through calibration. Supply the correct CFA pattern
when Auto cannot determine it, and inspect the demosaiced registered result for
checkerboards, swapped colors, or zippering.

**Checkpoint:** every master lists its intended inputs; calibrated/registered
views show no new clipping, dust inversion, residual glow, or CFA artifact.

**Recovery:** stop at the calibration stage, replace the wrong master or CFA
setting, and rerun affected groups rather than masking the artifact later.

### 4. Register, align, normalize, and integrate per filter

1. Select one strong reference across compatible Ha/OIII/SII lights. The worked
   run used a fixed Ha light and one 24-light registration group.
2. A conservative native starting point is **Star Matching**, **Homography**,
   transform behavior **Auto**, alignment **Standard**, interpolation **Auto**.
   Use a simpler transform when residuals support it; use polynomial models only
   with enough reliable matches and genuine distortion.
3. For each light group, the worked run used Average, Winsorized Sigma Clipping
   `3.0/3.0`, Global Additive With Scaling, IKSS, no weighting, no drizzle, and
   no final solve. These reproduce evidence; tune rejection/weighting from your
   set and the stacking reference.
4. Set maximum cores without starving the desktop; the worked run used `8`.
5. Execute, monitor the timeline, and wait for a safe cancellation checkpoint
   if you stop the workflow.
6. Inspect the HTML/Markdown report, registered lights, normalization choices,
   frame retention, rejection, master previews, and warning/error section.

**Checkpoint:** every required group succeeds, stars share one grid, output
edges are understood, expected masters exist, and each warning has an explicit
fix/accept decision. `success=true` alone is insufficient.

**Recovery:** replace a weak reference or bad frames and rerun the affected
registration/integration scope. Use history/last-successful state to distinguish
reused artifacts from regenerated products.

### 5. Inspect, crop, and correct backgrounds while linear

1. Open Ha, OIII, and SII masters. Use viewer autostretch only to compare them.
2. Inspect [Image Statistics](../interface/image-statistics.md) and
   [Histogram Analysis](../interface/image-histogram.md) for finite data,
   clipped endpoints, abnormal plateaus, or nearly empty channels.
3. Crop common registration borders with [Crop](../processes/geometry/crop.md).
   Apply exactly the same crop/grid to every channel.
4. Run [Background Extraction](../processes/background-extraction/background-extraction.md)
   separately on each channel. Keep samples off real nebulosity and bright
   halos; inspect the background model for target structure.

The representative evidence crop used 20 samples/line, tolerance `2.0`, RBF,
smoothing `0.50`, and subtraction. Use those only as an initial diagnostic.

**Checkpoint:** corrected channels remain finite/nonblank; the model contains
smooth low-frequency background rather than recognizable nebular detail; stars
and crop geometry still coincide.

**Recovery:** move/remove contaminated samples, reduce model flexibility, or
restore the integrated master and retry. Do not repair an overfit model with
later clipping.

### 6. Match channel intensity deliberately

Use [Linear Match](../processes/color-calibration/linear-match.md) when a common
intensity relationship is needed before direct composition. In the worked run,
Ha was the reference and OIII/SII were matched with accepted range
`1e-7..0.875`.

1. Choose a structurally representative reference, not automatically the
   brightest channel.
2. Ensure source/reference channel count is equal and geometry is registered.
3. Exclude clipped shadows/highlights while keeping enough ordinary signal for
   a stable fit.
4. Compare before/after histograms and several background/emission/star areas.

**Checkpoint:** channel scale/background is more compatible without clipping,
sign inversion, amplified gradients, or colored star rims after a preview
composition.

**Recovery:** widen the accepted range only after checking endpoints; otherwise
fix background/registration or choose a better reference. Preserve unmatched
masters for restart.

### 7. Build HOO, SHO, and custom palettes

Choose the smallest tool that expresses the intended mapping:

| Goal | Route and starting point | State/output caution |
| --- | --- | --- |
| Exact HOO | **Process > Color Spaces > Channel Composition**; RGB mode; `R=Ha`, `G=OIII`, `B=OIII` | Enable its Linear Match only if you intend that extra matching; retain mono sources |
| Exact SHO | Channel Composition; `R=SII`, `G=Ha`, `B=OIII` | Verify common geometry and clipping before color work |
| Compare fixed formulas | **Process > Color Spaces > Narrowband Palette Composer**; direct Ha/OIII/SII roles; begin at 100% intensities | Requires OIII plus Ha or SII; process output is tagged nonlinear |
| Flexible custom mix | **Process > Color Spaces > NB Color Mapper**; one mono layer per role; set hue/saturation, then screen blend | Mapper colorization/blend is nonlinear; tune layers one at a time |

Palette Composer includes SHO, HOO, HSO, HOS, OSS, OHH, OSH, OHS, HSS,
Realistic1/2, and Foraxx. With prepared roles `H`, `O`, and `S`, SHO is
`(S,H,O)` and HOO is `(H,O,O)`. The engine clips each raw channel to `[0,1]`
and then normalizes the entire RGB image by one global peak. Intensities change
relative roles, but values already clipped at one cannot respond further.

The evidence custom map used SII hue/saturation `18/0.82`, Ha `52/0.72`, and
OIII `195/0.78`, with no output STF. It is an execution checkpoint, not a
recommended aesthetic palette.

**Checkpoint:** output is three-channel and finite/nonblank; no channel is
clipped solely because its input was left unmatched; stars do not double; the
result expresses the intended structure at several zoom levels.

**Recovery:** return to matched mono masters, reset intensities/layers, compare
HOO/SHO before complex formulas, and change one role at a time. If OIII is
missing, supply it; the palette engine rejects that state.

### 8. Complete the narrowband OSC branch

After calibrated raw OSC data is demosaiced/integrated into a registered RGB
master, Narrowband Palette Composer can synthesize roles:

| Input port | Synthesized roles |
| --- | --- |
| OSC1 | Ha from red; OIII from mean of green and blue |
| OSC2 | SII from red; OIII from mean of green and blue |

If an explicit role already exists, synthetic input is blended `50/50` with it.
With explicit OIII plus both OSC ports, processing order affects the final OIII
weights; read the complete composer page before mixing all paths. OSC synthesis
does not know a filter's measured passbands, exposure balance, CFA leakage, or
sensor response.

1. Verify calibration-before-demosaic and the CFA pattern in Image Integration.
2. Confirm what the physical filter places in red and green/blue; do not assume
   every dual/tri-band filter matches the port labels.
3. Start with one OSC port, no explicit roles, and 100% intensities. Inspect HOO
   or the appropriate two-line palette.
4. Add a direct mono role only when its registration and weighting are known.

**Checkpoint:** synthesized roles are plausible, geometry matches, and HOO/other
outputs contain neither CFA artifacts nor unexplained channel duplication.

**Recovery:** return to the integrated RGB master, verify CFA/filter identity,
disable one OSC helper, or extract/prepare explicit channels through a process
suited to your physical filter. The worked run's same-target derivative proves
synthesis arithmetic only; it does not replace physical OSC validation.

### 9. Add narrowband signal to RGB with continuum handling

Use [NBRGB Combination](../processes/color-composition/nbrgb-combination.md)
only when you have a registered linear RGB target and same-size mono narrowband
sources. Enter documented effective bandwidths in nanometres; AfterNight does
not read measured filter curves or central wavelengths.

A conservative reproduction profile is RGB bandwidth `100`, red narrowband
`7`/scale `1.20`, green/blue `8.5`/scale `1.20`, fit rejection `0..0.92`, and
**Non Linear rescale** off. Replace widths with those of your actual filters.
Keep every narrowband width meaningfully below the RGB width.

Tune representative Preview areas containing background, emission, ordinary
stars, and highlights. Apply has no visible progress/Cancel control, so preview
before full-image execution.

**Checkpoint:** unselected RGB channels are unchanged; selected channels add
emission without exploded background noise, clipped cores, doubled stars, or
colored residuals; widths and assignments are recorded.

**Recovery:** fix registration/background first, verify bandwidth units, reduce
Scale, turn nonlinear rescale off, and widen the fit interval only when too few
ordinary pixels survive. Restore the original RGB rather than stacking several
failed enhancements.

### 10. Stretch, color-balance, and manage noise

1. Preserve the linear mono and composition masters.
2. Use [Generalized Hyperbolic Transformation](../processes/stretching/gh-transformation.md),
   [Histogram Transformation](../processes/stretching/histogram-transformation.md),
   or [ArcSinh Stretch](../processes/stretching/arcsinh-stretch.md) for a gentle
   first permanent stretch.
3. Inspect faint structures, background, bright cores, and stars at 100%.
4. Shape color after the major stretch with [Curves Transformation](../processes/stretching/curves-transformation.md),
   [Adjust HSL](../processes/color-adjustment/adjust-hsl.md), or
   [Color Saturation](../processes/color-adjustment/color-saturation.md).
5. Use masks for contrast/sharpening so noisy backgrounds and halos are not
   enhanced with the target.

**Checkpoint:** black point retains faint signal, highlights are not flat,
background chroma is controlled, and weak OIII/SII detail is not erased by an
Ha-dominated stretch.

**Recovery:** restore the last linear or pre-stretch checkpoint, reduce the
first stretch, and correct color/noise in smaller deliberate stages.

### 11. Choose and verify a star path

Three supported decisions have different prerequisites:

1. **Direct finish:** leave stars in the image. This is the dependency-free
   default.
2. **Native mask-limited work:** create a [Star Mask](../processes/star-operations/star-mask.md),
   use it to protect/select stars during nonlinear operations, and compare the
   result with the untouched pre-mask copy.
3. **Separated star/starless work:** use [StarNet++](../processes/star-operations/starnet.md)
   only after its executable/model configuration passes. Keep matched starless
   and stars products, process gently, then use
   [Starmask Recombination](../processes/star-operations/starmask-recombination.md)
   or the profiled variant.

Starmask Recombination performs additive recombination when no blend mask is
used. Therefore the starless and stars images must come from compatible
geometry/state and must not have been independently clipped into an impossible
sum. Broadband RGB stars must be registered/cropped to the narrowband result
and checked for PSF/halo mismatch.

**Checkpoint:** the star mask is neither empty nor full-frame; starless and star
products are finite/nonblank; recombination has no seams, dark rings, doubled
stars, clipped cores, or background shift at 100%.

**Recovery:** restore the pre-star image, soften/fix the mask, reduce star
processing, realign broadband stars, or recombine from products made at the same
stretch/state. Do not treat the worked run's native-mask split proxy as StarNet
output.

### 12. Export and archive restart points

1. Save a high-precision working master with processing history: FITS/XISF for
   the native working chain or TIFF where external editing requires it.
2. Reopen it and verify geometry, channels, bit depth/pixel type, processing
   state/history where supported, and finite/nonblank statistics.
3. From a deliberately stretched copy, save PNG/JPEG for sharing. Reopen and
   inspect it at 100% and at delivery size.
4. Archive source manifests/presets, Image Integration report/history, separate
   Ha/OIII/SII and RGB masters, pre-stretch composition, pre-star image,
   star/starless products, final working master, and delivery copies.

**Checkpoint:** the working master and share copy both reopen; the working file
retains the expected precision/state/history; the share copy looks like the
intended pixels rather than the viewer STF.

**Recovery:** if a save fails, keep the in-memory view open, create/choose a
writable parent, verify free space and extension, retry, then reopen. The worked
run verified this missing-parent failure/retry path.

## Parameter handoff matrix

| From step | Output/checkpoint | Decision criterion | To step | Parameters/state carried forward | Recovery |
| --- | --- | --- | --- | --- | --- |
| Acquisition audit | Classified frame inventory | Filter/camera/session/calibration relationships are explicit | Image Integration | Metadata, source paths, output root | Correct/isolate frames before import |
| Calibration | Per-filter calibrated lights/masters | No wrong-filter flat, CFA artifact, clipping, or unexplained warning | Registration | Linear state, filter role, camera mode | Replace master/CFA setting and rerun affected scope |
| Registration/integration | Ha/OIII/SII or OSC masters plus report | Required groups succeed; common grid and warnings accepted | Background preparation | Geometry, reference provenance, normalization/stack settings | Change reference/bad frames and rerun |
| Background preparation | Cropped/corrected channels | Model contains background, not target; grids remain identical | Linear Match | Linear state, common crop, channel identity | Restore master and refit samples/model |
| Linear Match | Reference-scaled mono roles | No clipping, inversion, gradient amplification, or halo shift | Palette branch | Reference identity and accepted range | Change range/reference or fix upstream |
| Mono roles | HOO/SHO/Foraxx/custom result | Three-channel finite/nonblank result expresses intended roles | Nonlinear finish | Formula, role intensities/hues, process state | Reset to matched masters and simpler mapping |
| OSC master | OSC1/OSC2 synthesized palette | Physical passband-to-role assumption is verified | Nonlinear finish | CFA/filter identity, chosen helper, intensities | Disable helper or rebuild explicit roles |
| RGB + NB roles | NBRGB result | Widths/registration/background recorded; enhancement is controlled | Nonlinear finish | Bandwidths, scales, rejection, rescale policy | Restore RGB; fix upstream; reduce Scale |
| Nonlinear finish | Pre-star final candidate | Stretch/color/noise checkpoints pass | Direct export or star path | Nonlinear state, masks, untouched checkpoint | Restore pre-stretch/pre-star checkpoint |
| Star path | Verified recombination | Geometry/state match; no rings/seams/doubling | Export | Star method, mask, recombination settings | Rebuild products from same source/state |
| Export | Reopened working/share files | Precision, history/state, geometry, and visible content pass | Archive/publish | Hashes, filenames, report/preset/history | Keep view open, repair path/space, retry |

## Algorithms and why the order matters

### Calibration, registration, and matching

Calibration corrects detector/optical patterns before interpolation. Demosaicing
OSC data first changes the sampling and prevents raw calibration masters from
operating on the original CFA pixels. Registration resamples every role onto a
common grid; palette math cannot repair translation, scale, rotation,
distortion, crop-origin, or PSF mismatch.

Linear Match estimates an affine intensity relationship over accepted samples.
For equal dimensions it fits paired pixels; for different dimensions it uses
independent moments, which cannot establish spatial alignment. Background
correction therefore precedes matching, and registration precedes both.

### Palette math and process state

For prepared Ha/OIII/SII samples `H`, `O`, `S`:

\[
SHO=(S,H,O),\qquad HOO=(H,O,O).
\]

Palette Composer also implements weighted Realistic and nonlinear Foraxx
formulas. Inputs are clipped, formulas are evaluated, and a global RGB peak
normalizes the output. This makes palette intensities relative rather than a
simple exposure-preserving scale. Its optional input autostretch is a nonlinear
preparation; applying it to already stretched sources double-stretches them.

NB Color Mapper maps each mono layer through hue/saturation and tonal controls,
then combines included colorized layers with an N-layer screen blend. It is a
creative nonlinear composition, not a spectral reconstruction.

### OSC synthesis

OSC1 uses red as synthetic Ha and mean green/blue as synthetic OIII. OSC2 uses
red as synthetic SII and the same mean as synthetic OIII. Explicit and synthetic
roles mix at fixed weights; no filter transmission, exposure, or noise model
sets those weights. Verify the physical filter before accepting the labels.

### Continuum arithmetic

For selected channel `c`, NBRGB treats the RGB and narrowband filters as
rectangular passbands. With broadband sample `R_c`, narrowband sample `N_c`, RGB
width `B_rgb`, and narrowband width `B_c`:

\[
q_c=\frac{B_c}{B_{rgb}},\qquad
C_c=\frac{N_c-q_cR_c}{1-q_c}.
\]

It median-centers and scales `C_c`, attempts a guarded affine fit over the
accepted range, and takes a per-pixel maximum with the boosted RGB plane.
Because `1-q_c` approaches zero when widths approach equality, wrong bandwidths
can amplify noise severely. The equation ignores measured transmission, central
wavelength, exposure, sensor response, and spectra; use it as an approximation.

### Stars, masks, and export

Star operations follow the major stretch only when the chosen process expects
nonlinear data. A mask must be checked for coverage before it controls a strong
operation. Additive recombination assumes compatible decomposed products;
independent clipping breaks that identity. Saving comes last, but restart
masters are saved throughout: irreversible color/star experiments should never
replace the only linear channel masters.

## Performance and resource planning

- Image Integration cost grows with pixels, frames, registration transforms,
  rejection, output maps, and drizzle. Registered lights alone can approach the
  selected light-set size; history and debug products add more.
- Palette Composer's grid calculates all 12 full-resolution float RGB results
  synchronously for preview and has no user-facing cancellation. Tune on smaller
  representative data where possible, then wait for completion.
- NB Color Mapper and NBRGB allocate several full-size intermediates. NBRGB
  processes selected channels serially while pixel loops use oneTBB; nonlinear
  rescale adds full-plane scans.
- Debug validation is not a user-performance benchmark: the same bounded
  integration of the worked run took 20.451 seconds in Release and 777.882
  seconds in Debug.
- Use representative crops for parameter discovery, then confirm one full-frame
  preview before Apply. A crop cannot prove full-frame gradients/highlights.
- Cancellation in Image Integration is cooperative. Wait for the safe stage
  boundary, inspect partial/history state, and confirm what will be reused before
  resuming.
- No network/catalog/external tool was required for the validated native path.

## Verified worked run

- **Sample-set provenance:** the mono SHO data set of M16 captured on
  2025-06-18, project-owned and supplied by the maintainer; 48
  deterministically selected inputs, all hashed under sanitized IDs.
- **Application platform:** Windows MSVC x64 Release and Debug, Qt 6.10.3.
- **Integration:** eight 300-second lights and eight flats per Ha/OIII/SII;
  fixed Ha registration reference; 24/24 effective and registered; three
  `3008 x 3004` 32-bit float FITS masters; zero errors; three expected warnings
  for flats without bias/dark calibration.
- **Downstream crop:** centered `1024 x 1024`; background sample counts Ha `323`,
  OIII `357`, SII `371`; OIII/SII matched to Ha at `1e-7..0.875`.
- **Palettes:** direct SHO, HOO, and Foraxx plus custom NB Color Mapper all
  finite/nonblank. Missing OIII raised the expected process error.
- **OSC boundary:** a same-target RGB derivative exercised OSC1 red-to-Ha and
  mean-green/blue-to-OIII; derived HOO matched direct HOO within sampled mean
  absolute delta `1e-6`. No physical OSC claim is made.
- **Continuum boundary:** a same-target mapped RGB derivative exercised NBRGB
  rectangular-bandwidth arithmetic and changed the image. No measured
  broadband-continuum or photometric claim is made.
- **Stars:** native Star Mask split proxy selected `0.744%`; starless/stars were
  finite/nonblank; additive recombination matched the stretched input within
  sampled mean absolute delta `1e-6`. No StarNet execution claim is made.
- **Export/recovery:** the historical `1024 x 1024 x 3` 32-bit float FITS
  reopened with a linear state that the old test assigned incorrectly. The
  corrected harness follows the production Palette Composer nonlinear contract
  and awaits an approved-data rerun. The historical HISTORY and finite/nonblank
  content checks remain valid; the 16-bit PNG reopened finite/nonblank, and the
  missing-parent save failed then passed after parent creation and retry.
- **Regressions:** the current remediation Release and Debug runs each passed 61
  focused engine and 21 focused process/UI contract tests. The corrected
  private-data filters compiled and skipped 2/2 in both builds because the
  approved-data variables were absent; a fresh approved-data run remains
  required. The standard repository-wide Release runner passed all discovered
  `Test_*.exe` test executables in 1,067.6 seconds.

Hashes, exact settings, output observations, privacy policy, and exclusions are
in the guide's validation record.

## Warnings and limitations

!!! warning "Verification remains blocked on representative branch data"
    The physical mono branch has partial execution evidence, but no approved
    physical narrowband OSC or matched broadband-continuum set was supplied.
    Same-target arithmetic proxies do not satisfy the every-branch mode-E gate.
    The corrected palette-state harness must also be rerun with approved data.

!!! warning "Calibrate flats correctly for production"
    The supplied session had per-filter flats but no approved bias/dark data.
    The run intentionally retained three uncalibrated-flat warnings as evidence.
    Do not copy that limitation into a production workflow.

!!! warning "The OSC and continuum executions are bounded proxies"
    They verify current native algorithm paths on representative same-target
    signal. They do not verify raw CFA/demosaic/sensor behavior, physical
    dual-band separation, measured continuum, filter transmission, or
    photometric fidelity.

!!! warning "Equal dimensions are not registration"
    Palette and continuum tools generally validate dimensions/types, not WCS,
    star alignment, crop origin, PSF, or filter identity. Misregistration can
    produce plausible-looking colored halos.

!!! warning "Palette tools are not spectrophotometry"
    Fixed formulas, global normalization, HSV colorization, screen blend, and
    user-entered bandwidths create mapped color. They do not recover physical
    emission ratios without a separate calibrated measurement workflow.

!!! warning "Optional star tools require separate readiness"
    The worked run did not use StarNet. Configure and validate its
    executable/model before relying on a starless branch; keep a direct native
    finish available.

- The worked values describe one bounded sample and representative crop.
- No full-frame polished result, print, Linux, installer, catalog, solver,
  network, external model, or general performance claim is made.
- Process pages remain authoritative for complete controls, state behavior,
  masks, progress/cancellation gaps, and metadata limitations.

## Troubleshooting and recovery

| Stage/symptom | Verified cause or failed checkpoint | Recovery | Resume/restart consequence | Evidence |
| --- | --- | --- | --- | --- |
| Flat warning | No bias/dark master assigned to that filter's flat group | Supply a compatible master or explicitly accept the bounded limitation | Rebuild flat and downstream light groups | Worked run: three warnings |
| Expected filter master missing | Wrong grouping/filter label, failed stage, or output collision | Correct metadata/plan/output name and rerun affected group | Preserve successful unrelated groups/history | Image Integration source/run |
| Low-signal OIII/SII rejected | Weak frames, normalization/reference/rejection mismatch | Inspect frame metrics; change reference/rejection or exclude only verified failures | Rerun affected normalization/stack | Workflow report contract |
| Colored halos/doubled stars | Channels have unequal registration/crop/PSF | Register/crop on one grid; inspect stars at center/edges | Restart from mono/RGB masters | Source + decision checkpoint |
| One role dominates palette | Background/intensity mismatch or clipped input | Correct backgrounds, Linear Match deliberately, reduce intensity/layer saturation | Restart composition from preserved masters | Worked-run matching/palettes |
| Palette Composer rejects inputs | OIII absent, no Ha/SII after synthesis, invalid/missing image, or unresolved sizes | Supply required role, fix/enable size preparation, reopen valid views | No output; inputs unchanged | Negative path + process page |
| OSC palette looks implausible | Physical passbands/CFA do not match OSC helper assumptions | Verify filter/CFA; disable helper; prepare explicit roles | Restart from calibrated OSC master | Scoped OSC execution |
| NBRGB background/noise explodes | Bad registration/background, wrong units/widths, or width near RGB width | Fix upstream; verify widths; reduce Scale; keep nonlinear rescale off | Restart from original RGB | Source + scoped arithmetic |
| NBRGB fit seems inactive | Fewer than three usable pairs or near-constant corrected plane | Inspect clipping and rejection; widen range carefully | Fallback uses corrected NB directly | NBRGB source/tests |
| Star mask empty/full | Detection threshold/size/shape unsuitable | Inspect mask, tune one control, regenerate before processing | Restore pre-star checkpoint | Worked-run native star path |
| Recombination has seams/rings | Mask too hard or star/starless states no longer form a compatible pair | Soften/rebuild mask; recombine matched products | Restart from pre-star image | Recombination source/checkpoint |
| Saved file is dark | Only viewer autostretch was visible | Permanently stretch a copy, then export it | Linear master remains valid | Save/viewer contract |
| Save fails | Missing/unwritable parent, disk, lock, or extension issue | Keep view open; repair destination; retry; reopen | No processing restart required | Worked-run failure/recovery |

## Related workflows and references

- [Image Integration](../processes/stacking-integration/image-integration.md) —
  calibration, common registration, per-filter integration, artifacts, and
  reports.
- [Linear Match](../processes/color-calibration/linear-match.md) — accepted
  range, affine fit behavior, geometry, and state.
- [Channel Composition](../processes/color-composition/channel-composition.md) —
  exact HOO/SHO assignments and optional linear matching.
- [Narrowband Palette Composer](../processes/color-composition/narrowband-palette-composer.md) —
  OSC roles, formulas, intensities, global normalization, UI execution, state,
  and provenance.
- [NB Color Mapper](../processes/color-composition/nb-color-mapper.md) — custom
  layer colorization, modifications, screen blend, and output behavior.
- [NBRGB Combination](../processes/color-composition/nbrgb-combination.md) —
  continuum equation, bandwidths, fit/merge, performance, and limitations.
- [Star Mask](../processes/star-operations/star-mask.md),
  [StarNet++](../processes/star-operations/starnet.md), and
  [Starmask Recombination](../processes/star-operations/starmask-recombination.md)
  — direct/native/external star paths.
- [Saving and Exporting](../interface/saving-and-exporting.md) and
  [File Formats](../reference/file-formats.md) — working masters, delivery
  copies, processing history, and readback.
- [Broadband Imaging](broadband-imaging.md) — production OSC and RGB/LRGB
  acquisition/integration context for real continuum or broadband-star inputs.
- The guide's validation record — the sanitized execution record and claim
  boundaries.

The palette-composer and mapper process pages record their upstream source-code
provenance and license constraints. NBRGB is native AfterNight GPL code. This
workflow adds no external algorithm, asset, model, or network dependency.
