---
kind: process
stage: color
menu: Process > Color Calibration > Spectrophotometric Color Calibration
summary: >-
  Calibrates the colors of a plate-solved linear RGB image against star colors computed from Gaia DR3 spectra through
  your sensor's and filters' responses.
data_in: linear
derived: true
---
# Spectrophotometric Color Calibration

<!-- MEDIA_SLOT: UG-MEDIA-050 -->

## Purpose and expected result

Spectrophotometric Color Calibration (SPCC) calibrates a plate-solved, linear
RGB image by comparing measured image-star color ratios with synthetic
ratios integrated from Gaia DR3 XP spectra through a selected sensor/filter
response and white reference.

On success, **Apply** replaces the image in the view with the calibrated RGB
image as one image-history step, writes versioned calibration metadata, and
reports the gains it found. A mask on the target weighs the result. On
failure or cancellation the image and its history are left exactly as they
were.

The current implementation is a validation/prototype path. Catalog and response
assets can pass runtime readiness, but the repository's production evidence
manifest still has no real-image reference set, external comparison, support-
envelope report, or packaged-QA report. This page therefore explains verified
implementation behavior without claiming production color accuracy.

## At a glance

| Item | Current behavior |
| --- | --- |
| Menu path | **Process > Color Calibration > Spectrophotometric Color Calibration** |
| Enabled when | An image view is active. The window works only on a three-channel RGB image (for a sequence, the frame it shows): over any other image **Apply** stays disabled and the status asks for an RGB target. It needs the view to itself and closes if the selected target becomes unsupported. **Apply** is enabled once the readiness, decided on a worker, reports a ready or preparable catalog, a resolved and compatible response profile with its white reference, and a compatible image state. The plate solution is not part of the readiness: an unsolved image is refused by name when **Apply** is pressed. |
| Inputs | A linear, unstretched RGB image whose cards state a plate solution; an approved local Gaia DR3 XP sampled catalog that covers the field, or an approved managed descriptor from which the missing chunks can be downloaded; a reviewed runtime response repository; a compatible response profile; and a white reference. |
| Outputs | The calibrated image replaces the image in the view as one undoable history step, in the image's own representation, marked color calibrated in its meaning, cards, and lineage; diagnostics, status, and a completion message. No separate report file or new image view is created. |
| Recommended image state | Linear RGB after integration, gradient correction, and plate solving, before stretching. The target must not already be color calibrated (by PCC, SPCC, or any calibration its meaning or cards record) and must not name a narrowband filter. |
| Preview support | None. **Apply** runs the full workflow; there is no real-time or full-image preview. |
| Sequence support | **Apply** on a sequence view calibrates every frame as a still image of it. Each frame needs its own plate solution, and the whole calibrated sequence is published or nothing; a recording whose frames carry no plate solution is refused. |
| Mask support | The target's mask weighs the result pixel by pixel: where the mask is black the source is kept, where it is white the calibrated value. Stars, fit, and sky are measured on the whole image whatever the mask. |
| External dependencies | Local Gaia DR3 XP sampled `.anxp` catalog data and local response/white-reference JSON assets. Managed downloads use network and persistent cache storage when explicitly requested or when approved missing catalog chunks are prepared on **Apply**. |

## Before you start

1. Use a broadband RGB master that is still linear. Unknown, nonlinear,
   display-space, narrowband, or already color-calibrated state is blocked.
2. Plate solve the final target. SPCC reads the solution from the image's
   cards and does not trust it alone: it detects stars and rematches projected
   catalog sources before photometry.
3. In **Settings > Catalogs & Data**, select or install an SPCC Gaia XP sampled
   catalog and response repository. The ordinary Gaia `.ansp` catalog used by
   Internal Gaia/PCC is not an SPCC spectral catalog.
4. Confirm that the catalog covers this WCS footprint. A missing or partial
   managed catalog is runnable only when an approved descriptor can download the
   chunks the field needs; a regional cache must explicitly cover the field.
5. Choose a response matching the sensor and filter train. Exact unambiguous
   image metadata can resolve a profile; ambiguous or missing metadata requires
   manual selection, which is remembered for that camera and filter. Do not
   substitute an arbitrary response to make readiness turn green.
6. Assign **linear RGB** state only when you know the import has not been
   stretched, display-converted, or color calibrated. The assignment is an edit
   the image history records, with the file or view and the revision of the
   image you looked at as its evidence; it is not a harmless one-run override.
7. Keep the linear master file. **Apply** changes the image in the view; the
   step can be undone from the image history, but saving afterwards writes the
   calibrated result, held to its range as described below.

## Procedure

1. Activate the solved linear RGB image and open **Process > Color Calibration
   > Spectrophotometric Color Calibration**.
2. Wait for the readiness. It is decided on a worker each time the target, its
   state, the catalog/response settings, or a selection changes, and **Apply**
   stays disabled until the latest decision lands. Read the readiness banner and
   resolve catalog, response, white-reference, image-state, or coverage blockers
   before changing fit thresholds.
3. Select the correct **Sensor** and conditional OSC/DSLR or mono RGB filters.
   Use **Response profile** only for repositories that do not expose structured
   component choices. Confirm **White reference** and the response-details line.
4. Expand **Catalog and WCS Preflight**. Start with **Choose magnitude limit
   automatically** selected and the runtime defaults. The **Magnitude cap** is
   still the maximum allowed automatic depth even though its editor is disabled.
5. Expand **Photometry and Fit**. Start with the defaults and leave **Apply
   background neutralization** selected only when global background statistics
   are meaningful for this image.
6. Confirm **Target image**, then choose **Apply**. All parameters and readiness
   controls are locked. If the readiness said catalog chunks will be prepared,
   their checksum-verified download runs first, shown in the window's progress
   bar by bytes and files.
7. The calibration then runs as the process framework's **Apply**: a progress
   dialog follows the engine's eight stages (star detection, source selection,
   catalog rematch, catalog colors, aperture photometry, fit, sky measurement,
   and pixel application), and its **Cancel** stops the run. While a download or
   run is busy, **Close** reads **Cancel**.
8. Read **Diagnostics** and the completion message. On success, inspect the
   quality score, accepted samples, WCS residuals, R/G/B gains, offsets,
   guidance lines, and the history step. Treat low-support guidance as a reason
   to verify catalog coverage and star quality rather than as proof of a good
   calibration.

## Complete parameter and action reference

| UI label/action | Type and unit | Runtime default | Persisted-default exception | Valid values, step, or constraints | Effect of increasing, decreasing, or selecting | Dependencies and enable/visibility rules | Quality, artifact, runtime, memory, or disk trade-off | Scope: preview/apply, channel, mask, still/sequence | Persistence |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Readiness banner | Status | Current blocked/warning/ready summary | Reflects persistent catalogs, profiles, mappings, and image metadata | Repository- and target-derived text | Identifies the highest-priority prerequisite, or says what the run will prepare first. | Visible only when the summary is nonempty; tone is Ready, Warning, or Blocked. Decided on a worker after every change of the target, its state, the settings, **Match radius (arcsec):**, **Choose magnitude limit automatically**, a selection, or **Reset**; only the latest decision is shown, and **Apply** waits for it. Hidden while the image shown is the one this window just calibrated. | Follow the named blocker; the banner is not a fit-quality result. | Dialog/readiness; the frame shown for a sequence | Recomputed; not independently persisted. |
| **Sensor:** | Conditional choice | Metadata/saved/manual resolution when available; otherwise **Select sensor** | Saved camera/filter mapping can pre-resolve a profile | Repository-defined OSC or mono sensor components | Chooses the sensor family used to construct a throughput profile. | Visible only when ready response assets expose structured profiles. Disabled while busy. | A mismatched sensor response biases synthetic colors and gains. | Apply; all RGB channels | Manual choice can save a mapping when camera metadata, repository ID, and alias-registry version are available. |
| **OSC filter:** | Conditional choice | First compatible option or resolved profile component | Saved mapping can preselect | Repository-defined; can include **No OSC filter** | Selects the additional OSC-filter component. | Structured OSC sensor only; hidden if no meaningful choice. | Incorrect optical-train response changes predicted ratios. | Apply; RGB response | Saved with the resolved manual profile mapping when eligible. |
| **DSLR LPF:** | Conditional choice | First compatible option or resolved profile component | Saved mapping can preselect | Repository-defined; can include **No DSLR LPF** | Selects the low-pass/window component combined with sensor/filter response; a sensor/filter/window profile the repository does not hold is synthesized from its three measured profiles. | Structured OSC sensor only; hidden if no meaningful choice. | Same response-model accuracy trade-off as **OSC filter**. | Apply; RGB response | Saved through the resulting profile mapping when eligible. |
| **Red filter:** | Conditional choice | First compatible component or resolved profile component | Saved mapping can preselect | Repository-defined red-channel mono filters | Selects the red filter in a mono-sensor RGB set. | Structured mono sensor only. | Wrong filter curves bias R/G/B synthetic ratios. | Apply; red response | Saved through the resulting profile mapping when eligible. |
| **Green filter:** | Conditional choice | First compatible component or resolved profile component | Saved mapping can preselect | Repository-defined green-channel mono filters | Selects the green filter in a mono-sensor RGB set. | Structured mono sensor only. | Green is the ratio reference, so a mismatched curve affects both fitted color axes. | Apply; green response | Saved through the resulting profile mapping when eligible. |
| **Blue filter:** | Conditional choice | First compatible component or resolved profile component | Saved mapping can preselect | Repository-defined blue-channel mono filters | Selects the blue filter in a mono-sensor RGB set. | Structured mono sensor only. | Wrong filter curves bias synthetic B/G ratios. | Apply; blue response | Saved through the resulting profile mapping when eligible. |
| **Response profile:** | Conditional choice | A choice made in this window, else a saved mapping, else an exact alias, else unresolved | Saved mapping can preselect, and wins over an exact alias | Repository-defined profiles plus **Select response profile** | Selects one immutable throughput-profile identity when structured component choices are unavailable. | Visible only for ready, unstructured profile lists. Execution stays blocked until resolved, reviewed, and compatible with the image's channel basis and any recorded RAW white balance or camera matrix. Two exact aliases are ambiguous. | This is a scientific input, not a cosmetic preset. | Apply; all RGB channels | Manual mapping persists only when its metadata/repository identity can be keyed safely. |
| **White reference:** | Conditional choice | Profile default/resolved white reference | A saved mapping or explicit selection can override | Repository-defined white-reference spectra plus **Select white reference** | Sets the spectral reference where the fitted channel relationship is evaluated. | Visible when ready response assets contain white references; required for **Apply**. | Different legitimate references target different calibrated balance; missing/invalid data blocks. | Apply; gain solution | Can persist with a manual camera/filter/profile mapping. |
| Response/white-reference details | Read-only status | Resolved selection | Reflects saved/manual/metadata origin | Response ID, white reference, and selection origin (manual, saved mapping, or image metadata) | Confirms the actual identities used. | Visible only when a profile is selected. | Record these identities with diagnostics for reproducibility. | Dialog/readiness | Recomputed. |
| **Open Catalogs & Data...** | Conditional action | Hidden when not required | Uses persistent settings | Opens **Settings > Catalogs & Data** | Lets the user select/install/repair catalog or response roots; the readiness is decided again when the page returns. | Shown for a catalog blocker that needs the user: not installed and not preparable, rights not approved, or the field not covered with no approved download. | Settings changes affect disk/network and all later SPCC windows. | Setup, not Apply | Catalog/response settings persist globally. |
| **Download Offline Catalog** | Conditional action | Hidden when not required | Uses persistent settings | Opens **Catalogs & Data**; it does not itself start the full download | Provides the full-catalog management route. | Visible for the same catalog blockers as **Open Catalogs & Data...**. | A full sampled-XP catalog can require substantial disk and network; inspect the download dialog's actual size. | Setup, not Apply | Any completed selection/install persists globally. |
| **Download Needed Region** | Conditional action | Hidden when not required | Saves a pending regional request | Requires a plate solution, a valid footprint, and savable global settings | Derives center/radius/magnitude from the target, stores the request, then opens **Catalogs & Data**. | Visible when the installed catalog does not cover the field and no approved managed descriptor can prepare the missing chunks. | A matching approved prebuilt region is smaller than a full catalog; absence of a matching descriptor still blocks. | Setup for the current image | Pending request persists globally until handled/cleared. |
| **Download Response Profiles** | Conditional action | Hidden when not required | Installs to managed cache and saves its path | Requires an approved current managed descriptor; download can be cancelled | Downloads and checksum-verifies the preferred approved response repository directly from the process; on success clears this window's manual selections and decides the readiness again. | Visible only when response assets are missing or incomplete and managed preparation is available. | Uses network/disk; failure leaves no completed promoted install, and an existing install is preserved by downloader contracts. | Setup, not image Apply | Successful managed repository path persists globally. |
| **Assign Image State** | Conditional action | Hidden for compatible or already-calibrated state | Records an edit after confirmation | **Yes** or **No** in **Assign Linear Image State** | Marks the current RGB image linear in a linear-RGB basis, with the attestation (the file or `view:` and view name, the revision of the image you looked at, `AfterNight user`, the UTC time, and a warning text) and a lineage entry, as a history step named **Assign Linear Image State**. | Visible for any incompatible image state that is not already color calibrated: unknown or nonlinear linearity, an unknown or incompatible channel basis, an incomplete attestation, or a narrowband filter name (which attesting does not change). Refused, with nothing changed, when another edit changes the image before the attestation lands. | A false assignment invalidates the scientific premise; use only with external knowledge. | Current still | History step; the cards persist when the image is saved. |
| **Catalog and WCS Preflight** | Collapsible-section action | Collapsed | None | Expanded/collapsed | Shows or hides seven preflight controls. | Always available while idle. | No processing effect by itself. | Dialog only | Dialog lifetime. |
| **Choose magnitude limit automatically** | Boolean | Selected | None | Selected/unselected | Selected: queries Gaia G from 8.0 upward in 0.5 steps until the field returns twice as many catalog stars as there are selected sources (at least 64, at most 4,096; 512 with no sources) or the cap is reached. Unselected: uses the cap directly. | Disables **Magnitude cap** editing when selected; all controls lock while busy. | Deeper queries can load/match more sources and may increase runtime or confusion in crowded fields. | Apply; catalog query | Dialog lifetime; **Reset** restores selected. |
| **Magnitude cap:** | Gaia G magnitude | 17.6 | None | 6.0–20.0; step 0.1. With Auto selected, a cap below 8.0 is refused when the run starts. | Raises/lowers maximum query depth; when Auto is off it is the direct limit. Also sets the depth of a **Download Needed Region** request. | Editor enabled only when Auto is off, but its value still caps Auto. | Fainter limits can improve sparse-field support but increase query/match/photometry work and weak-source contamination. | Apply; catalog query | Dialog lifetime; **Reset** restores 17.6. |
| **Match radius (arcsec):** | Arcseconds | 5.0 | None | 0.5–30.0; step 0.5 | Changes the maximum detected-to-projected angular separation for candidate rematches, and widens the catalog cone the field reads and the readiness checks. | Always visible; idle only. A change decides the readiness again. | Too small loses matches; too large raises false-pair risk. Pairs are taken in increasing residual (ties by source order, then Gaia identifier), one per source and one per catalog star; a field with more than 4,000,000 candidate pairs is refused. | Apply; WCS preflight | Dialog lifetime; **Reset** restores 5.0. |
| **Max detected stars:** | Count | 4096 | None | 64–20,000; step 256 | Caps the stars kept from the green channel's detection and the signal-to-noise-sorted sources passed to matching. | Always visible; idle only. | Higher values cost detection/matching memory/time and can help dense fields; lower values can starve support. | Apply; image detection | Dialog lifetime; **Reset** restores 4096. |
| **Detected star SNR:** | Ratio | 5.0 | None | 1.0–100.0; step 0.5 | Raises/lowers the signal-to-noise floor a detected star must reach to become a matching source; stars with a clipped core are refused regardless. | Always visible; idle only. | Higher is cleaner but may leave fewer than 20 WCS matches/35 fit samples; lower admits weak or spurious detections. | Apply; image detection | Dialog lifetime; **Reset** restores 5.0. |
| **Max residual (px):** | Pixels | 3.0 | None | 0.1–20.0; step 0.1 | Sets the accepted WCS RMS pixel limit; maximum residual may be at most twice this value. | Always visible; idle only. | Tight limits reject weak WCS; loose limits can place apertures on wrong stars. | Apply; WCS gate | Dialog lifetime; **Reset** restores 3.0. |
| **Max residual (arcsec):** | Arcseconds | 3.0 | None | 0.1–30.0; step 0.1 | Sets the accepted WCS RMS angular limit; maximum residual may be at most twice this value. | Always visible; idle only. | Evaluate with the pixel limit and image scale; loosening can increase false correspondence. | Apply; WCS gate | Dialog lifetime; **Reset** restores 3.0. |
| **Photometry and Fit** | Collapsible-section action | Collapsed | None | Expanded/collapsed | Shows or hides seven photometry/fit controls. | Always available while idle. | No processing effect by itself. | Dialog only | Dialog lifetime. |
| **Minimum samples:** | Count | 35 | None | 5–500; step 1 | Changes the hard minimum usable samples/inliers required for both color fits, and the number of validated ImageIntegration hand-off sources needed before the run narrows to them. | Always visible; idle only. | Lowering below the tested 35-sample support envelope can make unstable fits appear executable; raising needs wider/deeper fields. The engine's low-support threshold is the larger of this value and 60, and **Diagnostics** prints sparse-field guidance whenever fewer than 60 inliers support the fit. | Apply; robust fit | Dialog lifetime; **Reset** restores 35. |
| **FWHM aperture scale:** | Multiplier | 2.0 | None | 0.5–5.0; step 0.1 | Sets aperture radius to scale × detected FWHM, clamped to 2–10 px. The annulus starts at max(7 px, 3 × FWHM), is 10 px wide, and may not reach beyond 64 px. | Always visible; idle only. | Too small misses flux; too large adds background/neighbors and increases edge rejection. | Apply; all image channels | Dialog lifetime; **Reset** restores 2.0. |
| **Annulus sigma clip:** | Sigma | 3.0 | None | 1.0–8.0; step 0.1 | Clips each channel's annulus samples once about their mean at this many standard deviations; the local sky is the median of what remains, and at least 8 samples must remain. | Always visible; idle only. | Lower clips more aggressively and can leave too few background samples; higher retains contaminating stars/outliers. | Apply; per-star RGB photometry | Dialog lifetime; **Reset** restores 3.0. |
| **Outlier policy:** | Choice | **AfterNight iterative** | None | **AfterNight iterative**, **Single-pass** | Iterative fits, clips, and refits over the survivors until the membership settles or the iteration limit is reached, then refits once more over the final membership; Single-pass fits once, clips once, and keeps that first line. | Always visible; idle only. | Iterative adapts to the retained set; single-pass is simpler but still requires the minimum inliers. | Apply; red/green and blue/green fits | Dialog lifetime; **Reset** restores iterative. |
| **Fit outlier sigma:** | Sigma | 3.0 | None | 1.0–8.0; step 0.1 | Multiplies the robust residual scale (1.4826022185056018 × the median absolute deviation) used to retain samples. | Always visible; idle only. | Lower rejects more samples; higher keeps larger color-ratio deviations. Both can fail through insufficient inliers or excessive gain uncertainty. | Apply; robust fit | Dialog lifetime; **Reset** restores 3.0. |
| **Fit iterations:** | Count | 4 | None | 1–20; step 1 | Limits iterative clipping/refit cycles. | Used by **AfterNight iterative**; retained in UI for Single-pass but its algorithm does only one clipping pass. | More cycles can refine outliers but cost fit time; convergence also stops when the membership stops changing. | Apply; robust fit | Dialog lifetime; **Reset** restores 4. |
| **Apply background neutralization** | Boolean | Selected | None | Selected/unselected | Measures each channel's sky as the median of the samples within 2.8 robust sigmas below and 2.0 above its median, over the samples the image's validity admits, with the window not clamped to [0, 1]; then adds offsets so the gain-scaled skies meet at their mean. Unselected writes zero offsets. | Always visible; idle only. No ROI is exposed by this process. | Useful on a representative background; gradients/large target structure can bias global statistics. An integer result is held to its range; a floating result is kept as computed. | Apply; full target RGB | Dialog lifetime; **Reset** restores selected. |
| **Diagnostics** | Read-only text | Empty before execution | None | Quality score, the run's refusal, WCS matches and residuals, accepted/rejected/usable samples, gains, offsets, coverage or sparse-field guidance, rejected outlier source IDs, and hand-off/fallback lines | Records why a run succeeded, warned, failed, or fell back. | Cleared at **Apply** start and filled when the run ends. A refused run shows its reason with zero counts and unit gains, because the engine's refusal carries no counts. | Copy important identities/counts externally; no separate report file is written. | Full Apply | Dialog lifetime. |
| **Target image:** | Choice | RGB view bound when opened | None | Supported open RGB still or sequence views | Changes the exclusive target and decides the readiness again. | Unsupported target closes the process; selector locks while busy. | A new target can require different WCS chunks and response mapping. | Full still, or every frame of a sequence | Dialog lifetime. |
| **Apply** | Action | Disabled until ready | None | One operation at a time | Downloads the approved missing chunks first when the readiness says so, then runs the framework's Apply: the edit holds the image, a worker calibrates a working copy, and the history records the step before the view shows the result. | Requires the latest readiness to have landed and be ready, a supported target, and idle state. A missing plate solution does not disable it; pressing it on an unsolved image is refused with **SPCC requires a plate-solved image.** | Undoable from the history; no change on failure or cancellation. Can use significant I/O, CPU, and memory. | Full still or every frame of a sequence; mask weighs the result; no preview | Result/history/metadata persist with the image when saved; the parameters stay as set. |
| **Reset** | Action | Available while idle | None | Restores all visible process defaults and clears this window's manual profile/white-reference selections | Decides the readiness again. | Disabled while busy. | Does not delete data or clear an already saved camera/filter mapping; such a mapping can resolve again immediately. | Dialog only | Process parameters reset; an **Apply** never resets them. |
| **Close** / **Cancel** | Action | **Close** | None | Label becomes **Cancel** while a download or run is busy | Idle closes. Busy cancels the download and the run's progress dialog; the window hides and closes once the run has ended. | Always enabled; cancellation can wait for the current cooperative boundary. | Verified downloaded chunks may remain cached; nothing is applied after cancellation. | Dialog/full Apply/download | Not persisted. |
| Status and progress | Read-only status | Readiness status; progress hidden | None | Status line, the window's progress bar, and the run's progress dialog | Shows readiness, download bytes/files, **Preparing SPCC workflow...**, **Running spectrophotometric color calibration...**, and the final status. | The window's bar is visible only during downloads; the progress dialog follows the engine's eight stages and offers **Cancel**. | A stage message is progress evidence, not a quality result. | Dialog/full Apply/download | Not persisted. |
| **Yes** / **No** in **Assign Linear Image State** | Conditional confirmation | **No** is the default | None | Warning question after **Assign Image State** | **Yes** records the attestation as a history step; **No** makes no change. | Only when the assignment action is available. | Incorrect confirmation can invalidate every subsequent fit assumption. | Current image | The attestation persists with the image's cards when saved. |
| **OK** in the completion or error message | Action | Available after the run ends | None | Dismisses the message | Success shows the information message **SPCC completed. Samples: N. Gains R=… G=… B=…**; a refusal before the run shows its reason under **Spectrophotometric Color Calibration**; a failed run shows the framework's **Error executing the process** message naming the refusal; a result computed but not applied shows a warning. | A cancelled run shows none; the success and not-applied messages are not shown once the window was closed. | Does not change the applied/discarded result. | Dialog only | Not persisted. |

The current build does not create **Observing Conditions** or **Narrowband**
sections: `kSpccProcessAtmosphericCorrectionControlsEnabled` and
`kSpccProcessNarrowbandControlsEnabled` are both false. A narrowband filter name
is refused, and every run uses an observation model with no atmosphere and no
narrowband passbands. Atmosphere/passband parameters remain engine capabilities,
not user controls, and are not part of the supported workflow.

## How the algorithm works

### Conceptual pipeline

1. On a worker, after every change: decide the readiness from the catalog's
   install, rights, and the chunks the field's cone needs (without hashing the
   chunks), the response repository, the profile resolution and its
   compatibility, and the image's declared state. When an approved managed
   descriptor can supply the missing chunks, **Apply** downloads and verifies
   them first.
2. On **Apply**, on the edit's worker: read the plate solution from the image's
   cards; hash every installed chunk of the catalog and hold it to the engine's
   readiness rule (local access profile, identity, checksums, approved rights,
   the field's chunks declared); read and decode the chunks the field's cone
   needs, and those holding the ImageIntegration hand-off's sources; load the
   response repository and resolve the profile (synthesizing an OSC
   sensor/filter/window profile from its three measured profiles when needed)
   and the white reference; plan the recipe, which refuses a narrowband filter
   name and integrates the white reference once.
3. Make a working copy: Float64 for a Float64 or 32-bit integer image and Float32 for any other,
   holding the samples as fractions of full scale (an unsigned integer divided
   by its largest value, a signed integer counted from its smallest value, a
   floating sample as it is), described as linear RGB.
4. Detect stars in the green channel, keep at most **Max detected stars**, and
   select as sources those reaching **Detected star SNR:** without a clipped
   core, in descending signal to noise. Query the Gaia XP catalog inside the WCS
   footprint, move each star to the observation epoch by its proper motion (the
   exposure start as a Julian year from J2000.0 at 2000-01-01T12:00, or the
   catalog's 2016.0 reference epoch when the image states none), project through
   TAN or TAN-SIP WCS, and pair sources with projected stars one to one in
   increasing residual.
5. Require at least 20 rematches across at least three image quadrants, finite
   residual statistics, RMS within both UI limits, and maximum residual within
   twice both limits; otherwise the quality gate refuses by name.
6. For each rematch, integrate the Gaia sampled spectrum through the selected
   R/G/B response and measure background-subtracted RGB aperture flux at the
   detected position. Aperture sampling rejects invalid geometry, edge-clipped
   annuli, too few background samples, and a sample the image's validity
   rejects or that is not finite; it masks the other matched stars from each
   annulus but does not enable aperture-level saturation, SNR, or blend
   rejection. Form R/G and B/G image/catalog ratios from the samples that remain.
7. Fit red and blue ratio relationships with a repeated-median line and robust
   clipping. Require sample support, a catalog color span of at least 0.15 per
   channel, both channel fits, finite gains, and at most 5% relative gain
   uncertainty; otherwise the fit is refused by its verdict's name.
8. Optionally measure the sky and derive shared-pedestal background offsets.
   Apply gains/offsets to the working copy on the operation's threads, keeping
   every result as computed, write it back to the image's own representation - an
   integer one saturating at its smallest and largest values - and
   record the calibration in its metadata. The framework weighs the result by the
   target's mask, records the history step, and then shows it.

If the target's file was written by Image Integration, the run locates the
workflow archive beside it and reads the Gaia `source_id` references its
registration matched. They are held to this image: the same spectrum catalog,
this plate solution (by a digest of the cards the solution writes and the image
size), the observation epoch, the proper-motion model, a residual of at most
3 px, and a separation of at most 5 arcseconds. When at least **Minimum
samples:** of them hold, the run calibrates against those sources alone, and if
that run fails (other than by cancellation) it runs again over the whole field;
with fewer, it calibrates over the whole field directly. Diagnostics record
each decision.

### Model and equations

For catalog star `i` and channel `c`, AfterNight integrates a photon-counting
synthetic response on the 343-sample Gaia XP grid (336–1020 nm, 2 nm spacing):

\[
S_{i,c}=\int F_i(\lambda)\,\lambda\,T_c(\lambda)\,d\lambda,
\]

where `F_i(λ)` is sampled spectral flux density and `T_c(λ)` is the composed
sensor/filter throughput. The implementation resamples response components to
the XP grid with Akima interpolation (linearly below five samples), uses zero
outside a component's range, forms the product, and integrates it with Akima's
piecewise cubic quadrature, computed in the engine with the same arithmetic as
the GNU Scientific Library's Akima interpolation. A star's flux is always weighted
by wavelength; a white reference's is too, unless its asset says its samples are
already photon weighted.

Aperture flux for star `i`, channel `c`, is

\[
A_{i,c}=\sum_p w_p I_c(p)-B_{i,c}\sum_p w_p,
\]

where `w_p` is a partial-pixel circle weight and `B_{i,c}` is the median of the
FWHM-scaled annulus after one clip about its mean. The aperture is centred on
the detected position in engine pixel coordinates, where the first pixel's
centre is at one half. Samples use image ratios `y_R=A_R/A_G`, `y_B=A_B/A_G`
and catalog ratios `x_R=S_R/S_G`, `x_B=S_B/S_G`.

Each channel fits a robust line

\[
y_c=a_c+b_c x_c.
\]

The robust scale that clipping and the gain uncertainty use is
1.4826022185056018 times the median absolute deviation of the residuals. At the
selected white reference's catalog ratio `x_{w,c}`, the raw corrective gain is

\[
g'_c=\frac{1}{a_c+b_c x_{w,c}},\qquad g'_G=1.
\]

Applied gains are max-normalized so the largest is one:

\[
g_c=\frac{g'_c}{\max(g'_R,g'_G,g'_B)}.
\]

With background neutralization, each channel's sky `B_c` is the median of the
samples inside `[m_c − 2.8 s_c, m_c + 2.0 s_c]`, where `m_c` is the channel's
median and `s_c` is 1.4826022185056018 times its median absolute deviation. The
window is decided once and is not clamped to [0, 1], so a linear frame whose sky
sits near zero keeps its negative samples in the estimate. With
`P=(B_R+B_G+B_B)/3`, the per-channel offset is

\[
o_c=P-g_c B_c,
\]

so the applied pixel model on the working copy is
`I'_c=g_c I_c+o_c`, with every value a fraction of full scale, and an integer
image saturates at its range when the copy is written back. A
sample the image's validity rejects is copied unchanged. This is a global
shared-pedestal model; the UI exposes no background ROI.

### Boundary and failure behavior

- Readiness blocks missing/unapproved catalog rights, an incomplete or invalid
  install, absent target coverage without an approved preparation path, missing
  or invalid response assets, unresolved/ambiguous/unreviewed/incompatible
  profiles, and unknown/nonlinear/already-calibrated/incompatible-basis/
  narrowband image state. It does not require the plate solution, which
  **Apply** checks before anything is read.
- At **Apply**, the installed catalog is held again to the engine's readiness
  rule with its checksums verified, and refused by the reason's name.
- WCS preflight supports TAN/TAN-SIP projection and applies Gaia proper motion.
  Unsupported/incomplete projection, fewer than 20 matches, fewer than three
  quadrants, or excessive/nonfinite residuals fail before sampling.
- Aperture radius is `clamp(scale × FWHM, 2, 10)` px. The annulus begins at
  `max(7, 3 × FWHM)` px and is 10 px wide. Edge clipping of the annulus rejects
  a sample. The process does not set finite saturation thresholds, a photometry
  SNR gate, or blend rejection; a sample the image's validity rejects, or one
  that is not finite, inside the aperture rejects the star.
- Fit refusals distinguish too few samples, too little catalog color span, too
  few inliers, excessive uncertainty, and nonfinite solutions; an invalid request
  is refused before the fit runs. Nothing is written to the working copy until
  every stage before the apply succeeds, and the image in the view changes only
  when the whole run succeeds.
- Cancellation is cooperative through downloads, every engine stage, and the
  apply. A stopped run publishes nothing, adds no history step, and shows no
  error. Verified cache pieces may remain.
- The run's edit stops when the view's image is closed or replaced under it. A
  result the run computed that the view did not show is reported as completed
  but not applied.
- On a sequence, every frame is held to being RGB, plate-solved, not
  narrowband by its filter name, and in a state SPCC accepts - linear, in a
  linear RGB or camera RGB basis, not already color calibrated - as the
  readiness holds the frame the view shows.

## Parameter interactions and starting points

| Combination | Conservative starting point | Too weak looks like | Too strong looks like | Adjustment order |
| --- | --- | --- | --- | --- |
| Catalog coverage + magnitude | Auto selected, cap 17.6; use a complete field footprint | Too few catalog candidates, matches, or accepted samples; low-support guidance | Crowded match set, more weak detections, longer query/photometry | 1. Coverage/chunks; 2. WCS; 3. magnitude; 4. detection cap/SNR. |
| Match radius + residual limits | 5 arcsec match; 3 px and 3 arcsec residual limits | Too few preflight matches despite a good solve | False matches and displaced apertures can pass a permissive candidate radius/gate | 1. Re-solve/inspect WCS; 2. image scale; 3. match radius; 4. residual gates. |
| Aperture scale + annulus clip | 2.0 FWHM; 3 sigma | Flux loss or many unstable ratios | Neighbor/background contamination or edge rejection | 1. Star FWHM/detection; 2. aperture scale; 3. annulus clip. |
| Minimum samples + fit clipping | Minimum 35; iterative, 3 sigma, 4 iterations | Outliers bias line or fit has insufficient support | Valid color extremes rejected; insufficient inliers | 1. Coverage/photometry; 2. keep 35; 3. inspect the refusal and guidance; 4. sigma/policy; 5. iterations. |
| Response + image channel basis | Exact reviewed sensor/filter response compatible with recorded basis | Readiness unresolved/incompatible or systematic residuals | There is no meaningful “stronger” response; an arbitrary profile makes the model wrong | 1. Image provenance; 2. sensor; 3. filters/LPF; 4. white reference. |
| Background neutralization + gradients | Correct gradients first; compare selected versus unselected only on a copy or by undoing the step | Residual channel-dependent background cast | Global offsets biased by target structure or gradient; clipping at zero or full scale | 1. Gradient correction; 2. verify linear state; 3. neutralization choice; 4. inspect background/statistics. |

Do not relax fit/WCS thresholds merely to obtain a green completion message.
Catalog coverage, response identity, WCS, and photometry quality come first.

## Inputs, outputs, and metadata

SPCC accepts one RGB image (a still, or each frame of a sequence) and keeps its
dimensions, channel count, representation, validity, acquisition record, plate
solution, and cards. It calibrates a working copy and writes it back to the
image's own representation, to the nearest value and saturated; the view then
shows the result as a new revision of the same image, so the view name does not
change and no output file is created. A sequence's calibrated frames are written
to a new temporary sequence file, which the view then shows.

An integer image clips at its smallest and largest values when the result is
written back; a floating image keeps what the gains and offsets make, negative
and greater-than-one values included.
A floating image stored in other units (for example ADU) is calibrated as if its
samples were fractions of full scale.

Successful metadata marks the meaning color calibrated and appends a lineage
entry for `an::calibrateSpectrophotometricColor` that names the recipe
`spcc-sampled-xp-color-calibration-v3`, the response profile, white reference,
and catalog identities with their versions, and the gains and offsets at full
precision. The cards record the calibration: the color calibration (`ANCCAL`,
spectrophotometric) with the processing state that records it, the applied
gains and offsets (`ANCCGN`, `ANCCOF`; the offsets are fractions of full
scale), the response-profile and white-reference IDs/versions,
the catalog family/manifest/access profile, the recipe version, the input's
linearity and channel basis as its processing-state cards recorded them
(`ANINLIN`, `ANINBAS`), and a checksummed calibration document.

The history step is named **Spectrophotometric Color Calibration** and described
`Response profile: <id>, White reference: <id>, Background neutralization: on`
(or `off`). It is recorded before the view shows the result and undoes to the
image as it was. The gains are not in the step's description; they are in the
status line, **Diagnostics**, the completion message, and the image's cards.
After a successful run the window recognizes the image it calibrated by the
revision its run published, so it keeps the success status instead of warning
that the image is already calibrated; any later edit makes it another image.

**Assign Image State** writes the attestation into the processing-state cards
(the source file, or `view:` and the view name; the revision of the image you
looked at, as asset, pixel, meaning, and validity generations; `AfterNight
user`; the UTC time; the tool; and a warning text), states the meaning's
transfer function linear, and appends a lineage entry.

Manual response mappings persist globally by camera/filter metadata,
repository ID, and alias-registry version, and a saved mapping wins over an
exact alias. Catalog/response roots, selected pack, and pending regional
requests also persist globally; a chunk download prepared on **Apply** saves the
catalog root and pack it installed. Ordinary SPCC parameters do not persist
between process windows.

## Performance

Cost comes from readiness decisions, catalog checksum verification and chunk
I/O, star detection, candidate matching, per-star synthetic integration and
aperture scans, robust fitting, the sky statistics, the working copy's
conversions, and a full three-channel pixel pass.

Each readiness decision runs on a worker: it reads the catalog manifest without
hashing chunks and loads the whole response repository, composing every profile
on the XP grid. A large repository makes each decision slower; a decision that a
later one overtakes still runs, but only the latest is shown, and **Apply** waits
for it.

**Apply** first hashes every installed chunk file of the selected catalog to
verify its checksums, then reads and decodes only the chunks the field's cone
needs (plus those holding the hand-off's sources) and builds one catalog for the
run; it loads the response repository again. On a sequence, all of this is
repeated for every frame. A partial cache may add network transfer and checksum
verification before computation.

Source detection is capped by **Max detected stars**. The preflight counts every
source/catalog pair inside the match radius and refuses a field with more than
4,000,000 of them; deeper magnitude limits and wider fields increase both sets.
Aperture measurements run as one batch on the operation's threads, the
repeated-median fit computes its per-point medians on the operation's threads,
and above 1,024 usable samples a channel's line is fitted through an evenly
spaced subset in catalog-ratio order, which bounds the quadratic cost; the
answers do not depend on the thread count. Final application scales as
`O(width × height × 3)`.

The worker reads the image under the edit's lease and holds a three-channel
Float32 working copy (Float64 for a Float64 or 32-bit integer image), the result in the image's
representation, the decoded chunks, the response repository, and star, sample,
and fit structures; the history keeps a copy of the state the step replaces.

Prototype one representative solved RGB still before a large master. Inspect
the download dialog's reported bytes rather than relying on a fixed catalog-size
estimate. No approved real-image runtime, peak-memory, hosted-download, or
packaged-performance measurement has been made for this page.

## Worked example

- Evidence mode: Source-and-test-derived.
- Input condition/provenance and settings: the `SpectrophotometricWindowTest`
  field, a 768×576 Float32 RGB image of 140 stars on a 48-pixel grid, each with
  a linear sampled XP spectrum, under a TAN plate solution of 2.4-arcsecond
  pixels turned 17 degrees about RA 180°, Dec +12°; its cards state linear RGB
  state and the camera **Test Camera**. A local-full spectrum catalog of those
  stars and a reviewed response repository with one color-sensor profile and a
  flat white reference are installed. Beyond each star's color, red is
  multiplied by 1.3 and blue by 0.75. The dialog keeps its defaults.
- Expected checkpoint: the readiness resolves the profile through the camera
  alias and the status reads **Ready to run SPCC with response profile … and
  white reference ….** After **Apply**, the sky-subtracted red, green, and blue
  of the checked stars scale by the gains that undo the casts and the white
  reference's color, normalized so the largest is one, within 2 percent; the
  three channels' sky medians agree within 0.002; **Diagnostics** lists the
  accepted samples; the image is marked color calibrated with recipe
  `spcc-sampled-xp-color-calibration-v3`; and the history holds one
  **Spectrophotometric Color Calibration** step that undoes to the original.
- Inspect at 100%/statistics/history: the same tests run the field as UInt8,
  Int16, UInt16, and Float64 (within 8 percent for UInt8 and 2 percent
  otherwise), with a half-masked target, a stopped run, 500 required samples
  (refused, image unchanged), an image with no state cards attested linear, a
  hand-off of 80 matching and of 80 stale references, a hand-chosen profile, and
  a two-frame recording without plate solutions. The UI history text and
  messages are source-derived.
- Limitations: this fixture is designed to prove implementation contracts, not
  color accuracy. The 2026-07-12 maintainer environment check found the
  selected 2026b local-full catalog cache incomplete (2 of 768 chunks) and the
  2026a response repository installed/ready (245 components, 145 white
  references, 2,342 profiles); it was not repeated for this review. No approved
  solved broadband RGB target was available, so no actual image result, visual
  comparison, or production timing is claimed.

## Warnings and limitations

- The repository's production checklist still lacks real-image validation,
  external comparisons, a support-envelope report, and packaged QA. Treat SPCC
  as validation/prototype functionality, not a certified color standard.
- **Apply** changes the image in the view. It is undoable from the history,
  but keep the linear pre-SPCC master file.
- An integer result is held to its range; a floating result is kept as
  computed, so check a floating image for negative or greater-than-one values
  before stretching it.
- Do not assign linear RGB state merely to bypass readiness. The action records
  the user assertion in metadata and can make an invalid workflow appear valid.
- Narrowband, mono, nonlinear, unknown-channel, and already color-calibrated
  inputs are outside the current support envelope. The hidden atmosphere and
  narrowband controls are not available.
- On a sequence, the readiness reads only the frame shown; the run holds every
  other frame to the same image rules as it calibrates it. Each frame needs its
  own plate solution.
- The response profile must describe the actual channel basis and optical train.
  Auto resolution accepts only exact unambiguous aliases; manual choices remain
  the user's scientific responsibility.
- A catalog's presence does not prove target coverage. Partial caches may need
  network access; installed regional caches must cover the WCS footprint.
- PSF flux is not used. The current baseline is aperture-sum photometry.
- The process exposes no background ROI, bad-pixel mask (the image's own
  validity is used), finite saturation thresholds, blend-rejection switch, or
  aperture-SNR control. Review sample rejection diagnostics and avoid
  clipped/crowded/edge stars.
- Background neutralization uses global image statistics. Correct gradients and
  large background contamination first.
- A refused run's **Diagnostics** give the engine's reason and any catalog
  coverage guidance, without sample counts or gains, because the refusal does
  not carry them.
- A manual mapping survives **Reset** in global settings and can resolve again.
  Manage the underlying metadata/repository mapping when it is wrong.
- Managed-remote streaming catalogs are not a current runtime access profile;
  supported execution uses local-full or installed regional/cache chunks.

## Troubleshooting

| Symptom or exact error | Verified cause/validation path | Recovery | Evidence source |
| --- | --- | --- | --- |
| **Apply** stays disabled just after opening or after a change | The readiness decision is still on its way on a worker. | Wait for it to land; an **Apply** asked for before then does nothing. | `SpectrophotometricWindowTest.ApplyWaitsForTheReadinessDecidedOnAWorker` |
| **Select an RGB target image to evaluate SPCC readiness.** | No supported RGB image is selected. | Select the linear RGB still or sequence; mono views are unsupported. | process support/availability source |
| **SPCC input image state is not compatible: …** | Linearity is unknown or nonlinear, the channel basis is unknown or incompatible, an attestation is incomplete, or the filter name is narrowband. | Use the correct linear RGB master. Assign state only when independently known true. | `WhatTheRunLacksIsRefusedByName`; image-state readiness tests |
| **SPCC input image is already color calibrated. Choose a linear RGB image to run SPCC again.** | The cards or meaning record a color calibration. | Use the uncalibrated linear master; no assignment is offered. | `WhatTheRunLacksIsRefusedByName`; `TheResultIsColourCalibratedAndRecordsTheRecipe` |
| **SPCC requires a plate-solved image.** | The image's cards state no usable solution, or a sequence frame carries none. | Plate solve the final master, save/reopen if needed, and confirm WCS metadata; use sequence frames that each carry a solution. | `WhatTheRunLacksIsRefusedByName`; `ASequenceWhoseFramesCarryNoSolutionIsRefused` |
| Catalog missing/incomplete/footprint message | Selected spectral pack is absent, invalid, unapproved, partially cached without preparation, or does not cover this WCS. | Open **Catalogs & Data**; install/repair a complete approved pack or request/download a covering region. | catalog readiness tests |
| **SPCC catalog readiness rejected the installed catalog: …** | At **Apply**, a chunk checksum failed or the install lags its manifest, the rights are not approved, the access profile is not local, or the field's chunks are not declared. | Repair or reinstall the pack from **Catalogs & Data**. | process source |
| Response repository/profile unresolved, ambiguous, or **…uses private or unreviewed response assets.** | Assets are absent/invalid or unreviewed, no exact alias matches, several aliases match, or the profile is incompatible with the image's basis. | Download/select reviewed response data, then choose the exact sensor/filter/profile and white reference. | `WhatTheRunLacksIsRefusedByName`; response readiness/resolution tests |
| **Error executing the process** naming **the world-coordinate quality gate refused: …** | `TooFewMatchedStars`, `PoorSkyCoverage`, `IncompleteProjection`, `UnsupportedDistortion`, `IncompleteResiduals`, `RmsResidualExceeded`, or `MaximumResidualExceeded`. | Re-solve; verify scale/SIP and star detection; adjust match/residual settings only with diagnostic justification. | WCS preflight source/tests |
| **Error executing the process** naming **the green channel yielded no star to calibrate on** or **the preflight matched no source to a catalog record** | Detection, source selection, or rematching found nothing. | Check the image, the SNR floor, the solution, and catalog coverage. | engine calibration source |
| **Error executing the process** naming **the spectrophotometric fit was refused: …** | `InsufficientSamples`, `InsufficientCatalogSpan`, `InsufficientInliers`, `ExcessiveGainUncertainty` (above 5%), or `NonFiniteFit`. | Check coverage, the photometry, and the response; use a wider/deeper valid field before lowering minimum or loosening clipping. | `AFitTheFieldCannotSupportSaysWhyAndChangesNothing`; fit tests |
| Background color becomes implausible, or highlights/floor clip | Global shared-pedestal estimate was biased by gradient/structure, or an integer result was held to its range. | Undo the step; correct gradients or rerun without neutralization. | background-neutralization source/tests |
| **SPCC cancelled.** | User closed/cancelled or a cooperative stop ended a download, a stage, or the apply. | Wait for the run to end, then rerun when prerequisites are stable. Verified cached pieces may be reused. | `AStoppedRunPublishesNothing` |
| **SPCC completed, but the target image was closed or changed before the result could be applied.** | The run computed a result the view did not show. | Reselect the intended unchanged target and rerun. | process finalization source/tests |
| **The image was not marked linear: it changed after it was attested, or it was closed. Attest it again.** | Another edit changed the image before the attestation landed, or the view closed, so the attestation was refused. | Attest the image again as it is now. | `AnAttestationOfAnImageThatChangedSinceIsRefused` |

## Related workflow steps

SPCC normally follows [Image Integration](../stacking-integration/image-integration.md),
gradient correction such as [Auto Gradient Removal](../background-extraction/auto-gradient-removal.md),
and [Plate Solving](../registration-alignment/plate-solving.md). Catalog and
response installation lives in [Settings](../../interface/settings-dialog.md).
After successful SPCC, keep the result linear for compatible linear operations,
then move to [Histogram Transformation](../stretching/histogram-transformation.md).
Use [Photometric Color Calibration](photometric-cc.md) or another supported
workflow when SPCC's image-state, response, catalog, or validation envelope is
not satisfied.

## References and provenance

- AfterNight implementation, tests and engine contracts, validation manifest,
  and production checklist are
  authoritative for current UI, thresholds, data formats, provenance, and
  limitations.
- [Gaia DR3 documentation](https://gea.esac.esa.int/archive/documentation/GDR3/)
  defines the release products, including externally calibrated sampled BP/RP
  mean spectra and DataLink access.
- Gaia Collaboration et al., [The Galaxy in your preferred colours: Synthetic
  photometry from Gaia low-resolution spectra](https://doi.org/10.1051/0004-6361/202243709),
  A&A 674, A33 (2023), is the primary synthetic-photometry reference.
- De Angeli et al., [Processing and validation of BP/RP low-resolution spectral
  data](https://doi.org/10.1051/0004-6361/202243680), A&A 674, A2 (2023), and
  Montegriffo et al., [External calibration of BP/RP low-resolution
  spectroscopic data](https://doi.org/10.1051/0004-6361/202243880), A&A 674,
  A3 (2023), describe XP processing and flux calibration.
- [Gaia DR3 credit and citation instructions](https://gea.esac.esa.int/archive/documentation/GDR3/Miscellaneous/sec_credit_and_citation_instructions/)
  require ESA/Gaia/DPAC credit and the applicable mission/release/product
  citations. AfterNight's catalog manifest/provenance gate preserves that review.
- Response data is versioned scientific input. The reviewed AfterNight response
  manifest records source/license/attribution/transformation/checksum metadata;
  private or unreviewed assets are blocked according to the local repository
  contract.
