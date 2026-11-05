# SPCC Catalog Generation, Validation, Provenance, And Updates

Operational runbook for creating, validating, publishing, updating, and auditing AfterNight SPCC `.anxp` catalog packs.

This document complements:

- `docs/dev/SPCC_CATALOG_REQUIREMENTS.md` for runtime pack requirements and selection guidance.
- `docs/dev/SPCC_CATALOG_DISTRIBUTION.md` for Hugging Face staging/upload examples.
- `docs/dev/SPCC_USER_WORKFLOW.md` for the process-facing user workflow.

SPCC catalogs are immutable data products. Any change to source selection, quality cuts, response-derived cache fields,
chunking, rights/provenance state, or sidecar structure must produce a new manifest ID rather than mutating a published
pack in place.

## How the application reads a pack

The product reads a pack through `SpccCatalogSource.h`
(`src/AfterNightProcesses/processes/SpectrophotometricColorCalibration/`): the field's chunks are selected with
`selectSpectrumChunksForRegion` on a worker and handed to the engine's `planSpectrophotometricColorCalibration` and
`calibrateSpectrophotometricColor`, which open no pack file themselves. What the engine computes over a pack is
documented in `src/AfterNightEngine/include/AfterNightEngine/Algorithms/Spectrophotometry/SpectrophotometricColorCalibration.h`, and what the product does around it -
the target checks, the readiness decided on a worker, the asset revisions - in
"Photometric and spectrophotometric colour calibration" in `docs/dev/guides/PRODUCT_ARCHITECTURE.md`.

## Catalog Lifecycle

The supported lifecycle is:

1. Estimate source counts and storage.
2. Fetch or prepare source/spectrum inputs.
3. Generate an `.anxp` catalog pack.
4. Validate the generated pack.
5. Stage one or more packs into a dataset layout.
6. Verify the staged dataset.
7. Upload or package the dataset.
8. Verify the hosted or packaged artifact.
9. Update app mirrors/descriptors only after verification.

Private validation packs may stop after local dataset verification. Production packs must pass stricter rights,
provenance, validation, and hosting checks before they become user-facing.

## Manifest IDs

Use immutable manifest IDs. Recommended pattern:

```text
spcc-gaia-dr3-xp-sampled-<scope>-<yyyymmdd-or-version>
```

Examples:

- `spcc-fixture-smoke-2026a`
- `spcc-gaia-dr3-xp-sampled-tiny-validation-20260510`
- `spcc-gaia-dr3-xp-sampled-m31-region-20260510`
- `spcc-gaia-dr3-xp-sampled-full-2026b`

Create a new manifest ID when:

- source-count query logic changes;
- Gaia source filters or quality cuts change;
- Gaia bulk/DataLink inputs change;
- `.anxp` format, chunking, or quantization changes;
- source transcript or provenance fields change;
- rights/provenance status changes from private to production-approved;
- a pack is regenerated after a failed or stale upload.

Do not reuse a manifest ID for a different byte payload.

## Source Count And Size Estimation

Estimate catalog size before generation:

```bash
python tools/estimate_spcc_catalog_size.py \
    --preset sampled-full \
    --tap-official \
    --output-transcript catalogs/spcc_counts/sampled-full-2026a-counts.json
```

For deterministic tests or offline planning, use `--counts-fixture` with a JSON mapping of preset IDs to counts.

Keep the count transcript with the generated catalog artifacts. It is part of the provenance chain and should inform
download-size, disk-footprint, and support-envelope decisions.

## Fetching Gaia Validation Inputs

For a tiny private validation pack:

```bash
python tools/fetch_spcc_gaia_validation_fixture.py \
    catalogs/spcc_validation/tiny_gaia_xp_sampled \
    --source-count 3 \
    --probe-datalink
```

For a regional cache validation pack:

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
```

The fetcher writes a JSONL input and a `gaia_validation_transcript.json`. The transcript records source-query and XP
retrieval provenance and should be passed into generation and validation.

## Preparing Full-Sky Inputs

Before the full-sky `.anxp` packer can run, prepare Gaia DR3 metadata CSV.GZ parts and then prejoin them with XP sampled
spectra. The metadata export queries official Gaia TAP by HEALPix-8 source-id range and writes a manifest that the
prejoin step can consume directly. Official TAP export filters out rows missing non-defaultable production quality
fields such as `ruwe`; blank Gaia proper-motion fields are normalized to `0.0`, matching the validation fetcher.

```bash
python tools/export_spcc_gaia_xp_metadata.py \
    catalogs/spcc_production/full_sampled_2026a/metadata \
    --tap-official \
    --healpix8-start 0 \
    --healpix8-end 786432 \
    --healpix8-step 64
```

If metadata export is interrupted, resume the same output directory:

```bash
python tools/export_spcc_gaia_xp_metadata.py \
    catalogs/spcc_production/full_sampled_2026a/metadata \
    --resume
```

Then download the official Gaia `XpSampledMeanSpectrum_*.csv.gz` bulk files under
`catalogs/spcc_production/full_sampled_2026a/xp_sampled/`. The downloader lists the official Gaia CDN index, writes a
manifest, and can resume after completed files:

```bash
python tools/download_spcc_gaia_xp_sampled_files.py \
    catalogs/spcc_production/full_sampled_2026a/xp_sampled
```

If the download is interrupted, resume the same output directory:

```bash
python tools/download_spcc_gaia_xp_sampled_files.py \
    catalogs/spcc_production/full_sampled_2026a/xp_sampled \
    --resume
```

Run the prejoin using the metadata and XP sampled manifests. This avoids passing thousands of `--xp-sampled-csv`
arguments and avoids Windows command-line length limits.

```bash
python tools/export_spcc_gaia_full_xp_sampled_sources.py \
    catalogs/spcc_production/full_sampled_2026a/prejoined \
    --metadata-manifest catalogs/spcc_production/full_sampled_2026a/metadata/spcc_gaia_xp_metadata_export_manifest.json \
    --xp-sampled-manifest catalogs/spcc_production/full_sampled_2026a/xp_sampled/spcc_gaia_xp_sampled_download_manifest.json \
    --partition-count 4096
```

The prejoin can take a long time because it streams and partitions the metadata and spectra before joining them by
`source_id`. It writes resumable work state under `.spcc_full_xp_source_export_work/` and final JSONL shards under
`parts/`. If it is interrupted, resume with:

```bash
python tools/export_spcc_gaia_full_xp_sampled_sources.py \
    catalogs/spcc_production/full_sampled_2026a/prejoined \
    --resume
```

The successful output manifest is:

```text
catalogs/spcc_production/full_sampled_2026a/prejoined/spcc_full_xp_sampled_source_export_manifest.json
```

## Generation

Generate a deterministic smoke fixture:

```bash
python tools/generate_spcc_catalog.py catalogs/spcc_fixture \
    --fixture \
    --fixture-source-count 8 \
    --manifest-id spcc-fixture-smoke-2026a \
    --access-profile local-full \
    --rights-review-status private-local
```

Generate from fetched Gaia validation inputs:

```bash
python tools/generate_spcc_catalog.py \
    catalogs/spcc_validation/tiny_gaia_xp_sampled/catalog \
    --input-jsonl catalogs/spcc_validation/tiny_gaia_xp_sampled/tiny_gaia_spcc_sources.jsonl \
    --source-transcript catalogs/spcc_validation/tiny_gaia_xp_sampled/gaia_validation_transcript.json \
    --manifest-id spcc-gaia-dr3-xp-sampled-tiny-validation-20260510 \
    --access-profile local-full \
    --rights-review-status private-local
```

Generate a regional-cache pack:

```bash
python tools/generate_spcc_catalog.py \
    catalogs/spcc_validation/m31_region/catalog \
    --input-jsonl catalogs/spcc_validation/m31_region/m31_region_spcc_sources.jsonl \
    --source-transcript catalogs/spcc_validation/m31_region/gaia_validation_transcript.json \
    --regional-request catalogs/spcc_validation/m31_region/spcc_regional_request.json \
    --manifest-id spcc-gaia-dr3-xp-sampled-m31-region-20260510 \
    --preset sampled-region \
    --rights-review-status private-local
```

The optional `--regional-request` file uses the same `spcc-regional-cache-request-v1` JSON shape as the app's saved
`catalogs/spccRegionalCacheRequest` setting. When supplied, the generator forces `installed-regional-cache` access,
writes `regional_cache_request.json`, records its SHA-256 in `catalog_meta.json`, stamps descriptor-compatible
`coverage` into `catalog_meta.json` and `coverage_summary.json`, and `build_spcc_hf_dataset.py` carries that coverage
into the staged dataset and mirror manifests.

Build a full-sky sampled-XP catalog from prejoined local exports:

```bash
python tools/build_spcc_full_xp_sampled_catalog.py \
    catalogs/spcc_production/full_sampled_2026a/catalog \
    --input-manifest catalogs/spcc_production/full_sampled_2026a/prejoined/spcc_full_xp_sampled_source_export_manifest.json \
    --manifest-id spcc-gaia-dr3-xp-sampled-full-2026b \
    --cap-per-healpix8 127 \
    --rights-review-status private-local
```

This full-sky builder is resumable and intended for long runs. It partitions each input into durable work files under
`.spcc_full_export_work/`, selects the brightest sources up to `--cap-per-healpix8` per order-8 HEALPix tile, then writes
the final chunk manifest, coverage summary, source-id index, source-count transcript, and `.anxp` chunks. If the run is
interrupted, resume it with:

```bash
python tools/build_spcc_full_xp_sampled_catalog.py \
    catalogs/spcc_production/full_sampled_2026a/catalog \
    --resume
```

Keep full-sky output as `private-local` until Gaia redistribution and mirror-hosting review is recorded.

For interrupted generation, use `--resume` only when continuing the same output directory and manifest ID with the same
input files. If inputs changed, remove the output directory or generate to a new manifest ID.

## App Catalogs/Data Surface

Settings -> Catalogs & Data shows two distinct Gaia catalog surfaces:

- Gaia DR3 Astrometric Catalogs: the existing chunk catalogs used by internal plate solving and PCC. These continue to
  use `platesolve/gaiaCatalogDirectory` and `platesolve/gaiaCatalogPack`, with local status and eviction available from
  Catalogs/Data.
- Gaia DR3 XP Sampled SPCC Catalogs: SPCC spectral catalogs using `catalogs/spccCatalogDirectory` and
  `catalogs/spccCatalogPack`.

Keep these roots and pack IDs separate. A plate-solving/PCC Gaia chunk pack is not an SPCC XP sampled spectral catalog,
and an SPCC regional cache is not a replacement for the astrometric/PCC catalog.

The existing Gaia DR3 astrometric/PCC Hugging Face dataset was checked during the 2026-05-14 SPCC rights pass:
`emruiz81/afternight-gaia-catalogs` is public and currently exposes `gaia_dr3_g12`, `gaia_dr3_g14`, `gaia_dr3_g16`,
and `gaia_dr3_g18` `.ansp` packs. Those `catalog_meta.json` files carry source/checksum fields but no SPCC-style
rights review block. SPCC uses a stricter manifest gate because the XP sampled payload is a separate derived Gaia DR3
spectral distribution.

Settings -> Plate Solving reads the astrometric Gaia settings for internal-solver availability and does not edit or
download those catalogs. Use Catalogs/Data for Gaia catalog install, selection, and eviction.

## App Regional-Cache Request Handoff

The SPCC readiness panel's `Download Needed Region` action records a WCS-derived regional-cache request in global settings
before opening `Settings -> Catalogs & Data`. The persisted key is `catalogs/spccRegionalCacheRequest`, and the payload
uses schema version `spcc-regional-cache-request-v1` with:

- catalog family `spcc_gaia_dr3_xp_sampled`;
- access profile `installed-regional-cache`;
- target name and request timestamp;
- WCS center RA/Dec, query radius, magnitude limit, and image dimensions.

Catalog descriptors intended to satisfy a pending regional request must declare:

- catalog family and access profile;
- rights review status approved for download;
- chunk distribution type and manifest URL;
- region center RA/Dec, coverage radius, and limiting magnitude.

Catalogs & Data evaluates approved descriptors against the saved request and reports whether a descriptor covers the
pending footprint. When a covering descriptor exists, `Download Matching Region...` opens the SPCC catalog download
assistant with only matching regional packs. The existing manifest/checksum installer downloads the pack, promotes the
install-complete regional cache, and clears the pending request after the saved selected pack is verified as ready. The
installer also cross-checks SPCC `catalog_meta.json` family, manifest ID, access profile, and rights review status
against the selected descriptor before promoting a downloaded pack. Automated coverage exercises both local `file://`
staging and local HTTP manifest/sidecar/chunk transfer; live hosted verification against an approved mirror remains a
release gate.

This flow installs prebuilt, hosted regional-cache packs. The generator consumes a saved request through
`--regional-request`; the app neither fetches Gaia inputs nor generates a pack from a WCS request. Separately, a run with
no SPCC catalog, or with a partial install of an approved managed pack, downloads only the chunks its field needs when it
starts.

## App Descriptor Mirror Config

The app reads approved SPCC descriptors from `spcc_catalog_mirrors.json` in the application data directory when that
file exists (it never creates it), and filters non-current descriptors out of normal install offers. Without the file,
the app offers its built-in `spcc-gaia-dr3-xp-sampled-full-2026b` descriptor from the default mirror; when the file
names no local-full pack, the app adds that descriptor from the file's first mirror, or the default one
(`availableSpccCatalogPacks`).

The config lives in Qt's `AppLocalDataLocation` and uses this shape:

```json
{
  "schema_version": "spcc-catalog-mirror-config-v1",
  "publication_policy": {
    "channel": "preview",
    "published_at_utc": "2026-05-12T00:00:00Z",
    "update_policy": "immutable-versioned"
  },
  "mirrors": ["https://example.invalid/afternight-spcc-catalogs/resolve/main"],
  "catalog_packs": [
    {
      "id": "spcc-gaia-dr3-xp-sampled-orion-region-20260511",
      "display_name": "SPCC Gaia DR3 XP Sampled Orion Regional Cache",
      "description": "Verified regional cache for the Orion field.",
      "manifest_path": "spcc_gaia_dr3_xp_sampled/installed-regional-cache/spcc-gaia-dr3-xp-sampled-orion-region-20260511/catalog_meta.json",
      "distribution_type": "chunks",
      "catalog_family_id": "spcc_gaia_dr3_xp_sampled",
      "access_profile": "installed-regional-cache",
      "rights_review_status": "approved-for-managed-download",
      "release_status": "current",
      "recommended": true,
      "supersedes": [],
      "download_size_bytes": 123456,
      "installed_size_bytes": 234567,
      "coverage": {
        "center_ra_deg": 83.6331,
        "center_dec_deg": 22.0145,
        "radius_deg": 1.25,
        "magnitude_limit": 17.0
      }
    }
  ]
}
```

Descriptor admission rules are intentionally narrow:

- non-empty configs must include `publication_policy.channel = stable|preview`,
  `publication_policy.published_at_utc` as an ISO-8601 UTC timestamp, and
  `publication_policy.update_policy = immutable-versioned`.
- `catalog_family_id` must be `spcc_gaia_dr3_xp_sampled`.
- `distribution_type` must be `chunks`.
- `access_profile` must be `local-full` or `installed-regional-cache`.
- `rights_review_status` must be `approved-public`, `approved-for-managed-download`, or `approved-for-bundling`.
- `release_status` must be `current` or `superseded`; current descriptors must be recommended, while superseded
  descriptors must set `recommended = false` and point `superseded_by` at a current replacement descriptor.
- relative `manifest_path` values must stay under the configured mirror and end in `catalog_meta.json`.
- `installed-regional-cache` descriptors must declare valid sky coverage and limiting magnitude.

Only add descriptors after the referenced hosted catalog manifest, sidecars, chunks, rights state, and support-envelope
records have passed the validation gates in this runbook. Multiple current regional descriptors are allowed because they
can represent different sky footprints; supersession links track descriptor replacement provenance without collapsing
all regional packs into one global channel.

Before installing or shipping a non-empty descriptor config, run:

```bash
python tools/verify_spcc_catalog_mirror_config.py \
    path/or/url/to/spcc_catalog_mirrors.json \
    --chunk-policy head
```

Use `--chunk-policy first` or `--chunk-policy all` for controlled validation windows where downloading catalog chunks is
acceptable. The verifier checks publication policy, descriptor admission rules, current/superseded replacement links,
fetches each referenced `catalog_meta.json`, verifies sidecar checksums, confirms manifest family/ID/access-profile/rights
consistency, and optionally checks `.anxp` chunk availability or checksums. Private datasets use `HF_TOKEN`,
`HUGGINGFACE_HUB_TOKEN`, or a token file unless `--no-token` is supplied.

SPCC response repositories use a separate descriptor config and verifier so catalog packs and response assets can remain
independently reviewed and updated. See `docs/dev/SPCC_RESPONSE_PROVENANCE.md` and run
`python tools/verify_spcc_response_repository_config.py <path-or-url-to-spcc_response_repositories.json>` before any
managed response repository config is enabled.

## Validation

Validate every generated pack:

```bash
python tools/validate_spcc_catalog.py \
    catalogs/spcc_validation/tiny_gaia_xp_sampled/catalog \
    --sample-count 3 \
    --gaia-validation-transcript catalogs/spcc_validation/tiny_gaia_xp_sampled/gaia_validation_transcript.json
```

For production candidates, add strict review:

```bash
python tools/validate_spcc_catalog.py \
    catalogs/spcc_production/full_sampled_2026a \
    --sample-count 16 \
    --strict-production-review
```

Validation checks metadata schema, required sidecars, optional regional-cache request sidecar checksums, chunk checksums,
ANXP preamble/version, record size, sample count, spectrum finiteness, quality-field declarations, coverage
consistency, source-ID index consistency, and optional Gaia transcript consistency.

Run the sampled-XP support-envelope go/no-go checker before treating a regional or full sampled-XP candidate as
production-ready. The checker requires both catalog coverage and validation metrics; coverage alone is not enough:

```bash
python tools/check_spcc_support_envelope.py \
    catalogs/spcc_production/full_sampled_2026a \
    --validation-metrics docs/dev/spcc/spcc-support-validation-metrics-2026a.json \
    --output-json docs/dev/spcc/spcc-support-go-no-go-2026a.json \
    --manifest-entry-json docs/dev/spcc/spcc-support-envelope-manifest-entry-2026a.json \
    --manifest-entry-id sampled-xp-support-envelope-2026a
```

The metrics file must be a JSON object with `schema_version: "spcc-support-envelope-validation-v1"` and a `runs` array
keyed by unique immutable `manifest_id` values. Each run records at least integer `runtime_used_stars` plus boolean
`validation_has_stable_gains` and `validation_residuals_acceptable` fields. The checker rejects wrong schema versions,
duplicate manifest IDs, missing fields, negative runtime-star counts, and string/number values where booleans are
required. When `--manifest-entry-json` is supplied, the checker also writes a ready-to-review
`support_envelope_reports` entry with metrics/report paths, SHA-256 checksums, schema versions, expected per-catalog
statuses, rights status, validation command, and tolerance version. Review that entry before adding it to
`docs/dev/spcc/spcc-validation-data-manifest.json`.

Run the tool test suite when changing generation or validation logic:

```bash
python -m unittest tools.test_spcc_catalog_tools
```

## Staging A Dataset

Stage validated packs into the Hugging Face-style dataset layout:

```bash
python tools/build_spcc_hf_dataset.py \
    catalogs/spcc_hf_validation_dataset \
    catalogs/spcc_validation/tiny_gaia_xp_sampled/catalog \
    catalogs/spcc_validation/m31_region/catalog \
    --repo-id emruiz81/afternight-spcc-catalogs \
    --publication-mode private-staging \
    --mirror-hosting-policy staging-only \
    --clean
```

For production candidates, use `--publication-mode public-production`, `--mirror-hosting-policy approved-public`, and
`--strict-production-review`. Do not use `--no-validate` for production staging.

The staging builder writes three top-level manifests:

- `spcc_dataset_manifest.json` for dataset verification and upload bookkeeping.
- `spcc_mirror_manifest.json` for remote mirror provenance/cache-key auditing.
- `spcc_catalog_mirrors.json` for the app-facing descriptor config.

`spcc_catalog_mirrors.json` intentionally includes only packs with approved managed-use rights. Private-local validation
packs remain staged for developer verification but are not offered to users through Catalogs/Data. Non-empty generated
configs include immutable-versioned `publication_policy` metadata and mark staged approved descriptors as
`release_status = current`. If a later descriptor supersedes an older one, keep the older descriptor addressable as
`release_status = superseded`, set `recommended = false`, and point `superseded_by` at the current replacement. Use
`--direct-base-url <local-or-hosted-base>` when you want the generated descriptor config to point at a local staging
directory for verifier smoke; omit it for normal Hugging Face direct-download URLs.

## Dataset Verification

Verify the staged dataset locally:

```bash
python tools/verify_spcc_hf_dataset.py \
    catalogs/spcc_hf_validation_dataset/spcc_dataset_manifest.json \
    --base-url-override catalogs/spcc_hf_validation_dataset \
    --chunk-policy all
```

For full catalogs, use `--chunk-policy head` for quick hosted checks and reserve `first` or `all` for controlled CI or
repair validation windows.

After upload, verify the hosted dataset manifest:

```bash
python tools/verify_spcc_hf_dataset.py \
    https://huggingface.co/datasets/emruiz81/afternight-spcc-catalogs/resolve/main/spcc_dataset_manifest.json \
    --chunk-policy first
```

Private datasets use `HF_TOKEN`, `HUGGINGFACE_HUB_TOKEN`, or the configured token file unless `--no-token` is supplied.

After the dataset verification passes, verify the app-facing descriptor config that points at the same hosted manifests.
For a private-only staging dataset with no approved descriptors, add `--allow-empty`:

```bash
python tools/verify_spcc_catalog_mirror_config.py \
    catalogs/spcc_hf_validation_dataset/spcc_catalog_mirrors.json \
    --chunk-policy head
```

Current full-sky public-production evidence:

- Full-sky catalog `spcc-gaia-dr3-xp-sampled-full-2026b` is the current managed-download descriptor. The hosted
  app-facing config was generated on 2026-05-17, reports 29,648,017 sampled-XP records, and records
  `catalog_meta.json` SHA-256 `1fb5af737d0673c0cb34b4ae7d070094c1c66760d7b9c0739bf2785bbfafc364`.
- The current public-production dataset is available from `emruiz81/afternight-spcc-catalogs` commit
  `421acfc5e960522b3d5a242706ee8d114b32dc28`.
- Direct hosted checks on 2026-05-19 confirmed that the app-facing config reports one descriptor for the approved
  local-full catalog and that `spcc-gaia-dr3-xp-sampled-full-2026a/catalog_meta.json` returns 404. The retired
  `2026a` full-sky catalog must not be exposed by app-facing catalog discovery.

## Provenance Requirements

Every generated catalog must preserve:

- Gaia archive release and source query text/checksum.
- Source query timestamp.
- Gaia/DataLink or bulk XP retrieval metadata.
- Input JSONL/transcript checksums.
- Output manifest ID and catalog family ID.
- Access profile and generation preset.
- Rights review status and mirror-hosting policy.
- Chunk checksums and source-count transcript.
- Coverage summary and quality-cut class.

Production updates require a reviewed rights/provenance state. A private-local pack can be staged privately for testing,
but it must not be promoted to a public production mirror without manifest approval and strict-production validation.
For Gaia DR3 SPCC packs, record that approval before public staging:

```bash
python tools/approve_spcc_catalog_rights.py \
    catalogs/spcc_production/full_sampled_2026a/catalog \
    --reviewer "AfterNight Gaia DR3 rights review"
```

The approval tool records ESA/Gaia/DPAC credit, Gaia DR3 citation requirements, reviewed source URLs, reviewer,
timestamp, redistribution policy, derivative-data policy, and `approved-public` mirror-hosting policy in
`catalog_meta.json`.

## Updating Existing Datasets

Use this update flow:

1. Choose a new manifest ID.
2. Regenerate the pack from reviewed inputs.
3. Validate the pack.
4. Stage the dataset with old valid packs plus the new pack.
5. Verify the staged dataset locally.
6. Upload to a staging/private revision first.
7. Verify the hosted staging revision.
8. Promote by uploading or tagging the approved production revision.
9. Update app mirror/descriptors only after hosted verification passes.
10. Keep the previous manifest available until install/repair telemetry confirms the new pack is healthy.

The dataset root manifests may change when adding a new pack, but previously published pack directories must stay
byte-stable. If a pack was published with bad bytes, publish a replacement manifest ID and mark the bad manifest blocked
in follow-up metadata or app descriptors instead of overwriting it.

## Repair, Rollback, And Reupload

For a failed local generation:

1. Remove the incomplete output directory, or resume only when inputs and manifest ID are unchanged.
2. Regenerate.
3. Revalidate with transcript checks enabled.

For a failed staged dataset:

1. Rebuild with `--clean`.
2. Verify with `--chunk-policy all` for small/regional packs.
3. Diff `spcc_dataset_manifest.json` and `spcc_mirror_manifest.json` against the expected changes.

For a failed upload:

1. Do not mutate an already verified production pack directory.
2. Reupload the same staged dataset only if the local staged bytes are unchanged.
3. If hosted bytes differ or the manifest contents need correction, generate a new manifest ID.
4. Verify the hosted revision before updating app mirrors.

Rollback means selecting the prior verified manifest/revision in app descriptors or mirror configuration. Avoid deleting
old catalog packs until users have a migration path and installers no longer reference them.

## Updating The Validation Manifest

When catalog data is used for SPCC release validation, update `docs/dev/spcc/spcc-validation-data-manifest.json` with:

- artifact ID and purpose;
- catalog family and manifest ID;
- file paths or hosted URLs;
- SHA-256 checksums;
- license/provenance state;
- expected metric version and tolerance rationale;
- support-envelope metrics and go/no-go report artifacts, when a pack is used for sampled-XP production-readiness
  decisions;
- external comparison references, if any.

Support-envelope report entries use the `support_envelope_reports` section. Each entry records the covered
`catalog_manifest_ids`, `rights_review_status`, the checksum and schema version for the validation metrics file
(`spcc-support-envelope-validation-v1`), the checksum and schema version for the go/no-go report
(`spcc-support-envelope-go-no-go-v1`), the expected per-manifest statuses, the command used to produce the report, and a
tolerance version. `tools/check_spcc_support_envelope.py --manifest-entry-json` can generate this entry next to the
report so sparse-field, rights-blocked, validation-missing, and future production-ready decisions stay auditable instead
of remaining as loose local files.

Then validate it:

```bash
python tools/validate_spcc_validation_manifest.py docs/dev/spcc/spcc-validation-data-manifest.json
```

Use `--strict-production-review` before release-candidate validation. Use `--release-ready` before marking SPCC complete
or production-ready; this stricter mode requires the manifest to contain a production-approved response repository, a
production-ready support-envelope report, at least one production-approved real-image reference set, and at least one
production-approved external comparison output.

For real-image and external-comparison evidence, prefer generated entries over hand-entered checksums:

```bash
cp docs/dev/spcc/spcc-real-image-validation-source-manifest.template.json \
    <reviewed-real-image-validation-source-manifest.json>
python tools/build_spcc_real_image_validation_entries.py \
    <reviewed-real-image-validation-source-manifest.json> \
    <real-image-validation-manifest-entries.json> \
    --strict-production-review
```

Merge the generated `real_image_reference_sets` and `external_comparison_outputs` entries into the validation manifest.
Generated response, support-envelope, real-image, and external-comparison evidence can be merged in one validated pass:

```bash
python tools/merge_spcc_validation_manifest_entries.py \
    docs/dev/spcc/spcc-validation-data-manifest.json \
    <response-manifest-entries.json> \
    <support-envelope-manifest-entry.json> \
    <real-image-validation-manifest-entries.json> \
    <packaged-qa-manifest-entry.json> \
    --output <merged-validation-manifest.json> \
    --release-ready
```

After production catalog and response datasets are staged locally, write the final Hugging Face upload and hosted
verification handoff:

```bash
python tools/write_spcc_hf_upload_instructions.py \
    --catalog-dataset-dir <catalog-dataset-dir> \
    --response-dataset-dir <response-dataset-dir> \
    --response-source-manifest <reviewed-response-source-manifest.json> \
    --real-image-validation-source-manifest <reviewed-real-image-validation-source-manifest.json> \
    --packaged-qa-manifest-entry <packaged-qa-manifest-entry.json> \
    --output-markdown <spcc-hf-upload-instructions.md>
```

For the final production gate, run the aggregate release-readiness check so the validation manifest, catalog descriptors,
response descriptors, reviewed response source manifest, and reviewed real-image validation source manifest are checked
together:

```bash
python tools/check_spcc_release_readiness.py \
    --validation-manifest docs/dev/spcc/spcc-validation-data-manifest.json \
    --catalog-mirror-config <path-or-url-to-spcc_catalog_mirrors.json> \
    --response-repository-config <path-or-url-to-spcc_response_repositories.json> \
    --response-source-manifest <reviewed-response-source-manifest.json> \
    --real-image-validation-source-manifest <reviewed-real-image-validation-source-manifest.json> \
    --chunk-policy head
```

## Release Gate

Before a catalog pack becomes a recommended production input, confirm:

- Strict production catalog validation passed.
- Dataset verification passed for the hosted or packaged artifact.
- Rights/provenance review is approved for the deployment mode.
- Real-image validation ties the pack to expected SPCC metrics.
- Catalog requirements and support-envelope docs identify the intended users and limitations.
- App Catalogs & Data descriptors point to the verified manifest/revision.
- The previous recommended pack remains available or has a documented migration path.
