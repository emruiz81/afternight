# Photometric Color Calibration

## Purpose and expected result

Photometric Color Calibration (PCC) balances a plate-solved, linear broadband
RGB image by matching its stars to a configured local Gaia DR3 catalog. It
compares measured red/green and blue/green stellar flux ratios with a compact
Gaia BP-RP color model, derives one global gain for red and blue while keeping
green at `1.0`, and can add background-neutralizing offsets.

On success, PCC replaces the selected image in place as one Image History step,
marks the result color calibrated and records the calibration in its metadata,
and can draw the stars the fit used over the view. This implementation is a
fast project-specific color-index approximation. It does not integrate Gaia
spectra through the user's sensor and filters, so it should not be confused
with [Spectrophotometric Color Calibration](spectrophotometric-cc.md).

## At a glance

| Item | Current behavior |
| --- | --- |
| Menu path | **Process > Color Calibration > Photometric Color Calibration** |
| Enabled when | An image view is active and no exclusive process owns it. The window accepts open three-channel RGB targets, then enables **Apply** only for an image that is broadband by its filter name, plate solved, and not marked nonlinear, with a ready local Gaia DR3 catalog. |
| Inputs | One RGB target in any sample type, the plate solution its WCS cards state, its filter and exposure start (`DATE-OBS`) when recorded, the configured local Gaia DR3 catalog's G magnitudes and BP-RP colors, an optional background ROI, and an optional enabled image-view mask. |
| Outputs | Destructive in-place replacement after complete success, in the image's own sample type; the image marked color calibrated with a lineage entry and PCC color-calibration cards; one history step; a status line and completion message; and an optional matched-star overlay. No new result image or report file is created. |
| Recommended image state | Integrated, gradient-corrected, cropped, plate-solved, broadband linear RGB before stretching, with floating-point samples held as fractions of full scale. An image marked nonlinear is refused; an image whose linearity is not recorded is accepted, and a previous PCC pass is not refused. |
| Preview support | None. **From Selection** edits the background ROI and **Show matched stars** displays the fitted stars only after a successful full Apply; neither is a calibration preview. |
| Sequence support | No sequence-wide run. Over a sequence view, PCC calibrates the frame the view shows, as an edit Image History records, and only when that frame carries its own plate solution. The recording on disk and the other frames are not changed. |
| Mask support | Supported as a post-calibration blend. An enabled view mask and its inversion state are snapshotted at Apply; the mask does not affect star detection, catalog matching, gain fitting, or background statistics. |
| External dependencies | A ready local Gaia DR3 chunk catalog configured through **Settings > Plate Solving**. PCC makes no network request and does not use the separate SPCC Gaia XP/response repositories. |

## Before you start

1. Use broadband RGB data. PCC deliberately rejects filter names it recognizes
   as H-alpha, OIII, SII, narrowband, dual-band, or tri-band.
2. Finish calibration, registration/integration, gradient correction, and
   cropping while the data is linear. PCC refuses an image marked nonlinear,
   as AfterNight's stretches mark their results, but it cannot detect a stretch
   nothing recorded. Black borders, gradients, or an unrecorded nonlinear
   stretch still bias measurements.
3. Plate solve the final target. The plate solution its WCS cards state is
   required to place detected green-channel stars on the sky and match Gaia
   sources.
4. Configure a ready local Gaia DR3 catalog in **Settings > Plate Solving**.
   Only a complete chunk catalog is ready; an incomplete install or a catalog in
   a layout this version does not read is not. The readiness button opens that
   settings page; no catalog is downloaded by Apply.
5. Preserve observation time as FITS `DATE-OBS` where possible. PCC carries
   Gaia proper motions from the catalog's reference epoch (J2016.0 for Gaia DR3)
   to the Julian epoch of the exposure start, counted from J2000.0
   (2000-01-01T12:00); an image that records no exposure start keeps the
   catalog's epoch.
6. Hold floating-point samples as fractions of full scale. PCC reads a float
   sample as stored and keeps every result in `[0,1]`, so a float image stored
   on another scale is clipped at `1`.
7. Save a copy or rely on Image History. Apply is destructive and has no
   before/after preview.
8. If background neutralization is selected, identify a representative empty-
   sky area. Prefer a clean ROI when the full image contains strong nebulosity,
   galaxy structure, gradients, or invalid borders.
9. Disable the view mask while solving the global settings unless a local
   blend is intentional. The solver still measures the entire source/ROI even
   when a mask limits the final pixel replacement.

## Procedure

1. Activate the solved RGB image and open **Process > Color Calibration >
   Photometric Color Calibration**.
2. Resolve the readiness banner. Choose **Plate Solve...** if the image is not
   plate solved or **Configure Gaia Catalog...** if the configured local catalog
   is not ready. A narrowband, grey, or nonlinear-marked image offers no action.
   A ready status reads “Ready to calibrate photometric gains from Gaia-matched
   stars.”
3. Start with **Magnitude limit** `14.0` and **Match radius** `5.0` arcsec. Use a
   fainter limit only when a sparse field lacks matches; enlarge the radius
   only when verified WCS accuracy/image scale warrants it.
4. Leave **Apply background neutralization** selected for a representative
   linear background. Enable **Background ROI Override** when global statistics
   would include too much target structure, then enter the rectangle or use
   **From Selection** on the target view.
5. Confirm **Target image**. Leave **Show matched stars** selected if you want
   the fitted star positions displayed after success.
6. Choose **Apply**. Controls lock while a worker reads the catalog region,
   detects stars, fits gains, and writes the result; the status line names the
   engine's current stage.
7. Read the completion message and status: they report the number of stars
   the fit used and the R/G/B gains, and the status says whether the Gaia color
   model or the neutral default described most stars. Green overlay circles
   identify stars with Gaia BP-RP color; yellow circles identify stars that used
   the neutral fallback.
8. Inspect shadows/highlights at 100%, compare channel statistics with the same
   display stretch, and inspect Image History. If the result clips or has a
   cast, undo it before changing one setting group at a time.

## Complete parameter and action reference

| UI label/action | Type and unit | Runtime default | Persisted-default exception | Valid values, step, or constraints | Effect of increasing, decreasing, or selecting | Dependencies and enable/visibility rules | Quality, artifact, runtime, memory, or disk trade-off | Scope: preview/apply, channel, mask, still/sequence | Persistence |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Readiness warning banner | Dynamic status | Hidden when ready | Reflects image metadata and global catalog settings | RGB, narrowband, plate-solve, nonlinear, and catalog messages, checked in that order | Names the first blocker found. | Visible for a non-RGB image (error tone), a narrowband filter name, a missing plate solution, an image marked nonlinear, or a catalog that is not ready. While another edit holds the target, the window keeps what it showed. | Resolve the stated prerequisite; this is not a fit-quality diagnostic. | Dialog/readiness; current image | Recomputed; not persisted. |
| **Plate Solve...** | Conditional action | Hidden | Uses the selected target and existing solver settings | Click action | Starts Plate Solving for the current PCC target. | Visible only when the target is RGB and broadband but states no plate solution; disabled while PCC is busy. | Plate solving has its own catalog/CPU/runtime requirements and changes target WCS metadata. | Setup for current target | WCS persists when the image is saved. |
| **Configure Gaia Catalog...** | Conditional action | Hidden | Opens global settings | Click action | Opens **Settings > Plate Solving** to select/install a local Gaia catalog. | Visible only when the target passes every image check but the catalog is not ready; disabled while busy. | Catalog setup can require separate disk/network activity; PCC itself does not download. | Setup, not Apply | Saved Plate Solving settings persist globally. |
| **Magnitude limit:** | Gaia G magnitude | `14.0` | None | `6.0`–`20.0`; step `0.5`; one decimal | Higher includes fainter catalog sources; lower restricts the catalog region read and the match to brighter sources. Stars without a G magnitude are never read. | Enabled only when all readiness gates pass and PCC is idle. | Fainter sources can improve sparse-field support but add tile/matching work and weak-star measurements. Each channel's detection still keeps at most 2,000 stars, brightest first. | Full Apply; catalog region and match | Window lifetime; **Reset** restores `14.0`, and a window opened again starts at `14.0`. |
| **Match radius (arcsec):** | Arcseconds | `5.0` | None | `0.5`–`20.0`; step `0.5`; one decimal | Changes the allowed separation between a detected green star and a Gaia entry, and widens the catalog cone by the same margin. | Ready and idle only. The separate red/blue-to-green radius is fixed internally at `3.0` pixels. | Too small loses valid pairs; too large admits more false candidates. Candidate pairs are accepted in order of increasing separation, one star to one entry. | Full Apply; WCS/Gaia matching | Window lifetime; **Reset** restores `5.0`. |
| **Apply background neutralization** | Boolean | Selected | None | Selected/unselected | Selected measures each channel's sky and adds offsets so the gain-scaled skies meet at one shared pedestal. | Ready and idle only. Turning it off disables **Background ROI Override**. | A clean background removes a cast; structured or gradient-dominated statistics can create an offset error and more clipping. A channel with no valid sample in the measured area fails the run. | Full Apply; RGB; before final mask blend | Window lifetime; **Reset** selects it. |
| **Background ROI Override** | Checkable group | Off | None | Off/On | Off measures the sky over the whole target; On restricts it to the rectangle. | Enabled only when neutralization is selected, readiness passes, and PCC is idle. | A smaller clean area reduces object contamination but may be less representative/noisier. | Sky measurement only; not star fit or target mask | Window lifetime; **Reset** turns it off. |
| **Left:** | Integer pixels | `0`; enabling a fresh ROI starts at `0` | None | `0` to image width minus 1; Apply requires Left + Width inside target | Moves the ROI origin horizontally and reduces the maximum Width. | ROI Override on, neutralization on, ready, and idle. | Changes the pixels used for all three sky measurements. | Background ROI | Window lifetime; target change/Reset clears it. |
| **Top:** | Integer pixels | `0`; enabling a fresh ROI starts at `0` | None | `0` to image height minus 1; Apply requires Top + Height inside target | Moves the ROI origin vertically and reduces the maximum Height. | Same ROI dependencies. | Changes background evidence. | Background ROI | Window lifetime; target change/Reset clears it. |
| **Width:** | Integer pixels | Full target width when a fresh ROI is enabled | None | `0` to image width minus Left; Apply refuses a zero Width unless Height is also `0` (see boundary behavior) | Narrows or expands the sampled rectangle horizontally. | Same ROI dependencies. | Very narrow ROIs provide fewer samples and can overrepresent local structure. | Background ROI | Window lifetime; target change/Reset clears it. |
| **Height:** | Integer pixels | Full target height when a fresh ROI is enabled | None | `0` to image height minus Top; Apply refuses a zero Height unless Width is also `0` (see boundary behavior) | Narrows or expands the sampled rectangle vertically. | Same ROI dependencies. | Very short ROIs provide fewer samples and can overrepresent local structure. | Background ROI | Window lifetime; target change/Reset clears it. |
| **From Selection** | Checkable action | Off | None | Draw/move one rectangular selection in the target view | Clears the rectangle to `0`, starts the target-view crop controller, and mirrors selection geometry into Left/Top/Width/Height. | ROI Override on, neutralization on, ready, and idle. Turning it off leaves the current rectangle, or restores the full frame when Width or Height is `0`. | Visual placement aid only; it does not preview neutralization or restrict star fitting. | Target-view ROI overlay | Selection and state last only for this process window. |
| Status text | Read-only status | Ready text or empty when blocked | None | Ready, running, or final message | During Apply first reads “Calibrating photometric gains from Gaia-matched stars...”, then names the engine's current stage with its progress count. After success it says whether the Gaia color model or the neutral default described most stars, and gives the number of stars the fit used and the gains to three decimals. | Recomputed with readiness/execution. | The UI omits the engine's RMS log residual, offsets, match separations, and per-star fluxes. | Dialog/full Apply | Window lifetime. |
| **Target image:** | Open-image selector | Launching RGB view | None | Compatible open three-channel RGB views | Switches the destructive target and recomputes ROI bounds/readiness; clears the previous result and overlay. | Locked while busy. A target that stops being RGB closes the process. | A different field needs its own WCS/catalog coverage and can need different limits/ROI. | Full Apply; current image or shown sequence frame | Window lifetime. |
| **Show matched stars** | Boolean overlay toggle | Selected | None | Selected/unselected | Shows/hides the stars the last successful fit used without discarding them. Green means BP-RP color available; yellow means neutral fallback. | Ready and idle controls; an overlay exists only for the selected target and has stars only after success. | Display-only; fixed 8-screen-pixel circles at each star's measured position do not change calibration or files. | View overlay; no pixel effect | Window lifetime; **Reset** selects it and clears the stars. |
| **Apply** | Action | Enabled only when ready | None | One execution at a time; selected ROI must be nonzero and in bounds | Snapshots the dialog, the target's filter and epoch, the catalog location, and the mask, then edits the target: a worker holding the image's writer reads the Gaia region, calibrates a floating working copy, converts it back, blends the mask, and records the metadata. Image History records the step before the view shows it. | Requires an RGB, broadband, plate-solved image not marked nonlinear, a ready catalog, a valid ROI, and idle state. If another edit holds the target, a busy warning is shown instead. | A floating working copy plus the result in the image's sample type (and one more copy with a mask) increase memory; catalog/star work increases with field and depth. Destructive on success. | Current image or shown sequence frame; all RGB channels; post-blend mask | Pixels, metadata, and history persist with the open image/save; settings do not. |
| **Reset** | Action | Available while idle | None | Click action | Restores magnitude/radius, neutralization, ROI, and overlay defaults; clears result/status before readiness is recomputed. | Disabled while busy. | Discards unapplied settings and overlay stars; does not change an already applied image or global catalog settings. | Dialog only | No preset; first-open defaults restored. |
| **Close** | Action/title-bar close | Available | None | Click action | Idle closes without applying. Busy asks the run to stop, hides and disables the window, and closes once the run has landed. | Always enabled. | The engine checks the stop between and inside its stages, so a stopped run changes nothing, adds no history step, and shows no message. Only a run whose work had already finished when Close was chosen is still shown. | Dialog/full Apply | Not persisted. |
| **OK** in completion/error message | Result-message action | Shown after a completion or failure while the window is open | None | Dismisses the message | Closes the informational/error message only. | The completion message (title **Photometric Color Calibration**) gives the star count and gains; a failure (title **Error running photometric color calibration**) gives the engine's reason. Suppressed when the window was closed during execution. | Does not undo or change the result. | Dialog only | Not persisted. |

## How the algorithm works

### Conceptual pipeline

1. On the GUI thread, check a three-channel RGB target that is broadband by its
   filter name, plate solved, and not marked nonlinear, a ready local Gaia
   catalog, and the optional background ROI. Take the epoch from the exposure
   start, otherwise the catalog's own reference epoch.
2. On the edit's worker, read the plate solution from the image's cards and
   read the Gaia stars inside a cone at the field center whose radius is half
   the field diagonal plus **Match radius**, no fainter than **Magnitude
   limit**, carried to the epoch, each with its BP-RP color where both bands are
   measured.
3. Make a working copy in Float32 (Float64 for a Float64 or 32-bit integer image) holding the
   samples as fractions of full scale: an unsigned integer divided by its
   largest value, a signed integer counted from its smallest value to its
   largest, a float as stored.
4. Detect up to 2,000 stars in red, green, and blue in turn with a fixed finder:
   a 5-pixel search radius, a threshold five noise sigmas above the sky,
   roundness at least `0.4`, and a Gaussian profile fit with the optional shape
   and residual checks relaxed. Place the green stars on the sky through the
   solution and accept unique matches within **Match radius**, closest first.
   Each matched green star then claims the nearest unclaimed red and blue
   detection within the fixed 3-pixel channel radius.
5. Estimate each star's channel flux from its fitted profile. Convert Gaia BP-RP
   color into expected red/green and blue/green ratios, form one correction per
   star and channel, and robustly reduce the corrections to global red and blue
   gains. At least 10 usable samples are required.
6. Optionally measure each channel's sky over the full image or ROI and compute
   shared-pedestal offsets. Apply gain plus offset to every valid sample of the
   working copy, hold the result in `[0,1]`, and write it back to the image's
   own sample type to the nearest value.
7. If a target mask was enabled, blend the original and the calibrated image.
   Mark the result color calibrated, append its lineage entry, and write its
   cards. Image History records the step, the view shows it, and the status and
   overlay are published.

### Model and equations

For a detected star with fitted profile amplitude `A` and widths `sigma_x` and
`sigma_y`, PCC uses the profile volume as the flux

\[
F=\max(A,0)\,2\pi\max(|\sigma_x|,0.5)\max(|\sigma_y|,0.5).
\]

A star triplet is a usable sample only when all three channel fluxes are finite
and above `10^-9`.

Let `C` be Gaia `BP-RP`, clamped to `[-0.5,3.5]`. When BP or RP is missing,
PCC substitutes `C=0`. Its current project-specific expected ratios are

\[
E_{R/G}=e^{0.32C},\qquad E_{G/G}=1,\qquad E_{B/G}=e^{-0.40C}.
\]

Gaia G magnitude is used to limit/query sources; it does not enter these ratio
equations. This is not a sensor/filter passband integration or a published Gaia
color-transformation formula.

For usable sample `i`, observed ratios are `O_R=F_R/F_G` and `O_B=F_B/F_G`.
The candidate corrective gains are

\[
q_{R,i}=\frac{E_{R/G,i}}{O_{R,i}},\qquad
q_{B,i}=\frac{E_{B/G,i}}{O_{B,i}}.
\]

Each channel takes the median of its candidates, estimates
`sigma=max(1.4826022185056018 MAD,0.05)`, keeps values within `3 sigma` of the
median, and
takes the retained median (the first median when nothing is retained). Thus

\[
g_R=\operatorname{median}_{3\sigma}(q_R),\qquad g_G=1,
\qquad g_B=\operatorname{median}_{3\sigma}(q_B).
\]

Gains are not max-normalized and can exceed `1.0`. The engine also computes an
RMS logarithmic residual across corrected R/G and B/G ratios, but the process
UI does not display it. The status says the Gaia color model was used only
when more than half the usable samples have BP-RP; individual missing-color
samples always use the `C=0` fallback.

For sky channel `c`, PCC computes, over the image's valid samples in the full
image or ROI, the median `M_c` and `sigma_c=1.4826022185056018 MAD_c`, then the
median `B_c`
of the samples inside the window

\[
[M_c-2.8\sigma_c,\;M_c+2.0\sigma_c].
\]

The window is not clipped to `[0,1]`, so a sky near zero keeps its negative
samples in the estimate. With shared pedestal `P=(B_R+B_G+B_B)/3`, the offset
and pixel transform are

\[
o_c=P-g_cB_c,\qquad I'_c=\operatorname{clip}_{[0,1]}(g_cI_c+o_c),
\]

computed on the working copy's fractions of full scale. Therefore each measured
source background maps to the same original mean pedestal. Without
neutralization, every `o_c` is zero. Samples the image's validity rule or mask
marks invalid are left unchanged. The result is then written back to the
image's sample type to the nearest value.

An enabled normal mask with normalized value `m` post-blends

\[
O=(1-m)I+mI',
\]

and mask inversion swaps the weights. A one-channel mask weighs every channel;
a three-channel mask weighs each channel separately. Mask values never enter
the fit.

### Boundary and failure behavior

- The window blocks an image that is not three-channel RGB, a recognized
  narrowband filter name, an image whose cards state no usable plate solution,
  an image marked nonlinear, and a catalog that is not ready. An Apply asked
  for anyway is refused with the same reason. An unreadable catalog manifest or
  chunk, or a field cone that holds no catalog star at the chosen limit, can
  still fail after Apply.
- An image is marked nonlinear when its meaning states a nonlinear transfer
  function (sRGB, Rec. 709, or a power law), when its description says its
  samples are nonlinearly encoded, or, where the meaning states no transfer
  function, when its `ANLIN` card says nonlinear. An image whose linearity
  nothing records is accepted.
- Filter detection is a name heuristic. Ignoring case and surrounding spaces,
  it recognizes `Ha` or `SII` as the whole name or its first word, and
  `Halpha`, `H-alpha`, `OIII`, `S-II`, `narrow`, `dualband`, or `triad`
  anywhere in the name; incomplete or misleading metadata can bypass or falsely
  trigger it.
- A channel in which detection finds no star, no green star matching a catalog
  entry, or fewer than 10 usable samples fails without replacing the source.
  Match count is support, not a spatial-distribution or color-span quality
  gate. An image 28 pixels or less on a side cannot be searched, and a
  not-a-number sample that the image's validity rule counts as valid makes
  detection fail.
- Green stars are matched one to one, closest angular separation first. Red and
  blue pairing takes, for each matched green star in turn, the nearest
  unclaimed detection within a fixed 3-pixel image radius. There is no
  configurable residual-quality, quadrant-coverage, saturation, SNR,
  catalog-quality-flag, or color-span gate in PCC.
- The background ROI must have positive size and lie fully inside the target. A
  rectangle whose Width and Height are both `0`, which is what the fields hold
  after **From Selection** is turned on and before a selection is drawn, is
  refused by name rather than measured over the whole frame. A channel whose
  measured area holds no valid sample fails the run; offsets are never silently
  set to zero.
- All results are held in `[0,1]` of full scale, whatever the sample type.
  Gains above one and positive offsets can therefore clip stars/highlights;
  negative offsets can clip shadows. A floating image whose samples are not
  fractions of full scale is clipped at `1`.
- Apply runs off the UI thread as an edit that holds the image's writer, so
  nothing else changes the image during the run. Failure leaves the target
  untouched and adds no history step. If the target closes or changes, or
  Image History does not record the step, before the result is shown, the
  result is discarded.
- Closing the window, closing the target view, or a change of the target's
  document asks the run to stop. The engine checks between and inside its
  stages, so a stopped run publishes nothing and shows no message. A run whose
  work had already finished is still shown.
- The generic launcher admits a sequence view. PCC then calibrates the frame
  the view shows, using the plate solution that frame carries, as an edit
  Image History records. No sequence iterator, all-frame output, or rewrite of
  the recording exists.

## Parameter interactions and starting points

| Combination | Conservative starting point | Too weak looks like | Too strong looks like | Adjustment order |
| --- | --- | --- | --- | --- |
| Magnitude limit + Match radius | `14.0`, `5.0` arcsec on a verified plate solve | “fewer usable colour samples than the fit needs” or no matched green star, few/no overlay circles | False pairs, unreliable gains, or circles on mismatched stars when both are unnecessarily broad | Verify WCS first; deepen magnitude in `0.5` steps, then alter radius only with an accuracy/image-scale reason. |
| Background neutralization + ROI | Selected; clean empty-sky ROI when the global field is structured | Residual background cast because neutralization is off or the ROI is not representative | Shadow/highlight clipping or a new cast from a tiny/contaminated ROI | Correct gradients/crop borders, choose ROI, inspect statistics, then compare with neutralization off. |
| Gains + normalized clipping | Defaults; inspect reported gains before another pass | Residual color imbalance when catalog matches do not represent the field | Clipped stars/highlights for gain above one; clipped shadows from offsets | Undo, validate WCS/matches/color metadata, then adjust catalog depth/radius rather than stacking repeated PCC passes. |
| Show matched stars + fit quality | Selected after Apply | Overlay hidden, so correspondence cannot be inspected | Dense circles can obscure the view but do not affect pixels | Inspect circle placement/colors, toggle off for image judgment, and record star count/gains. |
| Active target mask + global PCC | Disable for the first global assessment | Dark/disabled mask makes little calibrated result visible | Hard mask edges produce local color discontinuities | Solve globally unmasked, undo if needed, then enable/soften/invert a deliberate mask and reapply once. |

## Inputs, outputs, and metadata

- PCC accepts a three-channel RGB image in any of the eight sample types
  (8-, 16-, and 32-bit unsigned and signed integers, and 32- and 64-bit
  floating point). It calibrates a Float32 working copy (Float64 for a Float64
  or 32-bit integer image) holding the samples as fractions of full scale, and writes the result
  back in the image's own sample type to the nearest value, saturated, with its
  description and validity unchanged.
- It refuses an image marked nonlinear but accepts one whose linearity is not
  recorded, and it does not refuse an image that is already color calibrated.
- It reads the plate solution from the image's WCS cards, as the viewer reads
  it, the filter name from its acquisition record, and the exposure start (FITS
  `DATE-OBS`) for the epoch. Gaia proper motion is applied to that epoch.
- The local Gaia catalog must be a complete chunk catalog, whose tiles are
  addressed by the standard HEALPix numbering; a field reads the tiles that
  cover it. It supplies positions carried to the epoch, G magnitudes, and BP-RP
  where both bands are measured. It is distinct from SPCC's sampled-XP catalog
  and response repository.
- A successful output keeps dimensions, RGB layout, sample type, plate
  solution, and acquisition record. Its meaning is marked color calibrated, and
  its lineage gains an `an::calibratePhotometricColor` entry naming the recipe
  `pcc-gaia-bp-rp-v1` with the gains and offsets. Its cards record the
  calibration: `ANCCAL` as photometric, the gains (`ANCCGN`) and offsets
  (`ANCCOF`), the catalog's name from its manifest (`ANCATF`), the algorithm
  version `pcc-gaia-bp-rp-v1` (`ANALGOV`), the input's linearity (`ANINLIN`)
  and channel basis (`ANINBAS`) as its processing-state cards stated them
  (unknown where it had none), the processing state that records the
  calibration, and the calibration document cards. A later SPCC readiness check
  treats this image as already color calibrated.
- The history step is named **Photometric Color Calibration** and described by
  the dialog's values, for example “Magnitude limit: 14.0, Match radius: 5.0
  arcsec, Background neutralization: on”. The step is described when the run is
  issued, so the gains and star count appear only in the status line and the
  completion message. No file records the RMS residual, per-star
  flux/color/separation, the ROI, the magnitude limit, or the match radius.
- Matched-star circles are transient view decorations. They are not rasterized
  into the image, saved to metadata, or exported as a report.
- With a mask, the final blended image retains the calibrated metadata even
  where the mask preserves original pixel values. No output filename,
  temporary file, or overwrite prompt is used.

## Performance

Cost grows with target pixel count, detected-star count, the catalog tiles the
field cone touches, and the catalog candidates inside it. Red, green, and blue
are searched in turn, each on the run's threads, and each keeps at most 2,000
stars, fitted brightest first. Matching compares the green detections with the
stars inside the cone, so a fainter **Magnitude limit** increases both catalog
reading and matching work. The catalog's chunk files are memory-mapped and only
the tiles the cone touches are decoded.

Gains and offsets are applied on the run's threads. A run holds the source, a
floating working copy of the same dimensions (Float32 for every sample type
except Float64 and the 32-bit integers, which take Float64, so twice the
source's size for 16-bit and 32-bit data and four times for 8-bit data), and the result in the image's own sample type; an active mask adds
one more full-size image for the blend, plus the mask. Image History keeps its
own copy of the step. Star lists, samples, and decoded Gaia tiles add smaller
field-dependent memory. There is no GPU path, network access, or disk output
beyond Image History's own snapshots; the status line names the engine's stage
during a run. Prototype settings on a duplicate/crop or use a brighter
magnitude limit before applying to a large field; no production timing was
measured for this page.

## Worked example

- Evidence mode: Source-and-test-derived; no approved astronomical target was
  executed.
- Input condition/provenance and settings: a synthetic 512x384 linear Float32
  RGB field of 60 Gaussian stars with Gaia BP-RP colors from `-0.3` to `2.1`,
  rendered through the color model with red multiplied by `1.25` and blue by
  `0.8` over skies of `0.05/0.06/0.04`, plate solved at 2.4 arcsec per pixel,
  with a Gaia chunk catalog of the same stars installed as the downloader
  installs one. The window runs at its defaults: G `14.0`, match radius `5.0`
  arcsec, neutralization on.
- Expected checkpoint: the gains undo the planted casts (red near `0.8`, blue
  near `1.25`, green `1.0`), so planted stars' red/green and blue/green ratios
  match the color model within 3% and the three sky medians agree within
  `0.002`. With the red sky raised by `0.03` over the left three quarters, a
  background ROI over the right quarter makes that quarter's sky neutral, while
  measuring the whole frame does not. Every integer and floating sample type
  gives the same ratios within 3% (8% for 8-bit data) and keeps its sample
  type. A mask that is black over the left half leaves that half unchanged. A
  catalog holding only 4 of the stars refuses the run and changes nothing. The
  result's cards record `pcc-gaia-bp-rp-v1`.
- Inspect at 100%/statistics/history: for real use, verify overlay circles land
  on stars, compare normalized shadows/highlights and channel statistics with
  one display stretch, and record the reported count/gains and history step.
- Limitations: synthetic agreement proves implementation equations and failure
  paths, not sensor-independent color accuracy, catalog completeness, visual
  quality, real-field robustness, or production runtime.

## Warnings and limitations

- Apply is destructive and there is no calibration preview. Save a copy or be
  ready to undo through Image History.
- Use linear broadband RGB. The window refuses an image marked nonlinear, but
  not an image whose linearity is unknown or an image that PCC already
  calibrated; repeated or nonlinear corrections can compound clipping and color
  error.
- PCC's exponential BP-RP ratio model is AfterNight-specific and independent of
  the user's camera/filter response. Use SPCC when supported spectral response
  assets and its stronger readiness/diagnostic gates are required.
- The minimum is only 10 usable samples. There is no displayed residual,
  accepted color span, uncertainty, spatial coverage, saturation, SNR, or Gaia
  quality diagnostic. A successful message is not proof of a scientifically
  good solution.
- Missing BP-RP uses a neutral color. Yellow overlay circles reveal those
  stars, but the UI does not report their count separately; the status only
  says whether they were the majority.
- Full-image background statistics can be biased by gradients, large objects,
  borders, stars, or masks. The ROI affects only neutralization and the active
  target mask affects only final blending.
- Every sample type is held in `[0,1]` of full scale. Inspect shadow and
  highlight clipping after gains/offsets, especially when reported gains exceed
  one, and normalize floating-point images before calibrating them.
- Closing the window stops a run, but a run whose work had already finished is
  still shown. Inspect Image History after a close during execution.
- Sequence-wide processing is absent. Over a sequence view only the shown frame
  changes, and only when it carries a plate solution. Calibrate the integrated
  still, not a sequence view.
- Overlay, status, and messages are transient; save the image to retain
  pixel/provenance changes and record important run settings separately.

## Troubleshooting

| Symptom or exact error | Verified cause/validation path | Recovery | Evidence source |
| --- | --- | --- | --- |
| “Photometric color calibration only supports RGB images.” | The target is not three-channel RGB. | Select an open RGB image. | Target checks; `WhatTheWindowOffersFollowsTheTarget` (grey case). |
| “Photometric color calibration is not applicable to narrowband data.” | The filter-name heuristic recognized a narrowband, dual-band, or tri-band filter. | Use a broadband RGB target; correct demonstrably wrong filter metadata outside PCC. | Engine heuristic; `WhatTheWindowOffersFollowsTheTarget`. |
| “The image is not plate-solved. A valid WCS solution is required for photometric calibration.” / **Apply** disabled | No plate solution could be read from the target's WCS cards. | Choose **Plate Solve...**, validate the solve, then return to PCC. | Target checks; `WhatTheWindowOffersFollowsTheTarget`. |
| “Photometric color calibration requires linear data; this image is marked nonlinear.” | The image's meaning, description, or `ANLIN` card marks it nonlinear, as a stretch records. | Calibrate the linear image: undo the stretch through Image History or reopen the linear master. The window has no override. | Target checks; `WhatTheWindowOffersFollowsTheTarget`. |
| “The Gaia DR3 catalog is not configured or not found. Configure it in the Plate Solving settings page.” | The configured directory is missing, malformed, incompletely installed, or in a layout this version does not read. | Choose **Configure Gaia Catalog...** and select/install a complete chunk catalog in Plate Solving settings. | Gaia settings source; `WhatTheWindowOffersFollowsTheTarget`. |
| “The PCC background ROI must be fully inside the target image and have a non-zero size.” | Width or Height is zero, or the rectangle extends outside the target. | Re-enable ROI for a fresh full-frame rectangle or redraw **From Selection** completely inside the image. | Process source. |
| “Another process is executing for this image, please cancel that job first before running this process” | Another edit or process holds the target. | Let it finish or cancel it, then Apply again. | View/process source. |
| Error “...a channel yielded no star to calibrate on” | Detection found no star in one channel. | Verify focus, channel balance, and broadband RGB; an empty or very faint channel cannot be calibrated. | Engine source. |
| Error “...no green star matched a catalog entry with a magnitude” or “...the Gaia catalog holds no star in the region” | The plate solution is wrong, the catalog does not cover the field, or **Magnitude limit** is too bright for the field. | Verify the plate solve and catalog coverage, then cautiously deepen the G limit. | Engine/catalog source. |
| Error “...fewer usable colour samples than the fit needs” | Fewer than 10 matched stars had red and blue companions within 3 pixels and finite positive fluxes. | Verify WCS/overlay and channel registration, then cautiously deepen G limit; change match radius only if WCS accuracy supports it. | `ARunTheCatalogCannotSupportSaysWhyAndChangesNothing`. |
| Status says “Calibrated with the neutral color default for most stars.” / many yellow circles | At most half the usable samples had finite BP-RP; missing colors used `C=0`. | Verify the configured Gaia catalog pack contains BP/RP for this field and prefer a run dominated by green circles. | Fit/overlay source. |
| Background remains colored or becomes worse | Neutralization was off, or global/ROI samples were contaminated. | Undo; correct gradients/borders, choose a clean representative ROI, and compare with/without neutralization. | `TheBackgroundRegionIsWhereTheSkyIsMeasured`. |
| Stars/shadows clip after Apply | Gain-plus-offset result left `[0,1]` of full scale; every sample type is held there. | Undo; inspect reported gains/matches, fix input/WCS/ROI, normalize a float image, and avoid repeated PCC passes. | Apply source; `PhotometricWindowTest` (every sample type). |
| “Calibration finished, but its result was not applied: the target image was closed or changed, or its history could not record the step.” | The work succeeded, but the target closed or changed, or Image History did not record the step, before the result could be shown. | Select the target again and repeat Apply. | Process/view source. |
| A result appeared although the window was closed | The run's work had finished before Close; it is still shown and recorded. | Inspect Image History and undo if the result is unwanted. | Process close source; `AStoppedRunPublishesNothing`. |

## Related workflow steps

Use [Background Extraction](../background-extraction/background-extraction.md) before PCC
when gradients would bias the target/background. [Plate Solving](../registration-alignment/plate-solving.md)
provides the required WCS, while [Catalogs and Data and Plate Solving Settings](../../interface/settings-dialog.md#plate-solving-settings)
explain local Gaia
readiness. PCC normally follows integration/cropping and precedes
[Histogram Transformation](../stretching/histogram-transformation.md)
or another nonlinear stretch.

Choose [Color Calibration](color-calibration.md) for explicit white/background
references without a catalog, [Background Neutralization](background-neutralization.md)
for an offset-only correction, or
[Spectrophotometric Color Calibration](spectrophotometric-cc.md) for a response-
aware Gaia XP workflow with stronger input/readiness/diagnostic gates. Do not
run PCC and SPCC sequentially on the same finished target.

## References and provenance

- AfterNight implementation and focused test files;
  algorithm version `pcc-gaia-bp-rp-v1` is the authoritative current project
  provenance.
- ESA/Gaia/DPAC, [Gaia DR3 Photometric System and G/BP/RP passbands](https://www.cosmos.esa.int/web/gaia/dr3-passbands),
  checked 2026-07-13. This defines the Gaia band identities; it does not define
  AfterNight's exponential PCC response approximation.
- ESA/Gaia/DPAC, [Gaia Data Release 3 documentation](https://gea.esac.esa.int/archive/documentation/GDR3/),
  checked 2026-07-13. Used for Gaia DR3 catalog and J2016.0 reference-epoch
  context.
