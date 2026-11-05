# SPCC Catalog Requirements And Pack Recommendations

Catalog requirements and pack-selection guidance for the Spectrophotometric Color Calibration process.

This document complements:

- `docs/dev/SPCC_USER_WORKFLOW.md` for the end-to-end process workflow.
- `docs/dev/SPCC_CATALOG_DISTRIBUTION.md` for catalog generation, staging, upload, and verification commands.
- `docs/dev/SPCC_CATALOG_MAINTENANCE.md` for generation, validation, provenance, update, and rollback lifecycle.
- `docs/dev/SPCC_RESPONSE_PROVENANCE.md` for response-curve and white-reference licensing rules.
- `docs/dev/spcc/spcc-production-release-evidence-checklist.md` for the open production release gate.

SPCC catalogs are not interchangeable with the existing Gaia plate-solving/PCC catalogs. SPCC needs Gaia XP spectral
data, synthetic-photometry metadata, source-ID lookup support, region coverage metadata, and stricter provenance review.

## How the application reads a pack

The product reads a pack through `SpccCatalogSource.h`
(`src/AfterNightProcesses/processes/SpectrophotometricColorCalibration/`): the field's chunks are selected with
`selectSpectrumChunksForRegion` on a worker and handed to the engine's `planSpectrophotometricColorCalibration` and
`calibrateSpectrophotometricColor`, which open no pack file themselves. What the engine computes over a pack is
documented in `src/AfterNightEngine/include/AfterNightEngine/Algorithms/Spectrophotometry/SpectrophotometricColorCalibration.h`, and what the product does around it -
the target checks, the readiness decided on a worker, the asset revisions - in
"Photometric and spectrophotometric colour calibration" in `docs/dev/guides/PRODUCT_ARCHITECTURE.md`.

## Catalog Families

### `spcc_gaia_dr3_xp_sampled`

This is the current v1 catalog family. It stores Gaia DR3 sampled BP/RP spectra in `.anxp` chunks, plus sidecar metadata
needed for region queries, source-ID lookup, coverage inspection, and install repair.

Use this family for SPCC runs.

### Continuous-XP Experimental Packs

Continuous BP/RP reconstruction packs are documented as future/experimental. They may be useful for sparse-field
research, but they are not production-selectable in the current app path. They must remain blocked or explicitly marked
experimental until GaiaXPy/equivalent reconstruction, rights review, validation metrics, and performance behavior are
complete.

### Existing Gaia Astrometric Catalogs

Existing Gaia `.ansp` catalogs are still used for plate solving and PCC. They are not valid SPCC catalogs because they do
not contain Gaia XP sampled spectra or SPCC synthetic-ratio metadata.

## Required Pack Layout

Each SPCC catalog pack must be an immutable manifest directory with this shape:

```text
spcc_gaia_dr3_xp_sampled/<access-profile>/<manifest-id>/
  catalog_meta.json
  chunk_manifest.json
  coverage_summary.json
  source_id_index.json
  source_count_transcript.json
  chunks/
    *.anxp
```

The selected directory may be the manifest directory itself or a root that the Catalogs & Data inspector can scan for
installed packs. A pack is ready when its manifest, sidecars and every declared chunk are present; with chunks missing it
is a partial cache, and a run downloads the chunks its field needs when an approved managed descriptor covers it. The
`.install_complete` marker is recorded but not required (`inspectSpccCatalogDirectoryDetailed`, `SpccCatalogSettings.h`).

## Required Sidecars

### `catalog_meta.json`

The catalog metadata file is the pack identity contract. It must include:

- `schema_version`
- `catalog_family_id`
- immutable `manifest_id`
- `access_profile`
- `source_kind`
- `payload_kind`
- `total_records`
- `.anxp` format details, including record size and wavelength grid
- rights/provenance block
- sidecar checksums
- chunk list

Runtime SPCC readiness depends on this file matching the selected catalog family and supported access profile.

### `chunk_manifest.json`

The chunk manifest lists every `.anxp` chunk, record count, declared size, and checksum. It is used for install
inspection, repair, and future package validation.

### `coverage_summary.json`

The coverage summary describes regional availability and support-envelope expectations. It must record chunk status,
estimated usable candidates, BP-RP/color-span estimates, quality-cut class, generation timestamp, and diagnostics.

Regional-cache packs must use this file to prove that the target image footprint is covered.

### `source_id_index.json`

The source-ID side index maps Gaia `source_id` values to chunk and record positions. It is required for:

- ImageIntegration SPCC handoff reuse
- direct matched-source resolution
- catalog-side cache-miss diagnostics
- fast source-ID performance measurement

### `source_count_transcript.json`

The source-count transcript records the source-query provenance used to build the pack. It is part of the reproducibility
and rights/provenance chain.

## Access Profiles

| Access Profile | Current Status | Recommendation |
| --- | --- | --- |
| `local-full` | Supported for local readable packs | Best validation path when disk space is available |
| `installed-regional-cache` | Supported for local readable packs | Recommended for target-specific validation and smaller installs |
| `managed-remote` | Planned, not implemented for runtime SPCC | Do not recommend until chunk transport, privacy, checksum, and cache behavior exist |
| `blocked` | Supported as a metadata state | Use for experimental or rights-incomplete packs that must not be production-selectable |

## Rights And Provenance Requirements

Private validation packs may use `rights.review_status = "private-local"`.

Production-selectable packs must not be published or recommended until the manifest records reviewed Gaia/DPAC credit,
source terms, derivative-data constraints, mirror-hosting approval, and one of:

- `approved-for-managed-download`
- `approved-for-bundling`

The app should continue to warn or block packs whose rights/provenance status is private, missing, stale, or otherwise
unapproved for the requested deployment mode.

## Quality Requirements

Generated packs must preserve enough metadata to support SPCC quality filtering and later audit:

- Gaia `source_id`
- position, proper motion, and reference epoch
- Gaia `G`, `BP`, and `RP` photometry
- sampled XP fluxes and flux errors
- source kind
- quality flags
- source-query transcript and count checks

The current v1 sampled-XP path uses aperture photometry as the image-flux baseline. The catalog pack must therefore
support enough usable stars and color-ratio span for the target field after WCS matching, photometry rejection, and robust
fit clipping.

## Runtime Fit Support Targets

Catalog recommendations should be guided by the current robust-fit envelope:

- Hard minimum: 35 used samples
- Low-support warning band: 35-59 used samples
- Comfort threshold: 60 or more red/blue inliers
- Minimum catalog `R/G` and `B/G` ratio span: 0.15
- Maximum relative gain uncertainty: 5%

Regional packs should cover a large enough radius around the target to stay above the comfort threshold after quality
cuts. Small fields that cannot reach that support should be documented as sparse-field cases, not silently treated as
production-ready.

## Recommended Packs

### Tiny Smoke Pack

Use a deterministic tiny fixture or private Gaia-derived tiny validation pack for:

- generator smoke tests
- installer/downloader smoke tests
- manifest verification
- parser and sidecar validation

Do not use tiny packs to judge calibration quality or support-envelope readiness.

### Regional Validation Pack

Use an installed regional cache for:

- process/readiness validation around a known field
- Catalogs & Data install/repair/eviction checks
- WCS-scoped coverage behavior
- query/source-ID performance checks on realistic local paths

The current private M31-core validation pack is a useful example because it exercises the installed-regional-cache path
without requiring a full local catalog.

### Local Full Sampled-XP Pack

Use a local-full sampled-XP pack for:

- broad support-envelope validation
- comparing different image fields and cameras
- avoiding regional-cache coverage uncertainty
- final pre-release local performance measurements

The app's recommended descriptor, `spcc-gaia-dr3-xp-sampled-full-2026b`, approved for managed download, is this shape.

### Managed Remote Packs

Do not recommend managed-remote packs for current SPCC runs. They need explicit implementation and validation for range
requests, privacy, cache keys, checksum repair, stale/partial cache diagnostics, cancellation, and performance before
they can become a user-facing option.

## Pack Selection Guidance

Choose the smallest pack that satisfies the workflow goal:

- For parser/tool tests, use a tiny smoke pack.
- For one target or one sky region, use an installed regional cache that fully covers the image footprint.
- For broad validation across FOVs/cameras, use local-full.
- For production use, the approved local-full pack `spcc-gaia-dr3-xp-sampled-full-2026b`.

If SPCC reports catalog misses, cache misses, sparse-field guidance, or low-support warnings, prefer improving catalog
coverage before relaxing fit thresholds.

## Validation Checklist

Before recommending a pack, confirm:

- Required sidecars exist and parse.
- `.anxp` chunks exist and match manifest counts.
- Sidecar and chunk checksums are known and repairable.
- Rights/provenance status matches the intended deployment mode.
- `coverage_summary.json` reports coverage for the target or validation region.
- `source_id_index.json` resolves expected Gaia source IDs.
- Query/source-ID timing is within the current local performance envelope.
- Real-image validation data ties the pack to expected SPCC metrics before release claims are made.
- `tools/check_spcc_support_envelope.py` reports `production_ready` for the immutable manifest and its validation
  metrics before the pack is described as production-ready.
