# Spectrophotometric Color Calibration User Workflow

Draft workflow and input requirements for the current AfterNight Spectrophotometric Color Calibration process.

SPCC calibrates a linear RGB astro image by matching image stars to Gaia XP sampled spectra, integrating those spectra
through a selected camera/filter response profile, measuring image-star fluxes, fitting robust RGB gains, and applying
the calibrated result back to the target image.

SPCC's production release gate (real-image validation, external comparisons, support envelope, packaged QA, as
`tools/check_spcc_release_readiness.py` checks them) is not yet passed. The app offers the approved
`spcc-gaia-dr3-xp-sampled-full-2026b` catalog and `afternight-spcc-response-baseline-2026a` responses, and readiness
still blocks unsupported image states, unreviewed response assets and uncovered fields.

For catalog pack selection and runtime catalog requirements, see `docs/dev/SPCC_CATALOG_REQUIREMENTS.md`. For
response-curve provenance and licensing rules, see `docs/dev/SPCC_RESPONSE_PROVENANCE.md`. For unsupported mono and
narrowband scope, see `docs/dev/SPCC_MONO_NARROWBAND_NOTES.md`.

## When To Use SPCC

Use SPCC when all of these are true:

- The target image is RGB, linear, and unstretched.
- The image has a plate-solve/WCS solution.
- The RGB channel basis is known and compatible with the selected response profile.
- The image has not already been PCC/SPCC color-calibrated.
- A local Gaia XP sampled SPCC catalog or installed regional cache is selected; with none installed, the run fetches the
  approved catalog's metadata and the chunks the image needs when it starts.
- A response repository is selected and contains a response profile for the image camera/filter metadata, or a manual
  profile is chosen.
- The field has enough usable catalog/image stars for the current fit thresholds.

Use PCC or another workflow instead when the image is nonlinear/stretched, monochrome, narrowband, missing WCS, already
color-calibrated, or from an unknown channel pipeline that cannot be assigned a truthful linear RGB state.

## Required Inputs

### Target Image

The target image must be linear RGB. AfterNight-native images should carry image-state metadata through the processing
pipeline. Imported FITS/XISF images with unknown state must be assigned a linear RGB state only when the user knows the
data has not been stretched, display-space converted, or color-calibrated.

The process can mark an imported target as linear RGB from the readiness panel. That assignment records user-confirmed
linearity/channel-basis provenance on the image metadata; it is not a per-run override.

### WCS Quality

SPCC requires a plate-solved image. The process rematches projected catalog stars against detected image stars before
sampling. The default preflight settings are:

- Magnitude limit: chosen from the WCS footprint and detected-star density, capped at Gaia `G <= 17.6`
- Match radius: `5.0` arcsec
- Max detected stars: `4096`
- Minimum detected-star SNR: `5.0`
- Max residual: `3.0` px and `3.0` arcsec

If preflight fails, solve the image again, improve star detection quality, or use a field/catalog region with better
coverage.

### Catalog Data

Select an SPCC Gaia XP sampled catalog from `Settings -> Catalogs & Data`. The current supported local paths are:

- Local full sampled-XP install
- Installed regional sampled-XP cache

The current app default can offer the approved full-sky managed-download descriptor
`spcc-gaia-dr3-xp-sampled-full-2026b`. Additional local or regional descriptors are shown only when their mirror config
passes descriptor, rights, checksum, release-status, and safe-path validation.

Regional caches must cover the target image footprint. When chunks the field needs are missing and an approved managed
descriptor covers the pack, the run downloads them; otherwise repair, reinstall or replace the pack from Catalogs & Data.

Catalogs & Data also includes the Gaia DR3 astrometric/PCC catalog used by internal plate solving. Keep that catalog
selected for solving, but do not treat it as the SPCC spectral catalog; SPCC requires the separate Gaia XP sampled
catalog section. Settings -> Plate Solving configures solver behavior and external solver paths; Gaia catalog install
and selection live in Catalogs & Data.

From the SPCC readiness panel, `Download Needed Region` derives the target's WCS footprint and opens Catalogs & Data
with a pending regional-cache request summary. If an approved downloadable regional descriptor covers that footprint,
Catalogs & Data enables `Download Matching Region...` and installs that prebuilt pack through the checksum-verified SPCC
catalog downloader. If no approved descriptor is available, use an existing local/full or manually generated regional
cache.

### Response Profile

Select an SPCC response repository in Catalogs & Data. The process resolves the target image camera/filter metadata
against the repository profile aliases. If no exact match is found, or if the metadata is ambiguous, choose a profile
manually from the readiness panel. Saved mappings are keyed by camera/filter metadata, repository ID, and alias registry
version.

The selected profile must include the response components and white reference needed to integrate catalog spectra into
expected RGB ratios.

### Star Coverage And Fit Support

The current robust-fit defaults are:

- Hard minimum: `35` used samples
- Low-support warning threshold: `60` red/blue inliers
- Minimum catalog `R/G` and `B/G` ratio span: `0.15`
- Outlier clipping: `3.0` sigma, `4` iterations
- Maximum relative gain uncertainty: `5%`

Fits below the hard minimum fail. Successful fits with 35-59 used samples are treated as provisional and report sparse
field guidance. Increase catalog coverage, use a wider field, or confirm regional-cache coverage if low-support warnings
appear.

## Workflow

1. Open the target RGB image.
2. Confirm the image is linear, unstretched, plate-solved, and not already color-calibrated.
3. Install or select an SPCC catalog and response repository from `Settings -> Catalogs & Data`.
4. Open `Color Calibration -> Spectrophotometric Color Calibration`.
5. Select the target image in the process window.
6. Read the readiness panel. Resolve any catalog, response-profile, image-state, or WCS blockers before applying.
7. Adjust catalog/WCS, photometry, or fit controls only when the defaults are inappropriate for the image.
8. Click `Apply`.
9. Monitor staged status text while catalog query, source detection, WCS preflight, photometry, fitting, and image
   application run.
10. Review diagnostics after completion.

If the target came from an ImageIntegration workflow with compatible persisted Gaia `source_id` handoff data, SPCC first
tries those validated references. If too few references are available or the constrained run fails and fallback is
allowed, SPCC runs the normal WCS region-query path and reports the fallback in diagnostics.

## Result And Diagnostics

On success, SPCC updates the target image and records a process-history step. The output metadata records the applied
gains, background offsets, response profile, white reference, catalog family, catalog manifest, catalog access profile,
algorithm version, input image state, and calibration provenance.

Diagnostics include:

- Quality score
- WCS residual and matched-star counts
- Accepted/rejected sample counts
- Applied RGB gains and background offsets
- Fit warnings and rejected outlier source IDs
- Catalog/response/image-state readiness messages
- ImageIntegration handoff/fallback messages, when present

Treat sparse-field, excessive gain-uncertainty, catalog-miss, stale WCS, or response-profile warnings as reasons to
inspect the result before continuing.

## Common Blockers

| Message Or Symptom | Typical Action |
| --- | --- |
| No installed Gaia XP sampled SPCC catalog is selected | Select or install a catalog in Catalogs & Data |
| Regional cache does not cover the target | Install a covering regional cache or use a local full catalog |
| Needed-region request is unavailable | Plate-solve the target image so the process can derive a WCS footprint |
| Response profile is unresolved or ambiguous | Choose a response profile manually or fix camera/filter metadata |
| Input image state is not compatible | Use a linear RGB target or assign linear RGB state only when it is true |
| Narrowband filter metadata is outside the support envelope | Use PCC or a dedicated narrowband workflow; do not run broadband SPCC |
| SPCC requires a plate-solved image | Plate-solve the image and rerun readiness |
| Too few accepted samples | Use a wider field, deeper magnitude limit, better WCS, or better catalog coverage |
| Excessive gain uncertainty | Inspect star quality, WCS residuals, response profile choice, and sample coverage |
| Target image was closed or changed before result application | Reopen or reselect the target and rerun |

## Current Limitations

- SPCC release readiness still depends on the current validation manifest, response assets, packaged install checks, and
  support-envelope evidence for the advertised catalog/response data.
- Real-image validation across cameras, fields of view, and external reference implementations is still open.
- PSF-derived flux is not enabled as the v1 fit source; aperture photometry remains the current baseline.
- Monochrome, narrowband, nonlinear, display-space, and unknown-channel-basis inputs are outside the current SPCC support
  envelope.
- Managed-remote catalog chunk performance is not implemented yet.

## Implementation

The window is
`src/AfterNightProcesses/processes/SpectrophotometricColorCalibration/SpectrophotometricColorCalibrationProcess.cpp`; it
reads the field's catalog chunks through `SpccCatalogSource.h`, the response repository's profile and white reference
through `SpccResponseRepository.h`, decides readiness on a worker (`SpccProcessReadinessPanel.cpp`) and calls the
engine's `planSpectrophotometricColorCalibration` and `calibrateSpectrophotometricColor`, sharing the target checks and
the calibrated metadata with Photometric Color Calibration through
`src/AfterNightProcesses/processes/PhotometricCalibrationSupport.h`. "Photometric and spectrophotometric colour calibration" in `docs/dev/guides/PRODUCT_ARCHITECTURE.md`
describes the product's side; the engine's is documented in the headers under
`src/AfterNightEngine/include/AfterNightEngine/Algorithms/Spectrophotometry/`.
