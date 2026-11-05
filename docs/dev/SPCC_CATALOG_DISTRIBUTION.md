# SPCC Catalog Distribution Guide

How to generate, validate, stage, upload, and verify AfterNight SPCC `.anxp` catalog datasets.

This workflow mirrors the Gaia v3 catalog distribution flow in `docs/dev/CATALOG_DISTRIBUTION.md`, but SPCC catalogs
carry stricter rights and provenance gates because Gaia XP spectra are Gaia-derived redistribution artifacts.

For runtime catalog requirements and pack-selection recommendations, see `docs/dev/SPCC_CATALOG_REQUIREMENTS.md`. For
the full generation, validation, provenance, update, and rollback lifecycle, see
`docs/dev/SPCC_CATALOG_MAINTENANCE.md`. For response-curve provenance and production review rules, see
`docs/dev/SPCC_RESPONSE_PROVENANCE.md`.

## Hosting Layout

The default staging target is a Hugging Face dataset:

- Repository: `emruiz81/afternight-spcc-catalogs`
- Direct download base: `https://huggingface.co/datasets/emruiz81/afternight-spcc-catalogs/resolve/main/`
- Dataset root manifests: `spcc_dataset_manifest.json` and `spcc_mirror_manifest.json`

Each immutable catalog manifest is staged under:

```text
spcc_gaia_dr3_xp_sampled/<access-profile>/<manifest-id>/
  catalog_meta.json
  chunk_manifest.json
  coverage_summary.json
  source_id_index.json
  source_count_transcript.json
  chunks/
    spcc_gaia_dr3_xp_sampled_chunk_0000.anxp
```

Chunk URLs are resolved relative to the catalog's `catalog_meta.json` URL. This keeps local-full installs,
installed regional caches, and future managed mirrors on the same manifest/chunk convention.

## Rights Gate

Private staging datasets may use catalogs whose `catalog_meta.json` has `rights.review_status = "private-local"`.
Public production uploads are blocked until each catalog manifest records:

- `rights.review_status = "approved-for-managed-download"` or `approved-for-bundling`
- `rights.mirror_hosting_policy = "approved-public"`
- Gaia/DPAC credit and citation requirements
- reviewed Gaia source terms and Hugging Face hosting constraints

The staging tool enforces this gate when run with `--publication-mode public-production`.
Record the reviewed Gaia DR3 state before public staging:

```bash
python tools/approve_spcc_catalog_rights.py \
    catalogs/spcc_production/full_sampled_2026a/catalog \
    --reviewer "AfterNight Gaia DR3 rights review"
```

## Prerequisites

Install and authenticate the Hugging Face CLI:

```bash
curl -LsSf https://hf.co/cli/install.sh | bash
hf auth login
```

If using Git for uploads, install Git LFS:

```bash
git lfs install
```

## Build A Tiny Smoke Dataset

Generate and validate a deterministic fixture catalog:

```bash
python tools/generate_spcc_catalog.py catalogs/spcc_fixture \
    --fixture \
    --fixture-source-count 8 \
    --manifest-id spcc-fixture-smoke-2026a \
    --access-profile local-full \
    --rights-review-status private-local

python tools/validate_spcc_catalog.py catalogs/spcc_fixture --sample-count 3
```

Stage the local Hugging Face dataset directory:

```bash
python tools/build_spcc_hf_dataset.py \
    catalogs/spcc_hf_smoke_dataset \
    catalogs/spcc_fixture \
    --repo-id emruiz81/afternight-spcc-catalogs \
    --clean
```

This creates:

```text
catalogs/spcc_hf_smoke_dataset/
  README.md
  .gitattributes
  spcc_dataset_manifest.json
  spcc_mirror_manifest.json
  spcc_gaia_dr3_xp_sampled/local-full/spcc-fixture-smoke-2026a/
```

## Build From Gaia Validation Exports

For a tiny private Gaia-derived validation catalog:

```bash
python tools/fetch_spcc_gaia_validation_fixture.py \
    catalogs/spcc_validation/tiny_gaia_xp_sampled \
    --source-count 3 \
    --probe-datalink

python tools/generate_spcc_catalog.py \
    catalogs/spcc_validation/tiny_gaia_xp_sampled/catalog \
    --input-jsonl catalogs/spcc_validation/tiny_gaia_xp_sampled/tiny_gaia_spcc_sources.jsonl \
    --manifest-id spcc-gaia-dr3-xp-sampled-tiny-validation-20260510 \
    --access-profile local-full \
    --rights-review-status private-local

python tools/validate_spcc_catalog.py \
    catalogs/spcc_validation/tiny_gaia_xp_sampled/catalog \
    --sample-count 3 \
    --gaia-validation-transcript catalogs/spcc_validation/tiny_gaia_xp_sampled/gaia_validation_transcript.json

python tools/build_spcc_hf_dataset.py \
    catalogs/spcc_hf_tiny_gaia_dataset \
    catalogs/spcc_validation/tiny_gaia_xp_sampled/catalog \
    --repo-id emruiz81/afternight-spcc-catalogs \
    --clean
```

Do not publish Gaia-derived private-local catalogs publicly. Use private or staging uploads only until the manifest
records approved redistribution and hosting review.

## Build A Regional Cache Validation Pack

The validation fetcher can also produce a modest regional sampled-XP pack for catalog-manager, coverage, downloader,
and engine-reader smoke tests. This example builds an M31-core private regional cache:

```bash
python tools/fetch_spcc_gaia_validation_fixture.py \
    catalogs/spcc_validation/m31_region \
    --source-count 120 \
    --artifact-prefix m31_region \
    --region-name m31-core \
    --ra-deg 10.6847083 \
    --dec-deg 41.26875 \
    --radius-deg 1.5 \
    --mag-min 9 \
    --mag-max 16 \
    --min-bp-rp-snr 20 \
    --timeout-seconds 240

python tools/generate_spcc_catalog.py \
    catalogs/spcc_validation/m31_region/catalog \
    --input-jsonl catalogs/spcc_validation/m31_region/m31_region_spcc_sources.jsonl \
    --source-transcript catalogs/spcc_validation/m31_region/gaia_validation_transcript.json \
    --regional-request catalogs/spcc_validation/m31_region/spcc_regional_request.json \
    --manifest-id spcc-gaia-dr3-xp-sampled-m31-region-20260510 \
    --preset sampled-region \
    --rights-review-status private-local

python tools/validate_spcc_catalog.py \
    catalogs/spcc_validation/m31_region/catalog \
    --sample-count 8 \
    --gaia-validation-transcript catalogs/spcc_validation/m31_region/gaia_validation_transcript.json
```

If `--regional-request` is provided, `catalog_meta.json` and `coverage_summary.json` receive descriptor-compatible
regional coverage, the request is preserved as `regional_cache_request.json`, and staged dataset/mirror manifests carry
that coverage forward for descriptor authoring.

Stage the tiny smoke pack and regional pack into one private HF dataset folder:

```bash
python tools/build_spcc_hf_dataset.py \
    catalogs/spcc_hf_validation_dataset \
    catalogs/spcc_validation/tiny_gaia_xp_sampled/catalog \
    catalogs/spcc_validation/m31_region/catalog \
    --repo-id emruiz81/afternight-spcc-catalogs \
    --clean

python tools/verify_spcc_hf_dataset.py \
    catalogs/spcc_hf_validation_dataset/spcc_dataset_manifest.json \
    --base-url-override catalogs/spcc_hf_validation_dataset \
    --chunk-policy all
```

The dataset builder also writes `spcc_catalog_mirrors.json`, the app-facing descriptor config. It includes only catalogs
whose `rights.review_status` is approved for managed use; private-local validation packs are copied into the dataset but
not exposed as downloadable app descriptors. Non-empty configs include immutable-versioned `publication_policy` metadata
and mark generated approved descriptors as `release_status = current`. Superseded descriptors may remain in the config
for update provenance, but Catalogs/Data only offers current descriptors for normal install actions. For local
descriptor-verifier smoke before upload, pass `--direct-base-url` when staging:

```bash
python tools/build_spcc_hf_dataset.py \
    catalogs/spcc_hf_validation_dataset \
    catalogs/spcc_validation/m31_region/catalog \
    --repo-id emruiz81/afternight-spcc-catalogs \
    --direct-base-url catalogs/spcc_hf_validation_dataset \
    --clean

python tools/verify_spcc_catalog_mirror_config.py \
    catalogs/spcc_hf_validation_dataset/spcc_catalog_mirrors.json \
    --chunk-policy first \
    --no-token
```

## Upload

Upload the staged dataset directory:

```bash
hf upload emruiz81/afternight-spcc-catalogs \
    catalogs/spcc_hf_smoke_dataset . \
    --repo-type dataset \
    --revision main
```

For production, use a tag or immutable branch name once the manifest is approved and final:

```bash
hf upload emruiz81/afternight-spcc-catalogs \
    catalogs/spcc_hf_production_dataset . \
    --repo-type dataset \
    --revision spcc-gaia-dr3-xp-sampled-2026a
```

## Verify Direct Downloads

After upload, fetch the dataset and catalog manifests:

```bash
python tools/verify_spcc_hf_dataset.py \
    https://huggingface.co/datasets/emruiz81/afternight-spcc-catalogs/resolve/main/spcc_dataset_manifest.json \
    --chunk-policy first
```

For private datasets, the verifier automatically uses `HF_TOKEN`, `HUGGINGFACE_HUB_TOKEN`, or the cached
Hugging Face token at `~/.cache/huggingface/token`. For full catalogs, use `--chunk-policy head` first and reserve
full checksum downloads for CI/cache repair smoke tests.

Before enabling an app-facing `spcc_catalog_mirrors.json` with non-empty `catalog_packs`, verify the descriptor config
against the hosted or local staged manifests. The verifier checks publication metadata, immutable update policy,
current/superseded descriptor links, catalog manifest identity, sidecar checksums, rights state, and optional chunk
availability/checksums:

```bash
python tools/verify_spcc_catalog_mirror_config.py \
    path/or/url/to/spcc_catalog_mirrors.json \
    --chunk-policy head
```

Use `--chunk-policy first` or `all` when you intentionally want the verifier to download chunks and validate checksums.

## App Install Path Status

`CatalogDownloadService` installs SPCC packs from a `catalog_meta.json` URL as an AfterNight-managed chunk cache.
Hugging Face remains static file hosting: the app fetches `catalog_meta.json`, sidecars, and either the requested WCS
region's required `.anxp` chunks or the full chunk set. The catalog metadata still uses `access_profile = "local-full"`;
partial cache state is an app-side install state, not a separate catalog identity. The SPCC path cross-checks the
downloaded manifest's family, manifest ID, access profile, and rights review status against the selected descriptor
before writing cache files.

For SPCC manifests it downloads and checksum-verifies:

- `catalog_meta.json`
- `chunk_manifest.json`
- `coverage_summary.json`
- `source_id_index.json`
- `source_count_transcript.json`
- optional `regional_cache_request.json` when declared in `catalog_meta.json`
- required-region `.anxp` chunks, or all `.anxp` chunks for full-catalog installs

SPCC cache writes are incremental. Existing verified files are reused, new files are written as `.part` files and then
renamed into place, unrelated cached chunks are preserved, and a cancelled or failed region download keeps every chunk
that was already verified. `.install_complete` is written only after a full catalog install. A manually downloaded full
repository is still treated as ready when the manifest, sidecars, and all declared chunks are present, even without that
marker.

The current automated smoke tests use local `file://` URLs plus a local HTTP server. They cover SPCC manifest fetch,
HTTP sidecar/chunk transfer through `QNetworkAccessManager`, descriptor metadata mismatch rejection, staging reuse,
cancellation, checksum-failure preservation of an existing install, reinstall promotion, install-complete marker
creation, and local installed-pack detection. Hosted Hugging Face smoke testing is still required for
authenticated/private dataset access and large regional/full-pack repair scenarios.

`SpccCatalogSettings.h` provides local install inspection for `catalog_meta.json`, sidecars, `.anxp` chunks,
rights-review status, access profile, and `.install_complete`. It reports ready full installs, partial chunk caches, and
metadata/sidecar failures separately. It scans both flat installs and the staged dataset
layout:

```text
spcc_gaia_dr3_xp_sampled/<access-profile>/<manifest-id>/
```

App defaults expose no private validation-pack descriptor; they expose the
approved full-sky managed-download descriptor `spcc-gaia-dr3-xp-sampled-full-2026b`; additional downloadable SPCC pack
descriptors can be supplied through `spcc_catalog_mirrors.json` as approved `catalog_packs` entries for a verified
mirror. The app filters that config to SPCC chunk descriptors with supported access profiles, approved rights-review
status, `release_status = current`, safe descriptor IDs, safe `catalog_meta.json` manifest paths, and valid regional
coverage when the descriptor is an installed regional cache. Private validation packs and superseded descriptors remain
documented here for developer staging, update provenance, and tool verification only. The same config should pass
`tools/verify_spcc_catalog_mirror_config.py` before it is handed to users or packaged as an approved mirror override.

`CatalogDownloadDialog` takes SPCC descriptors with `spccCatalogDownloadInstallInspection` as its install inspector; its
default constructor is Gaia's. A local install whose `catalog_meta.json`, sidecars and every declared chunk are present
shows as downloaded and reinstallable, the `.install_complete` marker not required; one missing chunks shows as a
partial cache.

`CatalogsDataSettingsPage` is the Settings -> Catalogs & Data entry point for SPCC catalog and
response-repository management. It also surfaces the existing Gaia DR3 astrometric/PCC catalog controls in the same
page, still backed by `platesolve/gaiaCatalogDirectory` and `platesolve/gaiaCatalogPack`, so internal plate solving/PCC
and SPCC spectral catalogs are visible but kept separate. Settings -> Plate Solving does not edit these settings; it
reads the same settings for internal-solver availability while remaining focused on solver
configuration. Catalogs/Data stores the selected SPCC catalog root and pack in
`catalogs/spccCatalogDirectory`/`catalogs/spccCatalogPack`, stores the selected response repository in
`catalogs/spccResponseRepositoryDirectory`, shows installed-pack status through the SPCC install inspector, validates the
selected response repository root, and supports local pack eviction. Its SPCC download/repair action offers the current
approved managed-download descriptors. If a WCS-derived regional-cache request is pending and an approved descriptor
covers that footprint, `Download Matching Region...` opens the SPCC catalog download dialog with only the matching
packs.
Managed response repositories have a tool-verified descriptor-config contract
(`spcc-response-repository-config-v1` plus `tools/verify_spcc_response_repository_config.py`). The app parses approved
descriptors from that config, reports availability in Catalogs & Data, and can install the recommended descriptor into
the managed response cache through the checksum-verified downloader. This managed install path has local file
and local HTTP smoke coverage. The verifier enforces stable/preview publication metadata, immutable-versioned
updates, exactly one current recommended descriptor per repository channel, and explicit supersession links. The app's
built-in descriptor is the approved `afternight-spcc-response-baseline-2026a` on `emruiz81/afternight-spcc-responses`
(`SpccResponseSettings.h`). `tools/build_spcc_response_repository_dataset.py` stages reviewed
response repositories into the hosted layout and emits the app-facing descriptor config for verifier smoke before
upload. App-side discovery reads that config from the app data location and exposes only current descriptors to
Catalogs & Data; superseded descriptors remain addressable in the config for update provenance but are not offered as
normal managed installs.

`SpccResponseSettings.h` provides the app-side response repository inspector. It validates `response_meta.json`,
component and white-reference asset files, SHA-256 checksums, response payload schema/IDs, wavelength units, value
semantics, payload `data_sha256`, throughput-profile references, compatible channel bases, rights-review status, and
deployment scope.

`SpccResponseRepository.h` (`src/AfterNightProcesses/processes/SpectrophotometricColorCalibration/`) reads the same
response repository shape for the process. It parses `response_meta.json`, loads component and white-reference JSON
assets, validates manifest/payload identity and `data_sha256` metadata, composes the RGB throughput curves from
`channel_component_ids` into the engine's response profiles and spectra, and resolves exact camera/filter aliases; the
engine itself opens no repository file and resolves no camera name. Release-package smoke checks, multi-root discovery,
and ambiguity-resolution UX remain future implementation work.

`SpccProcessReadinessPanel`, beside it, is the Spectrophotometric Color Calibration window's readiness panel. It
evaluates the selected SPCC catalog and response repository, loads the response repository through
`SpccResponseRepository.h` on a worker, preselects only exact camera/filter response-profile aliases, exposes a manual
response-profile selector when metadata is missing or ambiguous, opens Settings -> Catalogs & Data through its management
action, and keeps Apply disabled until catalog, response repository, and response profile all validate. With no catalog
installed, or with chunks the field needs missing from an approved managed pack, the run downloads the metadata and
only those chunks when it starts (`populateCatalogRegionReadiness`); the process and its target checks are described in
"Photometric and spectrophotometric colour calibration" in `docs/dev/guides/PRODUCT_ARCHITECTURE.md`.

## Repair Or Reupload

1. Regenerate the affected catalog pack.
2. Re-run `tools/validate_spcc_catalog.py`.
3. Re-run `tools/build_spcc_hf_dataset.py` with `--clean`.
4. Re-upload the staged dataset directory with `hf upload`.
5. Verify `spcc_dataset_manifest.json`, the changed `catalog_meta.json`, and at least one changed chunk.

The changed catalog manifest SHA-256 and chunk SHA-256 values force existing installs to detect stale or incomplete
downloads during future downloader/readiness checks.

## Storage Expectations

Sampled-XP full local packs are expected to land in the tens-of-GB range after `.anxp` chunking. The current full pack,
`spcc-gaia-dr3-xp-sampled-full-2026b`, is 45,539,563,436 bytes to download and 47,690,985,471 installed (about 42 and
44 GiB; `SpccCatalogSettings.h`). Regional-cache datasets should be
generated and measured before the engine/UI depends on them.
